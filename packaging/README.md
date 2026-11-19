# Packaging

Patchy's release packages and their sources are organized by platform.

- [windows](windows/README.md): signed installer and portable zip, built by
  `scripts\release\build-release.bat`.
- [macos](macos/README.md): signed and notarized DMG.
- [Linux Flatpak](linux/README.md) ships as a signed repository and standalone bundles. The [AppImage](../docs/appimage.md) is a portable x86_64 download.
- [WebAssembly](../docs/release-wasm.md) uses the site shell under `packaging/web`.
- [branding](branding/README.md): logo and icon source art.

The release order, safety checks and upload scripts are in
[docs/release-process.md](../docs/release-process.md).
