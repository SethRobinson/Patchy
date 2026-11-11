#!/usr/bin/env bash
# Remote half of scripts/remote/remote-build.ps1. Runs inside ~/patchy/src right after
# the snapshot checkout: configure + build the given preset, then run the test suites.
#   build-and-test.sh <preset> [--target <t>[,<t>...]] [--filter <ui substr>]
#                     [--core-filter <core substr>] [--skip-tests]
#   build-and-test.sh <preset> --warncheck [--clean]
#
# --warncheck runs the warning check instead (warncheck.sh, docs/platform.md "Warning
# verification"): the preset's default target in its own build/warncheck-<preset>, failing on
# any warning line, no tests; --clean starts it from an empty folder. The linux-appimage
# preset runs it in the AppImage's container (packaging/linux/make-appimage.sh --warncheck).
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

USAGE="usage: build-and-test.sh <preset> [--target t1,t2] [--filter substr] [--core-filter substr] [--skip-tests] | <preset> --warncheck [--clean]"
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
WARNCHECK=0
CLEAN=""
while [ $# -gt 0 ]; do
  case "$1" in
    --filter) FILTER="${2:?--filter needs a value}"; shift 2 ;;
    --core-filter) CORE_FILTER="${2:?--core-filter needs a value}"; shift 2 ;;
    --target) TARGETS="${2:?--target needs a value}"; shift 2 ;;
    --skip-tests) SKIP_TESTS=1; shift ;;
    --warncheck) WARNCHECK=1; shift ;;
    --clean) CLEAN=clean; shift ;;
    *) echo "unknown arg: $1" >&2; echo "$USAGE" >&2; exit 2 ;;
  esac
done
if [ "$WARNCHECK" = "1" ] && [ -n "$TARGETS$FILTER$CORE_FILTER" ]; then
  echo "--warncheck builds the default target and runs no tests" >&2; exit 2
fi
if [ -n "$CLEAN" ] && [ "$WARNCHECK" != "1" ]; then echo "--clean needs --warncheck" >&2; exit 2; fi
# The AppImage's toolchain (gcc-12, Qt 6.8.3) lives in its container, not on this host.
if [ "$WARNCHECK" = "1" ] && [ "$PRESET" = "linux-appimage" ]; then
  exec bash "$HOME/patchy/src/packaging/linux/make-appimage.sh" --warncheck ${CLEAN:+--clean}
fi
. "$HOME/patchy/src/scripts/remote/warncheck.sh"
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

if [ "$WARNCHECK" = "1" ]; then
  if [ "$PRESET" = "linux-flatpak-qt" ]; then
    # flatpak-builder compiles the package with the SDK's hardening flags, whose optimizer
    # false positives a plain preset never prints (docs/platform.md, "The flatpak build is a
    # second Linux compiler"). Take them from the last package build's own cache, else the
    # set the 6.11 SDK used; CMake reads CFLAGS and CXXFLAGS on the first configure, and a
    # change in them makes warncheck.sh start clean.
    FLATPAK_CFLAGS='-O2 -pipe -g -Wp,-D_FORTIFY_SOURCE=3 -Wp,-D_GLIBCXX_ASSERTIONS -fexceptions -fstack-protector-strong -grecord-gcc-switches -fasynchronous-unwind-tables -fstack-clash-protection -fcf-protection -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer'
    FLATPAK_CXXFLAGS="$FLATPAK_CFLAGS"
    FLATPAK_CACHE=$(ls -t "$HOME"/patchy/src/packaging/linux/.flatpak-builder/build/patchy-*/CMakeCache.txt 2>/dev/null | head -1 || true)
    if [ -n "$FLATPAK_CACHE" ]; then
      CACHED=$(sed -nE 's/^CMAKE_C_FLAGS:STRING=(.+)$/\1/p' "$FLATPAK_CACHE" | head -1)
      [ -z "$CACHED" ] || FLATPAK_CFLAGS="$CACHED"
      CACHED=$(sed -nE 's/^CMAKE_CXX_FLAGS:STRING=(.+)$/\1/p' "$FLATPAK_CACHE" | head -1)
      [ -z "$CACHED" ] || FLATPAK_CXXFLAGS="$CACHED"
    fi
    export CFLAGS="$FLATPAK_CFLAGS" CXXFLAGS="$FLATPAK_CXXFLAGS"
    echo "== warncheck: the Flatpak package's compile flags${FLATPAK_CACHE:+ (from $FLATPAK_CACHE)}: $CXXFLAGS =="
  fi
  WARNCHECK_STATUS=0
  patchy_warncheck "$HOME/patchy/src" "$PRESET" "$JOBS" "$CLEAN" || WARNCHECK_STATUS=$?
  exit "$WARNCHECK_STATUS"
