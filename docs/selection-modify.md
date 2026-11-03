# Select > Smooth and Select > Feather

Two Select menu commands beside Expand, Contract and Border (GitHub issue 614). Both act on the
selection's 8-bit coverage, so they work the same at every document bit depth and on soft
selections, and each is one undo step (`CanvasWidget::run_selection_command`).

| | Smooth | Feather |
| --- | --- | --- |
| Action, hotkey id | `selectSmoothAction`, `select.smooth` (no default key) | `selectFeatherAction`, `select.feather` (Shift+F6) |
| Dialog, spin box | `patchySmoothSelectionDialog`, `smoothSelectionRadiusSpin` | `patchyFeatherSelectionDialog`, `featherSelectionRadiusSpin` |
| Radius | 1 to 500 px, whole pixels, default 1 | 0.1 to 1000 px, one decimal, default 5 |
| Last radius (settings key) | `tools/selectionSmoothRadius` | `tools/selectionFeatherRadius` |
| Script | `doc.selection.smooth(radius)` | `doc.selection.feather(radius)` |

The dialogs live in `src/ui/selection_modify_dialogs.*` (`request_selection_smooth_radius`,
`request_selection_feather_radius`): Up/Down step one step, Shift+Up/Down ten (Ctrl does too,
through Qt's own step modifier). MainWindow refuses both commands without a selection before
any dialog opens, and the Select menu grays them (with Transform Selection) while it is open.
Both stay enabled under a filter dialog's selection session ([selection-tools.md](selection-tools.md#selection-edits-under-a-filter-dialog)).

## Algorithms (`src/core/selection_modify.*`)

Integer math only, cost O(area) for any radius.

- **Smooth** (`smooth_selection_mask`) is a majority vote over a (2r + 1) square window: the
  window's mean coverage, pushed away from 50% by a gain of 2r + 1, so a straight edge stays hard
  and in place while corners round off and specks and holes narrower than about the radius go.
  Curved edges come out anti-aliased (the gain leaves a one-pixel ramp); a feathered edge wider
  than the window steepens. The window is clipped to the canvas, so Select All stays whole and a
  selection keeps any canvas edge it touches. The work area is the selection bounds grown by r;
  row prefix sums and one row of 32-bit column sums keep memory to a row.
- **Feather** (`feather_selection_mask`) is three horizontal and vertical box passes of radius
  `selection_feather_pass_radius(r)`: (r + 1) / 2 as a real number, r itself below 1 px. At odd
  radii that is the marquee Feather option's pass radius; even radii blur half a pixel wider
  than the marquee option, which rounds down. A fractional pass radius weights the two outer
  taps (fixed point, 1/256). Coverage outside the canvas counts as unselected, like the
  marquee feather; the work area is the bounds grown by `selection_feather_padding(r)`.

Tests: core `selection_smooth_*`, `selection_feather_softens_by_radius`; UI
`ui_select_smooth_and_feather_commands_are_one_undo_step`, `ui_script_selection_smooth_and_feather`
(16-bit document), and both dialogs in `ui_offscreen_dialog_sweep_is_themed_in_both_schemes`.
