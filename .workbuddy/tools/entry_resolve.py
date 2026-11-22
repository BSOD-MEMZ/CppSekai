"""Resolve EntryScreenLayersOutGame's external ScreenLayerData assets to their names.

The list stores PPtrs whose m_FileID indexes a per-asset bundle table:
  globalgamemanagers.assets, then one hash-named bundle per ScreenLayerData.
For each of them we open its collection page and read the asset names.
"""
import concurrent.futures
import re
import sys
import urllib.parse
import urllib.request

BASE = 'http://127.0.0.1:17654'
OP = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def get(path):
    return OP.open(BASE + path, timeout=180).read().decode('utf-8', 'replace')


def q(obj):
    return urllib.parse.quote(obj)


def table(html):
    out = []
    for r in re.findall(r'<tr[^>]*>(.*?)</tr>', html, re.S):
        cells = [re.sub(r'<[^>]+>', '', c).strip()
                 for c in re.findall(r'<t[dh][^>]*>(.*?)</t[dh]>', r, re.S)]
        if cells:
            out.append(cells)
    return out


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '3085'

    bundles = get('/Bundles/View?Path=' + q('{"P":[]}'))
    hash_to_i = {}
    for href, text in re.findall(r'<a[^>]*href="([^"]+)"[^>]*>([^<]*)</a>', bundles):
        m = re.search(r'"I":(\d+)', urllib.parse.unquote(href))
        if m and re.fullmatch(r'[0-9a-f]{32}', text.strip()):
            hash_to_i[text.strip()] = int(m.group(1))
    print('bundles indexed:', len(hash_to_i), flush=True)

    coll = get('/Collections/View?Path=' + q('{"B":{"P":[]},"I":%s}' % root))
    rows = table(coll)
    files = {}
    started = False
    for cells in rows:
        if cells and cells[0] == 'File ID':
            started = True
            continue
        if started and len(cells) >= 2 and cells[0].isdigit():
            files[int(cells[0])] = cells[1]

    print('file refs:', len(files), flush=True)

    def probe(item):
        fid, name = item
        idx = hash_to_i.get(name)
        if idx is None:
            return fid, name, '(no bundle index)', []
        try:
            html = get('/Collections/View?Path=' + q('{"B":{"P":[]},"I":%d}' % idx))
        except Exception as exc:
            return fid, name, 'ERR %s' % exc, []
        assets = []
        st = False
        for cells in table(html):
            if cells and cells[0] == 'Path ID':
                st = True
                continue
            if st and len(cells) >= 3 and cells[0].isdigit():
                assets.append((cells[0], cells[1], cells[2]))
        return fid, name, idx, assets

    items = [(fid, name) for fid, name in files.items() if fid != 1]
    with concurrent.futures.ThreadPoolExecutor(max_workers=12) as ex:
        for fid, name, idx, assets in ex.map(probe, items):
            label = ', '.join('%s:%s(%s)' % a for a in assets[:3])
            print('%3d %s -> %s   %s' % (fid, name[:12], idx, label), flush=True)


if __name__ == '__main__':
    main()
