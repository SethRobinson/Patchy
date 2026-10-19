#!/usr/bin/env bash
# Remote half of scripts/remote/remote-build.ps1. Runs inside ~/patchy/src right after
# the snapshot checkout: configure + build the given preset, then run the test suites.
#   build-and-test.sh <preset> [--target <t>[,<t>...]] [--filter <ui substr>]
#                     [--core-filter <core substr>] [--skip-tests]
#
# --target builds only those CMake targets instead of the preset's default target, and then
# only the suites whose executables this run built are run (patchy_core_tests,
# patchy_ui_visual_tests, and on macOS patchy_mac_a11y_tests); a suite that was not built is
# reported and skipped, never run from an older binary still in the build directory.
#
# The configure and build output also goes to build/<preset>/remote-build.log, and the build
# ends with a count of the warning lines it printed. Ninja prints only what it compiled or
# linked in this run, so the count covers exactly the targets built and the sources that
# changed: zero-warning verification still needs the default target from a clean build
# directory (docs/platform.md, "Warning verification").
set -euo pipefail

USAGE="usage: build-and-test.sh <preset> [--target t1,t2] [--filter substr] [--core-filter substr] [--skip-tests]"
PRESET="${1:?$USAGE}"
shift

# linux-flatpak-qt builds and tests against the Qt Linux users actually run: the
# org.kde.Sdk the Flatpak manifest names (compiler, Qt and fonts all come from it, never
# from .deps or the host). Re-run this script inside that sandbox; /.flatpak-info marks
# being inside. The SDK is the one make-flatpak.sh already needs installed.
if [ "$PRESET" = "linux-flatpak-qt" ] && [ ! -f /.flatpak-info ]; then
  MANIFEST="$HOME/patchy/src/packaging/linux/flatpak/com.rtsoft.patchy.yml"
  SDK=$(sed -nE 's/^sdk:[[:space:]]*([A-Za-z0-9._-]+).*$/\1/p' "$MANIFEST" | head -1)
  SDK_VERSION=$(sed -nE "s/^runtime-version:[[:space:]]*'?([0-9.]+)'?.*$/\1/p" "$MANIFEST" | head -1)
  if [ -z "$SDK" ] || [ -z "$SDK_VERSION" ]; then
    echo "ERROR: could not read sdk and runtime-version from $MANIFEST" >&2
    exit 1
  fi
  echo "== entering $SDK//$SDK_VERSION =="
  exec flatpak run --user --filesystem=home --command=bash "$SDK//$SDK_VERSION" \
    "$HOME/patchy/src/scripts/remote/build-and-test.sh" "$PRESET" "$@"
fi

FILTER=""
CORE_FILTER=""
TARGETS=""
SKIP_TESTS=0
while [ $# -gt 0 ]; do
  case "$1" in
    --filter) FILTER="${2:?--filter needs a value}"; shift 2 ;;
    --core-filter) CORE_FILTER="${2:?--core-filter needs a value}"; shift 2 ;;
    --target) TARGETS="${2:?--target needs a value}"; shift 2 ;;
    --skip-tests) SKIP_TESTS=1; shift ;;
    *) echo "unknown arg: $1" >&2; echo "$USAGE" >&2; exit 2 ;;
  esac
done
# Comma list -> words for cmake --target and for the suite checks below.
TARGET_LIST=$(printf '%s' "$TARGETS" | tr ',' ' ')
built() {
  # Without --target the default target builds every suite.
  [ -z "$TARGET_LIST" ] && return 0
  case " $TARGET_LIST " in *" $1 "*) return 0 ;; esac
  return 1
}

# Host tools and the sudo-free dependency sysroot (see setup-linux.sh: dev packages
# extracted with dpkg-deb -x when apt install wasn't possible) are for native builds
# only. Inside the Flatpak SDK they would shadow its cmake, ninja and libraries.
if [ ! -f /.flatpak-info ]; then
  export PATH="$HOME/.patchy-tools/bin:$HOME/.local/bin:$PATH"
fi
if [ ! -f /.flatpak-info ] && [ -d "$HOME/.patchy-tools/sysroot/usr" ]; then
  export CMAKE_PREFIX_PATH="$HOME/.patchy-tools/sysroot/usr${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
  export LD_LIBRARY_PATH="$HOME/.patchy-tools/sysroot/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

