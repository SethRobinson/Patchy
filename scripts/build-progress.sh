#!/usr/bin/env bash
# Live x/y progress of a Patchy build and of the Raven external project inside it
# (cmake/patchy_raven.cmake), on this machine or inside the AppImage build container.
#
#   scripts/build-progress.sh [options] [build-dir]
#     build-dir          default build/linux-dev; with --container, /src/build/appimage
#                        (a path inside the container, where the repository is /src)
#     --container[=ID]   read the build inside a running podman container: the newest one
#                        made from localhost/patchy-appimage-build* (make-appimage.sh),
#                        or the container ID or name given
#     -i, --interval S   redraw every S seconds (default 2)
#     --once             print one snapshot and exit
#
# Progress comes from ninja itself (`ninja -n` counts what is left, `ninja -t commands`
# what there is), so it needs no build log and costs nothing in the build. Raven's
# provisioning phase (scripts/provision-*.py) prints no counter; it is shown with the
# number of MinGW compilers running. Steps CMake runs on every build (the always-built
# helper, the build stamp and what they relink) never count as done, so "idle" means no
# ninja is running. Ctrl+C quits. Timings for later comparison: scripts/build-metrics.py.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
INTERVAL=2
ONCE=0
CONTAINER=""
USE_CONTAINER=0
BUILD=""

while [ $# -gt 0 ]; do
  case "$1" in
    --container) USE_CONTAINER=1 ;;
    --container=*) USE_CONTAINER=1; CONTAINER="${1#--container=}" ;;
    -i|--interval) INTERVAL="${2:?--interval needs seconds}"; shift ;;
    --once) ONCE=1 ;;
    -h|--help) sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) echo "unknown option $1 (see --help)" >&2; exit 2 ;;
    *) BUILD="$1" ;;
  esac
  shift
done

if [ "$USE_CONTAINER" = 1 ]; then
  command -v podman >/dev/null || { echo "podman is not installed" >&2; exit 1; }
  if [ -z "$CONTAINER" ]; then
    CONTAINER=$(podman ps --sort created --format '{{.ID}} {{.Image}}' |
      awk '$2 ~ /patchy-appimage-build/ {id=$1} END {print id}')
    [ -n "$CONTAINER" ] || { echo "no running patchy-appimage-build container" >&2; exit 1; }
  fi
  BUILD="${BUILD:-/src/build/appimage}"
else
  BUILD="${BUILD:-$ROOT/build/linux-dev}"
fi

# ninja and file access where the build lives: inside the container, the tree is read
# through podman exec (its paths are the container's, /src being the repository).
in_build_env() {
  if [ "$USE_CONTAINER" = 1 ]; then podman exec "$CONTAINER" "$@"; else "$@"; fi
}
cache_value() {
  in_build_env sed -n "s/^$1:[A-Z]*=//p" "$BUILD/CMakeCache.txt" 2>/dev/null | head -1
}

