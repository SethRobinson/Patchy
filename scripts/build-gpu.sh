#!/usr/bin/env bash
# Configure, build, and test the desktop GPU-capable Patchy binary.
# Usage: scripts/build-gpu.sh [preset] [--skip-tests] [--with-dawn] [--dawn-only]
set -euo pipefail

PRESET="${PATCHY_PRESET:-qt-local}"
SKIP_TESTS=0
WITH_DAWN="${PATCHY_BUILD_DAWN:-OFF}"
DAWN_ONLY=0
if [[ $# -gt 0 && "$1" != -* ]]; then
  PRESET="$1"
  shift
fi
while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-tests) SKIP_TESTS=1; shift ;;
    --with-dawn) WITH_DAWN=ON; shift ;;
    --dawn-only) WITH_DAWN=ON; DAWN_ONLY=1; shift ;;
    -h|--help)
      cat <<'EOF'
Usage: scripts/build-gpu.sh [preset] [options]

Options:
  --with-dawn   Build the pinned optional Dawn dependency before Patchy and use
                its local install prefix for CMake discovery.
  --dawn-only   Build and verify Dawn, then stop before configuring Patchy.
  --skip-tests  Build Patchy without running its test commands.

Environment:
  PATCHY_BUILD_DAWN=ON has the same effect as --with-dawn.
  PATCHY_DAWN_ROOT and PATCHY_DAWN_PREFIX override the helper's local paths.
  PATCHY_BUILD_JOBS controls both Dawn and Patchy parallelism.
EOF
      exit 0
      ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

JOBS="${PATCHY_BUILD_JOBS:-$(command -v nproc >/dev/null 2>&1 && nproc || echo 4)}"
ENABLE_WEBGPU="${PATCHY_ENABLE_WEBGPU:-ON}"

cd "$(dirname "$0")/.."
REPO_ROOT="$(pwd -P)"

if [[ "$WITH_DAWN" == "ON" ]]; then
  echo "== build pinned Dawn =="
  DAWN_ARGS=()
  if [[ -n "${PATCHY_DAWN_ROOT:-}" ]]; then
    DAWN_ARGS+=("PATCHY_DAWN_ROOT=${PATCHY_DAWN_ROOT}")
  fi
  if [[ -n "${PATCHY_DAWN_PREFIX:-}" ]]; then
    DAWN_ARGS+=("PATCHY_DAWN_PREFIX=${PATCHY_DAWN_PREFIX}")
  fi
  env PATCHY_BUILD_JOBS="$JOBS" "${DAWN_ARGS[@]}" scripts/build-dawn.sh
  if [[ "$DAWN_ONLY" == 1 ]]; then
    exit 0
  fi
  if [[ -z "${PATCHY_DAWN_PREFIX:-}" ]]; then
    DAWN_CONFIG="${PATCHY_DAWN_CONFIG:-$(printf '%s' "${PATCHY_DAWN_BUILD_TYPE:-Release}" | tr '[:upper:]' '[:lower:]')}"
    PATCHY_DAWN_PREFIX="${PATCHY_DAWN_ROOT:-${REPO_ROOT}/.deps/dawn}/install/${DAWN_CONFIG}"
  fi
fi

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
if [[ -n "${PATCHY_DAWN_ROOT:-}" ]]; then
  CMAKE_ARGS+=("-DPATCHY_DAWN_ROOT=${PATCHY_DAWN_ROOT}")
fi
if [[ -n "${PATCHY_DAWN_PREFIX:-}" ]]; then
  CMAKE_ARGS+=("-DPATCHY_DAWN_PREFIX=${PATCHY_DAWN_PREFIX}")
  if [[ -f "${PATCHY_DAWN_PREFIX}/lib/cmake/Dawn/DawnConfig.cmake" ]]; then
    CMAKE_ARGS+=("-DDawn_DIR=${PATCHY_DAWN_PREFIX}/lib/cmake/Dawn")
  fi
  if [[ -f "${PATCHY_DAWN_PREFIX}/lib/cmake/webgpu_dawn/webgpu_dawnConfig.cmake" ]]; then
    CMAKE_ARGS+=("-Dwebgpu_dawn_DIR=${PATCHY_DAWN_PREFIX}/lib/cmake/webgpu_dawn")
  fi
  if [[ -z "${PATCHY_QT_PREFIX:-}" ]]; then
    CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${PATCHY_DAWN_PREFIX}")
  else
    CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${PATCHY_QT_PREFIX};${PATCHY_DAWN_PREFIX}")
  fi
fi
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
