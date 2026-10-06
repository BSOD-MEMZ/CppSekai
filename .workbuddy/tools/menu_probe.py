"""Probe the native menu bar of a running CppSekai (设置 > 系统 > 原生菜单栏).

Why it exists: the menu bar is the one piece of this game that lives entirely in
Win32's non-client area, so neither `--screenshot` (GL framebuffer only) nor the
in-game debug flags can see any of it. Everything here goes through user32 on the
running window instead:

    python .workbuddy/tools/menu_probe.py rect
        window rect / client rect / border widths, plus whether a menu is
        attached and how tall it makes the non-client area. This is the cheap
        check for "did SetMenu actually take" (the client loses ~20px of height,
        the window box does not move) and for "does it stay off in borderless /
        fullscreen / 自绘无框".

    python .workbuddy/tools/menu_probe.py dump
        walks the menu bar: every item with its command id, label and state
        (勾 / 单选点 / 置灰). What the check marks *should* be is decided by
        platform::menu::sync, so this is how the wiring gets verified.

    python .workbuddy/tools/menu_probe.py send <commandId>
        PostMessage(WM_COMMAND, id) - exactly what Windows sends when an item is
        picked. The command is handled by the window subclass, so this exercises
        the real path (minus the modal menu loop) and the game's own log says
        what happened.

    python .workbuddy/tools/menu_probe.py menuloop enter|exit
        PostMessage(WM_ENTERMENULOOP / WM_EXITMENULOOP). Synthetic - no real
        menu loop is running - but it drives the subclass branch that keeps
        frames coming while the menu is open, which is the part that cannot be
        reached any other way from a headless session. `enter` should be
        followed by "[menu] WM_ENTERMENULOOP" and then a stream of
        "[window] drag frames: N (… timer=N)" lines; if the timer were being
        killed by the "mouse button is up = drag over" guard, those would stop
        immediately.

Window lookup is (image name -> pid -> visible titled top-level window), same as
win_window.py, so pass --pid when another copy of the game is running.
"""

import argparse
import ctypes
import subprocess
import sys
from ctypes import wintypes

u32 = ctypes.WinDLL("user32", use_last_error=True)
CallbackType = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

WM_COMMAND = 0x0111
WM_ENTERMENULOOP = 0x0211
WM_EXITMENULOOP = 0x0212

MF_BYPOSITION = 0x00000400
MF_BYCOMMAND = 0x00000000
MF_SEPARATOR = 0x00000800
MF_POPUP = 0x00000010
MF_CHECKED = 0x00000008
MF_DISABLED = 0x00000002
MF_GRAYED = 0x00000001

u32.EnumWindows.argtypes = [CallbackType, wintypes.LPARAM]
u32.IsWindowVisible.argtypes = [wintypes.HWND]
u32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
u32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
u32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
u32.GetMenu.argtypes = [wintypes.HWND]
u32.GetMenu.restype = wintypes.HMENU
u32.GetSubMenu.argtypes = [wintypes.HMENU, ctypes.c_int]
u32.GetSubMenu.restype = wintypes.HMENU
u32.GetMenuItemCount.argtypes = [wintypes.HMENU]
u32.GetMenuItemID.argtypes = [wintypes.HMENU, ctypes.c_int]
u32.GetMenuState.argtypes = [wintypes.HMENU, ctypes.c_uint, ctypes.c_uint]
u32.GetMenuStringW.argtypes = [wintypes.HMENU, ctypes.c_uint, wintypes.LPWSTR, ctypes.c_int,
                               ctypes.c_uint]
u32.GetMenuStringW.restype = ctypes.c_int
u32.PostMessageW.argtypes = [wintypes.HWND, ctypes.c_uint, wintypes.WPARAM, wintypes.LPARAM]
u32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
u32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
u32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]


def pids_of(image):
    # 解码要显式指定：tasklist 的输出跟着控制台代码页走（中文系统上是 GBK），
    # 而 subprocess 的 text=True 默认按 UTF-8 解 —— 环境一变就 UnicodeDecodeError。
    # errors="replace" 兜底：这一行只是为了拿 pid，个别字节坏了也不该整个脚本挂掉。
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq %s" % image, "/FO", "CSV", "/NH"],
                         capture_output=True, encoding="utf-8", errors="replace").stdout or ""
    pids = []
    for line in out.splitlines():
        cells = [c.strip().strip('"') for c in line.split('","')]
        if len(cells) >= 2 and cells[0].lower() == image.lower():
            try:
                pids.append(int(cells[1]))
            except ValueError:
                pass
    return pids


def windows_of(pids):
    found = []

    def cb(hwnd, _param):
        pid = wintypes.DWORD()
        u32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value in pids and u32.IsWindowVisible(hwnd):
            length = u32.GetWindowTextLengthW(hwnd)
            if length > 0:
                buf = ctypes.create_unicode_buffer(length + 1)
                u32.GetWindowTextW(hwnd, buf, length + 1)
                found.append((hwnd, buf.value))
        return True

    u32.EnumWindows(CallbackType(cb), 0)
    return found


