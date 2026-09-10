"""Report which parts of effect.png the embedded particles actually use.

The native core keeps the original Unity particle systems as JSON strings
(`EmbeddedEffect{ "name", "<json>" }` in core/native/generated/
generated_resources.h). Every system - including sub-emitters under "children" -
carries a `textureSheetAnimation` block, and all of them sample the single
effect.png atlas. The frame arithmetic is verified against the renderer:

    EffectView.cpp:  frame = splitX * splitY * startFrame + splitX * splitY * frameOverTime
    Renderer.h:      row = frame / splitX,  col = frame % splitX   (row-major, top-left)

so a reference is fully described by (numTilesX, numTilesY, frame).

Usage (from the repo root):
    python .workbuddy/tools/effect_sheet_usage.py [--dump out.png]

Output: how much of the atlas is referenced, how much of it is empty padding,
and how much *opaque* art is never sampled by any particle.
"""

import argparse
import json
import re
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "core/native/generated/generated_resources.h"
SHEET = ROOT / "assets/mmw/effect.png"

EFFECT_RE = re.compile(
    r'EmbeddedEffect\{\s*"(?P<name>[^"]+)"\s*,\s*"(?P<body>(?:\\.|[^"\\])*)"\s*\}',
    re.S,
)
ESCAPES = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\", "/": "/"}


def unescape(text):
    """Undo the C string escaping the generator applied to the JSON payload."""
    out = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == "\\" and i + 1 < len(text):
            out.append(ESCAPES.get(text[i + 1], text[i + 1]))
            i += 2
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def walk(node, visit):
    if isinstance(node, dict):
        visit(node)
        for value in node.values():
            walk(value, visit)
    elif isinstance(node, list):
        for value in node:
            walk(value, visit)


def collect():
    """-> (refs, per_effect, effect_count, system_count, inert_curve, zero_life)

    refs: (nx, ny, tile) -> set(effect names that reference it)
    """
    src = HEADER.read_text(encoding="utf-8", errors="replace")
    effects = list(EFFECT_RE.finditer(src))
    if not effects:
        sys.exit("no EmbeddedEffect entries found in %s" % HEADER)

    refs = {}
    per_effect = {}
    systems = 0
    inert_curve = 0
    zero_life = 0

    for match in effects:
        name = match.group("name")
        try:
            doc = json.loads(unescape(match.group("body")))
        except json.JSONDecodeError as exc:
            print("  ! %s: unparsable JSON (%s)" % (name, exc))
            continue

        def visit(system, _name=name):
            nonlocal systems, inert_curve, zero_life
            anim = system.get("textureSheetAnimation")
            if not isinstance(anim, dict):
                return
            systems += 1
            nx = int(anim.get("numTilesX") or 0)
            ny = int(anim.get("numTilesY") or 0)
            if nx <= 0 or ny <= 0:
                return
            total = nx * ny

            start = anim.get("startFrame") or {}
            over = anim.get("frameOverTime") or {}

            # mode 3 = curve. Every curve in this data set is empty, and those
            # systems are zero-lifetime wrappers, so they render nothing.
            if start.get("mode") == 3:
                if not (start.get("curveMin") or start.get("curveMax")):
                    inert_curve += 1
                    return
                return

            tiles = {int(float(start.get("constant", 0.0) or 0.0) * total)}

            over_mode = over.get("mode", 0)
            if over_mode == 1:  # random between two constants
                for key in ("randomMin", "randomMax"):
                    tiles.add(int(float(over.get(key, 0.0) or 0.0) * total))
            elif over_mode == 0:
                tiles.add(int(float(over.get("constant", 0.0) or 0.0) * total))

            life = (system.get("startLifetime") or {}).get("constant", 0.0) or 0.0
            renderable = float(life) > 0.0
            if not renderable:
                zero_life += 1

            for t in tiles:
                t = max(0, min(t, total - 1))
                refs.setdefault((nx, ny, t), set()).add(name)
                if renderable:
                    per_effect.setdefault(name, set()).add((nx, ny, t))

        walk(doc, visit)

    return refs, per_effect, len(effects), systems, inert_curve, zero_life


