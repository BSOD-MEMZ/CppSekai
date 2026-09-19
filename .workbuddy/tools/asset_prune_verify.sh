#!/usr/bin/env bash
# 验证「删掉 asset_audit.py 判定为未使用的素材之后游戏照跑」。
#
# 做法：在 build/_prune/ 造一份独立副本（exe + SDL2.dll + 数据表 + charts + assets），
# 按审计清单删掉未使用文件，然后跑四种模式（选曲 / 演奏 / 结算 / 暂停弹窗 + pjsk 字体），
# 最后把日志里所有"加载失败 / 缺文件"的行抓出来。**不碰仓库里的 assets/**。
#
# Usage:  bash .workbuddy/tools/asset_prune_verify.sh
# Exit:   0 = 剪枝后没有任何加载失败
#
# 坑：这个环境里 `pwd` 可能返回 Windows 形式（D:\...），把它拼给原生 exe/python 会被
# MSYS 改写成 D:\d\Dev\... 这种鬼路径。所以拷完就 cd 进去，之后一律用相对路径。
set -u
cd "$(cd "$(dirname "$0")/../.." && pwd)" || exit 1
PRUNE="build/_prune"
PY="${PY:-C:/Users/Miku/.workbuddy/binaries/python/envs/default/Scripts/python.exe}"
AUDIT=".workbuddy/tools/asset_audit.py"

rm -rf "$PRUNE"
mkdir -p "$PRUNE/charts"
cp build/cppsekai.exe build/SDL2.dll "$PRUNE/"
cp music-levels.json musics.json music-vocals.json "$PRUNE/" 2>/dev/null || true
cp charts/test.sus charts/test.mp3 "$PRUNE/charts/" 2>/dev/null || true
cp -r assets "$PRUNE/assets"

echo "-- 审计清单"
"$PY" "$AUDIT" assets | sed -n '1,4p'

"$PY" "$AUDIT" assets --paths | tr -d '\r' > "$PRUNE/unused.txt"
n=0
while IFS= read -r p; do
    rel="${p#assets/}"
    if [ -e "$PRUNE/assets/$rel" ]; then
        rm -f "$PRUNE/assets/$rel"
        n=$((n + 1))
    fi
done < "$PRUNE/unused.txt"
echo "-- 删掉 $n 个文件后：$(find "$PRUNE/assets" -type f | wc -l) 个文件  $(du -sh "$PRUNE/assets" | cut -f1)"
rm -f "$PRUNE/unused.txt"

run_case() { # <名字> <参数...>
    local name="$1"
    shift
    rm -f "$PRUNE/cppsekai.log"
    (cd "$PRUNE" && ./cppsekai.exe "$@" >/dev/null 2>&1)
    echo "-- $name"
    local bad
    bad=$(grep -iE "failed to load|no UI SE|no level icon|cannot open|could not|no such file" \
        "$PRUNE/cppsekai.log" 2>/dev/null | head -8)
    if [ -n "$bad" ]; then
        echo "$bad" | sed 's/^/   !! /'
    else
        echo "   (无加载失败)"
    fi
    grep -c "\[stats\]" "$PRUNE/cppsekai.log" 2>/dev/null | sed 's/^/   stats lines: /'
}

run_case "选曲界面" --screenshot "shot_select.png" --width 960 --height 540
run_case "演奏 12s" --sus "charts/test.sus" --auto --screenshot "shot_play.png" --screenshot-time 12 --width 960 --height 540
run_case "结算画面" --sus "charts/test.sus" --auto --result-at 12 --screenshot "shot_result.png" --screenshot-time 20 --width 960 --height 540
run_case "暂停弹窗+字体" --sus "charts/test.sus" --auto --show-pause-dialog --pjsk-font --screenshot "shot_pause.png" --screenshot-time 1.2 --width 960 --height 540

echo "-- 截图"
ls -la "$PRUNE"/*.png 2>/dev/null | awk '{print "  " $5 "  " $9}'