fi

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
# array under set -u on macOS's bash 3.2. The count and list skip ninja's progress lines
# (warncheck.sh), which name objects such as no_layer_warning_tests.cpp.
BUILD_STATUS=0
nice -n 10 cmake --build --preset "$PRESET" -j "$JOBS" ${TARGET_ARGS[@]+"${TARGET_ARGS[@]}"} 2>&1 | tee -a "$BUILD_LOG" || BUILD_STATUS=$?
WARNINGS=$(patchy_warncheck_count "$BUILD_LOG")
echo "== warnings printed by this build: $WARNINGS ($PRESET, $TARGET_NOTE; only what this run compiled and linked) =="
patchy_warncheck_list "$BUILD_LOG"
[ "$BUILD_STATUS" = "0" ] || exit "$BUILD_STATUS"

if [ "$SKIP_TESTS" = "1" ]; then
  echo "== tests skipped =="
  exit 0
fi

if ! built patchy_core_tests && ! built patchy_ui_visual_tests && ! built patchy_mac_a11y_tests; then
  echo "== no test suite among the built targets; tests skipped =="
  exit 0
fi

# Every suite runs even when an earlier one fails, as in build-and-test.ps1: one run reports
# every failure, and the exit code is the first failing suite's.
FIRST_FAILURE=0
note_failure() {
  if [ "$1" != "0" ] && [ "$FIRST_FAILURE" = "0" ]; then FIRST_FAILURE="$1"; fi
}

if built patchy_core_tests; then
  echo "== patchy_core_tests $CORE_FILTER =="
  SUITE_STATUS=0
  if [ -n "$CORE_FILTER" ]; then
    (cd "$BUILD_DIR" && nice -n 10 ./patchy_core_tests "$CORE_FILTER") || SUITE_STATUS=$?
  else
    (cd "$BUILD_DIR" && nice -n 10 ./patchy_core_tests) || SUITE_STATUS=$?
  fi
  note_failure "$SUITE_STATUS"
else
  echo "== patchy_core_tests not built in this run; skipped =="
fi

if built patchy_ui_visual_tests; then
  echo "== patchy_ui_visual_tests (offscreen) $FILTER =="
  SUITE_STATUS=0
  if [ -n "$FILTER" ]; then
    (cd "$BUILD_DIR" && QT_QPA_PLATFORM=offscreen nice -n 10 ./patchy_ui_visual_tests "$FILTER") || SUITE_STATUS=$?
  else
    (cd "$BUILD_DIR" && QT_QPA_PLATFORM=offscreen nice -n 10 ./patchy_ui_visual_tests) || SUITE_STATUS=$?
  fi
  note_failure "$SUITE_STATUS"
else
  echo "== patchy_ui_visual_tests not built in this run; skipped =="
fi

# GitHub issue 95: the shipped cocoa plugin is a patched build (docs/building-qt.md, "Patched
# Qt cocoa plugin"). The setup script is a no-op until the patch changes; the test needs
# the real cocoa platform, which the logged-in console session provides.
if [ "$(uname -s)" = "Darwin" ] && built patchy_mac_a11y_tests; then
  echo "== patched Qt cocoa plugin =="
  SUITE_STATUS=0
  bash scripts/setup-qt-cocoa-patch-mac.sh || SUITE_STATUS=$?
  if [ "$SUITE_STATUS" = "0" ]; then
    echo "== patchy_mac_a11y_tests (cocoa) =="
    (cd "$BUILD_DIR" && ./patchy_mac_a11y_tests "$PWD/../../.deps/qt-cocoa-6.8.3-patched") || SUITE_STATUS=$?
  fi
  note_failure "$SUITE_STATUS"
elif [ "$(uname -s)" = "Darwin" ]; then
  echo "== patchy_mac_a11y_tests not built in this run; skipped =="
fi
exit "$FIRST_FAILURE"
