#!/usr/bin/env bash
# Configure, build, and test the desktop GPU-capable Patchy binary.
# Usage: scripts/build-gpu.sh [preset] [--skip-tests]
set -euo pipefail

PRESET="${PATCHY_PRESET:-qt-local}"
SKIP_TESTS=0
if [[ $# -gt 0 && "$1" != -* ]]; then
  PRESET="$1"
  shift
fi
while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-tests) SKIP_TESTS=1; shift ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

JOBS="${PATCHY_BUILD_JOBS:-$(command -v nproc >/dev/null 2>&1 && nproc || echo 4)}"
ENABLE_WEBGPU="${PATCHY_ENABLE_WEBGPU:-ON}"

CMAKE_ARGS=(
  -DPATCHY_ENABLE_GPU_CANVAS=ON
  -DPATCHY_ENABLE_WEBGPU="${ENABLE_WEBGPU}"
)

# Explicit prefixes are useful when the machine has a system Qt plus a local
# Qt or Dawn installation. The preset remains the source of defaults otherwise.
if [[ -n "${PATCHY_QT_PREFIX:-}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${PATCHY_QT_PREFIX}")
  CMAKE_ARGS+=("-DQt6_DIR=${PATCHY_QT_PREFIX}/lib/cmake/Qt6")
fi
if [[ -n "${PATCHY_DAWN_PREFIX:-}" ]]; then
  CMAKE_ARGS+=("-DDawn_DIR=${PATCHY_DAWN_PREFIX}/lib/cmake/Dawn")
  CMAKE_ARGS+=("-Dwebgpu_dawn_DIR=${PATCHY_DAWN_PREFIX}/lib/cmake/webgpu_dawn")
  if [[ -z "${PATCHY_QT_PREFIX:-}" ]]; then
    CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${PATCHY_DAWN_PREFIX}")
  else
    CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${PATCHY_QT_PREFIX};${PATCHY_DAWN_PREFIX}")
  fi
fi

cd "$(dirname "$0")/.."
echo "== configure (${PRESET}) =="
cmake --preset "$PRESET" "${CMAKE_ARGS[@]}"
echo "== build (${PRESET}, ${JOBS} jobs) =="
cmake --build --preset "$PRESET" --parallel "$JOBS"

if [[ "$SKIP_TESTS" == 1 ]]; then
  echo "== tests skipped =="
  exit 0
fi

case "$PRESET" in
  qt-local) BUILD_DIR="build/app" ;;
  *) BUILD_DIR="build/${PRESET}" ;;
esac

if [[ -x "${BUILD_DIR}/patchy_core_tests" ]]; then
  echo "== core tests =="
  ctest --test-dir "$BUILD_DIR" --output-on-failure
else
  echo "No test executable was produced in ${BUILD_DIR}; the selected preset may have skipped tests."
fi

if [[ -x "${BUILD_DIR}/patchy_ui_visual_tests" ]]; then
  echo "== GPU backend selection test =="
  QT_QPA_PLATFORM=offscreen "${BUILD_DIR}/patchy_ui_visual_tests" \
    ui_canvas_renderer_selects_a_safe_runtime_backend
fi

echo "GPU build completed: ${BUILD_DIR}"
