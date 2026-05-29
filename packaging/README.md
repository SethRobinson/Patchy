# Packaging

Patchy should ship as signed native binaries:

- Windows: local signed/unsigned zip package and per-user installer first, signed/published installer later.
- macOS: signed and notarized DMG or PKG.
- Linux: AppImage and Flatpak.

Release packaging must include:

- Dependency license notices.
- Module SBOM/license metadata for deployed third-party runtime components.
- Debug symbol upload.
- Crash-reporting configuration.
- Auto-update metadata.

The Windows zip package and installer are created by `build-release.bat`; they currently include Qt module SPDX notices copied from the local Qt installation. Publishing automation, notarization, and Linux packaging scripts are placeholders until CI has a real signing and publishing environment.
