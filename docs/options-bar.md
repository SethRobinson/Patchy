# Options bar layout

How the Options bar keeps one height and fits narrow windows (GitHub issue 91). Control conventions (row height, scrub labels, `configure_toolbar_spinbox`, unit fields) stay in [ui-conventions.md](ui-conventions.md).

## Grouping rules

Every tool's bar follows these rules; a new bar or control is placed by them (worked out on GitHub issues 284 to 292 with the reporter and Seth).

1. **Groups, in this order**: tool modes or presets | size fields | blending, strength or edge (Feather, Opacity) | the tool's own settings | checkbox group | model or advanced options | action buttons. A bar uses only the groups it has.
2. **One separator between neighboring groups**, never inside a group: a label and its field, a W/H pair with its swap or link button, and a value and the unit or slider beside it stay together.
3. **Checkboxes form one group** next to each other, after the value fields (issue 292). A checkbox that belongs to one field's meaning stays with that field: Anti-alias sits right after Feather on every selection bar, and checkboxes that follow it with no field between join that group (Object Selection: Feather, Anti-alias, Sample All Layers | Model; issue 292).
4. **Action buttons come last** (Remove Object, Apply/Cancel, Add Artboard).
5. **Never a separator first, last, or right after another one.** Register separators with `add_option_separator` for the tools whose groups they divide, and let hidden groups take care of themselves: `stray_option_separators` drops a separator that a tool, mode or style would leave leading, trailing or doubled.
6. **A label precedes its field** as one scrub zone ([ui-conventions.md](ui-conventions.md)), spelled out ("Tolerance", not "Tol"; issue 290). Short names only for paired size fields (W/H) and the swap or link between them.
7. **Icon buttons between two fields sit evenly**: the glyph is as far from the field before as from the label after (the swap button: 11 px each side, issue 286 follow-up).
8. **One row at 1366 px.** A separator costs 6 px (2 px line, 4 px spacing); when a row has no room, drop a separator before a control.

Example, the Brush bar: `Preset [combo] | Size [spin][slider] | Mode [combo] Opacity [spin] Soft [spin] Flow [spin] [Airbrush] | Smooth [spin][gear] | Tip [picker][dynamics]`. Example, the Patch bar: `[modes] | Patch [combo] | [Transparent] [Sample All Layers] | [Remove Object]`.

Coverage: `ui_options_bars_follow_the_separator_rules` walks every tool's bar (and the variants its style or mode combo switches to) in both schemes and fails on a leading, trailing or doubled separator or a wrapped row. Rules 1 to 4, 6 and 7 are review rules.

## One height per window width

- The bar never shrinks on a tool switch, because a height change there moves the tool palette, the canvas and the docks. Nor does it grow before it has to: `OptionsFlowContainer` reports the visible controls' wrapped height, kept sticky per width (the tallest any tool needed since the last width change), so a tool that fits one row never shows an empty second row, a tool that needs two grows the bar once and later tools keep it, and a resize resets the reservation to the current tool. Reserving every tool's tallest row in advance was the first attempt and showed an empty second row at widths where only one tool wrapped (GitHub issue 91).
- `refresh_options_bar` swaps control visibility under `set_updating(true)`: a layout pass in the middle can see the old and new controls together, and the reservation must not learn that transient height.
- `MainWindow::refresh_options_bar_rows` hands `FlowLayout` (`src/ui/options_flow_layout.hpp`) every tool's row: the widgets `refresh_options_bar` would show for that tool under the current vector mode, live shape layer and crop style. It rebuilds the rows only when that key changes. The rows drive the slider collapse below, which stays global so the bar looks the same from tool to tool.
- Hidden widgets are measured the way `QWidgetItem` would measure them shown, after `ensurePolished`. A widget added to the flow outside `register_option_action` is invisible to the measure and can still grow the bar.

## Selection tool rows

