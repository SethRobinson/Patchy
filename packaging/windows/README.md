# Windows Packaging

Planned release shape:

- Build with MSVC 2022.
- Bundle Qt runtime dependencies using `windeployqt`.
- Sign binaries and installer.
- Produce a simple local per-user installer, with WiX Toolset or a commercial installer system still available later if project requirements grow.
- Generate optional MSIX after core installer behavior stabilizes.

## Local package artifacts

Run the release script from a Developer Command Prompt or regular `cmd.exe` in the repo root:

```bat
scripts\release\build-release.bat
```

The script configures and builds the `release` preset, deploys the Qt runtime, stages runtime files under `build\package\staging\Patchy`, and creates:

```text
build\package\PatchyWindowsNoInstaller.zip
build\package\PatchyWindowsInstaller.exe
```

`PatchyWindowsNoInstaller.zip` contains a top-level `Patchy` folder. Users can drag that folder to the desktop or another writable location and run `patchy.exe` from there.

`PatchyWindowsInstaller.exe` is built with Windows IExpress from the zip payload plus installer-only helper executables. It opens a small per-user setup wizard, installs to `%LOCALAPPDATA%\Programs\Patchy` by default or to a folder the user chooses, creates a Start Menu shortcut, offers a default-checked desktop shortcut, registers a Windows uninstall entry under the current user, and offers to launch Patchy when setup finishes. The wizard and update mode start Patchy through `Start-PatchyInForeground` while their own window is still up: Windows lets a new process take the foreground only while its starter owns it, so a Patchy started after the window closed opens under other windows.

The wizard heading shows `Install Patchy <version>` using the version passed by the packaged launcher. The legal notice, installation status, progress bar, and bottom buttons occupy separate rows with space between them.

The package is intentionally limited to the files needed by end users:

