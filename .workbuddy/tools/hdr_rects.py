"""Resolve absolute 1920x1080 rects for an ms_geom.py dump.

Usage: python hdr_rects.py hdr.json [nameSubstring ...]

ms_geom.py writes {gameObjectPathId: {name, rt, parent, anchorMin, anchorMax,
anchoredPosition, sizeDelta, pivot, scale}}. `parent` is the parent *GameObject*
path id (0 = top level). AnchoredPosition is pivot-relative (SELECT-UI.md), so:

  refX = lerp(anchorMin.x, anchorMax.x, pivot.x)
  refY = 1 - lerp(anchorMin.y, anchorMax.y, pivot.y)      # y down
  w    = (anchorMax.x-anchorMin.x)*parentW + sizeDelta.x
  h    = (anchorMax.y-anchorMin.y)*parentH + sizeDelta.y
  left = parentLeft + refX*parentW + anchoredPosition.x - pivot.x*w
  top  = parentTop  + refY*parentH - anchoredPosition.y - (1-pivot.y)*h
"""
import json
import sys

CANVAS = (1920.0, 1080.0)


def main():
    path = sys.argv[1]
    wanted = sys.argv[2:]
    data = json.load(open(path, encoding='utf-8'))

    by_parent = {}
    for go_id, g in data.items():
        by_parent.setdefault(int(g.get('parent') or 0), []).append((int(go_id), g))

    result = {}

    def walk(go_id, g, pl, pt, pw, ph, depth):
        an = g.get('anchorMin') or [0.5, 0.5]
        ax = g.get('anchorMax') or [0.5, 0.5]
        ap = g.get('anchoredPosition') or [0.0, 0.0]
        sd = g.get('sizeDelta') or [0.0, 0.0]
        pv = g.get('pivot') or [0.5, 0.5]
        sc = g.get('scale') or [1.0, 1.0, 1.0]
        w = (ax[0] - an[0]) * pw + sd[0]
        h = (ax[1] - an[1]) * ph + sd[1]
        refx = an[0] + (ax[0] - an[0]) * pv[0]
        refy = 1.0 - (an[1] + (ax[1] - an[1]) * pv[1])
        left = pl + refx * pw + ap[0] - pv[0] * w
        top = pt + refy * ph - ap[1] - (1.0 - pv[1]) * h
        result[go_id] = dict(name=g.get('name'), left=left, top=top, w=w, h=h,
                             depth=depth, pivot=pv, scale=sc)
        name = g.get('name', '?')
        if not wanted or any(s.lower() in name.lower() for s in wanted):
            print('%-34s x %7.1f..%7.1f  y %7.1f..%7.1f   (%6.1f x %6.1f)  sc=(%.2f,%.2f) d=%d'
                  % (name[:34], left, left + w, top, top + h, w, h, sc[0], sc[1], depth))
        for cid, c in by_parent.get(go_id, []):
            walk(cid, c, left, top, w * abs(sc[0]), h * abs(sc[1]), depth + 1)

    for go_id, g in by_parent.get(0, []):
        walk(go_id, g, 0.0, 0.0, CANVAS[0], CANVAS[1], 0)

    with open(path.replace('.json', '.rects.json'), 'w', encoding='utf-8') as fh:
        json.dump(result, fh, ensure_ascii=False, indent=1)


if __name__ == '__main__':
    main()
