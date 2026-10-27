# One-click update (Windows installer, Linux AppImage)

Read this before touching the update prompt, `latest_version.json`'s installer keys, the
installer's update mode, or the release announcement step.

On Windows, a copy of Patchy that the installer put there can update itself from the update
prompt: **Update Now** downloads the installer, closes Patchy through the normal save
prompts, installs into the same folder, and starts Patchy again. The Linux AppImage does
the same by replacing its own file ("AppImage" below). Everything else keeps the browser
download: the portable zip, a development build, macOS, the Flatpak (whose prompt shows
the `flatpak update` command), an AppImage in a read-only folder, and any manifest that
does not carry the pinned keys.

## When the startup check runs

`MainWindow::begin_startup_update_check` (called from `src/app/main.cpp` only) decides, in
this order. Every key is permanent; accessors live in `src/ui/app_settings.hpp`.

- `updates/checkOnStartup` (bool, default true) is the master switch.
- `updates/checkIntervalDays` (int: 0, 1, 7, 14 or 30; anything else reads as 0) is the
  Preferences choice beside the checkbox (GitHub issue 92; 1 is "Daily", issue 189). 0 is
  "Every launch" and stays the default. `startup_update_check_due` (`update_checker.hpp`)
  counts local calendar days from `updates/lastCheckSecsSinceEpoch`, not 24-hour spans, so
  "Daily" checks on the first launch of each day; it is also true with no record or one in
  the future.
- The record is written when a check gets an answer, never on `NetworkError`, so a launch
  without a network tries again next time.
- `updates/skippedVersion` holds the version from the prompt's "Don't ask again about
  version X" checkbox (stored on any button except Update Now). `update_prompt_wanted`
  shows the prompt only for a version newer than it. The check still runs and the start
  panel still says an update is available.

The About window and the connector's own check (below) ignore the interval and the skipped
version: neither raises a prompt.

## Flow

1. `request_update_check` (`src/ui/update_checker.cpp`) reads the manifest. The `windows`
   entry may carry three extra keys, all or none:
   `installer_url` (https, the versioned `releases/download/v<version>/...` URL, so the file
   behind it never changes), `installer_size` (bytes) and `installer_sha256` (hex).
   `update_has_installer()` is true only with all three valid. The key names are permanent.
   Builds older than this feature ignore them and use `download_url`.
2. `MainWindow::can_install_update` also needs `installed_copy_can_self_update()`
   (`src/ui/update_installer.cpp`): the per-user uninstall entry
   `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Patchy` names the folder the
   app runs from as `InstallLocation`, and `UninstallPatchy.exe` sits beside `patchy.exe`.
3. `MainWindow::download_and_install_update` (`src/ui/main_window_files.cpp`) downloads
   into `<per-user app data>/updates/patchy-update-<version>/PatchyWindowsInstaller.exe` (never the
   shared temp folder) with
   `start_model_download`, the same downloader as the Object Selection model: every host
   resolves on a detached thread first, redirects must be https, the file is written through
   `QSaveFile`, and it is kept only when size and SHA-256 match the manifest. A file already
   there with the right hash is reused. A failure offers the browser download instead.
4. It then sets `pending_update_install_` and calls `close()`. `closeEvent` asks about
   unsaved documents as on any quit. Cancel there drops the pending update and Patchy stays
   open. Once every document is settled, `launch_update_installer` starts the installer
   detached with `PATCHY_UPDATE_INSTALL_DIR` and `PATCHY_UPDATE_WAIT_PID` in its
   environment. It first checks the file again (the save prompts may have taken minutes): size
   and SHA-256 as pinned, and the signer rule below, holding the file open against writes
   until the process exists. If a check fails or it cannot start, the close is refused so
   Patchy does not quit un-updated. Once the close has gone through,
   `download_and_install_update` calls `request_application_exit`
   (`QCoreApplication::exit(0)`) rather than relying on Qt's quit on the last window's close.
   That quit is a posted event, and any window shown before it runs deletes it; more
   important, it is inactive before `app.exec()`. Opening a file given on the command line
   pumps events before the main loop starts (its "Opening ..." progress), the startup update
   check answers in there, and an update accepted at that point closed the window and started
   the installer before `exec()`: Qt posted nothing, `exit()` had no loop to stop, and `exec()`
   then ran with no window while the installer waited two minutes and asked the user to close
   a Patchy nobody could see (October 2026, Patchy 1.12 started by double-clicking a JPG; a
   launch without a file was fine). `src/app/main.cpp` now skips `exec()` when the window is
   already closed at that point, and 250 ms after `lastWindowClosed` a still-hidden main window
   ends a running loop.
