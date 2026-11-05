# Release process

How to cut and publish a Patchy release. Read this in full before bumping a version or running any release batch file. The per-change build/test handoff (every code change must refresh `build\opt-dev\patchy.exe`) is separate and lives in AGENTS.md. Before a release (any release batch file, upload or publish), also read [release-process-scripts.md](release-process-scripts.md) and [release-process-github.md](release-process-github.md) in full.

Related documents:

- [release-process-scripts.md](release-process-scripts.md): unattended runs of the release scripts (the automated driver, exit markers, shells, `NO_PAUSE`), the batch-file rules, `scripts\vs-env.bat`, and the relink of a running app or connector.
- [release-process-github.md](release-process-github.md): publishing the GitHub release: the stable asset names, the tag, draft, verify and publish, and the update manifest.
- [release-wasm.md](release-wasm.md): the web build, which ships with every desktop release: its build, local check and upload scripts.

## Packages and build hosts

Unchanged development builds keep their binaries and build date. Packaging must
request a new date before its build: the Windows/web builders, macOS packager and
AppImage container run `patchy_refresh_build_stamp`; the Flatpak packager generates
a manifest with a fresh `PATCHY_BUILD_STAMP_REFRESH_TOKEN`, invalidating the cached
app module. Hand-run packaging follows the same rule. Do not enable the refresh
target in the default build, which would force every unchanged build to relink.

Desktop packages also carry automatic selection: ONNX Runtime plus the 19 MB MobileSAM pair in `ai-models/` (12 MB compressed); Object Selection's High Quality model (234 MB, release `ai-models-2`) is a user download, never packaged ([ai-selection-models.md](ai-selection-models.md)). Every build host runs `scripts\setup-ai-deps.ps1` (Windows) or `bash scripts/setup-ai-deps.sh` (mac/Linux) once per pinned version, and the mac host also `scripts/setup-openmp-mac.sh` (G'MIC's OpenMP, [gmic.md](gmic.md)) and `scripts/setup-qt-cocoa-patch-mac.sh` (patched Qt cocoa plugin, [building-qt.md](building-qt.md#patched-qt-cocoa-plugin-macos)); `build-release.bat` configures with `-DPATCHY_REQUIRE_AI_SELECTION=ON` and refuses to package without them, the Flatpak fetches the same pinned files itself, and `packaging/package-selftest.js` runs both models in the unpacked package.

