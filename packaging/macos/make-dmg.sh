#!/usr/bin/env bash
# Builds the distributable macOS artifact: build/package/Patchy-<version>.dmg
# Run from a built mac-release tree (scripts/remote/remote-build.ps1 -Target mac, or
# locally: cmake --preset mac-release && cmake --build --preset mac-release):
#   bash packaging/macos/make-dmg.sh
#
# Signing/notarization run only when the environment provides both:
#   PATCHY_MAC_SIGN_IDENTITY  e.g. "Developer ID Application: Robinson Technologies Corporation (XXXXXXXXXX)"
#   PATCHY_NOTARY_PROFILE     a notarytool keychain profile (xcrun notarytool store-credentials)
# Otherwise those steps are skipped with a message and an unsigned dmg is produced
# (mirrors the RT_PROJECTS gate in build-release.bat). See packaging/macos/README.md.
set -euo pipefail
cd "$(dirname "$0")/../.."

BUILD_DIR=build/mac-release
APP="$BUILD_DIR/Patchy.app"
QT_BIN=".deps/Qt/6.8.3/macos/bin"
PACKAGE_DIR=build/package

[ -d "$APP" ] || { echo "ERROR: $APP not found; build the mac-release preset first."; exit 1; }
[ -x "$QT_BIN/macdeployqt" ] || { echo "ERROR: $QT_BIN/macdeployqt not found."; exit 1; }

VERSION=$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$APP/Contents/Info.plist")
DMG="$PACKAGE_DIR/Patchy-$VERSION.dmg"
mkdir -p "$PACKAGE_DIR"
# Delete ALL previous dmgs up front (not just this version's): if any later step fails,
# nothing stale remains for the newest-file upload script to pick up by accident.
rm -f "$PACKAGE_DIR"/Patchy-*.dmg

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
cp -R "$APP" "$STAGE/Patchy.app"
# Dev-tree extras that are not part of the shipped app.
rm -rf "$STAGE/Patchy.app/Contents/MacOS/test-fixtures"

echo "== macdeployqt (bundling Qt frameworks and plugins) =="
"$QT_BIN/macdeployqt" "$STAGE/Patchy.app"

if [ -n "${PATCHY_MAC_SIGN_IDENTITY:-}" ]; then
  if [ -n "${PATCHY_KEYCHAIN_PASSWORD:-}" ]; then
    # SSH sessions get their own security context where the login keychain starts
    # LOCKED (codesign then fails with errSecInternalComponent); unlock it for this
    # session. The password lives in ~/.patchy-release-env (chmod 600) on the build mac.
    security unlock-keychain -p "$PATCHY_KEYCHAIN_PASSWORD" ~/Library/Keychains/login.keychain-db
  fi
  echo "== codesign (hardened runtime) =="
  codesign --force --deep --options runtime --timestamp -s "$PATCHY_MAC_SIGN_IDENTITY" "$STAGE/Patchy.app"
  codesign --verify --deep --strict "$STAGE/Patchy.app"
else
  echo "PATCHY_MAC_SIGN_IDENTITY is not set; skipping code signing (unsigned dmg)."
fi

echo "== dmg =="
DMG_STAGE=$(mktemp -d)
cp -R "$STAGE/Patchy.app" "$DMG_STAGE/"
ln -s /Applications "$DMG_STAGE/Applications"
rm -f "$DMG"
hdiutil create -volname "Patchy $VERSION" -srcfolder "$DMG_STAGE" -ov -format UDZO "$DMG"
rm -rf "$DMG_STAGE"

if [ -n "${PATCHY_MAC_SIGN_IDENTITY:-}" ]; then
  # Sign the DMG container itself too (spctl assesses the dmg's own signature); must
  # happen BEFORE notarization -- signing after stapling would invalidate the ticket.
  codesign --force --timestamp -s "$PATCHY_MAC_SIGN_IDENTITY" "$DMG"
fi

if [ -n "${PATCHY_MAC_SIGN_IDENTITY:-}" ] && [ -n "${PATCHY_NOTARY_PROFILE:-}" ]; then
  echo "== notarize + staple =="
  xcrun notarytool submit "$DMG" --keychain-profile "$PATCHY_NOTARY_PROFILE" --wait
  xcrun stapler staple "$DMG"
  spctl -a -t open --context context:primary-signature -v "$DMG" || true
else
  echo "PATCHY_NOTARY_PROFILE and/or the sign identity are not set; skipping notarization."
fi

echo "DMG written: $DMG"
