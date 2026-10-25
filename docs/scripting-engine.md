# Scripting engine rules (binding design decisions)

Split from [scripting.md](scripting.md). Script and API identifiers are permanent (`AGENTS.md`); every API change is logged in [scripting-api-changes.md](scripting-api-changes.md).

- **One run at a time, on the UI thread.** A run stays alive until the synchronous
  evaluation AND every timer and script canvas window are done. A fresh QJSEngine per
  run means stored QJSValues die with their engine (canvas windows drop theirs first).
- **One undo entry per run and session by default.** The first mutation a run makes to a
  session pushes one "Script: name" snapshot (`prepare_mutation`); everything after rides
  it, so a 60fps animation undoes in one step. `app.undoEnabled = false` (per run,
  resets each run) skips the snapshot for speed; those edits cannot be undone, but
  sessions are still marked modified so closing protects the work. Connector sessions
  reject it so failed edits remain recoverable. `patchy.ui.slowMode` instead separates
  native strokes and undoable edits ([automation-feedback.md](automation-feedback.md)).
- **Wrappers hold ids, never pointers.** Layer wrappers keep session id + LayerId and
  re-resolve on every access, throwing a JS error when the target is gone (a stored
  `Layer*` is the historical use-after-free pattern). Reads resolve through const
  documents (mutable layer accessors bump revisions on access).
- **Refresh is coalesced.** Mutations mark per-session dirt; one deferred flush per
  event-loop turn repaints the canvas and refreshes panels for the active session only
  (structure changes rebuild the layer panel; pixel-only changes refresh thumbnails).
  Visible MCP/CLI runs also present completed edits periodically (`patchy.ui.present`
  gives explicit frames and pacing; CLI runs have a status-bar Stop control), and
  `patchy.ui.paused` parks visible automation after a native edit, manual edits then
  splitting script Undo groups. Both: [automation-feedback.md](automation-feedback.md).
- **The watchdog measures INACTIVITY, never total runtime.** Legitimate scripts run for
  hours; a blanket runtime limit is wrong by design. A helper thread arms around every
  evaluate and callback, every hot service call feeds it (a lock-free atomic in
  `pump_progress_indicator`), and it calls `QJSEngine::setInterrupted` only when a script
  made NO API call for the whole window (default 2 minutes; `PATCHY_SCRIPT_TIMEOUT_MS`
  overrides it, which the tests use): the only defense against `while (true) {}`, since
  a frozen UI thread cannot prompt. Silent pure-JS computation still dies, so heavy
  scripts log or write progress periodically. Connector-owned runs pump through a
  throttled progress callback instead of the modal panel (ai-control.md). Never remove
  the arm/disarm pairing around a new entry point into script code; route new callback
  invocations through `call_script_callback`.
- **Reentrancy: never destroy the engine from inside script code.** A failing timer or
  window-event callback schedules a deferred finish (`schedule_completion_check`) instead
  of tearing down from inside itself. Stop during evaluation only interrupts; the
  evaluate caller finishes the run.
