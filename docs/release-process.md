# Release process

How to cut and publish a Patchy release. Read this in full before bumping a version or running any release batch file. The per-change build/test handoff (every code change must refresh `build\release\patchy.exe`) is separate and lives in AGENTS.md.

Unchanged development builds keep their binaries and build date. Packaging must
request a new date before its build: the Windows/web builders, macOS packager and
AppImage container run `patchy_refresh_build_stamp`; the Flatpak packager generates
a manifest with a fresh `PATCHY_BUILD_STAMP_REFRESH_TOKEN`, invalidating the cached
app module. Hand-run packaging follows the same rule. Do not enable the refresh
target in the default build, which would force every unchanged build to relink.

## Version bump checklist

Desktop packages also carry automatic selection: ONNX Runtime plus the 19 MB MobileSAM pair in `ai-models/` (12 MB compressed); Object Selection's High Quality model (234 MB, release `ai-models-2`) is a user download, never packaged ([ai-selection.md](ai-selection.md)). Every build host runs `scripts\setup-ai-deps.ps1` (Windows) or `bash scripts/setup-ai-deps.sh` (mac/Linux) once per pinned version, and the mac host also `scripts/setup-openmp-mac.sh` (G'MIC's OpenMP, [gmic.md](gmic.md)) and `scripts/setup-qt-cocoa-patch-mac.sh` (patched Qt cocoa plugin, [building.md](building.md)); `build-release.bat` configures with `-DPATCHY_REQUIRE_AI_SELECTION=ON` and refuses to package without them, the Flatpak fetches the same pinned files itself, and `packaging/package-selftest.js` runs both models in the unpacked package.

Desktop packages include `patchy-mcp` and the assembled `patchy-control` skill; the Windows package also carries the two legacy plug-in hosts `patchy-8bf-host32.exe` and `patchy-8bf-host64.exe` (docs/plugins.md), which `build-release.bat` requires, stages and signs like the app.
Staging and resource paths are specified in [ai-control.md](ai-control.md). Each
desktop packaging script runs the installed connector's `--check` smoke test;
Windows signs both executables and macOS deploys Qt for both. The remote build
helper builds with every core but four (`getconf _NPROCESSORS_ONLN` minus 4, at
least one) and runs builds/tests with lower priority. The Linux builder also writes the
signed Flatpak update repository ([packaging/linux/README.md](../packaging/linux/README.md))
and the AppImage ([appimage.md](appimage.md)).

When bumping the release version, update the version fields:

- `CMakeLists.txt` (`project(... VERSION x.y)`). The version reaches the code through the configure-time generated `patchy_version.hpp` (from `cmake/patchy_version.hpp.in`), not a target-wide define, so a bump recompiles only the handful of files that include it rather than all of `patchy_ui`.
- Keep `latest_version.json` at the last published version until the new GitHub release and its downloads are verified. Then update its per-platform `version` entries in a separate announcement commit: windows always, with its three installer keys ([auto-update.md](auto-update.md); the publish script prints them), and `windows-arm64` with the same version and its own `download_url` and installer keys (an x64 copy emulated on an ARM64 PC reads that entry first); macos/linux only when those artifacts ship. From the first release that ships the AppImage on, the `linux` entry also carries `appimage_url` and the three `appimage_install_*` keys for its one-click update (the publish script prints all four; [auto-update.md](auto-update.md)). Pushing this manifest to main announces the update independently of release publication, so it deliberately differs from an unpublished build.
- A `<release>` with a short `<description>` in `packaging/linux/com.rtsoft.patchy.metainfo.xml`,
  then `python scripts\release\check-metainfo.py --fix`: it repoints the metainfo's screenshot
  URLs at `v<version>` when `docs/images/screenshots/` changed since the tag they pin, and
  must end in `passed` (1.17 and 1.18 shipped the v1.16 pictures; [packaging/linux/README.md](../packaging/linux/README.md)).
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
`splashContributors` check in `tests/ui/app_shell_tests.cpp`; the start panel
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
core suite.

