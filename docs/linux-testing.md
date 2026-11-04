# Linux testing: the Flatpak's own Qt

This page covers the Flatpak. The AppImage is built and tested on its own toolchain
(Ubuntu 22.04, GCC 12, aqt Qt 6.8.3): `packaging/linux/make-appimage.sh --tests` and
`packaging/linux/appimage/test-appimage.sh`, described in [appimage.md](appimage.md).

The Flatpak takes Qt from the
`org.kde.Platform` runtime named in `packaging/linux/flatpak/com.rtsoft.patchy.yml`
(6.11 as of October 2026). The Windows and macOS builds use Qt 6.8.3. Linux verification
therefore runs on the runtime's Qt, never on a different one (Seth, October 13, 2026):
GitHub issue 88 was a Qt 6.11 behavior change that every suite on Qt 6.8.3 passed.

## The rule

- Linux test runs use the Flatpak SDK's Qt. `scripts\remote\remote-build.ps1 -Target
  linux` does this by default.
- A Linux bug report is reproduced on the Flatpak (the installed one, or a build of the
  tree), not on a host build against another Qt.
- A UI fix for a Linux report is verified on that Qt before it is called fixed.
- When the runtime version in the manifest changes, run both full suites again; the
  scripts read the version from the manifest, so nothing else needs editing.

## How it runs

`-Target linux` selects the `linux-flatpak-qt` preset (`build/linux-flatpak-qt`, Release,
tests on, no `CMAKE_PREFIX_PATH`). `scripts/remote/build-and-test.sh` sees that preset,
reads `sdk` and `runtime-version` from the manifest, and re-runs itself inside the SDK:

    flatpak run --user --filesystem=home --command=bash org.kde.Sdk//<version> \
      ~/patchy/src/scripts/remote/build-and-test.sh linux-flatpak-qt [--filter ...]

Inside the sandbox (`/.flatpak-info` exists) the script skips the host tool path and the
dpkg sysroot, so cmake, ninja, the compiler (GCC 15.2.0 for 6.11), Qt and the fonts all
come from the SDK. The source tree and `.deps` (ONNX Runtime, AI models) are the host's,
through `--filesystem=home`. The only prerequisite is the SDK `make-flatpak.sh` already
needs: `flatpak install --user -y flathub org.kde.Platform//<version> org.kde.Sdk//<version>`.

To run one test by hand, enter the same sandbox and run the binary from
`build/linux-flatpak-qt` with `QT_QPA_PLATFORM=offscreen`.

`-NativeQt` selects the older `linux-release` preset (system GCC, the aqt Qt 6.8.3 in
`.deps`). Use it only for tools that need a host binary: the `linux-asan` sync, or a
quick A/B to tell a Qt-version difference from a Patchy bug.

What this does not cover:

- The compiler matches the Flatpak build but the flags do not: flatpak-builder adds
  hardening flags, so a warning from the release build may still need the single-file
  check in [platform.md](platform.md).
- The suites run offscreen. A bug that needs real input or a real window is checked on
  the Flatpak itself in a private X server: `Xvfb :<n>`, then `flatpak run
  --env=QT_QPA_PLATFORM=xcb --env=PATCHY_SETTINGS_DIR=<dir> com.rtsoft.patchy` under
  `dbus-run-session`, or `flatpak-builder --run <build dir> <manifest> patchy` for an
  unreleased tree built with `flatpak-builder <build dir> <manifest>` (no `--repo`, and
  never `make-flatpak.sh`, which deletes the release bundles in `build/package`). XTest
  through Python `ctypes` on `libXtst.so.6` sends real clicks without extra packages;
  ImageMagick `import -window root` captures.
