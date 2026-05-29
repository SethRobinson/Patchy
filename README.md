# Patchy

Open source photo editing. No subscriptions, no gatekeeping.

## Download

Patchy is available for Windows 10/11, 64-bit. Releases are code signed by Seth A. Robinson.

| Package           | Best for                     | Download                                                                                  |
| ----------------- | ---------------------------- | ----------------------------------------------------------------------------------------- |
| Windows installer | Standard installation        | [PatchyWindowsInstaller.exe](https://rtsoft.com/files/PatchyWindowsInstaller.exe) (14 MB) |
| Portable ZIP      | Running without an installer | [PatchyWindows.zip](https://rtsoft.com/files/PatchyWindows.zip) (13 MB)                   |

## Features

- Open and save layered PSD files with groups, masks, text objects, blend modes, layer styles and more
- Common raster editing tools (brush, eraser, selection, transform, etc.)
- Supports palettized saving of low-color bitmap savings (2/4/8 bit)
- Cross-platform architecture (currently Windows-focused, but designed for extensibility)
- Rich text allowing color, font, size, and style changes within a single text layer
- Supports legacy .8bf plugins
- Reads/writes PSD, TIFF, PNG, JPEG, BMP, webp
- Built with C++ and Qt for performance and a native desktop experience
- Privacy: YES! Absolutely no telemetry, no tracking, no data collection. (If update checks are enabled, it contacts GitHub only to check for a newer version) 
- App settings are stored locally in a JSON file under the user's AppData folder on Windows
- Localized in English and Japanese (can change language in File->Preferences)

## Building it yourself

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
build\package\PatchyWindowsInstaller.exe
```

The zip contains a top-level `Patchy` folder so it can be dragged anywhere. The installer is a local per-user installer that installs to `%LOCALAPPDATA%\Programs\Patchy`, creates a Start Menu shortcut, and registers an uninstall entry. Publishing automation is not implemented yet; `latest_version.json` is the update metadata file.

## Current Status

Patchy is not Photoshop-compatible across the full PSD surface yet, but a round-trip psd mostly works with basic layers, groups, masks, and blend modes. 

- Missing any kind of vector editing
- Missing batch/automation/scripting
- Missing a lot of other Photoshop features
- Not tested much yet, expect bugs

## License

Patchy is released under the MIT License. Third-party runtime notices are tracked in `NOTICE-THIRD-PARTY.md`.

## AI Disclosure

This project was developed with significant assistance from AI tools.  I mean, you can still blame me (Seth) for bugs, but I just wanted to mention it.

## Credits

Created by Seth A. Robinson - [Homepage](https://www.rtsoft.com/) | [Blog](https://www.codedojo.com/) | [Twitter](https://twitter.com/rtsoft) | [Bluesky](https://bsky.app/profile/rtsoft.com) | [Mastodon](https://mastodon.gamedev.place/@rtsoft)