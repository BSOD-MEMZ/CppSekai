"""Extract sprite names / colours / text styles for every node of one prefab.

Usage: python prefab_style.py <prefabPathId> <out.tsv> [prefixI]

Walks the prefab's GameObjects[] list, reads each node's components through
AssetRipper's YAML view and keeps the bits that decide *how it looks*:
  CustomImage  -> spriteName + m_Color + m_Type + m_PreserveAspect
  CustomText   -> m_FontSize + m_Text + m_Color
  Image        -> m_Sprite / sprite name + m_Color
  CanvasGroup  -> m_Alpha
Requests run on a small thread pool; output is one TSV row per real component.
"""
import concurrent.futures
import re
import sys
import urllib.parse
import urllib.request

BASE = 'http://127.0.0.1:17654'
OP = urllib.request.build_opener(urllib.request.ProxyHandler({}))
GO_PREFIX = ''


def get(url):
    return OP.open(url, timeout=180).read().decode('utf-8', 'replace')


def rows(html):
    out = []
    for th, td in re.findall(r'<tr><th>(.*?)</th><td>(.*?)</td></tr>', html, re.S):
        label = re.sub(r'<[^>]+>', '', td).strip()
        m = re.search(r'"D":(\d+)', urllib.parse.unquote(td))
        out.append((th.strip(), label, int(m.group(1)) if m else None))
    return out


def yaml_url(d):
    return BASE + '/Assets/Yaml?Path=' + urllib.parse.quote(GO_PREFIX + ',"D":%d}' % d)


def view_url(d):
    return BASE + '/Assets/View?Path=' + urllib.parse.quote(GO_PREFIX + ',"D":%d}' % d)


def main():
    global GO_PREFIX
    prefab = int(sys.argv[1])
    out_path = sys.argv[2]

    page = get(BASE + '/Assets/View?Path=' +
               urllib.parse.quote('{"C":{"B":{"P":[0]},"I":0},"D":%d}' % prefab))
    m = re.search(r'<tr><th>GameObjects\[\]</th><td><a href="([^"]+)"', page)
    GO_PREFIX = urllib.parse.unquote(m.group(1)).split('/Assets/View?Path=')[1].rsplit(',"D":', 1)[0]
    gos = [(label, d) for th, label, d in rows(page) if th == 'GameObjects[]' and d]
    print('gameobjects:', len(gos), flush=True)

    def one(item):
        label, d = item
        try:
            view = get(view_url(d))
        except Exception as exc:
            return [(label, '-', 'ERR', str(exc))]
        recs = []
        comps = [(cn, cd) for th, cn, cd in rows(view) if th == 'm_Components[].m_Component' and cd]
        for cname, cd in comps:
            if cname not in ('MonoBehaviour', 'CanvasGroup'):
                continue
            try:
                y = get(yaml_url(cd))
            except Exception:
                continue
            spr = re.search(r'spriteName: *(\S*)', y)
            col = re.search(r'm_Color: \{r: ([\d.]+), g: ([\d.]+), b: ([\d.]+), a: ([\d.]+)\}', y)
            size = re.search(r'm_FontSize: *(\d+)', y)
            text = re.search(r'm_Text: *(.*)', y)
            alpha = re.search(r'm_Alpha: *([\d.]+)', y)
            style = re.search(r'm_FontStyle: *(\d+)', y)
            if not (spr or col or size or alpha):
                continue
            recs.append((
                label, cname,
                'spr=%s' % spr.group(1) if spr else '',
                'rgba=%s' % ','.join('%d' % round(float(col.group(i)) * 255) for i in (1, 2, 3, 4)) if col else '',
                'fs=%s' % size.group(1) if size else '',
                'style=%s' % style.group(1) if style else '',
                'text=%s' % (text.group(1).strip()[:40] if text else ''),
                'alpha=%s' % alpha.group(1) if alpha else '',
            ))
        return recs

    with concurrent.futures.ThreadPoolExecutor(max_workers=10) as ex:
        allrecs = []
        for recs in ex.map(one, gos):
            allrecs.extend(recs)

    with open(out_path, 'w', encoding='utf-8') as fh:
        fh.write('node\tcomp\tsprite\tcolor\tfont\tstyle\ttext\talpha\n')
        for r in allrecs:
            fh.write('\t'.join(r) + '\n')
    print('rows:', len(allrecs), '->', out_path)


if __name__ == '__main__':
    main()
