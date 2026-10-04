"""Read the 多人游玩 room's shared block, read-only, from outside the game.

Why this exists: when a room looks stuck ("准备中…" and nothing happens) the
game itself is the last place you want to look - it is the process that might
be wedged. This maps `Local\\CppSekai.Party.v2` (the same named file mapping
platform/Party.cpp uses) and prints the control fields, so you can tell *which*
side is stuck:

  * phase Charging + startCounter 0            -> the load never finished
  * phase Charging + startCounter in the past  -> the instant was armed and
    nobody crossed over: that is the host failing to act on its own arm
  * seats stuck at Ready instead of Loaded     -> the chart decode is what hung

Mapping is opened FILE_MAP_READ, so this can never disturb a live game - it
only reads, and it works on someone else's process since the block is a kernel
object, not a heap pointer.

Layout is duplicated from platform/Party.cpp (SharedBlock / SharedSeat). If
those structs change, this must follow - the mapping name carries a version
for exactly that reason (v2 added trackEndMs).

    python .workbuddy/tools/mp_probe.py            # one snapshot
    python .workbuddy/tools/mp_probe.py --watch 1  # re-read every second
"""

import argparse
import ctypes
import struct
import sys
import time
from ctypes import wintypes

MAPPING_NAME = r"Local\CppSekai.Party.v2"
FILE_MAP_READ = 0x0004
MAX_SLOTS = 8
SEAT_SIZE = 72  # 8 x LONG + char[40]

PHASES = {0: "Lobby", 1: "SongLocked", 2: "Charging", 3: "Running"}
SEATS = {0: "Lobby", 1: "Choosing", 2: "Ready", 3: "Playing", 4: "Result", 5: "Loaded"}

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenFileMappingW.restype = wintypes.HANDLE
k32.OpenFileMappingW.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR]
k32.MapViewOfFile.restype = ctypes.c_void_p
k32.MapViewOfFile.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD,
                              wintypes.DWORD, ctypes.c_size_t]
k32.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
k32.CloseHandle.argtypes = [wintypes.HANDLE]
k32.QueryPerformanceCounter.argtypes = [ctypes.POINTER(ctypes.c_longlong)]
k32.QueryPerformanceFrequency.argtypes = [ctypes.POINTER(ctypes.c_longlong)]


def qpc():
    freq = ctypes.c_longlong()
    k32.QueryPerformanceFrequency(ctypes.byref(freq))
    now = ctypes.c_longlong()
    k32.QueryPerformanceCounter(ctypes.byref(now))
    return now.value, freq.value


def pack_pair(lo, hi):
    return (lo & 0xFFFFFFFF) | ((hi & 0xFFFFFFFF) << 32)


def read_block():
    handle = k32.OpenFileMappingW(FILE_MAP_READ, False, MAPPING_NAME)
    if not handle:
        return None
    try:
        view = k32.MapViewOfFile(handle, FILE_MAP_READ, 0, 0, 0)
        if not view:
            return None
        try:
            buf = ctypes.string_at(view, 412 + MAX_SLOTS * SEAT_SIZE)
        finally:
            k32.UnmapViewOfFile(view)
    finally:
        k32.CloseHandle(handle)
    return buf


def cstr(raw):
    return raw.split(b"\0", 1)[0].decode("utf-8", "replace")


def snapshot():
    buf = read_block()
    if buf is None:
        print("no room: Local\\CppSekai.Party.v2 is not mapped (no instance is running,")
        print("or every instance was started with --no-party / 多人游玩 switched off).")
        return None

    (seq, phase, epoch, host_slot, paused, music_id, host_diff, lead_in_ms,
     start_lo, start_hi, host_song_ms, clock_lo, clock_hi, clock_valid,
     track_end_ms) = struct.unpack_from("<15i", buf, 0)
    start = pack_pair(start_lo, start_hi)
    song_key = cstr(buf[60:60 + 160])
    song_title = cstr(buf[220:220 + 192])

    now, freq = qpc()
    print("room: phase=%s epoch=%d host=seat %d paused=%d" % (
        PHASES.get(phase, "?"), epoch, host_slot, paused))
    print("      song=%r key=%r musicId=%d hostDiff=%d leadIn=%dms" % (
        song_title, song_key, music_id, host_diff, lead_in_ms))
    if start == 0:
        print("      startCounter=0  (not armed - still loading, or lobby)")
    else:
        delta = (now - start) / freq
        state = "IN THE PAST by %.2fs" % delta if delta >= 0 else "in %.2fs" % -delta
        print("      startCounter=%d  (%s)" % (start, state))
    print("      hostClock valid=%d songMs=%d trackEndMs=%d" % (
        clock_valid, host_song_ms, track_end_ms))

    print("  seats:")
    for i in range(MAX_SLOTS):
        off = 412 + i * SEAT_SIZE
        if off + SEAT_SIZE > len(buf):
            break
        alive, hb, ready, diff, seat, score, combo, life = struct.unpack_from("<8i", buf, off)
        name = cstr(buf[off + 32:off + 32 + 40])
        if not alive and not name:
            continue
        age = (ctypes.c_ulong(k32.GetTickCount64() & 0xFFFFFFFF).value - hb) & 0xFFFFFFFF
        print("    [%d] %-8s alive=%d hb=%dms ago seat=%s ready=%d diff=%d score=%d combo=%d" % (
            i, name or "-", alive, age, SEATS.get(seat, "?"), ready, diff, score, combo))
    return buf


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--watch", type=float, metavar="SEC", help="re-read every SEC seconds")
    args = ap.parse_args()

    if args.watch is None:
        snapshot()
        return
    while True:
        try:
            print("--- %s ---" % time.strftime("%H:%M:%S"))
            snapshot()
            sys.stdout.flush()
            time.sleep(args.watch)
        except KeyboardInterrupt:
            return


if __name__ == "__main__":
    main()
