"""Scan libil2cpp for BL/B instructions targeting one VA (arm64 AArch64)."""
import struct
import sys

path = sys.argv[1]
target = int(sys.argv[2], 16)
lo = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0
hi = int(sys.argv[4], 16) if len(sys.argv) > 4 else None

data = open(path, 'rb').read()

# Parse ELF64 program headers to map file offsets <-> VAs.
assert data[:4] == b'\x7fELF'
e_phoff = struct.unpack_from('<Q', data, 0x20)[0]
e_phentsize = struct.unpack_from('<H', data, 0x36)[0]
e_phnum = struct.unpack_from('<H', data, 0x38)[0]
loads = []
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    p_type = struct.unpack_from('<I', data, off)[0]
    if p_type != 1:
        continue
    p_offset = struct.unpack_from('<Q', data, off + 0x08)[0]
    p_vaddr = struct.unpack_from('<Q', data, off + 0x10)[0]
    p_filesz = struct.unpack_from('<Q', data, off + 0x20)[0]
    loads.append((p_offset, p_vaddr, p_filesz))


def off_to_va(o):
    for p_offset, p_vaddr, p_filesz in loads:
        if p_offset <= o < p_offset + p_filesz:
            return p_vaddr + (o - p_offset)
    return None


def va_to_off(v):
    for p_offset, p_vaddr, p_filesz in loads:
        if p_vaddr <= v < p_vaddr + p_filesz:
            return p_offset + (v - p_vaddr)
    return None


start = va_to_off(lo) if lo else 0
end = va_to_off(hi) if hi else len(data)
hits = []
for o in range(start, end - 3, 4):
    w = struct.unpack_from('<I', data, o)[0]
    if (w & 0xFC000000) != 0x94000000:
        continue
    imm = w & 0x03FFFFFF
    if imm & 0x02000000:
        imm -= 0x04000000
    va = off_to_va(o)
    if va is None:
        continue
    tgt = va + imm * 4
    if tgt == target:
        hits.append(va)
for h in hits:
    print('BL 0x%X -> 0x%X' % (h, target))
print('total', len(hits))
