#!/usr/bin/env bash
# One-time (idempotent) provisioning of the automatic-selection dependencies on macOS and
# Linux: the pinned ONNX Runtime CPU package and the pinned segmentation models (plus, with
# --web, the pinned onnxruntime-web package a wasm build stages), into .deps (gitignored). Every download is checked against the SHA-256 pinned below; a file
# that fails the check is deleted, never used. See docs/ai-selection.md.
#
#   bash scripts/setup-ai-deps.sh            # provisions <main checkout>/.deps
#   bash scripts/setup-ai-deps.sh /some/deps  # explicit deps folder
#   bash scripts/setup-ai-deps.sh --web       # also onnxruntime-web (wasm builds)
# Rerunning is a fast no-op once everything is present and verified.
set -euo pipefail

# Keep in sync with scripts/setup-ai-deps.ps1 and cmake/patchy_ai.cmake.
ORT_VERSION="1.30.0"
# onnxruntime-web: the npm tarball's SHA-256 and the license texts at the same tag (the npm
# package carries none).
ORT_WEB_VERSION="1.30.0"
ORT_WEB_PACKAGE_SHA256="d2228df7e4616bc3348bf504ee888f3bec43789a273f0a63f3e68d203ce3bf71"
ORT_WEB_FILES=(ort.wasm.min.mjs ort-wasm-simd-threaded.mjs ort-wasm-simd-threaded.wasm)
ORT_WEB_LICENSES=(
  "LICENSE|2f07c72751aed99790b8a4869cf2311df85a860b22ded05fa22803587a48922c"
  "ThirdPartyNotices.txt|143764b952fdb1a7c69ce653bfba74a7744d6a8a573bfb73e235fba356c83de3"
)
# The models Patchy ships: converted from the pinned upstream fp32 files by
# scripts/ai/quantize_models.py (byte-reproducible) and hosted on the ai-models-N releases.
# MobileSAM is what builds bundle. EfficientViT-SAM (ai-models-2) is Object Selection's High
# Quality model: users download it in the app; dev trees fetch it so tests can stage it.
MODELS=(
  "efficientvit_sam_xl0_encoder_fp16.onnx|https://github.com/SethRobinson/Patchy/releases/download/ai-models-2/efficientvit_sam_xl0_encoder_fp16.onnx|a28cf6978672e202b597e948f71c09a31267ee947d26579f6ca290281e5bf101"
  "efficientvit_sam_xl0_decoder_multi_fp16.onnx|https://github.com/SethRobinson/Patchy/releases/download/ai-models-2/efficientvit_sam_xl0_decoder_multi_fp16.onnx|632613ec55ebd52a72f0a1933c297ce67b8563ff2e970b0c2eea059373ba496b"
  # TrustMark watermark models (docs/watermarks.md): Adobe's fp16 ONNX files, MIT.
  "trustmark_decoder_Q.onnx|https://cai-watermark.adobe.net/watermarking/trustmark-models/decoder_Q.onnx|ee3268f057c9dabef680e169302f5973d0589feea86189ed229a896cc3aa88df"
  "trustmark_decoder_P.onnx|https://cai-watermark.adobe.net/watermarking/trustmark-models/decoder_P.onnx|be6d7c33f8a7b376f179e75f3f7c58ff816a9ac7bb6d37fd0a729a635f624c35"
  "trustmark_decoder_C.onnx|https://cai-watermark.adobe.net/watermarking/trustmark-models/decoder_C.onnx|3d92f4a278bcf87ac7dc40248c36ae664fe5fdedfd19ca265f7ca730b271100a"
  "trustmark_encoder_Q.onnx|https://cai-watermark.adobe.net/watermarking/trustmark-models/encoder_Q.onnx|19b3d1b25836130ffd78775a8f61539f993375d1823ef0e59ba5b8dffb4f892d"
  "mobile_sam_image_encoder_int8.onnx|https://github.com/SethRobinson/Patchy/releases/download/ai-models-1/mobile_sam_image_encoder_int8.onnx|6af3f249f7a5a6a8a3ef988b6814015e8c3a300977c135d1f9572ed2f2930cd7"
  "sam_mask_decoder_multi_int8.onnx|https://github.com/SethRobinson/Patchy/releases/download/ai-models-1/sam_mask_decoder_multi_int8.onnx|aa1410bec8f050b6017fba29ca07f16317029a8f74a98e5998aeed46680c10a6"
)
# Select Subject is held back pending patent review. Do not download BiRefNet;
# existing local copies are left alone and the default build never stages them.

case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) ORT_PLATFORM="osx-arm64"; ORT_SHA256="6ebb5062a934537c352937821f9fe9718e7de1a2db1122a93dd363ffd53a7012" ;;
  Linux-x86_64) ORT_PLATFORM="linux-x64"; ORT_SHA256="a5ed5a3cac51fbb2e90da632ae43d19212faaa20e76484e62bcb7c23ddb3b3fd" ;;
  Linux-aarch64) ORT_PLATFORM="linux-aarch64"; ORT_SHA256="e16a27a8ed330bbc698df7330b0cf56e722f354e3bcc92118682c74ef3c3e3da" ;;
  *) echo "setup-ai-deps: unsupported platform $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

