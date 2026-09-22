#!/usr/bin/env bash
# Build the pinned optional Dawn/WebGPU dependency outside the Patchy tree.
# Usage: scripts/build-dawn.sh [--clean] [--print-config]
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)"
LOCK_FILE="${PATCHY_DAWN_LOCK_FILE:-${REPO_ROOT}/cmake/dawn-version.cmake}"
DAWN_ROOT="${PATCHY_DAWN_ROOT:-${REPO_ROOT}/.deps/dawn}"
DAWN_SOURCE="${PATCHY_DAWN_SOURCE:-${DAWN_ROOT}/src}"
DAWN_BUILD_TYPE="${PATCHY_DAWN_BUILD_TYPE:-Release}"
DAWN_CONFIG="${PATCHY_DAWN_CONFIG:-$(printf '%s' "$DAWN_BUILD_TYPE" | tr '[:upper:]' '[:lower:]')}"
DAWN_BUILD_DIR="${PATCHY_DAWN_BUILD_DIR:-${DAWN_ROOT}/build/${DAWN_CONFIG}}"
DAWN_INSTALL_DIR="${PATCHY_DAWN_PREFIX:-${DAWN_ROOT}/install/${DAWN_CONFIG}}"
DAWN_JOBS="${PATCHY_BUILD_JOBS:-}"
CLEAN=0
PRINT_CONFIG=0

usage() {
  cat <<'EOF'
Usage: scripts/build-dawn.sh [options]

Fetch, build, install, and verify the exact Dawn revision in
cmake/dawn-version.cmake. All generated files stay below .deps/dawn by default.

Options:
  --clean          Remove only the selected Dawn build and install directories.
  --print-config   Print the pinned revision, paths, and host backend selection;
                   do not access the network or run CMake.
  -h, --help       Show this help.

Environment:
  PATCHY_DAWN_ROOT       Workspace containing src/, build/, and install/.
  PATCHY_DAWN_SOURCE     Existing Dawn checkout, or the checkout path to create.
  PATCHY_DAWN_PREFIX      Install prefix. Defaults to .deps/dawn/install/<config>.
  PATCHY_DAWN_BUILD_TYPE  CMake build type. Defaults to Release.
  PATCHY_DAWN_CONFIG       Directory name below build/install. Defaults to lowercase type.
  PATCHY_BUILD_JOBS        Parallel build jobs. Defaults to nproc or 4.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --clean) CLEAN=1; shift ;;
    --print-config) PRINT_CONFIG=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

lock_value() {
  local key="$1"
  local value
  [[ -f "$LOCK_FILE" ]] || { echo "Dawn lock file not found: $LOCK_FILE" >&2; exit 1; }
  value="$(sed -n "s/^set(${key} \"\([^\"]*\)\")$/\1/p" "$LOCK_FILE")"
  [[ -n "$value" ]] || { echo "Dawn lock value is missing: $key" >&2; exit 1; }
  if [[ "$(printf '%s\n' "$value" | wc -l)" -ne 1 ]]; then
    echo "Dawn lock value is not unique: $key" >&2
    exit 1
  fi
  printf '%s' "$value"
}

DAWN_REPOSITORY="$(lock_value PATCHY_DAWN_REPOSITORY)"
DAWN_COMMIT="$(lock_value PATCHY_DAWN_COMMIT)"
DAWN_LOCK_FORMAT="$(lock_value PATCHY_DAWN_LOCK_FORMAT)"

[[ "$DAWN_LOCK_FORMAT" == 1 ]] || {
  echo "Unsupported Dawn lock format: $DAWN_LOCK_FORMAT" >&2
  exit 1
}
[[ "$DAWN_COMMIT" =~ ^[0-9a-f]{40}$ ]] || {
  echo "Dawn lock must contain a complete lowercase SHA-1: $DAWN_COMMIT" >&2
  exit 1
}
[[ "$DAWN_REPOSITORY" == "https://dawn.googlesource.com/dawn" ]] || {
  echo "Unexpected Dawn repository in lock file: $DAWN_REPOSITORY" >&2
  exit 1
}

host_platform() {
  case "$(uname -s)" in
    Linux*) printf 'linux' ;;
    Darwin*) printf 'macos' ;;
    MINGW*|MSYS*|CYGWIN*) printf 'windows' ;;
    *) echo "Unsupported host for the pinned Dawn helper: $(uname -s)" >&2; exit 1 ;;
  esac
}

PLATFORM="$(host_platform)"
case "$PLATFORM" in
  linux)
    BACKEND_SUMMARY='Vulkan + desktop OpenGL; null fallback; no window-surface backends'
    ;;
  macos)
    BACKEND_SUMMARY='Metal; null fallback'
    ;;
  windows)
    BACKEND_SUMMARY='D3D11 + D3D12 + Vulkan; null fallback'
    ;;
esac

if [[ "$DAWN_JOBS" == "" ]]; then
  if command -v nproc >/dev/null 2>&1; then
    DAWN_JOBS="$(nproc)"
  else
    DAWN_JOBS=4
  fi
