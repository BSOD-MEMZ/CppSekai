#!/usr/bin/env bash
# 无头截图，带白屏重试。
#
# 项目自己的 --screenshot 偶尔会抓到一帧还没画完的窗口（整张纯白，PNG 只有
# ~35KB）。这是时序问题，不是渲染 bug —— 同一份 exe 重跑就好。这个脚本把
# "截图 + 检查是不是白图 + 重试" 包起来，省得每次都手动重来。
#
# 用法:
#   ./shot.sh <输出路径> [额外参数...]
# 例:
#   ./shot.sh shots/tapfx.png --no-party --tap-fx-test
set -u

OUT="${1:?用法: shot.sh <输出路径> [额外参数...]}"
shift
MIN_BYTES=${MIN_BYTES:-120000}
TRIES=${TRIES:-4}

mkdir -p "$(dirname "$OUT")"

for i in $(seq 1 "$TRIES"); do
    rm -f "$OUT"
    timeout 120 ./build/cppsekai.exe "$@" --screenshot "$OUT" >/dev/null 2>&1
    size=$(stat -c%s "$OUT" 2>/dev/null || echo 0)
    if [ "$size" -ge "$MIN_BYTES" ]; then
        echo "ok ($size bytes, try $i) -> $OUT"
        exit 0
    fi
    echo "  白图重试 $i/$TRIES ($size bytes)"
    sleep 1
done

echo "失败: $TRIES 次都是白图或没产出"
exit 1
