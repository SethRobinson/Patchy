# Scripting API compatibility

`app.apiVersion` is 1. Bump it only for breaking API changes. Every addition and
behavioral change is recorded here, newest first, in the same change that makes it; the
entries are a permanent record. Entries through 2026-09-30 (the July 2026 API, MCP
attachment, palettes, vector automation, brushes, Slow mode and Pause, text direction,
Remove Object, Align/Distribute and smart objects) are in
[scripting-api-changes-through-2026-09-30.md](scripting-api-changes-through-2026-09-30.md),
those of 2026-10-01 to 2026-10-19 in
[scripting-api-changes-2026-10-01-to-19.md](scripting-api-changes-2026-10-01-to-19.md), and
those of 2026-10-20 to 2026-10-28 in
[scripting-api-changes-2026-10-20-to-28.md](scripting-api-changes-2026-10-20-to-28.md).
those of 2026-10-20 to 2026-10-29 in
[scripting-api-changes-2026-10-20-to-29.md](scripting-api-changes-2026-10-20-to-29.md).

Rollover: once this file passes 27,000 bytes, move its oldest entries, unchanged and in order,
out of it until about 12,000 bytes remain, so the next rollover is weeks away. They go into the
newest archive (the last one named above) until that archive nears 27,000 bytes, then into a new
archive. Renaming the newest archive for its new end date changes one link in each file that
links it. Archives are frozen records: when entries move into one, links to them from other
documents point at the archive file without a `#anchor`, and an archived file is edited only
when `python scripts/check-docs.py` reports a broken link in it.

Every entry below is API 1.