fi
[[ "$DAWN_JOBS" =~ ^[1-9][0-9]*$ ]] || {
  echo "PATCHY_BUILD_JOBS must be a positive integer: $DAWN_JOBS" >&2
  exit 2
}

if [[ "$PRINT_CONFIG" == 1 ]]; then
  printf 'PATCHY_DAWN_REPOSITORY=%s\n' "$DAWN_REPOSITORY"
  printf 'PATCHY_DAWN_COMMIT=%s\n' "$DAWN_COMMIT"
  printf 'PATCHY_DAWN_PLATFORM=%s\n' "$PLATFORM"
  printf 'PATCHY_DAWN_BACKENDS=%s\n' "$BACKEND_SUMMARY"
  printf 'PATCHY_DAWN_SOURCE=%s\n' "$DAWN_SOURCE"
  printf 'PATCHY_DAWN_BUILD=%s\n' "$DAWN_BUILD_DIR"
  printf 'PATCHY_DAWN_PREFIX=%s\n' "$DAWN_INSTALL_DIR"
  printf 'PATCHY_BUILD_JOBS=%s\n' "$DAWN_JOBS"
  exit 0
fi

require_command() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Required command not found: $1" >&2
    exit 1
  }
}
require_command git
require_command cmake
require_command ninja
require_command python3

mkdir -p "$DAWN_ROOT" "$(dirname -- "$DAWN_SOURCE")" "$(dirname -- "$DAWN_BUILD_DIR")" "$(dirname -- "$DAWN_INSTALL_DIR")"

if [[ "$CLEAN" == 1 ]]; then
  [[ -n "$DAWN_BUILD_DIR" && "$DAWN_BUILD_DIR" != / ]] || { echo 'Refusing to clean an empty/root Dawn build path' >&2; exit 1; }
  [[ -n "$DAWN_INSTALL_DIR" && "$DAWN_INSTALL_DIR" != / ]] || { echo 'Refusing to clean an empty/root Dawn install path' >&2; exit 1; }
  rm -rf -- "$DAWN_BUILD_DIR" "$DAWN_INSTALL_DIR"
fi

