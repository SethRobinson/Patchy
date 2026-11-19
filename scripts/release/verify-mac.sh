#!/usr/bin/env bash
# Release verification on the mac build host (docs/release-process.md): the MCP client
# suite against the built connector and against the connector inside the release dmg, and
# the downloadable text recognition engine inside the dmg.
# scripts/release/verify-remote.ps1 -Target mac pipes this script over ssh after the full
# native suites; it can also run on the host by hand:
#   bash scripts/release/verify-mac.sh <version>
set -euo pipefail
VERSION="${1:?usage: verify-mac.sh <version>}"
cd "$HOME/patchy/src"
export QT_QPA_PLATFORM=offscreen
unset PATCHY_UI_TEST_FILTER
PYTHON=.deps/mcp-client-py312/bin/python
DMG="build/package/Patchy-$VERSION.dmg"
[ -f "$DMG" ] || { echo "missing $DMG (run the mac builder first)" >&2; exit 1; }

echo '== NATIVE MCP SUITE =='
nice -n 10 "$PYTHON" tests/mcp_client_tests.py build/mac-release/Patchy.app/Contents/MacOS/patchy-mcp

mount_dir="$PWD/build/verify-dmg-check"
mkdir -p "$mount_dir"
hdiutil attach -readonly -nobrowse -mountpoint "$mount_dir" "$DMG"
trap 'diskutil eject "$mount_dir"' EXIT
licenses="$mount_dir/Patchy.app/Contents/Resources/licenses"
for notice in LICENSE NOTICE-THIRD-PARTY.md; do
  cmp "$notice" "$licenses/$notice"
done
cmp build/mac-release/Patchy.app/Contents/Resources/licenses/libomp-LICENSE.txt "$licenses/libomp-LICENSE.txt"
echo '== DMG TEXT RECOGNITION ENGINE =='
# The downloadable PP-OCR engine inside the package (docs/ocr-engines.md, "PP-OCR"): listed
# as a download while its models folder is empty, then reading drawn text from this host's
# staged models (.deps/ai-models, scripts/setup-ai-deps.sh). PATCHY_DOWNLOADED_MODELS_DIR
# keeps the runs out of the user's own models folder; nothing is downloaded.
[ -f .deps/ai-models/ppocr_v6_small_det.onnx ] || { echo 'missing .deps/ai-models/ppocr_*: run scripts/setup-ai-deps.sh' >&2; exit 1; }
ocr_work="$PWD/build/verify-ocr/dmg"
mkdir -p "$ocr_work/settings" "$ocr_work/no-models"
for ocr_mode in absent present; do
  ocr_models="$PWD/.deps/ai-models"
  ocr_arg=""
  if [ "$ocr_mode" = absent ]; then ocr_models="$ocr_work/no-models"; ocr_arg="--script-arg models=absent"; fi
  : > "$ocr_work/$ocr_mode.txt"
  # shellcheck disable=SC2086 # $ocr_arg is two plain words, or nothing
  PATCHY_SETTINGS_DIR="$ocr_work/settings" PATCHY_NO_SOUND=1 PATCHY_DOWNLOADED_MODELS_DIR="$ocr_models" \
    nice -n 10 "$mount_dir/Patchy.app/Contents/MacOS/Patchy" --headless \
    --run-script "$PWD/packaging/ocr-models-check.js" $ocr_arg --script-output "$ocr_work/$ocr_mode.txt" || true
  if [ "$(tail -n 1 "$ocr_work/$ocr_mode.txt")" != "[done]" ]; then
    echo "[FAIL] text recognition engine in the dmg (models $ocr_mode):"
    cat "$ocr_work/$ocr_mode.txt"
    exit 1
  fi
  echo "[PASS] dmg, models $ocr_mode: $(tail -n 2 "$ocr_work/$ocr_mode.txt" | head -n 1)"
done
echo '== DMG MCP SUITE =='
nice -n 10 "$PYTHON" tests/mcp_client_tests.py "$mount_dir/Patchy.app/Contents/MacOS/patchy-mcp"
echo '== ALL MAC RELEASE CHECKS PASSED =='
# end of script (a comment absorbs a carriage return a Windows pipe may append)
