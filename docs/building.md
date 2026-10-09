# Building Patchy

[Back to Patchy](../README.md)

## Build from source

Build the dependency-light core and tests without the Qt app:

```sh
cmake --preset dev -DPATCHY_BUILD_APP=OFF
cmake --build --preset dev
ctest --preset dev
```

Build the Qt desktop app:

```sh
cmake --preset qt-local
cmake --build --preset qt-local
```

The local Qt app preset writes `patchy.exe` under `build/app`.

Run the standard local test script:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run-tests.ps1
```

### macOS and Linux

Install Qt 6.8.3 into `.deps/Qt` (for example `pip install aqtinstall && aqt install-qt
mac desktop 6.8.3 -m qtimageformats qtpdf -O .deps/Qt`, or `linux desktop 6.8.3
linux_gcc_64` on Linux), then build the matching preset. The `qtpdf` module is optional:
without it Patchy still exports PDF, it just cannot open one.

```sh
cmake --preset mac-release      # or linux-release
cmake --build --preset mac-release
```

### Qt version status (researched October 9, 2026)

The desktop presets pin Qt 6.8.3, the last open-source 6.8 release (6.8.4 and later
are commercial-only). The wasm presets use 6.10.3 and the Flatpak builds against the
KDE 6.11 runtime, so the code already compiles on 6.10 and 6.11. Facts for the next
desktop bump (decision, October 9, 2026: stay on 6.8.3 and go straight to 6.12 once the
separate Qt PDF 6.140.x package is downloadable for Windows, mac and Linux; skip 6.11):

- Qt 6.11 (patches end around March 2027) needs macOS 13+; Qt 6.12 LTS (open-source
  patches only until 6.13, about April 2027) needs macOS 14.4+ and Xcode 16, and is the
  last Qt that supports Windows 10. The README, `CMAKE_OSX_DEPLOYMENT_TARGET`, and
  `Info.plist.in` advertise macOS 12, so a bump must raise them together.
- Qt 6.12 no longer ships `qtpdf`: Qt PDF and WebEngine became a separately versioned
  product (first release 6.140.0, Chromium 140). Without it the desktop build falls back to
  `pdf_import_stub.cpp` and loses PDF import, so confirm `qtpdf@6.140.x` installs for the
  target kit before bumping to 6.12.
- Released aqtinstall (3.3.0) cannot install any Windows desktop kit from 6.11 on (new
  per-toolchain repository layout; fix only in aqtinstall master, PR 1000, and PR 1048 for
  6.12's extensions). macOS and Linux kits are unaffected.
- A new Qt bundles newer FreeType/HarfBuzz, so expect offscreen text pins (text re-edit
  rasters, area-text render digests) to need re-verification, not blind re-pinning.

macOS produces `build/mac-release/Patchy.app`; Linux produces
`build/linux-release/patchy`. `packaging/macos/make-dmg.sh` and
`packaging/linux/make-flatpak.sh` create the distributable artifacts. Both test suites
run offscreen on all three platforms (`QT_QPA_PLATFORM=offscreen`).

## MSVC Release codegen

CMakeLists.txt owns the MSVC Release codegen flags (`/Zi /GL` on compiles, `/DEBUG:FULL /INCREMENTAL:NO /LTCG` on links), so every configure emits `patchy.pdb` (for symbolizing WER dumps from `%LOCALAPPDATA%\CrashDumps`) and link-time optimized binaries. Never hand-edit `build\release\CMakeCache.txt`. To symbolize a dump from an older build, rebuild that commit in a temporary worktree; full links reproduce the binary layout.

## Windows Release Package

Create local Windows release artifacts:

```bat
scripts\release\build-release.bat
```

The script configures and builds the `release` preset, signs `patchy.exe`, `patchy-mcp.exe`, the two legacy plug-in hosts, the installer helper executables, and the installer (signing is required; `PATCHY_ALLOW_UNSIGNED=1` allows a deliberately unsigned local build), deploys the minimum Qt runtime needed by the current app, copies third-party notices, and creates:

```text
build\package\PatchyWindowsNoInstaller.zip
build\package\PatchyWindowsInstaller.exe
```

The zip contains a top-level `Patchy` folder so it can be dragged anywhere and does not include installer-only helpers. The installer is a local per-user installer that installs to `%LOCALAPPDATA%\Programs\Patchy`, creates a Start Menu shortcut, offers a desktop shortcut, and registers an uninstall entry.  `latest_version.json` is the update metadata file.
