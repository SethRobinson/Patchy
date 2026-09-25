# Release process

How to cut and publish a Patchy release. Read this in full before bumping a version or running any release batch file. The per-change build/test handoff (every code change must refresh `build\release\patchy.exe`) is separate and lives in AGENTS.md.

## Version bump checklist

Desktop packages include `patchy-mcp` and the assembled `patchy-control` skill.
Staging and resource paths are specified in [ai-control.md](ai-control.md). Each
desktop packaging script runs the installed connector's `--check` smoke test;
Windows signs both executables and macOS deploys Qt for both. The remote build
helper builds with every core but four (`getconf _NPROCESSORS_ONLN` minus 4, at
least one) and runs builds/tests with lower priority. The Flatpak packager runs its
sandbox build at lower priority with the same job count.

When bumping the release version, update the version fields:

- `CMakeLists.txt` (`project(... VERSION x.y)`). The version reaches the code through the configure-time generated `patchy_version.hpp` (from `cmake/patchy_version.hpp.in`), not a target-wide define, so a bump recompiles only the handful of files that include it rather than all of `patchy_ui`.
- `latest_version.json`: the per-platform `version` entries: windows always; macos/linux only when those artifacts actually ship. This is the update-check manifest served to the app from raw.githubusercontent.com on main, and only takes effect once pushed.
- The `<release>` tag in `packaging/linux/com.rtsoft.patchy.metainfo.xml`
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
- The code contributor credits, for any pull request accepted since the last
  release (see "What's New author credits" below).

## What's New author credits

Always credit the correct author on each "What's New" bullet, including entries
moved to `RELEASE-HISTORY.md`. Seth is the default and is left uncredited; any
feature or fix contributed by someone else must name them with a GitHub handle
link like `([@handle](https://github.com/handle))`. Check `git log`'s author for
the commits behind each bullet (e.g. `git log --format='%an %s'`) rather than
assuming, and when one bullet mixes work from more than one person, credit the
specific clause that person wrote (see the existing 0.10/0.12 entries in
`RELEASE-HISTORY.md` for the mid-bullet style).

Every version bump also checks for newly accepted pull requests (Seth, September
2026): `gh pr list --state merged --limit 50 --json number,title,author,mergedAt`,
plus `git log --format='%an <%ae>' | sort -u` for work merged by hand. Each new
contributor gets an entry in `kContributors` (`src/ui/app_credits.cpp`, which
feeds the About dialog and the start panel; update the `splashContributors` and
`startPanelContributors` checks in `tests/ui/app_shell_tests.cpp`) and in the
README's "Code contributions from" line under Credits, in merge order. Credit
the GitHub handle, never the person's real or display name (they may not want
it broadcast), linked to `https://github.com/<handle>`.

## Build and upload order

Before publishing, run both full native suites and the full
`tests/mcp_client_tests.py` suite on Windows, macOS, and Linux. Repeat the MCP
suite against each staged desktop package, including inside the Flatpak sandbox;
install the current user bundle and run `tests/flatpak_mcp_tests.py` on the Linux host to verify attachment between
separate app and connector sandboxes using their default endpoint.
The packagers' `--check` smoke tests do not cover the full protocol or attachment
lifecycle. Use isolated offscreen workspaces and run test processes sequentially
on each machine. See [testing.md](testing.md) for the client dependency and commands.
WebAssembly does not ship `patchy-mcp`; build both web variants and run the full
wasm core suite.

Build order matters: finalize the README first (the Windows zip/installer embed a copy), then `scripts\release\release-all.bat` (four consoles: local Windows and wasm builds plus remote mac/linux; every builder deletes its previous artifacts up front so a failed build can never leave stale files for the upload scripts), then `scripts\release\upload-to-rtsoft.bat` (the mirror plus the wasm site), then commit and push the release commit, then `scripts\release\publish-github-release.bat` (the canonical download, see below). Push and publish are the last two steps on purpose: the GitHub tag and the update manifest both name the commit the packages were built from, so that commit must exist before the tag does.

## The GitHub release (canonical downloads)

