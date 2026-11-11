#!/usr/bin/env bash
# Sourced by build-and-test.sh (mac, the Flatpak SDK) and packaging/linux/appimage/
# build-in-container.sh (the AppImage's gcc-12). Defines patchy_warncheck, the warning check
# of docs/platform.md ("Warning verification"); scripts\remote\warncheck.ps1 is its Windows
# twin and the two behave the same:
#
#   patchy_warncheck <repo root> <preset> <jobs> [clean]
#
# Builds the preset's default target in its own build/warncheck-<preset> (never the preset's
# warm build directory), then fails when the configure or build printed any warning line.
# The folder is kept after a pass, so the next run compiles only what changed since: every
# warning a change brings is printed by the first run that compiles it. Anything that could
# hide one starts the next run from an empty folder: a run that did not pass (its warned
# objects are already built and would not be printed again), a changed compiler or Qt (a
# toolchain update warns in unchanged files), the first run, or "clean" as the fourth word.
# build/warncheck-<preset>.passed holds the toolchain fingerprint of the last pass;
# build/warncheck-<preset>.log the last run's output.

# Ninja's own [N/M] progress lines name objects (no_layer_warning_tests.cpp), never a
# diagnostic. An underscore or digit beside the word makes it part of a name. BSD grep has
# no \b, hence the explicit classes.
PATCHY_WARNCHECK_RE='(^|[^A-Za-z0-9_])warning([^A-Za-z0-9_]|$)'

patchy_warncheck_count() {
  grep -vE '^\[[0-9]+/[0-9]+\] ' "$1" | grep -ciE "$PATCHY_WARNCHECK_RE" || true
}

patchy_warncheck_list() {
  grep -vE '^\[[0-9]+/[0-9]+\] ' "$1" | grep -iE "$PATCHY_WARNCHECK_RE" | sort -u | head -40 | sed 's/^/  /' || true
}

# The compiler (its path and --version line), the CFLAGS and CXXFLAGS a first configure
# would read, and Qt (Qt6ConfigVersion.cmake) a configured build directory uses, as one
# checksum. Never fails: an unreadable part just changes it.
patchy_warncheck_fingerprint() {
  local cache="$1" cxx="" qtdir=""
  if [ -f "$cache" ]; then
    cxx=$(sed -nE 's/^CMAKE_CXX_COMPILER:[A-Z]+=(.*)$/\1/p' "$cache" | head -1)
    qtdir=$(sed -nE 's/^Qt6_DIR:[A-Z]+=(.*)$/\1/p' "$cache" | head -1)
  fi
  {
    echo "flags=${CFLAGS:-}|${CXXFLAGS:-}"
    echo "cxx=$cxx"
    if [ -n "$cxx" ] && [ -x "$cxx" ]; then "$cxx" --version 2>/dev/null | head -1; fi
    echo "qt=$qtdir"
    if [ -n "$qtdir" ] && [ -f "$qtdir/Qt6ConfigVersion.cmake" ]; then
      grep -m1 'PACKAGE_VERSION' "$qtdir/Qt6ConfigVersion.cmake" || true
    fi
  } | cksum | cut -d' ' -f1
}

patchy_warncheck() {
  local root="$1" preset="$2" jobs="$3" clean="${4:-}"
  if [ -z "$root" ] || [ ! -f "$root/CMakeLists.txt" ]; then
    echo "warncheck: not the repository root: $root" >&2; return 2
  fi
  case "$preset" in ""|*[!A-Za-z0-9_.+-]*) echo "warncheck: bad preset name: $preset" >&2; return 2 ;; esac
  local dir="$root/build/warncheck-$preset"
  local log="$root/build/warncheck-$preset.log"
  local passed="$root/build/warncheck-$preset.passed"
  local reason=""
  if [ "$clean" = "clean" ]; then reason="asked for a clean build"
  elif [ ! -f "$dir/CMakeCache.txt" ]; then reason="first run"
  elif [ ! -f "$passed" ]; then reason="the last run did not pass"
  elif [ "$(patchy_warncheck_fingerprint "$dir/CMakeCache.txt")" != "$(cat "$passed")" ]; then
    reason="the compiler or Qt changed since the last pass"
  fi
  rm -f "$passed"
  mkdir -p "$root/build"
  if [ -n "$reason" ]; then
    echo "== warncheck ($preset): clean build, $reason =="
    rm -rf "${root:?}/build/warncheck-${preset:?}"
  else
    echo "== warncheck ($preset): incremental, the last run passed with this compiler and Qt =="
  fi
  local start status=0
  start=$(date +%s)
  echo "== warncheck ($preset): configure into build/warncheck-$preset, then the default target, -j $jobs =="
  { cmake --preset "$preset" -B "$dir" 2>&1 && nice -n 10 cmake --build "$dir" -j "$jobs" 2>&1; } | tee "$log" || status=$?
  local warnings steps minutes
  warnings=$(patchy_warncheck_count "$log")
  steps=$(grep -cE '^\[[0-9]+/[0-9]+\] ' "$log" || true)
  minutes=$(( ($(date +%s) - start + 59) / 60 ))
  patchy_warncheck_list "$log"
  echo "== warncheck ($preset): $warnings warning lines, $steps build steps, $minutes min, log $log =="
  if [ "$status" != "0" ]; then echo "== WARNCHECK FAILED ($preset): the build exited $status =="; return 1; fi
  if [ "$warnings" != "0" ]; then echo "== WARNCHECK FAILED ($preset): $warnings warning lines =="; return 1; fi
  patchy_warncheck_fingerprint "$dir/CMakeCache.txt" > "$passed"
  echo "== WARNCHECK PASSED ($preset) =="
  return 0
}
