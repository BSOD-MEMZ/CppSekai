#!/usr/bin/env python3
# Minimal ECMA-335 metadata reader: dump interface method declaration order
# from a .winmd file (that order == WinRT ABI vtable order).
import struct, sys

path = sys.argv[1] if len(sys.argv) > 1 else r"C:\Windows\System32\WinMetadata\Windows.Media.winmd"
want = sys.argv[2:] if len(sys.argv) > 2 else []

data = open(path, "rb").read()

# --- PE ---
pe_off = struct.unpack_from("<I", data, 0x3C)[0]
assert data[pe_off:pe_off+4] == b"PE\0\0"
nsec = struct.unpack_from("<H", data, pe_off + 6)[0]
opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
opt_off = pe_off + 24
sections = []
sec_off = opt_off + opt_size
for i in range(nsec):
    o = sec_off + i * 40
    name = data[o:o+8].rstrip(b"\0")
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, o + 8)
    sections.append((name, vaddr, vsize, raddr, rsize))

def rva_to_off(rva):
    for name, vaddr, vsize, raddr, rsize in sections:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return raddr + (rva - vaddr)
    raise ValueError("rva %x not mapped" % rva)

# --- CLI header (data directory 14) ---
magic = struct.unpack_from("<H", data, opt_off)[0]
dd_off = opt_off + (96 if magic == 0x10B else 112)
cli_rva, cli_size = struct.unpack_from("<II", data, dd_off + 14 * 8)
cli = rva_to_off(cli_rva)
md_rva, md_size = struct.unpack_from("<II", data, cli + 8)
md = rva_to_off(md_rva)

assert data[md:md+4] == b"BSJB"
vlen = struct.unpack_from("<I", data, md + 12)[0]
p = md + 16 + vlen + 2  # root: sig+ver+reserved+vlen, version, flags(2)
nstreams = struct.unpack_from("<H", data, p)[0]
p += 2
streams = {}
for _ in range(nstreams):
    off, size = struct.unpack_from("<II", data, p)
    p += 8
    e = data.index(b"\0", p)
    name = data[p:e].decode()
    p = e + 1
    p = (p + 3) & ~3
    streams[name] = (md + off, size)

tbl_off = streams["#~"][0]
heap_sizes = data[tbl_off + 6]
rowcounts = []
p = tbl_off + 8
valid = struct.unpack_from("<Q", data, tbl_off + 8)[0]
p += 8
sorted_ = struct.unpack_from("<Q", data, p)[0]
p += 8
for i in range(64):
    if valid & (1 << i):
        rowcounts.append(struct.unpack_from("<I", data, p)[0])
        p += 4
    else:
        rowcounts.append(0)

def rowcount(t):
    return rowcounts[t] if t < len(rowcounts) else 0

def idx_size(t):
    return 4 if rowcount(t) >= (1 << 16) else 2

def heap_idx_size(bit):
    return 4 if (heap_sizes & bit) else 2

STR, GUID, BLOB = 0x01, 0x02, 0x04
s_size, g_size, b_size = heap_idx_size(STR), heap_idx_size(GUID), heap_idx_size(BLOB)

def coded_size(tables):
    bits = (len(tables) - 1).bit_length()
    m = max(rowcount(t) for t in tables)
    return 4 if m >= (1 << (16 - bits)) else 2

def build_layout():
    # (table_id: [column kinds])
    L = {}
    L[0x00] = ["u16", "str", "guid", "guid", "guid"]
    L[0x01] = ["coded:TypeDefOrRef", "str", "str"]
    L[0x02] = ["u32", "str", "str", "coded:TypeDefOrRef", "idx:0x04", "idx:0x06"]
    L[0x04] = ["u16", "str", "blob"]
    L[0x06] = ["u32", "u16", "u16", "str", "blob", "idx:0x08"]
    L[0x08] = ["u16", "u16", "str"]
    L[0x09] = ["u16", "u16", "idx:0x06", "coded:TypeDefOrRef"]  # InterfaceImpl
    L[0x0A] = ["coded:MemberRefParent", "str", "blob"]           # MemberRef
    L[0x0B] = ["u16", "coded:HasConstant", "blob"]               # Constant
    return L

# Coded-index column kinds -> the tables they can point at (ECMA-335 II.24.2.6).
# The tag width depends on the table count, the index width on the largest
# row count, so both must come from the real table lists.
CODES = {
    "TypeDefOrRef": [0x02, 0x01, 0x1B],
    "MemberRefParent": [0x02, 0x01, 0x00, 0x06, 0x1B],
    "HasConstant": [0x04, 0x08, 0x17],
}

L = build_layout()
# compute table start offsets
offsets = {}
p2 = p
for t in range(64):
    if rowcount(t) == 0:
        continue
    offsets[t] = p2
    rowsize = 0
    for c in L.get(t, []):
        if c == "u16":
            rowsize += 2
        elif c == "u32":
            rowsize += 4
        elif c == "str":
            rowsize += s_size
        elif c == "blob":
            rowsize += b_size
        elif c == "guid":
            rowsize += g_size
        elif c.startswith("idx:"):
            rowsize += idx_size(int(c[4:], 16))
        elif c.startswith("coded:"):
            rowsize += coded_size(CODES[c.split(":", 1)[1]])
    p2 += rowsize * rowcount(t)

