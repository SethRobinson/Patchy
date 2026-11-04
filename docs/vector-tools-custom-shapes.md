# Custom shapes: library, Shapes Manager, picker, bundled pack

Split from [vector-tools.md](vector-tools.md): the Custom Shape tool's shape library (builtins, seeding, user entries), the Shapes Manager and the options-bar Shape picker, the bundled game-icons.net pack, and Photoshop .csh import.

## Custom shape library, Shapes Manager and picker

The CustomShapeLibrary (JSON sidecars in a `shapes` folder beside the settings
file, unit-box paths, v1 text codec) ships 17 builtins: ids shape.builtin.*
are append-only, geometry is code-authoritative (restore_default_shapes
rewrites drifted builtin sidecars, keeping user renames). Edit > Define New Preset > Define
Custom Shape from Path adds a user entry. Builtins seed once under the
`shapes/defaultShapesVersion` stamp (`kDefaultCustomShapesVersion`, each
builtin's `introduced_version`), so a deleted default stays deleted.
Edit > Presets > Shapes Manager (`request_custom_shape_manager`, GitHub
issue 486; the hotkey id stays `edit.presets_custom_shapes`) is the Pattern
Manager's shape twin: rename, folder, Move to New Folder, Rename Folder,
drag shapes onto a folder row (`set_drop_callback`; the
rows never reorder, the library sorts by folder and name, so there is no
manual order), duplicate, delete, Import of SVG files (`custom_shape_path_from_svg_file`, shared with
Define Custom Shape from SVG File) and Photoshop `.csh` files
(`CustomShapeLibrary::import_csh`: one user entry per shape, in a folder
named after the file, one `changed()`), Restore Default Shapes (re-adds and
resets builtin names and folders) and Use Shape, which picks it in the bar.
Every edit reaches the bar's picker at once through `changed()`. The bar's
Shape picker (`CustomShapePickerCombo`, custom_shape_picker.cpp) stays a
QComboBox for every caller, but its dropdown is a popup: a search field, the
library as a folder tree (folders open and close; the closed ones persist in
`tools/customShapePickerCollapsedFolders`, never rewritten while searching),
and a Shapes Manager... button that opens the manager on the current shape.
Both show the library through `PresetBrowserView` (preset_grid_widget.cpp):
List is the `PresetTreeWidget`, Grid (the default) a `PresetGridWidget`,
icon-only cells (name in the tooltip and accessible name) wrapping under
full-width folder header rows that open and close from their arrow, a
double-click, Enter or Left/Right (in the picker any click on a header). The
grid matches the tree's selection (a selected header is its folder's shapes),
filter, collapse memory and drop-onto-folder rules, and lists unfiled shapes
before the folders. Switching keeps the current shape, the closed folders and
the search; the view persists in `tools/customShapeManagerGridView` and
`tools/customShapePickerGridView`. The picker popup resizes from a corner
`VisibleSizeGrip` (the Brush Tip and Font pickers' grip; a frameless Qt::Popup
has no border to drag) and the grid reflows to the width; closing stores the size
in `tools/customShapePickerSize`, and each opening clamps it to the minimum and
the screen's available geometry (`custom_shape_picker_popup_size`, also the
popup's maximum).

## Bundled game-icons.net shapes

GitHub #521: 878 curated shapes from game-icons.net (CC BY 3.0; Viscious Speed and Zeromancer
CC0), all drawn by people, ship inside the app as the Qt resource
`:/shapes/game-icons.json` (`src/ui/shapes.qrc`, compressed by rcc), so the installer, macOS
bundle, Flatpak, AppImage and web build carry them with no file to copy. The pick is
`scripts/dev/gameicons/game_icons_manifest.tsv` (id, source, author, folder, name; edit it by
hand, never change an id) and `curate_game_icons.py` (`select` was the first pick from the
site's tags; `extend` applies its REMOVED, MOVES and ADDITIONS lists to the manifest; `build`
writes the pack from a clone at the pinned commit). Ids `shape.gameicons.<author>.<file>` are
permanent and append-only (a removed id was never pushed). Its arrows and signs join the builtin Arrows and Symbols folders (one folder per name in
the picker, already translated, still open by default); cards, dice, chess and tarot are in Games. Rules: credited authors only (badges/ and various-artists/ name
none), no shape tied to another company's property, brand or logo, no gore or game-UI glyphs,
thinned variant runs, nothing too fine for a thumbnail, and only outlines that fill the same
even-odd as nonzero (custom shapes fill even-odd).
`CustomShapeLibrary::ensure_bundled_shapes_loaded` adds them, read-only (`bundled`, no
sidecar: no rename, move or delete, and a folder holding one cannot be renamed; Duplicate
makes a user copy), the first time the picker,
the Shapes Manager or a script asks (`MainWindow::custom_shape_library_with_bundled`; a
remembered `tools/customShapeId` from the pack loads it at start). Paths are SVG path data
parsed by `svg::parse_svg_path_data`, one even-odd group, normalized to the unit box.
Thumbnails draw on first paint (`custom_shape_icon`, a caching icon engine), so only visible
cells cost anything. The 16 folders only the pack fills start closed in the picker (never-toggled default,
plus a one-time merge into an existing `tools/customShapePickerCollapsedFolders`) and the
manager. Folder names are translated (`bundled_custom_shape_folders`); shape names stay the
authors' English titles (878 names in 11 languages is out of proportion; search runs on them).
Credits (binding, docs/legal-licensing.md): the author and license in every tooltip
(`custom_shape_tool_tip`), the authors in the Shapes Manager (`customShapeManagerCreditLabel`)
and `NOTICE-THIRD-PARTY.md`. A stamped shape is a plain path in documents and PSDs. Tests:
`ui_custom_shape_bundled_pack_loads_on_request`, `ui_custom_shape_picker_opens_with_the_bundled_pack`.

## Photoshop .csh files

`psd/csh_io.cpp` (`read_csh`) reads them, written from files only (two CC0
packs and a Photoshop 2026 load-and-draw check, October 2026; never from
another reader's source). Big-endian: `cush`, u32 version 2, u32 shape
count; per shape a u32 UTF-16 unit count and the name (terminator
included), zero padding to a 4-byte file offset, a u32 that is always 1,
and a u32 block length. The block: a Pascal id string (a GUID or zeros),
four i32 bounds (top, left, bottom, right; the defining document's pixels),
then the PSD path-resource record stream (`parse_path_resource_records`,
points as 8.24 fractions of the bounds box), zero padded to a 4-byte file
offset. Cut-outs are one compound group (lead op Add, continuation
contours op 0xFFFF on the same group), filled even-odd. Photoshop draws a
shape by fitting the outline's exact curve bounds to the drag rectangle
(Aries, 1128 x 690 px: first anchor at 927.76, 541.91), so the reader
normalizes by curve bounds, not control points. A damaged shape is skipped
with a warning; a damaged tail keeps the shapes before it. No .csh writer
yet: the builtins could be written in this layout as Patchy's own file.
Tests: `csh_reader_*` (core; the CC0 packs in `local-test-fixtures/csh`)
and `ui_custom_shape_csh_import_reads_unicode_paths_and_stamps_holes`.
