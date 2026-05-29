# Patchy

Patchy is a native C++20 image editor foundation focused on PSD/PSB-oriented editing, a Qt desktop shell, and a plug-in architecture that can grow without binding the core to a single UI toolkit.

Created by Seth A. Robinson. Home page: [SethRobinson/Patchy](https://github.com/SethRobinson/Patchy).

The repository currently includes document and layer primitives, tiled compositing, a PSD/PSB reader/writer foundation, built-in filters, legacy Photoshop plug-in probing, Qt 6 Widgets UI, Windows packaging notes, and automated tests.

## Requirements

- CMake 3.26 or newer
- Ninja or another CMake-supported generator
- A C++20 compiler
- Qt 6 Widgets, PrintSupport, SVG, and ImageFormats for the desktop app

The `qt-local` and Windows release paths expect Qt at `.deps/Qt/6.8.3/msvc2022_64` by default. Set `CMAKE_PREFIX_PATH` for CMake builds or `PATCHY_QT_PREFIX` for `build-release.bat` when Qt is installed elsewhere.

## Build

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

## Windows Release Package

Create local Windows release artifacts:

```bat
build-release.bat
```

The script configures and builds the `release` preset, signs `build\release\patchy.exe` and the installer when the local signing environment is available, deploys the minimum Qt runtime needed by the current app, copies third-party notices, and creates:

```text
build\package\PatchyWindows.zip
build\package\PatchyInstallerWindows.exe
```

The zip contains a top-level `Patchy` folder so it can be dragged anywhere. The installer is a local per-user installer that installs to `%LOCALAPPDATA%\Programs\Patchy`, creates a Start Menu shortcut, and registers an uninstall entry. Publishing automation is not implemented yet; `latest_version.json` is the update metadata file.

## Current Status

Patchy is not Photoshop-compatible across the full PSD surface yet. It supports a native Qt editing shell, pixel tools, layers, flat PSD export, and layered PSD round trips for common 8-bit RGB/RGBA pixel-layer documents.

Patchy is released under the MIT License. Third-party runtime notices are tracked in `NOTICE-THIRD-PARTY.md`.
