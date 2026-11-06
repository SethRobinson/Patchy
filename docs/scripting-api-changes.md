# Scripting API compatibility

`app.apiVersion` is 1. Bump it only for breaking API changes. Every addition and
behavioral change is recorded here, newest first, in the same change that makes it; the
entries are a permanent record. Entries through 2026-09-30 (the July 2026 API, MCP
attachment, palettes, vector automation, brushes, Slow mode and Pause, text direction,
Remove Object, Align/Distribute and smart objects) are in
[scripting-api-changes-through-2026-09-30.md](scripting-api-changes-through-2026-09-30.md),
those of 2026-10-01 to 2026-10-19 in
[scripting-api-changes-2026-10-01-to-19.md](scripting-api-changes-2026-10-01-to-19.md), and
those of 2026-10-20 to 2026-10-23 in
[scripting-api-changes-2026-10-20-to-23.md](scripting-api-changes-2026-10-20-to-23.md).

Rollover: once this file passes 27,000 bytes, move its oldest entries, unchanged and in order,
out of it until about 12,000 bytes remain, so the next rollover is weeks away. They go into the
newest archive (the last one named above) until that archive nears 27,000 bytes, then into a new
archive. Renaming the newest archive for its new end date changes one link in each file that
links it. Archives are frozen records: when entries move into one, links to them from other
documents point at the archive file without a `#anchor`, and an archived file is edited only
when `python scripts/check-docs.py` reports a broken link in it.

