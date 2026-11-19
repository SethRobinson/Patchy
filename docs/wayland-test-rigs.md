# Headless Wayland test rigs

Split from [wayland.md](wayland.md). Use GNOME for Mutter and portal behavior, and sway for popup protocol traces and timed drags.

## Reproducing on a headless GNOME Shell

`scripts/dev/wayland-gnome/gnome-rig.sh` runs the installed Flatpak on headless Mutter.
Its `start-shell` command starts `gnome-shell --headless --wayland --no-x11 --unsafe-mode --virtual-monitor <MON>`
on a private session bus and writes the environment to `build/gnome-rig/env`. Set
`MON=1366x768` to reproduce placement issues on a small screen.

Use `patchy <scale>` to start the Flatpak, then `script <file.js>` to forward a script
to that instance. Put the script under `$HOME` so the sandbox can see it. The `drive`
command reads one instruction per line from stdin:

- Move with `move X Y` (output pixels) or `rel DX DY` (relative drag motion). Use
  `press`, `release`, `click`, `rpress` and `rrelease` for the mouse buttons.
- Use `kdown SYM` and `kup SYM` for X keysyms. For held modifiers, use the evdev keycode
  commands `kcdown CODE` and `kcup CODE`. Send a key first because the virtual keyboard drops its first press.
- `sleep MS` waits, `pointer` prints Mutter's pointer position, and `shot NAME`
  captures through `org.gnome.Shell.Screenshot`. Use `shotc NAME` to include the pointer.

The pointer is `org.gnome.Mutter.RemoteDesktop` relative motion anchored at the bottom-left
corner, because the top-left one is the Activities hot corner and opens the overview; if
the overview is up anyway, `gdbus call ... org.gnome.Shell.Eval 'Main.overview.hide()'`.
`stop` kills Patchy by Flatpak id and the shell by the pids it recorded; never by name.

Portals on the rig bus: the shell D-Bus-activates xdg-desktop-portal while it starts, so
`start-shell` publishes `WAYLAND_DISPLAY` and `XDG_CURRENT_DESKTOP` to the bus's
activation environment before the shell runs; a portal activated without them never
shows a dialog and Patchy waits on it forever (`flatpak kill` it, then `stop` and start
over). `RIG_DESKTOP` sets the desktop name: `GNOME` (default) selects the GTK 4
xdg-desktop-portal-gnome backend through the host's `gnome-portals.conf`; for the GTK 3
backend write `~/.config/xdg-desktop-portal/<name>-portals.conf` holding
`[preferred]` / `default=gtk;` and start with `RIG_DESKTOP=<name>`. The rig bus has no
working document portal (the live session owns `/run/user/<uid>/doc`), so `flatpak run`
prints "Can't get document portal" and goes on. `drive` also takes `wheel N` (discrete
wheel steps, positive scrolls down) for lists such as that dropdown.

The sway rig is faster for anything that needs no GNOME specifics: `run.sh` with `KEEP=1`
leaves the container up and `scripts/dev/wayland-popups/run-patchy.sh` launches, scripts,
clicks (`vptr` now has `press`/`release` for drags) and captures the Flatpak on it. Both rigs
share the single-instance socket: stop one Patchy before starting the next, or the launch
only forwards to the running one. `live-drag.sh` in the same folder times a Move drag of a text layer on the sway rig, `live-canvas-drag.sh` a Path Select, path transform or Gradient Fill handle drag
(pointer lag reports). Every drag that does real work per move coalesces its moves: [the drag rule](interactive-previews-drag-coalescing.md#rule-for-new-drags).

## Measuring popup placement headlessly

`scripts/dev/wayland-popups/run.sh <scale> [menu|combo] [fix]` runs on the Linux build
host (podman plus the user-installed Flatpak SDK) and never touches a desktop session. It
builds `popup_probe` (a menu bar with Image > Mode and a combo box, linked with
`wayland_popup_anchor.cpp`; `fix` turns the workaround on) and a virtual pointer client
inside the manifest's `org.kde.Sdk`, starts a headless sway in a Debian 12 container on its
own socket in `$XDG_RUNTIME_DIR` (`--userns=keep-id`, so the socket belongs to the user),
clicks the target, and prints Qt's view of each popup, the anchors Qt sent
(`xdg_positioner.set_anchor_rect`) and the positions the compositor chose
(`xdg_popup.configure`, relative to the parent surface). Screenshots (grim) and the full
`WAYLAND_DEBUG` log land in `build/wayland-popups/`. `KEEP=1` keeps sway for the next run.

Why this setup:

- Qt makes a grabbing popup only after real input on the parent: Qt 6.8.3 otherwise maps
  menus as toplevels, and 6.11 logs "Failed to create grabbing popup". weston's headless
  backend has no seat, and wtype key presses did not count for 6.11. sway accepts a
  `zwlr_virtual_pointer_v1` device, which gives the seat a pointer and the click a serial;
  `vptr.c` keeps the device alive until its input ends.
- sway refuses to run as root (`--userns=keep-id` plus `setpriv`) and refuses an NVIDIA host
  without `--unsupported-gpu`; with no GPU in the container it renders with pixman.
- For a Qt 6.8.3 comparison, build the probe directory against `.deps/Qt/6.8.3/gcc_64` on
  the host and run it with `WAYLAND_DISPLAY` set to the sway socket (`build/wayland-popups/display`).
