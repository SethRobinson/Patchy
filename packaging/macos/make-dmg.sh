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
# (mirrors the RT_PROJECTS gate in scripts/release/build-release.bat). See packaging/macos/README.md.
#
# PATCHY_REQUIRE_SIGNING=1 turns those skips into hard errors and additionally proves
# the result: stapler validate and spctl must both accept the finished dmg. Release
# runs set it (scripts/remote/release-mac.ps1) because an unsigned or un-notarized dmg
# is not a shippable artifact -- Gatekeeper refuses to open it -- and the skip messages
# scroll past in a long build log. Leave it unset for a local unsigned dev build.
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

# macdeployqt bundles libqcocoa only; the offscreen platform is what --headless loads,
# so it is copied by hand. It lands before codesign so the hardened-runtime signature
# covers it, and the smoke check below proves it loads from the bundle.
QT_OFFSCREEN_PLUGIN="$QT_BIN/../plugins/platforms/libqoffscreen.dylib"
if [ ! -f "$QT_OFFSCREEN_PLUGIN" ]; then
  echo "ERROR: $QT_OFFSCREEN_PLUGIN not found; install the matching Qt platform plugins." >&2
  exit 1
fi
echo "== copy Qt offscreen platform plugin =="
mkdir -p "$STAGE/Patchy.app/Contents/PlugIns/platforms"
cp "$QT_OFFSCREEN_PLUGIN" "$STAGE/Patchy.app/Contents/PlugIns/platforms/libqoffscreen.dylib"

if [ -n "${PATCHY_MAC_SIGN_IDENTITY:-}" ]; then
  if [ -n "${PATCHY_KEYCHAIN_PASSWORD:-}" ]; then
    # SSH sessions get their own security context where the login keychain starts
    # LOCKED (codesign then fails with errSecInternalComponent); unlock it for this
    # session. The password lives in ~/.patchy-release-env (chmod 600) on the build mac.
    #
    # Bounded, because this is a local operation that should take milliseconds and
    # instead blocks forever when every keychain request from this ssh session is
    # queued behind a SecurityAgent dialog on the machine's own screen that nobody
    # can answer (September 2026, twice: a release sat here 30+ minutes with no
    # output; later even this password unlock spawned a fresh SecurityAgent per
    # attempt while the console session was locked or asleep). A release that hangs
    # silently is as bad as one that ships unsigned, so fail loudly and say where to
    # look. macOS ships no timeout(1), hence the watchdog.
    security unlock-keychain -p "$PATCHY_KEYCHAIN_PASSWORD" ~/Library/Keychains/login.keychain-db &
    unlock_pid=$!
    ( sleep 60; kill -9 "$unlock_pid" 2>/dev/null ) &
    unlock_watchdog=$!
    if ! wait "$unlock_pid"; then
      kill "$unlock_watchdog" 2>/dev/null || true
      echo "ERROR: unlocking the login keychain failed or timed out after 60s." >&2
      echo "Every keychain request from ssh is waiting on a SecurityAgent dialog on" >&2
      echo "studiomac's own screen that nobody can answer, usually because the console" >&2
      echo "session is locked or asleep (then 'screencapture -x /tmp/x.png' over ssh" >&2
      echo "fails with 'could not create image from display'). Unlock the mac at its" >&2
      echo "screen, dismiss any prompt, and rerun. Do not probe with" >&2
      echo "'security show-keychain-info': it raises its own prompt and hangs the same" >&2
      echo "way; killing SecurityAgent only cancels the current request. Nothing was" >&2
      echo "signed, so no artifact was produced." >&2
      exit 1
    fi
    kill "$unlock_watchdog" 2>/dev/null || true
  fi
  echo "== codesign (hardened runtime) =="
  codesign --force --deep --options runtime --timestamp -s "$PATCHY_MAC_SIGN_IDENTITY" "$STAGE/Patchy.app"
  codesign --verify --deep --strict "$STAGE/Patchy.app"
elif [ "${PATCHY_REQUIRE_SIGNING:-0}" = "1" ]; then
  echo "ERROR: PATCHY_REQUIRE_SIGNING=1 but PATCHY_MAC_SIGN_IDENTITY is not set." >&2
  echo "A release dmg must be signed; Gatekeeper blocks an unsigned one. Check that" >&2
  echo "~/.patchy-release-env exists on this mac and exports the signing identity" >&2
  echo "(see packaging/macos/README.md)." >&2
  exit 1
