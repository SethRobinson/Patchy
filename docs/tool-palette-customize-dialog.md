# Customize Tool Palette dialog

Open **Window > Customize Tool Palette** (`window.customize_tool_palette`, no default shortcut), use the palette's right-click menu, or open it from Preferences > Application. The Layers footer menu and the Layers panel settings gear open the **Layers Panel** tab. Right-clicking the panel title opens the panel list. [Layers panel settings gear](layer-thumbnails.md#layers-panel-settings-gear).

Only one non-modal `toolPaletteCustomizeDialog` opens at a time. A second request raises it on the requested tab. When opened from Preferences it closes with Preferences. The dialog fits the available screen and remembers a user resize in `toolPalette/customizeDialogSize`. Each combo opens wide enough for its longest translated entry.

## Tools tab

- **Tool list.** Show or hide individual tools. Shortcut keys align left. `ShortcutColumnDelegate` reserves 12 px on the right for the scrollbar. The group expand arrow aligns with the heading text. Both the headings and shortcut keys use the current theme's `text_secondary` color when painted, including after a theme switch.
- **Layout.** Choose the columns, grouping, separators and wheel scrolling. Hiding separators also removes the gaps between groups, so in two columns the next group fills a partly used row. The options to show scroll arrows and the arrow for extra columns are available while wheel scrolling is enabled.
- **Buttons and footer.** Choose the toolbar icon size, how tool groups open, and whether to show the color swatches, Quick Mask and screen mode controls. Tool order is fixed.
- **Preview.** The preview uses the chosen icon size, draws the flyout corner on grouped tools and centers the footer under two columns. It shows as many complete rows as its height allows, followed by the visible footer. When it omits rows, a torn gap between the rows and footer uses the theme's `tool_palette_separator`. The properties `toolRows`, `shownToolRows` and `stripHeight` expose what was drawn.

The palette starts with Move. Keep the retired setting `toolPalette/showCustomizeButton` reserved. The scripting property `patchy.ui.toolPalette.showGearButton` remains accepted and ignored.

## Layers Panel tab

- **Buttons.** Choose the footer icon size, show or hide buttons, and reorder them with the up and down buttons or by dragging between rows. The controls disable moves beyond either end of the list.
- **Presets.** Patchy uses its default order and visibility. Adobe Photoshop, Affinity Photo, GIMP and Krita order and hide Patchy's own buttons. Selecting Custom keeps the current arrangement. After each edit, `matching_layer_footer_preset` compares the visible buttons in order and shows the matching preset or Custom. Hidden buttons' positions do not affect the match. There is no separate stored preset.
- **Rows.** Set compact rows, row tooltips, the Fill field, row color, thumbnail size and thumbnail display. [Layers panel row options](layer-panel-row-options.md).

Keep the other editors' presets. They use only buttons and original icons that Patchy supplies. The Adobe Photoshop preset starts with Link Layers. Preserve the product names in translations. [Trademark and asset rules](legal-licensing.md#format-copyright-trademarks-and-assets).

## Applying and testing changes

OK applies and saves both tabs. Cancel discards the dialog edits. Reset to Default restores both tabs, including the footer order and row options. The toolbar icon size and group-opening behavior are shared Preferences settings. Both dialogs apply them through `MainWindow::apply_tool_palette_preferences`, which synchronizes any open controls. Scripts can read and set the customization through `patchy.ui.toolPalette`. [Settings and compatibility](tool-palette.md#customizing-the-palette).

The dialog and preview live in `src/ui/tool_palette_customization.cpp`. `ToolPaletteLayout` builds the actual palette. Coverage includes:

- Entry points, sizing and preview: `ui_layers_panel_gear_opens_layer_button_customize`, `ui_tool_palette_customize_combos_show_their_entries`, `ui_tool_palette_footer_items_hide_and_preview`, `ui_tool_palette_customize_preview_fits_its_height`.
- Footer ordering and presets: `ui_tool_palette_dialog_reorders_layer_buttons`, `ui_layer_footer_presets_set_order_and_shown_buttons`, `ui_tool_palette_dialog_layer_button_presets`.
