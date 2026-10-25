"""把游戏的启动日志每一行和当时的实际内存对齐打印，定位"哪个阶段吃了多少"。

    python mem_trace.py [exe] [args...]

stdout 走管道时 CppSekai 不往 stdout 说话（它判断"没附加控制台"就改写 cwd 的
cppsekai.log），所以这里跟着日志文件读：每出现一行新日志就采一次
WorkingSetSize，把 `[boot] xxx` / `[intro] ...` / `[tex] ...` 当锚点。

没有日志输出的空档由一个线程每 250ms 补采一次，标成 `·`（看得到 Windows 延迟
归还 working set 的那个台阶）。

2026-10-25 用它查出：被拒的字体候选 TTF 数据仍常驻、谱面扫描一次涨 42MB。
"""
import ctypes
import os
import subprocess
import sys
import threading
import time
from ctypes import wintypes

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
psapi = ctypes.WinDLL('psapi', use_last_error=True)

PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
CWD = r'D:\Dev\CppSekai'
LOG = os.path.join(CWD, 'cppsekai.log')


class PMC(ctypes.Structure):
    _fields_ = [('cb', wintypes.DWORD), ('PageFaultCount', wintypes.DWORD),
                ('PeakWorkingSetSize', ctypes.c_size_t), ('WorkingSetSize', ctypes.c_size_t),
                ('QuotaPeakPagedPoolUsage', ctypes.c_size_t), ('QuotaPagedPoolUsage', ctypes.c_size_t),
                ('QuotaPeakNonPagedPoolUsage', ctypes.c_size_t), ('QuotaNonPagedPoolUsage', ctypes.c_size_t),
                ('PagefileUsage', ctypes.c_size_t), ('PeakPagefileUsage', ctypes.c_size_t)]


def read(handle):
    pmc = PMC()
    pmc.cb = ctypes.sizeof(PMC)
    if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(pmc), pmc.cb):
        return None
    return pmc


def main():
    args = sys.argv[1:] or [r'D:\Dev\CppSekai\build\cppsekai.exe', '--no-party',
                            '--screenshot', 'D:/tmp/mem/trace.png', '--screenshot-time', '16']
    try:
        os.remove(LOG)
    except OSError:
        pass

    t0 = time.time()
    proc = subprocess.Popen(args, cwd=CWD, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    handle = k32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, proc.pid)
    pos = 0
    buf = ''
    last = {'t': 0.0}
    lock = threading.Lock()
    stop = threading.Event()

    def sample():
        pmc = read(handle)
        return (pmc.WorkingSetSize / 1048576.0, pmc.PeakWorkingSetSize / 1048576.0) if pmc else (0.0, 0.0)

    def bg():
        while not stop.is_set():
            ws, _ = sample()
            now = time.time() - t0
            with lock:
                if now - last['t'] >= 0.24:
                    last['t'] = now
                    print('  %6.3fs   %7.1f MB   \u00b7' % (now, ws), flush=True)
            stop.wait(0.05)

    th = threading.Thread(target=bg, daemon=True)
    th.start()

    while proc.poll() is None:
        if os.path.exists(LOG):
            with open(LOG, 'r', encoding='utf-8', errors='replace') as fh:
                fh.seek(pos)
                chunk = fh.read()
                pos = fh.tell()
            if chunk:
                buf += chunk
                while '\n' in buf:
                    line, buf = buf.split('\n', 1)
                    ws, peak = sample()
                    now = time.time() - t0
                    with lock:
                        last['t'] = now
                    print('  %6.3fs   %7.1f MB   %s' % (now, ws, line), flush=True)
        stop.wait(0.02)

    stop.set()
    th.join(timeout=0.5)
    print('--- peak work set 372-ish/final: %s' % (sample(),))


if __name__ == '__main__':
    main()
