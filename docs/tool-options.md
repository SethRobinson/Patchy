# Tool options that survive a restart

Read this before adding an options-bar control or a tool. The next launch selects the tool that was active at quit and every options bar shows the values it had, the way Photoshop keeps tool options (GitHub issue 254). Settings keys are permanent identifiers (AGENTS.md invariants): never rename one, and never reuse a retired key for a different meaning. Tool behaviors live in [tools.md](tools.md), the palette in [tool-palette.md](tool-palette.md), the brush engine in [brushes.md](brushes.md).

## How it works

- **Load once, at the first canvas.** Startup opens on the start panel with no document, and the options bar needs a canvas to apply values to, so `MainWindow::load_tool_settings` (main_window_tool_options.cpp) runs when the first document session arrives (`startup_tool_settings_pending_`), followed by `sync_tool_option_controls_from_canvas`. Until then `save_tool_settings` writes nothing: a save before the load would overwrite the stored values with the defaults donor's. Every later canvas inherits the window's mirrors (`create_session` and `activate_document_canvas`).
- **Save on change, debounced.** Controls call `schedule_save_tool_settings` (250 ms) or `save_tool_settings`; the close event flushes a pending save. Nothing runs per repaint.
- **The active tool** is stored as its hotkey command id (`tools/activeTool`, "tools.elliptical_marquee"; `tool_hotkey_id` in main_window_shared.hpp), never an enum index. `restore_startup_tool_selection` (constructor, after `create_actions`) checks the palette button and gives the flyout that member's face while the start panel shows; the first canvas then runs `activate_tool` so the group handler does the real switch (Eraser values, status hint). An unknown or missing token is Brush, the palette's built-in default.
- **Malformed keys.** `stored_int` reads a number only when it parses, clamped to the control's range; `stored_bool` accepts true/false/1/0. Anything else, and a tip or preset id the library no longer holds, is the default. A stored tip falls back to Round inside `set_active_brush_tip` without re-applying the tip's imported ABR flow and Airbrush over the stored values.
- **Tests:** `ui_active_tool_and_options_restore_on_next_launch` (three windows from one store: tool, flyout member, Brush, Eraser and Marquee values), `ui_tool_settings_fall_back_on_missing_or_malformed_keys`, `ui_startup_restores_stored_brush_and_gradient_settings`. The suite shares one settings store, so `cleanup_after_visual_test` (tests/ui/ui_test_support.cpp) clears `tools/activeTool` and the brush keys around every test; a test that builds a second window inside itself sees the first window's saves, which is the behavior under test.

## Deliberately not restored

Anything a user would read as "the tool is broken" on a fresh launch starts from its default:

- An in-progress Free Transform, Warp, Crop box or text edit (the Free Transform fields, pivot, link, interpolation mode aside, the Warp style and bend, the Crop W/H size fields).
- Quick Mask mode and a temporary tool (the spacebar Hand, the Alt eyedropper, the Zoom modifier): canvas state, never written.
- The selection combine mode (New/Add/Subtract/Intersect), the vector Combine operation (New Layer/Add/Subtract/...), the Patch tool's Mode and Transparent (retired keys `tools/patchMode` and `tools/patchTransparent`, [healing.md](healing.md)), the Align To choice (retired `tools/alignTo`, [alignment.md](alignment.md)).
- The Marquee and shape Style rows (Normal/Fixed Ratio/Fixed Size with their W/H): a persisted Fixed Size silently resizes the first drag.
- Round and Square brush dynamics (session values, [brushes.md](brushes.md)), the Mixer "useful combinations" row (derived from Wet/Load/Mix).
- The Type tool's font, style, size, color, alignment and orientation: they mirror the selected text layer; a new layer takes the application font. Remembering the last-used type style is a separate feature.

## Inventory

Every options-bar control, in bar order. "Before" is the state before issue 254. Keys under `tools/` unless noted.