def tile_rect(tile, nx, ny, w, h):
    col, row = tile % nx, tile // nx
    return (round(col * w / nx), round(row * h / ny),
            round((col + 1) * w / nx), round((row + 1) * h / ny))


def union_mask(refs, size):
    """1bpp-as-8bpp mask (0/255) of every referenced tile rectangle."""
    mask = Image.new("L", size, 0)
    draw = ImageDraw.Draw(mask)
    w, h = size
    for (nx, ny, tile) in refs:
        draw.rectangle(tile_rect(tile, nx, ny, w, h), fill=255)
    return mask


def count_set(mask):
    return sum(mask.histogram()[255:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", help="write a used/unused overlay PNG here")
    args = ap.parse_args()

    refs, per_effect, effect_count, systems, inert_curve, zero_life = collect()
    im = Image.open(SHEET).convert("RGBA")
    w, h = im.size
    total_px = w * h
    alpha = im.getchannel("A")
    opaque = alpha.point(lambda v: 255 if v > 0 else 0, mode="L")
    opaque_px = count_set(opaque)

    live = {key for key in refs if any(
        key in tiles for tiles in per_effect.values())}

    print("effect.png        : %dx%d = %s px" % (w, h, "{:,}".format(total_px)))
    print("embedded effects  : %d" % effect_count)
    print("particle systems  : %d with a texture sheet (%d inert curve wrappers,"
          " %d zero-lifetime)" % (systems, inert_curve, zero_life))
    print()
    print("fully transparent : %s px (%.1f%%)  <- padding, never visible"
          % ("{:,}".format(total_px - opaque_px), 100.0 * (total_px - opaque_px) / total_px))
    print("opaque            : %s px (%.1f%%)"
          % ("{:,}".format(opaque_px), 100.0 * opaque_px / total_px))
    print()

    for label, keys in (("EVER referenced (incl. zero-lifetime systems)", refs),
                        ("renderable only (startLifetime > 0)", live)):
        mask = union_mask(keys, (w, h))
        used_px = count_set(mask)
        visible = count_set(ImageChops.multiply(mask, opaque))
        grids = {}
        for (nx, ny, tile) in keys:
            grids.setdefault((nx, ny), set()).add(tile)
        print("== %s" % label)
        for grid in sorted(grids):
            print("   grid %2dx%-2d -> %3d tile(s)" % (grid[0], grid[1], len(grids[grid])))
        print("   referenced area          : %s px (%.1f%% of sheet)"
              % ("{:,}".format(used_px), 100.0 * used_px / total_px))
        print("   referenced & visible     : %s px (%.1f%% of opaque)"
              % ("{:,}".format(visible), 100.0 * visible / opaque_px))
        print("   OPAQUE but never sampled : %s px (%.1f%% of sheet, %.1f%% of opaque)"
              % ("{:,}".format(opaque_px - visible),
                 100.0 * (opaque_px - visible) / total_px,
                 100.0 * (opaque_px - visible) / opaque_px))
        print()

    if args.dump:
        mask = union_mask(refs, (w, h))
        overlay = im.copy()
        px = overlay.load()
        use = mask.load()
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                if use[x, y]:
                    px[x, y] = (r, g, b, 150) if a else (90, 90, 110, 255)
                else:
                    px[x, y] = (255, 0, 90, 255) if a else (26, 24, 36, 255)
        out = Path(args.dump)
        overlay.save(out)
        print("overlay -> %s" % out)
        print("   magenta = opaque art no particle ever samples")
        print("   grey    = empty padding inside a referenced tile")
        print("   normal  = referenced content")


if __name__ == "__main__":
    main()