- **The automatic busy indicator pumps events mid-evaluation: keep the guards.**
  `pump_progress_indicator()` (called at the hot service entry points; it feeds the
  watchdog unconditionally) engages once the CURRENT synchronous burst (the main
  evaluate or one callback, timed by `burst_clock`, restarted per burst) exceeds 500 ms
  (`PATCHY_SCRIPT_BUSY_DELAY_MS` overrides; skipped for unattended runs): the canvas
  processing overlay plus the application-modal `ScriptStopPanel`, then
  `processEvents(AllEvents)`, so the panel's Stop is the only reachable control.
  Stop opens a NON-BLOCKING confirm (with an "Undo the changes it made" checkbox when
  the run pushed undo snapshots) while the job keeps working; never exec a nested loop
  from the panel's handler (a timer-driven click could not be answered from its own
  nested loop). Confirming interrupts the run and `finish_run` undoes the snapshot in
  every touched session; the confirm closes by itself when the run ends first.
  The invariants that make the pumping safe: the script timer slot defers when
  `sync_running || in_callback` (single-shots re-arm via `start(0)`) because QJSEngine
  is not reentrant; `end_progress_indicator()` closes overlay+panel when the burst,
  the session (close_session), or the run ends; and `ModalWatchdogPause` ends the
  indicator on entry (the panel must not fight the script's own dialogs) and restarts
  the burst clock on exit. `createCanvas` does the same through
  `dismiss_busy_indicator()`, and the panel is not raised at all while the run owns an
  open canvas window (`has_open_canvas_window`): a window created under an
  application-modal window is blocked by it and skipped by key delivery, permanently
  on wasm (docs/wasm.md).
  Side effect: the pump runs the coalesced refresh flush, so scripts that push pixels
  repeatedly paint progressively. A pure-JS loop with no API calls cannot pump, so heavy
  bundled scripts write their buffer to the layer a few times mid-computation
  (setPixels REPLACES the layer's pixels: re-send the whole buffer, never strips).
- **Palette mode** ([palette-mode.md](palette-mode.md)): `setPixels` and `fill` snap to
  the document palette like tools (`apply_palette_to_pixels`, dither None, the editing
  alpha threshold); `applyFilter` stays advisory. The palette calls validate before Undo,
  keep layer pixels and default to enabled mode; getters read const and return copies.
  PNG save/export uses the indexed writer; previews stay truecolor.
- **Text layers go through the real pipeline.** `addTextLayer` and the `text` setter
  drive inline-editor sessions (the `cli_append_text_to_text_layers` technique), so
  rasters render through the normal commit path. `addTextLayer` clears the active layer
  first so `add_text_at` cannot latch onto an existing text layer. `size` is the height in
  DOCUMENT PIXELS (the script path sets `setPixelSize(size * zoom)`,
  `ui_script_text_size_is_zoom_independent`); `font` goes through
  `apply_text_family_to_editor` because the commit reads the session family, not the char
  format (`ui_script_text_font_option_applies`). The `text` setter replaces the selection
  in one `insertText(text, format)` with the first character's format, never
  delete-then-insert: an emptied block's char format is the fallback font only
  (`ui_la_methode_script_text_setter_matches_interactive_commit_if_available`). Rich runs
  (`addTextLayer([{text, font, size, bold, italic, color}, ...])`, `layer.setTextRuns`)
  type each run with its own `QTextCharFormat` (`apply_text_run_to_format`); a paragraph
  break is its own run because Qt gives the block separator the preceding text's format.
  `box` opens the session as paragraph text; `align`/`textAlign` go through
  `apply_text_alignment_to_editor`. `textRuns`, `textBox` (null unless the flow metadata
  says box) and `textAlign` read stored metadata without a session. Scripted layers clear
  `kTextStyleNameFormatProperty` and carry `kTextExactSizeFormatProperty`, so the requested
  face and size commit at every zoom (`ui_script_text_face_ignores_the_options_bar_style`,
  `ui_script_text_size_survives_low_zoom_reedit`, `ui_script_text_runs_create_and_read_back`,
  `ui_script_text_box_wraps_and_aligns`, `ui_script_set_text_runs_edits_existing_layer`).
  Area text (`{area}`, `layer.textArea`): [area-text.md](area-text.md).
- **A script move is a Move tool move.** `layer.moveTo` and the `x`/`y` setters shift
  the bounds, then call `translate_moved_layer_metadata` on the layer and every
  descendant: the linked mask, shape model, linked vector mask, smart-object quads and
  text transform (Photoshop's text anchor) travel with the pixels, and unlinked masks
  stay. Never shift a mask by hand beside that helper (it moves twice). A supported
  Smart Filter stack re-renders at the new place (`rerender_moved_smart_filters`); a
  failed render restores the document and throws. `alignLayers`/`distributeLayers`
  (`CanvasWidget::offset_layers`) and `duplicate(targetDocument)`
  (`offset_copied_layer_tree`) follow the same rule. A group has empty bounds: `x`/`y`
  read 0 and `moveTo` offsets its contents. Pinned by `ui_script_move_*`; Photoshop
  check: `scripts\dev\photoshop-text-move-check.ps1`.
- **Blend mode ids** (`script_blend_mode_id`) are a compatibility contract: append-only,
  in BlendMode order, never renamed; Paint.NET's four: [blend-modes.md](blend-modes.md#paintnet-modes).
- **`app.apiVersion` is 1.** Bump only for breaking changes. Record additions and
  behavioral changes in [scripting-api-changes.md](scripting-api-changes.md).
  The change log merges with git's `union` driver (`.gitattributes`), so parallel branches
  never conflict there: write each entry as one block that starts with its date and ends
  with a blank line, add it directly under the intro, and never edit another entry in the
  same change. Same-day entries may land in either order, and two entries merged into one
  spot can lose the blank line between them; add it back when you next touch the file.
  The MCP connector APIs (stable-ID lookups, strokes, previews, history access,
  structured results) are additive; their contracts are in [ai-control.md](ai-control.md).
- **Deep documents** (16/32-bit, on by default; [high-bit-depth.md](high-bit-depth.md)):
  `getPixels`/`setPixels` stay RGBA8 and convert at the boundary, `fill`/`fillRect` and
  `applyFilter` work at depth, `applyPlugin` takes 16-bit layers through an 8-bit copy,
  and `drawStrokes` still requires an 8-bit layer.
- **`include()` resolution order**: relative to the including script, then the user
  root, then the bundled root; a bundled result maps through the shadow-override store.
  `patchy.isMainScript()` is false during an included file's top-level code
  (include-depth counter), so a script can be both a library and runnable
  (`Effects/fancy-background.js` is the model). include() saves and restores the
  includer's global `OPTIONS` binding around the nested evaluation, so a library's own
  OPTIONS block never replaces the caller's.
- **Sound effects (`patchy.ui.playTone`/`playSound`)**: `src/ui/sound_effects.{hpp,cpp}`
  + `sound_effects_mac.mm`, a deterministic 16-bit mono tone synth and per-OS
  fire-and-forget playback (winmm, `NSSound`, a detached `paplay`/`pw-play`/`aplay`
  process; none installed = silent no-op). Deliberately NOT Qt Multimedia (absent from
  the vendored Qt). `playSound` resolves relative paths the include() way, requires
  RIFF/WAVE, and throws for missing/invalid files. `PATCHY_NO_SOUND=1` validates but
  skips the OS call (offscreen tests stay silent).
- **Modal helpers pause the watchdog.** Every interactive helper that blocks in a modal
  wraps itself in `ModalWatchdogPause` (disarms, re-arms with a FRESH timeout on exit, so
  time spent at a dialog never interrupts the script). New modal helpers must do the same.
- **`app.runCommand(id)`** triggers registered QActions by their HotkeyRegistry command
  id; false for unknown or disabled commands, `script.` ids (one run at a time), and
  `edit.undo`/`edit.redo`/`file.quit` during a run.
- **`patchy.ui.zoom` / `patchy.ui.fitOnScreen()`** are the view controls (percent of
  device pixels, active document); fitting settles posted layout first. Window captures
  wait for [Dynamic Vector Preview](vector-preview.md).
- **`patchy.ui.expandPanel`/`collapsePanel`/`closePanel`/`panelState`** take permanent
  lowercase names that `kScriptSidePanels` (script_engine.cpp) maps to the panel ids of
  [right-docks.md](right-docks.md); a new panel appends a name, none is ever renamed.
- The script canvas window deliberately bypasses `run_non_modal_dialog` (its nested
  event loop would park the running script) and applies `keep_dialog_above_parent_window`
  directly, the macOS-critical part of the rule. The editor dialog uses
  `run_non_modal_dialog` as usual.
