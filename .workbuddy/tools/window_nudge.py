#!/usr/bin/env python3
"""Nudge the game window a couple of pixels once a second.

Why this exists: CppSekai's built-in fps sampler (main.cpp, the block that
prints `[frame] N frame(s)/s`) resets every second but only *prints* when the
window moved >0.5 px or some frame in that second took >60 ms. A window that
just sits there therefore reports nothing, and the one line you do get is the
startup second - the noisiest one there is. Shaking the window makes the game
print a fresh sample every second, which is the only steady-state fps readout
the program has.

The move is a plain SetWindowPos, so SDL never sees WM_ENTERSIZEMOVE and the
drag path (which parks the message pump) is not involved.

usage:
    python window_nudge.py [seconds] [window-class]
    python window_nudge.py 20 SDL_app
"""
import ctypes
import sys
import time

u = ctypes.windll.user32


class RECT(ctypes.Structure):
    _fields_ = [
        ("left", ctypes.c_long),
        ("top", ctypes.c_long),
        ("right", ctypes.c_long),
        ("bottom", ctypes.c_long),
    ]


def main():
    seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    cls = sys.argv[2] if len(sys.argv) > 2 else "SDL_app"
    handle = u.FindWindowW(cls, None)
    if not handle:
        print(f"no window of class '{cls}'", flush=True)
        return 1
    for i in range(seconds):
        rect = RECT()
        u.GetWindowRect(handle, ctypes.byref(rect))
        dx = 2 if i % 2 == 0 else -2
        SWP_NOZORDER = 0x0004
        SWP_NOACTIVATE = 0x0010
        u.SetWindowPos(handle, 0, rect.left + dx, rect.top,
                       rect.right - rect.left, rect.bottom - rect.top,
                       SWP_NOZORDER | SWP_NOACTIVATE)
        print(f"{i:3d} rect=({rect.left},{rect.top},{rect.right},{rect.bottom}) dx={dx}", flush=True)
        time.sleep(1.0)
    print("done", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
