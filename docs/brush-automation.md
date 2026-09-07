# Advanced brush automation

`patchy.brushes` and `layer.drawStrokes` expose the existing native Brush, Eraser,
and Mixer Brush engines. API 1 remains compatible with earlier round strokes.
Public signatures and numeric limits live in `scripts/bundled/patchy.d.ts`.

## Ownership and settings

`ui/brush_automation` owns the strict settings resolver and complete preset store.
`ui/script_brush` implements discovery, resource writes, previews, and point/batch
validation. The host supplies document services. `canvas_widget_script_stroke`
uses native stamping, midpoint smoothing, stabilizer, pickup and compositing;
it never sends desktop input events. The preset UI shares the resolver/store.

Resolve defaults, selected preset, then explicit overrides. A supplied replacement
tip loads its own spacing, shape, and dynamics before explicit overrides. Nested
settings merge by field; unknown fields and invalid values fail before history.
Legacy sizeJitter/scatter remain aliases; conflicting nested values fail. All
resources in a batch resolve before painting and are held by shared ownership.
Temporary settings never modify a tip sidecar. Artist tool, colors, pen mapping,
tip, dynamics, smoothing and stroke state restore even after interruption.
Focus loss during a native script stroke skips manual gesture cleanup: zoom-field
focus or switching windows must not reset its coverage, spacing, or Mixer pickup.

Brush dynamics follow the engine's existing JSON vocabulary. Sidecar parsing
remains tolerant for compatibility; API validation is strict before conversion.
Brush supports every implemented dynamics family. Mixer and Eraser support static
tips and their native controls; explicit Brush dynamics on these tools fail.
Legacy Eraser jitter/scatter acceptance is retained. Mixer uses native full
opacity and meters deposition through Flow. Global dynamic controls resolve
against explicit script pen settings, defaulting to the historical size/opacity
mapping, rather than the artist's preferences.

`getCurrent` captures settings explicitly. `activate` changes the manual tool;
ordinary stroke calls restore it. Presets saved through the AI appear in the
existing preset control, whose final entries save/manage user presets. Built-in
IDs and startup Round behavior remain unchanged. Working changes do not update
a saved preset. UI activation preserves colors unless the preset includes them.

## Resource persistence

Tips use the existing grayscale PNG/JSON library. Coverage buffers use 255 for
paint; image and document capture use inverted luminance times alpha and optional
selection coverage. Capture is bounded to 4096 square. ABR returns every new ID
and warnings and retains partial success when individual brushes cannot import.

Complete presets live under the settings directory's `brush-presets`, one
version-1 JSON record per UUID. Each holds full settings, name/folder, explicit
color-inclusion state, and an optional base64 lossless PNG tip snapshot. The
snapshot decouples saved brushes from subsequent source-tip edits/deletion.
File writes use QSaveFile; explicit existing IDs are required for replacement.
A lock serializes preset writes. Brush import/creation serialize their API writes.
Document Undo does not roll back resource operations; these return their IDs.
Owned MCP sessions retain the original brush settings filename before moving their
window preferences into temporary storage. Tips, preset files, and the default-tip
migration version therefore persist and are shared with the artist. Test settings
roots remain isolated from the real library.

Directory fingerprints, including JSON contents to detect same-size rapid edits,
refresh libraries across processes. Refresh
occurs before API access and periodically while the window is idle, never from
paintEvent. A changed tip directory refreshes the tip library and its controls;
preset-only changes do not invalidate active tip entry pointers. Resource reads
and scratch previews do not change document history or revisions.

## Time, feedback, and cancellation

Untimed points preserve spatial painting. Timed points start at zero, are complete
and nondecreasing, and hold their last pen/position between samples. Airbrush
requires timing. At an equal timestamp, pointer samples apply before due ticks;
smoothing ticks precede airbrush ticks. Native 16 ms smoothing and 50 ms airbrush
cadences run in simulated time, independent of wall-clock delays and present().
Repeated positions with later times create dwell. The parser bounds timeline
duration and expanded ticks before history. Path duration is assigned by distance
within each subpath; each subpath starts a new stroke and pickup state.

Smoothing uses the existing controls and an explicit reference zoom for the
screen-relative case. Preview pacing cannot add paint. Dirty rectangles reach
the existing progressive-refresh machinery; stroke progress updates the current
MCP/CLI operation label. Stop checks run between pointer samples and native ticks.
Normal mode creates one Undo entry per touched document per script. Slow mode
separates each native stroke, including strokes within one batch or strokePath.
It presents completed changes without changing simulated time; see
[automation-feedback.md](automation-feedback.md).

MCP state contains compact current-brush metadata and the library revision.
Attached fingerprints additionally include current brush settings. Fetch full
settings through targeted scripts rather than adding all dynamics to each state.

## Limits and verification

No physical bristle, pigment, paper, fluid, or impasto simulation is added. Wet
Edges uses the existing whole-stroke coverage boundary; Mixer uses the existing
canvas-only running pickup average and fresh stroke-start source snapshot. These
remain separate effects. Existing palette restrictions and brush legal boundaries
in [brushes.md](brushes.md) and [mixer.md](mixer.md) remain authoritative.

`ui_script_advanced_brush_*` covers native parity, settings restoration, malformed
batches, timeline determinism, independent pressure controls, preset snapshots,
cross-instance refresh, Unicode previews, UI availability and PSD reopening.
Legacy automation/brush/Mixer/smoothing tests remain required, together with MCP
and theme/hotkey checks. Art examples teach swatch selection and repeated preview
review; an oil label alone does not establish convincing painting technique.