GitHub Releases is where users download Patchy (issue 26); rtsoft.com stays a permanent mirror (Seth, September 2026). `scripts\release\publish-github-release.bat` (a wrapper over `publish-github-release.ps1`, `nopause`/`NO_PAUSE` convention, `upload-github` target in `release-all-automated.bat`) publishes tag `v<major>.<minor>` with the four desktop artifacts under their stable names plus `SHA256SUMS.txt`, and release notes taken from that version's "What's New" entry in `README.md` (or `RELEASE-HISTORY.md` once it has moved). It needs `gh` logged in as the repo owner (`gh auth status`).

- **Stable asset names inside versioned tags.** `README.md`, `latest_version.json`, and the Linux one-liner use `https://github.com/SethRobinson/Patchy/releases/latest/download/<name>` permalinks. Those only resolve because every release uploads the same four names (`PatchyWindowsInstaller.exe`, `PatchyWindowsNoInstaller.zip`, `PatchyMacOS.dmg`, `PatchyLinux.flatpak`); the script renames the versioned `Patchy-<version>.dmg`/`.flatpak` while staging under `build\github-release\v<version>` and never touches the stable-name staging copies in `build\package` (those belong to the rtsoft upload scripts and are whatever shipped last). Do not rename an asset or drop one: a permalink would 404 and the in-app update check would offer a dead link.
- **The tag points at the commit the packages were built from.** Without `-Target` the script refuses unless HEAD is clean and equal to `origin/main`, so push first. Build from the committed release state, not from a tree with later edits: 0.98 was built after the version-bump commit and tagged at the follow-up "Tweaks for the V0.98 release" commit (`180e21ff`), which is why `-Target <full sha>` exists, together with `-Version` and `-AssetDir` for a backfill or republish from saved files. `--target` must be a full SHA for `gh`.
- **Safe from any shell.** The `.bat` resets `PSModulePath` to the Windows PowerShell 5.1 set (the pwsh 7 problem in "Agent/non-interactive runs" below) and the `.ps1` hashes through .NET rather than `Get-FileHash`, so it also runs from pwsh 7 or the Claude terminal. The first v0.98 backfill was launched from pwsh 7 before either guard existed and published a `SHA256SUMS.txt` with empty hashes; the script now fails instead of writing a blank hash. To replace one asset on a public release by hand: `gh.exe release upload v<version> <file> --clobber`. Write `gh.exe`, not `gh`, in an interactive PowerShell: Seth's profile aliases `gh` to `Get-History`, which fails with "Cannot bind parameter 'Count'". The script removes that alias for itself.
- **Draft, verify, then publish.** The release is created as a draft (invisible to visitors, and no tag yet), every asset's size is read back and compared with the local file, and only then is it flipped public and marked Latest, which is when GitHub creates the tag. A failure at any point leaves at most a draft; re-running reuses it and re-uploads with `--clobber`. A tag that is already public is never modified: fix forward with a new version. The "it shipped" evidence is the script's `Published v<version>: <url>` line, or `gh release view v<version> --json assets`.
- The update manifest (`latest_version.json`) is fetched from `main` on raw.githubusercontent.com and its `download_url` entries are the `latest/download` permalinks, so once the release is public and the manifest's version is pushed, older builds are offered the GitHub file. The checker accepts any http(s) URL; no code change is needed to move a download.

`build\package` must never hold a previous version's files once a new build starts (Seth, September 2026). The mac and Linux builders write versioned artifacts (`Patchy-<version>.dmg`, `Patchy-<version>.flatpak`); the upload scripts copy the newest one over the published names `PatchyMacOS.dmg` and `PatchyLinux.flatpak` and upload that copy, so those unversioned files are upload staging copies of whatever shipped LAST. `release-mac.ps1` and `release-linux.ps1` delete them along with the old versioned artifacts, and the Windows packager deletes its own final-named outputs before rebuilding. An agent that builds packages by hand must do the same delete before reporting the folder as release-ready.

## Release safety checks

Three checks keep a broken build from looking like a shipped one. Keep every escape hatch explicit and opt-in, and keep the safe behavior the default.

