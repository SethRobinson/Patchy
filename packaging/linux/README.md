# Linux packaging (Flatpak)

Patchy ships on Linux as a self-hosted single-file Flatpak bundle
(`build/package/Patchy-<version>.flatpak`; `upload-linux-to-rtsoft.bat` publishes the
newest one to rtsoft.com under the stable name `PatchyLinux.flatpak`, matching the
Windows "latest" convention). Users install it with
`flatpak install ./PatchyLinux.flatpak`; the KDE runtime it needs is fetched
automatically from Flathub.

- `flatpak/com.rtsoft.patchy.yml` — the manifest. Runtime `org.kde.Platform//6.8`
  matches the Qt line the app is developed against; Qt is deliberately not vendored.
  The manifest is kept Flathub-compliant so a Flathub submission stays a cheap later
  option. `--filesystem=home` is the deliberate v1 choice (recents, CLI file
  arguments, and brush/palette folders behave like a desktop editor; file dialogs
  still go through the portal). The `add-extensions` block declares
  `org.freedesktop.Platform.ffmpeg-full//24.08`, which supplies the HEVC decode
  plugin the runtime's libheif loads for HEIC opens. Verified 2026-07:
  single-file BUNDLE installs never auto-pull the extension (even with flathub
  visible to the installation; repo-based installs such as a future Flathub
  listing would). Everything else works without it; only HEIC opens are
  affected, and Patchy's open-error dialog shows the exact one-line
  `flatpak install` fix, which the README download section also documents.
  Patchy bundles no HEVC code, and the block goes away if the runtime moves to
  6.10+, whose base inherits codecs-extra instead (auto-installed with the
  runtime, so HEIC then works with zero user action).
- `com.rtsoft.patchy.desktop`, `com.rtsoft.patchy.metainfo.xml`, `icons/hicolor/*` —
  freedesktop integration, installed by CMake's `UNIX AND NOT APPLE` install rules
  (binary in `bin/`, fonts/translations under `share/patchy/`). The icons were
  extracted from the native layers of `src/app/patchy.ico`. Bump the metainfo
  `<release>` tag with each version (see `docs/release-process.md`).
- `make-flatpak.sh` — builds the bundle on a machine with `flatpak-builder`
  (glados.local): `bash packaging/linux/make-flatpak.sh`. One-time setup is in the
  script header. `scripts/remote/release-linux.ps1` drives it from Windows.

Known Wayland caveats (accepted for v1): a second launch raises the running window
but compositors may only flash the taskbar entry instead of stealing focus (no
xdg-activation token), and clipboard content set by Patchy vanishes when the app
exits (no clipboard manager in the sandbox). `--socket=fallback-x11` lets users force
`QT_QPA_PLATFORM=xcb` if a Wayland quirk bites.
