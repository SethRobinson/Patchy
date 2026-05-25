# Packaging

Photoslop should ship as signed native binaries:

- Windows: local signed/unsigned zip package first, signed installer later.
- macOS: signed and notarized DMG or PKG.
- Linux: AppImage and Flatpak.

Release packaging must include:

- Dependency license notices.
- SBOM.
- Debug symbol upload.
- Crash-reporting configuration.
- Auto-update metadata.

The Windows zip package is created by `build-release.bat`. Installer, notarization, and Linux packaging scripts are placeholders until CI has a real signing and publishing environment.
