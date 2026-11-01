"""List AssetRipper search hits as `PathID,Class,Name,Collection`.

Usage: python arip_search.py <query> [classFilter]

Handy when hunting for a prefab by name: only `PrefabHierarchyObject` rows are
the actual prefab hierarchies (see RE.md 5).
"""
import re
import sys
import urllib.parse
import urllib.request

BASE = 'http://127.0.0.1:17654'
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def main():
    q = sys.argv[1]
    want = sys.argv[2] if len(sys.argv) > 2 else ''
    url = BASE + '/Search/View?q=' + urllib.parse.quote(q)
    html = OPENER.open(url, timeout=120).read().decode('utf-8', 'replace')
    rows = re.findall(r'<tr data-class="([^"]+)">(.*?)</tr>', html, re.S)
    print('hits: %d' % len(rows))
    for cls, body in rows:
        if want and cls != want:
            continue
        pid = re.search(r'<td>(\d+)</td>', body)
        name = re.search(r'p-0 m-0">([^<]*)</a>', body)
        if not (pid and name):
            continue
        print('%-22s %-8s %s' % (cls, pid.group(1), name.group(1)))


if __name__ == '__main__':
    main()
