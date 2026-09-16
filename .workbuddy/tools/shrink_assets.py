#!/usr/bin/env python3
"""Shrink the handful of oversized CppSekai textures that are safe to shrink.

Only textures whose *consumer* derives its UVs / layout from the texture's own
dimensions are listed here. Anything used as a sprite atlas with hard-coded
sprite rectangles (notes*.png, effect.png, longNoteLine*, touchLine*,
assets/mmw/overlay/**) is deliberately left alone - resizing those shifts every
sprite.

Originals are copied to .workbuddy/backup/assets-<date>/ first.

Usage:
  python shrink_assets.py            # dry run: print what would happen
  python shrink_assets.py --apply
"""
import os
import shutil
import sys
from PIL import Image

sys.stdout.reconfigure(encoding='utf-8')

BACKUP = os.path.join('.workbuddy', 'backup', 'assets-20260916')

# path -> (target_width or None, target_height or None, note)
#   "crop" = keep the top rows only, no resampling (exact pixels)
TARGETS = [
    ('assets/mmw/stage.png', 2048, 1176,
     'only the top 2048x1176 rows are sampled by the stage quad'),
    ('assets/mmw/background_overlay.png', 1024, 1024,
     'full-texture UV; the room plate is washed out anyway'),
    ('assets/select/img_smartphone.png', 517, 971,
     'drawn into a rect, inner layout is fractional'),
]


def main():
    apply = '--apply' in sys.argv
    saved = 0
    for path, tw, th, why in TARGETS:
        p = path.replace('/', os.sep)
        if not os.path.exists(p):
            print('MISSING  %s' % path)
            continue
        before = os.path.getsize(p)
        im = Image.open(p)
        ow, oh = im.size
        crop = path.endswith('stage.png')
        if crop:
            new = im.crop((0, 0, tw, th))
        else:
            new = im.resize((tw, th), Image.LANCZOS)
        tmp = p + '.new'
        new.save(tmp, 'PNG', optimize=True)
        after = os.path.getsize(tmp)
        dec_before = ow * oh * 4
        dec_after = tw * th * 4
        print('%-42s %dx%d -> %dx%d  decoded %.2f -> %.2f MB  disk %.0f -> %.0f KB  (%s)'
              % (path, ow, oh, tw, th, dec_before / 1e6, dec_after / 1e6,
                 before / 1024, after / 1024, why))
        if apply:
            os.makedirs(BACKUP, exist_ok=True)
            backup_path = os.path.join(BACKUP, os.path.basename(p))
            if not os.path.exists(backup_path):
                shutil.copy2(p, backup_path)
            os.replace(tmp, p)
        else:
            os.remove(tmp)
        saved += dec_before - dec_after
    print('total decoded saving: %.1f MB' % (saved / 1e6))
    if not apply:
        print('(dry run - pass --apply to write)')


if __name__ == '__main__':
    main()