- **Signing is mandatory.** `packaging/macos/make-dmg.sh` errors under `PATCHY_REQUIRE_SIGNING=1` (set by `scripts\remote\release-mac.ps1`) and requires `spctl` to report `source=Notarized Developer ID` on the finished dmg; `build-release.bat`'s `:SignFile` errors unless `PATCHY_ALLOW_UNSIGNED=1` is set for a deliberately unsigned local build. A missing `~/.patchy-release-env` (mac) or `RT_PROJECTS` (Windows) is a failure, not a "signing skipped" line. The `stapler validate` hang to avoid is in [packaging/macos/README.md](../packaging/macos/README.md).
- **Uploads are verified.** Every desktop artifact goes through `scripts\release\upload-one-file.bat`, which fails on a bad `scp` and then compares the local SHA-256 against one the server computes over what landed; `upload-to-rtsoft.bat` collects per-platform failures and ends with a named summary and a non-zero exit. Never call `scp` or `%RT_PROJECTS%\UploadFileToRTsoftSSH.bat` directly for a release artifact. (`upload-wasm-to-rtsoft.bat` does its own multi-file transfer plus a live COOP/COEP header check, which is why it stays separate.)
- **Packages must run headless.** `--headless` is a shipped feature, so every packager runs the staged `patchy.exe --headless --run-script` on a one-line script and fails unless the output ends in `[done]`: `build-release.bat` copies `qwindows.dll` and `qoffscreen.dll` itself (`:CopyRequiredPlatformPlugins`; windeployqt deploys only the former) before `:HeadlessSmokeCheck`; `packaging/macos/make-dmg.sh` copies `libqoffscreen.dylib` after macdeployqt and runs the same check; `packaging/linux/make-flatpak.sh` runs it inside the built sandbox with `flatpak-builder --run` (the plugin comes from the org.kde.Platform runtime). All three run before any artifact is written. `release-linux.ps1` builds with `-SkipTests`, so this smoke check is the only thing that executes the Flatpak on release day; the suites run natively on the linux build host through `remote-build.ps1` during per-change verification.

After any upload, the claim "it shipped" needs evidence: the helper's `Verified <name> (sha256 ...)` line, or `curl -sI` against the public URL.

## The wasm (web) release

The web build ships with the desktop releases because the site redeploy is its only update mechanism (the in-app update check is stubbed out on wasm). Three scripts, all in `scripts\release` (plus `build-wasm-and-upload-to-rtsoft.bat`, a convenience wrapper that chains the build and upload scripts by full path and stops with an error, honoring `nopause`/`NO_PAUSE`, instead of uploading when the build fails):

