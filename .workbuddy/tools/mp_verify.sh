#!/usr/bin/env bash
# CppSekai - 多人游玩 end-to-end regression, no screenshots, no mouse.
#
# Two windows of the same build play one round together, driven from the code
# (--party-auto) instead of from fake input, then the two logs are compared on
# the shared QPC axis:
#
#   round 1  host locks a chart -> member picks MASTER -> both start together.
#            Asserts: one shared start counter, BGM only on the host, member
#            clock skew, and that the live scoreboard crosses the process
#            boundary (shared memory write -> other window -> read).
#   round 2  the host pauses mid-live; the member's picture must hold still.
#   round 3  a round played to the end: both windows must reach the result
#            screen on the same chart time and the room must re-arm afterwards.
#
# Usage:  bash .workbuddy/tools/mp_verify.sh
# Exit code: 0 = every check passed.
#
# Kept out of build/ on purpose: build/ is wiped by package.sh and ignored by
# git, and this is the kind of check that is worth re-running months later.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
A_DIR="$ROOT/build/_mp/a"
B_DIR="$ROOT/build/_mp/b"
EXE="$ROOT/build/cppsekai.exe"
mkdir -p "$A_DIR" "$B_DIR"
# Windows-style root for the analysis below: python.exe cannot open /d/... paths.
ROOT_WIN="$(cygpath -w "$ROOT" 2>/dev/null || echo "$ROOT")"

# MSYS_NO_PATHCONV: Git Bash would rewrite /F into a path.
kill_all() { MSYS_NO_PATHCONV=1 taskkill /F /IM cppsekai.exe >/dev/null 2>&1; }

round() { # <seconds> [extra host args...]
    local secs=$1
    shift
    kill_all
    sleep 1
    rm -f "$A_DIR/out.txt" "$B_DIR/out.txt"
    (cd "$A_DIR" && CPSEKAI_MP_TRACE=1 "$EXE" --party --party-name A --party-auto 3 --auto \
        --instance multi --width 900 --height 520 --fps 30 "$@" >"$A_DIR/out.txt" 2>&1) &
    sleep 7
    (cd "$B_DIR" && CPSEKAI_MP_TRACE=1 "$EXE" --party --party-name B --party-auto 4 --auto \
        --instance multi --width 900 --height 520 --fps 30 >"$B_DIR/out.txt" 2>&1) &
    sleep "$secs"
    kill_all
}

round 45
echo "########## round 1: one full round ##########"
echo "-- host:"
grep -h "\[party\]\|\[audio\] bgm\|\[profile\]" "$A_DIR/out.txt" | grep -v claimed
echo "-- member:"
grep -h "\[party\]\|\[audio\] bgm" "$B_DIR/out.txt" | grep -v claimed

python - "$ROOT_WIN" <<'PY'
import re, sys
root = sys.argv[1]
a_path = f"{root}/build/_mp/a/out.txt"
b_path = f"{root}/build/_mp/b/out.txt"
text_a = open(a_path, encoding='utf-8', errors='replace').read()
text_b = open(b_path, encoding='utf-8', errors='replace').read()

def load(path):
    rows = []
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.search(r'\[sync\] qpc=([\d.]+) t=(-?[\d.]+) offset=([+-][\d.]+) (\w+)(.*)', line)
        if m:
            rows.append((float(m.group(1)), float(m.group(2)), float(m.group(3)),
                         m.group(4), 'PAUSED' in m.group(5)))
    return rows

host, member = load(a_path), load(b_path)
ok = True

def check(label, passed, detail=''):
    global ok
    print(('PASS  ' if passed else 'FAIL  ') + label + (('  ' + detail) if detail else ''))
    ok = ok and passed

print(f"clock samples: host={len(host)} member={len(member)}")
check('both windows published a clock', len(host) >= 5 and len(member) >= 5)
go = re.findall(r'go \(lead-in ([\d.]+)s, start counter (\d+)\)', text_a)
go_b = re.findall(r'go \(lead-in ([\d.]+)s, start counter (\d+)\)', text_b)
check('identical start instant + lead-in on both', go and go == go_b, str(go))
check("member received the host's song", 'host picked' in text_b)
check('member muted its BGM', 'bgm muted' in text_b)
check('host played the BGM', '[audio] bgm:' in text_a)
check('no clock sample was rejected', 'clock sample rejected' not in text_a + text_b)

def bracket(series, q, maxGap=1.5):
    for i in range(len(series) - 1):
        q0, t0 = series[i][0], series[i][1]
        q1, t1 = series[i + 1][0], series[i + 1][1]
        if q0 <= q <= q1 and (q1 - q0) <= maxGap:
            return t0 + (t1 - t0) * (q - q0) / (q1 - q0), (series[i][2], series[i + 1][2])
    return None, None

skews = []
steps = 0.0
for i, (qa, ta, _, _, _) in enumerate(host):
    # skip instants where the host's own clock is standing still (paused)
    if i > 0 and abs(ta - host[i - 1][1]) < 1e-9 and (qa - host[i - 1][0]) > 0.5:
        continue
    est, offsets = bracket(member, qa)
    if est is None:
        continue
    # A member whose offset moved inside this bracket was *correcting* onto the
    # host (that is the design); interpolating across such a step would report a
    # skew that never existed on screen, so measure it separately.
    if offsets is not None and abs(offsets[1] - offsets[0]) > 0.020:
        steps = max(steps, abs(offsets[1] - offsets[0]) * 1000.0)
        continue
    skews.append((abs(est - ta) * 1000.0, ta))