# "done total" for a ninja tree (and optional target); fails when the tree is not there.
# The dry run reads a throwaway manifest that only includes build.ninja: build.ninja itself
# is a target (CMake's re-run and CONFIGURE_DEPENDS glob check), so a dry run on it stops
# at "Re-running CMake" instead of counting the real work.
ninja_progress() {
  local dir=$1 target=${2:-} out total left first
  out=$(in_build_env sh -c 'test -f "$1/build.ninja" || exit 3
    m=$(mktemp) || exit 3; echo "include build.ninja" > "$m"
    printf "total %s\n" "$(ninja -C "$1" -f "$m" -t commands $2 2>/dev/null | wc -l)"
    ninja -C "$1" -f "$m" -n $2 2>/dev/null | grep -m1 -E "^\[[0-9]+/[0-9]+\]"
    rm -f "$m"' sh "$dir" "$target") || return 1
  total=$(sed -n 's/^total //p' <<<"$out")
  first=$(grep -m1 -oE '^\[[0-9]+/[0-9]+\]' <<<"$out")
  if [ -n "$first" ]; then left=${first#*/}; left=${left%]}; else left=0; fi
  [ "$left" -gt "$total" ] && total=$left
  echo "$(( total - left )) $total"
}

bar() {  # bar done total width
  local d=$1 t=$2 w=${3:-40} f=0
  [ "$t" -gt 0 ] && f=$(( w * d / t ))
  [ "$f" -gt "$w" ] && f=$w
  printf '['; printf '%*s' "$f" '' | tr ' ' '#'; printf '%*s' $(( w - f )) '' | tr ' ' '.'; printf ']'
}
fmt_time() { printf '%02d:%02d' $(( $1 / 60 )) $(( $1 % 60 )); }

START=$(date +%s)
declare -A first_done first_time
eta() {  # eta key done total -> remaining time from the rate since the first snapshot
  local key=$1 d=$2 t=$3 now; now=$(date +%s)
  if [ -z "${first_time[$key]:-}" ]; then first_time[$key]=$now; first_done[$key]=$d; fi
  local dt=$(( now - first_time[$key] )) dd=$(( d - first_done[$key] ))
  if [ "$dt" -gt 5 ] && [ "$dd" -gt 0 ]; then fmt_time $(( (t - d) * dt / dd )); else printf -- '--:--'; fi
}

line() {  # line label done total
  local label=$1 d=$2 t=$3 pct=0
  [ "$t" -gt 0 ] && pct=$(( 100 * d / t ))
  printf '%-8s %s %5d/%-5d %3d%%   ETA %s\n' "$label" "$(bar "$d" "$t")" "$d" "$t" "$pct" "$(eta "$label" "$d" "$t")"
}

snapshot() {
  local complete=1 where="this machine"
  [ "$USE_CONTAINER" = 1 ] && where="container $CONTAINER"
  [ "$ONCE" = 1 ] || printf '\033[H\033[J'
  printf 'Patchy build progress   %s   %s   watching %s\n%s\n\n' "$(date +%T)" "$where" \
    "$(fmt_time $(( $(date +%s) - START )))" "$BUILD"

  local p
  if p=$(ninja_progress "$BUILD"); then
    set -- $p
    line Patchy "$1" "$2"
    [ "$1" -lt "$2" ] && complete=0
  else
    echo "Patchy   not configured ($BUILD/build.ninja missing)"
  fi

  local work; work=$(cache_value PATCHY_RAVEN_WORK_DIR)
  if [ -n "$work" ]; then
    local gcc; gcc=$(pgrep -fc 'w64-mingw32-(gcc|g\+\+)' 2>/dev/null || echo 0)
    if pgrep -f "provision-rtl.py" >/dev/null; then
      echo "Raven    phase 1/3  guest RTL                ${gcc} MinGW compilers running"; complete=0
    elif pgrep -f "provision-controls.py" >/dev/null; then
      echo "Raven    phase 2/3  guest DLLs and programs  ${gcc} MinGW compilers running"; complete=0
    elif p=$(ninja_progress "$work/build" raven); then
      set -- $p
      line Raven "$1" "$2"
      [ "$1" -lt "$2" ] && complete=0
    else
      echo "Raven    waiting for its configure step"; complete=0
    fi
  fi

  # Host-visible processes cover the container too (rootless podman runs them as us).
  echo
  if [ "$complete" = 1 ]; then
    echo "Both builds are complete."
  elif ! pgrep -x ninja >/dev/null; then
    echo "Idle: no ninja is running (what is left runs on the next build)."
  fi
  [ "$ONCE" = 1 ] || printf '\nCtrl+C to quit'
  return 0
}

if [ "$ONCE" = 1 ]; then snapshot; exit 0; fi
trap 'printf "\n"; exit 0' INT
while true; do snapshot; sleep "$INTERVAL"; done
