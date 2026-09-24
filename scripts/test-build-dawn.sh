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

grep -Fq 'outputOrigin: vec2<i32>' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'let localCoord = vec2<i32>(invocation.xy);' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'textureLoad(backdropTexture, localCoord, 0)' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'textureStore(outputTexture, localCoord' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'fn blendIfThresholdAlphaByte' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'sourceAlpha = sourceAlpha * ((1.0 - backdropAlpha) + backdropAlpha * underlyingFactor);' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'return f32((sourceByte * backdropByte) / 255u) / 255.0;' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'acquire_scratch_texture' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'last_metrics_.source_upload_bytes' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'mask_revision' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'last_metrics_.queue_submissions' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'cached.bind_groups' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'uniforms_by_tile' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'std::vector<PendingTile>' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'wgpuCommandEncoderFinish(encoder.get()' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'wgpuCommandEncoderCopyTextureToBuffer(encoder.get()' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq '++last_metrics_.queue_submissions' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'static_assert(sizeof(Params) == 192);' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'compose_tiles(const CanvasGpuDocument& document' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'wgpuCommandEncoderCopyTextureToBuffer' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'compose_incremental(const CanvasGpuDocument& document' \
  "$REPO_ROOT/src/ui/webgpu_render_backend.cpp"
grep -Fq 'last_readback_bytes_' \
  "$REPO_ROOT/src/ui/webgpu_render_backend.cpp"
grep -Fq 'adapter_vendor_id_ = info.vendorID' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'observation.external_image_api_available = true' \
  "$REPO_ROOT/src/ui/webgpu_document_compositor.cpp"
grep -Fq 'adapter-match=%12' \
  "$REPO_ROOT/src/ui/vulkan_qt_interop_probe.cpp"
grep -Fq 'native-device-adoption=no' \
  "$REPO_ROOT/src/ui/vulkan_qt_interop_probe.cpp"

echo 'Dawn helper syntax and lock checks passed'