Desktop packages include `patchy-mcp` and the assembled `patchy-control` skill; the Windows package also carries the two legacy plug-in hosts `patchy-8bf-host32.exe` and `patchy-8bf-host64.exe` (docs/plugins.md) and the GEGL helper `patchy-gegl-host.exe` ([gegl-filters.md](gegl-filters.md#bundled-gegl)), which `build-release.bat` requires, stages and signs like the app.
Staging and resource paths are specified in [ai-control.md](ai-control.md). Each
desktop packaging script runs the installed connector's `--check` smoke test;
Windows signs both executables, the plug-in and GEGL hosts, and the installer with its helpers, and macOS deploys Qt for both. The Linux builder also writes the
signed Flatpak update repository ([packaging/linux/README.md](../packaging/linux/README.md))
and the AppImage ([appimage.md](appimage.md)).

The Linux builder (`release-linux.ps1`) also builds the aarch64 Flatpak in a Lima VM on the mac
build host (the `linux-arm64` entry of `hosts.local.json`), in parallel with the x86_64 build,
so the `linux` and `mac` targets share the mac. The linux build host signs both arches into one
repository and exports both bundles; the signed aarch64 bundle is then installed and
smoke-checked in the VM ([packaging/linux/README.md](../packaging/linux/README.md#aarch64)).
The aarch64 build runs no test suites: its checks are the sandboxed headless smoke check,
`patchy-mcp --check` and that install. The mac (arm64) and Linux x86_64 suites cover its code.

`build\package` must never hold a previous version's files once a new build starts. The mac and Linux builders write versioned artifacts (`Patchy-<version>.dmg`, `Patchy-<version>.flatpak`, `Patchy-<version>-aarch64.flatpak`, `Patchy-<version>-x86_64.AppImage`); `publish-github-release.ps1` publishes this version's. Unversioned copies left by the retired rtsoft.com mirror uploads are deleted by `release-mac.ps1` and `release-linux.ps1` along with the old versioned artifacts, and the Windows packager deletes its own final-named outputs before rebuilding. An agent that builds packages by hand must do the same delete before reporting the folder as release-ready.

## Version bump checklist

When bumping the release version, update the version fields:

- `CMakeLists.txt` (`project(... VERSION x.y)`). The version reaches the code through the configure-time generated `patchy_version.hpp` (from `cmake/patchy_version.hpp.in`), not a target-wide define, so a bump recompiles only the handful of files that include it rather than all of `patchy_ui`.
- Keep `latest_version.json` at the last published version until the new GitHub release and its downloads are verified. Then update its per-platform `version` entries in a separate announcement commit: windows always, with its three installer keys ([auto-update.md](auto-update.md); the publish script prints them), and `windows-arm64` with the same version and its own `download_url` and installer keys (an x64 copy emulated on an ARM64 PC reads that entry first); macos/linux only when those artifacts ship. The `linux` entry also carries `appimage_url` and the three `appimage_install_*` keys for its one-click update (the publish script prints all four; [auto-update.md](auto-update.md)). Pushing this manifest to main announces the update independently of release publication, so it deliberately differs from an unpublished build.
- A `<release>` with a short `<description>` in `packaging/linux/com.rtsoft.patchy.metainfo.xml`,
  then `python scripts\release\check-metainfo.py --fix`: it repoints the metainfo's screenshot
  URLs at `v<version>` when `docs/images/screenshots/` changed since the tag they pin, and
  must end in `passed` ([packaging/linux/README.md](../packaging/linux/README.md)).
- The latest-release line in `README.md`'s Download section, with the published
  version and release date matching the newest "What's New" entry.
- A new top entry under `README.md`'s "What's New" section for that version,
  dated with the release date and summarizing the user-visible changes.
- Keep only the two newest release entries in `README.md`. After adding the new
  entry, move the entry that has become third-newest to the top of
  `RELEASE-HISTORY.md`, preserving its date, wording, order, and author credits.
  Keep the `[Older releases](RELEASE-HISTORY.md)` link immediately after the two
  README entries. `RELEASE-HISTORY.md` stays newest-first and must not duplicate
  either release still shown in the README.
- The contributor credits (`src/ui/app_credits.cpp`, mirrored in the README's
  "Incredible people" line): add anyone whose pull request was accepted or whose
  issue was resolved since the last release, by GitHub handle (see "What's New
  author credits" below).

## What's New author credits

Always credit the correct author on each "What's New" bullet, including entries
moved to `RELEASE-HISTORY.md`. Seth is the default and is left uncredited; any
feature or fix contributed by someone else must name them with a GitHub handle
link like `([@handle](https://github.com/handle))`. Check `git log`'s author for
the commits behind each bullet (e.g. `git log --format='%an %s'`) rather than
assuming, and when one bullet mixes work from more than one person, credit the
specific clause that person wrote (see the existing 0.10/0.12 entries in
`RELEASE-HISTORY.md` for the mid-bullet style).

Every version bump also checks for new contributors: accepted pull requests
(`gh pr list --state merged --limit 50 --json number,title,author,mergedAt`, plus
`git log --format='%an <%ae>' | sort -u` for work merged by hand) and reporters
whose issue's fix ships in this release. Each new contributor gets an entry in
`kContributors` (`src/ui/app_credits.cpp`: code contributors first in the order
their first pull request merged, then issue reporters in the order their first
issue was resolved). The list feeds only the About dialog (update the
`splashContributors` check in `tests/ui/app_shell_tests_language_chrome.cpp`; the start panel
shows no contributors) and is mirrored, same order, in the README's "Incredible
people" line under Credits. Credit the GitHub handle, never the person's real or
display name (they may not want it broadcast), linked to
`https://github.com/<handle>`.

## Build and upload order

Before publishing, run both full native suites and the full
`tests/mcp_client_tests.py` suite on Windows, macOS, and Linux. Repeat the MCP
suite against each staged desktop package, including inside the Flatpak sandbox;
install the current user bundle and run `tests/flatpak_mcp_tests.py` on the Linux host to verify attachment between
separate app and connector sandboxes using their default endpoint.
For the AppImage, run `make-appimage.sh --tests` (both full suites on its gcc-12 / Qt 6.8.3
toolchain), `appimage/test-appimage.sh` on the built file, the MCP suite through the
AppImage, and `tests/appimage_mcp_tests.py` (attach across two FUSE launches) ([appimage.md](appimage.md), "Testing").
The packagers' `--check` smoke tests do not cover the full protocol or attachment
lifecycle. Use isolated offscreen workspaces and run test processes sequentially
on each machine. See [testing.md](testing.md) for the client dependency and commands.
WebAssembly does not ship `patchy-mcp`; build the web app and run the full wasm
core suite. The `wasm` builder does both on the remote wasm host ([release-wasm.md](release-wasm.md#remote-web-build)).

The `verify-windows`, `verify-mac` and `verify-linux` targets of `release-all-automated.bat`
run all of the above after the builders (scripts in `scripts\release`: `verify-windows.ps1`;
`verify-remote.ps1 -Target mac|linux`, which runs the full suites through `remote-build.ps1`
and then pipes `verify-mac.sh` / `verify-linux.sh` to the host). Each reads the version from
`CMakeLists.txt`, logs to `build\release-logs\verify-<target>*.log` with `.exit` markers,
and passes only with exit 0, no `[FAIL]` and, on the hosts, the script's closing
`ALL ... RELEASE CHECKS PASSED` line. The suite steps also print each suite's `[SUMMARY]`
and skip count and fail on a skipped test missing from that host's untracked expected-skip
list, `local-test-fixtures\expected-skips\<host>-<suite>.txt`; a host without a list gets its
skips printed and a candidate list beside the log to review and install
([testing-skips.md](testing-skips.md#expected-skip-lists)). `verify-linux.sh` installs the Flatpak bundle for the
host's user, and stops (never deletes) when a `PatchyMcp-*` endpoint is left in
`/run/user/<uid>/app/com.rtsoft.patchy`: check nothing listens on it, then remove it by hand.
Skips for a re-verification after a narrow change: `verify-windows.ps1 -SkipCore` /
`-SkipUi`, `verify-remote.ps1 -SkipSuites`. `verify-windows-arm64` checks the ARM64
package on the ARM64 host (next section).

### Windows ARM64 packages

The `windows` builder (`build-release.bat` with no argument) packages x64 and then Windows
ARM64 from the same tree and version: `PatchyWindowsArm64Installer.exe` and
`PatchyWindowsArm64NoInstaller.zip` (permanent names, beside the unchanged x64 ones). ARM64 is
cross-compiled with `windows-arm64-cross-release` (setup in [building.md](building.md#windows-arm64-cross-builds)),
signed like x64, staged in `build\package\staging-arm64`, and checked by
`verify-windows-package.ps1 -Arch arm64`, which on an x64 PC skips the two steps that run its
executables (the headless smoke check and `patchy-mcp --check` are skipped too). A failure
in the ARM64 half fails the builder after the x64 packages are already built and verified.
A failed signature (the token's smart card error 0x8010001F) is retried twice, 15 s apart, but
only once a file has signed in that run, so a wrong PIN is never resent.
`verify-windows-arm64` (`verify-remote.ps1 -Target windows-arm64`) runs both full suites from
a native `windows-arm64-opt-dev` build on the `windows-arm64` host (an LTCG link may not fit
its 16 GB), then copies the two packages there and runs `verify-windows-package.ps1 -Arch
arm64 -RequireRun -NoDesktop` (package self-test and connector check included; the wizard
step already ran here). The plug-in hosts stay x86 and x64 in the ARM64 package. ARM64 has
no PDF import (no qtpdf for ARM64 in Qt 6.8.3), so its `licenses\qt` has no qtpdf notice.
The installer refuses to run on an x64 or x86 PC (`PatchyArchitecture.txt` in its payload,
[packaging/windows/README.md](../packaging/windows/README.md)).

### Release order

Keep building, publishing, and announcing in this order:

1. Bump the build version and finalize the README (the Windows zip/installer embed it). Leave `latest_version.json` at the last published version.
2. Run `scripts\release\release-all.bat` and the required tests above (the `verify-*` targets). The four builders produce Windows, macOS, Linux, and the web build, deleting previous artifacts first.
3. Commit the exact tree used for the verified packages. This is the build commit the release tag must name.
4. Stop until Seth explicitly says to publish (asking to do or continue the release is not that). Then run `scripts\release\upload-to-rtsoft.bat` for the Flatpak repository and web site (`upload-to-rtsoft.bat desktop`, target `upload-desktop`, when the release ships no new web build: it leaves the web site alone instead of uploading the previous build again), then push the build commit to `origin/main`.
5. Run `scripts\release\publish-github-release.bat` ([release-process-github.md](release-process-github.md)). Verify that the release is public and its download URLs serve the new artifacts.
6. Update the shipped platforms in `latest_version.json` (windows: the installer keys too), then commit and push that announcement separately. No package rebuild is needed for this manifest-only follow-up; the tag stays on the build commit.

Never announce first: during the draft upload, `latest/download` still resolves to the previous public release. An early manifest push can offer the new version while its download is still the old file.

## Release safety checks

These checks keep a broken build from looking like a shipped one. Keep every escape hatch explicit and opt-in, and keep the safe behavior the default.

- **Signing is mandatory.** `packaging/macos/make-dmg.sh` errors under `PATCHY_REQUIRE_SIGNING=1` (set by `scripts\remote\release-mac.ps1`) and requires `spctl` to report `source=Notarized Developer ID` on the finished dmg; `build-release.bat`'s `:SignFile` errors unless `PATCHY_ALLOW_UNSIGNED=1` is set for a deliberately unsigned local build. A missing `~/.patchy-release-env` (mac) or `RT_PROJECTS` (Windows) is a failure, not a "signing skipped" line. The `stapler validate` hang to avoid is in [packaging/macos/README.md](../packaging/macos/README.md).
- **Uploads are verified.** The Flatpak repository goes through `scripts\release\upload-one-file.bat` (`upload-flatpak-repo-to-rtsoft.bat`), which fails on a bad `scp` and then compares the local SHA-256 against one the server computes over what landed; `upload-to-rtsoft.bat` collects failures and ends with a named summary and a non-zero exit. Never call `scp` or `%RT_PROJECTS%\UploadFileToRTsoftSSH.bat` directly for a release artifact. (`upload-wasm-to-rtsoft.bat` does its own multi-file transfer plus a live COOP/COEP header check, which is why it stays separate.) Every upload script takes the server account from `upload-target.bat` (the `upload` entry of `scripts/remote/hosts.local.json`), never from the script itself.
- **Packages must run headless.** `--headless` is a shipped feature, so every packager runs the staged `patchy.exe --headless --run-script` on a one-line script and fails unless the output ends in `[done]`: `build-release.bat` copies `qwindows.dll` and `qoffscreen.dll` itself (`:CopyRequiredPlatformPlugins`; windeployqt deploys only the former) before `:HeadlessSmokeCheck`; `packaging/macos/make-dmg.sh` copies `libqoffscreen.dylib` after macdeployqt and runs the same check; `packaging/linux/make-flatpak.sh` runs it inside the built sandbox with `flatpak-builder --run`. Those three run before any artifact is written. `packaging/linux/make-appimage.sh` runs it after its ABI audit, in the 22.04 container and again on the host through FUSE ([appimage.md](appimage.md)). `release-linux.ps1` builds with `-SkipTests`, so this smoke check is the only thing that executes the Flatpak on release day; the suites run in the Flatpak SDK through `remote-build.ps1` during per-change verification.

- **The Linux metainfo describes the release.** `release-all.bat`, `release-all-automated.bat`, `release-linux.ps1` and `publish-github-release.ps1` run `scripts\release\check-metainfo.py` (Python 3 on PATH) and stop when a screenshot URL pins a tag whose image differs from the tree's, pins a tag that does not exist, declares a wrong size, or when the version has no `<release>` entry. Between releases the pins name the next tag, so the check reports the pending bump; the version bump with `--fix` makes it pass. A backfill (`-Target`) skips it.

- **The built Windows packages are unpacked and exercised** (an installer nothing ran once shipped dead at its first window, issue 55). After signing, `build-release.bat` runs `scripts\release\verify-windows-package.ps1`: it unpacks the installer without running it, runs the wizard in `-SmokeTest` mode, checks the zip against its manifest, requires every DLL import to be in the package or part of Windows (System32 copies of Qt and the MSVC runtime do not count), and runs `packaging\package-selftest.js` on the unpacked `patchy.exe`. A failing package is moved to `build\package\rejected`. Details in [packaging/windows/README.md](../packaging/windows/README.md). It does not click through the wizard, so after a change to the installer script, run the built installer by hand before publishing.

After any upload, the claim "it shipped" needs evidence: the helper's `Verified <name> (sha256 ...)` line, or `curl -sI` against the public URL.

Do not say a release was created unless the release preset build completed successfully.
