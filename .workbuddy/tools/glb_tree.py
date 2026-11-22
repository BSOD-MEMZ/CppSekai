import json, struct, sys

def load_glb(p):
    d = open(p, 'rb').read()
    assert d[:4] == b'glTF', d[:4]
    ver, total = struct.unpack('<II', d[4:12])
    off = 12
    js = None
    while off < len(d):
        ln, ty = struct.unpack('<II', d[off:off+8])
        chunk = d[off+8:off+8+ln]
        if ty == 0x4E4F534A:
            js = json.loads(chunk.decode('utf-8'))
        off += 8 + ln
    return js


def main(p, only_scene=True):
    g = load_glb(p)
    nodes = g.get('nodes', [])
    print('nodes', len(nodes), 'meshes', len(g.get('meshes', [])),
          'materials', len(g.get('materials', [])))
    parents = {}
    for i, n in enumerate(nodes):
        for c in n.get('children', []) or []:
            parents[c] = i
    roots = [i for i in range(len(nodes)) if i not in parents]
    print('roots', roots)

    def fmt(n, depth):
        name = n.get('name', '?')
        t = n.get('translation')
        s = n.get('scale')
        r = n.get('rotation')
        mesh = n.get('mesh')
        extra = []
        if t and any(abs(x) > 1e-6 for x in t):
            extra.append('T=(%.2f, %.2f, %.2f)' % tuple(t))
        if s and any(abs(x - 1) > 1e-6 for x in s):
            extra.append('S=(%.3f, %.3f, %.3f)' % tuple(s))
        if r and (abs(r[3] - 1) > 1e-6):
            extra.append('R=' + str([round(x, 4) for x in r]))
        if mesh is not None:
            m = g['meshes'][mesh]
            extra.append('mesh:%s prims=%d' % (m.get('name'), len(m.get('primitives', []))))
        if n.get('extras'):
            extra.append('extras=' + json.dumps(n['extras'], ensure_ascii=False)[:200])
        print('  ' * depth + name + ('   ' + ' '.join(extra) if extra else ''))
        for c in n.get('children', []) or []:
            fmt(nodes[c], depth + 1)

    for r in roots:
        fmt(nodes[r], 0)


if __name__ == '__main__':
    main(sys.argv[1])
