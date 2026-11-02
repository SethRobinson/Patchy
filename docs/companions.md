# Companion apps

A companion is a separate program, such as OpenFill (local generative fill through ComfyUI), that works on the document open in Patchy while the artist keeps using Patchy's own tools: select with Lasso or Object Selection, generate in the companion's panel, select again, and so on. Companions use the same channel as AI assistants: `patchy-mcp --attach` ([ai-control.md](ai-control.md)), a stdio JSON-RPC proxy into the running interactive Patchy. There is no in-process plug-in and no separate protocol.

## Talking to Patchy

- Spawn `patchy-mcp --attach` (on Linux `patchy --mcp-connector --attach` or the Flatpak's `patchy-mcp`), send `initialize`, then call tools. Python needs only `subprocess` and `json`.
- `get_state` reports documents, layer IDs, the active document and layer, selection bounds, history, and the `stateToken`. Attached sessions must pass the latest token as `expectedState` to every mutating tool; a stale one returns `stale_state`.
- `execute_script` runs JavaScript with the full `patchy.*` API ([scripting guide](../scripts/bundled/scripting-guide.md)). One run is one undo step per document it changes.
- Generation happens between requests, so nothing is locked while the companion works. The token covers selection geometry, so take a fresh `get_state` right before inserting a result: the artist may have reselected meanwhile. `busy` means the artist is mid-gesture, in a modal dialog, or another request is running; retry later. Never replay a request that returned `workspace_disconnected`.

## Pixel exchange

Large pixel blocks travel as image files. A script holds paths, never inlined arrays (script source and results are capped at 4 Mi characters). The calls are general scripting API, in `src/ui/script_pixel_io.cpp`:

| Call | Use |
|---|---|
| `doc.getCompositePixels(rect?)` | Flattened visible RGBA8 for a rect at full resolution (`flatten_document_rgba8_rect`, the flat-export pixels). Deep documents come back as 8-bit display values. |
| `doc.selection.getMask(rect?)` | Selection coverage, 1 byte per pixel, soft edges kept (`CanvasWidget::selection_coverage_buffer`). Null without a selection or rect. |
| `doc.selection.setMask(mask, {operation})` | Selection from coverage (`set_selection_from_coverage`), combined by replace, add (max), subtract (c × (255 − m)) or intersect (c × m), rounded integer math. All-zero coverage deselects. Feather and Anti-alias settings never apply. |
| `layer.setLayerMask(...)` | A raster `LayerMask` from a block or `{fromSelection, invert}`. `feather` and `density` are the mask's render-time Properties values, so the painted coverage stays crisp and PSD saves carry them as Photoshop does. Masks convert to the document's depth (`coverage_at_document_depth`). |
| `layer.getLayerMask()` / `removeLayerMask()` | Read back (8-bit) or delete. |
| `patchy.io.readImage` / `writeImage` | Decode any Qt-readable image to RGBA8; encode RGBA or a 1-byte mask as grayscale by extension, through `QSaveFile`. |

## Insert recipe

One `execute_script` call, so the result is one undo step and one Undo takes it back (pinned by `ui_script_layer_mask_companion_insert_is_one_undo_step`):

```js
var doc = app.getDocument(DOC_ID);
var result = doc.addLayer('Generative Fill: ' + PROMPT);
var px = patchy.io.readImage(RESULT_PNG);
px.x = RECT.x; px.y = RECT.y;
result.setPixels(px);
result.setLayerMask({fromSelection: true, feather: 4});
patchy.setResult({layer: result.id});
```

Variations go into `doc.addGroup` in the same run. Discard is `doc.undo()` (or the `undo` tool) while the history is as the companion left it; check `get_state` history first so an artist's later edit is never undone. Keep with one variation: undo, then insert only the chosen layer, in one script (undo must precede mutations in a run).

## Boundaries

- Patchy only exchanges pixels; the companion produces the content. A result is an ordinary pixel layer with a raster mask. Before integrating generated output into Smart Filter stacks or a one-click merge-visible operation, read the US 12530821 and US 8655112 notes in [legal-constraints.md](legal-constraints.md).
- Pixels cross at 8 bits per channel. A 16 or 32-bit document takes the RGBA8 result at its depth through `setPixels`.
