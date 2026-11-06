#!/usr/bin/env python3
"""反汇编 Android arm64 libil2cpp.so（按 dump.cs 的 VA/RVA 定位）。

用法:
  python il2cpp_disasm.py <so> --va 0x506F818 --count 130
  python il2cpp_disasm.py <so> --range 0x506F818 0x506FA08
  python il2cpp_disasm.py <so> --va 0x506F818 --count 130 --strict   # 遇 ret 即停

依赖: capstone (arm64)
"""
import sys
import struct
import argparse
import re

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from capstone.arm64 import ARM64_OP_IMM, ARM64_OP_MEM, ARM64_OP_REG


def parse_phdrs(path):
    with open(path, "rb") as f:
        ident = f.read(16)
        if ident[:4] != b"\x7fELF":
            raise SystemExit("not an ELF")
        is64 = ident[4] == 2
        if not is64:
            raise SystemExit("only ELF64 supported")
        endian = "<" if ident[5] == 1 else ">"
        f.seek(32)
        e_phoff = struct.unpack(endian + "Q", f.read(8))[0]
        f.seek(54)
        e_phentsize = struct.unpack(endian + "H", f.read(2))[0]
        e_phnum = struct.unpack(endian + "H", f.read(2))[0]
        segs = []
        f.seek(e_phoff)
        for _ in range(e_phnum):
            raw = f.read(e_phentsize)
            p_type = struct.unpack(endian + "I", raw[0:4])[0]
            if p_type != 1:  # PT_LOAD
                continue
            p_offset, p_vaddr, _p_paddr, p_filesz, p_memsz = struct.unpack(
                endian + "QQQQQ", raw[8:48])
            segs.append((p_vaddr, p_offset, p_filesz, p_memsz))
        return segs


def va_to_off(segs, va):
    for vaddr, off, filesz, _memsz in segs:
        if vaddr <= va < vaddr + filesz:
            return off + (va - vaddr)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("so")
    ap.add_argument("--va", type=lambda s: int(s, 16))
    ap.add_argument("--range", nargs=2, type=lambda s: int(s, 16))
    ap.add_argument("--count", type=int, default=80)
    ap.add_argument("--strict", action="store_true", help="遇到 ret 停止")
    a = ap.parse_args()

    segs = parse_phdrs(a.so)
    if a.range:
        start, end = a.range
    else:
        start = a.va
        end = start + a.count * 4

    off = va_to_off(segs, start)
    if off is None:
        raise SystemExit("VA 0x%X 不在任何 PT_LOAD 里" % start)
    n = end - start
    with open(a.so, "rb") as f:
        f.seek(off)
        code = f.read(n)

    md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
    md.detail = True
    adrp_cache = {}
    for ins in md.disasm(code, start):
        ann = []
        mnem = ins.mnemonic
        if mnem == "adrp":
            rd = ins.reg_name(ins.operands[0].reg)
            adrp_cache[rd] = ins.operands[1].imm
        elif mnem in ("add", "ldr", "ldrb", "ldrh", "ldrsb", "ldrsw",
                      "str", "strb", "strh", "ldur", "stur"):
            base = None
            disp = 0
            for op in ins.operands:
                if op.type == ARM64_OP_MEM:
                    base = ins.reg_name(op.mem.base)
                    disp = op.mem.disp
            if base and base in adrp_cache and mnem in ("add", "ldr", "ldrsw", "ldrb", "ldrh"):
                tgt = adrp_cache[base] + disp
                ann.append("; =0x%X" % (tgt & 0xFFFFFFFFFFFFFFFF))
        if mnem in ("bl", "b", "b.eq", "b.ne", "b.lt", "b.ge", "b.gt", "b.le",
                    "b.hi", "b.ls", "b.hs", "b.lo", "cbz", "cbnz", "tbz", "tbnz"):
            for op in ins.operands:
                if op.type == ARM64_OP_IMM:
                    ann.append("; -> 0x%X" % op.imm)

        print("0x%08X  %-8s %-34s %s" % (ins.address, ins.bytes.hex(), ins.mnemonic + " " + ins.op_str,
                                        " ".join(ann)))
        if a.strict and mnem == "ret":
            break


if __name__ == "__main__":
    main()