The `verify-windows`, `verify-mac` and `verify-linux` targets of `release-all-automated.bat`
run all of the above after the builders (scripts in `scripts\release`: `verify-windows.ps1`;
`verify-remote.ps1 -Target mac|linux`, which runs the full suites through `remote-build.ps1`
and then pipes `verify-mac.sh` / `verify-linux.sh` to the host). Each reads the version from
`CMakeLists.txt`, logs to `build\release-logs\verify-<target>*.log` with `.exit` markers,
and passes only with exit 0, no `[FAIL]` and, on the hosts, the script's closing
`ALL ... RELEASE CHECKS PASSED` line. `verify-linux.sh` installs the Flatpak bundle for the
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
`verify-windows-arm64` (`verify-remote.ps1 -Target windows-arm64`) runs both full suites from
a native `windows-arm64-opt-dev` build on the `windows-arm64` host (an LTCG link may not fit
its 16 GB), then copies the two packages there and runs `verify-windows-package.ps1 -Arch
arm64 -RequireRun -NoDesktop` (package self-test and connector check included; the wizard
step already ran here). The plug-in hosts stay x86 and x64 in the ARM64 package. ARM64 has
no PDF import (no qtpdf for ARM64 in Qt 6.8.3), so its `licenses\qt` has no qtpdf notice.
The installer refuses to run on an x64 or x86 PC (`PatchyArchitecture.txt` in its payload,
[packaging/windows/README.md](../packaging/windows/README.md)). The first release that ships
ARM64 adds its rows to the README download table and platform table.

Keep building, publishing, and announcing in this order:

1. Bump the build version and finalize the README (the Windows zip/installer embed it). Leave `latest_version.json` at the last published version.
2. Run `scripts\release\release-all.bat` and the required tests above (the `verify-*` targets). The four builders produce Windows, macOS, Linux, and the web build, deleting previous artifacts first.
3. Commit the exact tree used for the verified packages. This is the build commit the release tag must name.
4. Stop until Seth explicitly says to publish (asking to do or continue the release is not that). Then run `scripts\release\upload-to-rtsoft.bat` for the mirror and web site (`upload-to-rtsoft.bat desktop`, target `upload-desktop`, when the release ships no new web build: it leaves the web site alone instead of uploading the previous build again), then push the build commit to `origin/main`.
5. Run `scripts\release\publish-github-release.bat`. Verify that the release is public and its download URLs serve the new artifacts.
6. Update the shipped platforms in `latest_version.json` (windows: the installer keys too), then commit and push that announcement separately. No package rebuild is needed for this manifest-only follow-up; the tag stays on the build commit.

Never announce first: during the draft upload, `latest/download` still resolves to the previous public release. An early manifest push can offer the new version while its download is still the old file.

## The GitHub release (canonical downloads)

GitHub Releases is where users download Patchy (issue 26); rtsoft.com stays a permanent mirror. `scripts\release\publish-github-release.bat` (a wrapper over `publish-github-release.ps1`, `nopause`/`NO_PAUSE` convention, `upload-github` target in `release-all-automated.bat`) publishes tag `v<major>.<minor>` with the desktop artifacts under their stable names plus `SHA256SUMS.txt`, and release notes taken from that version's "What's New" entry in `README.md` (or `RELEASE-HISTORY.md` once it has moved). It needs `gh` logged in as the repo owner (`gh auth status`).

