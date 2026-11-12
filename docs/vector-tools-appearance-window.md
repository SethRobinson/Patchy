# The stay-open Shape Appearance window

Split from [vector-tools-appearance.md](vector-tools-appearance.md), which holds the rows themselves (paint, stroke, geometry, Edge), the modal dialog and its worker-thread preview.

## The stay-open window

Every entry point calls `MainWindow::edit_active_shape_appearance()`, which shows the Shape
Appearance window (GitHub issue 694): `ShapeAppearancePanel` (`src/ui/shape_appearance_panel.*`)
owning one `BrushToolWindow` named `shapeAppearanceWindow`. It is a floating tool window with no
nested event loop and no edit lock, so the document stays editable while it is open: draw,
select, undo, switch or close documents. Its rows are a page (`shapeAppearanceBody`) built by
`build_shape_appearance_body` in `Live` mode: Reset and Close, no OK, Cancel or Preview.
`edit_active_shape_appearance_modal` keeps the modal dialog (`shapeAppearanceDialog`, the same
rows in `Modal` mode, under the preview-dialog edit lock) for New Fill Layer, the options bar's
color and pattern pickers, and the dialogs the window's own rows open.

Rules, each pinned in `tests/ui/shape_appearance_panel_tests.cpp`:

- **No pointers between events.** The panel keeps a session id and layer ids
  (`ShapeAppearanceTarget`), and the host resolves them again on every use. A run keeps copies
  of the target layers, never pointers.
- **Following the selection.** `sync()` runs from `refresh_layer_controls`,
  `refresh_options_bar` and when the edit lock is taken or released. The target is
  `editable_selected_shape_layer_ids()`. The page key is the session id, the ids, each layer's
  render and content revision, the selected count and the field units; a changed key builds a
  new page from the layers (the rows depend on the target: gradient rows, a line's rows, the
  batch summary, the Fill Layer Settings title), keeps the scroll position, and grows the window
  only when the page needs it. Fields are never refilled in place.
- **A page is never deleted under its own handler.** The rebuild is deferred to the event loop
  and waits (`rebuild_blocked`) while the page is applying a change, while a dialog a row opened
  is running, and while any modal window, popup or child dialog of the window is up.
  `BrushToolWindow::replace_body` hides the old page, takes it out of the window and deletes it
  later.
- **Unavailable.** With no document, another dialog holding the edit lock, a Free Transform,
  Warp or path transform running, or no editable shape layer selected, the page stays but is
  disabled under a one-line reason, and `apply_live_shape_appearance_edit` applies nothing. It
  also refuses a change from a page built for other layers.
- **One undo step per gesture.** The first value of a gesture goes through
  `commit_shape_appearance_edit` (the options bar's path: the undo step "Shape appearance" is
  pushed before the first write) and opens a `ShapeAppearanceLiveRun`. Later values of the same
  property rewrite the same layers from the run's copies (`write_live_shape_appearance_step`),
  with no new step. The run ends on another property, after 600 ms without a value, when a
  target's revisions show something else changed it, and in
  `finish_pending_shape_appearance_edit()`, which every history, selection, document and tab
  transition already calls; the edit lock and the first preview of a gradient handle drag end it
  too. The document never holds a change that is not in history, so there is nothing to restore.
- **Each run carries its own edits.** When a run ends the page is rebased
  (`ShapeAppearanceBody::rebase`): the edits captured so far are dropped and later ones are
  captured against the layers as they are now, so a run replays only its own operations.
- **Bake cost.** A run's values are baked on the UI thread. A large full-canvas gradient fill
  takes the draft bake while the run lasts (`update_gradient_fill_draft_raster`, as the Gradient
  tool's handle drags do) and the run's end bakes the last value exactly. Measured November
  2026 on an i9-12900KS at 4000 x 3000: 24 ms per value on a Gradient Fill layer, 90 ms on a
  gradient shape with a path (an exact bake; the modal dialog bakes on a worker).
  `ui_shape_appearance_window_step_timing` prints both.
- **Dialogs the rows open** (the color pickers, the Gradient Manager, the Shapes Manager) have
  their own OK and Cancel, so they run through `run_shape_appearance_panel_child_dialog` as the
  modal edit does: the document locked, changes as previews, one undo step on OK, the layers put
  back on Cancel. The lock is also what keeps the selection, and with it the page, in place
  under the row's handler.
- **Window.** Keys no field takes go to the canvas and the main window's shortcuts work while
  it is active (`BrushToolWindow`). The main window's close hides it; it does not come back at
  the next launch. Under automation (`unattended_automation`) it does not open.
