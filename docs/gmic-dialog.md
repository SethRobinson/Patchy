# G'MIC Filter Library dialog

Split from [gmic.md](gmic.md) (vendoring, build, code, allowlist, tests). Dialog conventions: [ui-conventions.md](ui-conventions.md).

Filter > "G'MIC Filter Library..." (`filterGmicLibraryAction`, hotkey id `filter.gmic_library`,
a document action, created only when `gmic::engine_available()`).

- `src/ui/gmic_library.{hpp,cpp}`: `gmic_library()` loads the embedded text once and keeps the
  allowlisted catalog.
- `src/ui/gmic_parameter_panel.{hpp,cpp}`: `GmicParameterPanel::set_filter` generates one form
  row per declared parameter. Float and int are a slider with a fixed-width spin box (float
  decimals follow the range and are never coarser than the declaration writes its numbers),
  choice a combo, bool a check box, color a swatch through `request_patchy_color` (each pick previews live, picker Cancel restores) plus an
  Alpha spin when declared, text a line or multi-line edit, point two percent fields, button
  a one-shot flag (1 for the run after its press; `release_buttons` puts it back once a
  run has read it, and `gmic::release_button_values` does the same for remembered values),
  value hidden. Notes are rich text (`gmic_note_rich_text` turns Pango
  `<span color=...>` and the literal `\n` into Qt markup), links open externally, separators
  are a themed rule. `values()` returns one G'MIC argument per value-carrying parameter.
  Parameter names, choices and notes are G'MIC's English text, deliberately untranslated;
  Patchy's own labels around them are translated.
- `src/ui/gmic_filter_library_dialog.{hpp,cpp}`: search field (every word must match the name
  or a folder; Enter picks the first match, and `keyPressEvent` keeps that Enter from the
  default OK button), folder tree in library order, the panel in a scroll area, Reset, Preview,
  About G'MIC, OK and Cancel, plus an Input layers combo (tooltip explains the layer rules)
  that starts from the filter's declared `input_mode` and is part of the `GmicFilterChoice`.
  About G'MIC (`gmicAboutDialog`) credits the GREYC team, the pronunciation (the author says
  "gimmick" and accepts any, on discuss.pixls.us), the license and gmic.eu. It never runs the engine: it reports a
  `GmicPreviewRequest` (the chosen filter, values and input layers, or none, and whether
  Preview is checked) through `set_preview_callback` whenever
  the filter, a value or the Preview box changes. Slider drags are coalesced
  (`CoalescedPreviewEmitter`); a parameter declared `_typedef` does not request a preview.
  `apply_status` feeds a run's returned status back into the panel (`{v1}{v2}...` with optional
  `_0/_1/_2` visibility per value), and `set_preview_message` shows a failed preview's reason
  under the filter name.
