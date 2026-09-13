"""Click inside a window with the *real* cursor.

`build/winsend.exe` posts window messages, which is enough for the game's own
SDL input path - but not for ImGui: the SDL2 backend overwrites io.MousePos
with the actual global cursor position whenever the window has keyboard focus,
so a posted click lands wherever the physical cursor happens to be. This tool
moves the physical cursor instead (ClientToScreen + SetCursorPos + mouse_event),
which is what a scripted UI regression needs.

Usage:
    python click_at.py <window title> <client x> <client y> [--move-only]
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
