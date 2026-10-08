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

`PatchyWindowsInstaller.exe` is built with Windows IExpress from the zip payload plus installer-only helper executables. It opens a small per-user setup wizard, installs to `%LOCALAPPDATA%\Programs\Patchy`, creates a Start Menu shortcut, offers a default-checked desktop shortcut, registers a Windows uninstall entry under the current user, and offers to launch Patchy when setup finishes.

The wizard heading shows `Install Patchy <version>` using the version passed by the packaged launcher. The legal notice, installation status, progress bar, and bottom buttons occupy separate rows with space between them.

The package is intentionally limited to the files needed by end users:

- `patchy.exe`
- `patchy-8bf-host32.exe` and `patchy-8bf-host64.exe`, the out-of-process hosts for legacy Photoshop .8bf filter plug-ins (docs/plugins.md), signed like the app
- `plugins\README.txt` (from `packaging/plugins/README.txt`): the folder users drop .8bf plug-ins into, shipped so its location is obvious
- `Patchy.ico` and `PatchyInstallManifest.txt`
- Qt DLLs for Core, GUI, Widgets, PrintSupport, Network, SVG, and the Qt ImageFormats plugins
- the Windows and offscreen platform plugins (offscreen is what `--headless` loads; the script smoke-tests the staged tree headless before zipping), current Windows style plugin, SVG icon engine, TLS backend, and JPEG, SVG, TIFF, and WebP image plugins
- app-local Microsoft Visual C++ runtime DLLs copied from the local Visual Studio redist CRT directory
- app and Qt base translations for every shipped language (German, Spanish, French, Italian, Japanese, Korean, Polish, Brazilian Portuguese, Russian, Simplified and Traditional Chinese) under `translations`
- bundled compatibility fonts under `fonts`
- `README.md`, `LICENSE`, `NOTICE-THIRD-PARTY.md`, and Qt module SPDX notices under `licenses\qt`

The zip does not include build files, tests, test fixtures, Qt translations for languages Patchy does not ship, Qt generic input plugins, installer-only helpers such as `InstallPatchy.exe` and `UninstallPatchy.exe`, the Visual C++ Redistributable installer, or developer packaging notes.

The installer copies a signed `UninstallPatchy.exe` into the installed app folder and records it in `PatchyInstallManifest.txt`. The uninstaller uses that manifest to remove only files installed by the package. If a user saves documents into the install directory, those files are left in place and the install directory remains until the user removes them.

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
- unpacks the zip and compares it with `PatchyInstallManifest.txt`;
- checks the unpacked executable's display name, product name, original filename,
  and file/product versions against the package version;
- reads the imports of every executable and DLL with `dumpbin` and requires each one to be in the package or part of Windows. Qt and Visual C++ runtime DLLs must be in the package even when the build machine has copies in System32;
- runs `packaging\package-selftest.js` on the unpacked `patchy.exe` with only Windows on `PATH`: every image format plugin writes and reads a file, the bundled font is listed, the scripts, translations, AI kit and TLS plugin are present, and a 32-bit and a 64-bit legacy plug-in run through their hosts;
- runs the unpacked `patchy-mcp.exe --check`.

A package that fails is moved to `build\package\rejected` and the build fails, so the upload scripts cannot publish it. The script also runs by itself, with `-Installer` and `-Zip` for files downloaded from a release. The wizard step needs an interactive desktop session.

`InstallPatchy.ps1 -CheckLogo` is the earlier, narrower check the batch file runs before compiling the launcher. Neither check clicks through the wizard, so run the built installer by hand after changing the installer script.

When Seth's local signing setup is available, the script signs `build\release\patchy.exe` before staging it, signs `InstallPatchy.exe` and `UninstallPatchy.exe` before IExpress packs them, and signs `build\package\PatchyWindowsInstaller.exe` after IExpress creates it:

- `RT_PROJECTS` must point at the RT projects root.
- The script calls `%RT_PROJECTS%\Signing\sign.bat "%EXE%" "Patchy" "rtsoft.com"`.
- Signature verification uses `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe`.

If `RT_PROJECTS` or the signing script is missing, the build fails. Set `PATCHY_ALLOW_UNSIGNED=1` for a deliberately unsigned local build (see `docs/release-process.md`).

Publishing automation and a CI signing pipeline are not implemented yet; the current local handoff artifacts are the zip package and installer executable, with `latest_version.json` providing update metadata for published builds.