- `build-wasm.bat` builds the `wasm-release` preset and stages only the deployable files into `build\package\wasm-site`: `patchy.js`, `patchy.wasm`, `patchy.data`, `qtloader.js`, their precompressed `.br`/`.gz` variants (written by `scripts\wasm\precompress-site.mjs` on the emsdk-bundled node; `.htaccess` serves them via `AddEncoding` plus `RemoveType` and IfModule-guarded rewrite rules keyed on Accept-Encoding and file existence, identity fallback without the modules, and `serve.mjs` mirrors the negotiation locally; first-visit transfer drops from ~92 MB to ~29 MB. The progress bar counts decompressed bytes, so the uncompressed wasm size is baked into the page as `__PATCHY_WASM_SIZE__` for the total; html stays identity-encoded), `patchy-logo.png` (the 256px Linux packaging icon), `NOTICE-THIRD-PARTY.md`, `libheif-COPYING.txt`, `.htaccess` (from `packaging\web`), and the shell page as both `patchy.html` and `index.html` (so `rtsoft.com/patchy/` serves the app directly). The notice and license copy are required by the browser build's LGPL libheif parser and must stay linked from the loading page. The homepage, GitHub source, and license links navigate the top-level page because the loading page may be inside the interface-scale iframe, where opening a new tab can be rejected by browser popup policy. The page is not Qt's generated shell: it is configured from `packaging\web\patchy.html.in`, which shows the Patchy logo, name, version, and a real download progress bar. The script substitutes `__PATCHY_VERSION__` (from CMakeLists.txt, same extraction as `build-release.bat`) and `__PATCHY_CACHE_TAG__` (version plus a build timestamp). Every asset URL in the page carries `?v=<cache tag>`, so each deploy busts browser caches; `.htaccess` marks the html `no-cache` (revalidated every load) and the tagged assets cacheable forever, with all directives IfModule-guarded so a host missing a module degrades instead of erroring. The staging directory is deleted up front, so a failed build leaves nothing for the upload script. `release-all.bat` runs this in its fourth console. The shell page's text is static html outside the Qt localization system and stays English.
- `start-local-wasm-server.bat` serves the staged `build\package\wasm-site` payload (the exact files an upload would publish) on `http://localhost:8973/` using the emsdk-bundled node and `scripts\wasm\serve.mjs`. An optional first argument overrides the port. It is safe to run repeatedly: it first frees the port through `scripts\wasm\free-server-port.ps1`, which stops a node server left listening by an earlier run (and waits for the socket to clear, since `Stop-Process` returns before the port is released) but reports and leaves any non-node process alone rather than killing someone else's program. Then it starts the server in the foreground, still Ctrl+C to stop, and `serve.mjs --open` hands the URL to the default browser from its `listen` callback, which is the only moment that cannot race the server. That helper is a separate `.ps1` and must stay one: cmd cannot parse literal parentheses inside a `for /f` backquote command, which PowerShell needs for subexpressions, so an inline one-liner dies with `( was unexpected at this time`. Error paths honor `NO_PAUSE`. For serving the raw `build\wasm-release` directory during development, use `start-local-wasm-test-server.bat` (same port-freeing, wraps `scripts\wasm\serve-app.ps1` passing `--open` through to the same `serve.mjs` browser-opening path, and serves `patchy.html` from Qt's generated shell rather than the branded one); the two scripts name each other in their headers because running the wrong one is otherwise an easy mistake to make.
- `upload-wasm-to-rtsoft.bat` verifies the staged files, creates `www/patchy` on the server, and uploads everything in one error-checked `scp` with the html files listed last, so a visitor loading mid-upload never gets new html pointing at assets that are not up yet. After the upload it curls the live site and fails loudly if the `Cross-Origin-Opener-Policy` or `Cross-Origin-Embedder-Policy` header is missing: the multithreaded build needs cross-origin isolation for SharedArrayBuffer, and the IfModule guard in `.htaccess` would otherwise drop the headers silently on a host without `mod_headers`. The same curl only warns when `Content-Encoding` is missing (identity serving still works, just slower). It talks to ssh/scp directly rather than through `UploadFileToRTsoftSSH.bat` (that helper is single-file and ignores transfer failures). It honors the same positional `nopause` argument as the other per-platform upload scripts, and `upload-to-rtsoft.bat` calls it after the desktop uploads.

There are no versioned wasm artifacts: the site serves stable names and a redeploy replaces them in place.

`build-wasm.bat` also needs the `wasm-release-st` preset and stages that single-threaded artifact under `st/` in the site directory. By default that variant is built on the Windows offload host while the multithreaded one builds locally (the two took well over an hour back to back): the batch file starts `scripts\remote\build-wasm-st-remote.ps1` in the background (snapshot push, `scripts\remote\build-wasm-st.ps1` on the host, scp of the four output files into local `build\wasm-release-st`; log and `exit=` marker in `build\release-logs\wasm-st-remote.*`), builds `wasm-release` itself, then waits for the marker (three-hour deadline) and appends the remote log to its own before staging. `PATCHY_WASM_ST_LOCAL=1`, or a `hosts.local.json` without a `windows` entry, builds both variants locally as before. The host's work tree is shared with `remote-build.ps1 -Target windows`, so do not run the two at once (four files plus their precompressed variants; the page serves it to Safari/WebKit visitors, see [wasm.md](wasm.md) and [wasm-memory.md](wasm-memory.md); the uncompressed st wasm size is baked in as `__PATCHY_WASM_SIZE_ST__`). Both upload scripts create the remote `st/` directory and push that set before the main one, keeping the html files last overall. `build-wasm.bat` additionally stages the memory-diagnostics harness (`stress-harness.html` from `scripts\wasm`, cache-tag substituted, plus `memsoak.js`). The production upload list deliberately excludes the harness; `upload-wasm-to-rtsoft-beta.bat` publishes the same staged site plus the harness to the staging copy at `rtsoft.com/patchy-beta` (same nopause convention and COOP/COEP checks) for Safari/iOS device testing. The diagnostics workflow lives in [performance.md](performance.md).