- Killing the `flatpak run` process leaves the sandboxed Patchy running (it does not
  match `pgrep -f com.rtsoft.patchy` either). Stop the app's own PID (`pgrep -x -f
  "patchy <args>"`), confirm with `flatpak ps`, and remove the attach and single-instance
  sockets a signal leaves in `$XDG_RUNTIME_DIR/app/com.rtsoft.patchy/` once nothing listens.
- flatpak-builder sets `SOURCE_DATE_EPOCH` to the manifest's modification time, so the
  in-app build date of such a build is that time, not the build time.

## What differs inside the sandbox

The sandbox differs from a host build in ways that change test and app behavior:

- **Fonts live elsewhere.** The runtime keeps Liberation and DejaVu in
  `/usr/share/fonts/liberation-fonts` and `/usr/share/fonts/dejavu`, not Debian's
  `/usr/share/fonts/truetype/...`; it ships no CJK face, and the host's fonts appear
  under `/run/host/fonts`. `tests/test_fonts.hpp` lists both layouts. With no registered
  UiDefault font the suite silently runs on a different default face, which shows as wrong
  options bar heights, clipped labels and "Arial" resolving as a missing font.
- **Every run has the same pids.** The sandbox is a pid namespace: the test binary gets
  the same low pid each run, and the Flatpak app is always pid 2. Anything keyed on a
  pid, or trusting a `QLockFile` pid to mean "another live process", is wrong there.
  The suite's per-process stores (`tests/ui/main.cpp`) handle a leftover store that names
  their own pid. Crash recovery therefore never takes a lock's pid alone as a live instance
  ([document-recovery.md](document-recovery.md)).
- **`FLATPAK_ID` is the SDK's.** The harness unsets it, since the test binary is not the
  installed Flatpak; a test of Flatpak-specific behavior sets it itself.

The sandbox's fallback face is DejaVu Sans (Noto Sans on the Linux build host), so text
measurements differ there: a box-text calibration error too small to fail on the host can
fail `ui_tips_psd_speed_mode_line_clip_if_available` (runs only where the local `tips.psd`
fixture exists) in the sandbox ([text-render-calibration.md](text-render-calibration.md)).

The tests build on both Qt 6.8.3 and the runtime's Qt: `QImage::mirrored` is deprecated
from Qt 6.9 and its replacement `flipped` does not exist in 6.8.3, so tests call a
version-checked helper (`mirrored_horizontally`). The SDK's GCC 15 reports `-Warray-bounds`
false positives in test helpers that reserving the vector up front avoids.

## Runtime updates and the spin box field (Qt 6.11.3)

The runtime branch takes Qt patch releases without a manifest change: 6.11 moved to Qt
6.11.3 on October 31, 2026, and the shipped Flatpak with it. Nine UI tests then failed.
Qt 6.11.3's `QStyleSheetStyle` (and 6.12.0, reported upstream as QTBUG-151308) cuts a spin
box's edit field at its buttons' rectangles, and a box without buttons gets empty ones at
the origin: a style sheet box (a border or padding) with `NoButtons`, which is every Patchy
field, got a 1 px line edit at x = 0 and showed no text. It also drops the left inset when
buttons are shown; nothing fixes that (text starts on the border).

`install_spin_box_edit_field_fix` (`src/ui/spin_box_edit_field_fix.cpp`, installed by
`main.cpp` and `tests/ui/main.cpp`) corrects the line edit on its every move or resize to
the field Qt 6.8 gives: the right inset from a right-to-left query (which cuts nothing),
both insets from `sizeFromContents` less the base style's own, and the broken field's top
and bottom. It acts only on that exact broken field (left 0, width 1, left-to-right,
`NoButtons`), so a Qt without the bug never changes. Test:
`ui_spin_box_edit_field_fix_keeps_styled_fields_readable`. To compare Qt versions, a
probe program built in the SDK (`g++ ... $(pkg-config --cflags --libs Qt6Widgets)`) and
against `.deps/Qt/6.8.3/gcc_64` on the host takes seconds. After any runtime update, run
the full suites before a release.

## Foreign drags and the KDE rig (GitHub issue 62)

Any foreign drag (a file manager's, another app's) reaches every enabled `acceptDrops` widget under the cursor with drag events that carry no layers.

**Never repolish a widget from inside its own event filter.** `installEventFilter` puts a filter ahead of those already installed, and KDE's Breeze style installs filters in `polish()`. A filter that calls `style()->unpolish/polish` on the watched widget is moved behind Qt's loop cursor and runs again for the same event; done unconditionally it never ends (issue 62: the layer action buttons' drag filter froze the Flatpak on KDE). Change the property only when it differs and repolish one event-loop hop later (`handle_layer_action_button_drag_event`; `ui_layer_action_button_foreign_drag_never_repolishes_in_filter` stands a filter-installing proxy style in for Breeze). It needs Breeze (the Flatpak's KDE runtime, `XDG_CURRENT_DESKTOP=KDE`) and enabled buttons (a document open). KWin's "not responding" kill sends SIGABRT: that coredump is a hang.

KDE rig on the Linux build host, with no root and no desktop control:

- KWin: a private apt configuration (own `Dir::State`, `Dir::Cache`, empty status file, the host's Ubuntu keyring) pointed at a newer Ubuntu series downloads `kwin-wayland`, `qt6-wayland` and `plasma-wayland-protocols` plus dependencies; `dpkg-deb -x` each into a prefix. Run it through the prefix's own loader: `<prefix>/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 --library-path <prefix libs> <prefix>/usr/bin/kwin_wayland --virtual --width 1600 --height 1000 --socket <name> --no-lockscreen --no-global-shortcuts --no-kactivities` under `dbus-run-session`, with `QT_PLUGIN_PATH`, `XDG_DATA_DIRS`, `XKB_CONFIG_ROOT` pointing into the prefix and `KWIN_FORCE_OWN_QPA=1` (the loader hides the executable name), `KWIN_COMPOSE=Q`, `KWIN_WAYLAND_NO_PERMISSION_CHECKS=1`, private `XDG_CONFIG_HOME`.
- Input: a client of `org_kde_kwin_fake_input` (authenticate, `pointer_motion_absolute`, `button` 272); a few dozen lines of Python over the Wayland socket are enough. Releasing happens when the client disconnects, so keep one process per gesture.
- App: `WAYLAND_DISPLAY=<name> flatpak run --user --env=XDG_CURRENT_DESKTOP=KDE --env=PATCHY_SETTINGS_DIR=/tmp/x --env=PATCHY_NO_SINGLE_INSTANCE=1 com.rtsoft.patchy <image>`. Add `--devel --command=gdb` for stacks (SIGINT the app's host PID to sample), or `--command=<host build>/patchy --env=LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu` to run a fresh host build on the runtime's Qt (Breeze loads). `WAYLAND_DEBUG=1` traces the protocol; `patchy --screenshot` captures a native instance.

- A container variant (issue 491): a Fedora image with `plasma-workspace kwin` (plus `python3-dbus` for KWin's D-Bus scripting and `ScreenShot2`) runs `kwin_wayland --virtual` and `plasmashell` with `XDG_RUNTIME_DIR` on a mounted folder. A Flatpak SDK run on the host then reaches the socket by absolute path: `flatpak run --user --filesystem=home --env=WAYLAND_DISPLAY=<folder>/<socket> --env=PATCHY_UI_TEST_PLATFORM=wayland --command=bash org.kde.Sdk//<version> -c "<build>/patchy_ui_visual_tests <filter>"`. `WAYLAND_DEBUG=client` shows the `xdg_toplevel` requests and configures. A KWin script loaded over D-Bus (`org.kde.kwin.Scripting.loadScript`, signature `ss`) can `print()` window state (`frameGeometry`, `fullScreen`, `maximizeMode`) into KWin's log under `QT_LOGGING_RULES=js.debug=true`.

GNOME variant: `gnome-shell --headless --wayland --no-x11 --unsafe-mode --virtual-monitor 1600x1000 --wayland-display <name>` under `dbus-run-session` (without `--no-x11` the shell deadlocks in PulseAudio's X11 lookup), driven via `org.gnome.Mutter.RemoteDesktop` and `org.gnome.Shell.Eval`/`Screenshot`. Script: `scripts/dev/wayland-gnome/gnome-rig.sh`. `start-shell` publishes `WAYLAND_DISPLAY` to the rig bus before the shell activates the portals (docs/wayland.md).

Stop every rig process by the PID recorded at launch. A name-based kill (`pgrep -n -x gnome-shell`) once took down the host's own desktop shell.
