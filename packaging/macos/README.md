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
   # Used by the noninteractive unlock helper. Keep the file chmod 600.
   # This is the keychain password, which can differ from the current login password.
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

A dialog such as "com.apple.iCloudHelper wants to use the login keychain" means the
desktop session's login keychain is locked; it does not mean a build asked for
anything. Read the unified log before blaming a build (read-only, no `security`
commands, safe over ssh):

```
log show --last 6h --style compact --predicate 'process == "launchd" AND eventMessage CONTAINS "SecurityAgent"'
log show --last 6h --style compact --predicate 'process == "securityd" AND (eventMessage CONTAINS "thread limit" OR eventMessage CONTAINS "makeUnlocked")'
ls -lt /Library/Logs/DiagnosticReports | head
ps -axo pid,lstart,command | grep "[s]ecurityd -i"
```

`Successfully spawned SecurityAgent` is the dialog appearing. A `securityd-<date>.ips`
report together with a recent start time on `/usr/sbin/securityd -i` means securityd
crashed, and every session's keychains come back locked when it restarts. September 11,
2026: securityd logged "reached its thread limit (100) - service deadlock is possible"
and crashed at 17:03 with no Patchy build or ssh session running (the last remote build
was two days earlier); the iCloudHelper dialog followed at 17:08. The build scripts only
ever unlock the keychain and never lock it, and a `SecKeychainGetStatus` probe from an
ssh session reports that session, not the desktop. The login keychain password equals
the login password on studiomac (verified September 11, 2026: the stored credential
passes `sudo -S`), so login and reboot unlock the keychain automatically; after a
securityd crash in the middle of a session nothing can, and the dialog is answered
with the login password or by the script below. If the passwords ever diverge (a
login password reset that skips the keychain), Keychain Access > login > Change
Password for Keychain restores the match, and `PATCHY_KEYCHAIN_PASSWORD` must be
updated with it.

Recovery without touching the desktop: `packaging/macos/desktop-keychain-unlock.sh`
(run over ssh) bootstraps a one-shot helper into the desktop `gui/<uid>` launchd
domain, the only place that can read or change the desktop session's keychain state.
The helper unlocks the login keychain there once, with UI disabled, using the release
credential, then removes itself, so iCloudHelper's next retry succeeds silently
(September 11, 2026: status 2 = locked before, 7 after). That repairs the session, not
the cause.
