# Companion apps

A companion is a separate program, such as OpenFill (local generative fill through ComfyUI), that works on the document open in Patchy while the artist keeps using Patchy's own tools: select with Lasso or Object Selection, generate in the companion's panel, select again, and so on. Companions use the same channel as AI assistants: `patchy-mcp --attach` ([ai-control.md](ai-control.md)), a stdio JSON-RPC proxy into the running interactive Patchy. There is no in-process plug-in and no separate protocol.

## Talking to Patchy

- Spawn `patchy-mcp --attach` (on Linux `patchy --mcp-connector --attach` or the Flatpak's `patchy-mcp`), send `initialize`, then call tools. Python needs only `subprocess` and `json`.
- `get_state` reports documents, layer IDs, the active document and layer, selection bounds, history, and the `stateToken`. Attached sessions must pass the latest token as `expectedState` to every mutating tool; a stale one returns `stale_state`.
- `execute_script` runs JavaScript with the full `patchy.*` API ([scripting guide](../scripts/bundled/scripting-guide.md)). One run is one undo step per document it changes.
- Up to four clients attach at once, so a companion and an AI assistant can both be connected. Each has its own state token; while one client's script runs, the others get `busy`.
- Generation happens between requests, so nothing is locked while the companion works. The token covers selection geometry, so take a fresh `get_state` right before inserting a result: the artist may have reselected meanwhile. `busy` means the artist is mid-gesture, in a modal dialog, or another request is running; retry later. Never replay a request that returned `workspace_disconnected`.

## Protocol

The companion protocol is an integer version covering what companions rely on: the events, the manifest keys, the Companions menu commands (`show`, `quit`) and the pixel-exchange script calls. Version 1 is everything on this page. An addition gets a capability token in `get_info`; only a breaking change raises the maximum, and the minimum rises only when Patchy stops serving an old version (`kCompanionProtocolMin`/`Max`, `ui/mcp_session.hpp`). Compare protocol numbers, never Patchy's version: development builds report the last release's version.

- `get_info.companionProtocol` is `{min, max}` from the running Patchy. A Patchy without it predates companions (1.24 and older): tell the artist which Patchy release they need and stop.
- At `initialize` a companion declares `experimental["patchy/companion"]`: `{id, name, version, minProtocol, maxProtocol}` (a bare string is taken as the id). Patchy shows "*name* needs a newer version of Patchy" or "*name* is too old for this version of Patchy; update it" in the status bar when the ranges do not overlap, but still serves the connection: the companion makes the same comparison and stops.
- The `initialize` reply's `experimental["patchy/companion"]` is `{minProtocol, maxProtocol}`. `patchy-mcp` answers `initialize` before it reaches the window, so that is the connector's range; `get_info` is the window's.
- A manifest's optional `minProtocol` and `maxProtocol` (whole numbers from 1) grey out its menu entry, labelled "(needs a newer Patchy)" or "(needs an update)", when this Patchy serves no version in the range. A Patchy that predates companions has no menu and never reads the manifest, so the companion's own check is still needed.

## Pixel exchange

Large pixel blocks travel as image files. A script holds paths, never inlined arrays (script source and results are capped at 4 Mi characters). The calls are general scripting API, in `src/ui/script_pixel_io.cpp`:

| Call | Use |
|---|---|
| `doc.getCompositePixels(rect?)` | Flattened visible RGBA8 for a rect at full resolution (`flatten_document_rgba8_rect`, the flat-export pixels). Deep documents come back as 8-bit display values. |
| `doc.selection.getMask(rect?)` | Selection coverage, 1 byte per pixel, soft edges kept (`CanvasWidget::selection_coverage_buffer`). Null without a selection or rect. |
| `doc.selection.setMask(mask, {operation})` | Selection from coverage (`set_selection_from_coverage`), combined by replace, add (max), subtract (c × (255 − m)) or intersect (c × m), rounded integer math. All-zero coverage deselects. Feather and Anti-alias settings never apply. |
| `layer.setLayerMask(...)` | A raster `LayerMask` from a block or `{fromSelection, invert}`. `feather` and `density` are the mask's render-time Properties values, so the painted coverage stays crisp and PSD saves carry them as Photoshop does. Masks convert to the document's depth (`coverage_at_document_depth`). |
| `layer.getLayerMask()` / `removeLayerMask()` | Read back (8-bit) or delete. |
| `patchy.io.readImage(path, {grayscale})` / `writeImage` | Decode any Qt-readable image to RGBA8, or with `grayscale: true` to 1 byte per pixel (luminance, alpha ignored: a saved mask); encode RGBA or a 1-byte mask as grayscale by extension, through `QSaveFile`. |

## Insert recipe

One `execute_script` call, so the result is one undo step and one Undo takes it back (pinned by `ui_script_layer_mask_companion_insert_is_one_undo_step`). The mask comes from the job's own saved mask, not the current selection: the artist may have selected something else while the companion worked.

```js
var doc = app.getDocument(DOC_ID);
var result = doc.addLayer('Generative Fill: ' + PROMPT);
var px = patchy.io.readImage(RESULT_PNG);
px.x = RECT.x; px.y = RECT.y;
result.setPixels(px);
var mask = patchy.io.readImage(MASK_PNG, {grayscale: true});
mask.x = RECT.x; mask.y = RECT.y;
result.setLayerMask({x: mask.x, y: mask.y, width: mask.width, height: mask.height, data: mask.data, feather: 4});
patchy.setResult({layer: result.id});
```

Expand grows the canvas in the same run: `doc.resizeCanvas(W, H, {x: LEFT, y: TOP})` moves the content and the selection and extends the Background, then the result is inserted as above. `get_state` reports each layer's `hasLayerMask` and `layerMask` settings, so a companion can find its inserted layers without reading mask pixels.

Variations go into `doc.addGroup` in the same run. Discard is `doc.undo()` (or the `undo` tool) while the history is as the companion left it; check `get_state` history first so an artist's later edit is never undone. Keep with one variation: undo, then insert only the chosen layer, in one script (undo must precede mutations in a run).

## Events

A client opts in at `initialize` with `capabilities: {experimental: {"patchy/events": true}}` (the reply advertises `experimental["patchy/events"]`). It then receives `notifications/patchy/event` with `params.type`:

| Type | Fields | When |
|---|---|---|
| `selectionChanged` | `documentId`, `bounds` (`{x, y, width, height}` or null) | The active document's selection bounds change, or another document becomes active |
| `activeDocumentChanged` | `documentId` (null with no document) | Another tab becomes active |
| `documentClosed` | `documentId` | A document closes |
| `stateChanged` | `stateToken` | Anything the state token covers changed (history, layers, selection) |
| `companionCommand` | `companion`, `action` (`"show"` or `"quit"`) | The artist picked this companion, or its Quit entry, in Plugins > Companions |

`McpSession` polls on the UI thread: selection, documents and the active tab every 100 ms (no layer data), the state token every 500 ms (it serializes the layer tree), never during a script run. A selection drag reports once, when it commits. `companionCommand` reaches a client that declared `experimental["patchy/companion"]` (its manifest id) even without the event stream (`McpAttachment::send_companion_command`).

## Companions menu

Plugins > Companions (`companionsMenu`, `ui/companion_apps.*`, owned by the Plugins menu; not on the web) lists one entry per manifest in `<per-user app data>/companions/*.json`, read again each time the submenu opens, plus Open Companions Folder. Picking an entry sends `companionCommand` `show` to the attached companion with that id, and while one is attached a Quit entry (`companionQuitAction_<id>`) sends `quit`: the companion should exit as it does when Patchy closes; with none attached it starts the manifest's command, detached, in the manifest's folder (or `workingDirectory`), with `PATCHY_MCP_ENDPOINT` (this window's endpoint), `PATCHY_COMPANION_ID`, `PATCHY_EXECUTABLE` and `PATCHY_MCP` (the connector, when installed) added to the environment. Inside an AppImage both name the `.AppImage` file, never its per-launch mount: run `PATCHY_MCP` as `<file> patchy-mcp --attach`. A command never runs except from that menu pick. Inside the Flatpak, which cannot start host programs, an entry whose companion is not attached reads "(start it outside Patchy)" and is greyed out: the artist starts the companion (it reaches this Patchy through `flatpak run --command=patchy-mcp com.rtsoft.patchy --attach`, and its manifest goes in the Flatpak's data folder, `~/.var/app/com.rtsoft.patchy/data/RTsoft/Patchy/companions`), and once it attaches the entry and Quit work as elsewhere.

Manifest keys (a file-format contract, never renamed):

```json
{"id": "io.github.example.fill", "name": "Example Fill", "menuLabel": "Example Fill",
 "command": "bin/example-fill.exe", "args": ["--patchy"], "workingDirectory": "",
 "minProtocol": 1, "maxProtocol": 1}
```

`id` is reverse-DNS and unique (a duplicate is skipped); `name` and `command` are required; `menuLabel` defaults to `name`; `minProtocol` defaults to 1 and `maxProtocol` to no limit ([Protocol](#protocol)). `command` is absolute, relative to the manifest's folder when it contains a slash, or looked up on PATH. An installer writes the file; deleting it removes the entry.

## Boundaries

- Patchy only exchanges pixels; the companion produces the content. A result is an ordinary pixel layer with a raster mask. Before integrating generated output into Smart Filter stacks or a one-click merge-visible operation, read the US 12530821 and US 8655112 notes in [legal-constraints.md](legal-constraints.md).
- Pixels cross at 8 bits per channel. A 16 or 32-bit document takes the RGBA8 result at its depth through `setPixels`.
