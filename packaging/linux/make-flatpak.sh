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
flatpak build-bundle "$REPO_DIR" "$PACKAGE_DIR/Patchy-$VERSION.flatpak" "$APP_ID"
echo "Bundle written: $PACKAGE_DIR/Patchy-$VERSION.flatpak"
