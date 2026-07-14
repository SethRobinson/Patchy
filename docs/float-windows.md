# Float windows: documents can leave the tab bar

Window > Float in Window (also on the tab context menu) moves a document's canvas out of
the tab widget into a `DocumentFloatWindow` (src/ui/document_float_window.{hpp,cpp}): a
MainWindow-parented `Qt::Window` with a native frame, so two documents can be visible at
once (a second monitor works) while every panel stays in the main window and follows the
focused document. Window > Dock to Tabs and Window > Consolidate All to Tabs reverse it
(the tab returns at its remembered index, clamped); closing a float closes that document
with the usual save prompt, and Cancel keeps both the window and the document. Hotkey ids
(persisted, never rename): `window.float_document`, `window.dock_document`,
`window.consolidate_all_to_tabs`, `window.float_all`, `window.tile_windows`,
`window.cascade_windows`, all without default shortcuts.

## Gestures and arrangement

- **Tear-off**: dragging a tab 24+ px above or below the tab bar floats its document at
  the cursor and hands the drag to `startSystemMove` so the window keeps following in
  the same motion (horizontal dragging stays QTabBar's reorder). Implemented in the
  MainWindow event filter on the tab bar; `tear_off_document_tab` first sends the bar a
  synthetic release so QTabBar's internal move-drag ends before its tab vanishes.
- **Drag to dock**: dropping a float on the tab-bar strip docks it.
  `DocumentFloatWindow::moveEvent` notifies `handle_float_window_drag_moved`, which arms
  a 150 ms settle timer ONLY while the left button is held (so programmatic moves from
  creation/Tile/Cascade never dock anything); when the moves stop and the button is up,
  `maybe_dock_float_at(QCursor::pos())` checks `float_dock_zone_global()` (the tab bar's
  strip, or the tab widget's top strip when no tabs remain). The candidate is tracked by
  session id, never a window pointer. While the drag hovers the zone, a translucent
  palette-Highlight overlay (`floatDockHighlight`, mouse-transparent, lazily created)
  lights the strip so the user can see that releasing will dock.
- **Window > Float All in Windows / Tile / Cascade**: Photoshop's arrange semantics.
  Tile and Cascade first float every document, then lay the float windows out over the
  DOCUMENT WORKSPACE (`document_workspace_global()`: the tab-widget area, so the tool
  palette, options bar, and panels stay visible; falls back to the screen's work area
  when the workspace is degenerate or the window is minimized) as a near-square grid /
  36 px staggered stack at 60% size. New floats also spawn cascaded from the workspace's
  corner. The active document's window is raised last and stays active.
  `set_frame_geometry` compensates for native frame margins so tiled windows do not
  overlap their title bars (offscreen reports no frame and degrades to plain
  setGeometry).

## Invariants

- `canvas_` is the single source of truth for the ACTIVE document: `session()` /
  `document()` / `has_active_document()` resolve through it, and every activation source
  (tab `currentChanged`, `tabBarClicked` on the already-current tab, float
  `WindowActivate`, canvas `FocusIn` in the app-level event filter) funnels through
  `activate_document_canvas`, which settles any open inline text edit BEFORE reassigning
  `canvas_` (the commit rasterizes into the active session). Never derive the active
  document from the tab widget's current index: a floated document has no tab. Clicking
  a dock panel or the main window title bar deliberately does NOT move activation away
  from a float (Photoshop behavior: panels edit the focused document wherever it lives).
- Canvas history callbacks (`set_before_edit_callback`, the selection-history callback)
  resolve `session_for_canvas(canvas)` at fire time, so an edit or an async completion
  lands in the OWNING session's undo stack regardless of which document is active, and
  the undo selection snapshot is captured from the owning canvas. UI-refresh callbacks
  gate on `canvas == canvas_`. A new pixel-mutating canvas callback must follow the same
  split (route data by owning session, gate UI on the active canvas). Inside the
  session-targeted `push_undo_snapshot`/`push_selection_history`, the History panel,
  status-bar label, and the pending layer-opacity flush run only when the target IS the
  active session; the shared History panel mirrors the active document alone.
- Close paths are session-based: `close_document_session` is the real close (Close All /
  Close Others iterate `sessions_`; the smart-object child recursion resolves children by
  session id, since a floated child has no tab index and closing one child can
  recursively close another). `close_document_tab(int)` survives as a thin wrapper for
  the stress test and `MainWindowTestAccess`. After a close, `fallback_active_canvas()`
  keeps the invariant "`canvas_ == nullptr` iff no sessions" (current tab first, then the
  most recent float), and closing a BACKGROUND document never moves activation.
- The preview-dialog edit lock stores the locked CANVAS
  (`preview_dialog_edit_lock_canvas_`, a QPointer, not a tab index) and edit-locks EVERY
  session canvas: a float stays clickable while a Levels-style dialog is open, so
  activation snap-back alone would not stop its first click from editing. Activating any
  other canvas while locked snaps back (raising the locked float when the locked
  document is floated) and shows the standard message. Sessions created while locked
  (drag & drop, second-instance open) join the lock in `add_document_session`, which
  also skips stealing activation from the locked document.
- Hotkeys work inside floats because float creation `addAction`s every
  `hotkey_registry_.commands()` action onto the window: Qt's WindowShortcut context
  matches when any associated widget lives in the active window. Do NOT switch actions
  to ApplicationShortcut instead; that leaks document shortcuts into modal dialogs.
  The snapshot is complete because every `register_hotkey` call happens during
  MainWindow construction; a feature that registers hotkeys later must also add its
  action to every existing float (or move the association into HotkeyRegistry).
- `MainWindow::closeEvent` hides all floats after the session confirm loop: a visible
  owned top-level would block `lastWindowClosed` (the tile-preview hazard). The float is
  released with `hide()` + `deleteLater()`, never a synchronous delete (it may be inside
  its own `closeEvent`), and its canvas is detached with `take_canvas()` first.
- Quit stays one atomic decision: during OS session end (`QGuiApplication::
  isSavingSession()`, Qt's default handling delivers closeEvent to every top-level) a
  float accepts WITHOUT closing its document, the main window's confirm loop owns every
  save prompt, and a cancelled close re-shows the floats the shutdown already hid.
- `--screenshot` captures the main window only; floated documents are not in the grab.

## Deliberately out of scope

Float geometry persistence across restarts (floats always start tabbed on relaunch) and
a second view of the same document (selection and overlays live per-canvas).

Coverage: the `ui_float_*`, `ui_window_float_all_*`, and `ui_tab_drag_out_*` tests in
tests/ui/float_window_tests.cpp.