def find_window(image, pid, title):
    pids = [pid] if pid else pids_of(image)
    if not pids:
        sys.exit("no %s process (tasklist)" % image)
    windows = windows_of(pids)
    if title:
        windows = [w for w in windows if title in w[1]]
    if not windows:
        sys.exit("no visible titled window for pid(s) %s" % pids)
    if len(windows) > 1:
        print("note: %d windows, using the first: %s" % (len(windows), windows), file=sys.stderr)
    return windows[0][0], windows[0][1]


def label_of(menu, pos, by_id=False):
    flag = MF_BYCOMMAND if by_id else MF_BYPOSITION
    length = u32.GetMenuStringW(menu, pos, None, 0, flag)
    if length <= 0:
        return ""
    buf = ctypes.create_unicode_buffer(length + 1)
    u32.GetMenuStringW(menu, pos, buf, length + 1, flag)
    return buf.value


def state_of(menu, pos, by_id=False):
    flag = MF_BYCOMMAND if by_id else MF_BYPOSITION
    state = u32.GetMenuState(menu, pos, flag)
    if state == 0xFFFFFFFF:
        return "?"
    bits = []
    if state & MF_GRAYED:
        bits.append("greyed")
    elif state & MF_DISABLED:
        bits.append("disabled")
    if state & MF_CHECKED:
        bits.append("checked")
    return ",".join(bits) if bits else "-"


def dump_menu(menu, indent, depth=0):
    count = u32.GetMenuItemCount(menu)
    for i in range(count):
        text = label_of(menu, i)
        state = state_of(menu, i)
        sub = u32.GetSubMenu(menu, i)
        if sub:
            print("%s[%d] %-24s  (popup, %s)" % (indent, i, text, state))
            dump_menu(sub, indent + "    ", depth + 1)
            continue
        if not text:
            print("%s[%d] ---- separator" % (indent, i))
            continue
        item_id = u32.GetMenuItemID(menu, i)
        print("%s[%d] %-24s  id=%d  %s" % (indent, i, text, item_id, state))


def rect_info(hwnd):
    win = wintypes.RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(win))
    cli = wintypes.RECT()
    u32.GetClientRect(hwnd, ctypes.byref(cli))
    origin = wintypes.POINT(0, 0)
    u32.ClientToScreen(hwnd, ctypes.byref(origin))
    menu = u32.GetMenu(hwnd)
    print("window rect : (%d,%d)-(%d,%d)  %dx%d" % (
        win.left, win.top, win.right, win.bottom, win.right - win.left, win.bottom - win.top))
    print("client rect : (%d,%d)-(%d,%d)  %dx%d" % (
        origin.x, origin.y, origin.x + cli.right, origin.y + cli.bottom, cli.right, cli.bottom))
    print("border L/R/T/B : %d / %d / %d / %d px" % (
        origin.x - win.left, win.right - (origin.x + cli.right),
        origin.y - win.top, win.bottom - (origin.y + cli.bottom)))
    print("menu bar : %s" % ("attached (HMENU=0x%X)" % menu if menu else "none"))
    # 菜单栏算在"上边框"里，所以带菜单时 border T 会比不带时大一条菜单栏的高度。
    print("  -> 上边框里含标题栏 + 菜单栏；对照同一窗口关掉菜单栏时这个数会小 ~20px")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("action", choices=["rect", "dump", "send", "menuloop"])
    ap.add_argument("arg", nargs="?", help="send: command id / menuloop: enter|exit")
    ap.add_argument("--image", default="cppsekai.exe")
    ap.add_argument("--pid", type=int, default=0)
    ap.add_argument("--title", default="CppSekai")
    args = ap.parse_args()

    hwnd, title = find_window(args.image, args.pid, args.title)
    print("window : %s (hwnd=0x%X)" % (title, hwnd))

    if args.action == "rect":
        rect_info(hwnd)
        return
    if args.action == "dump":
        menu = u32.GetMenu(hwnd)
        if not menu:
            sys.exit("no menu bar on this window")
        dump_menu(menu, "  ")
        return
    if args.action == "send":
        if args.arg is None:
            sys.exit("send needs a command id")
        item_id = int(args.arg, 0)
        ok = u32.PostMessageW(hwnd, WM_COMMAND, item_id, 0)
        print("PostMessage(WM_COMMAND, %d) -> %s" % (item_id, "ok" if ok else "failed"))
        return
    if args.action == "menuloop":
        if args.arg not in ("enter", "exit"):
            sys.exit("menuloop needs enter|exit")
        msg = WM_ENTERMENULOOP if args.arg == "enter" else WM_EXITMENULOOP
        ok = u32.PostMessageW(hwnd, msg, 0, 0)
        print("PostMessage(0x%04X) -> %s" % (msg, "ok" if ok else "failed"))
        return


if __name__ == "__main__":
    main()
