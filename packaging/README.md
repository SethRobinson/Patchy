# Packaging

Photoslop should ship as signed native binaries:

- Windows: signed installer first, MSIX later.
- macOS: signed and notarized DMG or PKG.
- Linux: AppImage and Flatpak.

Release packaging must include:

- Dependency license notices.
- SBOM.
- Debug symbol upload.
- Crash-reporting configuration.
- Auto-update metadata.

The scripts in platform folders are placeholders until CI has a real signing environment.