cd "$HOME/patchy/src"
# Every core but four (at least one), so the remote machine stays usable.
JOBS=$(( $(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 5) - 4 ))
[ "$JOBS" -ge 1 ] || JOBS=1

BUILD_DIR="build/$PRESET"
BUILD_LOG="$BUILD_DIR/remote-build.log"
TARGET_ARGS=()
TARGET_NOTE="default target"
if [ -n "$TARGET_LIST" ]; then
  # shellcheck disable=SC2206 # target names are plain words (remote-build.ps1 checks them)
  TARGET_ARGS=(--target $TARGET_LIST)
  TARGET_NOTE="targets $TARGET_LIST"
fi

mkdir -p "$BUILD_DIR"
echo "== configure ($PRESET) =="
cmake --preset "$PRESET" 2>&1 | tee "$BUILD_LOG"
echo "== build ($PRESET, $TARGET_NOTE, -j $JOBS) =="
# pipefail keeps the build's exit status through tee. The ${X[@]+...} form expands an empty
# array under set -u on macOS's bash 3.2. The pattern avoids \b, which BSD grep lacks.
BUILD_STATUS=0
nice -n 10 cmake --build --preset "$PRESET" -j "$JOBS" ${TARGET_ARGS[@]+"${TARGET_ARGS[@]}"} 2>&1 | tee -a "$BUILD_LOG" || BUILD_STATUS=$?
WARNING_RE='(^|[^a-z])warning([^a-z]|$)'
WARNINGS=$(grep -ciE "$WARNING_RE" "$BUILD_LOG" || true)
echo "== warnings printed by this build: $WARNINGS ($PRESET, $TARGET_NOTE; only what this run compiled and linked) =="
grep -iE "$WARNING_RE" "$BUILD_LOG" | sort -u | head -40 | sed 's/^/  /' || true
[ "$BUILD_STATUS" = "0" ] || exit "$BUILD_STATUS"

if [ "$SKIP_TESTS" = "1" ]; then
  echo "== tests skipped =="
  exit 0
fi

if ! built patchy_core_tests && ! built patchy_ui_visual_tests && ! built patchy_mac_a11y_tests; then
  echo "== no test suite among the built targets; tests skipped =="
  exit 0
fi

if built patchy_core_tests; then
  echo "== patchy_core_tests $CORE_FILTER =="
  if [ -n "$CORE_FILTER" ]; then
    (cd "$BUILD_DIR" && nice -n 10 ./patchy_core_tests "$CORE_FILTER")
  else
    (cd "$BUILD_DIR" && nice -n 10 ./patchy_core_tests)
  fi
else
  echo "== patchy_core_tests not built in this run; skipped =="
fi

if built patchy_ui_visual_tests; then
  echo "== patchy_ui_visual_tests (offscreen) $FILTER =="
  if [ -n "$FILTER" ]; then
    (cd "$BUILD_DIR" && QT_QPA_PLATFORM=offscreen nice -n 10 ./patchy_ui_visual_tests "$FILTER")
  else
    (cd "$BUILD_DIR" && QT_QPA_PLATFORM=offscreen nice -n 10 ./patchy_ui_visual_tests)
  fi
else
  echo "== patchy_ui_visual_tests not built in this run; skipped =="
fi

# GitHub issue 95: the shipped cocoa plugin is a patched build (docs/building.md, "Patched
# Qt cocoa plugin"). The setup script is a no-op until the patch changes; the test needs
# the real cocoa platform, which the logged-in console session provides.
if [ "$(uname -s)" = "Darwin" ] && built patchy_mac_a11y_tests; then
  echo "== patched Qt cocoa plugin =="
  bash scripts/setup-qt-cocoa-patch-mac.sh
  echo "== patchy_mac_a11y_tests (cocoa) =="
  (cd "$BUILD_DIR" && ./patchy_mac_a11y_tests "$PWD/../../.deps/qt-cocoa-6.8.3-patched")
elif [ "$(uname -s)" = "Darwin" ]; then
  echo "== patchy_mac_a11y_tests not built in this run; skipped =="
fi