5. `packaging/windows/InstallPatchyLauncher.cs` turns those variables into
   `InstallPatchy.ps1 -Update -InstallDir <dir> -WaitForProcessId <pid>` and removes them
   from the child's environment. Without the variable the wizard runs as before. The
   self-extractor takes no arguments in either case.
6. `InstallPatchy.ps1 -Update` waits up to two minutes for the process to exit (Patchy can
   take about ten seconds to finish background work), shows a small "Updating Patchy to
   version N..." window, installs with the wizard's own install function (in a second copy
   of the script, `-UpdateWorker`, so the window keeps animating instead of going "Not
   Responding"), keeps the desktop shortcut only
   if there is one, and starts the installed `patchy.exe`. A problem (Patchy still running,
   a file in use, a failed copy) shows the same Retry or error box the wizard would.
   One exception to "still running": when the wait expires and the process named by
   `-WaitForProcessId` is alive, runs from inside the install folder and owns no visible
   top-level window (`Test-PatchyHeadlessUpdateRequester`), it is a Patchy 1.13 or older
   that accepted the update before its event loop started (step 4). Its documents were settled before its window closed and
   nobody can close it from the desktop, so `Stop-PatchyHeadlessUpdateRequester` ends that
   exact process id and the install goes on. A Patchy with a window (a cancelled save
   prompt) still gets the Retry box.
7. If the script fails or never runs, the launcher starts the installed `patchy.exe` again
   when it is still there, or says where to download the installer.
8. The updated copy deletes the downloaded installer 30 seconds after startup
   (`remove_update_download`, by exact file name, then the empty folder).

## Windows ARM64

The manifest's `windows-arm64` entry (a permanent key, never a rename of `windows`) has the
same keys as `windows` and names the ARM64 installer (`PatchyWindowsArm64Installer.exe`).
`current_update_platforms()` (`update_checker.cpp`) lists the entries a build reads, first
match wins (`inspect_update_manifest` with a list):

- An ARM64 build reads `windows-arm64` only. Without that entry the check reports
  `MissingPlatform`: an ARM64 copy is never offered the x64 installer.
- An x64 build on an ARM64 PC (Windows runs it emulated) reads `windows-arm64` first, so its
  next update installs the native build into the same folder, and `windows` while the
  manifest has no ARM64 entry. The PC's processor comes from `IsWow64Process2`'s native
  machine, never `PROCESSOR_ARCHITECTURE`, which says AMD64 inside an emulated process.
- Any other x64 build reads `windows`. Builds older than this rule read only `windows`.

A same-version ARM64 entry is no update: the move happens with the next version. The ARM64
installer refuses an x64 or x86 PC before touching anything
([packaging/windows/README.md](../packaging/windows/README.md)). Test:
`update_platform_prefers_windows_arm64_on_arm64_pcs`.

## Switching it off from the manifest

If a release's self-update turns out to be broken, edit `latest_version.json` on `main`; no
client has to change. Both switches leave the update prompt itself working, with the old
"download in the browser" button.

- **Only for versions older than X:** add `"installer_min_version": "X"` to the `windows`
  entry. Copies running a version older than X keep the browser download; X and newer still
  install by themselves. Example: 1.10's updater is broken and 1.11 fixes it, so the entry
  for 1.11 carries `"installer_min_version": "1.11"`. A value that is not a version string
  switches self-install off for everyone, so a typo fails safe.
- **For everyone:** remove the three installer keys (or just one of them).

The key name is permanent, like the other three.

## Rules