- `src/ui/main_window_gmic.cpp`. Every engine run, pixel conversion included, happens off
  the UI thread; nothing proportional to the layer runs there except the final buffer swap.
  - `render_gmic_filter` (file-local, thread-safe, translates nothing) is the whole run from a
    `GmicRunInput`: layers through `load_rgba_row` in the Encoded domain (every bit depth,
    0..255 floats), the engine, outputs back in layer formats (1, 2, 3 or 4 channels; no
    alpha means opaque).
  - **Layers (decided October 2026): a filter changes every chosen layer it may change,
    like G'MIC-Qt's "In place" output.** The input mode (`.` active, `*` all, `+` active and
    the one below, `-` active and the one above, `v` visible, `i` hidden) picks layers from
    `gmic_layer_snapshots`: top of the stack first, groups opened, adjustment layers skipped,
    "visible" meaning the layer's eye and every enclosing group's. A layer is writable
    (`gmic_layer_writable`) with editable RGB pixels and no pixel lock; text, shape and Smart
    Object layers are read as their pixels and never changed. No layer chosen: "No layer
    matches the chosen input layers"; none writable: refused too. How a run reads them
    (`gmic_reads_layers_together`):
    - **A filter made for one image** (declared input `.`; Sepia, CRT Scanlines) runs once
      per writable chosen layer, alone at its own size (`render_gmic_each_layer`). Its first
      image replaces that layer, the others become new layers. Never pad these onto a
      document-sized image: about 140 fill transparency, so a small layer came back
      canvas-sized.
    - **A filter made for several layers** (declared `*`, `+` and so on: Blend, Morph, Align
      Layers) reading more than one gets them lined up (`render_gmic_lined_up`): each on a
      transparent document-sized image, results cropped to content (`trim_to_content`).
      G'MIC's merge filters resize inputs to the largest and ignore `pos()`, so they need
      equal frames. K read, M returned: M >= K, each writable layer takes the image in its
      place; M < K, the first image replaces the active layer (`merged`); other images become
      new layers (anchored on the lowest read layer, or the active one).
    - **Stacking.** G'MIC lists images top of the stack first (Ball moves its sphere to
      `[0]` over the original), so `insert_gmic_new_layers` puts new layers below their
      anchor, each below the last. A Background stays at the bottom (Photoshop's rule; the PSD
      reader takes a top full-canvas Background as a reversed legacy file): it takes the LAST
      image and the rest go above it in that order (Ball's sphere on top); no new layer is
      named Background.
    - Output names carry layer settings (`gmic::parse_output_layer_name`):
      `mode(...)`, `opacity(...)`, `pos(x,y)`, `name(...)`. New layers take all four
      (`blend_mode_from_gmic` maps G'MIC's blend names; unknown ones, such as grainmerge,
      become Normal); a replaced layer takes only the position, and gains an alpha channel
      when it has none and the result has a transparent pixel (Ball on a JPEG layer was
      black around the sphere; `original_needs_alpha_for`). An output with no `pos` that
      differs in size from what it replaces is centred on it (a frame grows evenly; CRT
      Scanlines multiplies the size by its Upscale Factor, 4 by default, past the canvas).
    - The selection applies to each replaced layer in its own frame (outside restored, edge
      blended by coverage). A result of another size (CRT Phosphors, CRT Scanlines, frames)
      is placed where it lands without a selection and seen through the layer's frame
      (`filter_bounds` keeps the whole result's): the layer keeps its size, uncovered
      selected pixels keep the original, and a result note says so (decided October 2026, as
      Krita masks output by the selection). New layers ignore it.
    - `commit_gmic_render` writes every changed layer and new one under one undo snapshot;
      the status bar gives the count or new size.
  - **Memory.** A run holds float copies (16 bytes a pixel for RGBA): Patchy's planar
    image, the interpreter's copy, and the result. `gmic_run_memory_mb` estimates three
    times the largest layer (or every lined-up one) and refuses a run above 60 percent of
    installed memory before allocating (`PATCHY_GMIC_MEMORY_LIMIT_MB` for tests).
    `render_gmic_filter` never throws (an exception escaping a worker ends the process).
    Worker waits sleep on a future; a bare `processEvents` loop spins a core.
  - **Added layers in the preview.** A full preview inserts them where OK will (ids in
    `Shown::added`); drafts add none. They come out when the shown result changes, Preview
    goes off or the dialog closes, always before the commit inserts its own. The Layers
    panel keeps only its eyes (`preview_lock_allows_layer_visibility_`,
    `LayerListWidget::set_visibility_only`) to inspect them; a preview layer's eye carries
    to the next result with as many layers and to the layer OK adds. Eyes do not change
    which layers "visible" reads: those are fixed when the dialog opens.
  - **Result notes.** Each full preview's `gmic_result_notes` fills the dialog's info banner
    (`gmicResultNotes`): layers changed or combined, a size change (and any part outside
    the canvas), layers added, chosen layers that cannot change. Whether a filter adds
    layers depends on its settings, so it is reported from a run, never guessed.
  - `gmic_target_layer` gates like Liquify (Quick Mask, editable RGB pixel layer, no Smart
    Object, locks, the rasterize prompt for text and shapes). The dialog command gates before
    it opens, since the preview needs the layer.
  - **Preview** (`gmic_filter_library_dialog`) is the filter dialogs' shared pattern: an
    `AsyncPixelPreviewState<GmicPreviewRequest>` whose `start` spawns a
    `run_tracked_background_worker`, brackets it with `CanvasWidget::begin_preview_render` /
    `end_preview_render` (the canvas "Rendering preview..." badge), and whose queued completion
    writes the result into the real layer only when its generation is still current and nothing
    newer is pending. Departures from that pattern, all in the dialog's preview callback:
    - New settings ask the run in flight to stop and start at once beside it: a stale run
      stops only at the interpreter's next check (up to about 10 s on 12 MP). At most two live
      (`kMaxLivePreviewRuns`); beyond that the newest request waits as `pending`. Showing a
      result bumps the generation, so stale runs never replace it.
    - Preview off only puts the original back: the run carries on, and its result (draft
      or full, `Shown::preview`) is kept, so Preview on shows it at once with no new run.
      A run for settings other than the dialog's is stopped while Preview is off.
    - The last full result is kept with the choice it belongs to, so OK commits that very
      result instead of running the filter again, and returning to those settings shows it
      without a run. A dialog timer copies the running preview's `RunControl::progress`
    (G'MIC's `progress`, 0..100, from some filters only) to the canvas badge ("Rendering
    preview... 42%") through `set_preview_render_progress`; the Apply progress box shows it too.
    The preview runs the filter's apply command (never its preview command); OK commits
    only a full-resolution result.
    The snapshots taken when the dialog opens are the originals every run reads (previews
    change the real layers); `Shown::layers` tracks which layers show a preview.
    `lock_preview_dialog_edits` and a scope guard restore them on every exit, and the
    originals always go back before the commit so the undo snapshot is clean.
  - **Draft, then full quality.** On an input above about 1.5 megapixels the preview runs
    twice (`Stage` in `gmic_filter_library_dialog`): a draft with `GmicRunInput::scale`
    below 1 (the float images reduced to about a megapixel in all by `reduced_image`, each
    changed layer's result enlarged back to its full frame by `enlarged_image`, added layers
    left out), shown at once, then the full run with the values the draft returned, under the
    badge "Refining preview...". Sizes a filter takes in pixels are not rescaled, so a
    draft looks somewhat stronger than the result. No draft when no chosen layer is
    writable, or when this filter's last full run this session was quick
    (`gmic_full_ms_per_megapixel_`, under 400 ms estimated). `PATCHY_GMIC_DRAFT_PIXELS`
    overrides the megapixel budget for tests. OK while the full run for the accepted
    settings is in flight waits for that run (`wait_for_gmic_run`) instead of starting
    another; a request that repeats what is already rendering is dropped.
  - Non-modal (`run_non_modal_dialog`): the canvas can be zoomed and panned.
  - **Point handles.** `GmicParameterPanel::points()` lists the visible point parameters in
    percent of the image the filter works on (the active layer, each chosen layer taking the
    same percentages of its own frame, or the document when layers are lined up); the
    dialog reports them through `set_points_callback` whenever the filter, a
    position or the input layers change, and `move_point` is the way back. MainWindow turns
    them into `CanvasWidget::set_preview_point_handles` (canvas_widget_preview_points.cpp):
    markers in document coordinates that win the left press over the active tool within
    10 px, report every move, and are removed when the dialog ends. While markers exist they
    own the cursor (`apply_preview_point_cursor`): open hand over one, closed hand while
    dragging, arrow elsewhere (the Zoom tool keeps its own); no brush ring is drawn. A marker being dragged
    keeps the pointer's position when its owner echoes the move back. A point the
    filter declares hidden is not one, and the two blur filters that are not offered would
    get none
    ([legal-constraints.md](legal-constraints.md)).
  - `MainWindow::apply_gmic_filter(id, values, input_mode)` (the active layer, with prompts
    and status text) and the dialog's OK without a matching preview go through
    `render_gmic_with_progress`: the same
    run on a worker this frame joins while a progress box pumps events (Cancel sets the abort
    flag). `commit_gmic_render` reports failures or pushes one "Filter: <name>" undo step.
