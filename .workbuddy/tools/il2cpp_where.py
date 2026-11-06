#!/usr/bin/env python3
"""按 VA 反查 dump.cs 里所属的方法/类型（Il2CppDumper 的 dump.cs 文本）。

用法:
  python il2cpp_where.py <dump.cs> 0x4F7822C [0x53E07EC ...]
"""
import re
import sys

RVA_RE = re.compile(r"// RVA: (0x[0-9A-Fa-f]+)")
DECL_RE = re.compile(r"^\t(?:public|private|protected|internal|static|\s)*[^\n]*\(")


def load(dump_path):
    """dump.cs 里 RVA 注释在方法声明**上一行**，按「先见 RVA、后见声明」配对。"""
    entries = []  # (rva, line_text, method_text)
    pending = None
    with open(dump_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = RVA_RE.search(line)
            if m:
                pending = int(m.group(1), 16)
                continue
            s = line.rstrip("\n")
            if pending is not None and s.startswith("\t") and "(..." not in s and " { }" in s and "(" in s:
                entries.append((pending, s.strip()))
                pending = None
    entries.sort(key=lambda x: x[0])
    return entries


def main():
    entries = load(sys.argv[1])
    rvas = [e[0] for e in entries]
    import bisect
    for a in sys.argv[2:]:
        va = int(a, 16)
        i = bisect.bisect_right(rvas, va) - 1
        if i < 0:
            print("%s: ?" % a)
            continue
        print("%s -> 0x%X  %s" % (a, entries[i][0], entries[i][1]))


if __name__ == "__main__":
    main()
