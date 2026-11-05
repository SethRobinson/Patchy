# Wayland pointer lock for the brush-resize drag

The Alt+right-drag brush resize (GitHub issue 637) keeps the size disc on the press point
for a mouse and ends with the pointer back there, so the brush stays on the spot being
adjusted. On Windows, macOS and X11 `end_brush_adjust_drag` (src/ui/canvas_widget_brush.cpp)
moves the pointer back with `move_pointer_to_global_position` (`QCursor::setPos`, a
CoreGraphics warp on macOS). Wayland lets no client move the pointer, so before this the
pointer stayed where the drag ended and the next move snapped the brush there. Pens are
absolute and keep their own path: the disc is centered on the pen and nothing is locked.

## The lock

On a `wayland*` platform `begin_brush_adjust_drag` asks `lock_pointer_at`
(src/ui/pointer_lock.cpp) for a lock; on Linux that is `create_wayland_pointer_lock`
(src/ui/wayland_pointer_lock.cpp):

- `zwp_pointer_constraints_v1.lock_pointer` on the canvas window's `wl_surface`, lifetime
  oneshot, with `zwp_locked_pointer_v1.set_cursor_position_hint` at the press point in
  surface coordinates (widget position mapped to the window, plus Qt's decoration margins,
  times Qt's own scale factor). The hint is double-buffered; the drag's first repaint
  commits the surface. The pointer does not move while locked, so it is still on the press
  point at the end; KWin and Mutter also apply the hint when the client destroys the lock.
- A locked pointer sends no `wl_pointer.motion`, so the drag reads
  `zwp_relative_pointer_v1.relative_motion` (the accelerated `dx`, `dy`, divided by Qt's
  scale factor) and feeds the accumulated position into the existing size math
  (`brush_adjust_pointer_moved_relative`). The lock is used only when the compositor
  offers both globals.
- Mouse moves Qt delivers while the lock holds carry a stale position and are ignored.
  When the compositor ends the lock early (`unlocked`), the next absolute position
  re-anchors an offset so the size does not jump back.

Qt access: `QNativeInterface::QWaylandApplication` gives `wl_display` and the seat's
`wl_pointer`; `QPlatformNativeInterface::nativeResourceForWindow("surface")` gives the
surface, so `patchy_ui` links `Qt6::GuiPrivate` on Linux (Qt 6.10 and later ship it as the
separate `Qt6GuiPrivate` package; CMake defines `PATCHY_HAVE_WAYLAND_POINTER_LOCK` only when
the target exists). libwayland-client is never linked: `dlopen("libwayland-client.so.0",
RTLD_NOLOAD)` finds the copy Qt's Wayland plug-in loaded (an X11 session has none and gets no
lock), and the protocol tables are written out by hand from the wayland-protocols XML (MIT)
in the `wl_message`/`wl_interface` layout, so the build needs no wayland headers or scanner.
The globals are bound once on a private event queue; Qt's event thread files their events
into it and the session dispatches it on `QAbstractEventDispatcher::awake` and a 4 ms timer.

## Ending the lock

Destroying the session (`brush_adjust_pointer_lock_.reset()`) sends `destroy` and flushes.
`end_brush_adjust_drag` does it on every exit: right-button release, Escape (cancel), focus
loss, `hideEvent` (tab switch, window closing), a tool switch (`set_tool`), a document switch
(`set_document_internal`), a stale drag ended by the next press or move, and app quit (the
session is a canvas member). Callbacks never run after destruction starts.

## Fallback

When `lock_pointer_at` returns nothing and `platform_can_move_pointer()` is false (Wayland
without the protocols, or Qt without the surface), the mouse drag behaves like a pen's: the
disc follows the pointer and grows twice as fast, so the gesture ends with the brush under
the pointer at the size it shows. If a lock was created but never held (or ended early),
the brush is left where the pointer really is.

## Tests and checks

`brush_adjust_pointer_lock_tests.cpp` drives the drag offscreen with a fake lock factory
(`set_pointer_lock_factory_for_tests`, `set_platform_can_move_pointer_for_tests`): relative
motion gives the same size as the absolute drag, stale absolute moves are ignored, the lock
ends on every exit path, a lost lock continues without a jump, and both fallbacks. Real
compositors: the GNOME rig ([wayland.md](wayland.md), "Reproducing on a headless GNOME
Shell") and the KWin rig (`scripts/dev/wayland-kde/kwin-rig.sh`,
[linux-testing.md](linux-testing.md)), each driven with `kcdown 56`, `rpress`, `rel`,
`rrelease` and read with `pointer`: with the lock the pointer stays on the press point for
the whole drag. A `linux-flatpak-qt` build runs on the runtime's Qt through `flatpak run
--command=<build>/patchy org.kde.Sdk//<version>`; a `-NativeQt` (`linux-release`) build is
the AppImage's Qt 6.8.3. X11 (Xvfb plus xdotool) keeps the old path: no lock, the pointer
moves during the drag and `QCursor::setPos` puts it back.

Which setups had the bug (checked November 2026): Qt 6.10 and later move the pointer with
the `wp_pointer_warp_v1` protocol where the compositor offers it, so the Flatpak (Qt 6.11)
on KWin 6.6 already returned the pointer; the AppImage (Qt 6.8.3) on KWin, and both packages
on Mutter 46 (no pointer-warp), left it where the drag ended. The lock fixes all of them.
