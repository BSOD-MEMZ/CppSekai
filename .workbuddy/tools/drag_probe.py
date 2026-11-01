"""模拟一次标题栏拖动，量拖动期间到底服务了多少帧。

WM_NCLBUTTONDOWN(HTCAPTION) 会让 Windows 进入自己的模态移动循环（和真人拖标题栏
一模一样），期间由窗口子类里的 WM_TIMER/WM_MOVING 喂帧。拖动结束后读
cppsekai.log 里的 "[window] drag over ... N frame(s)"，再除以拖动时长。
"""
import ctypes
import ctypes.wintypes as wt
import subprocess
import sys
import time

u32 = ctypes.windll.user32

WM_NCLBUTTONDOWN = 0xA1
HTCAPTION = 2
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
HTCAPTION_OK = True


def find_window(title_part="CppSekai"):
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        n = u32.GetWindowTextLengthW(hwnd)
        if n:
            buf = ctypes.create_unicode_buffer(n + 1)
            u32.GetWindowTextW(hwnd, buf, n + 1)
            if title_part in buf.value and u32.IsWindowVisible(hwnd):
                found.append(hwnd)
        return True

    u32.EnumWindows(cb, 0)
    return found[0] if found else None


def main():
    hwnd = find_window()
    if hwnd is None:
        print("window not found")
        return 1
    rect = wt.RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(rect))
    # 标题栏：客户区上方那条（窗口是标准带边框窗口，caption 高约 30）
    mode = sys.argv[1] if len(sys.argv) > 1 else 'move'
    if mode == 'resize':
        # 右下角边框：往外 2px 落在非客户区的缩放边上
        x, y = rect.right - 2, rect.bottom - 2
    else:
        x = (rect.left + rect.right) // 2
        y = rect.top + 14
    print("hwnd=%s rect=%s,%s,%s,%s grab=%d,%d" % (hwnd, rect.left, rect.top, rect.right, rect.bottom, x, y))

    u32.SetCursorPos(x, y)
    time.sleep(0.05)

    # 真实按下：只发 WM_NCLBUTTONDOWN 不够 —— 模态移动循环看的是真实的按键状态，
    # 没有按下的左键它立刻退出。用 mouse_event 真按，系统自己就开始拖。
    t0 = time.time()
    u32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    time.sleep(0.15)

    # 拖动：走一个小方框再回来
    path = []
    if mode == 'resize':
        # 缩小 240x160 再放回来
        for d in range(0, 241, 20):
            path.append((x - d, y - int(d * 2 / 3)))
        for d in range(240, -1, -20):
            path.append((x - d, y - int(d * 2 / 3)))
    else:
        for dx in range(0, 200, 20):
            path.append((x + dx, y))
        for dy in range(0, 120, 20):
            path.append((x + 200, y + dy))
        for dx in range(200, -1, -20):
            path.append((x + dx, y + 120))
        for dy in range(120, -1, -20):
            path.append((x, y + dy))
    for px, py in path:
        u32.SetCursorPos(px, py)
        time.sleep(0.05)

    u32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
    dur = time.time() - t0
    rect2 = wt.RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(rect2))
    print("drag lasted %.2f s, window %s,%s -> %s,%s" % (dur, rect.left, rect.top, rect2.left, rect2.top))
    return 0


sys.exit(main())
