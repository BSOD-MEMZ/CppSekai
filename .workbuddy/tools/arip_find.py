"""Search AssetRipper and print the exact asset handles (raw Path= query) per hit.

Usage: python arip_find.py <query> [classFilter]
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
    html = OPENER.open(url, timeout=180).read().decode('utf-8', 'replace')
    rows = re.findall(r'<tr data-class="([^"]+)">(.*?)</tr>', html, re.S)
    print('hits: %d' % len(rows))
    for cls, body in rows:
        if want and cls != want:
            continue
        m = re.search(r'href="([^"]*Assets/(?:View|Yaml)\?Path=[^"]+)"', body)
        name = re.search(r'p-0 m-0">([^<]*)</a>', body)
        if not m:
            continue
        print('%-24s %-46s %s' % (cls, name.group(1) if name else '?',
                                  urllib.parse.unquote(m.group(1))))


if __name__ == '__main__':
    main()