- Two independent checks guard the installer. Its size and SHA-256 come from the manifest
  on `main`, fetched over https. And a signed Patchy runs it only when Windows verifies its
  Authenticode signature and the signer is the publisher that signed the running `patchy.exe`
  (`file_signature`, `installer_signer_acceptable`): the download carries no Mark of the Web
  and shows no SmartScreen prompt, so without this a bad push to the manifest alone could run
  any file. An unsigned Patchy (development, the trial script) has no publisher and relies on
  the checksum. Never accept an installer whose size or hash was not in the manifest, never
  run one from `download_url`, and never drop the signer check to make an update work.
- Nothing from the manifest except the version reaches an AI assistant as text.
- Nothing is ever killed, with one exception: the Patchy that asked for the update, once the
  installer's wait has expired and it has no window left (flow step 6; only that process id,
  only from inside the install folder). AI apps are left alone (see below).
- The install is per-user and needs no elevation, so there is no UAC prompt in the flow.
- Announcing a release now means adding the three keys to the `windows` entry as well as
  bumping `version`; `publish-github-release.ps1` prints them. Still announce last
  (docs/release-process.md). A manifest with a new `version` and stale installer keys would
  fail the checksum and fall back to the browser download.

## AppImage

An AppImage is one file ([appimage.md](appimage.md)), so its update is a download and a
rename; there is no installer.

- **Keys** (permanent, in the `linux` entry; the Flatpak ignores them):
  `appimage_install_url`, `appimage_install_size`, `appimage_install_sha256` (all or none,
  the same rules as the installer keys), and `appimage_min_version`, the off switch with
  `installer_min_version`'s semantics. `appimage_url` stays the browser download.
  `read_appimage_install_fields` (`update_checker.cpp`) also requires the URL to start with
  `kAppImageReleaseDownloadPrefix`, this repository's GitHub release downloads: no publisher
  signature backs an AppImage, so a manifest edit alone must not be able to point it at
  another host. `PATCHY_UPDATE_MANIFEST_URL` lifts that, as it lifts the https rule.
- **When:** `can_install_update` is true for an AppImage (`appimage_file_path()`) when the
  keys are present and `appimage_can_be_replaced` (the file exists and its folder is
  writable). The prompt says Patchy will replace this AppImage; otherwise it keeps the
  download prompt that names the file.
- **Download:** the same `download_and_install_update` and `start_model_download`, into the
  AppImage's own folder as the hidden `.<file name>.update-<version>`
  (`appimage_update_download_name`), so the final rename stays in one folder. An update
  cancelled at the save prompt keeps it for reuse; the copy that runs that version deletes it
  at startup (`remove_appimage_update_download`, exact name).
- **On close:** the pending `UpdateInstallerLaunch` carries `appimage_target`, and
  `launch_update_installer` runs `replace_appimage`: size and SHA-256 again, mode 0755, one
  `std::filesystem::rename` over the running file. The running copy's mount keeps the old
  data until it exits, and nothing ever sees a half-written AppImage. A failure
  (`InstallerMissing`, `ChecksumMismatch`, `ReplaceFailed`) refuses the close and offers the
  browser download, as on Windows. Then a detached `/bin/sh` waiter starts the new file once
  this process has exited (at most two minutes later), so the new copy cannot forward its
  launch to the old one; it runs without the old launch's `APPIMAGE`/`APPDIR`. A waiter that
  cannot start does not fail the update: the next launch runs the new version.
- **Connectors** started from the old file keep running from their own mounts; connector
  freshness watches the `.AppImage` file, so they report `updateNotice`.
- **Announce** with the four `linux` keys `publish-github-release.ps1` prints
  (`appimage_install_url` is the versioned `releases/download/v<version>/` URL).
- **Tests:** `update_manifest_parser_handles_supported_cases` (the keys, the host rule, the
  off switch), `update_appimage_replacement_checks_and_renames` (every platform), and
  `ui_update_one_click_replaces_appimage_on_close` (Linux: prompt, download, save prompt,
  settled close, real replacement, read-only fallback). Not automated: the relaunch waiter
  and a real update between two released AppImages; try one with `PATCHY_UPDATE_MANIFEST_URL`
  pointing at a loopback manifest before the first release that ships it.

## Connectors that are running during an update

