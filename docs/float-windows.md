# Float windows: documents can leave the tab bar

Window > Float in Window (also on the tab context menu) moves a document's canvas out of
the tab widget into a `DocumentFloatWindow` (src/ui/document_float_window.{hpp,cpp}): a
MainWindow-parented `Qt::Window` with a native frame, so two documents can be visible at
once (a second monitor works) while every panel stays in the main window and follows the
focused document. Window > Dock to Tabs and Window > Consolidate All to Tabs reverse it
(the tab returns at its remembered index, clamped); closing a float closes that document
with the usual save prompt, and Cancel keeps both the window and the document. Hotkey ids
(persisted, never rename): `window.float_document`, `window.dock_document`,
`window.consolidate_all_to_tabs`, all without default shortcuts.

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

## Deliberately out of scope (v1)

Drag-a-tab-outward tear-off gesture, dragging a float back onto the tab bar, float
geometry persistence across restarts, Tile/Cascade arrangement commands, and a second
view of the same document (selection and overlays live per-canvas).

Coverage: the `ui_float_*` tests in tests/ui_visual_tests.cpp.
