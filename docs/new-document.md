# New Document

File > New (`MainWindow::create_new_document`, `src/ui/main_window_document_dialogs.cpp`)
opens the dialog in `src/ui/new_document_dialog.cpp` (`request_new_document_settings`) and
builds the document through `reset_document`; a Clipboard card goes through
`create_clipboard_document`, the Artboards check through the artboard branch of
`create_new_document` ([artboards.md](artboards.md)).

## Persisted identifiers

Preset ids (the Last Used card's `last-used` included, the `user-N` ids of saved presets)
and the `newDocument/` keys are compatibility contracts, append-only: `lastPresetId`,
`lastWidth`, `lastHeight`, `lastPpi`, `lastBackground`, `lastBitsPerChannel`, `lastUnit`,
`lastResolutionUnit`, `artboards`, `lastCategory`, `recent`, `userPresets`,
`addEmptyLayer`. The JSON field names of `recent` and `userPresets` (`w`, `h`, `ppi`,
`unit`, `resolutionUnit`, `background`, `bits`, `artboards`, `colorMode`, and `id`, `name`
for presets) and the `colorMode` tokens `rgb` and `cmykProof` are contracts too; a missing
`colorMode` reads as `rgb`, so entries from before GitHub issue 360 load unchanged.

## Templates, Recent row and search

Layout (GitHub issues 342 and 497): one row across the top holds the search field (160 to
400 px, it gives up width first) and the group chips (a fixed-size `QHBoxLayout` row in
`newDocumentChipRow` that never wraps: chips that do not fit beside the narrowest search field,
a longer translation or a larger font, widen the dialog); below it the template list frame and
the settings pane start at the same top. The settings labels are left aligned and end in a
colon; the summary (template name, size, memory) starts at the fields' left edge, with Save
in the label column to its left, so the Create/Cancel row never widens the column.
`ui_new_document_dialog_fits_short_screens` pins the top row, the five columns, every group
unscrolled and the dialog's height; `ui_new_document_layout_lines_up_labels_fields_and_summary`
pins the one-row chips (Saved chip shown) at the opening and minimum widths, the label edge,
the summary and Save placement.

A Recent row (`newDocumentRecentList`) is the top section of the framed list area
(`newDocumentListFrame`, GitHub issue 342), above a divider, a caption naming the group or
"Search results" (`newDocumentGroupLabel`) and the group's grid, on every group and through a
search: Clipboard while the clipboard holds an image, then the last creates newest first,
deduplicated, as many as one row fits (refit on resize). They are stored in
`newDocument/recent` (JSON, up to 8; seeded from the old `last*` keys until it exists); the
newest card keeps the `last-used` id, older ones `recent-N`, and each restores its create
(size, resolution, units, background, bit depth, Artboards, color mode). The dialog opens on
the newest Recent card (every create, a Clipboard create's size and PPI included, leads the
row) or on Default before the first create; a clipboard image never preselects the Clipboard
card. It opens in the group last picked (`newDocument/lastCategory`, written on every chip
pick, create or cancel; tokens `screen`, `mobile`, `social`, `web`, `print`, `photo`, `icons`,
`saved` from `preset_category_token`), else, or for an unknown token or an empty Saved group,
in the selected card's group. `lastPresetId` is written only for older builds, which restore
from it.

Screen presets and `reset_document` default to 72 PPI; print presets use 300 PPI. Templates
sit in seven groups (`kPresetCategories`; at most 12 each, so a group fits the five-column,
three-row grid unscrolled; GitHub issues 222 and 342); a template keeps its id when it moves
group. The search field (`newDocumentSearchEdit`, issue 223) matches every word against name
and group name across all groups, saved presets included (a word with a digit also against
the size, so "x" alone matches no size), drops Clipboard and Last Used and the chip check
while text is in it, selects the first match for Return, and Escape or a chip click ends the
search. `artboard_size_presets` follows the same order and carries each template id, without
the saved presets; it splits Mobile into Phone and Tablet (a template's `tablet` flag, GitHub
issue 281) and the New Artboard dialog's combo puts a separator between groups.
`size_preset_matches_search` is the search rule both the dialog and the Artboard bar's picker
use.

## Template units (local-013)

Every template carries a display unit (`NewDocumentPreset::unit`): ISO paper (A6 to A2, A3+, the EU
business card) in mm, ANSI paper, the US business card, the poster and the inch photo prints in
inches, the 10 x 15 and 13 x 18 cm prints in cm, everything else (Photo 3:2 included) in
pixels. A card shows its size in its unit (`preset_size_text`, item role
`kNewDocumentPresetSizeTextRole`): the unit's spin decimals (`measurement_unit_decimals`) with
trailing zeros dropped, so whole-pixel templates at 300 PPI read their nominal size ("210 x 297
mm", "8.5 x 11 in"); `ui_new_document_templates_show_their_unit` pins every print and photo
label, so a new paper template whose pixels round to another figure fails there. Pixels stay the
truth: picking a template sets its pixels and PPI, then switches the W/H unit combo to its unit
(a pixel template back to Pixels). The opening card does not: it keeps the remembered unit, or
the ruler unit before a first create. Recent cards and saved presets show and restore the unit
they stored; Clipboard shows pixels and leaves the unit alone. Search also matches the shown
size ("210", "8.5", "210x297mm"; `size_preset_matches_search`'s `shown_size`). The summary's
printed-size line follows the unit combo, inches while it shows pixels.

## Name (GitHub issue 374)

`newDocumentNameEdit`, the first row, starts as "Untitled-N" (N = open documents + 1) and
is never remembered. Typed characters a Windows file name cannot hold (`\ / : * ? " < > |`
and control codes) are dropped as they arrive; on create, `sanitized_new_document_name` also
trims spaces and trailing dots, and an empty result falls back to the default. The name is
the session title, so it is the tab title and the base of Save As's suggested file name
(`Name.psd`). Unicode names stay as typed.

## Color Mode and Color Profile (GitHub issue 360)

`newDocumentColorModeCombo` offers RGB Color and CMYK Proof. CMYK Proof turns the new
session's `cmyk_proof` on through `set_session_cmyk_proof`, the switch Image > Mode > CMYK
Proof uses ([cmyk.md](cmyk.md)), after any bit-depth conversion. Under the formula (no CMYK
profile in Preferences) the CMYK Proof entry is disabled with a tooltip saying why. A
Clipboard card greys the list (clipboard documents open in RGB).

`newDocumentColorProfileLabel` names what the colors mean: `kSrgbProfileName` for RGB,
since Patchy edits and shows RGB as sRGB and an untagged save is sRGB
([rgb-profiles.md](rgb-profiles.md)); the Preferences CMYK working profile for CMYK Proof.
It is text, not a choice: saves do not tell "sRGB" from "no embedded profile" apart, and
other RGB profiles would only tag the file until real working spaces exist. Grayscale, Lab,
Bitmap and true CMYK document modes do not exist in Patchy; Indexed stays an Image > Mode
conversion.

## Saved presets (GitHub issue 374)

Save... (`newDocumentSavePresetButton`, accessible name "Save Preset", left of the summary) is enabled only while no
template and no saved preset holds the current settings: a template holds its size and
resolution with the plain settings (white background, 8 bits, RGB, no Artboards); a saved
preset holds everything but the units (`RecentDocumentSettings::same_document`). The Clipboard
card disables it. It asks for a name (`newDocumentSavePresetDialog`, default "W x H px"),
appends a `user-N` preset (N one above the highest existing) to `newDocument/userPresets`
(JSON, oldest first) and selects its card in the Saved group (`newDocumentSavedChip`, token
`saved`), which shows only while saved presets exist. A saved card restores every setting it
stored. Delete or Backspace on the grid (`newDocumentDeleteCurrentPresetAction`, a
widget-scoped shortcut, so the main window's Delete never sees it) or a saved card's context
menu (`newDocumentDeletePresetAction`) removes it without a prompt; templates cannot be deleted.

## Empty layer above the Background (GitHub issue 363)

`reset_document` adds the transparent "Paint Layer" above the Background unless told not
to; then the document is the Background alone, selected. The default is the Preferences
checkbox "Add an empty layer above the Background in new documents"
(`preferencesNewDocumentEmptyLayerCheck`, Application page, key `newDocument/addEmptyLayer`,
default on). The dialog's "Empty layer above Background" (`newDocumentEmptyLayerCheck`, above
Artboards) decides for one document: it opens at the preference every time, never writes it,
and is not stored in Recent cards or saved presets (it follows Preferences). It reaches
`reset_document` as `NewDocumentSettings::add_empty_layer`; callers that pass nothing (test
helpers, the stress test) follow the preference. It is greyed while Artboards is checked or
the Clipboard card is current: artboard documents replace the layers and clipboard documents
have no Background, and neither follows the setting. `app.newDocument` has no option for it:
script documents are a single Background regardless.

## Scripting

`app.newDocument(width, height, {name, colorMode})`: `name` goes through
`sanitized_new_document_name` (throws when nothing is left), `colorMode` is `"rgb"` or
`"cmykProof"` (throws otherwise, or without a CMYK profile); both are checked before the
document exists. Script documents keep their single white Background ([scripting.md](scripting.md),
[scripting-api-changes.md](scripting-api-changes.md)).

## Tests

UI: the `ui_new_document_*` family (templates, Recent row, search, units, bit depth,
clipboard), plus `ui_new_document_color_mode_creates_cmyk_proof`,
`ui_new_document_color_mode_needs_a_cmyk_profile`, `ui_new_document_empty_layer_preference`,
`ui_new_document_empty_layer_checkbox`,
`ui_new_document_name_reaches_tab_and_save_as` (Unicode name included),
`ui_new_document_save_preset_and_reload` and `ui_script_new_document_options`
(`tests/ui/new_document_options_tests.cpp`). `accept_new_document_dialog` (test support)
resets Artboards and the color mode so a Last Used card from an earlier test never leaks.