An AI app keeps one `patchy-mcp.exe` per session, often for days, and each is a whole
Patchy holding about twenty files open (the executable, the Qt DLLs, plug-ins, ONNX
Runtime). Stopping them would destroy an agent's unsaved isolated workspace, so the update
works around them:

- **The installer sets locked files aside.** Before removing anything, every install mode
  runs `Confirm-PatchyFilesFreeForInstall`: each installed file another program holds open
  is renamed to `<name>.old-<yyyyMMddHHmmss>` (Windows lets a loaded image be renamed, not
  replaced), and the new copy takes its name. The old process keeps running the old code.
  Only a file that cannot even be renamed stops the install with a Retry box naming it.
  This happens after the payload is unpacked and checked, immediately before the old files
  are removed, and a replacement that then fails gives each set-aside file its name back
  (`Restore-PatchySetAsideFiles`). The check also covers the new version's file list, so an
  install with a missing or damaged manifest is handled the same way.
  The suffix goes after the extension so Qt never loads a set-aside plug-in DLL.
- **Set-aside files stay in `PatchyInstallManifest.txt`**, so the next install and the
  uninstaller delete them once nothing uses them; one still in use is carried forward.
- **Patchy tells the user after the restart.** Update mode lists the apps whose connector
  ran from the install (the connector's parent process, by product name) and starts Patchy
  with them in `PATCHY_UPDATE_CONNECTOR_CLIENTS` (`|` separated). It starts Patchy before
  closing the progress window (`Start-PatchyInForeground`), or Patchy opens under other windows.
  `MainWindow::show_connector_restart_notice` shows them once: restart these apps to finish
  updating. No user-facing text says "reconnect": most AI apps have no such control.
- **The old connector tells the assistant.** `ui/connector_freshness.*` remembers the
  executable's size and modification time at startup; once a different file sits at that
  path, every tool result from an owned connector or the attached proxy carries
  `updateNotice`. New sessions start the new connector by themselves.
- **A connector also checks for updates**, for people who never open the app: an owned
  connector runs `request_update_check` once (same switches as the app, the user's
  `updates/checkOnStartup` setting read from their real settings file, never for a suite
  with its own `PATCHY_SETTINGS_DIR`), and `get_info` reports a newer version as
  `updateAvailable`. The attached proxy does not: its Patchy shows the prompt itself.

What an old session can still get wrong until its app restarts: help text and bundled
scripts are read from disk per request, so they are the new version's, and a plug-in it
had not loaded yet would be the new build.

## Tests

- `tests/ui/app_shell_tests_updates_preferences.cpp`, filter `update`: the manifest keys,
  the install-location match, the prompt for an installed copy, and the whole path with a
  `file://` stand-in installer and the launch captured through `MainWindowTestAccess` (wrong
  checksum, Cancel at the save prompt, a launch that fails, a settled close, which must
  request the application exit exactly once through `set_application_exit_hook`).
  `ui_update_connector_restart_notice_names_the_apps` and
  `connector_update_notice_reports_a_replaced_executable` cover the connector notices.
  `ui_update_check_interval_gates_the_startup_check`,
  `ui_update_startup_check_honors_stored_interval` (a loopback manifest server),
  `ui_update_preference_offers_daily_interval` and
  `ui_update_prompt_skip_version_silences_until_a_newer_one` cover the interval and the
  skipped version.
- `scripts\release\verify-windows-package.ps1` runs `InstallPatchy.ps1 -UpdateCheckRoot`:
  the process wait, the locked-file check, setting a file in use aside (renamed,
  recorded in the manifest, removed by the next install) on stand-in files in a scratch
  folder, and the headless requester (a hidden copy of `powershell.exe` named `patchy.exe`
  inside the scratch install is ended; the same outside it is left alone).
- Not automated: the real install and relaunch. It writes the uninstall entry and
  shortcuts, so it cannot run on a machine whose installed Patchy must stay untouched.
  Before a release that changes any of this, install an older package into a scratch
  folder, point `PATCHY_UPDATE_MANIFEST_URL` at a manifest naming the new installer, and
  click through it with an unsaved document open. With that variable set, `installer_url`
  may be http, so a loopback server can serve both files.
  `scripts\dev\auto-update-trial.ps1` builds such a trial from `build\release` without
  touching `build\package`.
