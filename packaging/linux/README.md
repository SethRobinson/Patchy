# Linux packaging (Flatpak)

Patchy ships on Linux as a self-hosted single-file Flatpak bundle
(`build/package/Patchy-<version>.flatpak`; `scripts\release\upload-linux-to-rtsoft.bat` publishes the
newest one to rtsoft.com under the stable name `PatchyLinux.flatpak`, matching the
Windows "latest" convention). Users install it with the README one-liner, which adds the
Flathub user remote if it is missing and then runs `flatpak install --user -y
/tmp/PatchyLinux.flatpak`. The app's update dialog shows the same command.

Two things keep that install working on a machine with no preconfigured remote and no
root (GitHub issue 14, CachyOS, September 2026):

- `make-flatpak.sh` passes `--runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo`
  to `flatpak build-bundle`. A single-file bundle has no origin remote, so that metadata
  key is the only hint `flatpak install` gets about where `org.kde.Platform//6.8` lives;
  without it the install aborts with "requires the runtime ... which was not found".
  The value lives in the bundle file's own header, not in the app's `metadata` file, so
  check a bundle with `strings -n 12 Patchy-<version>.flatpak | grep -m1 flatpakrepo`.
  For a full end-to-end check, point `FLATPAK_SYSTEM_DIR` and `FLATPAK_USER_DIR` at two
  empty directories (a machine with no runtimes and no remotes) and run `flatpak install
  --user -y <bundle>`: flatpak must create the `flathub` remote and pull the runtime by
  itself. Verified 2026-09 on the linux build host; the pre-fix 0.97 bundle fails there with the
  exact error from the issue.
- Every documented command uses `--user`. Without it flatpak targets the system
  installation and asks polkit for root, which is the permission error normal users hit.
  The documented commands carry no `remote-add` step: every published bundle from 0.97
  on has `--runtime-repo`, and the `flathub` remote flatpak creates from it is what the
  codec line below resolves in the user installation. Keep `README.md`, `main_window_files.cpp`
  (`show_update_available`), the HEIC hint in `heif_document_io.cpp`, and the
  `app_shell_tests.cpp` assertion in step.

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
  `flatpak install --user` fix, which the README download section also documents.
  Patchy bundles no HEVC code, and the block goes away if the runtime moves to
  6.10+, whose base inherits codecs-extra instead (auto-installed with the
  runtime, so HEIC then works with zero user action).
- `com.rtsoft.patchy.desktop`, `com.rtsoft.patchy.metainfo.xml`, `icons/hicolor/*` —
  freedesktop integration, installed by CMake's `UNIX AND NOT APPLE` install rules
  (binary in `bin/`, fonts/translations under `share/patchy/`). The icons were
  extracted from the native layers of `src/app/patchy.ico`. Bump the metainfo
  `<release>` tag with each version (see `docs/release-process.md`).
- `make-flatpak.sh` — builds the bundle on a machine with `flatpak-builder`
  (the linux build host): `bash packaging/linux/make-flatpak.sh`. One-time setup is in the
  script header. `scripts/remote/release-linux.ps1` drives it from Windows.

The source manifest excludes root `test-artifacts`, including Unix sockets left
by interrupted MCP tests. Generated test output is not a packaging input.

Known Wayland caveats (accepted for v1): a second launch raises the running window
but compositors may only flash the taskbar entry instead of stealing focus (no
xdg-activation token), and clipboard content set by Patchy vanishes when the app
exits (no clipboard manager in the sandbox). `--socket=fallback-x11` lets users force
`QT_QPA_PLATFORM=xcb` if a Wayland quirk bites.

Headless runs need no `--env`: `flatpak run com.rtsoft.patchy --headless --run-script
/path/to/script.js --script-output /path/to/out.txt` selects Qt's offscreen platform
inside the sandbox (the org.kde.Platform runtime ships the plugin), and
`--filesystem=home` covers the script, the output file, and the documents it opens.
Offscreen startup disables the process's desktop D-Bus connection so Qt's portal
appearance query cannot delay a headless command or MCP client disconnect.
The default MCP socket lives in `$XDG_RUNTIME_DIR/app/$FLATPAK_ID`, shared by
separate app and connector sandboxes. It retains per-user socket permissions;
the private `/tmp` in each invocation cannot support this attachment.
`make-flatpak.sh` runs that command inside the built sandbox (`flatpak-builder --run`,
nothing installed) before `flatpak build-bundle` and fails unless the script output ends
in `[done]`, so a bundle that cannot run headless is never produced.

The Patchy Flatpak installed on the linux build host is a manual test install and the release flow
never refreshes it (September 2026: it still reported 0.88 after the 0.91 release). To
test the shipped bundle there, reinstall it first:
`flatpak install --user -y --reinstall --bundle build/package/Patchy-<version>.flatpak`,
then `flatpak run --user com.rtsoft.patchy --headless --run-script ...`.