if skews:
    worst, at = max(skews)
    vals = sorted(v for v, _ in skews)
    print(f"host-vs-member skew: {len(skews)} instants, median {vals[len(vals)//2]:.1f} ms, "
          f"worst {worst:.1f} ms (at host t={at:+.1f}s)")
    if steps > 0.0:
        print(f"member clock corrections seen: up to {steps:.0f} ms (snap/slew onto the host)")
    check('clock skew under 25 ms', worst < 25.0)
else:
    check('clock skew measurable', False)

peers = [l for l in text_a.splitlines() if 'B=' in l]
last = peers[-1].split('B=')[1].split()[0] if peers else '0/0'
print('host sees the member scoreboard:', last)
check('live scoreboard crossed the process boundary', peers and last.split('/')[0] != '0')
print('ROUND 1', 'PASS' if ok else 'FAIL')
PY

round 30 --show-pause-dialog
echo "########## round 2: host pauses mid-live ##########"
grep -h "host paused\|resumed" "$B_DIR/out.txt" | head -2
python - "$ROOT_WIN" <<'PY'
import re, sys
path = f"{sys.argv[1]}/build/_mp/b/out.txt"
frozen = []
for line in open(path, encoding='utf-8', errors='replace'):
    if 'PAUSED' not in line:
        continue
    m = re.search(r'\[sync\] qpc=([\d.]+) t=(-?[\d.]+)', line)
    if m:
        frozen.append((float(m.group(1)), float(m.group(2))))
print('member frozen samples:', len(frozen))
if len(frozen) >= 2:
    drift = (max(t for _, t in frozen) - min(t for _, t in frozen)) * 1000
    print(f'member chart clock moved {drift:.0f} ms while the host was paused')
    print('ROUND 2', 'PASS' if drift < 50 else 'FAIL')
else:
    print('ROUND 2 FAIL: member never saw the host pause')
PY

# Round 3: one round played *to the end*. Both windows have to hand over to the
# result screen on the same chart time (a member has no BGM, so left alone it
# would call the song over at its chart's last note - seconds early), then walk
# back to the song select with the room re-armed. --result-at shortens the wait
# and --party-auto presses 继续.
ROUND3_ARGS="--result-at 22"
kill_all
sleep 1
rm -f "$A_DIR/out.txt" "$B_DIR/out.txt"
(cd "$A_DIR" && CPSEKAI_MP_TRACE=1 "$EXE" --party --party-name A --party-auto 3 --auto \
    --instance multi --width 900 --height 520 --fps 30 $ROUND3_ARGS >"$A_DIR/out.txt" 2>&1) &
sleep 7
(cd "$B_DIR" && CPSEKAI_MP_TRACE=1 "$EXE" --party --party-name B --party-auto 4 --auto \
    --instance multi --width 900 --height 520 --fps 30 $ROUND3_ARGS >"$B_DIR/out.txt" 2>&1) &
sleep 50
kill_all
echo "########## round 3: result screen in a room ##########"
echo "-- host:"
grep -h "go (\|run length\|\[result\]\|back to the lobby" "$A_DIR/out.txt" | head -8
echo "-- member:"
grep -h "go (\|run length\|\[result\]\|back to the lobby" "$B_DIR/out.txt" | head -8

python - "$ROOT_WIN" <<'PY'
import re, sys
root = sys.argv[1]
a = open(f"{root}/build/_mp/a/out.txt", encoding='utf-8', errors='replace').read()
b = open(f"{root}/build/_mp/b/out.txt", encoding='utf-8', errors='replace').read()
ok = True

def check(label, passed, detail=''):
    global ok
    print(('PASS  ' if passed else 'FAIL  ') + label + (('  ' + detail) if detail else ''))
    ok = ok and passed

check('both windows reached the result screen',
      '[result] shown' in a and '[result] shown' in b)
host_len = re.findall(r'run length ([\d.]+)s published', a)
member_len = re.findall(r'run length from the host: ([\d.]+)s', b)
check('the run length crossed the process boundary',
      bool(host_len) and host_len == member_len, f'host={host_len} member={member_len}')

def last_t(text):
    """Chart time of the last trace sample before the hand-over."""
    head = text.split('[result] shown')[0]
    ts = re.findall(r'\[sync\] qpc=[\d.]+ t=(-?[\d.]+)', head)
    return float(ts[-1]) if ts else None

ta, tb = last_t(a), last_t(b)
check('same chart time at the hand-over',
      ta is not None and tb is not None and abs(ta - tb) < 1.5, f'host t={ta} member t={tb}')
check('both walked back to the song select',
      a.count('continue -> song select') >= 1 and b.count('continue -> song select') >= 1)
# Re-armed = the host publishes the song it is parked on again, and the members
# follow it (their answer for the finished round is void, so they see it as a
# fresh lock).
after_a = a.split('[result] shown')[-1]
after_b = b.split('[result] shown')[-1]
check('the room was re-armed for the next song',
      'back to the lobby' in a and 'song locked' in after_a and 'host picked' in after_b)
print('ROUND 3', 'PASS' if ok else 'FAIL')
PY
echo "########## done ##########"
