#!/usr/bin/env python3
"""Release gate: which CPU is the oldest this exe can run on?

Why this exists (2026-09-25): build.sh used to let zig pick its default CPU,
which is the *native* CPU of whatever machine builds it (here: `alderlake`,
an i7-1260P). The 2026-09-13 / 09-19 / 09-24 / 09-25 releases therefore all
carried AVX2 / FMA / BMI2 / **AVX-VNNI**. A player on an older CPU got
`exception 0xC000001D` (STATUS_ILLEGAL_INSTRUCTION) at the first such
instruction - "the splash screen appears, then the process vanishes" with no
console output at all (Windows-subsystem binary). The faulting byte at
rva 0x2113A1 was `C4 E2 59 52` = VPDPWSSD, an AVX-VNNI 16-bit dot product that
needs Intel 12th gen (Alder Lake, 2021 Q4) or AMD Zen 5 (2024).

With `-mcpu=baseline` (what build.sh pins now) the answer this tool prints is
"SSE2 - any 64-bit CPU from 2003 on", which is the point of the flag: the
project promises Windows 7 SP1+ and gets run on school PCs.

Measured on this project (cppsekai.exe, ~3 MB of executable sections), same
source, only -mcpu differs:

    family                        native CPU           -mcpu=baseline
    AVX (VEX byte ratio)          116 668  (3.80%)      10 749  (0.36%)
    AVX-VNNI                       41                    0
    movbe                         365                    0
    crc32                          41                    0
    popcnt                          4                    0
    lzcnt/tzcnt                    17                    1
    SSSE3 / SSE4.1 / SSE4.2         0                    0
    SSE3 (odd encodings)            8                    6

0.36% is the *noise floor*: `C4`/`C5` are ordinary immediate/data bytes too and
random bytes hit them ~0.78% of the time, so real AVX code pushes the ratio up
by an order of magnitude. The per-family counts are raw byte patterns, so a
handful of hits (lzcnt 1, SSE3 6) is expected even in a clean build - the
limits below sit well above that noise and were calibrated against the native
build, where the same patterns really are instructions.

No disassembler exists in this toolchain (no objdump, no capstone, zig's lld
drops --export-all-symbols and rejects -Wl,-Map), hence byte patterns + ratio.
For an exact answer on our own TUs, compile to assembly:

    zig c++ <flags> -mcpu=baseline -S -o build/out.s main.cpp
    grep -nE "^\\s+v[a-z]|popcnt|crc32|movbe" build/out.s

`-S` needs the output *directory* to already exist, and the path must be a
Windows path (zig is a native binary; /tmp is not visible to it).

Usage:
    python .workbuddy/tools/cpu_isa_scan.py build/cppsekai.exe [more files...]
Exit code 1 if any file needs something newer than the x86-64 baseline.
"""
import struct
import sys

# Above this share of C4/C5 bytes the file is really using AVX (measured: 0.36
# clean vs 3.80 native; pure random bytes would give ~0.78).
VEX_RATIO_LIMIT = 0.01
# Per-family limits: measured 0 in the baseline build, far above each limit in
# the native build, so none of them can fire on noise.
LIMITS = {
    "avxvnni": 5,     # native 41
    "movbe": 50,      # native 365
    "crc32": 20,      # native 41
    "popcnt": 20,     # native 4 (clang prefers the VEX forms)
    "lzcnt": 10,      # native 17, baseline 1 (noise)
    "sse4x": 20,      # both 0 - a guard against future drift
}

SSE4X_PATTERNS = (
    [bytes([0x66, 0x0F, 0x38, x]) for x in
     list(range(0x00, 0x0C)) +                                  # SSSE3
     [0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,           # SSE4.1
      0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2A, 0x2B,
      0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
      0x37, 0x38, 0x39, 0x3A, 0x3C, 0x3E, 0x3F, 0x40, 0x41]]     # + SSE4.2
    + [bytes([0x66, 0x0F, 0x3A, x]) for x in
       [0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,           # SSE4.1 palignr…
        0x14, 0x15, 0x16, 0x17, 0x20, 0x21, 0x22, 0x40, 0x41, 0x42,
        0x60, 0x61, 0x62, 0x63]]
)
SSE3_PATTERNS = [b"\xf2\x0f\xd0", b"\xf2\x0f\x7c", b"\xf2\x0f\x7d",
                 b"\xf2\x0f\x12", b"\xf2\x0f\xf0"]


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
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, o + 8)
        out.append(data[rawptr:rawptr + min(vsize, rawsize)])
    return out