2026-11-09 (behavioral): `patchy.ui.captureWindow` leaves the running script's own status-bar
indicator (Running script, Stop, Pause, Slow) out of the capture; it shows again right after. Pinned
by `ui_script_capture_window_omits_its_run_indicator`.
2026-11-08 (additive): `app.buildBookPdf(folder, path, options)` writes the PDF of a Scan a Book
to PDF project folder ([book-scan.md](book-scan.md)). Its option keys (`mode` with `"color"`,
`"gray"`, `"bw"`; `threshold`, `dpi`, `imageQuality`, `pageSize` with `"median"`, `"largest"`,
`"cover"` or `[width, height]`; `title`, `author`) and the `book.json` format id `patchy-book-scan`
are permanent identifiers.
2026-11-08 (additive): the Type Mask tool (#670, [type-mask.md](type-mask.md)). Its tool id
`tools.type_mask` (theme tool id `type_mask`) and the Type group's cycle command `tools.cycle.type`
are permanent identifiers; `patchy.ui.toolPalette`'s `hiddenTools` accepts `tools.type_mask`.
2026-11-08 (additive): text alignment takes Photoshop's other justify modes (GitHub issue 672):
`layer.textAlign` and `doc.addTextLayer`'s `align` accept and read `"justifyCenter"`,
`"justifyRight"` and `"justifyAll"` beside `"left"`, `"center"`, `"right"` and `"justify"` (last
line left). They differ in where a box's last line goes; point text sets them flush left
([text-tool.md](text-tool.md#justify-modes)). Permanent identifiers: the three values. Pinned by
`ui_text_paragraph_alignment_names_round_trip` and `ui_script_text_align_justify_modes`.

2026-11-06 (additive): `layer.linkGroup` reads and sets a layer's link group (Layer > Arrange >
Link Layers, #656): layers sharing a nonzero number are linked; 0 is unlinked, and a layer whose
partners are gone reads 0. `patchy.ui.toolPalette`'s layer button ids take `"link"`, the Layers
footer's Link Layers button, hidden by default.
2026-11-06 (additive): IPTC Data Mining (#658). `doc.fileInfo` and `doc.setFileInfo` gain `dataMining`
(plus:DataMining: `""` for not set, else a PLUS vocabulary URI such as
`"http://ns.useplus.org/ldf/vocab/DMI-PROHIBITED-AIMLTRAINING"`; setFileInfo also accepts the value the
document already has and throws for any other string) and `otherConstraints` (plus:OtherConstraints, a
string). `app.saveMetadataPreferences` and `app.setSaveMetadataPreferences` gain `dataMining` (the
Personal default, `""` or a vocabulary URI; settings key `saveMetadata/dataMining`). Permanent
identifiers: the key names, the settings key and the URIs, which are IPTC's. Pinned by
`ui_script_file_info_data_mining` and `ui_save_metadata_data_mining_default`.

2026-11-06 (behavioral): `app.listFonts()` names families without Qt's " [Foundry]" suffix and lists
a family two foundries install once (Linux, #646 follow-up); a text family given with the suffix
resolves to the plain one.

2026-11-06 (behavioral): every animated export (`doc.exportAnimatedWebp`, `doc.exportAnimatedPng`,
the `animate` key of `saveAs`/`exportAs`/`encodeForWeb`) reads layers named "all": a top-level layer
named "all" is drawn in every frame and is not a frame; a top-level group named "all" turns each unit
inside it into a frame drawn inside the group; clipped layers go with their base. Documents without
such names keep their frames. Rules in [animation.md](animation.md#frames-and-layers-named-all).

2026-11-05 (additive): `patchy.ui.rotation`, the active document's view rotation in degrees,
clockwise, in (-180, 180] ([view-navigation-rotation.md](view-navigation-rotation.md)). Setting wraps
any finite angle and turns the view about its center; NaN or no document throws; 0 with no document.
Only the view turns. Permanent identifiers: `rotation`, the tool id `tools.rotate_view`, the command
ids `view.rotate_left`, `view.rotate_right`, `view.rotate_reset` and `tools.cycle.hand`. Pinned by
`ui_script_ui_view_rotation`.

2026-11-05 (additive and behavioral): line geometry takes `arrowConcavity` (-50..50 percent, default 0)
and `getShape()` returns it with the line's other arrowhead values (GitHub #622 follow-up,
[vector-tools.md](vector-tools.md#polygon-custom-shape-and-line-arrowheads)). `updateShape` with a line
geometry replacing a line now keeps that line's head proportions, scaled with the new weight, and its
concavity when `arrowWidth`, `arrowLength` or `arrowConcavity` are left out (they were reset to 5 and
10 times the weight and 0). Permanent identifier: `arrowConcavity`. Pinned by
`ui_script_vector_line_arrowheads_keep_proportions`.

2026-11-05 (additive): `layer.colorLabel` takes and returns `"seafoam"`, `"indigo"`, `"magenta"` and
`"fuchsia"`, Photoshop 2024's layer colors (PSD `lclr` 8 to 11, GitHub issue 635,
[layer-panel.md](layer-panel.md#color-labels)). Permanent identifiers: those four ids and the command
ids `layer.color_seafoam`, `layer.color_indigo`, `layer.color_magenta` and `layer.color_fuchsia`.
Pinned by `ui_script_layer_color_label`.

2026-11-03 (additive): `doc.selection.smooth(radius)` and `doc.selection.feather(radius)`, Select >
Smooth and Select > Feather (GitHub issue 614, [selection-modify.md](selection-modify.md)): Smooth
takes a whole radius 1..500, Feather 0.1..1000; both work on soft selections at any bit depth, do
nothing without a selection and throw for a radius out of range. Permanent identifiers: the method
names, command ids `select.smooth` and `select.feather`, and the settings keys
`tools/selectionSmoothRadius` and `tools/selectionFeatherRadius`. Pinned by
`ui_script_selection_smooth_and_feather`.

2026-11-03 (additive): `layer.convertToShape()` and `layer.createWorkPath()`, Type > Convert to
Shape and Type > Create Work Path (GitHub issue 598, [text-outlines.md](text-outlines.md)): a text
layer becomes a shape layer from its glyph outlines in place (returns the layer), or its outlines
replace the work path with the text kept (returns the work path); both throw for other layers.
Permanent identifiers: the method names and command ids `type.convert_to_shape` and
`type.create_work_path`. Pinned by `ui_script_text_convert_to_shape_and_work_path`.

2026-11-03 (additive and behavioral, companions): a companion started from Plugins > Companions also
gets `PATCHY_MCP_ARGS`, a JSON array of the arguments that follow `PATCHY_MCP` to attach
(`["--attach"]`, or `["patchy-mcp","--attach"]` inside an AppImage). A manifest's `command` may name
a macOS app bundle (`*.app`), which runs its `Contents/MacOS` executable; a relative `workingDirectory`
resolves against the manifest's folder (it was Patchy's working folder). Inside an AppImage the
companion no longer inherits `APPDIR`, `APPIMAGE`, `ARGV0`, `OWD` or the platform theme AppRun chose
([companions.md](companions.md#companions-menu)). Permanent identifier: `PATCHY_MCP_ARGS`. Pinned by
`ui_companion_launch_guard_bundles_and_manifest_problems` and
`ui_companion_environment_names_the_appimage`.

2026-11-02 (additive): text runs (`addTextLayer`, `setTextRuns`) take the rest of the Character
panel's OpenType toggles (GitHub issue 346, [text-character-styles.md](text-character-styles.md)):
booleans `ligatures` (default true), `contextualAlternates`, `discretionaryLigatures`, `swash`,
`stylisticAlternates` and `titlingAlternates`; an unset key keeps the layer's. `textRuns` reads all
six back. Permanent identifiers: those keys. Pinned by
`ui_text_opentype_features_use_the_font_features_if_available`.

2026-11-02 (additive): `patchy.gegl.filters()` lists the GEGL filters Patchy offers
(`{id, name, category, description, parameters}`, each parameter `{name, label, type, default,
minimum, maximum, choices}`), and `layer.applyFilter` runs them by id ([gegl-filters.md](gegl-filters.md)).
Permanent identifiers: the object, the method, its keys, the type tokens `integer`, `number`, `boolean`,
`choice`, the color choices `default`, `foreground`, `background`, `black`, `white`, and filter ids
`gegl.<operation>`, with GEGL's own setting names and choice nicks. Pinned by
`ui_gegl_filters_list_and_apply_if_available` and `ui_gegl_script_identifiers_are_pinned`.

2026-11-02 (additive, MCP): the companion protocol, version 1 ([companions.md](companions.md#protocol)).
`get_info` adds `companionProtocol` (`{min, max}`); the `initialize` reply's
`experimental["patchy/companion"]` is `{minProtocol, maxProtocol}`; a client's
`experimental["patchy/companion"]` may be `{id, name, version, minProtocol, maxProtocol}` (a string
is still its id); manifests take optional `minProtocol` and `maxProtocol`. Permanent identifiers:
those keys and field names. Pinned by `ui_companion_protocol_range_is_checked`.

2026-11-02 (additive, MCP): `companionCommand` adds the action `"quit"`, sent by the Quit entry
Plugins > Companions shows while that companion is attached. Permanent identifier: the action
value. Pinned by `ui_companion_menu_quits_attached_companion`.

2026-11-02 (additive): `doc.resizeCanvas(w, h, options)` takes `anchor` (nine ids, `"topLeft"`
to `"bottomRight"`) or `x`/`y` (the old canvas origin in the new canvas) and `fill`; with options
it works like Image > Canvas Size (content and selection move, the Background fills with `fill`,
default white). Without options it is unchanged. `get_state` layers add `hasLayerMask` and
`layerMask` (`bounds`, `enabled`, `defaultColor`, `density`, `feather`, or null). Permanent
identifiers: the option keys, the anchor ids and the two state fields. Pinned by
`ui_script_resize_canvas_anchor_and_offset` and `ui_mcp_state_reports_layer_masks`.

2026-11-02 (behavioral and additive, MCP): the attach socket serves up to four clients at once
instead of refusing a second one with `workspace_unavailable`; a request while another client's
script runs gets `busy`. `get_info` adds `mainWindow` (`geometry`, `minimized`, `nativeId` on
Windows) and the capabilities `pixelExchange` and `companionEvents`. A client that sets
`capabilities.experimental["patchy/events"]` at initialize receives `notifications/patchy/event`
(`selectionChanged`, `activeDocumentChanged`, `documentClosed`, `stateChanged`, `companionCommand`);
`experimental["patchy/companion"]` names its Companions menu id ([companions.md](companions.md)).
Permanent identifiers: the experimental keys, the notification method, the event types and
fields, the capability tokens, and the `mainWindow` fields. Pinned by
`ui_mcp_multi_client_attach_tokens_and_busy`, `ui_mcp_events_reach_subscribed_companions` and
`tests/mcp_client_tests.py`.

2026-11-02 (additive): pixel exchange for scripts and companion apps ([companions.md](companions.md)):
`doc.selection.getMask(rect?)` and `setMask(mask, {operation})` (one byte of selection coverage
per pixel, soft edges included), `layer.getLayerMask()`, `setLayerMask(mask)` (a coverage block or
`{fromSelection, invert}`, with `defaultColor`, `density` 0..100 and `feather`) and
`removeLayerMask()`, `doc.getCompositePixels(rect?)` (the flattened RGBA8 pixels), and
`patchy.io.readImage(path, {grayscale})` / `writeImage(path, block)`. Permanent identifiers: the
method names, the option keys `operation`, `fromSelection`, `invert`, `defaultColor`, `density`,
`feather`, `grayscale`, and the operation tokens. Pinned by `ui_script_selection_mask_reads_and_combines`,
`ui_script_layer_mask_set_get_and_remove`, `ui_script_layer_mask_companion_insert_is_one_undo_step`,
`ui_script_composite_pixels_match_flatten` and `ui_script_io_image_round_trips_unicode_path`.

2026-10-31 (behavioral): shape stroke `dashes` take 0 entries (a dot with round or square caps);
the list still needs one value above 0. Before, any 0 was refused, so a Dotted stroke read back
from `getShape()` ([0, 2]) could not be written again (GitHub issue 548). Pinned by
`ui_script_vector_stroke_accepts_zero_length_dots`.

2026-10-31 (additive): `patchy.ui.toolPalette.hiddenLayerButtons` knows the optional footer buttons
`merge_visible` and `rasterize` (GitHub issue 539, Layer > Merge Visible and Rasterize), hidden by
default like `merge_down`. Permanent identifiers: both ids. Pinned by
`ui_layer_footer_merge_visible_and_rasterize_buttons`.

2026-10-29 (additive): brush dynamics objects (`drawStrokes` `dynamics`, `patchy.brushes` settings
and tip records) take `shapeDynamicsEnabled`, `scatteringEnabled` and `transferEnabled` (booleans,
true by default): the Dynamics popup's section switches. Off keeps the section's values and paints
as if it were at defaults (GitHub issue 506, [brush-dynamics.md](brush-dynamics.md)). Permanent
identifiers: the three keys. Pinned by `ui_brush_dynamics_section_switch_keeps_values`.

2026-10-28 (additive): `patchy.ui.toolPalette.ungroupTools` (boolean, false by default): Customize
Tool Palette's "No tool groups", every tool on its own button (GitHub issue 499,
[tool-palette.md](tool-palette.md#customizing-the-palette)). Permanent identifier: the key. Pinned by
`ui_script_tool_palette_reads_and_sets`.

2026-10-28 (additive): `layer.resolvedTextFonts()`: per run of a text layer, `{text, font, family,
style}`, the font the run names and the family and style the text engine really draws it in
(`render_text_font_for_display_family` read back through `QFontInfo`). Permanent identifiers: the
method and its four keys. Pinned by `ui_headless_text_fonts_match_windowed_if_available`.
