# Wayland

Patchy behavior that differs on a Wayland session (Qt's `wayland` platform plug-in). X11
(`xcb`), Windows, macOS and the offscreen test platform take none of these paths, except
the frameless window chrome below, which is the Linux rule for X11 too. The right panels
never float on any platform (the column is Patchy's own widget, [right-docks.md](right-docks.md)),
so the Wayland dock-drag problems of GitHub issue 102 no longer exist; the KWin and GNOME
test rigs are in [testing.md](testing.md).

## The frameless window chrome (Linux, X11 and Wayland)

Linux builds (the native `linux-release` binary, the AppImage and the Flatpak) draw the
Windows chrome: `MainWindow::use_custom_window_chrome()` is true, the main window carries
`Qt::FramelessWindowHint`, and the menu-bar row holds the badge, the blank title-bar area
and the minimize/maximize/close controls (`configure_window_chrome`,
`main_window_chrome.cpp`). The frameless flag is what removes Qt's own client-side
decoration: mutter offers no server-side decorations, so a decorated Qt window on GNOME
got the stock `bradient` title bar, light and ignoring Patchy's theme. The window has no
compositor shadow; its silhouette is the 1px `window_border` edge
([platform.md](platform.md)). macOS keeps the native frame and global menu bar.

Everything the Win32 `WM_NCHITTEST` answers on Windows goes through Qt's platform-neutral
calls here, in `MainWindow::eventFilter` and `handle_window_resize_event`:

- **Drag.** A left press on the blank menu-bar area (`title_bar_drag_area_contains`: not a
  menu title, not the controls, not a resize border) only records the press. Once the
  pointer travels `QApplication::startDragDistance()` the window hands itself to the
  window system with `QWindow::startSystemMove()`: `xdg_toplevel.move` on Wayland
  (mutter and KWin accept the press serial while the button stays down, so starting from
  the motion event is fine), `_NET_WM_MOVERESIZE` on X11 (Qt's xcb plug-in sends it at
  the next motion anyway). The compositor then owns the gesture: a maximized window
  restores under the cursor, drag-to-top maximizes, and GNOME's edge tiling works. Never
  start the move from the press: a compositor grab would span every plain click, and the
  second press of a double-click would maximize a window the compositor was still
  dragging. Where `startSystemMove()` returns false (offscreen, a window manager without
  `_NET_WM_MOVERESIZE`) Qt moves the window itself, anchored at the press.
- **Double-click** on the blank area toggles maximize; a plain click changes nothing.
- **Resize.** The 10 px inner border (`kWindowResizeBorder`) shows the resize cursor and
  a press there calls `QWindow::startSystemResize(edges)` (`xdg_toplevel.resize`,
  `_NET_WM_MOVERESIZE` with the edge); the compositor keeps the minimum size Qt publishes.
  A maximized window has no resize border. The Qt-level geometry resize remains the
  fallback where the call returns false. The resize cursor is an application override
  cursor, cleared on `QEvent::Leave` so the compositor's own grab never leaves a resize
  arrow over Patchy's other windows.
- **Controls.** Minimize is `showMinimized()`, maximize/restore `showMaximized()` /
  `showNormal()`, close `close()`. The restore is asynchronous (the compositor's configure
  brings the saved size back), so `restore_window_from_maximize` sets no geometry on Linux;
  the Windows branch keeps its WS_MAXIMIZE workaround.

**Use the system title bar** (Preferences > Application, Linux builds only; GitHub issue
145) stores `window/systemTitleBar` (default false; a compatibility contract, helpers in
`app_settings.hpp`). `use_custom_window_chrome()` reads it once, on its first call, before
the window is built, so a change applies on the next launch and the accept branch shows the
usual restart notice (`preferencesSystemTitleBarMessageBox`). With it on, Linux behaves as
it did before the frameless chrome: Qt's client-side decoration on GNOME Wayland, the
server-side title bar on X11 and KDE, native move and resize, no badge or controls in the
menu bar, the blank menu-bar drag still a `startSystemMove`, and the menus in a global menu
bar where the desktop has one. The badge's 35 px of menu-bar padding keys on the
`patchyBadgeChrome` property `configure_window_chrome()` sets, so this menu bar starts its
first menu at the edge (GitHub issue 196, `ui_menu_bar_reserves_badge_space_only_with_custom_chrome`).
The key is stored but inert on Windows and macOS.

**Global menu desktops** (GitHub issue 161): the frameless chrome calls
`setNativeMenuBar(false)`, which keeps the menus in the window even where KDE's Global
Menu, elementary or a Unity-style panel would take them. So until the user writes the key
(`stored_system_title_bar_choice()` is nullopt), the default is the system title bar
exactly where `MainWindow::desktop_has_global_menu_bar()` is true. That probe builds a
throwaway `QMenuBar` before the window and reads `isNativeMenuBar()`: Qt creates a
platform menu bar only when its platform theme would export the menus (the generic theme
checks for `com.canonical.AppMenu.Registrar` on the session bus, the KDE theme its own
way), so Patchy decides the same thing Qt does. Offscreen it is false. The Preferences
checkbox shows the effective state (`system_title_bar_effective()`) and writes the key only
when the user changes it, so an untouched install keeps following the desktop.

`ui_system_title_bar_preference_disables_custom_chrome`,
`ui_system_title_bar_preference_defaults_off`,
`ui_system_title_bar_follows_global_menu_bar_until_set`,
`ui_preferences_system_title_bar_checkbox_persists_key` and
`ui_preferences_system_title_bar_checkbox_shows_desktop_default` cover it (the gate cache
resets through `MainWindowTestAccess::reset_window_chrome_gate_cache`, and
`set_global_menu_bar_probe` stands in for the desktop).

The maximize-after-first-show rule below (issue 132) is unchanged in both modes: a restored
maximized state is still requested from `showEvent`'s first-show turn, and
`available_window_height` still keeps `kWaylandShellPanelAllowance` free for the panel. Offscreen, every system call
returns false, so `ui_window_chrome_controls_drive_window_state`,
`ui_title_bar_drag_moves_window_only_past_threshold`,
`ui_maximized_title_bar_click_keeps_window_maximized` and
`ui_frameless_window_edges_resize` pin the Qt-level contract on Windows and Linux alike.

Manual check on the Linux build host's live GNOME session: build the native binary
(`remote-build.ps1 -Target linux -NativeQt -SkipTests`), launch it from the work tree
into that session (`XDG_RUNTIME_DIR`, `WAYLAND_DISPLAY`, `DBUS_SESSION_BUS_ADDRESS` of the
logged-in user, `QT_QPA_PLATFORM=wayland`) and confirm: no light title bar, the controls
in the menu-bar row, a blank-area drag that moves and tiles, a double-click that
maximizes and restores, edge resizing, and a maximize button restore that returns to the
saved size. `patchy --screenshot` captures the window itself for the record; `QT_QPA_PLATFORM=xcb`
covers the X11 path on the same session. The GNOME rig below drives the same native binary
headlessly (launch it with the rig's env, `PATCHY_NO_SINGLE_INSTANCE=1` and its own
`PATCHY_SETTINGS_DIR`; read mutter's `get_frame_rect()`/`get_maximized()` through
`Shell.Eval`). Edge tiling there follows the user's `org.gnome.mutter edge-tiling` gsetting
(off on the build host's user): switch it on for the run and restore it after. Mutter shakes
a maximized window loose only with vertical motion, and a tiled one with either axis.

## Popup anchors at a non-100% interface scale (GitHub issue 100)

The interface scale is `QT_SCALE_FACTOR` (`apply_gui_scale_factor` in `src/app/main.cpp`).
On Wayland Qt cannot place a popup itself: it sends the compositor an `xdg_positioner`
whose anchor rectangle is in the parent surface's pixels, which at a scale factor are the
native pixels, not Qt's device-independent ones.

Qt 6.11 added semantic popup placement (qtbase "widgets: Pass popup semantic information
to Wayland", QTBUG-135883): `QMenuPrivate::popup` and `QComboBox::showPopup` hand the
plug-in a control rectangle through the private `setParentControlGeometry`, and
`QWaylandXdgSurface::createPositioner` uses it as the anchor rectangle. The rectangle is in
widget coordinates and nothing converts it, so every anchored popup is off by the scale
factor. Still unconverted in qtbase `dev` in October 2026, and no matching Qt bug report
was found. Affected:

| Popup | Anchor Qt sends | Symptom at 125% / 90% |
|---|---|---|
| Menu bar menu | the menu bar item | opens left of the item and over its bottom edge / right of the item, a gap below |
| Submenu (Image > Mode, File > Open Recent File) | the parent item, anchored at its right edge | overlaps the parent menu / a gap between the menus |
| QPushButton menu (the layers panel's adjustment button) | the button | like the menu bar case |
| Combo box list | the combo box | covers the combo box / a gap below it |

Unaffected: context menus (`QMenu::popup`/`exec` at a point) and QToolButton menus, which
set no control rectangle; Qt 6.8.3 (the AppImage, Windows, macOS), whose anchor is the
popup's own native position; X11.

Measured on Qt 6.11.2 with the probe below, 125%, a 159 px wide Image menu: the Mode
submenu opened at x=127 of its parent (should be 159) and the Image menu at (41, 19)
instead of (51, 24). At 90%, a 114 px menu: submenu at 127 (should be 114). A combo box
whose bottom edge is at y=104 got its list at y=83 at 125%. Patchy 1.11 (the Flatpak) at
125%: Image > Mode at x=332 of a 416 px Image menu; at 90%, x=332 of a 300 px menu. With
the workaround the probe matches the X11 placement and Patchy opens Mode at the menu's
right edge (415 and 299).

### The workaround

`install_wayland_popup_anchor_fix()` (`src/ui/wayland_popup_anchor.cpp`, called from
`main()`) installs an application event filter when `wayland_popup_anchor_fix_needed()`
holds: a `wayland*` platform, Qt 6.11.x, and `QT_SCALE_FACTOR` other than 1. On each popup's
Show event, which arrives before the platform window becomes visible and the plug-in builds
the positioner, `wayland_popup_control_rect()` recomputes the rectangle Qt sent (same
cases and order as `QMenuPrivate::popup`: a pressed QPushButton showing the menu, then a
visible parent menu whose active action is this menu, then the menu bar's active item;
combo lists through `QStyle::SC_ComboBoxListBoxPopup`) and the filter stores it, scaled,
in the window property `_q_waylandPopupAnchorRect`. The plug-in reads that property after
the control rectangle and uses it as is; anchor, gravity and constraint adjustments still
come from Qt's popup type. A popup Qt anchors to nothing gets the property cleared, so a
menu reopened as a context menu does not keep a stale anchor.

Keep the version gate exact. If a later Qt converts the control rectangle, the property is
still right, but if it starts converting the property, a gate that included it would
scale twice. When the Flatpak runtime moves past 6.11, run the probe without `fix`: if Qt
still places popups off by the scale, extend the gate to that version; otherwise leave it.

`ui_wayland_popup_anchor_fix_scales_qt_anchor_rects` drives the filter offscreen (menu bar
menu, submenu, the same menu as a context menu, combo list, QPushButton menu) and checks
the property against the rectangles Qt computes. Offscreen cannot show the compositor's
placement; the probe does.

## A tool cursor over dialogs (GitHub issue 215)

Before Qt 6.10, `QWaylandCursor::changeCursor` ignores the window whose cursor changed and
sets the cursor on every seat's pointer, whatever surface the pointer is over (qtwayland
6.8.3 `src/client/qwaylandcursor.cpp`; 6.10 stores the cursor per window and applies it
only to the window under the pointer). A dialog that opens under the pointer gets the arrow
on `wl_pointer.enter`, but the canvas behind it keeps refreshing its tool cursor (the zoom
fit after a document opens, a modifier key, a brush size change), and each refresh put the
brush or crosshair cursor over the dialog. Qt applies a widget's cursor to its window only
when it changes, so the wrong cursor stayed until the pointer crossed a widget with a
cursor of its own. The AppImage (Qt 6.8.3) and source builds on Qt 6.8 or 6.9 have it; the
Flatpak (Qt 6.11), X11, Windows and macOS apply cursors per window.

`install_wayland_pointer_cursor_fix()` (`src/ui/wayland_pointer_cursor.cpp`, called from
`main()`) installs an application event filter when `wayland_pointer_cursor_fix_needed()`
holds: a `wayland*` platform and Qt before 6.10. It tracks the widget under the pointer
from the Enter and Leave events Qt sends as the pointer crosses widgets and windows. After
a `CursorChange` on a created widget in another native window, it re-applies the cursor
Qt shows over the pointer's widget (its own or its nearest ancestor's, the arrow when
disabled) to the pointer's window, unsetting it first because `QWindow::setCursor` skips a
shape the window already holds. It does nothing while an application override cursor is
set, which shows everywhere by design. The fix covers every window, modal or not, and the
reverse case of a dialog widget changing its cursor while the pointer is on the canvas.

`ui_wayland_pointer_cursor_fix_keeps_the_dialog_cursor` drives the filter offscreen with
the Enter and Leave events Qt sends and checks the cursor each window holds, which is what
the plug-in would show; `ui_wayland_pointer_cursor_fix_covers_preferences_and_missing_fonts`
does the same over the real modal dialogs while the canvas changes its brush cursor.
Offscreen cannot show the compositor's cursor.

## A window taller than the screen (GitHub issue 108)

Qt's `QScreen::availableGeometry` on Wayland is the whole output: no protocol reports a
panel, and an app cannot move its window either, so `clamp_window_to_available_screen`
could neither shrink the window below its minimum height nor nudge it onto the screen. On
a 1366x768 laptop at a 125% scale the main window's minimum (the right column's 300 px
Layers floor plus the collapsed strips, two options-bar rows, menu and status bars) was
taller than the screen: the column's bottom panels sat under GNOME's top bar and off the
bottom edge, and expanding Properties or Info demanded more height and grew the window
further out of view, which the reporter saw as cropped panels.

`MainWindow::available_window_height` is the client height that keeps the whole frame on
the screen: the available height less the decorations outside the client area (Qt's own
client-side title bar on GNOME, known only once the window is shown, hence the deferred
first-show clamp on Wayland in `showEvent`) less `kWaylandShellPanelAllowance` (48 logical
px) for the panel the compositor never reports. `fit_right_dock_floors_to_screen` lowers
the expanded sections' floors (later sections first, the Layers group last, then the
Layers panel's own 240 px and the list's 120 px) until the window's minimum fits that
budget; `clamp_window_to_available_screen` and every expand or visibility change call it,
so an expand takes room from the column while the column has any to give. Once every
panel is open, the content minimums alone (Info's four lines, the Properties scroll area)
can still exceed a 1366x768 screen at 125%, and then the window grows to that irreducible
minimum. Platform-neutral (a short Windows screen gets the same floors); only the panel
allowance is Wayland-specific.
`ui_right_dock_floors_fit_the_screen_height` covers it on the offscreen platform's 600 px
screen. See [right-docks.md](right-docks.md).

## A restored maximized window (GitHub issue 132)

`restore_window_geometry` used to set `Qt::WindowMaximized` before the first show. On
Wayland the compositor can drop a maximize requested before the first configure (GNOME did,
in the AppImage): the window came up at its normal size while Qt still reported it maximized
and its decoration showed the restore glyph, and the first-show clamp skipped it as
"maximized". Now the restore only records `wayland_maximize_on_first_show_` on Wayland and
`showEvent`'s first-show turn calls `showMaximized()` once the surface exists; other platforms
keep the pre-show state. Verify on the GNOME rig: seed `window/normalGeometry` and
`window/maximized=true` in the rig's settings file, launch, and read
`meta_window.get_maximized()` plus `get_frame_rect()` through `Shell.Eval`.

## Pick Screen Color (GitHub issue 128)

The color picker's "Pick Screen Color" cannot work the X11 way on Wayland: a compositor
neither lets Patchy's transparent overlay cover other applications' windows nor answers
`QScreen::grabWindow`. On a `wayland*` platform `start_screen_pick` (color_panel.cpp)
calls `org.freedesktop.portal.Screenshot.PickColor` through
`src/ui/portal_color_pick.cpp` (QtDBus, `PATCHY_HAVE_QT_DBUS`; the overlay remains the
fallback when the bus is unreachable). The portal runs the compositor's own picker
(GNOME's magnifier) and answers on `org.freedesktop.portal.Request.Response` at the path
`portal_request_path` derives from the bus name and `handle_token`; the subscription is
made before the call so an early answer is not lost. The result is sRGB `(ddd)`. The
canvas eyedropper's off-document sample (`screen_color_at_global_position`) still reads
nothing on Wayland: that is a drag, and the portal picker is a one-shot modal flow.
Manual check: on GNOME or KDE Wayland open the color picker, press Pick Screen Color,
pick over another application's window; the swatch takes that color. Cancelling with
Escape leaves the color unchanged.

## The portal Open dialog's type list (GitHub issue 270)

Inside the Flatpak, and in the AppImage through `QT_QPA_PLATFORMTHEME=xdgdesktopportal`,
File > Open is the desktop's portal dialog. On GNOME that is xdg-desktop-portal-gnome's
GTK 4 dialog, whose type dropdown is a popover capped at 400 px (GTK's `gtkdropdown.ui`,
`max-content-height`): about ten rows, scrolled by wheel or touchpad, with an overlay
scrollbar visible only while the pointer moves over the list. Every row Patchy sends is
there (rig captures `build/issue-shots/270`); the GTK 3 backend (xdg-desktop-portal-gtk),
KDE's dialog and Qt's widget dialog list all of them. Qt's portal helper
(`qxdgdesktopportalfiledialog.cpp`) sends every row whose spec matches
`QPlatformFileDialogHelper::filterRegExp` and xdg-desktop-portal validates the list whole,
so nothing drops rows. Before the hint cap the Camera Raw row's 27 patterns made the
popover as wide as the screen, with the scrollbar at the far screen edge. Patchy keeps
the popover narrow by capping each row's visible pattern hint
([file-formats.md](file-formats.md), "Open-dialog filter contract"); it cannot change the
cap or the scrollbar.

## Reproducing on a headless GNOME Shell

`scripts/dev/wayland-gnome/gnome-rig.sh` runs the installed Flatpak on a headless mutter
(`gnome-shell --headless --wayland --no-x11 --unsafe-mode --virtual-monitor <MON>`, its own
session bus, env in `build/gnome-rig/env`), the setup the issue 100 and 108 reporter has (Qt
draws its own title bar there). `start-shell` (with `MON=1366x768` for that laptop), then
`patchy <scale>` launches the Flatpak as the single instance, `script <file.js>` forwards a
`--run-script` to it (the script file must live under `$HOME`: the sandbox hides `/tmp`),
and `drive` reads pointer commands on stdin (`gnome-input.py`: `move X Y` in output pixels,
`rel DX DY` for the motion of a drag, `press`, `release`, `click`, `sleep MS`, `shot NAME`
through `org.gnome.Shell.Screenshot`).
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
only forwards to the running one.

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