## A running app or connector must not block the Windows relink

`build\release\patchy-mcp.exe` is usually running (an MCP client such as the Codex app keeps one `--attach` connector alive per thread), and Seth often has `build\release\patchy.exe` open, either of which would make its link fail with `LNK1104`. Never kill either. Both targets run the same PRE_LINK step (`cmake/unlock_locked_executable.cmake`), which renames a locked executable to `<name>.stale-<timestamp>.exe` (the process keeps running from the renamed file), the build links a fresh one that the packager signs and stages, and every later link deletes stale copies nothing runs any more.

`build-release.bat` additionally runs that script on both executables before it configures, so the packaged files can only come from the build it just ran: a link that fails leaves no executable and the script stops at its "was not created" check. This closed the September 25, 2026 hole where a locked `patchy.exe` failed the link and the script packaged the previous binary anyway, because `cmake --build` reported the failure as exit code -1 and `if errorlevel 1` is false for negative values (see the batch-file section below). Stale copies are build artifacts under the housekeeping rule in AGENTS.md and must never be packaged (the packager stages `patchy-mcp.exe` by name). Do not copy the connector elsewhere to dodge the lock: its `--attach` socket name hashes the executable's own folder, so a connector outside `build\release` cannot attach to a Patchy running from it.

## Batch files live in scripts\release and call their siblings by full path

Each release and upload batch file derives the repo root from its own location (`%~dp0..\..`) and cds there, so it runs from any launch cwd; `release-all.bat` reaches the mac/linux wrappers as `%~dp0..\remote\release-*.bat`. Do not "simplify" those relative hops: they encode the scripts' depth below the repo root.

Non-interactive shells set `NoDefaultCurrentDirectoryInExePath`, so cmd will not resolve a bare command name out of the current directory: `cmd /c "build-release.bat"` from inside `scripts\release` fails with `'build-release.bat' is not recognized`, while a path containing a separator (`scripts\release\build-release.bat` from the repo root) resolves fine. That is why `release-all.bat` and `upload-to-rtsoft.bat` launch their siblings as `"%~dp0name.bat"`; keep it that way, since a bare-name launch dies before the delete-previous-artifacts step and leaves the previous version's files for the newest-file upload scripts to pick up.

Never test a build or test step with `if errorlevel 1`: it means "errorlevel is 1 or greater", so the negative codes a failed `cmake --build` (-1 when a link fails) or a crashed test binary (-1073741819) return pass straight through. Compare against 0 instead, `if not "!ERRORLEVEL!"=="0" goto fail` under `EnableDelayedExpansion` (`build-release.bat`) or `if not "%ERRORLEVEL%"=="0"` on its own top-level line (`build-wasm.bat`); `||` also fires for any nonzero code.

Inside a parenthesized block such as `if errorlevel 1 ( ... )`, an unescaped `)` in echo text closes the block early and the resulting parse error ends the whole calling chain, even when the condition is false. Escape as `^)` or reword.

Batch files must have CRLF line endings on disk (`.gitattributes` says so, but a file written by a tool with LF, or checked out before the attribute existed, stays LF). cmd re-seeks the file after a parenthesized block and gets the offset wrong in an LF-only file, so a later line starts mid-word: `publish-github-release.bat` once ran `ile ...` instead of `powershell ... -File ...` and died with 9009. When a `.bat` fails with a truncated command name, check `git ls-files --eol` and convert to CRLF (`git checkout -- <file>` for an unmodified file, otherwise `perl -pi -e 's/\r?\n/\r\n/'`).

## scripts\vs-env.bat, not VsDevCmd.bat

