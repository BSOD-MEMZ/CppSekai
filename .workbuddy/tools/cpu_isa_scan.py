#!/usr/bin/env python3
"""Release gate: does this exe/dll need CPU instructions an old machine lacks?

Why this exists (2026-09-25): build.sh used to let zig pick its default CPU,
which is the *native* CPU of whatever machine builds it (here: `alderlake`).
The 2026-09-13 / 09-19 / 09-24 / 09-25 releases therefore all carried AVX2 /
FMA / BMI2 / **AVX-VNNI** instructions. A player on an older CPU got
`exception 0xC000001D` (STATUS_ILLEGAL_INSTRUCTION) at the first such
instruction - "the splash screen appears, then the process vanishes", with no
console output at all (Windows-subsystem binary). The faulting byte at
rva 0x2113A1 was `C4 E2 59 52` = VPDPWSSD, an AVX-VNNI 16-bit dot product that
needs Intel 12th gen (Alder Lake, 2021) or newer.

Reference numbers measured on this project (cppsekai.exe, ~3 MB of .text):

    -mcpu=baseline   VEX 前缀字节 ~10 700 (0.36%)   AVX-VNNI 模式 0   <- 期望值
    默认（native）    VEX 前缀字节 ~116 600 (3.80%)  AVX-VNNI 模式 41

0.36% is the *noise floor*: `C4`/`C5` are ordinary immediate/data bytes too, and
random bytes would hit them ~0.78% of the time. Any real AVX code pushes the
ratio up by an order of magnitude, so 1% is a safe line. The AVX-VNNI pattern
is ~0.4 expected false positives per 3 MB, so anything above a handful is real.

No disassembler is available in this toolchain (no objdump, no capstone, and
zig's lld silently drops --export-all-symbols), hence the byte-pattern +
ratio approach. For an exact answer on your own TUs, compile to assembly:

    zig c++ <flags> -mcpu=baseline -S -o out.s main.cpp && grep -nE "^\\s+v[a-z]" out.s

`-S` needs the output *directory* to already exist, and the path must be a
Windows path (zig is a native binary; /tmp is not visible to it).

Usage:
    python .workbuddy/tools/cpu_isa_scan.py build/cppsekai.exe [more files...]
Exit code 1 if any file looks like it needs AVX or newer.
"""
import struct
import sys

VEX_RATIO_LIMIT = 0.01   # measured: baseline 0.36%, native 3.8%
VNNI_LIMIT = 5           # measured: baseline 0, native 41; noise ~0.4


def exec_sections(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"MZ":
        return None
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        return None
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt = struct.unpack_from("<H", data, pe + 20)[0]
    base = pe + 24 + opt
    out = []
    for i in range(nsec):
        o = base + i * 40
        chars = struct.unpack_from("<I", data, o + 36)[0]
        if not (chars & 0x20000000):  # IMAGE_SCN_MEM_EXECUTE
            continue
        name = data[o:o + 8].rstrip(b"\0").decode("latin1")
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, o + 8)
        out.append((name, data[rawptr:rawptr + min(vsize, rawsize)]))
    return out


def scan(code):
    """(vex_bytes, vex2_seen, vnni_patterns) - see the docstring for the caveats."""
    vex = 0
    for i in range(len(code) - 1):
        if code[i] in (0xC4, 0xC5):
            vex += 1
    vnni = 0
    for i in range(len(code) - 4):
        # C4 P0 P1 op : VEX3, map 0F38, 66 prefix, opcode in the VNNI dot-product family
        if code[i] == 0xC4 and (code[i + 1] & 0x1F) == 2 and (code[i + 2] & 3) == 1 \
                and code[i + 3] in (0x52, 0xB4, 0xB5):
            vnni += 1
    return vex, vnni


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    bad = False
    for path in sys.argv[1:]:
        secs = exec_sections(path)
        if secs is None:
            print(f"{path}: 不是 PE 文件")
            continue
        code = b"".join(s for _, s in secs)
        if not code:
            print(f"{path}: 没有可执行节")
            continue
        vex, vnni = scan(code)
        ratio = vex / len(code)
        if vnni > VNNI_LIMIT:
            verdict = (f"FAIL - 含 AVX-VNNI 指令（{vnni} 处）：需要 Intel 12 代 "
                       f"(Alder Lake, 2021) 或更新的 CPU，老机器会 0xC000001D")
            bad = True
        elif ratio > VEX_RATIO_LIMIT:
            verdict = (f"FAIL - VEX 字节占比 {ratio:.2%} 远高于基线（0.36%）：疑似按 native "
                       f"CPU 编译，旧 CPU 上会 0xC000001D")
            bad = True
        else:
            verdict = "OK - 与 x86-64 基线一致（SSE2；Core 2 / Athlon 64 也能跑）"
        print(f"{path}\n    可执行节 {len(code)} 字节  VEX 字节 {vex} ({ratio:.2%})  "
              f"AVX-VNNI 模式 {vnni}\n    {verdict}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
