# macOS packaging

`make-dmg.sh` turns a built `build/mac-release/Patchy.app` into
`build/package/Patchy-<version>.dmg` (drag-to-Applications layout): it runs
`macdeployqt` to bundle the Qt frameworks/plugins, copies `libqoffscreen.dylib` in by
hand (macdeployqt bundles only the cocoa platform, and `--headless` needs offscreen),
code-signs and notarizes when the environment is configured (see below), runs a
`--headless --run-script` smoke check on the staged bundle, and images the result with
`hdiutil`.
`scripts/remote/release-mac.ps1` drives the whole flow from the Windows machine.

Bundle metadata lives in `Info.plist.in` (configured through CMake's
`MACOSX_BUNDLE_*` properties; the version comes from the CMake project version).
`patchy.icns` was generated from the native layers of `src/app/patchy.ico`
(largest layer is 256 px; regenerate with `iconutil -c icns` from an iconset if the
icon art changes).

## One-time signing setup (Seth)

Uses the existing Apple Developer account (Robinson Technologies Corporation).

1. Ensure a **Developer ID Application** certificate is in the login keychain on
   studiomac: `security find-identity -v -p codesigning` should list
   `Developer ID Application: Robinson Technologies Corporation (TEAMID)`. If not,
   create one at developer.apple.com > Certificates (type "Developer ID Application")
   and double-click the downloaded .cer.
2. Store notarization credentials (App Store Connect API key or app-specific
   password): `xcrun notarytool store-credentials patchy-notary`
3. Put both into `~/.patchy-release-env` on studiomac (sourced by the release script):

   ```sh
   export PATCHY_MAC_SIGN_IDENTITY="Developer ID Application: Robinson Technologies Corporation (TEAMID)"
   export PATCHY_NOTARY_PROFILE="patchy-notary"
   # SSH build sessions start with the login keychain locked; make-dmg.sh unlocks it
   # with this (the mac login password). Keep the file chmod 600.
   export PATCHY_KEYCHAIN_PASSWORD="..."
   ```

Without that file `make-dmg.sh` run by hand still produces an **unsigned** dmg (users
must right-click-open / approve in System Settings on first launch).

A RELEASE run is different: `scripts/remote/release-mac.ps1` passes
`PATCHY_REQUIRE_SIGNING=1`, which turns both skips into hard errors and verifies the
finished artifact. That flag exists because the skips are only messages in a long
build log, so a missing `~/.patchy-release-env` used to yield an unsigned dmg that
looked like a successful release and would have shipped (caught September 2026).
With it set, `make-dmg.sh` fails when either the signing identity or the notary
profile is absent, and after stapling it runs

```
spctl -a -t open --context context:primary-signature -v <dmg>
```

requiring both a non-zero-free assessment and the literal `source=Notarized Developer ID`
in the output, which is what separates a notarized dmg from a merely signed one. That
`spctl` call used to end in `|| true`, which hid a rejection.

Do not add `xcrun stapler validate` to that check. It blocks indefinitely on studiomac
(September 2026: still running after ten minutes, killed at sixty seconds on a bounded
retest) and would hang every release; `spctl` covers the same ground in about a third of
a second.

## When the keychain unlock times out

`make-dmg.sh` bounds `security unlock-keychain` at sixty seconds because the call hangs
whenever securityd is holding a SecurityAgent dialog on studiomac's own screen that
nobody can answer. Observed September 2026 (twice): the console session was locked or
asleep, `screencapture -x /tmp/x.png` over ssh failed with `could not create image from
display`, and every keychain request from ssh, the password unlock included, spawned a
fresh `SecurityAgent` process and blocked. The fix is at the mac: unlock its screen,
dismiss any prompt, then rerun `scripts\remote\release-mac.bat` and
`scripts\release\upload-mac-to-rtsoft.bat`, and only then bump the macOS entry in
`latest_version.json`. Do not diagnose with `security show-keychain-info`: it raises its
own prompt on a locked keychain and hangs the same way, and the stale process then keeps
that dialog alive for days. Killing `SecurityAgent` cancels only the current requester;
the next keychain call raises a new one.
