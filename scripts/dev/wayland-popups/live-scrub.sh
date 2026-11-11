#!/usr/bin/env bash
# Live options-bar scrub timing on the headless sway rig that run.sh left running (KEEP=1),
# for GitHub #700 (docs/ui-conventions-dialogs.md, "Scrub move coalescing"). Runs
# `patchy_perf_tests textscrub_live` from build/linux-flatpak-qt full screen on the rig:
# a text edit session with one word selected, then the virtual pointer scrubs the Size
# label: 3000 moves, a last move 200 px right of the press (so the size must end at
# 139 + 50 = 189 pt), then a release.
#   live-scrub.sh <on|off> [pace] [hogs] [point|box]
#   on|off   scrub move coalescing (off is the baseline without it)
#   pace N   a 1 ms sleep after every N moves (0 floods)
#   hogs N   pins the app to CPU 3 beside N busy loops there, like a slower machine
# SCALE=1.25 sets QT_SCALE_FACTOR; DOC=3000x2000@300 sets the document. Logs land in
# build/wayland-popups/live-scrub/.
set -u
export XDG_RUNTIME_DIR=/run/user/$(id -u)
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
[ -n "$ROOT" ] && [ -f "$ROOT/CMakeLists.txt" ] || { echo "ERROR: could not find the repository root" >&2; exit 1; }
POP=$ROOT/build/wayland-popups
WD=$(cat "$POP/display")
SDK="org.kde.Sdk//$(sed -nE "s/^runtime-version:[[:space:]]*'?([0-9.]+)'?.*$/\1/p" "$ROOT/packaging/linux/flatpak/com.rtsoft.patchy.yml" | head -1)"
OUT=$ROOT/build/wayland-popups/live-scrub
mkdir -p "$OUT"
mode=$1
pace=${2:-0}
hogs=${3:-0}
kind=${4:-box}
tag="$mode-$pace-$hogs-$kind-${SCALE:-1}"
hog_pids=()
for _ in $(seq "$hogs"); do taskset -c 3 sh -c 'while :; do :; done' & hog_pids+=($!); done
PIN=()
[ "$hogs" -gt 0 ] && PIN=(taskset -c 3)
rm -f "$OUT/ready"
WAYLAND_DISPLAY=$WD "${PIN[@]}" flatpak run --user --socket=wayland --filesystem=home \
  --env=QT_QPA_PLATFORM=wayland --env=QT_SCALE_FACTOR="${SCALE:-1}" --env=PATCHY_PERF_ONSCREEN=1 --env=PATCHY_PERF_LIVE_READY="$OUT/ready" \
  --env=PATCHY_PERF_LIVE_COALESCE="$mode" --env=PATCHY_PERF_LIVE_SECONDS=180 --env=PATCHY_PERF_TEXTSCRUB_KIND="$kind" \
  --env=PATCHY_PERF_TEXTSCRUB_DOC="${DOC:-1920x1080@72}" \
  --command="$ROOT/build/linux-flatpak-qt/patchy_perf_tests" "$SDK" textscrub_live > "$OUT/perf-$tag.log" 2>&1 &
pid=$!
for _ in $(seq 240); do [ -s "$OUT/ready" ] && break; sleep 0.5; done
if [ ! -s "$OUT/ready" ]; then
  echo "not ready"; tail -20 "$OUT/perf-$tag.log"; kill "$pid"
  for hog in "${hog_pids[@]}"; do kill "$hog"; done
  exit 1
fi
read -r x y w h < "$OUT/ready"
python3 "$HERE/live-scrub-moves.py" "$x" "$y" "$pace" > "$OUT/moves.txt"
start=$(date +%s.%N)
WAYLAND_DISPLAY=$WD flatpak run --user --filesystem=home --socket=wayland --command="$POP/vptr" "$SDK" "$w" "$h" < "$OUT/moves.txt"
end=$(date +%s.%N)
echo "vptr seconds: $(python3 -c "print(round($end - $start, 2))")"
wait "$pid"
for hog in "${hog_pids[@]}"; do kill "$hog"; done
grep -E "LIVE|PERF|FAIL" "$OUT/perf-$tag.log"
