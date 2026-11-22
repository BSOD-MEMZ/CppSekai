"""deep_dump.py JSON -> tree of rects + the fields that *drive* layout.

Usage: python deep_layout.py <deep.json> [--filter name1,name2] [--depth N]

Like deep_rects.py (same anchor/pivot math, same RectTransform-keyed tree) but
each line also carries the layout-relevant component fields, so a branch can be
read without going back to the YAML:

  LG   = Horizontal/GridLayoutGroup: spacing, childAlignment, childControl*,
         childForceExpand*, reverseArrangement, padding
  IMG  = Image: spriteName, m_Type (0 Simple / 1 Sliced), m_Color alpha
  TMP  = TextMeshProUGUI: fontSize, alignment
  MASK = Mask / RectMask2D: showMaskGraphic, padding, softness

The prefab stores stale anchoredPositions for layout-group children, so what
matters for those is the LG line + the child's own size, not its position.
"""
import json
import re
import sys

CANVAS = (1920.0, 1080.0)


def vec(y, name):
    m = re.search(r'%s:\s*\{([^}]*)\}' % name, y)
    if not m:
        return None
    d = dict((k, float(v)) for k, v in
             re.findall(r'(\w+):\s*(-?[\d.eE+-]+)', m.group(1)))
    return [d.get('x', 0.0), d.get('y', 0.0)]


def num(y, name, default=None):
    m = re.search(r'\b%s:\s*(-?[\d.eE+-]+)\s*$' % name, y, re.M)
    return float(m.group(1)) if m else default


def sval(y, name):
    m = re.search(r'\b%s:\s*(\S.*?)\s*$' % name, y, re.M)
    return m.group(1) if m else None


def describe(rec):
    bits = []
    for c in rec['components']:
        y = c['yaml']
        cls = c['class']
        if cls == 'MonoBehaviour':
            script = None
            m = re.search(r'm_Script:\s*\{m_FileID:\s*\d+,\s*m_PathID:\s*(\d+)', y)
            if m:
                script = m.group(1)
            if 'm_Spacing:' in y and 'm_ChildAlignment:' in y:
                pad = '?'
                if re.search(r'm_Padding:\s*\n', y):
                    m2 = re.search(r'm_Padding:\s*\n\s*m_Left:\s*(-?[\d.]+)\s*\n\s*m_Right:\s*(-?[\d.]+)\s*\n\s*m_Top:\s*(-?[\d.]+)\s*\n\s*m_Bottom:\s*(-?[\d.]+)', y)
                    if m2:
                        pad = 'L%s R%s T%s B%s' % m2.groups()
                    else:
                        pad = re.search(r'm_Padding:\s*\{(.*?)\}', y).group(1)
                bits.append('LG script=%s spacing=%s align=%s ccW=%s ccH=%s feW=%s feH=%s rev=%s pad=%s' % (
                    script, num(y, 'm_Spacing'), num(y, 'm_ChildAlignment'),
                    num(y, 'm_ChildControlWidth'), num(y, 'm_ChildControlHeight'),
                    num(y, 'm_ChildForceExpandWidth'), num(y, 'm_ChildForceExpandHeight'),
                    num(y, 'm_ReverseArrangement'), pad))
                continue
            if 'm_Spacing:' in y:  # GridLayoutGroup
                bits.append('GRID script=%s spacing=%s cell=%s align=%s' % (
                    script, sval(y, 'm_Spacing'), sval(y, 'm_CellSize'), num(y, 'm_ChildAlignment')))
                continue
            if sval(y, 'spriteName') is not None:
                col = re.search(r'm_Color:\s*\{([^}]*)\}', y)
                a = ''
                if col:
                    mm = re.search(r'a:\s*([\d.eE+-]+)', col.group(1))
                    if mm:
                        a = ' a=%s' % mm.group(1)
                bits.append('IMG %s type=%s%s' % (sval(y, 'spriteName'), num(y, 'm_Type'), a))
                continue
            if 'm_fontSize:' in y:
                bits.append('TMP size=%s align=%s text=%r' % (
                    num(y, 'm_fontSize'), num(y, 'm_textAlignment'), sval(y, 'm_text')))
                continue
            if 'm_ShowMaskGraphic:' in y:
                bits.append('MASK script=%s show=%s' % (script, num(y, 'm_ShowMaskGraphic')))
                continue
            if 'm_Softness:' in y:
                bits.append('RECTMASK script=%s soft=%s pad=%s' % (
                    script, sval(y, 'm_Softness'), sval(y, 'm_Padding')))
                continue
            if script:
                bits.append('MB script=%s' % script)
    return ' | '.join(bits)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    data = json.load(open(args[0], encoding='utf-8'))
    filt = []
    maxdepth = 99
    for i, a in enumerate(sys.argv):
        if a == '--filter':
            filt = [s.lower() for s in sys.argv[i + 1].split(',')]
        if a == '--depth':
            maxdepth = int(sys.argv[i + 1])

    nodes = data['nodes']
    rects = {}
    for go_pid, rec in nodes.items():
        for c in rec['components']:
            if c['class'] not in ('RectTransform', 'Transform'):
                continue
            y = c['yaml']
            fa = re.search(r'm_Father:\s*\{m_FileID:\s*\d+,\s*m_PathID:\s*(\d+)', y)
            rects[c['pid']] = {
                'rt': c['pid'], 'go': int(go_pid), 'name': rec.get('name'),
                'anchorMin': vec(y, 'm_AnchorMin') or [0.5, 0.5],
                'anchorMax': vec(y, 'm_AnchorMax') or [0.5, 0.5],
                'pos': vec(y, 'm_AnchoredPosition') or [0.0, 0.0],
                'size': vec(y, 'm_SizeDelta') or [0.0, 0.0],
                'pivot': vec(y, 'm_Pivot') or [0.5, 0.5],
                'scale': vec(y, 'm_LocalScale') or [1.0, 1.0],
                'father': int(fa.group(1)) if fa else 0,
                'info': describe(rec),
            }

    children, roots = {}, []
    for rt, r in rects.items():
        (children.setdefault(r['father'], []) if r['father'] in rects else roots).append(rt)

    out = []

    def walk(rt, pl, pt, pw, ph, depth):
        r = rects[rt]
        an, ax, ap, sd, pv, sc = (r['anchorMin'], r['anchorMax'], r['pos'],
                                  r['size'], r['pivot'], r['scale'])
        w = (ax[0] - an[0]) * pw + sd[0]
        h = (ax[1] - an[1]) * ph + sd[1]
        refx = an[0] + (ax[0] - an[0]) * pv[0]
        refy = 1.0 - (an[1] + (ax[1] - an[1]) * pv[1])
        left = pl + refx * pw + ap[0] - pv[0] * w
        top = pt + refy * ph - ap[1] - (1.0 - pv[1]) * h
        name = r['name'] or '?'
        if depth <= maxdepth:
            out.append('%s%-28s x %7.1f..%7.1f y %7.1f..%7.1f (%6.1f x %6.1f) sc=(%.2f,%.2f) %s' % (
                '  ' * depth, name[:28], left, left + w, top, top + h, w, h,
                sc[0], sc[1], r['info']))
        for c in children.get(rt, []):
            walk(c, left, top, w * abs(sc[0]), h * abs(sc[1]), depth + 1)

    for rt in roots:
        walk(rt, 0.0, 0.0, CANVAS[0], CANVAS[1], 0)

    text = '\n'.join(out)
    if filt:
        text = '\n'.join(l for l in text.splitlines()
                         if any(f in l.lower() for f in filt))
    print(text)


if __name__ == '__main__':
    main()
