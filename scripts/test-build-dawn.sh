#!/usr/bin/env bash
# Lightweight regression checks for the reproducible Dawn helper.
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)"

bash -n "$REPO_ROOT/scripts/build-dawn.sh"
bash -n "$REPO_ROOT/scripts/build-gpu.sh"

TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/patchy-dawn-test.XXXXXX")"
trap 'rm -rf -- "$TEST_ROOT"' EXIT
CONFIG="$(PATCHY_DAWN_ROOT="$TEST_ROOT" \
  "$REPO_ROOT/scripts/build-dawn.sh" --print-config)"

printf '%s\n' "$CONFIG" | grep -Fq 'PATCHY_DAWN_REPOSITORY=https://dawn.googlesource.com/dawn'
printf '%s\n' "$CONFIG" | grep -Eq '^PATCHY_DAWN_COMMIT=[0-9a-f]{40}$'
printf '%s\n' "$CONFIG" | grep -Fq 'PATCHY_DAWN_PLATFORM='
printf '%s\n' "$CONFIG" | grep -Fq 'PATCHY_DAWN_PREFIX='

grep -Eq '^set\(PATCHY_DAWN_COMMIT "[0-9a-f]{40}"\)$' \
  "$REPO_ROOT/cmake/dawn-version.cmake"
if grep -Eq 'set\(PATCHY_DAWN_(REPOSITORY|COMMIT) "(main|master|HEAD|latest)"\)' \
  "$REPO_ROOT/cmake/dawn-version.cmake"; then
  echo 'mutable Dawn revision found in lock file' >&2
  exit 1
fi

grep -Fq 'var sourceAlpha = clamp(sourceSample.a * params.layerOpacity * coverage, 0.0, 1.0);' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
if grep -Fq 'let sourceAlpha = clamp(sourceSample.a * params.layerOpacity * coverage, 0.0, 1.0);' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"; then
  echo 'WGSL sourceAlpha must be mutable because Blend If updates it' >&2
  exit 1
fi

grep -Fq 'if (width <= 0 || height <= 0)' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
if grep -Fq 'static_cast<int>(std::numeric_limits<uint32_t>::max())' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"; then
  echo 'WebGPU dimensions must not compare int sizes with a narrowed uint32_t maximum' >&2
  exit 1
fi

echo 'Dawn helper syntax and lock checks passed'