| Tool | Control (object name) | Key | Before | After |
|---|---|---|---|---|
| all | active tool (`tool*Action`, flyout member) | `activeTool` | no | yes |
| Move | Auto-Select (`moveAutoSelectCheck`) | `moveAutoSelect` | yes | yes |
| Move | Auto-Select target (`moveAutoSelectTargetCombo`) | `moveAutoSelectTarget` | yes | yes |
| Move | Transform Controls (`moveShowTransformControlsCheck`) | `showTransformControls` | yes | yes |
| Move | Snap (`moveSnapCheck`) | `view/snapEnabled` (View > Snap) | yes | yes |
| Move | Align / Distribute buttons, Align To | commands; `alignTo` retired | no | no (session) |
| Free Transform | Pivot, link, Apply/Cancel, Warp style/bend | none | no | no (session) |
| Free Transform | Interpolation (`freeTransformInterpolationCombo`) | `transformInterpolation` | yes | yes |
| Marquee, Lasso, Polygonal, Wand, Quick, Object | selection mode (`selection*ModeAction`) | none | no | no (session) |
| Marquee, Elliptical, Lasso, Polygonal, Magnetic, Wand, Quick, Object | Feather (`selectionFeatherSpin`) | `<tool>Feather` per tool | yes | yes |
| same, Quick Select aside | Anti-alias (`selectionAntiAliasCheck`) | `<tool>AntiAlias` per tool | yes | yes |
| Marquee | Radius (`selectionCornerRadiusSpin`) | `marqueeCornerRadius` | no | yes |
| Marquee, Elliptical | Style, W, H (`selectionStyleCombo`, `selectionRatio*`, `selectionFixed*`) | none | no | no (session) |
| Magnetic Lasso | Width, Contrast, Frequency (`magneticLasso*Spin`) | `magneticLassoWidth`, `...EdgeContrast`, `...Frequency` | yes | yes |
| Magic Wand | Tolerance, Contiguous, Sample All Layers | `wandTolerance`, `wandContiguous`, `wandSampleAllLayers` | yes | yes |
| Quick Select | Size, Sample All Layers, Enhance Edge | `quickSelectSize`, `...SampleAllLayers`, `...EnhanceEdge` | yes | yes |
| Object Selection | Sample All Layers, Model | `objectSelectSampleAllLayers`, `objectSelectModel` | yes | yes |
| Crop | Style, ratio W/H, Delete Cropped Pixels, Delete Layers | `cropStyle`, `cropRatioWidth`, `cropRatioHeight`, `cropDeletePixels`, `cropDeleteLayers` | yes | yes |
| Crop | Crop options popup: Show Cropped Area, shield on, color, opacity (`cropOptionsButton`) | `cropShowCroppedArea`, `cropShieldEnabled`, `cropShieldColor`, `cropShieldOpacity` | no | yes |
| Crop | Size W/H, ratio preset row, Apply/Cancel | none (live box; preset derived) | no | no (session) |
| Crop | Overlay, Turn Overlay, Always / While Dragging (`cropOverlayCombo`; `cropOverlayTurnAction` and the show choice in the Crop options menu) | `cropOverlay` (token), `cropOverlayOrientation`, `cropOverlayShow` | new (issue 325) | yes |
| Artboard | Size preset, W, H, Background, Auto-nest | `artboardWidth`, `artboardHeight`, `artboardBackground`, `artboardBackgroundColor`, `artboardAutoNest` | yes | yes |
| Brush family | Preset (`brushPresetCombo`, built-in presets) | `brushPreset` | no | yes |
| Brush family | Size (`brushSizeSpin`, slider) | `brushSize` | no | yes |
| Brush | Mode (`brushModeCombo`) | `brushMode` | yes | yes |
| Brush family | Opacity (`brushOpacitySpin`) | `brushOpacity` | no | yes |
| Brush family | Soft (`brushSoftnessSpin`) | `brushSoftness` | no | yes |
| Brush, Pattern Stamp | Flow (`brushFlowSpin`) | `brushFlow` | no | yes |
| Brush | Airbrush (`brushAirbrushCheck`) | `brushAirbrush` | no | yes |
| Brush, Mixer, Eraser | Smooth and its four options | `brushSmoothing`, `...PulledString`, `...CatchUp`, `...CatchUpEnd`, `...ZoomAdjust` | yes | yes |
| Brush, Mixer, Pattern Stamp, Eraser | Tip (`BrushTipPicker`) | `brushTip` | no | yes |
| Brush | Dynamics button | per-tip sidecar; Round/Square session | sidecar | sidecar |
| Eraser | Size, Opacity, Soft (own set) | `eraserSize` (before), `eraserOpacity`, `eraserSoftness` | size only | yes |
| Mixer Brush | Wet, Load, Mix, Flow, Sample All Layers | `mixerWet`, `mixerLoad`, `mixerMix`, `mixerFlow`, `mixerSampleAllLayers` | yes | yes |
| Pattern Stamp | Pattern (by pattern id), Aligned | `patternStampPatternId`, `patternStampAligned` | yes | yes |
| Clone | Aligned | `cloneAligned` | yes | yes |
| Clone, Healing, Spot Healing | Sample All Layers (`retouchSampleAllLayersCheck`) | `retouchSampleAllLayers` | yes | yes |
| Healing | Diffusion | `healingDiffusion` | yes | yes |
| Patch | Mode, Transparent | retired keys | no | no (session) |
| Dodge, Burn | Strength, Range, Protect Tones | `localAdjustmentStrength`, `localToneRange`, `localProtectTones` | yes | yes |
| Sponge | Mode, Vibrance | `spongeMode`, `spongeVibrance` | yes | yes |
| Gradient | Method, Opacity, Reverse, gradient stops | `gradientMethod`, `gradientOpacity`, `gradientReverse`, `gradientUseCustomStops`, `gradientStops` | yes | yes |
| Fill | Opacity, Soft, Tol, Contiguous | `fillOpacity`, `fillSoftness`, `fillTolerance`, `fillContiguous` | yes | yes |
| Line, Rect, Ellipse, Pen, Polygon, Custom Shape | Mode (Shape/Path/Pixels) | `vectorToolMode` | yes | yes |
| same | Fill and Stroke paint (by pattern/gradient id), Stroke width (0 = off; stored as Stroke on plus the last real width), Line weight | `vectorFill*`, `vectorStroke*`, `vectorLineWeight` | yes | yes |
| same, Path/Direct Select | Combine (`vectorCombineCombo`) | none | no | no (session) |
| Line | arrowheads (`lineArrow*Button`) | `lineArrowStart`, `lineArrowEnd` | yes | yes |
| Rectangle, Ellipse (Pixels) | Fill (`shapeFillCheck`) | `shapeFill` | no | yes |
| Rectangle | Radius (`shapeCornerRadiusSpin`) | `shapeCornerRadius` | yes | yes |
| Rect, Ellipse (Pixels) | Style, W, H (`shapeStyleCombo`, `shapeFixed*`) | none | no | no (session) |
| Pen | Auto Add/Delete | `penAutoAddDelete` | yes | yes |
| Polygon | Sides, Star inset | `polygonSides`, `polygonStarInset` | yes | yes |
| Custom Shape | Shape (by shape id) | `customShapeId` | yes | yes |
| Type | Font, Style, Size, Color, alignment, orientation | none | no | no (layer state) |
| Type | Smoothing (`textSmoothingCombo`) | `textSmoothing` | yes | yes |
| Pick | Sample Size, Sample, Sets, Show Sample Ring | `eyedropperSampleSize`, `eyedropperSampleLayers` (five-way token; the older bool `eyedropperSampleAllLayers` is still written and maps on first read), `eyedropperPicksBackground`, `eyedropperShowSampleRing` | yes | yes |
| Zoom | Zoom In/Out, Scrubby | `zoomToolZoomsOut`, `zoomScrubby` | yes | yes |

Counts: 55 rows (a row is one control or one group sharing a handler). 12 new keys across 11 rows: the active tool, the Brush family's preset, size, opacity, Soft, flow, Airbrush and tip, the Eraser's opacity and Soft, the Marquee radius, the pixel shapes' Fill. 35 rows were already persisted. 9 rows are deliberately session-only or layer state (the list above).

The keys the May 2026 brush reset retired (`brushSize`, `brushOpacity`, `brushSoftness`, `brushTip`, `brushPreset`, `eraserOpacity`, `eraserSoftness`) carry their old meaning and are back in use; `brushBuildUp` stays retired, `brushAirbrush` replaces it.