def count(code, patterns):
    n = 0
    for pat in patterns:
        start = 0
        while True:
            i = code.find(pat, start)
            if i < 0:
                break
            n += 1
            start = i + 1
    return n


def scan(code):
    """Byte-pattern census; every number is an upper bound (data can match)."""
    c = {}
    c["vex"] = sum(1 for i in range(len(code) - 1) if code[i] in (0xC4, 0xC5))
    # C4 P0 P1 op : VEX3, map 0F38, 66 prefix, VNNI dot-product opcode
    c["avxvnni"] = sum(1 for i in range(len(code) - 4)
                       if code[i] == 0xC4 and (code[i + 1] & 0x1F) == 2
                       and (code[i + 2] & 3) == 1 and code[i + 3] in (0x52, 0xB4, 0xB5))
    c["popcnt"] = count(code, [b"\xf3\x0f\xb8"])
    c["lzcnt"] = count(code, [b"\xf3\x0f\xbd", b"\xf3\x0f\xbc"])
    c["crc32"] = count(code, [b"\xf2\x0f\x38\xf0", b"\xf2\x0f\x38\xf1"])
    c["movbe"] = count(code, [b"\x0f\x38\xf0", b"\x0f\x38\xf1"])
    c["sse4x"] = count(code, SSE4X_PATTERNS)
    c["sse3"] = count(code, SSE3_PATTERNS)
    return c


def verdict(counts, size):
    """(is_bad, oldest_cpu, reasons) - is_bad true means "newer than baseline"."""
    ratio = counts["vex"] / size if size else 0.0
    reasons = []
    if counts["avxvnni"] > LIMITS["avxvnni"]:
        reasons.append(f"AVX-VNNI x{counts['avxvnni']}")
    if ratio > VEX_RATIO_LIMIT:
        reasons.append(f"AVX 家族 {ratio:.2%}（噪音底 0.36%）")
    for fam in ("movbe", "crc32", "popcnt", "lzcnt", "sse4x"):
        if counts[fam] > LIMITS[fam]:
            reasons.append(f"{fam} x{counts[fam]}")
    if not reasons:
        return False, ("SSE2 基线 —— 2003 年 x86-64 出现之后的**任何** 64 位 CPU "
                       "（Athlon 64 / Pentium 4 EM64T / Core 2 / K8~K10 / Atom 都行）"), []
    if counts["avxvnni"] > LIMITS["avxvnni"]:
        oldest = "AVX-VNNI —— 要 Intel 12 代（Alder Lake 2021）/ AMD Zen 5（2024）起"
    elif ratio > VEX_RATIO_LIMIT:
        oldest = "AVX2 —— 要 Intel 4 代 Haswell（2013）/ AMD Excavator（2015）起"
    elif counts["movbe"] > LIMITS["movbe"] or counts["crc32"] > LIMITS["crc32"]:
        oldest = "SSE4.2 —— 要 Intel Nehalem（2008）/ AMD K10（2007）起"
    else:
        oldest = "SSE3 及以上 —— 比 x86-64 基线新（零几年上半段的 CPU 会崩）"
    return True, oldest, reasons


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    bad = False
    for path in sys.argv[1:]:
        secs = exec_sections(path)
        if secs is None:
            print(f"{path}: 不是 PE 文件")
            continue
        code = b"".join(secs)
        if not code:
            print(f"{path}: 没有可执行节")
            continue
        c = scan(code)
        is_bad, oldest, reasons = verdict(c, len(code))
        bad = bad or is_bad
        print(f"{path}")
        print(f"    可执行节 {len(code)} 字节")
        print(f"    AVX 家族(VEX 前缀字节) {c['vex']} ({c['vex'] / len(code):.2%})   "
              f"AVX-VNNI {c['avxvnni']}")
        print(f"    popcnt {c['popcnt']}  lzcnt {c['lzcnt']}  crc32 {c['crc32']}  "
              f"movbe {c['movbe']}  SSSE3/SSE4.x {c['sse4x']}  （SSE3 {c['sse3']}，仅供参考）")
        print(f"    {'FAIL' if is_bad else 'OK'} - 最老可跑：{oldest}")
        if reasons:
            print(f"           触发：{'、'.join(reasons)}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
