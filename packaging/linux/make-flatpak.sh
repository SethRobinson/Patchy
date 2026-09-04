#!/usr/bin/env bash
# Builds the self-hosted Flatpak bundle: build/package/Patchy-<version>.flatpak
# Prerequisites (one-time):
#   sudo apt-get install -y flatpak flatpak-builder
#   flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
#   flatpak install -y flathub org.kde.Platform//6.8 org.kde.Sdk//6.8
# Users install the produced bundle with:  flatpak install ./Patchy-<version>.flatpak
set -euo pipefail
cd "$(dirname "$0")"

APP_ID=com.rtsoft.patchy
ROOT=../..
BUILD_DIR="$ROOT/build/flatpak"
REPO_DIR="$ROOT/build/flatpak-repo"
PACKAGE_DIR="$ROOT/build/package"

VERSION=$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9.]+).*$/\1/p' "$ROOT/CMakeLists.txt" | head -1)
[ -n "$VERSION" ] || { echo "ERROR: could not read the project version from CMakeLists.txt"; exit 1; }

mkdir -p "$PACKAGE_DIR"
# Delete ALL previous bundles up front (not just this version's): if the build fails,
# nothing stale remains for the newest-file upload script to pick up by accident.
rm -f "$PACKAGE_DIR"/Patchy-*.flatpak
flatpak-builder --force-clean --repo="$REPO_DIR" "$BUILD_DIR" "flatpak/$APP_ID.yml"

# Proves the sandboxed app runs with no display before any bundle exists. The Qt
# offscreen platform comes from the org.kde.Platform runtime, not from this repo, so
# this is the one check that the runtime still ships it and that --headless works
# inside the sandbox. flatpak-builder --run uses the build directory the bundle is
# exported from, so nothing is installed on the build machine. --headless never
# forwards to a running Patchy; PATCHY_SETTINGS_DIR keeps the run out of the real
# settings; the temp directory lives under $HOME because that is what the sandbox
# can see. The same check runs in the Windows and macOS packagers.
echo "== headless smoke check (the sandboxed app must run with no display) =="
SMOKE=$(mktemp -d "$HOME/.patchy-flatpak-smoke.XXXXXX")
trap 'rm -rf "$SMOKE"' EXIT
mkdir -p "$SMOKE/settings"
echo 'console.log("headless smoke")' > "$SMOKE/smoke.js"
smoke_status=0
timeout 180 flatpak-builder --run "$BUILD_DIR" "flatpak/$APP_ID.yml" \
  env PATCHY_SETTINGS_DIR="$SMOKE/settings" PATCHY_NO_SOUND=1 \
  patchy --headless --run-script "$SMOKE/smoke.js" --script-output "$SMOKE/smoke-output.txt" \
  > "$SMOKE/smoke-console.txt" 2>&1 || smoke_status=$?
if [ "$smoke_status" != "0" ]; then
  echo "ERROR: headless smoke check failed (exit $smoke_status). Console output:" >&2
  cat "$SMOKE/smoke-console.txt" >&2
  exit 1
fi
if [ "$(tail -n 1 "$SMOKE/smoke-output.txt" 2>/dev/null)" != "[done]" ]; then
  echo "ERROR: headless smoke check output did not end with [done]:" >&2
  cat "$SMOKE/smoke-output.txt" >&2 2>/dev/null || true
  exit 1
fi
echo "Headless smoke check passed."

flatpak build-bundle "$REPO_DIR" "$PACKAGE_DIR/Patchy-$VERSION.flatpak" "$APP_ID"
echo "Bundle written: $PACKAGE_DIR/Patchy-$VERSION.flatpak"
