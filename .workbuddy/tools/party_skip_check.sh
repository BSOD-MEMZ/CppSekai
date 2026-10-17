#!/usr/bin/env bash
# CppSekai - 多人游玩「开幕卡跳过」回归（两窗口，无头）。
#
# 验的是 2026-10-17 那条修：lead-in 属于房间，成员窗口按跳过只能记请求，
# 由房主真正执行。期望日志：
#   成员  [intro] lead-in skip requested (the host performs it)
#   房主  [intro] lead-in skipped (the whole room)
#
# 做法：A 起房（--party-auto 会等第二个玩家），B 起来后自动选难度+确定，
# 两边进演奏态后往 **B 的窗口**发一个真实的窗口消息点击，砸在跳过圆钮上
# （1920x1080 虚拟坐标 1834,63 → 1280x720 客户区 1222,42）。
# 跳过钮是游戏自己 hit-test 的（不走 ImGui），所以 PostMessage 够用。
#
# Usage: bash .workbuddy/tools/party_skip_check.sh
# Exit: 0 = 两行日志都在；1 = 缺日志（打印两边的 [party]/[intro] 行）
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
EXE="$ROOT/build/cppsekai.exe"
A_DIR="$ROOT/build/_mp_skip/a"
B_DIR="$ROOT/build/_mp_skip/b"
WINSEND="$ROOT/build/winsend.exe"
mkdir -p "$A_DIR" "$B_DIR"

kill_all() { MSYS_NO_PATHCONV=1 taskkill /F /IM cppsekai.exe >/dev/null 2>&1; }

if [ ! -x "$EXE" ]; then echo "missing $EXE (build first)"; exit 2; fi
if [ ! -x "$WINSEND" ]; then echo "missing $WINSEND (build winsend.c)"; exit 2; fi

kill_all
sleep 1
rm -f "$A_DIR/out.txt" "$B_DIR/out.txt"

(cd "$A_DIR" && CPSEKAI_MP_TRACE=1 "$EXE" --party --party-name A --party-auto 3 --auto \
    --instance multi --width 1280 --height 720 --fps 30 > out.txt 2>&1 &)
sleep 5
(cd "$B_DIR" && CPSEKAI_MP_TRACE=1 "$EXE" --party --party-name B --party-auto 4 --auto \
    --instance multi --width 1280 --height 720 --fps 30 > out.txt 2>&1 &)

for _ in $(seq 1 300); do
    grep -q "\[party\] go" "$B_DIR/out.txt" 2>/dev/null && break
    sleep 0.1
done
# 跳过钮只在 lead-in 的头 4 秒里可点，而 PostMessage 的点击要靠窗口拿到前台焦点
# （SDL 只认焦点窗口的按键/鼠标消息，而 SetForegroundWindow 有时会被系统拒绝），
# 所以在这一段里重复点几次 —— 多记几个请求无害（房主跑完一次之后 skipLeadIn 自己变 no-op）。
for _ in $(seq 1 8); do
    "$WINSEND" "CppSekai - B" click 1222 42 >/dev/null 2>&1
    sleep 0.25
    grep -q "lead-in skip requested" "$B_DIR/out.txt" 2>/dev/null && break
done
sleep 1.5
kill_all

echo "--- member (B) ---"
grep -h "\[party\]\|\[intro\] lead-in" "$B_DIR/out.txt" | grep -v claimed
echo "--- host (A) ---"
grep -h "\[party\]\|\[intro\] lead-in" "$A_DIR/out.txt" | grep -v claimed

ok=0
grep -q "lead-in skip requested" "$B_DIR/out.txt" && ok=1
grep -q "lead-in skipped (the whole room)" "$A_DIR/out.txt" && ok=$((ok + 1))
if [ "$ok" -eq 2 ]; then echo "PASS: the room skipped the lead-in"; exit 0; fi
echo "FAIL: $ok/2 expected log lines"
exit 1