- **Stable asset names inside versioned tags.** `README.md`, `latest_version.json`, and the Linux one-liner use `https://github.com/SethRobinson/Patchy/releases/latest/download/<name>` permalinks. Those only resolve because every release uploads the same names (`PatchyWindowsInstaller.exe`, `PatchyWindowsNoInstaller.zip`, `PatchyWindowsArm64Installer.exe`, `PatchyWindowsArm64NoInstaller.zip`, `PatchyMacOS.dmg`, `PatchyLinux.flatpak`, `PatchyLinux.AppImage`, `PatchyLinux.AppImage.zsync`); the script renames the versioned `Patchy-<version>.dmg`/`.flatpak`/`-x86_64.AppImage(.zsync)` while staging under `build\github-release\v<version>` and never touches the stable-name staging copies in `build\package` (those belong to the rtsoft upload scripts and are whatever shipped last). Do not rename an asset or drop one: a permalink would 404 and the in-app update check would offer a dead link.
- **The tag points at the commit the packages were built from.** Without `-Target` the script refuses unless HEAD is clean and equal to `origin/main`, so push first. Build from the committed release state, not from a tree with later edits. For a backfill or republish from saved files, `-Target <full sha>` (a full SHA, as `gh` requires) names a build commit already on `origin/main`, with `-Version` and `-AssetDir`.
- **Safe from any shell.** The `.bat` resets `PSModulePath` to the Windows PowerShell 5.1 set (the pwsh 7 problem in "Agent/non-interactive runs" below) and the `.ps1` hashes through .NET rather than `Get-FileHash`, so it also runs from pwsh 7 or the Claude terminal, and it fails rather than write a blank hash into `SHA256SUMS.txt`. To replace one asset on a public release by hand: `gh.exe release upload v<version> <file> --clobber`. Write `gh.exe`, not `gh`, in an interactive PowerShell: Seth's profile aliases `gh` to `Get-History`, which fails with "Cannot bind parameter 'Count'". The script removes that alias for itself.
- **Draft, verify, then publish.** The release is created as a draft (invisible to visitors, and no tag yet), every asset's size is read back and compared with the local file, and only then is it flipped public and marked Latest, which is when GitHub creates the tag. A failure at any point leaves at most a draft; re-running reuses it and re-uploads with `--clobber`. A tag that is already public is never modified: fix forward with a new version. The "it shipped" evidence is the script's `Published v<version>: <url>` line, or `gh release view v<version> --json assets`.
- The update manifest (`latest_version.json`) is fetched from `main` on raw.githubusercontent.com and its `download_url` entries are the `latest/download` permalinks, so publish and verify the release before pushing the manifest's version. Older builds are then offered the GitHub file. The checker accepts any http(s) URL; no code change is needed to move a download. The windows installer keys must describe the exact versioned file, or the one-click update fails its checksum and falls back to the browser download.

`build\package` must never hold a previous version's files once a new build starts. The mac and Linux builders write versioned artifacts (`Patchy-<version>.dmg`, `Patchy-<version>.flatpak`, `Patchy-<version>-x86_64.AppImage`); the upload scripts copy the newest one over the published names `PatchyMacOS.dmg` and `PatchyLinux.flatpak` and upload that copy, so those unversioned files are upload staging copies of whatever shipped LAST. `release-mac.ps1` and `release-linux.ps1` delete them along with the old versioned artifacts, and the Windows packager deletes its own final-named outputs before rebuilding. An agent that builds packages by hand must do the same delete before reporting the folder as release-ready.

## Release safety checks

These checks keep a broken build from looking like a shipped one. Keep every escape hatch explicit and opt-in, and keep the safe behavior the default.