WEB=0
if [[ "${1:-}" == "--web" ]]; then
  WEB=1
  shift
fi
if [[ $# -ge 1 ]]; then
  DEPS_DIR="$1"
else
  COMMON_DIR="$(git -C "$(dirname "$0")" rev-parse --path-format=absolute --git-common-dir)"
  MAIN_CHECKOUT="$(dirname "$COMMON_DIR")"
  [[ -f "$MAIN_CHECKOUT/CMakeLists.txt" ]] || { echo "main checkout not found at $MAIN_CHECKOUT; pass a deps folder" >&2; exit 1; }
  DEPS_DIR="$MAIN_CHECKOUT/.deps"
fi
mkdir -p "$DEPS_DIR"

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

# Downloads to a .part sibling, verifies, then renames, so an interrupted run never leaves a
# file a later run would mistake for a verified one.
fetch_verified() {
  local url="$1" destination="$2" expected="$3"
  if [[ -f "$destination" ]]; then
    [[ "$(sha256_of "$destination")" == "$expected" ]] && return 0
    echo "checksum mismatch, downloading again: $destination"
    rm -f "$destination"
  fi
  echo "downloading $url"
  curl -fL --retry 3 -o "$destination.part" "$url"
  local actual
  actual="$(sha256_of "$destination.part")"
  if [[ "$actual" != "$expected" ]]; then
    rm -f "$destination.part"
    echo "SHA-256 mismatch for $url (expected $expected, got $actual)" >&2
    exit 1
  fi
  mv "$destination.part" "$destination"
}

echo "== ONNX Runtime $ORT_VERSION ($ORT_PLATFORM) =="
ORT_ROOT="$DEPS_DIR/onnxruntime/$ORT_VERSION"
ORT_DIR="$ORT_ROOT/$ORT_PLATFORM"
if [[ ! -f "$ORT_DIR/include/onnxruntime_cxx_api.h" ]]; then
  mkdir -p "$ORT_ROOT"
  ARCHIVE="$ORT_ROOT/onnxruntime-$ORT_PLATFORM-$ORT_VERSION.tgz"
  fetch_verified "https://github.com/microsoft/onnxruntime/releases/download/v$ORT_VERSION/onnxruntime-$ORT_PLATFORM-$ORT_VERSION.tgz" \
    "$ARCHIVE" "$ORT_SHA256"
  rm -rf "${DEPS_DIR:?}/onnxruntime/$ORT_VERSION/$ORT_PLATFORM"
  tar -xzf "$ARCHIVE" -C "$ORT_ROOT"
  mv "$ORT_ROOT/onnxruntime-$ORT_PLATFORM-$ORT_VERSION" "$ORT_DIR"
  rm -f "$ARCHIVE"
fi
echo "ONNX Runtime ready at $ORT_DIR"

echo "== Segmentation models =="
MODEL_DIR="$DEPS_DIR/ai-models"
mkdir -p "$MODEL_DIR"
for entry in "${MODELS[@]}"; do
  IFS='|' read -r name url sha <<<"$entry"
  fetch_verified "$url" "$MODEL_DIR/$name" "$sha"
done
echo "models ready at $MODEL_DIR"

if [[ "$WEB" == 1 ]]; then
  echo "== onnxruntime-web $ORT_WEB_VERSION =="
  WEB_DIR="$DEPS_DIR/onnxruntime-web/$ORT_WEB_VERSION"
  mkdir -p "$WEB_DIR"
  web_ready=1
  for name in "${ORT_WEB_FILES[@]}"; do [[ -f "$WEB_DIR/$name" ]] || web_ready=0; done
  if [[ "$web_ready" == 0 ]]; then
    ARCHIVE="$WEB_DIR/onnxruntime-web-$ORT_WEB_VERSION.tgz"
    fetch_verified "https://registry.npmjs.org/onnxruntime-web/-/onnxruntime-web-$ORT_WEB_VERSION.tgz" "$ARCHIVE" \
      "$ORT_WEB_PACKAGE_SHA256"
    rm -rf "${DEPS_DIR:?}/onnxruntime-web/$ORT_WEB_VERSION/staging"
    mkdir -p "$WEB_DIR/staging"
    members=()
    for name in "${ORT_WEB_FILES[@]}"; do members+=("package/dist/$name"); done
    tar -xzf "$ARCHIVE" -C "$WEB_DIR/staging" "${members[@]}"
    for name in "${ORT_WEB_FILES[@]}"; do mv -f "$WEB_DIR/staging/package/dist/$name" "$WEB_DIR/$name"; done
    rm -rf "${DEPS_DIR:?}/onnxruntime-web/$ORT_WEB_VERSION/staging"
    rm -f "$ARCHIVE"
  fi
  for entry in "${ORT_WEB_LICENSES[@]}"; do
    IFS='|' read -r name sha <<<"$entry"
    fetch_verified "https://raw.githubusercontent.com/microsoft/onnxruntime/v$ORT_WEB_VERSION/$name" "$WEB_DIR/$name" "$sha"
  done
  echo "onnxruntime-web ready at $WEB_DIR"
fi
echo "== automatic-selection dependencies ready =="