Every entry below is API 1.

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
2026-10-28 (behavioral): a `--headless` run on Windows loads the installed fonts at startup, so
scripts, exports and screenshots resolve fonts exactly as a windowed run does (a PSD's Arial Black
rendered in another face before; [font-resolution.md](font-resolution.md)).
2026-10-28 (behavioral): the built-in brush presets lead with `soft_round`, now named "Round", then
`hard_round` (now 25 px, the old Round's values), `square`, `pencil`, `ink`, `airbrush`.
`brushes.listPresets()` no longer lists `round`; `getPreset("round")` and `presetId: "round"` still
work and return `hard_round` (GitHub issue 482, [brushes.md](brushes.md)). Pinned by
`ui_brush_preset_list_leads_with_round_and_retired_round_id_resolves`.
2026-10-27 (additive): `patchy.perspective.clone` takes `sample`: `"current"` (default, unchanged: the
layer alone), `"currentAndBelow"` or `"all"`, the Clone tool's Sample tokens
([perspective-planes.md](perspective-planes.md)). Permanent identifiers: the key and its three tokens.
Pinned by `ui_script_perspective_paste_paint_and_clone`.
2026-10-27 (additive): `patchy.perspective.placeLayer(layer, {plane, placement, target})`, Filter >
Perspective Planes' Place Layer: the layer's own content onto the plane (`placement` `"fill"` by
default, `"fit"` or `{x, y, scale, rotation}`), replacing the layer unless `target: "newLayer"`
([perspective-planes.md](perspective-planes.md)). Permanent identifiers: `placeLayer`, its option keys
and the `target` token `"layer"`/`"newLayer"`. Pinned by `ui_script_perspective_paste_paint_and_clone`.
2026-10-27 (additive): `patchy.perspective` ([perspective-planes.md](perspective-planes.md)):
`checkPlane(corners)`, `getPlanes(doc)` / `setPlanes(doc, planes)`, and `paste(layer, {plane, source,
placement, target})`, `clone(layer, {plane, sourcePlane, from, points, size, hardness, opacity, target})`
and `paint(layer, {plane, points, color, size, hardness, opacity, target})`, each one edit clipped to
its plane. Permanent identifiers: the namespace, the six names, their option keys, the
`checkPlane` results `"tooSmall"`, `"crossing"`, `"concave"`, `placement` `"fill"`/`"fit"` and
`target` `"newLayer"`/`"layer"`. Pinned by `ui_script_perspective_paste_paint_and_clone`.
2026-10-27 (additive): `metadata` in `doc.saveAs`/`doc.exportAs`/`doc.encodeForWeb` options takes
`"basic"`: the save stamp alone (program, creation and modification dates, the file's format,
size and resolution), nothing from File Info ([file-info.md](file-info.md)). An explicit
`thumbnail: true` for `.jpg` now writes the EXIF thumbnail with every `metadata` choice: with
`"none"` the file holds a minimal EXIF with only the thumbnail; without the key nothing changes.
Permanent identifiers: the `"basic"` token. Pinned by `ui_script_export_as_jpeg_coding_options`.
2026-10-27 (additive): `.png` in `doc.saveAs`/`doc.exportAs`/`doc.encodeForWeb` options takes
`animate`, `frameDelayMs` and `loopCount`, the keys `.jxl` takes: an animated PNG (APNG) from the
visible top-level layers, full color only, so `animate` with `pngColors: "palette"` throws
([web-export.md](web-export.md)). Permanent identifiers: the three keys for `.png`. Pinned by
`ui_script_export_as_png_animation`.
2026-10-27 (additive): theme scripts' `patchy.theme.startLoop(loop, intro)` and `stopLoop(outro)`: one
looping package WAV after an optional intro, stopped at once with an optional outro. Permanent
identifiers. Pinned by `ui_theme_script_loops_a_sound` and `ui_theme_wav_loop_helpers_and_player`.
2026-10-27 (additive): theme events `stroke-start` and `stroke-end` (payload `{tool}`) for a painting
stroke on the canvas, also usable as theme.json `"events"` keys and `sounds/<id>.wav` names. Permanent
identifiers. Pinned by `ui_theme_script_hears_brush_strokes` and
`ui_theme_sound_events_resolve_from_the_package`.
2026-10-26 (additive): the theme script API, a separate engine for theme packages' `scripts/main.js`
(`scripts/bundled/patchy-theme.d.ts`; [theme-files-scripts.md](theme-files-scripts.md#theme-scripts)): `patchy.theme`
`id`, `name`, `on`, `playSound`, `playEffect`, `showStatus`, `addCommand`, `setToolCursor`,
`showSplash`, `setPanelBackground`, `log`, plus the timer functions and `console`. User scripts do not
see it, and theme scripts see nothing else. Permanent identifiers: those names, the event ids, the
effect names and option keys, the panel ids and the `theme.<theme id>.<command id>` hotkey ids. Pinned
by `ui_theme_script_engine_is_isolated` and `ui_theme_menu_sits_right_of_help_with_hotkeys`.
2026-10-26 (behavioral): `patchy.ui.toolPalette.showGearButton` is a no-op: the tool palette's gear
was removed (Customize Tool Palette opens from the Window menu, the palette's right-click menu and
Preferences). It always reads false and setting it is ignored; the key stays (a permanent identifier).
Pinned by `ui_script_tool_palette_reads_and_sets`.
2026-10-26 (additive): `doc.encodeForWeb(format, options)` returns `{bytes, width, height}`
(`bytes` an ArrayBuffer) for `format` `"jpeg"`, `"webp"`, `"png"`, `"gif"`, `"avif"` or `"jxl"`
without writing a file; `options` are the `exportAs` keys of that format, strictly parsed
([web-export.md](web-export.md)). An unknown format, an unknown key or a bad value throws.
Permanent identifiers: the method name, the six format tokens and the result keys. Pinned by
`ui_script_encode_for_web`.
2026-10-26 (additive): text runs (`addTextLayer`, `setTextRuns`) take the Character panel styles
(GitHub issue 346, [text-character-styles.md](text-character-styles.md)): `caps` (`"all"`, `"small"`,
`"none"`), `script` (`"superscript"`, `"subscript"`, `"none"`), and booleans `underline`,
`strikethrough`, `ordinals`, `fractions`; an unset key keeps the layer's. `textRuns` reads all six
back. Another `caps` or `script` value throws. Permanent identifiers: those keys and values. Pinned by
`ui_text_underline_and_strikethrough_match_photoshop_if_available` and its siblings in
tests/ui/text_character_styles_tests.cpp.
2026-10-26 (additive): `doc.saveAs`/`doc.exportAs` options `colors` (2..256), `dither` (`"none"`,
`"diffusion"`) and `ditherAmount` (0..100) for `.gif`, and for `.png` with the new `pngColors`
(`"full"`, the default, or `"palette"`: an indexed PNG). Defaults keep the old bytes; the palette
keys on a full-color PNG, other extensions or bad values throw ([web-export.md](web-export.md)).
Permanent identifiers: the four keys and their tokens. Pinned by `ui_script_export_as_palette_keys`.
2026-10-27 (additive): `patchy.ui.toolPalette.hiddenLayerButtons` knows the optional footer buttons `move_up`, `move_down` and `merge_down` (GitHub issue 470), listed as hidden by default; an assigned list is the whole hidden set, so one without them shows them. Layer > Arrange > Bring to Front and Send to Back run through `app.runCommand("layer.bring_to_front")` and `"layer.send_to_back"` (468). Permanent identifiers: the three ids and both command ids. Pinned by `ui_layer_footer_optional_buttons_start_hidden`.
2026-10-26 (additive): `patchy.ui.toolPalette` reads and sets Customize Tool Palette (GitHub issues
213, 316 and 199; [tool-palette.md](tool-palette.md#customizing-the-palette)): `{hiddenTools, layout,
wheelScroll, showGearButton, hiddenLayerButtons}`, any subset on assignment, applied live and saved;
unknown ids or layouts throw. Permanent identifiers: the property and its keys, the layouts
`oneColumn` and `twoColumns`, the tool hotkey ids and the layer button ids. Pinned by
`ui_script_tool_palette_reads_and_sets`.
2026-10-26 (additive): `doc.saveAs`/`doc.exportAs` options for `.jpg`/`.jpeg` take `progressive`
(progressive scans), `optimizeCoding` (optimized Huffman tables), both default false so saves keep
their bytes, and `thumbnail` (the EXIF IFD1 thumbnail, default true), for File > Export for Web
([web-export.md](web-export.md)). Wrong types or other extensions throw. Permanent identifiers: the
three keys. Pinned by `ui_script_export_as_jpeg_coding_options`.
2026-10-26 (additive): `doc.trim` takes `tolerance` (0..255, default 0 = the exact match it always
made; GitHub issue 297): Transparent Pixels then cuts alpha up to it, a corner color every pixel whose
channels (alpha included) are all within it of the corner, in 8-bit units at every depth. Out of range
or not a number throws. Permanent identifiers: the option key `tolerance` and settings key
`trim/tolerance`. Pinned by `ui_script_trim` and `ui_trim_tolerance_cuts_near_matches`.
2026-10-25 (additive): `layer.applyCameraRawFilter(settings)` runs Filter > Camera Raw Filter (GitHub
issue 392, [camera-raw.md](camera-raw.md#camera-raw-filter)) on an RGB pixel layer at its depth, limited
to the selection like `applyFilter`. Permanent setting names: `temperature`, `tint` (-100..100,
relative), `exposure` (-5..5 EV), `contrast`, `highlights`, `shadows`, `saturation`, `vibrance`
(-100..100); missing ones are neutral, out-of-range numbers clamp, unknown names or non-numbers throw.
Pinned by `ui_camera_raw_filter_script_and_selection`.
2026-10-25 (additive): blend mode ids `soft-light-svg`, `additive-subtractive`, `arc-tangent`,
`geometric-mean`, `allanon`, `parallel`, `hard-overlay`, `interpolation` and `easy-dodge` (Krita's
modes, for .kra files, [kra.md](kra.md)), appended in that order after GIMP's to the permanent list
for `layer.blendMode` and stroke `mode`. 8 and 16-bit documents only; a PSD save writes them as
`normal` or bakes them. Pinned by `ui_script_krita_blend_mode_ids`.
2026-10-25 (additive): blend mode ids `grain-extract`, `grain-merge`, `hsv-hue`, `hsv-saturation`,
`hsl-color`, `hsv-value`, `lch-hue`, `lch-chroma`, `lch-color`, `lch-lightness`, `luminance`,
`luma-darken-only`, `luma-lighten-only`, `linear-multiply`, `linear-addition`, `linear-subtract` and
`linear-divide` (GIMP's modes, for .xcf files, [xcf.md](xcf.md)), appended in that order to the
permanent list for `layer.blendMode` and stroke `mode`. 8 and 16-bit documents only; a PSD save writes
them as `normal` or bakes them. Pinned by `ui_script_gimp_blend_mode_ids`.
2026-10-25 (additive): `app.open` and the other path-taking calls open GIMP `.xcf` files (also
`.xcf.gz`, `.xcf.bz2`, `.xcf.xz`, `.xcfgz`, `.xcfbz2`, `.xcfxz`) as layered documents, read-only like
`.pdn`; `doc.importNotes` lists what was approximated ([xcf.md](xcf.md)).
2026-10-25 (behavioral): `layer.transform` on a pixel layer while the document has a selection
transforms only the selected pixels, as Free Transform now does (GitHub issue 399,
[free-transform-targets.md](free-transform-targets.md#transforming-the-selected-pixels-github-issue-399)): the box
is the selection bounds clipped to the layer's pixels, the transformed pixels land back on the
layer, and the selection follows them. Without a selection, and on text, shape and smart object
layers, it transforms the whole layer as before. Pinned by
`ui_free_transform_selection_on_background_and_script`.
2026-10-25 (additive): save option `unsupportedBlendModes` for `.psd`, `.psb`, layered `.tif`/`.tiff`
and `.aseprite`/`.ase` (GitHub issue 379, [blend-modes.md](blend-modes.md#layered-saves)): `"normal"`
(the default, as before) writes layers in blend modes the format cannot store as Normal; `"bake"`
rebuilds each as a Normal layer that keeps the look. Scripted saves never show the Save prompt that
asks this. The key and its tokens are permanent identifiers. Pinned by
`ui_script_save_unsupported_blend_modes_option`.
2026-10-25 (additive): `doc.importNotes` (read only, string array): the notes the file's reader left
when the document opened (dropped or approximated features, the import notes the status bar shows),
empty for new documents. Permanent identifier: the property name. Pinned by
`ui_script_import_notes_list_reader_notices`.

2026-10-24 (additive): `doc.saveAs` and `doc.exportAs` write OpenRaster for a `.ora` path
([ora.md](ora.md)): the layer tree stays, and what the format cannot hold is baked into layer pixels
with no prompt (a scripted save answers it, like the Aseprite Fill Opacity one). `saveAs` points the
document at the file, as for `.psd`. The extension token `ora` is a permanent identifier. No save
options apply to it. Pinned by
`ora_round_trip_keeps_layers_and_pixels` and `ui_ora_save_warns_about_baked_content_and_stays_on_ora`.
2026-10-24 (additive): blend mode ids `reflect`, `glow`, `negation` and `xor` (Paint.NET's modes,
GitHub issue 379), appended to the permanent list for `layer.blendMode` and stroke `mode`. 8 and
16-bit documents only; a PSD save writes them as `normal`. Pinned by
`ui_script_paint_dot_net_blend_mode_ids`.

2026-10-24 (behavioral): `layer.redEye` uses the revised Red Eye method ([red-eye.md](red-eye.md)): the
area is grown with hysteresis and its rim and glint halo corrected by redness, and Darken Amount keeps
a quarter of the brightness at 100 instead of going black (Darken 50 keeps about 60%). Options and
result keys are unchanged. Pinned by `ui_script_red_eye_corrects_pupil`.

2026-10-24 (additive): the Color panel (Window > Color). `patchy.ui.expandPanel`,
`collapsePanel`, `closePanel` and `panelState` take the panel name `color` (d.ts
`PatchyPanelName`), a permanent identifier mapped to the panel id `colorDock`. Pinned by
`ui_color_panel_window_menu_and_script_name`.

2026-10-24 (additive): `doc.exportAnimatedPng(path, options?)` writes the visible top-level layers
as an animated PNG (APNG), like `exportAnimatedWebp`: options `frameDelayMs` and `loopCount`, a
`.png` or `.apng` path, document path and modified state unchanged. The loop count is kept in
document metadata `apng.loop_count` (also set when an animated PNG opens). Permanent identifiers:
the method and option names, the metadata key and command id `file.export_animated_png`. Pinned by
`ui_script_apng_animation_export_preserves_document_and_validates_options`.
2026-10-24 (additive): `app.newDocument(width, height, options?)` takes `name` (the tab title and
default Save As name, through the New Document dialog's file-name rule; throws when nothing usable is
left) and `colorMode` (`"rgb"` default, or `"cmykProof"`; throws for other values and without a CMYK
profile). Both are checked before the document is created. Permanent identifiers: the option keys and
the `colorMode` tokens ([new-document.md](new-document.md)). Pinned by `ui_script_new_document_options`.
2026-10-24 (additive): `layer.redEye({x, y, width?, height?, pupilSize?, darkenAmount?})`, the Red
Eye tool ([healing.md](healing.md#red-eye)): a click at `{x, y}` or a box, Pupil Size and Darken
Amount 1..100 (default 50), returns `{pupilPixels, x, y, width, height}`, throws when no red is found.
Permanent identifiers: the method and key names, hotkey command id `tools.red_eye` and the settings
keys `tools/redEyePupilSize`, `tools/redEyeDarkenAmount`. Pinned by `ui_script_red_eye_corrects_pupil`.
