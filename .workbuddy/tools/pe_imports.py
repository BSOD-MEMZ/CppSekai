#!/usr/bin/env python3
"""PE 导入表检查（无第三方依赖）。

用法:
    python pe_imports.py <exe/dll> [more...]

作用: 解析 PE 的 Import Directory (IMAGE_DIRECTORY_ENTRY_IMPORT = 1)，
打印每个 DLL 的导入函数名。配合 win7_api_check.py 可判断 Win7 SP1 兼容性。

踩坑记录:
- 别用 `strings | grep KERNEL32` 判断: 字符串可能来自资源/调试信息，不一定是 IAT。
- 导入表按 DLL 展开，DLL 名是 RVA 指向的 ASCII 串（在 .rdata 里）。
"""
import struct
import sys
import os


def rva_to_off(secs, rva):
    for (name, vaddr, vsize, rawptr, rawsize) in secs:
        if vaddr <= rva < vaddr + max(vsize, rawsize):
            return rawptr + (rva - vaddr)
    return None


def read_cstr(data, off):
    end = data.index(b"\x00", off)
    return data[off:end].decode("ascii", "replace")


def parse(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"MZ":
        raise SystemExit(f"{path}: not a PE")
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_off:pe_off + 4] != b"PE\0\0":
        raise SystemExit(f"{path}: bad PE signature")
    num_sec, = struct.unpack_from("<H", data, pe_off + 6)
    opt_off = pe_off + 24
    magic, = struct.unpack_from("<H", data, opt_off)
    is64 = magic == 0x20B
    # DataDirectory 偏移: PE32+ = 112, PE32 = 96
    dd_off = opt_off + (112 if is64 else 96)
    import_rva, import_size = struct.unpack_from("<II", data, dd_off + 1 * 8)
    delay_rva, delay_size = struct.unpack_from("<II", data, dd_off + 13 * 8)
    sec_off = opt_off + struct.unpack_from("<H", data, pe_off + 20)[0]
    secs = []
    for i in range(num_sec):
        o = sec_off + 40 * i
        name = data[o:o + 8].rstrip(b"\x00").decode("ascii", "replace")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, o + 8)
        secs.append((name, vaddr, vsize, rawptr, rawsize))

    out = {}

    def walk(desc_rva, is_delay):
        while True:
            off = rva_to_off(secs, desc_rva)
            if off is None:
                return
            if is_delay:
                # IMAGE_DELAYLOAD_DESCRIPTOR 全部为零表示结束，大小 32 字节
                attrs, name_rva = struct.unpack_from("<II", data, off)
                if attrs == 0 and name_rva == 0:
                    return
                int_rva = struct.unpack_from("<I", data, off + 16)[0]
            else:
                oft, stamp, fwd, name_rva, first = struct.unpack_from("<IIIII", data, off)
                if name_rva == 0:
                    return
                int_rva = oft or first
            noff = rva_to_off(secs, name_rva)
            if noff is None:
                return
            dll = read_cstr(data, noff)
            fns = out.setdefault(dll, set())
            i = 0
            while True:
                eoff = rva_to_off(secs, int_rva + i * (8 if is64 else 4))
                if eoff is None:
                    break
                ent = struct.unpack_from("<Q" if is64 else "<I", data, eoff)[0]
                if ent == 0:
                    break
                if ent & (1 << (63 if is64 else 31)):
                    # 序号导入: 低位 16 位是序号
                    fns.add(f"#{ent & 0xFFFF}")
                else:
                    hoff = rva_to_off(secs, ent)
                    if hoff is not None:
                        fns.add(read_cstr(data, hoff + 2))
                i += 1
            desc_rva += 32 if is_delay else 20

    if import_rva:
        walk(import_rva, False)
    if delay_rva:
        walk(delay_rva, True)
    return out


API_SETS = {
    # Windows 7 才有的导入：Win7 兼容做完之后还要支持 Vista 就得看这一组。
    # （Vista = 6.0，Win7 = 6.1；SRWLock / FlsAlloc / GetFileInformationByHandleEx /
    #  GetFinalPathNameByHandleW / InitOnceExecuteOnce / GetTickCount64 都是 Vista 就有的，
    #  不要放进这一组。）
    "win7": [
        "GetLogicalProcessorInformationEx", "SetThreadGroupAffinity",
        "GetActiveProcessorCount", "GetMaximumProcessorCount",
        "GetActiveProcessorGroupCount", "SetThreadErrorMode", "GetThreadErrorMode",
        "Wow64GetThreadContext", "Wow64SetThreadContext", "GetSystemDefaultLocaleName",
        "CreateFileMappingNumaW", "CreateFile2", "GetPackageFullName",
    ],
    # 关键 Win8 / Win8.1 / Win10+ 才有的导入，Win7 上加载即失败。
    # 注意 win7 基线是 **Vista+**：GetFileInformationByHandleEx / SetFileInformationByHandle /
    # GetFinalPathNameByHandleW / FlsAlloc / InitOnceExecuteOnce / SRWLock 这一批都是 Vista 就有，
    # 别误报。
    "win8": [
        "GetSystemTimePreciseAsFileTime", "WaitOnAddress", "WakeByAddressAll",
        "WakeByAddressSingle", "CreateFile2", "GetOverlappedResultEx",
        "PrefetchVirtualMemory", "CreateThreadpoolIo", "StartThreadpoolIo",
        "GetProcessMitigationPolicy", "SetProcessMitigationPolicy",
        "GetPackageFamilyName", "GetApplicationUserModelId",
    ],
    "win81": [
        "DiscardVirtualMemory", "VirtualAlloc2", "MapViewOfFile3",
        "GetProcessInformation", "SetProcessInformation",
    ],
    "win10": [
        "SetThreadDescription", "GetThreadDescription", "GetTempPath2W",
        "GetTempPath2A", "GetSystemTimeAdjustmentPrecise", "ProcessPrng",
        "GetSystemCpuSetInformation", "RtlGenRandom",
    ],
}


def main():
    for path in sys.argv[1:]:
        print(f"=== {os.path.basename(path)} ===")
        try:
            imps = parse(path)
        except Exception as e:  # noqa: BLE001
            print(f"  !! {e}")
            continue
        hits = []
        for dll in sorted(imps):
            fns = imps[dll]
            print(f"  {dll}: {len(fns)} imports")
            # win7 组单独报：它只是"比 Vista 新"，不是"Win8 独占"
            win7 = [(dll, n) for n in API_SETS.get("win7", []) if n in fns]
            for _dll, n in win7:
                print(f"  [win7-only] {_dll}!{n}")
            for level, names in API_SETS.items():
                if level == "win7":
                    continue
                for n in names:
                    if n in fns:
                        hits.append((level, dll, n))
        if hits:
            print("  --- 高风险导入 ---")
            for lv, dll, n in hits:
                print(f"  [{lv}] {dll}!{n}")
        else:
            print("  --- 未发现已知 Win8+ 独占导入 ---")
        print()


if __name__ == "__main__":
    main()