Every build entry point (`scripts\release\build-release.bat`, `scripts\run-tests.ps1`, `scripts\make-readme-screenshots.ps1`, the handoff command in AGENTS.md) enters the developer environment through `scripts\vs-env.bat`, which forwards its arguments to VsDevCmd.bat. It is the only place that knows where Visual Studio is installed, and it prepends the VS Installer directory to `PATH`, which is what silences the harmless but alarming `'vswhere.exe' is not recognized` line. If some caller prints that line, check whether it went through vs-env.bat rather than chasing the message.

## Agent/non-interactive runs

1. Launch from cmd or Windows PowerShell 5.1, not Git Bash and not pwsh 7. Bash mangles the quoted `start "<title>"` and Seth gets a modal "Windows cannot find" dialog while nothing launches. pwsh 7 puts its own module directories on `PSModulePath`, the `powershell` 5.1 one-liners inside the scripts then load an incompatible `Microsoft.PowerShell.Utility`, `Get-FileHash` is "not recognized", and `upload-one-file.bat` refuses every desktop upload. From pwsh 7, reset `PSModulePath` first to `%USERPROFILE%\Documents\WindowsPowerShell\Modules;%ProgramFiles%\WindowsPowerShell\Modules;%SystemRoot%\system32\WindowsPowerShell\v1.0\Modules`.
2. Run `cmd /c scripts\release\release-all-automated.bat` (no arguments: the four builders; or name targets such as `windows wasm`, `upload-wasm`, or `upload-all` to run a subset). It starts every target through `scripts\release\release-worker.bat`, which sets `NO_PAUSE=1`, `CMAKE_BUILD_PARALLEL_LEVEL=20` (local Windows and wasm builds; the remote mac/Linux helpers use every core but four), and the Windows PowerShell 5.1 `PSModulePath` itself, logs to `build\release-logs\<target>.log`, and writes `exit=<code>` to `build\release-logs\<target>.exit` when the target ends. `NO_PAUSE=1` is what keeps `%RT_PROJECTS%\Signing\sign.bat` and the upload scripts from waiting on a key. Never launch a builder or upload script by hand for an unattended run: without `NO_PAUSE` it stops on a `pause` with no marker, which looks like a hung build.
3. Wait for the `.exit` files, never for the consoles (`release-mac.bat` and `release-linux.bat` end in an unconditional `pause`, which is why the worker calls the `.ps1` files directly). Judge each target by its code and its log. Do not capture exit codes with Windows PowerShell 5.1's `Start-Process -PassThru` while redirecting output: its `ExitCode` comes back empty there. If you write your own marker, put the redirect first (`>"marker" echo exit=%ERRORLEVEL%`), never `echo %ERRORLEVEL%> "marker"`: cmd reads a digit directly before `>` as a file-handle number, so a zero exit redirects stdin and leaves the marker empty.
4. In any wrapper of your own, invoke the test binaries as `.\patchy_core_tests.exe`, a path (a bare name exits 9009 under `NoDefaultCurrentDirectoryInExePath`), and directly, not through a second `start "" /b /wait`, which would make the recorded `%ERRORLEVEL%` always 0. Where a single command does need throttling, `scripts\run-throttled.bat` both lowers priority and returns the child's code.
5. Run the suites one at a time and not alongside a build (they share the QSettings store; see [testing.md](testing.md)). The tests themselves tolerate a loaded machine: wall-clock limits are hang guards, not performance bounds.
   The remote full-suite runs (`scriptsemoteemote-build.ps1 -Target mac|linux` without `-SkipTests`) print no summary: `build-and-test.sh` ends with the UI suite, so a finished log simply stops after the last test's `[PASS]`/`[FAIL]` line. Judge the run by its exit code and `[FAIL]` count, run it as a harness-tracked foreground or background command rather than a detached wrapper that writes a marker file (a detached wrapper can die with the marker unwritten), and give every wait a deadline.
6. `upload-to-rtsoft.bat` passes the positional `nopause` argument to the per-platform upload scripts and skips its own final `pause` when `NO_PAUSE` is set (the worker's `upload-all` target); run by hand, it still pauses so the summary stays readable.

Do not say a release was created unless the release preset build completed successfully.
