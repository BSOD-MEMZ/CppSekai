"""Click inside a window with the *real* cursor.

`build/winsend.exe` posts window messages, which is enough for the game's own
SDL input path - but not for ImGui: the SDL2 backend overwrites io.MousePos
with the actual global cursor position whenever the window has keyboard focus,
so a posted click lands wherever the physical cursor happens to be. This tool
moves the physical cursor instead (ClientToScreen + SetCursorPos + mouse_event),
which is what a scripted UI regression needs.

Usage:
    python click_at.py <window title (substring ok)> <client x> <client y> [--move-only]
"""

import ctypes
import sys
import time
from ctypes import wintypes

MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    title = sys.argv[1]
    client_x = int(sys.argv[2])
    client_y = int(sys.argv[3])
    move_only = "--move-only" in sys.argv

    user32 = ctypes.windll.user32
    hwnd = user32.FindWindowW(None, title)
    if not hwnd:
        # The game titles its window "CppSekai - 玩家 1" / "... - 玩家 2", so an
        # exact-title lookup for plain "CppSekai" misses. Fall back to a
        # substring match - but the downloader is also called "CppSekai 谱面
        # 下载器", and clicking into *that* window silently does nothing useful
        # (it ate a click at 788,513 during an EULA check on 2026-09-18).
        # Collect every candidate and prefer the game's own title shape.
        buf = ctypes.create_unicode_buffer(512)
        EnumProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

        def cb(h, _):
            if not user32.IsWindowVisible(h):
                return True
            n = user32.GetWindowTextW(h, buf, 512)
            if n and title in buf.value:
                candidates.append((h, buf.value))
            return True

        candidates: list[tuple[int, str]] = []
        user32.EnumWindows(EnumProc(cb), 0)
        # "CppSekai - 玩家 1" first, then any other "CppSekai - ..." window,
        # and only then a bare substring hit (the downloader).
        def rank(item: tuple[int, str]) -> int:
            t = item[1]
            if t.startswith("CppSekai - "):
                return 0
            if t.startswith("CppSekai") and "谱面" not in t:
                return 1
            return 2

        candidates.sort(key=rank)
        hwnd = candidates[0][0] if candidates else 0
        if hwnd and rank(candidates[0]) > 0:
            print(f"note: matched {candidates[0][1]!r}, not the game window")
    if not hwnd:
        print("window not found:", title)
        return 1

    point = wintypes.POINT(client_x, client_y)
    if not user32.ClientToScreen(hwnd, ctypes.byref(point)):
        print("ClientToScreen failed")
        return 1

    user32.SetForegroundWindow(hwnd)
    user32.SetCursorPos(point.x, point.y)
    time.sleep(0.25)
    if move_only:
        print("moved to", point.x, point.y)
        return 0

    def click() -> None:
        user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
        time.sleep(0.05)
        user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)

    # SDL2 drops the click that gives an unfocused window focus
    # (SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH defaults to off), so a scripted first
    # click on a background window would vanish. Spend one click on focusing.
    if user32.GetForegroundWindow() != hwnd:
        click()
        time.sleep(0.4)

    click()
    print("clicked at", point.x, point.y)
    return 0


if __name__ == "__main__":
    sys.exit(main())
