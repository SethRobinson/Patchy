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

The zip contains a top-level `Patchy` folder so it can be dragged anywhere. The installer is a local per-user installer that installs to `%LOCALAPPDATA%\Programs\Patchy`, creates a Start Menu shortcut, offers a desktop shortcut, and registers an uninstall entry.  `latest_version.json` is the update metadata file.

## Current Status

Patchy is not Photoshop-compatible across the full PSD surface yet, but a round-trip from/to Photoshop mostly works with RGB/RGBA 8-bit documents that use basic pixel layers, text objects, groups, masks, blend modes, layer styles, and the currently supported adjustment layers.

Important Photoshop features that are not supported yet, or are only partially supported:

- Vector/path workflows, including pen paths, editable shape layers, vector masks, and editable stroke/fill appearance
- Smart Objects, linked assets, Smart Filters, and broad non-destructive filter stacks
- Full Photoshop adjustment-layer compatibility beyond Patchy's current adjustment support
- CMYK/Lab editing and export, spot channels, extra alpha-channel workflows, 16/32-bit editing, HDR/EXR, and full color-management parity (patchy will convert CMYK/Lab to RGB on open, but doesn't support editing or saving in those color modes)
- Layer comps, timeline/video/animation workflows, Camera Raw, Liquify/warp, content-aware tools, and generative tools
- Actions, batch processing, scripting, UXP/JSX panels, and other automation workflows
- High-fidelity PSD/PSB edge cases, including layered PSB writing and byte-perfect preservation of every Photoshop-only metadata block
- Not tested much yet; expect bugs
- Patchy is much slower than Photoshop, especially on large documents, it's had very little optimization work, and it doesn't yet support GPU acceleration, so performance is not great yet.  Expect slowdowns and high CPU usage, especially on large documents.

## License

Patchy is released under the MIT License. Third-party runtime notices are tracked in `NOTICE-THIRD-PARTY.md`.

## Trademark Notice

Patchy is not affiliated with, endorsed by, or sponsored by Adobe. Adobe Photoshop is a trademark of Adobe in the United States and/or other countries.

## AI Disclosure

This project was developed with significant assistance from AI tools.  I mean, you can still blame me (Seth) for bugs, but I just wanted to mention it.

## Credits

Created by Seth A. Robinson - [Homepage](https://www.rtsoft.com/) | [Blog](https://www.codedojo.com/) | [Twitter](https://twitter.com/rtsoft) | [Bluesky](https://bsky.app/profile/rtsoft.com) | [Mastodon](https://mastodon.gamedev.place/@rtsoft)
