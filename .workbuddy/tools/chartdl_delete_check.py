"""Live check for chartdl's 删除文件 button (the confirmation path + row layout).

`--delete-selected` exercises the deletion itself but skips the message box,
because no headless run can answer one. This drives the real GUI instead: it
posts a real click on 删除文件 and answers the box both ways.

  * 否 -> nothing may be deleted (the default answer is 否 on purpose);
  * 是 -> exactly the selected song's files go, and another song's stay.

Then it clamps the window to its minimum size (by asking for a 0x0 window, the
cheapest way to make Windows apply WM_GETMINMAXINFO) and checks that the three
buttons that share the second row - 取消 / 删除文件 / 设置… - still have a gap.

The song's files are created by this script, not downloaded: the id is taken
from the row that --select picks, read out of the `[delete] <title> (<id4>)`
line the binary already logs.

Usage:  python chartdl_delete_check.py <chartdl.exe> <scratch-dir> [--select N]
"""

import ctypes
import ctypes.wintypes as w
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chartdl_minimize_check as ck  # find_window() / grab(), keyed by PID

u = ctypes.windll.user32

WM_COMMAND = 0x0111
K_ID_DELETE = 1020
IDYES, IDNO = 6, 7


def find_dialog(pid):
    hit = []

    def cb(h, _):
        owner = w.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(owner))
        if owner.value != pid:
            return True
        cls = ctypes.create_unicode_buffer(64)
        u.GetClassNameW(h, cls, 64)
        if cls.value == "#32770":  # the class MessageBoxW uses
            hit.append(h)
        return True

    u.EnumWindows(ctypes.WINFUNCTYPE(ctypes.c_bool, w.HWND, w.LPARAM)(cb), 0)
    return hit[0] if hit else 0