- `patchy.exe`
- `patchy-8bf-host32.exe` and `patchy-8bf-host64.exe`, the out-of-process hosts for legacy Photoshop .8bf filter plug-ins (docs/plugins.md), signed like the app
- `plugins\README.txt` (from `packaging/plugins/README.txt`): the folder users drop .8bf plug-ins into, shipped so its location is obvious
- `Patchy.ico` and `PatchyInstallManifest.txt`
- Qt DLLs for Core, GUI, Widgets, PrintSupport, Network, SVG, and the Qt ImageFormats plugins
- the Windows and offscreen platform plugins (offscreen is what `--headless` loads; the script smoke-tests the staged tree headless before zipping), current Windows style plugin, SVG icon engine, TLS backend, and JPEG, SVG, TIFF, and WebP image plugins
- app-local Microsoft Visual C++ runtime DLLs copied from the local Visual Studio redist CRT directory, plus `vcomp140.dll` from the OpenMP directory beside it (the G'MIC engine)
- app and Qt base translations for every shipped language (German, Spanish, French, Italian, Japanese, Korean, Polish, Brazilian Portuguese, Russian, Simplified and Traditional Chinese) under `translations`
- bundled compatibility fonts under `fonts`
- `README.md`, `LICENSE`, `NOTICE-THIRD-PARTY.md`, Qt module SPDX notices under `licenses\qt`, and the G'MIC and pocketfft license texts under `licenses\gmic`

The zip does not include build files, tests, test fixtures, Qt translations for languages Patchy does not ship, Qt generic input plugins, installer-only helpers such as `InstallPatchy.exe` and `UninstallPatchy.exe`, the Visual C++ Redistributable installer, or developer packaging notes.

The installer copies a signed `UninstallPatchy.exe` into the installed app folder and records it in `PatchyInstallManifest.txt`. The uninstaller uses that manifest to remove only files installed by the package. If a user saves documents into the install directory, those files are left in place and the install directory remains until the user removes them. It also deletes Object Selection's downloaded High Quality model, the two files `efficientvit_sam_xl0_encoder_fp16.onnx` and `efficientvit_sam_xl0_decoder_multi_fp16.onnx` by those exact names in `%APPDATA%\RTsoft\Patchy\ai-models`, and that folder only when nothing else is left in it; the rest of the user's Patchy data stays (docs/ai-selection.md).

## Windows ARM64

`build-release.bat` with no argument also builds `build\package\PatchyWindowsArm64NoInstaller.zip` and `build\package\PatchyWindowsArm64Installer.exe` (`build-release.bat arm64` builds only those; docs/release-process.md). The contents match the x64 package, with these differences: `patchy.exe`, `patchy-mcp.exe`, Qt, ONNX Runtime and the MSVC runtime are ARM64; the two plug-in hosts stay x86 and x64 and link the C++ runtime statically, because plug-ins are x86/x64 code; there is no Qt PDF module or notice. Both installers install to the same folder under the same uninstall entry, so either replaces the other.

The installer payload carries `PatchyArchitecture.txt` (`x64` or `arm64`; a payload without it counts as x64). `InstallPatchy.ps1` reads it before any wizard, update or quiet install and refuses an ARM64 payload on a PC whose processor is x64 or x86, with `WrongArchitectureMessage` (every shipped language) pointing at the releases page. The processor comes from `IsWow64Process2`, so the answer is right inside an emulated process too. An unknown processor is never refused. The launcher stays an x64 .NET program and runs emulated on an ARM64 PC.

## Install location

The wizard's install folder box is editable and has a Browse button (issue 85). It starts at the folder of the registered install (`InstallLocation` under `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Patchy`, when `patchy.exe` is still there), else `%LOCALAPPDATA%\Programs\Patchy`. `InstallPatchy.ps1 -InstallDir <folder>` sets the exact folder, for the wizard and for quiet installs.

- Browse adds a `Patchy` folder inside the chosen one unless it is already named `Patchy` or already holds a Patchy install (`D:\Apps` becomes `D:\Apps\Patchy`). Typed text is used as-is.
- `Test-PatchyInstallLocation` accepts only a new folder, an empty one, or an earlier Patchy install (`patchy.exe` or `PatchyInstallManifest.txt` present). It refuses relative paths, drive and share roots, the Windows folder, files, and folders holding other files. This is a safety rule, not a preference: installing and uninstalling remove every empty folder under the install root, and the uninstaller then removes the root itself.
- Setup stays per-user: no elevation and nothing in HKLM. Before copying it creates the folder and writes a scratch file. A folder the user cannot write to, such as Program Files, is refused with a message, and folders the probe created are removed again.
- Installing to a different folder than the registered one moves the install: once the new copy, shortcuts and registration succeed, the old folder's manifest files are removed (`Remove-PatchyInstalledFiles`), and then the folder if that leaves it empty. Nested old and new folders are left alone.
- The refusal messages are in `$PatchyInstallerText` in every shipped language. `UninstallPatchy.exe` needs no location of its own: it treats its own folder as the install root.

## Update mode

A running Patchy can start the installer to update itself ([docs/auto-update.md](../../docs/auto-update.md)). It sets `PATCHY_UPDATE_INSTALL_DIR` and `PATCHY_UPDATE_WAIT_PID` in the installer's environment; `InstallPatchyLauncher.cs` passes them on as `InstallPatchy.ps1 -Update -InstallDir <folder> -WaitForProcessId <pid>`. No wizard is shown. The script waits for that process to exit, shows an "Updating Patchy to version N..." window, installs into the given folder, creates the desktop shortcut only if one exists, and starts Patchy. Without the variable the installer behaves as always.

Before any install removes a file, `Confirm-PatchyFilesFreeForInstall` renames every installed file another program holds open (a running `patchy-mcp.exe` connector and the DLLs it loaded, typically) to `<name>.old-<yyyyMMddHHmmss>`, so the new copy can take its place while that program keeps running the previous version. The set-aside files are listed in `PatchyInstallManifest.txt`, and the next install or the uninstaller deletes them once they are free. Only a file that cannot be renamed stops setup: the wizard and update mode name it and offer Retry, a quiet install fails without changing anything. Setup never stops another process. After an update, Patchy names the AI apps that need a restart (docs/auto-update.md).

## Open with

The installer lists Patchy under Explorer's "Open with" for the image types it opens (the `$PatchyOpenWithExtensions` list in `InstallPatchy.ps1`; PDF is left out). It writes, all under `HKCU\Software\Classes`:

- `Applications\patchy.exe` (`FriendlyAppName` = `Patchy Image Editor`, `shell\open\command`, `SupportedTypes`), which names the app;
- the `Patchy.Image` ProgID (a persisted identifier) with the same open command;
- a `Patchy.Image` value in each extension's `OpenWithProgids` list. This is what puts Patchy in the "Open with" submenu: the `Applications` key alone only reaches "Choose another app" (measured with `SHAssocEnumHandlers` on Windows 11, October 2026).

An `SHChangeNotify` follows so Explorer sees it at once.

This registration must stay passive: no extension's default value, `UserChoice`, or Default Apps entry is written, and nothing goes to HKLM. A type that already has a default program keeps it. A type no installed program handles (on a machine without Photoshop, `.psd`; `.rttex` almost everywhere) has Patchy as its only candidate, so a double-click offers it. A failure to register only warns.

The uninstaller removes all of it when the registered command points into the install being removed, taking only its own value out of each `OpenWithProgids` list and deleting an extension key only when that leaves it empty. On the Windows offload host an install followed by an uninstall left the `HKCU\Software\Classes` extension keys identical to before. The portable zip registers nothing.

Every Windows app build embeds `FileDescription = Patchy Image Editor` and
`ProductName = Patchy` in its version resource, so a portable copy also has a display
name when chosen through Explorer. These are fixed product branding, independent
of the in-app language. `src/app/patchy.rc.in` is configured from the CMake project
version; preserve the textual version (for example `1.07`) while normalizing the
four numeric resource components to decimal (`1,7,0,0`).

## Package verification

After signing, `build-release.bat` runs `scripts\release\verify-windows-package.ps1` on the finished installer and zip. Nothing is installed: no install folder, shortcut, or registry key is touched, apart from a `HKCU\Software\PatchyVerify-<guid>` scratch key the "Open with" check writes and deletes. It:

- unpacks the installer with IExpress's extract-only switches (`/Q /C /T:<folder>`) and checks that its payload is complete, carries the same zip as the portable download, and names the expected version;
- runs the unpacked `InstallPatchy.ps1` with the launcher's arguments plus `-SmokeTest`, which builds the whole wizard, shows it invisibly, and closes it. A wizard that cannot open fails here (issue 55);
- runs it again with `-OpenWithCheckRoot <scratch key>`, which writes the "Open with" registration under that key instead of the real Classes tree, and checks the name, the command, the type list, and that an extension gets the ProgID in `OpenWithProgids` with no default value;
- runs it with `-LocationCheckRoot <scratch folder>`, which checks the install folder rules and the Browse rule on cases built inside that folder;
- runs it with `-UpdateCheckRoot <scratch folder>`, which checks update mode's wait for the closing Patchy and the locked-file check on stand-in files inside that folder;
- runs it with `-ArchitectureCheck`, which tests the processor rule above and the wrong-processor message in every language, and checks that the payload's `PatchyArchitecture.txt` names the package's architecture;
- checks the machine type of every shipped `.exe` and `.dll`: the package's architecture (`-Arch x64` or `arm64`), except `patchy-8bf-host32.exe` (x86) and `patchy-8bf-host64.exe` (x64). An ARM64X image (an x64 header with hybrid ARM64EC code; the ARM64 runtime's `vcruntime140_1.dll` is one) passes in either package;
- unpacks the zip and compares it with `PatchyInstallManifest.txt`;
- checks the unpacked executable's display name, product name, original filename,
  and file/product versions against the package version;
