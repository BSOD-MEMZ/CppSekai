#!/usr/bin/env python3
"""Dump the bytes of a PE image at a given RVA (crash-log addresses are RVAs).

Usage:
    python pe_rva_dump.py <exe> <rva-hex> [bytes-before] [bytes-after]

Prints the file offset, the surrounding bytes, and a crude scan for VEX/legacy
SSE encodings so an "illegal instruction" (0xC000001D) can be attributed to a
CPU feature the user's machine may not have (AVX/AVX2/FMA/BMI/...).

No PE library needed: the section table is parsed by hand.
"""
import struct
import sys


def sections(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"MZ":
        raise SystemExit("not a PE file")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise SystemExit("bad PE signature")
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    sec = pe + 24 + opt_size
    out = []
    for i in range(nsec):
        off = sec + i * 40
        name = data[off:off + 8].rstrip(b"\0").decode("latin1")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, off + 8)
        out.append((name, vaddr, vsize, rawptr, rawsize))
    return data, out


VEX1 = {0x58: "vaddps", 0x59: "vmulps", 0x5C: "vsubps", 0x54: "vandps", 0x6F: "vmovdqu",
        0x7F: "vmovdqu(store)", 0xEF: "vpxor", 0xFE: "vpaddd", 0xFA: "vpsubd", 0x77: "vzeroupper"}
VEX1_MAP_D = {0x58: "vaddpd", 0x59: "vmulpd", 0x54: "vandpd", 0xAB: "vfmadd...ps"}


def describe(byte0, byte1, byte2, byte3):
    """Very rough opcode guess from the bytes starting at the instruction."""
    if byte0 == 0xC5:
        return f"VEX2 prefix (opcode {byte2:02X}) -> AVX: {VEX1.get(byte2, '?')}"
    if byte0 == 0xC4:
        return f"VEX3 prefix (map {byte1 & 0x1F:02X}, opcode {byte3:02X}) -> AVX/AVX2/FMA"
    table = {
        (0xF3, 0x0F, 0x1E, 0xFA): "endbr64",
        (0x0F, 0x38, 0xF0): "movbe", (0x0F, 0x38, 0xF1): "movbe",
        (0xF3, 0x0F, 0xB8): "popcnt", (0xF3, 0x0F, 0xBD): "lzcnt",
        (0xA2,): "cpuid", (0x0F, 0x0B): "ud2 (unreachable/illegal)",
        (0x0F, 0x05): "syscall",
    }
    return "legacy/SSE: " + table.get((byte0, byte1, byte2), "?")


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    path = sys.argv[1]
    rva = int(sys.argv[2], 16)
    before = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    after = int(sys.argv[4]) if len(sys.argv) > 4 else 48
    data, secs = sections(path)
    for name, vaddr, vsize, rawptr, rawsize in secs:
        if vaddr <= rva < vaddr + max(vsize, rawsize):
            file_off = rawptr + (rva - vaddr)
            print(f"{name}: rva 0x{rva:X} -> file offset 0x{file_off:X}")
            start = file_off - before
            chunk = data[start:file_off + after]
            for i in range(0, len(chunk), 16):
                addr = rva - before + i
                mark = " <== fault" if (addr <= rva < addr + 16) else ""
                hexs = " ".join(f"{b:02X}" for b in chunk[i:i + 16])
                print(f"  0x{addr:08X}  {hexs:<47}{mark}")
            hit = data[file_off:file_off + 16]
            print(f"\nfault bytes: {' '.join(f'{b:02X}' for b in hit)}")
            print("guess: " + describe(*hit[:4]))
            return
    raise SystemExit(f"rva 0x{rva:X} is not inside any section")


if __name__ == "__main__":
    main()
