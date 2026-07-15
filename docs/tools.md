# Shape tools, Merge Down, and tool icons

Small tool/command behaviors that don't have their own doc. Selection tools live in [selection-tools.md](selection-tools.md); brushes in [brushes.md](brushes.md); the text tool in [text-tool.md](text-tool.md).

## Shape tools (Rectangle/Ellipse/Line)

- Rectangle/ellipse honor brush Opacity/Soft through the signed-distance renderer in pixel_tools.cpp.
- Options-bar Style/Width/Height are deliberately session-only (mirrored via `current_shape_*`, never persisted).
- Alt = draw-from-center, so Rectangle/Ellipse are exempt from the Alt temporary-eyedropper (the two meanings fight).
- ALL constraint math lives in `CanvasWidget::shape_drag_rect()` — it intentionally duplicates `marquee_selection_rect()` (selection-only concerns, separately pinned). Do not merge them.
- The Fill command has its OWN persisted Opacity/Soft (`tools/fillOpacity`/`tools/fillSoftness`, default 100/0); tests laying down setup color call `use_solid_fill_settings(canvas)`.
- Coverage: `ui_shape_fill_and_corner_radius_apply_to_new_documents` also pins the `current_*` mirror pattern (see AGENTS.md gotchas).

## Merge Down

`MainWindow::merge_down()` flattens folders and any multi-selection into the bottom-most visible item, Photoshop-style. Hidden layers are discarded, never blocking (they must not abort the merge); cross-folder leaf merges may leave an emptied folder behind — intended. Coverage: `ui_merge_down_*`.

## Tool palette layout

- Built imperatively in `MainWindow::create_actions()` (main_window_actions.cpp); order is the construction order, in separator-delimited clusters: select (Move, Marquee, Lasso, Wand), paint (Brush, Eraser, Gradient+Fill), retouch (Stamp, Healing, Smudge, Dodge), draw/type (Shapes, Type), view (Pick, Hand, Zoom).
- Tools sharing a slot go through the shared `create_flyout_tool_action` + `configure_tool_flyout` lambdas (seven flyouts: marquee, lasso, wand, gradient+fill, stamp, smudge/detail, dodge/tone, shape). The flyout swaps the button's default action on each member action's `triggered`, which also covers hotkey activation.
- Every flyout button sets the `toolFlyout` Qt property; the app stylesheet keys the corner-triangle `::menu-indicator` and its sizing on `QToolButton[toolFlyout="true"]`, so a new flyout only needs the property (via `configure_tool_flyout`), not a new QSS selector. Coverage: `ui_stamp_and_gradient_flyouts_swap_tools`.
- Clone/Pattern Stamp (S / Shift+S) and Gradient/Fill (G / Shift+G) deliberately share slots like the other paired-shortcut groups; Clone and Gradient are the defaults.

## Tool palette icons

- Original SVGs at `src/ui/icons/tool-*.svg` (32x32, `#dce2eb` strokes, at most one non-load-bearing `#74c0ff` accent). Generic metaphors, no Photoshop geometry, so the license stays clean.
- QtSvg renders a Tiny-1.2 subset only, and a typo'd qrc alias renders EMPTY silently — review with `patchy_ui_visual_tests.exe ui_tool_palette_icons` (writes contact-sheet artifacts, checks per-icon coverage + pairwise distinctness).
- Clone keeps the classic rubber-stamp silhouette; Pattern Stamp is deliberately a different silhouette (stamp knob over a large checkerboard tile) because a small in-base detail was indistinguishable from Clone at the palette's 20px render size. Keep any future pair of related tools distinct in SILHOUETTE, not just in accent color.
