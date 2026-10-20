# Linux AppImage

Patchy ships on Linux as a Flatpak ([packaging/linux/README.md](../packaging/linux/README.md))
and, since GitHub issue 83, as an AppImage: `PatchyLinux.AppImage` on GitHub Releases (with
`PatchyLinux.AppImage.zsync`) and the rtsoft.com mirror. One x86_64 file, no runtime to
install, runs on Ubuntu 22.04 and every newer distro (Mint 21+, Debian 12+, Fedora 36+).

## Compatibility floor: build on Ubuntu 22.04

An AppImage runs on the host's glibc and libstdc++, so it only runs where those are at
least as new as the build machine's. The community AppImage in issue 83 was built on 24.04
and failed on 22.04 with `GLIBC_2.38` / `GLIBCXX_3.4.32 not found`. So everything is
compiled inside an Ubuntu 22.04 container (glibc 2.35) with 22.04's own GCC 12 (libstdc++
GLIBCXX_3.4.30, CXXABI_1.3.13). glibc and libstdc++ are never bundled; every newer distro
provides compatible ones.

- Qt is the aqt 6.8.3 `linux_gcc_64` kit with `qtimageformats qtpdf`, the same Qt as the
  Windows and macOS builds (it needs only glibc 2.28). The Flatpak runs Qt 6.11 from its
  KDE runtime, so the two Linux packages can differ in Qt behavior.
- The code needs C++20 library features GCC 11+ has; GCC 12 is the AppImage compiler, so
  `linux-appimage` is a supported build under the zero-warning rule ([platform.md](platform.md)).