- reads the imports of every executable and DLL with `dumpbin` and requires each one to be in the package or part of Windows. Qt and Visual C++ runtime DLLs must be in the package even when the build machine has copies in System32;
- runs `packaging\package-selftest.js` on the unpacked `patchy.exe` with only Windows on `PATH`: every image format plugin writes and reads a file, the bundled font is listed, the scripts, translations, AI kit and TLS plugin are present, and a 32-bit and a 64-bit legacy plug-in run through their hosts;
- runs the unpacked `patchy-mcp.exe --check`.

A package that fails is moved to `build\package\rejected` and the build fails, so the upload scripts cannot publish it. The script also runs by itself, with `-Installer` and `-Zip` for files downloaded from a release. The wizard step needs an interactive desktop session (`-NoDesktop` skips it). On a PC that cannot run the package (ARM64 on x64) the self-test and connector steps print `SKIP`; `-RequireRun` makes that a failure, as `verify-remote.ps1 -Target windows-arm64` does on the ARM64 host.

`InstallPatchy.ps1 -CheckLogo` is the earlier, narrower check the batch file runs before compiling the launcher. Neither check clicks through the wizard, so run the built installer by hand after changing the installer script.

When Seth's local signing setup is available, the script signs `build\release\patchy.exe` before staging it, signs `InstallPatchy.exe` and `UninstallPatchy.exe` before IExpress packs them, and signs `build\package\PatchyWindowsInstaller.exe` after IExpress creates it:

- `RT_PROJECTS` must point at the RT projects root.
- The script calls `%RT_PROJECTS%\Signing\sign.bat "%EXE%" "Patchy" "rtsoft.com"`.
- Signature verification uses `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe`.

If `RT_PROJECTS` or the signing script is missing, the build fails. Set `PATCHY_ALLOW_UNSIGNED=1` for a deliberately unsigned local build (see `docs/release-process.md`).

Publishing automation and a CI signing pipeline are not implemented yet; the current local handoff artifacts are the zip package and installer executable, with `latest_version.json` providing update metadata for published builds.