stroff, strsize = streams["#Strings"]

import os
if os.environ.get("WINMD_DEBUG"):
    for t in range(64):
        if rowcount(t):
            print("table 0x%02X rows=%d off=%s layout=%s" % (
                t, rowcount(t), offsets.get(t), L.get(t, "MISSING")))

def getstr(i):
    o = stroff + i
    e = data.index(b"\0", o)
    return data[o:e].decode("utf-8", "replace")

def read_col(t, row, col):
    base = offsets[t]
    rowsize = (offsets[t] if False else 0)
    # recompute rowsize
    rs = 0
    for c in L[t]:
        if c == "u16":
            rs += 2
        elif c == "u32":
            rs += 4
        elif c == "str":
            rs += s_size
        elif c == "blob":
            rs += b_size
        elif c == "guid":
            rs += g_size
        elif c.startswith("idx:"):
            rs += idx_size(int(c[4:], 16))
        elif c.startswith("coded:"):
            rs += coded_size(CODES[c.split(":", 1)[1]])
    o = base + rs * (row - 1)
    out = []
    for c in L[t]:
        if c == "u16":
            out.append(struct.unpack_from("<H", data, o)[0]); o += 2
        elif c == "u32":
            out.append(struct.unpack_from("<I", data, o)[0]); o += 4
        elif c == "str":
            v = struct.unpack_from("<I" if s_size == 4 else "<H", data, o)[0]; o += s_size
            out.append(getstr(v))
        elif c == "blob":
            v = struct.unpack_from("<I" if b_size == 4 else "<H", data, o)[0]; o += b_size
            out.append(v)
        elif c == "guid":
            o += g_size; out.append("guid")
        elif c.startswith("idx:"):
            v = struct.unpack_from("<I" if idx_size(int(c[4:], 16)) == 4 else "<H", data, o)[0]
            o += idx_size(int(c[4:], 16)); out.append(v)
        elif c.startswith("coded:"):
            tables = CODES[c.split(":", 1)[1]]
            sz = coded_size(tables)
            shift = (len(tables) - 1).bit_length()
            v = struct.unpack_from("<I" if sz == 4 else "<H", data, o)[0]; o += sz
            out.append(v >> shift)
    return out

bloboff, blobsize = streams["#Blob"]

def getblob(i):
    """Raw bytes of blob-heap entry i (compressed length prefix included)."""
    o = bloboff + i
    b0 = data[o]
    if b0 & 0x80 == 0:
        n, o = b0, o + 1
    elif b0 & 0xC0 == 0x80:
        n, o = ((b0 & 0x3F) << 8) | data[o + 1], o + 2
    else:
        n, o = ((b0 & 0x1F) << 24) | (data[o + 1] << 16) | (data[o + 2] << 8) | data[o + 3], o + 4
    return data[o:o + n]

def constant_int(blob):
    """Decode a Constant-table value blob as a signed 32-bit int (enum case)."""
    if not blob:
        return None
    elem = blob[0]
    if elem == 0x08 and len(blob) >= 5:      # ELEMENT_TYPE_I4
        v = struct.unpack_from("<i", blob, 1)[0]
        return v
    if elem == 0x09 and len(blob) >= 5:      # ELEMENT_TYPE_U4
        return struct.unpack_from("<I", blob, 1)[0]
    if elem == 0x02:                         # ELEMENT_TYPE_BOOLEAN
        return blob[1] if len(blob) > 1 else None
    return None

n = rowcount(0x02)
all_types = []
for rid in range(1, n + 1):
    flags, name, ns, extends, flist, mlist = read_col(0x02, rid, 0)
    all_types.append((rid, ns, name, flist, mlist))

targets = []
for rid, ns, name, flist, mlist in all_types:
    if want:
        # A name prefixed with "~" matches as a substring, so a versioned type
        # (ISystemMediaTransportControls2/3/4, MediaPlaybackDisplayUpdater...)
        # can be found without knowing the exact name.
        if any((w[1:] in name if w.startswith("~") else w == name) for w in want):
            targets.append((rid, ns, name, flist, mlist))
    else:
        targets.append((rid, ns, name, flist, mlist))

nm = rowcount(0x06)
# A typedef's methods run until the NEXT typedef's MethodList, so the end index
# must be computed over ALL typedefs - doing it over the matched subset only
# padded every range to the end of the method table (useless for ABI checks).
ends = {}
ordered = sorted(all_types, key=lambda x: x[4])
for i, t in enumerate(ordered):
    end = ordered[i + 1][4] if i + 1 < len(ordered) else nm + 1
    ends[t[0]] = end

# NOTE: enum members are deliberately NOT dumped. Their numeric values live in
# the Constant table (0x0B) and the row layout in a winmd is not the ECMA order
# ([Value u32][Type u16][Parent u16] reads consistently, but the values still
# land on the wrong members) - a wrong number here would be worse than none.
# Get enum values from the official docs instead, and use this tool for the
# thing it is good at: interface method order == vtable order.

for rid, ns, name, flist, mlist in targets:
    end = ends[rid]
    print("=== %s.%s  (methods %d..%d) ===" % (ns, name, mlist, end - 1))
    for m in range(mlist, end):
        rva, impl, flags, mname, sig, plist = read_col(0x06, m, 0)
        print("   %2d  %s" % (m - mlist, mname))
