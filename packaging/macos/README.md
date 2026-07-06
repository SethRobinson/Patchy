# macOS packaging

`make-dmg.sh` turns a built `build/mac-release/Patchy.app` into
`build/package/Patchy-<version>.dmg` (drag-to-Applications layout): it runs
`macdeployqt` to bundle the Qt frameworks/plugins, code-signs and notarizes when the
environment is configured (see below), and images the result with `hdiutil`.
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

Without that file the scripts still produce an **unsigned** dmg (users must
right-click-open / approve in System Settings on first launch).