else
  echo "PATCHY_MAC_SIGN_IDENTITY is not set; skipping code signing (unsigned dmg)."
fi

# Proves the staged bundle runs with no display before any artifact exists: the
# frameworks, the hand-copied offscreen plugin, fonts, and translations all come from
# $STAGE. On a release run this is the signed hardened-runtime bundle loading the
# freshly signed plugin. --headless never forwards to a running Patchy, and
# PATCHY_SETTINGS_DIR keeps the run out of the real settings. macOS ships no
# timeout(1), hence the watchdog (same shape as the keychain unlock above).
echo "== headless smoke check (the staged app must run with no display) =="
SMOKE=$(mktemp -d)
trap 'rm -rf "$STAGE" "$SMOKE"' EXIT
mkdir -p "$SMOKE/settings"
echo 'console.log("headless smoke")' > "$SMOKE/smoke.js"
PATCHY_SETTINGS_DIR="$SMOKE/settings" PATCHY_NO_SOUND=1 \
  "$STAGE/Patchy.app/Contents/MacOS/Patchy" --headless \
  --run-script "$SMOKE/smoke.js" --script-output "$SMOKE/smoke-output.txt" \
  > "$SMOKE/smoke-console.txt" 2>&1 &
smoke_pid=$!
# The watchdog's own stderr goes to /dev/null so the job notice for its killed sleep
# never reaches the release log; && keeps it from firing after the sleep is killed.
( sleep 180 && kill -9 "$smoke_pid" 2>/dev/null ) 2>/dev/null &
smoke_watchdog=$!
smoke_status=0
wait "$smoke_pid" || smoke_status=$?
# Kill the watchdog's sleep before the watchdog itself: an orphaned sleep keeps the
# inherited stdout open, which holds an ssh-driven release session for the full 180 s
# after the script has finished. Then reap the watchdog so bash prints no "Terminated"
# job notice into the release log.
pkill -P "$smoke_watchdog" 2>/dev/null || true
kill "$smoke_watchdog" 2>/dev/null || true
wait "$smoke_watchdog" 2>/dev/null || true
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

  # Positive proof, not just "the commands above ran". spctl makes the same assessment
  # Gatekeeper will make on a user's machine, and it used to end in "|| true", which
  # swallowed a rejection and let a dmg that macOS would refuse leave the build looking
  # healthy. Its "source=Notarized Developer ID" line is what distinguishes a notarized
  # dmg from one that is merely signed, so the build asserts on it rather than trusting
  # that notarytool and stapler did their jobs.
  #
  # Deliberately NOT "xcrun stapler validate": on this build mac it blocks forever
  # (measured September 2026, still spinning after 10 minutes, killed at 60s in a
  # bounded retest) and would hang every release. spctl covers the same ground in
  # about a third of a second.
  echo "== verify signature and Gatekeeper assessment =="
  if ! SPCTL_OUT=$(spctl -a -t open --context context:primary-signature -v "$DMG" 2>&1); then
    echo "ERROR: Gatekeeper rejected the finished dmg:" >&2
    echo "$SPCTL_OUT" >&2
    exit 1
  fi
  echo "$SPCTL_OUT"
  case "$SPCTL_OUT" in
    *"Notarized Developer ID"*) ;;
    *)
      echo "ERROR: the dmg was accepted, but not as a notarized artifact." >&2
      echo "Users would be warned on first open. spctl said:" >&2
      echo "$SPCTL_OUT" >&2
      exit 1
      ;;
  esac
elif [ "${PATCHY_REQUIRE_SIGNING:-0}" = "1" ]; then
  echo "ERROR: PATCHY_REQUIRE_SIGNING=1 but PATCHY_NOTARY_PROFILE is not set." >&2
  echo "A release dmg must be notarized and stapled or macOS will refuse to open it" >&2
  echo "on any machine that has not seen it before (see packaging/macos/README.md)." >&2
  exit 1
else
  echo "PATCHY_NOTARY_PROFILE and/or the sign identity are not set; skipping notarization."
fi

echo "DMG written: $DMG"
