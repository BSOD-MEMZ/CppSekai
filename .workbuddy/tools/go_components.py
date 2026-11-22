"""Dump the component YAML of one GameObject inside a prefab hierarchy.

Usage: python go_components.py <gameObjectPathId> [goPrefixI]

goPrefixI defaults to the ScreenLayerHeader hierarchy (3242). It resolves the
GameObjects[] link prefix from the hierarchy page, then prints every component's
class and YAML for the requested GameObject, plus its m_IsActive flag.
"""
import re
import sys
import urllib.parse
import urllib.request

BASE = 'http://127.0.0.1:17654'
OP = urllib.request.build_opener(urllib.request.ProxyHandler({}))

GO_PREFIX = ''


def get(url):
    return OP.open(url, timeout=120).read().decode('utf-8', 'replace')


def rows(html):
    out = []
    for th, td in re.findall(r'<tr><th>(.*?)</th><td>(.*?)</td></tr>', html, re.S):
        label = re.sub(r'<[^>]+>', '', td).strip()
        m = re.search(r'"D":(\d+)', urllib.parse.unquote(td))
        out.append((th.strip(), label, int(m.group(1)) if m else None))
    return out


def main():
    global GO_PREFIX
    d = int(sys.argv[1])
    hi = int(sys.argv[2]) if len(sys.argv) > 2 else 3242
    page = get(BASE + '/Assets/View?Path=' +
               urllib.parse.quote('{"C":{"B":{"P":[0]},"I":0},"D":%d}' % hi))
    m = re.search(r'<tr><th>GameObjects\[\]</th><td><a href="([^"]+)"', page)
    GO_PREFIX = urllib.parse.unquote(m.group(1)).split('/Assets/View?Path=')[1].rsplit(',"D":', 1)[0]

    view = get(BASE + '/Assets/View?Path=' + urllib.parse.quote(GO_PREFIX + ',"D":%d}' % d))
    print('=== GameObject', d, '===')
    for th, label, cd in rows(view):
        if th in ('m_Name', 'm_IsActive', 'm_TagString', 'm_Layer'):
            print('%-14s %s' % (th, label))

    comps = [(th, label, cd) for th, label, cd in rows(view)
             if th == 'm_Components[].m_Component']
    for th, label, cd in comps:
        print('\n--- component %s (%s) ---' % (cd, label))
        if cd is None:
            continue
        ty = get(BASE + '/Assets/Yaml?Path=' + urllib.parse.quote(GO_PREFIX + ',"D":%d}' % cd))
        body = ty[ty.find('--- !u!'):]
        print(body[:4000])


if __name__ == '__main__':
    main()
