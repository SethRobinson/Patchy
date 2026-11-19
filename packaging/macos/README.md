# macOS packaging

`make-dmg.sh` turns a built `build/mac-release/Patchy.app` into
`build/package/Patchy-<version>.dmg` (drag-to-Applications layout): it runs
`macdeployqt` to bundle the Qt frameworks/plugins, copies `libqoffscreen.dylib` in by
hand (macdeployqt bundles only the cocoa platform, and `--headless` needs offscreen),
code-signs and notarizes when the environment is configured (see below), runs a
`--headless --run-script` smoke check on the staged bundle, and images the result with
`hdiutil`.
`scripts/remote/release-mac.ps1` drives the whole flow from the Windows machine.

Run the staged connector's `--check` after code signing, like the headless app
check. `macdeployqt` rewrites Mach-O load commands and invalidates existing
signatures; executing that intermediate connector can be killed by macOS.

Bundle metadata lives in `Info.plist.in` (configured through CMake's
`MACOSX_BUNDLE_*` properties; the version comes from the CMake project version).
Its `CFBundleDocumentTypes` list the formats Finder offers Patchy for; keep them in
step with File > Open (rules in [file-formats.md](../../docs/file-formats.md)).
`patchy.icns` contains native and Retina representations up to 1024 px, rendered
from the folded SVG source. Regenerate all platform icons together with
`scripts/dev/generate-branding.py`; see [branding](../branding/README.md).

## One-time signing setup

Uses the existing Apple Developer account (Robinson Technologies Corporation).

1. Ensure a **Developer ID Application** certificate is in the login keychain on
   the mac build host: `security find-identity -v -p codesigning` should list
   `Developer ID Application: Robinson Technologies Corporation (TEAMID)`. If not,
   create one at developer.apple.com > Certificates (type "Developer ID Application")
   and double-click the downloaded .cer.
2. Store notarization credentials (App Store Connect API key or app-specific
   password): `xcrun notarytool store-credentials patchy-notary`
3. Put both into `~/.patchy-release-env` on the mac build host (sourced by the release script):

   ```sh
   export PATCHY_MAC_SIGN_IDENTITY="Developer ID Application: Robinson Technologies Corporation (TEAMID)"
   export PATCHY_NOTARY_PROFILE="patchy-notary"
   # Used by the noninteractive unlock helper. Keep the file chmod 600.
   # This is the keychain password, which can differ from the current login password.
   export PATCHY_KEYCHAIN_PASSWORD="..."
   ```

Without that file `make-dmg.sh` run by hand still produces an **unsigned** dmg (users
must right-click-open / approve in System Settings on first launch).

For a release, `scripts/remote/release-mac.ps1` sets `PATCHY_REQUIRE_SIGNING=1`.
The packager requires a signing identity and a notary profile. After stapling, it runs

```
spctl -a -t open --context context:primary-signature -v <dmg>
```

The assessment must return exit code 0 and report `source=Notarized Developer ID`.

Keep `xcrun stapler validate` out of unattended verification. It can block indefinitely on the build host.

## Unattended keychain access

`make-dmg.sh` uses the system Python 3 and `unlock-keychain.py`. The helper disables
Security.framework user interaction before opening the login keychain, unlocks it
using the existing environment credential, and verifies the unlocked status. The
password never enters command-line arguments or logs. A worker has a fifteen-second
deadline, is killed and reaped on timeout, and is never retried automatically. A bad
password or denied access fails the release without raising a password dialog.
The parent script removes the password from its environment after the unlock.

Do not use `security show-keychain-info` or prompt-capable unlock probes to diagnose
an unattended build. Read status with `SecKeychainGetStatus` after calling
`SecKeychainSetUserInteractionAllowed(false)`. Settings queries can require an
unlocked keychain; a denied query is not evidence of corrupt settings.

Desktop and SSH sessions can have different keychain lock states. A successful
release unlock does not establish that the desktop login keychain is unlocked.
When system services repeatedly request the login password, inspect the desktop
session separately using an owned background helper with interaction disabled.
Preserve the user's password, items, access controls, and lock-on-sleep settings.
Do not reset the keychain, disable its security settings, repeatedly retry failed
requests, or kill system services to suppress dialogs. Canceling a dialog only
cancels that request. Fix the requesting session's locked state or credential.

### Diagnosing a keychain password dialog on the desktop

A system service requesting the login keychain can indicate that the desktop session's
keychain is locked. These read-only checks can run over SSH.

```
log show --last 6h --style compact --predicate 'process == "launchd" AND eventMessage CONTAINS "SecurityAgent"'
log show --last 6h --style compact --predicate 'process == "securityd" AND (eventMessage CONTAINS "thread limit" OR eventMessage CONTAINS "makeUnlocked")'
ls -lt /Library/Logs/DiagnosticReports | head
ps -axo pid,lstart,command | grep "[s]ecurityd -i"
```

`Successfully spawned SecurityAgent` marks the prompt. A `securityd-<date>.ips`
report and a recent `securityd` start time can identify a crash that relocked the
desktop keychain. `check-keychain-password.sh` checks whether the stored credential
matches the account login and prints only MATCH or DIFFERENT. After a login password change,
use Keychain Access > login > Change Password for Keychain if needed and update
`PATCHY_KEYCHAIN_PASSWORD` to match the keychain.

`desktop-keychain-unlock.sh` installs a one-shot helper in the desktop
`gui/<uid>` launchd domain. The helper uses the release credential to unlock the
keychain in that session, with UI disabled, then removes itself. From Windows,
run the checked-in scripts so SSH receives their shell quoting intact.

```
ssh <mac-build-host> bash ~/patchy/src/packaging/macos/check-keychain-password.sh
ssh <mac-build-host> bash ~/patchy/src/packaging/macos/desktop-keychain-unlock.sh
```

The mac checkout under `~/patchy/src` is the last remote-build snapshot, so a script
added since then must be pushed by a remote build (or copied with scp) before it exists
there.
