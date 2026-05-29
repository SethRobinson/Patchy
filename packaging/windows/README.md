# Windows Packaging

Planned release shape:

- Build with MSVC 2022.
- Bundle Qt runtime dependencies using `windeployqt`.
- Sign binaries and installer.
- Produce a simple local per-user installer, with WiX Toolset or a commercial installer system still available later if project requirements grow.
- Generate optional MSIX after core installer behavior stabilizes.

## Local package artifacts

Run the root script from a Developer Command Prompt or regular `cmd.exe`:

```bat
build-release.bat
```

The script configures and builds the `release` preset, deploys the Qt runtime, stages runtime files under `build\package\staging\Patchy`, and creates:

```text
build\package\PatchyWindows.zip
build\package\PatchyWindowsInstaller.exe
```

`PatchyWindows.zip` contains a top-level `Patchy` folder. Users can drag that folder to the desktop or another writable location and run `patchy.exe` from there.

`PatchyWindowsInstaller.exe` is built with Windows IExpress from the same zip payload. It opens a small per-user setup wizard, installs to `%LOCALAPPDATA%\Programs\Patchy`, creates a Start Menu shortcut, registers a Windows uninstall entry under the current user, and offers to launch Patchy when setup finishes.

The package is intentionally limited to the files needed by end users:

- `patchy.exe`
- `Patchy.ico`, `UninstallPatchy.exe`, and `PatchyInstallManifest.txt`
- Qt DLLs for Core, GUI, Widgets, PrintSupport, Network, SVG, and the Qt ImageFormats plugins
- the Windows platform plugin, current Windows style plugin, SVG icon engine, TLS backend, and JPEG, SVG, TIFF, and WebP image plugins
- app-local Microsoft Visual C++ runtime DLLs copied from the local Visual Studio redist CRT directory
- Japanese app and Qt base translations under `translations`
- `README.md`, `LICENSE`, `NOTICE-THIRD-PARTY.md`, and Qt module SPDX notices under `licenses\qt`

The zip does not include build files, tests, test fixtures, non-Japanese Qt translations, Qt generic input plugins, the Visual C++ Redistributable installer, or developer packaging notes.

The uninstaller uses `PatchyInstallManifest.txt` to remove only files installed by the package. If a user saves documents into the install directory, those files are left in place and the install directory remains until the user removes them.

When Seth's local signing setup is available, the script signs `build\release\patchy.exe` before staging it and signs `build\package\PatchyWindowsInstaller.exe` after IExpress creates it:

- `RT_PROJECTS` must point at the RT projects root.
- The script calls `%RT_PROJECTS%\Signing\sign.bat "%EXE%" "Patchy" "rtsoft.com"`.
- Signature verification uses `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe`.

If `RT_PROJECTS` or the signing script is missing, signing is skipped and the unsigned package artifacts are still created.

Publishing automation and a CI signing pipeline are not implemented yet; the current local handoff artifacts are the zip package and installer executable, with `latest_version.json` providing update metadata for published builds.