if [[ ! -d "$DAWN_SOURCE/.git" && ! -f "$DAWN_SOURCE/.git" ]]; then
  if [[ -e "$DAWN_SOURCE" && -n "$(find "$DAWN_SOURCE" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
    echo "Dawn source path exists but is not a Git checkout: $DAWN_SOURCE" >&2
    exit 1
  fi
  rm -rf -- "$DAWN_SOURCE"
  echo "== clone Dawn repository (filtered, no checkout) =="
  if ! git -c http.version=HTTP/1.1 -c http.lowSpeedLimit=1024 -c http.lowSpeedTime=60 \
      clone --depth 1 --filter=blob:none --no-checkout \
      "$DAWN_REPOSITORY" "$DAWN_SOURCE"; then
    echo "Retrying Dawn clone with HTTP/2 after the first transport attempt failed" >&2
    rm -rf -- "$DAWN_SOURCE"
    git -c http.version=HTTP/2 -c http.lowSpeedLimit=1024 -c http.lowSpeedTime=60 \
      clone --depth 1 --filter=blob:none --no-checkout \
      "$DAWN_REPOSITORY" "$DAWN_SOURCE"
  fi
fi

SOURCE_TOP="$(git -C "$DAWN_SOURCE" rev-parse --show-toplevel 2>/dev/null || true)"
[[ -n "$SOURCE_TOP" ]] || { echo "Not a Dawn Git checkout: $DAWN_SOURCE" >&2; exit 1; }
SOURCE_TOP="$(CDPATH= cd -- "$SOURCE_TOP" && pwd -P)"
EXPECTED_SOURCE="$(CDPATH= cd -- "$DAWN_SOURCE" && pwd -P)"
[[ "$SOURCE_TOP" == "$EXPECTED_SOURCE" ]] || {
  echo "Dawn source path must be the checkout root: $DAWN_SOURCE" >&2
  exit 1
}
if [[ -n "$(git -C "$DAWN_SOURCE" status --porcelain)" ]]; then
  echo "Dawn source checkout has local changes; refusing to reset it: $DAWN_SOURCE" >&2
  exit 1
fi

CURRENT_COMMIT="$(git -C "$DAWN_SOURCE" rev-parse HEAD 2>/dev/null || true)"
if [[ "$CURRENT_COMMIT" != "$DAWN_COMMIT" ]]; then
  echo "== fetch Dawn ${DAWN_COMMIT} =="
  # Some enterprise proxies terminate one HTTP version while allowing the other.
  # Keep the official repository and immutable SHA, but retry with the alternate
  # protocol before reporting a transport failure to the caller.
  if ! git -C "$DAWN_SOURCE" \
      -c http.version=HTTP/1.1 \
      -c http.lowSpeedLimit=1024 \
      -c http.lowSpeedTime=60 \
      fetch --depth 1 "$DAWN_REPOSITORY" "$DAWN_COMMIT"; then
    echo "Retrying Dawn fetch with HTTP/2 after the first transport attempt failed" >&2
    git -C "$DAWN_SOURCE" \
      -c http.version=HTTP/2 \
      -c http.lowSpeedLimit=1024 \
      -c http.lowSpeedTime=60 \
      fetch --depth 1 "$DAWN_REPOSITORY" "$DAWN_COMMIT"
  fi
  git -C "$DAWN_SOURCE" checkout -q --detach "$DAWN_COMMIT"
fi
[[ "$(git -C "$DAWN_SOURCE" rev-parse HEAD)" == "$DAWN_COMMIT" ]] || {
  echo "Dawn checkout verification failed" >&2
  exit 1
}

COMMON_CMAKE_ARGS=(
  -G Ninja
  "-DCMAKE_BUILD_TYPE=${DAWN_BUILD_TYPE}"
  "-DCMAKE_INSTALL_PREFIX=${DAWN_INSTALL_DIR}"
  -DDAWN_FETCH_DEPENDENCIES=ON
  -DDAWN_ENABLE_INSTALL=ON
  -DDAWN_BUILD_MONOLITHIC_LIBRARY=STATIC
  -DBUILD_SHARED_LIBS=OFF
  -DBUILD_TESTS=OFF
  -DBUILD_SAMPLES=OFF
  -DDAWN_BUILD_TESTS=OFF
  -DDAWN_BUILD_SAMPLES=OFF
  -DDAWN_BUILD_NODE_BINDINGS=OFF
  -DDAWN_ENABLE_NULL=ON
)
case "$PLATFORM" in
  linux)
    PLATFORM_CMAKE_ARGS=(
      -DDAWN_ENABLE_VULKAN=ON
      -DDAWN_ENABLE_DESKTOP_GL=ON
      -DDAWN_ENABLE_OPENGLES=OFF
      -DDAWN_ENABLE_D3D11=OFF
      -DDAWN_ENABLE_D3D12=OFF
      -DDAWN_ENABLE_METAL=OFF
      -DDAWN_USE_X11=OFF
      -DDAWN_USE_WAYLAND=OFF
    )
    ;;
  macos)
    PLATFORM_CMAKE_ARGS=(
      -DDAWN_ENABLE_METAL=ON
      -DDAWN_ENABLE_VULKAN=OFF
      -DDAWN_ENABLE_DESKTOP_GL=OFF
      -DDAWN_ENABLE_OPENGLES=OFF
      -DDAWN_ENABLE_D3D11=OFF
      -DDAWN_ENABLE_D3D12=OFF
    )
    ;;
  windows)
    PLATFORM_CMAKE_ARGS=(
      -DDAWN_ENABLE_D3D11=ON
      -DDAWN_ENABLE_D3D12=ON
      -DDAWN_ENABLE_VULKAN=ON
      -DDAWN_ENABLE_METAL=OFF
      -DDAWN_ENABLE_DESKTOP_GL=OFF
      -DDAWN_ENABLE_OPENGLES=OFF
      -DDAWN_USE_WINDOWS_UI=ON
    )
    ;;
esac

echo "== configure Dawn ${DAWN_COMMIT} (${PLATFORM}) =="
cmake -S "$DAWN_SOURCE" -B "$DAWN_BUILD_DIR" \
  "${COMMON_CMAKE_ARGS[@]}" "${PLATFORM_CMAKE_ARGS[@]}"
echo "== build Dawn (${DAWN_JOBS} jobs) =="
cmake --build "$DAWN_BUILD_DIR" --parallel "$DAWN_JOBS"
echo "== install Dawn =="
cmake --install "$DAWN_BUILD_DIR"

PACKAGE_FILES="$(find "$DAWN_INSTALL_DIR" -type f \( -name 'DawnConfig.cmake' -o -name 'webgpu_dawnConfig.cmake' \) -print 2>/dev/null || true)"
[[ -n "$PACKAGE_FILES" ]] || {
  echo "Dawn installation did not produce a CMake package under: $DAWN_INSTALL_DIR" >&2
  exit 1
}

PROBE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/patchy-dawn-probe.XXXXXX")"
cleanup_probe() { rm -rf -- "$PROBE_DIR"; }
trap cleanup_probe EXIT
cat > "$PROBE_DIR/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(PatchyDawnPackageProbe LANGUAGES CXX)
find_package(Dawn CONFIG REQUIRED)
if(NOT TARGET dawn::webgpu_dawn AND NOT TARGET Dawn::webgpu_dawn AND NOT TARGET webgpu_dawn)
  message(FATAL_ERROR "Dawn package has no supported webgpu_dawn target")
endif()
EOF
cmake -S "$PROBE_DIR" -B "$PROBE_DIR/build" -G Ninja \
  "-DCMAKE_PREFIX_PATH=${DAWN_INSTALL_DIR}" >/dev/null

echo "Dawn build verified: ${DAWN_INSTALL_DIR}"
echo "Use PATCHY_DAWN_PREFIX=${DAWN_INSTALL_DIR} scripts/build-gpu.sh <preset> --with-dawn"
