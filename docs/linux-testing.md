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
- flatpak-builder sets `SOURCE_DATE_EPOCH` to the manifest's modification time, so the
  in-app build date of such a build is that time, not the build time.

## What differs inside the sandbox

Triage of the first full run (October 13, 2026: 11 UI failures, all passing on the host's
Qt 6.8.3 build) found no Qt 6.11 behavior change among them. The causes were the
sandbox, and one of them was a real bug in the shipped Flatpak:

- **Fonts live elsewhere.** The runtime keeps Liberation and DejaVu in
  `/usr/share/fonts/liberation-fonts` and `/usr/share/fonts/dejavu`, not Debian's
  `/usr/share/fonts/truetype/...`; it ships no CJK face, and the host's fonts appear
  under `/run/host/fonts`. `tests/test_fonts.hpp` lists both layouts. With no registered
  UiDefault font the suite silently runs on a different default face, which is what nine
  of the eleven were (options bar height, clipped labels, "Arial" resolving as a missing
  font).
- **Every run has the same pids.** The sandbox is a pid namespace: the test binary gets
  the same low pid each run, and the Flatpak app is always pid 2. Anything keyed on a
  pid, or trusting a `QLockFile` pid to mean "another live process", is wrong there.
  The suite's per-process stores (`tests/ui/main.cpp`) handle a leftover store that names
  their own pid. In the app this was crash recovery: a crashed instance's lock named the
  scanning instance's own pid, so the Flatpak never offered the crashed documents
  ([document-recovery.md](document-recovery.md)).
- **`FLATPAK_ID` is the SDK's.** The harness unsets it, since the test binary is not the
  installed Flatpak; a test of Flatpak-specific behavior sets it itself.

The eleventh, `ui_tips_psd_speed_mode_line_clip_if_available` (runs only where the local
`tips.psd` fixture exists), was a real text bug that the sandbox's fallback face (DejaVu
Sans, against Noto Sans on the linux build host) made large enough to fail: calibrated
box text grew its render rect on every re-edit. Fixed in the app
([text-render-calibration.md](text-render-calibration.md)).

Source changes the first SDK compile of the tests needed: a missing `<algorithm>`
include, two GCC 15 `-Warray-bounds` false positives in test helpers (fixed by reserving
the vector up front), and `QImage::mirrored`, deprecated from Qt 6.9 (its replacement
`flipped` does not exist in 6.8.3, so tests call a version-checked helper).

## Foreign drags and the KDE rig (GitHub issue 62)

On Wayland the old dock column dragged its panels with a real `QDrag`, so every enabled `acceptDrops` widget under the cursor saw drag events that carried no layers. The panels no longer float or drag, but any foreign drag (a file manager's, another app's) still reaches those widgets the same way.

**Never repolish a widget from inside its own event filter.** `installEventFilter` puts a filter ahead of those already installed, and KDE's Breeze style installs filters in `polish()`. A filter that calls `style()->unpolish/polish` on the watched widget is moved behind Qt's loop cursor and runs again for the same event; done unconditionally it never ends (issue 62: the layer action buttons' drag filter froze the Flatpak on KDE). Change the property only when it differs and repolish one event-loop hop later (`handle_layer_action_button_drag_event`; `ui_layer_action_button_foreign_drag_never_repolishes_in_filter` stands a filter-installing proxy style in for Breeze). It needs Breeze (the Flatpak's KDE runtime, `XDG_CURRENT_DESKTOP=KDE`) and enabled buttons (a document open). KWin's "not responding" kill sends SIGABRT: that coredump is a hang.

KDE rig on the Linux build host, with no root and no desktop control:

- KWin: a private apt configuration (own `Dir::State`, `Dir::Cache`, empty status file, the host's Ubuntu keyring) pointed at a newer Ubuntu series downloads `kwin-wayland`, `qt6-wayland` and `plasma-wayland-protocols` plus dependencies; `dpkg-deb -x` each into a prefix. Run it through the prefix's own loader: `<prefix>/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 --library-path <prefix libs> <prefix>/usr/bin/kwin_wayland --virtual --width 1600 --height 1000 --socket <name> --no-lockscreen --no-global-shortcuts --no-kactivities` under `dbus-run-session`, with `QT_PLUGIN_PATH`, `XDG_DATA_DIRS`, `XKB_CONFIG_ROOT` pointing into the prefix and `KWIN_FORCE_OWN_QPA=1` (the loader hides the executable name), `KWIN_COMPOSE=Q`, `KWIN_WAYLAND_NO_PERMISSION_CHECKS=1`, private `XDG_CONFIG_HOME`.
- Input: a client of `org_kde_kwin_fake_input` (authenticate, `pointer_motion_absolute`, `button` 272); a few dozen lines of Python over the Wayland socket are enough. Releasing happens when the client disconnects, so keep one process per gesture.
- App: `WAYLAND_DISPLAY=<name> flatpak run --user --env=XDG_CURRENT_DESKTOP=KDE --env=PATCHY_SETTINGS_DIR=/tmp/x --env=PATCHY_NO_SINGLE_INSTANCE=1 com.rtsoft.patchy <image>`. Add `--devel --command=gdb` for stacks (SIGINT the app's host PID to sample), or `--command=<host build>/patchy --env=LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu` to run a fresh host build on the runtime's Qt (Breeze loads). `WAYLAND_DEBUG=1` traces the protocol; `patchy --screenshot` captures a native instance.

GNOME variant: `gnome-shell --headless --wayland --no-x11 --unsafe-mode --virtual-monitor 1600x1000 --wayland-display <name>` under `dbus-run-session` (without `--no-x11` the shell deadlocks in PulseAudio's X11 lookup), driven via `org.gnome.Mutter.RemoteDesktop` and `org.gnome.Shell.Eval`/`Screenshot`. Script: `scripts/dev/wayland-gnome/gnome-rig.sh`. `start-shell` publishes `WAYLAND_DISPLAY` to the rig bus before the shell activates the portals (docs/wayland.md).

Stop every rig process by the PID recorded at launch. A name-based kill (`pgrep -n -x gnome-shell`) once took down the host's own desktop shell.
