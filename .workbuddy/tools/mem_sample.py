"""采样一个进程的工作集 / 峰值 / 提交，用来定位"内存到底在哪一步涨上去的"。

    python mem_sample.py                     # 默认采 build/cppsekai.exe 的选曲界面启动曲线
    python mem_sample.py <exe> [args...]     # 采任意命令

每 150 ms 采一次（前 3 秒），之后 1.5 s 一次，最多 17 秒。读的是
PROCESS_MEMORY_COUNTERS：WorkingSetSize（≈任务管理器"内存"那一列）、
**PeakWorkingSetSize（启动峰值，比稳态更能说明问题）**、PagefileUsage（提交）。

2026-09-19 用它查出：CppSekai 启动峰值 291 MB → 12 秒后稳态 245 MB，中间那
46 MB 是启动期临时分配（字体文件整读 + 贴图解码）的延迟归还，跟"切背景"无关；
真正的大头是**常驻的 TTF 数据**（msyh.ttc 19.7 MB + msyhbd.ttc 16.9 MB）。
数据与结论见 .workbuddy/memory/2026-09-19.md。
"""
import ctypes
import subprocess
import sys
import time
from ctypes import wintypes

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
psapi = ctypes.WinDLL('psapi', use_last_error=True)

PROCESS_QUERY_LIMITED_INFORMATION = 0x1000


class PMC(ctypes.Structure):
    _fields_ = [('cb', wintypes.DWORD), ('PageFaultCount', wintypes.DWORD),
                ('PeakWorkingSetSize', ctypes.c_size_t), ('WorkingSetSize', ctypes.c_size_t),
                ('QuotaPeakPagedPoolUsage', ctypes.c_size_t), ('QuotaPagedPoolUsage', ctypes.c_size_t),
                ('QuotaPeakNonPagedPoolUsage', ctypes.c_size_t), ('QuotaNonPagedPoolUsage', ctypes.c_size_t),
                ('PagefileUsage', ctypes.c_size_t), ('PeakPagefileUsage', ctypes.c_size_t)]


def main():
    args = sys.argv[1:] or [r'D:\Dev\CppSekai\build\cppsekai.exe', '--no-party',
                            '--screenshot', 'D:/tmp/mem/stage.png', '--screenshot-time', '16']
    proc = subprocess.Popen(args, cwd=r'D:\Dev\CppSekai',
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    handle = k32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, proc.pid)
    if not handle:
        print('OpenProcess failed:', ctypes.get_last_error())
        return 1
    pmc = PMC()
    pmc.cb = ctypes.sizeof(PMC)
    t0 = time.time()
    print('   t     working set      peak          commit')
    while proc.poll() is None and time.time() - t0 < 17:
        psapi.GetProcessMemoryInfo(handle, ctypes.byref(pmc), pmc.cb)
        el = time.time() - t0
        print(f'{el:6.2f}s  {pmc.WorkingSetSize / 1048576:8.1f} MB  {pmc.PeakWorkingSetSize / 1048576:8.1f} MB  '
              f'{pmc.PagefileUsage / 1048576:8.1f} MB')
        sys.stdout.flush()
        time.sleep(0.15 if el < 3.0 else 1.5)
    return 0


if __name__ == '__main__':
    sys.exit(main())
