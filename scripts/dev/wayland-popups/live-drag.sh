#!/usr/bin/env bash
# Live Move-drag timing on the headless sway rig that run.sh left running (KEEP=1), for
# pointer-lag reports (docs/interactive-previews.md, "Drag move coalescing"). Runs
# `patchy_perf_tests textdrag_live` from build/linux-flatpak-qt full screen on the rig and
# drags its text layer with the virtual pointer: 3000 moves, then a release.
#   live-drag.sh <on|off> [pace] [hogs]
#   on|off   drag move coalescing (off is the baseline without it)
#   pace N   a 1 ms sleep after every N moves (0 floods)
#   hogs N   pins the app to CPU 3 beside N busy loops there, like a slower machine
# SCALE=1.25 sets QT_SCALE_FACTOR. Logs land in build/wayland-popups/live-drag/.
set -u
export XDG_RUNTIME_DIR=/run/user/$(id -u)
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
[ -n "$ROOT" ] && [ -f "$ROOT/CMakeLists.txt" ] || { echo "ERROR: could not find the repository root" >&2; exit 1; }
POP=$ROOT/build/wayland-popups
WD=$(cat "$POP/display")
SDK="org.kde.Sdk//$(sed -nE "s/^runtime-version:[[:space:]]*'?([0-9.]+)'?.*$/\1/p" "$ROOT/packaging/linux/flatpak/com.rtsoft.patchy.yml" | head -1)"
OUT=$ROOT/build/wayland-popups/live-drag
mkdir -p "$OUT"
mode=$1
pace=${2:-0}
hogs=${3:-0}
tag="$mode-$pace-$hogs-${SCALE:-1}"
hog_pids=()
for _ in $(seq "$hogs"); do taskset -c 3 sh -c 'while :; do :; done' & hog_pids+=($!); done
PIN=()
[ "$hogs" -gt 0 ] && PIN=(taskset -c 3)
rm -f "$OUT/ready"
WAYLAND_DISPLAY=$WD "${PIN[@]}" flatpak run --user --socket=wayland --filesystem=home \
  --env=QT_QPA_PLATFORM=wayland --env=QT_SCALE_FACTOR="${SCALE:-1}" --env=PATCHY_PERF_ONSCREEN=1 --env=PATCHY_PERF_LIVE_READY="$OUT/ready" \
  --env=PATCHY_PERF_LIVE_COALESCE="$mode" --env=PATCHY_PERF_LIVE_SECONDS=120 \
  --command="$ROOT/build/linux-flatpak-qt/patchy_perf_tests" "$SDK" textdrag_live > "$OUT/perf-$tag.log" 2>&1 &
pid=$!
for _ in $(seq 240); do [ -s "$OUT/ready" ] && break; sleep 0.5; done
if [ ! -s "$OUT/ready" ]; then
  echo "not ready"; tail -20 "$OUT/perf-$tag.log"; kill "$pid"
  for hog in "${hog_pids[@]}"; do kill "$hog"; done
  exit 1
fi
read -r x y w h < "$OUT/ready"
python3 "$HERE/live-drag-moves.py" "$x" "$y" "$pace" > "$OUT/moves.txt"
start=$(date +%s.%N)
WAYLAND_DISPLAY=$WD flatpak run --user --filesystem=home --socket=wayland --command="$POP/vptr" "$SDK" "$w" "$h" < "$OUT/moves.txt"
end=$(date +%s.%N)
echo "vptr seconds: $(python3 -c "print(round($end - $start, 2))")"
wait "$pid"
for hog in "${hog_pids[@]}"; do kill "$hog"; done
grep -E "LIVE|PERF|FAIL" "$OUT/perf-$tag.log"
