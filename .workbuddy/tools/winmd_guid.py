#!/usr/bin/env python3
# Extract GuidAttribute -> TypeDef name mapping from a .winmd (ECMA-335).
import struct, sys

path = sys.argv[1]
data = open(path, "rb").read()

pe_off = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe_off + 6)[0]
opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
opt_off = pe_off + 24
sections = []
for i in range(nsec):
    o = opt_off + opt_size + i * 40
    name = data[o:o+8].rstrip(b"\0")
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, o + 8)
    sections.append((name, vaddr, vsize, raddr, rsize))

def rva_to_off(rva):
    for name, vaddr, vsize, raddr, rsize in sections:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return raddr + (rva - vaddr)
    raise ValueError("rva")

magic = struct.unpack_from("<H", data, opt_off)[0]
dd_off = opt_off + (96 if magic == 0x10B else 112)
cli_rva = struct.unpack_from("<I", data, dd_off + 14 * 8)[0]
md = rva_to_off(struct.unpack_from("<I", data, rva_to_off(cli_rva) + 8)[0])
vlen = struct.unpack_from("<I", data, md + 12)[0]
p = md + 16 + vlen + 2
nstreams = struct.unpack_from("<H", data, p)[0]
p += 2
streams = {}
for _ in range(nstreams):
    off, size = struct.unpack_from("<II", data, p)
    p += 8
    e = data.index(b"\0", p)
    streams[data[p:e].decode()] = (md + off, size)
    p = (e + 1 + 3) & ~3

tbl = streams["#~"][0]
heap_sizes = data[tbl + 6]
valid = struct.unpack_from("<Q", data, tbl + 8)[0]
p = tbl + 24
rows = []
for i in range(64):
    if valid & (1 << i):
        rows.append(struct.unpack_from("<I", data, p)[0]); p += 4
    else:
        rows.append(0)

def rc(t): return rows[t]
def tsize(t): return 4 if rc(t) >= (1 << 16) else 2
hs = heap_sizes
S = 4 if hs & 1 else 2
B = 4 if hs & 4 else 2
G = 4 if hs & 2 else 2

def csize(bits, tabs):
    m = max(rc(t) for t in tabs) if tabs else 0
    return 4 if m >= (1 << (16 - bits)) else 2

TDR = [0x02, 0x01, 0x1B]
MRP = [0x02, 0x01, 0x1A, 0x06, 0x1B]
HCA = list(range(0, 22))
CAT = [0x06, 0x0A]
HCO = [0x04, 0x08, 0x17]

LAYOUT_RAW = {
    0x00: [("u16"), ("str"), ("guid"), ("guid"), ("guid")],
    0x01: [("c", TDR, 2), ("str"), ("str")],
    0x02: [("u32"), ("str"), ("str"), ("c", TDR, 2), ("t", 0x04), ("t", 0x06)],
    0x04: [("u16"), ("str"), ("blob")],
    0x06: [("u32"), ("u16"), ("u16"), ("str"), ("blob"), ("t", 0x08)],
    0x08: [("u16"), ("u16"), ("str")],
    0x09: [("t", 0x02), ("c", TDR, 2)],
    0x0A: [("c", MRP, 3), ("str"), ("blob")],
    0x0B: [("u8"), ("u8"), ("c", HCO, 2), ("blob")],
    0x0C: [("c", HCA, 5), ("c", CAT, 3), ("blob")],
}
LAYOUT = {k: [(c,) if isinstance(c, str) else c for c in v] for k, v in LAYOUT_RAW.items()}

def colsize(c):
    if c[0] == "u16": return 2
    if c[0] == "u32": return 4
    if c[0] == "u8": return 1
    if c[0] == "str": return S
    if c[0] == "blob": return B
    if c[0] == "guid": return G
    if c[0] == "t": return tsize(c[1])
    if c[0] == "c": return csize(c[2], c[1])
    raise Exception(c)

offs = {}
p2 = p
for t in range(0x0D):   # only tables up to CustomAttribute are needed
    if rc(t) == 0: continue
    offs[t] = p2
    rs = sum(colsize(c) for c in LAYOUT.get(t, []))
    if rs == 0 and rc(t): raise Exception("unknown layout for table %02x" % t)
    p2 += rs * rc(t)

stroff = streams["#Strings"][0]
bloboff = streams["#Blob"][0]
def getstr(i):
    o = stroff + i
    return data[o:data.index(b"\0", o)].decode("utf-8", "replace")

def blob(i):
    o = bloboff + i
    n, sz = 0, 0
    while True:
        b = data[o]
        o += 1; sz += 1
        if b & 0x80 == 0: n = b; break
        if b & 0xC0 == 0x80: n = ((b & 0x3F) << 8) | data[o]; o += 1; sz += 1; break
        if b & 0xE0 == 0xC0:
            n = ((b & 0x1F) << 24) | (data[o] << 16) | (data[o+1] << 8) | data[o+2]; o += 3; sz += 3; break
        raise Exception("bad blob len")
    return data[o:o+n], sz + n

def row(t, rid):
    out, o, tags = [], offs[t] + sum(colsize(c) for c in LAYOUT[t]) * (rid - 1), []
    for c in LAYOUT[t]:
        s = colsize(c)
        v = int.from_bytes(data[o:o+s], "little"); o += s
        if c[0] == "str": v = getstr(v)
        elif c[0] == "c": tags.append(v & ((1 << c[2]) - 1)); v >>= c[2]
        out.append(v)
    return out, tags

typename = {}
for r in range(1, rc(0x02) + 1):
    f, n, ns = row(0x02, r)[0][:3]
    typename[r] = (ns + "." + n if ns else n)
typeref = {}
for r in range(1, rc(0x01) + 1):
    v, tags = row(0x01, r)
    typeref[r] = v[2] + "." + v[1] if v[2] else v[1]

# MemberRef rows whose ctor belongs to GuidAttribute
guid_ctor = set()
for r in range(1, rc(0x0A) + 1):
    v, tags = row(0x0A, r)
    if v[1] != ".ctor": continue
    tag = tags[0]
    if tag == 1 and typeref.get(v[0], "").endswith("GuidAttribute"):
        guid_ctor.add(r)

print("guid ctor memberrefs:", len(guid_ctor))
found = {}
for r in range(1, rc(0x0C) + 1):
    v, tags = row(0x0C, r)
    if tags[1] != 3 or v[1] not in guid_ctor: continue   # 3 = MemberRef
    if tags[0] != 3: continue   # Parent = TypeDef
    raw, _ = blob(v[2])
    if len(raw) >= 20 and raw[0] == 1 and raw[1] == 0:
        g = raw[2:18]
        a, b, c = struct.unpack_from("<IHH", g, 0)
        d = g[8:16]
        guid = "%08X-%04X-%04X-%04X-%02X%02X%02X%02X%02X%02X" % (
            a, b, c, struct.unpack_from(">H", d, 0)[0],
            d[2], d[3], d[4], d[5], d[6], d[7])
        found[typename.get(v[0], "?")] = guid

for k in sorted(found):
    if "Media" in k or "Foundation" in k:
        pass
for name in sys.argv[2:]:
    # `found` is keyed by "Namespace.Type"; accept a bare type name too (that
    # is how the interfaces are named in the C/C++ headers).
    hit = found.get(name)
    if hit is None:
        for k, v in found.items():
            if k == name or k.endswith("." + name):
                hit = v
                break
    print("%-70s %s" % (name, hit if hit is not None else "NOT FOUND"))
