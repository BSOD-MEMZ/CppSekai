#!/usr/bin/env python3
"""Shrink the CppSekai textures whose consumer does *not* care about the file's
pixel size, and losslessly re-encode the rest.

Two passes, and the distinction between them is the whole point:

  1. RESIZE  - only for files whose drawn rect is given in virtual 1920x1080
               units by the C++ side (game/Hud.cpp, game/Result.cpp, ...) and
               which are sampled from (0,0)-(1,1) or from *fractional* UVs. The
               renderer never derives a layout from the texture's pixel size for
               these, so baking them down is free. They are still drawn through
               the texture's own aspect ratio, hence aspect-preserving resize.

  2. LOSSLESS - everything else the game actually loads: re-encode with
               `optimize=True` (and drop a fully-opaque alpha channel). Pixels
               come out bit-identical, so the rendering cannot change.

NEVER touched (sprite atlases): notes*.png, effect.png, longNoteLine*.png,
touchLine*.png. Their sprite rectangles are *pixel coordinates* baked into
core/native/generated/generated_resources.h and the renderer normalises UVs
against the loaded size, so resizing one shifts every sprite. They are also
skipped by the lossless pass - not because it would break, but because the
request was to leave them alone.

The "which files does the game load" list comes from asset_audit.py (the paths
the loaders name), not from a filename guess.

Originals are copied to .workbuddy/backup/assets-<date>/ before being replaced.

Usage (from the repository root):
  python .workbuddy/tools/shrink_assets.py            # dry run
  python .workbuddy/tools/shrink_assets.py --apply
"""
import fnmatch
import importlib.util
import io
import os
import shutil
import sys
from datetime import date

from PIL import Image

sys.stdout.reconfigure(encoding='utf-8')

BACKUP = os.path.join('.workbuddy', 'backup', 'assets-' + date.today().strftime('%Y%m%d'))

# (glob, target longer side, why) - aspect preserved, LANCZOS.
RESIZE = [
    ('assets/mmw/overlay_opt/life/v3/digit/*.png', 128,
     'drawn at 34px (37px shadow) on the 1920x1080 canvas; the file was 333x444, '
     'i.e. a 13x oversample - 128 still gives 3.8x at 1080p and 1.9x at 4K'),
]

# Sprite atlases / images the request said to leave alone (or that would break).
NEVER = [
    '*/notes*.png', '*/effect.png', '*/longNoteLine*.png', '*/touchLine*.png',
]

TEXTS = ('.png',)


def load_audit():
    spec = importlib.util.spec_from_file_location(
        'asset_audit', os.path.join('.workbuddy', 'tools', 'asset_audit.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def matches(path, patterns):
    return any(fnmatch.fnmatch(path, p) for p in patterns)


def has_real_alpha(im):
    """True when the alpha channel actually varies (so it must be kept)."""
    if im.mode not in ('RGBA', 'LA'):
        return False
    lo, hi = im.getchannel('A').getextrema()
    return lo < 255


def main():
    apply = '--apply' in sys.argv
    audit = load_audit()

    # Resolve the loader's paths to the files that exist on disk (overlay/ is
    # the fallback for overlay_opt/, so a path may map onto the other tree).
    loaded = set()
    for path, _size in audit.walk_assets('assets'):
        if path in audit.USED:
            loaded.add(path)

    files = sorted(f for f in loaded if f.lower().endswith(TEXTS) and os.path.exists(f))
    skipped = [f for f in files if matches(f, NEVER)]
    files = [f for f in files if f not in skipped]

    before_total = sum(os.path.getsize(f) for f in files)
    after_total = 0
    rows = []

    for path in files:
        rule = next(((g, n, why) for g, n, why in RESIZE if fnmatch.fnmatch(path, g)), None)
        im = Image.open(path)
        ow, oh = im.size
        nw, nh = ow, oh
        if rule is not None:
            target = rule[1]
            if max(ow, oh) > target:
                k = target / max(ow, oh)
                nw, nh = max(1, int(round(ow * k))), max(1, int(round(oh * k)))
        im = im.convert('RGBA') if im.mode not in ('RGB', 'RGBA') else im
        if nw != ow or nh != oh:
            im = im.resize((nw, nh), Image.LANCZOS)

        buf = io.BytesIO()
        if im.mode == 'RGBA' and not has_real_alpha(im):
            # Fully opaque: the alpha channel is dead weight in the file.
            im.convert('RGB').save(buf, 'PNG', optimize=True)
        else:
            im.save(buf, 'PNG', optimize=True)
        new = buf.getvalue()

        before = os.path.getsize(path)
        after_total += len(new)
        rows.append((before - len(new), before, len(new), path, ow, oh, nw, nh, rule))

        if apply and len(new) < before:
            os.makedirs(BACKUP, exist_ok=True)
            backup = os.path.join(BACKUP, path.replace('/', '__'))
            if not os.path.exists(backup):
                shutil.copy2(path, backup)
            with open(path, 'wb') as f:
                f.write(new)

    rows.sort(reverse=True)
    resized = [r for r in rows if r[8] is not None]
    lossless = [r for r in rows if r[8] is None and r[0] > 0]

    print('%d 个文件  %.2f MB -> %.2f MB  (省 %.2f MB / %.0f%%)'
          % (len(files), before_total / 1e6, after_total / 1e6,
             (before_total - after_total) / 1e6, 100 * (before_total - after_total) / before_total))
    print()
    print('降尺寸 %d 个：' % len(resized))
    for d, b, a, p, ow, oh, nw, nh, rule in resized:
        print('  %8.1f -> %7.1f KB  %sx%s -> %sx%s  %s' % (b / 1024, a / 1024, ow, oh, nw, nh, p))
    print()
    print('无损重压省得最多的 12 个：')
    for d, b, a, p, ow, oh, nw, nh, rule in lossless[:12]:
        print('  %8.1f -> %7.1f KB  (%+4.1f%%)  %s' % (b / 1024, a / 1024, -100 * d / b, p))
    print()
    print('跳过（精灵图集，不动）：%d 个' % len(skipped))
    for p in skipped:
        print('   ', p)
    if not apply:
        print()
        print('(dry run - pass --apply to write)')


if __name__ == '__main__':
    main()