- **Signing is mandatory.** `packaging/macos/make-dmg.sh` errors under `PATCHY_REQUIRE_SIGNING=1` (set by `scripts\remote\release-mac.ps1`) and requires `spctl` to report `source=Notarized Developer ID` on the finished dmg; `build-release.bat`'s `:SignFile` errors unless `PATCHY_ALLOW_UNSIGNED=1` is set for a deliberately unsigned local build. A missing `~/.patchy-release-env` (mac) or `RT_PROJECTS` (Windows) is a failure, not a "signing skipped" line. The `stapler validate` hang to avoid is in [packaging/macos/README.md](../packaging/macos/README.md).
- **Uploads are verified.** Every desktop artifact goes through `scripts\release\upload-one-file.bat`, which fails on a bad `scp` and then compares the local SHA-256 against one the server computes over what landed; `upload-to-rtsoft.bat` collects per-platform failures and ends with a named summary and a non-zero exit. Never call `scp` or `%RT_PROJECTS%\UploadFileToRTsoftSSH.bat` directly for a release artifact. (`upload-wasm-to-rtsoft.bat` does its own multi-file transfer plus a live COOP/COEP header check, which is why it stays separate.)
- **Packages must run headless.** `--headless` is a shipped feature, so every packager runs the staged `patchy.exe --headless --run-script` on a one-line script and fails unless the output ends in `[done]`: `build-release.bat` copies `qwindows.dll` and `qoffscreen.dll` itself (`:CopyRequiredPlatformPlugins`; windeployqt deploys only the former) before `:HeadlessSmokeCheck`; `packaging/macos/make-dmg.sh` copies `libqoffscreen.dylib` after macdeployqt and runs the same check; `packaging/linux/make-flatpak.sh` runs it inside the built sandbox with `flatpak-builder --run`. Those three run before any artifact is written. `packaging/linux/make-appimage.sh` runs it after its ABI audit, in the 22.04 container and again on the host through FUSE ([appimage.md](appimage.md)). `release-linux.ps1` builds with `-SkipTests`, so this smoke check is the only thing that executes the Flatpak on release day; the suites run in the Flatpak SDK through `remote-build.ps1` during per-change verification.

- **The Linux metainfo describes the release.** `release-all.bat`, `release-all-automated.bat`, `release-linux.ps1` and `publish-github-release.ps1` run `scripts\release\check-metainfo.py` (Python 3 on PATH) and stop when a screenshot URL pins a tag whose image differs from the tree's, pins a tag that does not exist, declares a wrong size, or when the version has no `<release>` entry. Between releases the pins name the next tag, so the check reports the pending bump; the version bump with `--fix` makes it pass. A backfill (`-Target`) skips it.

- **The built Windows packages are unpacked and exercised** (an installer nothing ran once shipped dead at its first window, issue 55). After signing, `build-release.bat` runs `scripts\release\verify-windows-package.ps1`: it unpacks the installer without running it, runs the wizard in `-SmokeTest` mode, checks the zip against its manifest, requires every DLL import to be in the package or part of Windows (System32 copies of Qt and the MSVC runtime do not count), and runs `packaging\package-selftest.js` on the unpacked `patchy.exe`. A failing package is moved to `build\package\rejected`. Details in [packaging/windows/README.md](../packaging/windows/README.md). It does not click through the wizard, so after a change to the installer script, run the built installer by hand before publishing.

After any upload, the claim "it shipped" needs evidence: the helper's `Verified <name> (sha256 ...)` line, or `curl -sI` against the public URL.

## The wasm (web) release

The web build ships with every desktop release; its build, local check and upload scripts are in [release-wasm.md](release-wasm.md).

## A running app or connector must not block the Windows relink

`build\release\patchy-mcp.exe` is usually running (an MCP client such as the Codex app keeps one `--attach` connector alive per thread), and Seth often has `build\release\patchy.exe` open, either of which would make its link fail with `LNK1104`. Never kill either. Both targets run the same PRE_LINK step (`cmake/unlock_locked_executable.cmake`), which renames a locked executable to `<name>.stale-<timestamp>.exe` (the process keeps running from the renamed file), the build links a fresh one that the packager signs and stages, and every later link deletes stale copies nothing runs any more.

`build-release.bat` additionally runs that script on both executables before it configures, so the packaged files can only come from the build it just ran: a link that fails leaves no executable and the script stops at its "was not created" check (a failed link exits -1, which `if errorlevel 1` misses; see the batch-file section below). Stale copies are build artifacts under the housekeeping rule in AGENTS.md and must never be packaged (the packager stages `patchy-mcp.exe` by name). Do not copy the connector elsewhere to dodge the lock: its `--attach` socket name hashes the executable's own folder, so a connector outside `build\release` cannot attach to a Patchy running from it.