- Separators group the selection bars (GitHub issues 286, 290, 291, 292): mode buttons | Feather and Anti-alias | the tool's own fields. Quick Select leads with Size (it has no Anti-alias, like Photoshop's Quick Selection bar), the Magic Wand sets Tolerance apart from its checkboxes, Object Selection reads Feather, Anti-alias, Sample All Layers | Model (as the reporter asked), and the marquees set Radius and Style apart from the Width/Height pair. Order is construction order in `build_options_bar`, so a tool-specific control that must precede a shared one is built before it.
- Fixed Ratio and Fixed Size each have a swap button between Width and Height (`selectionRatioSwapButton`, `selectionFixedSizeSwapButton`); `marquee_fixed_size_option_widgets_` hides the pixel pair's button under Normal, whose fields are greyed. The Artboard bar's `artboardSwapSizeButton` is the same button (`make_swap_dimensions_button`): an 18 px options-bar icon button showing `swap-dimensions.svg`, two opposed arrows in icon ink only; the New Document dialog's `newDocumentSwapDimensionsButton` shows the same glyph. The glyph sits evenly between the Width field's edge and the Height label's text (11 px each side): the button carries a 2 px left margin and the label after it drops its text padding (`label_after_swap_button`). The color well's `swap-colors` glyph carries swatches and is not for this. The marquee style is part of `refresh_options_bar_rows`' key. Coverage: `ui_selection_options_bars_group_their_controls`, `ui_marquee_swap_button_swaps_width_and_height`, `ui_swap_dimension_buttons_show_a_themed_full_size_glyph`, `ui_swap_dimension_buttons_sit_evenly_between_width_and_height`.

## Duplicate sliders drop first

- An inline slider that repeats the spin box beside it (the spin box already has the popup slider and the scrub label) carries `kOptionsBarCollapsibleProperty`. Today: Brush Size, Gradient Opacity, Quick Select Size, Fill Opacity and Soft. The brush family's Opacity and Soft have no inline slider at all (October 2026), like Flow and Smooth: the popup slider, a drag on the label or inside the field, and the number keys reach them.
- At a width where any row holding one would wrap with it, all of them hide, for every tool at once, so the bar looks the same from tool to tool. A resize across that width calls back into `refresh_options_bar`.
- Mark only true duplicates. Never mark a control that is the only way to reach a setting.

## Popups and the cursor

- A canvas cursor change made while a popup holds the pointer (a slider popup's quick value redraws the brush cursor) is applied by Qt to the whole main window, which then shows it over the options bar until the pointer enters another widget. `MainWindow::eventFilter` calls `refresh_cursor_under_pointer` (dialog_utils) after any `Qt::Popup` hides, so every popup is covered (GitHub issue 293, `ui_slider_popup_close_restores_cursor_under_pointer`).

## Text bar

- At 1366 px every tool row must fit on one line: `ui_options_bar_keeps_one_height_across_tools` checks it and prints each row's natural width. A new control on a long row (Text, Brush, Clone) needs that budget. Line (Shape mode) has no collapsible slider, so `ui_line_options_bar_fits_one_row_with_room_to_spare` keeps it 100 px under that width for wider desktop fonts; its arrowheads are glyph toggles, not checkboxes (GitHub issue 174).
- Text Size shows Photoshop's precision: at most two decimals, trailing zeros dropped ("12 pt", "14.45 pt"; `UnitSpinBox::set_trim_trailing_zeros`). Sizes are whole pixels underneath (`text_points_to_pixels`), so two decimals round-trip every pixel size below 7200 ppi. The automatic size compares against the value the field holds after rounding (`text_size_auto_points_`).
- A field whose displayed max is narrower than its realistic values sizes itself from `kToolbarSpinboxWidthSampleProperty` (Text Size: 888.88 pt).
- The font family combo's popup sizes itself; the style combo's list opens wider than the closed control.
- Paragraph direction is in the Paragraph panel with a mirror of the Vertical toggle, as in Photoshop; the bar keeps the toggle. The color swatch has no "Color:" label, and the align buttons are ragged-line glyphs (`text_alignment_icon`, columns for vertical type) with no "Align:" label. Option labels pad 5 px on the left only, controls sit 4 px apart, and options-bar checkboxes size to their text with a 58 px floor; the old widths were chip-era.