- CMake comes from pip in the image (22.04's apt CMake is 3.22; the project needs 3.26).
- Moving the floor (to 20.04, say) would need a newer compiler than the distro's, a
  bundled libstdc++ with a run-time version check in AppRun, and a bundled OpenSSL 3 (Qt
  6.8's TLS backend does not load 20.04's OpenSSL 1.1). Not done.

## Build

`packaging/linux/make-appimage.sh` on the linux build host (prerequisite: `sudo apt-get
install -y podman uidmap`, one time). It builds the image from
`packaging/linux/appimage/Containerfile` on first use (tag = hash of the Containerfile and
`scripts/setup-ai-deps.sh`, so editing either rebuilds it), then runs
`appimage/build-in-container.sh` in rootless podman with the repository mounted at `/src`
(read-write, `--userns=keep-id`, so outputs belong to the host user):

1. Configure and build preset `linux-appimage` into `build/appimage` (the Flatpak's
   options: tests off, `PATCHY_REQUIRE_AI_SELECTION=ON`, `PATCHY_GMIC_THREADS=ON`, ONNX
   Runtime and models from `/opt/deps`, which the image fills with `setup-ai-deps.sh`).
   The preset's paths (`/opt/qt`, `/opt/deps`, `gcc-12`) exist only in the container.
2. `cmake --install` into `build/appimage-appdir/usr` (the normal `bin/` + `share/patchy/`
   layout; every resource lookup already probes `../share/patchy`). `libonnxruntime.so*`
   goes to `usr/lib` (the app `dlopen`s it by name; linuxdeploy's `$ORIGIN/../lib` RUNPATH
   finds it). Qt's SPDX notices go beside the CMake-installed licenses.
3. linuxdeploy + linuxdeploy-plugin-qt bundle the libraries and Qt plug-ins: xcb (default),
   wayland-egl, wayland-generic and offscreen platforms (`EXTRA_PLATFORM_PLUGINS`), image
   formats, the SVG icon engine, TLS. linuxdeploy's excludelist keeps glibc, libstdc++,
   libGL/EGL, libX11/xcb core and fontconfig on the host. Bundling those is what made the
   community AppImage abort in `XCloseDisplay` at exit. The script fails if the SVG plug-ins
   (stylesheet images, GitHub issue 93), TLS, offscreen, xcb, wayland or the wayland
   plug-in folders are missing.
4. **ABI audit**: every ELF file in the AppDir must need at most GLIBC_2.35, GLIBCXX_3.4.30
   and CXXABI_1.3.13, and `ldd` must resolve everything inside 22.04. This is the guard
   for "runs on 22.04"; never relax it to make a build pass.
5. appimagetool packs onto the static type2 runtime (no libfuse2 needed, which 24.04+ lacks
   by default) with update information
   `gh-releases-zsync|SethRobinson|Patchy|latest|PatchyLinux.AppImage.zsync`. The zsync file
   records the AppImage's file name, so both are packed under their published names and
   only then renamed to `build/package/Patchy-<version>-x86_64.AppImage(.zsync)`.
6. Smoke checks inside the container (extracted run) and again on the host through a real
   FUSE mount: `--headless --run-script` must end in `[done]`, and
   `PatchyLinux.AppImage patchy-mcp --check` must pass.

**AppImage catalog** (appimage.github.io, entry `data/Patchy`, PR 10388 there): its test
mounts the newest release's AppImage and runs its `code/appdir-lint.sh` on Ubuntu 22.04.
That lint fails on any `desktop-file-validate` error from 22.04's desktop-file-utils 0.26,
which predates Desktop Entry 1.5, so the build drops `SingleMainWindow` from the AppImage's
copy of the desktop file (the Flatpak keeps it). It warns unless `.DirIcon` is a PNG, so the
build points `.DirIcon` at a root copy of the 256 px PNG instead of linuxdeploy's SVG. The
build fails if either check regresses. Releases up to 1.13 shipped `SingleMainWindow` and
failed the lint (GitHub issue 208); a newer host validator (24.04's 0.27) accepts the key,
so reproduce with 22.04's. Still warnings, by choice: "Linux" in the file name
(renaming breaks the zsync update name older installs follow), the dark default window, and
"No appdata file" (the lint looks only for `*appdata.xml`; ours is the current
`.metainfo.xml` name, which the worker still copies for screenshots). Keep that name: under
it the lint would run `appstreamcli validate-tree`, and 22.04's AppStream 0.15 fails our
metainfo on the 1.0 `<developer>` tag and `vcs-browser` URL type. `make-appimage.sh` and
`make-flatpak.sh` also validate the shipped desktop file and metainfo with the host's
`desktop-file-validate` and `appstreamcli` when installed. After a release with a fix,
`/retest` on that PR reruns the test.

All downloaded tools are pinned to release tags and SHA-256 in the Containerfile, and the
base images are pinned by digest. To bump one, update tag and checksum together.

`make-appimage.sh --tests [filter]` builds `linux-appimage-tests` in the same container
and runs both suites offscreen: the gate for the AppImage's toolchain (gcc-12, Qt 6.8.3).
Flatpak verification stays on the SDK's Qt ([linux-testing.md](linux-testing.md)).
The test container mounts the host's `/usr/share/fonts` read-only at `/usr/local/share/fonts`
(like the Flatpak SDK's `/run/host/fonts`) because several text tests depend on the installed
fonts, and the image's locale is `en_US.UTF-8`: under the C locale `QCollator` ignores
numeric mode and the natural-order tests fail (`page-10` before `page-2`).

## Layout and entry point

`packaging/linux/appimage/AppRun` dispatches: `PatchyLinux.AppImage [files]` starts
Patchy; `PatchyLinux.AppImage patchy-mcp [args]` (or a symlink to the AppImage named
`patchy-mcp`) starts the bundled connector. The argument is
`kAppImageConnectorArgument` (`src/ui/appimage_environment.hpp`). The root
`.desktop` file and icon are `com.rtsoft.patchy`.

AppRun is the only entry point. linuxdeploy wraps a custom AppRun in its own (the custom
one renamed `AppRun.wrapped`) that sources linuxdeploy-plugin-qt's hook, and that hook
forces `QT_QPA_PLATFORMTHEME=gtk2` on GNOME and XFCE. Qt 6 has no gtk2 theme, so the
AppImage fell back to Qt's widget file dialog, under the desktop's light title bar
(GitHub issue 156). The build moves `AppRun.wrapped` back, deletes the hook, and fails
if linuxdeploy adds any other hook. AppRun then sets `QT_QPA_PLATFORMTHEME=xdgdesktopportal`
when the user has not set one. Qt loads that bundled theme by itself only inside Flatpak
and Snap. It wraps the theme Qt would otherwise pick (`gnome`, `kde`, `generic`: fonts,
the color scheme and the global menu bar are unchanged) and adds the portal's file
dialogs, GNOME's or KDE's own, as in the Flatpak. Where no FileChooser portal answers
the startup version query, dialogs stay Qt's widget dialog. On the headless GNOME rig the
portals must get `WAYLAND_DISPLAY` from the bus's activation environment before the shell
starts (`gnome-rig.sh start-shell` does this), or the portal dialog never appears and
Patchy waits on it ([wayland.md](wayland.md), "Reproducing on a headless GNOME Shell").

## Running from a mount: app behavior

The runtime mounts the image at a new `/tmp/.mount_*` directory on every launch and sets
`APPIMAGE` (the file) and `APPDIR` (the mount). `appimage_file_path()` returns `APPIMAGE`
only when this executable is inside `APPDIR` and `FLATPAK_ID` is unset: an AppImage'd
program (an AI app, an IDE terminal) that starts a native Patchy leaves its own variables
behind. Wherever a path is shown, stored or compared across launches, use it (or
`user_visible_executable_path()`), never `applicationFilePath()`:

- MCP attach socket (`mcp_attachment_endpoint`): the identity is home + `appimage`, not the
  mount, so any Patchy AppImage of the user attaches to any other, including after the
  file is replaced by a newer release.
- Help > Set up AI Control: the connector is `"<file>" patchy-mcp`; the skill folder and
  local setup guide are in the mount, flagged as existing only while Patchy runs.
- Connector freshness watches the `.AppImage` file (replacing it is the update).
- The Script Manager's command-line example names the `.AppImage` file.
- Update prompt: with the manifest's pinned AppImage keys and a writable folder, **Update
  Now** downloads the new AppImage, replaces this file on close and starts it
  ([auto-update.md](auto-update.md), "AppImage"). Otherwise it names the file, Download is
  the default button and opens `linux.appimage_url` (else `kAppImageDownloadUrl`, the
  GitHub permalink). Never a flatpak command.
- HEIC: aqt's Qt has no HEIF plug-in. Instead the app dlopens the host's `libheif.so.1`
  at run time (`heif_document_io_linux.cpp`, GitHub issue 117), so HEIC opens wherever
  the distro's libheif and its HEVC plug-in are installed (Ubuntu/Debian: `libheif1` plus,
  on 24.04 and later, `libheif-plugin-libde265`; Fedora: `libheif` plus RPM Fusion's
  `libheif-freeworld`). The open error shows the exact install command. The AppImage must never
  bundle libheif or an HEVC decoder ([legal-constraints.md](legal-constraints.md));
  linuxdeploy copies only what the binary links, and dlopen links nothing. The loader
  uses `RTLD_LOCAL`, so the host library's codecs stay out of the app's symbol namespace;
  its own `libjpeg`/`libpng` dependencies resolve to the bundled copies when the sonames
  match, which is the ABI those libraries keep. AVIF needs no plug-in: the decoder is
  compiled in ([avif.md](avif.md)).

Single-instance forwarding needs nothing: its name is per user, not per path. Settings and
recovery live in the normal per-user folders (shared with a native Linux build).

## Applications menu entry

A Wayland compositor finds a window's icon through the installed `.desktop` file named after
its app id (`com.rtsoft.patchy`, `setDesktopFileName` in `main.cpp`), and an AppImage installs
none, so without one the taskbar shows a generic icon (GitHub issue 226). Most AppImages leave
this to helpers (Gear Lever, AppImageLauncher); Patchy offers it itself, never silently.
Code: `ui/appimage_menu_entry.{hpp,cpp}` (files), `main_window_appimage_menu_entry.cpp` (UI).

- **Help > Add Patchy to Applications Menu...** (`help.appimage_menu_entry`, after Set up AI
  Control) is visible only when `appimage_file_path()` is set: never in the Flatpak, a native
  build, Windows or macOS. Once added it reads **Remove Patchy from Applications Menu**.
  The state is re-read each time Help opens.
- **Add** writes `$XDG_DATA_HOME/applications/com.rtsoft.patchy.desktop` (default
  `~/.local/share`; a relative `XDG_DATA_HOME` is ignored, as the spec says): the AppImage's own
  shipped desktop file with every `Exec` program replaced by the quoted `.AppImage` path
  (`%F` kept; quoting and `%%` per the Desktop Entry spec, `desktop_entry_exec_argument`), a
  `TryExec` naming the file (a deleted AppImage hides the entry instead of leaving a dead one)
  and the marker `X-Patchy-AppImage=true`. Then the hicolor icons from `$APPDIR/usr/share`
  (7 PNG sizes and the scalable SVG, `appimage_menu_entry_icon_names`), icons first. Only
  files whose bytes differ are written, all through `write_file_bytes_atomically`.
- **Remove** deletes exactly those 9 paths (built from the fixed name list, no listing or
  wildcard), only when the desktop file carries the marker. Folders stay. Removing also sets
  `appimage/menuEntryPromptDeclined`.
- After a change Patchy runs `update-desktop-database -q <applications>` when installed, and
  `gtk-update-icon-cache` only when `icons/hicolor/icon-theme.cache` already exists: GTK
  trusts a cache newer than the theme folder, so a new user cache would hide icons other
  programs add later without refreshing it. Both detached, best effort.
- **Already in the menu** (`AddedElsewhere`): a `com.rtsoft.patchy.desktop` without the marker
  (a native install to `~/.local`), any desktop file in the user's applications folder whose
  `Exec` starts a `.AppImage` and that names Patchy (icon containing `com.rtsoft.patchy`, as
  AppImageLauncher's `appimagekit_<hash>_com.rtsoft.patchy`; `Name=Patchy`; or a file name
  containing "patchy", as Gear Lever's moved `patchy.appimage`), or a `com.rtsoft.patchy.desktop`
  in any `$XDG_DATA_DIRS` (the Flatpak export, a package; the window already gets its icon,
  and ours would hide that entry). Then there is no offer, and the item is disabled with a
  tooltip saying why; Help shows that one action's tooltip (`ActionToolTipFilter`).
- **Startup** (`begin_appimage_menu_entry_startup`, main.cpp only, phase
  `appimage.menu_entry`; never `--headless`, `--run-script`, export, stress, screenshot or
  benchmark runs, and tests call the offer explicitly): an entry Patchy added is rewritten
  when the AppImage moved or a new release ships different files (consent was given). Else,
  unless declined, a non-modal offer appears 2 s after startup (retrying while another dialog
  is up): Add / Not Now (asks again next launch) / Don't Ask Again (`appimage/menuEntryPromptDeclined`,
  a compatibility contract). It names the Help command for later. Its Add only adds: Help may
  have added the entry while it was open.
- Tests (`appimage_menu_entry_tests.cpp`): a Unicode data folder and an AppImage name with
  every reserved Exec character; Add writes the expected entry and icons, Remove deletes only
  them; the moved-AppImage refresh; AppImageLauncher, Gear Lever, packaged and foreign entries;
  the item's visibility and texts and the offer's buttons (Linux, faked `APPIMAGE`/`APPDIR`,
  `MainWindowTestAccess::set_appimage_bundled_share`, which also skips the cache tools).
- GNOME check on the headless rig ([wayland.md](wayland.md)): give the shell and the AppImage the
  same scratch `XDG_DATA_HOME` and `XDG_DATA_DIRS=/usr/local/share:/usr/share` (a user Flatpak's
  export would otherwise provide the entry), add through a forwarded `--run-script` calling
  `app.runCommand("help.appimage_menu_entry")`, and read
  `Shell.WindowTracker.get_default().get_window_app(w).get_id()` through `Shell.Eval`: it is
  `window:N` (the generic icon) without the entry and `com.rtsoft.patchy.desktop` with it (GNOME
  Shell 46, October 2026). A shell whose GLib has no file monitor (all inotify instances in use;
  its log says "Unable to find default local file monitor type") never sees an entry added while
  it runs, so whether a running GNOME picks the entry up live is checked on a real desktop.

## Testing

- `packaging/linux/appimage/test-appimage.sh <file> [ubuntu22 debian12 fedora40]` runs the
  AppImage in clean containers that have only stock desktop libraries: a headless script
  that runs Object Selection (bundled ONNX Runtime and models), a real X11 window under
  Xvfb (`--screenshot`, clean exit), a Wayland window under headless weston, and
  `patchy-mcp --check`. Screenshots land in `build/appimage-test/<distro>/`. Containers
  have no FUSE, so the AppImage extracts itself (same files).
- MCP on the host (real FUSE mounts, separate per launch): `tests/mcp_client_tests.py`
  with a `patchy-mcp` symlink to the AppImage as the connector (exercises AppRun's
  symlink dispatch), and `tests/appimage_mcp_tests.py <file>`: an offscreen GUI launch,
  then `<file> patchy-mcp --attach` from a second launch, guarded edit, preview, reconnect.
  Extracted runs share one directory and would hide a socket-identity bug, so that test
  refuses `APPIMAGE_EXTRACT_AND_RUN`.
- A real release runs all of the above plus `make-appimage.sh --tests`:
  `release-all-automated.bat verify-linux` (`scripts/release/verify-linux.sh`) does it.
- App-side unit tests: the `appimage_menu_entry` group (above), `appimage_environment_names_the_appimage_file`,
  `ui_update_available_dialog_offers_appimage_download` (Linux only),
  `ui_ai_setup_blurb_reports_missing_and_flatpak_forms`, and the `appimage_url` cases in
  `update_manifest_parser_handles_supported_cases` (app_shell_tests.cpp).

## Release

`release-linux.ps1` runs `make-appimage.sh` after `make-flatpak.sh` and copies both files
back; `publish-github-release.ps1` publishes them as `PatchyLinux.AppImage` and
`PatchyLinux.AppImage.zsync` (stable names, never renamed); `upload-linux-to-rtsoft.bat`
mirrors the AppImage only. The announcement commit adds the `appimage_url` and three
`appimage_install_*` keys `publish-github-release.ps1` prints to the `linux` entry of
`latest_version.json` ([release-process.md](release-process.md)).