def wait_dialog(pid, timeout=4.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        dlg = find_dialog(pid)
        if dlg:
            return dlg
        time.sleep(0.1)
    return 0


def dialog_text(dlg):
    msg = ctypes.create_unicode_buffer(1024)
    u.GetDlgItemTextW(dlg, 0xFFFF, msg, 1024)  # the static MessageBox uses
    title = ctypes.create_unicode_buffer(256)
    u.GetWindowTextW(dlg, title, 256)
    return title.value, msg.value


def buttons_of(hwnd):
    """Immediate children by caption -> (x, y, w, h), in client coordinates."""
    out = {}

    def cb(h, _):
        r = w.RECT()
        u.GetWindowRect(h, ctypes.byref(r))
        p = w.POINT(r.left, r.top)
        u.ScreenToClient(hwnd, ctypes.byref(p))
        txt = ctypes.create_unicode_buffer(64)
        u.GetWindowTextW(h, txt, 64)
        if txt.value:
            out[txt.value] = (p.x, p.y, r.right - r.left, r.bottom - r.top)
        return True

    u.EnumChildWindows(hwnd, ctypes.WINFUNCTYPE(ctypes.c_bool, w.HWND, w.LPARAM)(cb), 0)
    return out


def read_song_id(log_path):
    """The `[delete] <title> (<id4>): ...` line of a --delete-selected probe
    run tells us which song that row is and how its files are named. Returns
    (id4, title) - title is "" when the line is not there."""
    with open(log_path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = re.search(r"\[delete\] (.*) \((\d{4})\):", line)
            if m:
                return m.group(2), m.group(1)
    return "", ""


def main():
    import subprocess

    args = [a for a in sys.argv[1:] if not a.startswith("--select")]
    if len(args) < 2:
        print(__doc__.strip())
        return 2
    exe, scratch = args[0], args[1]
    row = "20"
    if "--select" in sys.argv:
        row = sys.argv[sys.argv.index("--select") + 1]
    os.makedirs(scratch, exist_ok=True)
    cwd = os.path.dirname(os.path.abspath(exe)) or "."
    log_path = os.path.join(os.getcwd(), "chartdl.log")

    # 1. Probe run: which song is that row, and what are its files called? It
    #    deletes nothing on the first pass (the folder is empty) and logs the id.
    if os.path.exists(log_path):
        os.remove(log_path)
    subprocess.run([exe, "--out", scratch, "--select", row, "--delete-selected",
                    "--screenshot", os.path.join(scratch, "probe.png")],
                   cwd=os.getcwd(), check=False)
    song, title_text = read_song_id(log_path)
    if not song:
        print("FAIL: the probe run did not report a song id")
        return 1
    print("[probe] row", row, "-> id", song, "title", title_text)

    # 2. Plant that song's files, plus a decoy belonging to another song.
    names = [f"{song}_{d}.sus" for d in ("easy", "normal", "hard", "expert", "master")]
    names += [f"{song}.png", f"{song}.json", f"{song}_01.mp3"]
    decoys = ["0045_master.sus", "0045.png"] if song != "0045" else ["0046.png", "0046_master.sus"]
    for name in names + decoys:
        open(os.path.join(scratch, name), "wb").write(b"x" * 512)
    before = sorted(os.listdir(scratch))

    # 3. Real GUI, real click, both answers.
    proc = subprocess.Popen([exe, "--out", scratch, "--select", row,
                             "--screenshot-time", "30"], cwd=os.getcwd())
    try:
        hwnd = ck.find_window(proc.pid)
        if not hwnd:
            print("FAIL: no window for pid", proc.pid)
            return 1
        time.sleep(2.0)

        u.PostMessageW(hwnd, WM_COMMAND, K_ID_DELETE, 0)
        dlg = wait_dialog(proc.pid)
        if not dlg:
            print("FAIL: clicking 删除文件 brought up no confirmation box")
            return 1
        title, text = dialog_text(dlg)
        print("[dialog]", title, "|", text.replace("\n", " / "))
        if title_text and title_text not in text:
            print("FAIL: the box does not name the song")
            return 1

        u.PostMessageW(dlg, WM_COMMAND, IDNO, 0)
        time.sleep(1.5)
        after_no = sorted(os.listdir(scratch))
        print("[否] files:", len(after_no), "unchanged" if after_no == before else "CHANGED")

        u.PostMessageW(hwnd, WM_COMMAND, K_ID_DELETE, 0)
        dlg = wait_dialog(proc.pid)
        if not dlg:
            print("FAIL: no confirmation box on the second click")
            return 1
        u.PostMessageW(dlg, WM_COMMAND, IDYES, 0)
        time.sleep(1.5)
        after_yes = sorted(os.listdir(scratch))
        print("[是] files left:", after_yes)

        # 4. Second row at the minimum window size.
        rect = w.RECT()
        u.GetWindowRect(hwnd, ctypes.byref(rect))
        u.SetWindowPos(hwnd, 0, rect.left, rect.top, 0, 0, 0)
        time.sleep(1.0)
        client = w.RECT()
        u.GetClientRect(hwnd, ctypes.byref(client))
        rows = buttons_of(hwnd)
        a, b, c = rows.get("取消"), rows.get("删除文件"), rows.get("设置…")
        print("[min] client", client.right, "x", client.bottom, "row:", a, b, c)
        ck.grab(hwnd, os.path.join(scratch, "min_size.png"))

        ok_no = after_no == before
        # probe.png is this script's own probe screenshot, not a chart file.
        ok_yes = after_yes == sorted(decoys + ["probe.png"])
        ok_row = bool(a and b and c) and b[0] - (a[0] + a[2]) >= 0 and c[0] - (b[0] + b[2]) >= 0
        print("RESULT:", "OK" if (ok_no and ok_yes and ok_row) else "FAIL",
              f"(否 kept={ok_no}, 是 deleted={ok_yes}, row fits={ok_row})")
        return 0 if (ok_no and ok_yes and ok_row) else 1
    finally:
        proc.terminate()


if __name__ == "__main__":
    sys.exit(main())