## Batch files live in scripts\release and call their siblings by full path

Each release and upload batch file derives the repo root from its own location (`%~dp0..\..`) and cds there, so it runs from any launch cwd; `release-all.bat` reaches the mac/linux wrappers as `%~dp0..\remote\release-*.bat`. Do not "simplify" those relative hops: they encode the scripts' depth below the repo root.

Non-interactive shells set `NoDefaultCurrentDirectoryInExePath`, so cmd will not resolve a bare command name out of the current directory: `cmd /c "build-release.bat"` from inside `scripts\release` fails with `'build-release.bat' is not recognized`, while a path containing a separator (`scripts\release\build-release.bat` from the repo root) resolves fine. That is why `release-all.bat` and `upload-to-rtsoft.bat` launch their siblings as `"%~dp0name.bat"`; keep it that way, since a bare-name launch dies before the delete-previous-artifacts step and leaves the previous version's files for the newest-file upload scripts to pick up.

Never test a build or test step with `if errorlevel 1`: it means "errorlevel is 1 or greater", so the negative codes a failed `cmake --build` (-1 when a link fails) or a crashed test binary (-1073741819) return pass straight through. Compare against 0 instead, `if not "!ERRORLEVEL!"=="0" goto fail` under `EnableDelayedExpansion` (`build-release.bat`) or `if not "%ERRORLEVEL%"=="0"` on its own top-level line (`build-wasm.bat`); `||` also fires for any nonzero code.

Inside a parenthesized block such as `if errorlevel 1 ( ... )`, an unescaped `)` in echo text closes the block early and the resulting parse error ends the whole calling chain, even when the condition is false. Escape as `^)` or reword.

Batch files must have CRLF line endings on disk (`.gitattributes` says so, but a file written by a tool with LF, or checked out before the attribute existed, stays LF). cmd re-seeks the file after a parenthesized block and gets the offset wrong in an LF-only file, so a later line starts mid-word (`ile ...` instead of `powershell ... -File ...`, exit 9009). When a `.bat` fails with a truncated command name, check `git ls-files --eol` and convert to CRLF (`git checkout -- <file>` for an unmodified file, otherwise `perl -pi -e 's/\r?\n/\r\n/'`).

## scripts\vs-env.bat, not VsDevCmd.bat

Every build entry point (`scripts\release\build-release.bat`, `scripts\run-tests.ps1`, `scripts\make-readme-screenshots.ps1`, the handoff command in AGENTS.md) enters the developer environment through `scripts\vs-env.bat`, which forwards its arguments to VsDevCmd.bat. It is the only place that knows where Visual Studio is installed, and it prepends the VS Installer directory to `PATH`, which is what silences the harmless but alarming `'vswhere.exe' is not recognized` line. If some caller prints that line, check whether it went through vs-env.bat rather than chasing the message.

## Agent/non-interactive runs

The automated driver launches all requested targets in parallel. Run builds, `upload-all`, and `upload-github` in separate invocations, waiting for successful exit markers before the next phase. Announce through the manifest only after publication and download verification.

