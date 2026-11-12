#!/usr/bin/env bash
# Live canvas drag timing on the headless sway rig that run.sh left running (KEEP=1), for
# GitHub #678 (Path Select shape drag on a large photo) and #705 (Gradient Fill end handle)
# (docs/interactive-previews.md, "Drag move coalescing"). Runs `patchy_perf_tests
# canvasdrag_live` full screen on the rig and drags with the virtual pointer: the moves of
# live-drag-moves.py (3000 on a wavy path), then a release.
#   live-canvas-drag.sh <shape|gradient|pathtransform> <on|off> [pace] [hogs]
#   on|off   drag move coalescing (off is the baseline without it)
#   pace N   a 1 ms sleep after every N moves (0 floods)
#   hogs N   pins the app to CPU 3 beside N busy loops there, like a slower machine
# NATIVE=1 runs build/linux-release (the aqt Qt 6.8.3 the AppImage ships) on the host instead
# of build/linux-flatpak-qt in the Flatpak SDK. DOC=WxH sets the document's size; PERF_ENV="A=1 B=2"
# passes more variables to the app. Logs land in build/wayland-popups/live-canvas-drag/.
set -u
export XDG_RUNTIME_DIR=/run/user/$(id -u)
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
[ -n "$ROOT" ] && [ -f "$ROOT/CMakeLists.txt" ] || { echo "ERROR: could not find the repository root" >&2; exit 1; }
POP=$ROOT/build/wayland-popups
WD=$(cat "$POP/display")
SDK="org.kde.Sdk//$(sed -nE "s/^runtime-version:[[:space:]]*'?([0-9.]+)'?.*$/\1/p" "$ROOT/packaging/linux/flatpak/com.rtsoft.patchy.yml" | head -1)"
OUT=$ROOT/build/wayland-popups/live-canvas-drag
mkdir -p "$OUT"
kind=$1
mode=$2
pace=${3:-0}
hogs=${4:-0}
tag="$kind-$mode-$pace-$hogs${NATIVE:+-native}${PERF_ENV:+-env}"
hog_pids=()
for _ in $(seq "$hogs"); do taskset -c 3 sh -c 'while :; do :; done' & hog_pids+=($!); done
PIN=()
[ "$hogs" -gt 0 ] && PIN=(taskset -c 3)
rm -f "$OUT/ready"
EXTRA=()
FLATPAK_EXTRA=()
for assignment in ${PERF_ENV:-}; do EXTRA+=("$assignment"); FLATPAK_EXTRA+=("--env=$assignment"); done
if [ -n "${NATIVE:-}" ]; then
  (cd "$ROOT/build/linux-release" && WAYLAND_DISPLAY=$WD QT_QPA_PLATFORM=wayland PATCHY_PERF_ONSCREEN=1 \
    PATCHY_PERF_LIVE_READY="$OUT/ready" PATCHY_PERF_LIVE_COALESCE="$mode" PATCHY_PERF_LIVE_SECONDS=180 \
    PATCHY_PERF_CANVASDRAG_KIND="$kind" PATCHY_PERF_CANVASDRAG_DOC="${DOC:-}" \
    "${PIN[@]}" env "${EXTRA[@]}" ./patchy_perf_tests canvasdrag_live) > "$OUT/perf-$tag.log" 2>&1 &
else
  WAYLAND_DISPLAY=$WD "${PIN[@]}" flatpak run --user --socket=wayland --filesystem=home \
    --env=QT_QPA_PLATFORM=wayland --env=PATCHY_PERF_ONSCREEN=1 --env=PATCHY_PERF_LIVE_READY="$OUT/ready" \
    --env=PATCHY_PERF_LIVE_COALESCE="$mode" --env=PATCHY_PERF_LIVE_SECONDS=180 \
    --env=PATCHY_PERF_CANVASDRAG_KIND="$kind" --env=PATCHY_PERF_CANVASDRAG_DOC="${DOC:-}" "${FLATPAK_EXTRA[@]}" \
    --command="$ROOT/build/linux-flatpak-qt/patchy_perf_tests" "$SDK" canvasdrag_live > "$OUT/perf-$tag.log" 2>&1 &
fi
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
