#!/usr/bin/env bash
# Remote half of scripts/remote/remote-build.ps1. Runs inside ~/patchy/src right after
# the snapshot checkout: configure + build the given preset, then run both test suites.
#   build-and-test.sh <preset> [--filter <substr>] [--skip-tests]
set -euo pipefail

PRESET="${1:?usage: build-and-test.sh <preset> [--filter substr] [--skip-tests]}"
shift
FILTER=""
SKIP_TESTS=0
while [ $# -gt 0 ]; do
  case "$1" in
    --filter) FILTER="${2:?--filter needs a value}"; shift 2 ;;
    --skip-tests) SKIP_TESTS=1; shift ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

export PATH="$HOME/.patchy-tools/bin:$HOME/.local/bin:$PATH"

# Sudo-free dependency sysroot (see setup-linux.sh): dev packages extracted with
# dpkg-deb -x when apt install wasn't possible. Harmlessly absent elsewhere.
if [ -d "$HOME/.patchy-tools/sysroot/usr" ]; then
  export CMAKE_PREFIX_PATH="$HOME/.patchy-tools/sysroot/usr${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
  export LD_LIBRARY_PATH="$HOME/.patchy-tools/sysroot/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

cd "$HOME/patchy/src"

echo "== configure ($PRESET) =="
cmake --preset "$PRESET"
echo "== build ($PRESET) =="
cmake --build --preset "$PRESET"

if [ "$SKIP_TESTS" = "1" ]; then
  echo "== tests skipped =="
  exit 0
fi

BUILD_DIR="build/$PRESET"

echo "== patchy_core_tests =="
(cd "$BUILD_DIR" && ./patchy_core_tests)

echo "== patchy_ui_visual_tests (offscreen) =="
if [ -n "$FILTER" ]; then
  (cd "$BUILD_DIR" && QT_QPA_PLATFORM=offscreen ./patchy_ui_visual_tests "$FILTER")
else
  (cd "$BUILD_DIR" && QT_QPA_PLATFORM=offscreen ./patchy_ui_visual_tests)
fi