1. Launch from cmd or Windows PowerShell 5.1, not Git Bash and not pwsh 7. Bash mangles the quoted `start "<title>"` and Seth gets a modal "Windows cannot find" dialog while nothing launches. pwsh 7 puts its own module directories on `PSModulePath`, the `powershell` 5.1 one-liners inside the scripts then load an incompatible `Microsoft.PowerShell.Utility`, `Get-FileHash` is "not recognized", and `upload-one-file.bat` refuses every desktop upload. From pwsh 7, reset `PSModulePath` first to `%USERPROFILE%\Documents\WindowsPowerShell\Modules;%ProgramFiles%\WindowsPowerShell\Modules;%SystemRoot%\system32\WindowsPowerShell\v1.0\Modules`.
2. Run `cmd /c scripts\release\release-all-automated.bat` (no arguments: the four builders; or name targets such as `windows wasm`, `verify-windows verify-mac verify-linux`, `upload-wasm`, `upload-all`, or `upload-desktop` to run a subset). The verify targets run after their builders, never beside a build on the same machine. It starts every target through `scripts\release\release-worker.bat`, which sets `NO_PAUSE=1`, `CMAKE_BUILD_PARALLEL_LEVEL=20` (the local builds), and the Windows PowerShell 5.1 `PSModulePath` itself, logs to `build\release-logs\<target>.log`, and writes `exit=<code>` to `build\release-logs\<target>.exit` when the target ends. `NO_PAUSE=1` is what keeps `%RT_PROJECTS%\Signing\sign.bat` and the upload scripts from waiting on a key. Never launch a builder or upload script by hand for an unattended run: without `NO_PAUSE` it stops on a `pause` with no marker, which looks like a hung build.
3. Wait for the `.exit` files, never for the consoles (`release-mac.bat` and `release-linux.bat` end in an unconditional `pause`, which is why the worker calls the `.ps1` files directly). Judge each target by its code and its log. Do not capture exit codes with Windows PowerShell 5.1's `Start-Process -PassThru` while redirecting output: its `ExitCode` comes back empty there. If you write your own marker, put the redirect first (`>"marker" echo exit=%ERRORLEVEL%`), never `echo %ERRORLEVEL%> "marker"`: cmd reads a digit directly before `>` as a file-handle number, so a zero exit redirects stdin and leaves the marker empty.
4. In any wrapper of your own, invoke the test binaries as `.\patchy_core_tests.exe`, a path (a bare name exits 9009 under `NoDefaultCurrentDirectoryInExePath`), and directly, not through a second `start "" /b /wait`, which would make the recorded `%ERRORLEVEL%` always 0. Where a single command does need throttling, `scripts\run-throttled.bat` both lowers priority and returns the child's code.
5. Run the suites one at a time and not alongside a build (they share the QSettings store; see [testing.md](testing.md)). The tests themselves tolerate a loaded machine: wall-clock limits are hang guards, not performance bounds.
   The remote full-suite runs (`scripts\remote\remote-build.ps1 -Target mac|linux` without `-SkipTests`) print no overall summary: each suite ends with its own `[SUMMARY]` line, and on mac the log stops after `patchy_mac_a11y_tests`. Judge the run by its exit code, `[FAIL]` count and each suite's skip count, run it as a harness-tracked foreground or background command rather than a detached wrapper that writes a marker file (a detached wrapper can die with the marker unwritten), and give every wait a deadline.
6. `upload-to-rtsoft.bat` passes the positional `nopause` argument to the per-platform upload scripts and skips its own final `pause` when `NO_PAUSE` is set (the worker's `upload-all` target); run by hand, it still pauses so the summary stays readable.
7. The whole flow also runs from a git worktree: the builders resolve `.deps` relative to their own repo root, so give the worktree a `.deps` junction to the main checkout's directory, and copy `local-test-fixtures` in (a worktree has no untracked files, and the fixture-gated tests otherwise skip). Remove the junction with a plain `rmdir .deps` before `git worktree remove`; a recursive delete follows it into the real Qt tree. Emscripten run through the junction prints `user library symbol '$invokeEntryPoint' depends on internal symbol` warnings on every wasm link (it takes its own pthread JS for a user library by path); they are absent from the main checkout, and switching between the junction and the real path clears the shared emscripten cache. Adding a committed PSD fixture without re-pinning `test-fixtures/psd/flatten-digests.txt` and `render-digests.txt` fails `composite_corpus_*` on every platform; re-pin from the actual digests the failing run writes, as [testing.md](testing.md) describes.

Do not say a release was created unless the release preset build completed successfully.
