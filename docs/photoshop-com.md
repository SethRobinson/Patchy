# Driving Photoshop over COM

Canonical COM workflow for capturing and verifying against Adobe Photoshop (PS 2026, the ground truth). The PSD write rules those captures pin live in [ps-compat.md](ps-compat.md). Driving Photoshop over COM is always authorized; clicking its dialogs is desktop automation and needs Seth's explicit permission.

Drive PS from PowerShell: `(New-Object -ComObject Photoshop.Application).DoJavaScript($jsx)`.

- Learn an encoding: save two PSDs differing in one UI toggle and byte-diff (mask flags bit 0 = unlinked; "use global light" = `uglg` + resources 1037/1049, psd_document_io.cpp).
- Read back interpretation with Action Manager getters (`executeActionGet` on a layer reference). Compare renders by diffing PS's exported flatten against `Compositor::flatten_rgb8`.
- `doc.saveAs`/`duplicate` fail with a fake "disk error (-1)" when smart-object layers reference missing global 'lnk2' data; workaround: selectAll, `selection.copy(true)`, paste into a fresh document, flatten, save BMP. `doc.colorSamplers` probes pixels (max 4; add/read/remove in a loop) but reads stale values on unflattened documents; sample a flattened duplicate.
- Hygiene: `app.displayDialogs = DialogModes.NO`; close only documents you opened, with `SaveOptions.DONOTSAVECHANGES`; set `rulerUnits = Units.PIXELS` before coordinate APIs; never name a JSX top-level variable `name` or `fonts` (read-only app globals, silent failure).
- Scratch tools that link Patchy's release libs (flattening a PSD through the reader outside the suites): see [testing.md](testing.md).
- Unknown-data prompt checks: `& scripts\dev\photoshop-open-check.ps1 -Files a.psd, b.psd` opens
  each file over COM while a watcher polls Photoshop's windows for the `PSDialogBox` whose text
  says "unknown data", printing CLEAN or UNKNOWN-DATA per file; `-InventoryDir` records what
  Photoshop kept after Keep Layers (a discarded fill layer comes back as an empty NORMAL layer).
  `-DismissUnknownData` clicks Keep Layers, which is desktop UI automation and needs Seth's
  explicit permission for the session; reading window text needs none.
- Opening checks: `DialogModes.ALL` can show the file picker even with an explicit `app.open(File(...))` argument; complete that picker for warning-enabled validation. `DialogModes.ERROR` opens without the picker and still surfaces error alerts (a COM call that returns promptly saw none). `DialogModes.NO` can still block on a corrupt-layer composite-fallback prompt; a pending COM call is not proof that Photoshop is still loading.
- **`maximizeCompatibility = true` is silently overridden by the app-level File Handling preference** (Never on this machine), leaving a fake all-white merged composite. Fixture scripts must force `queryAlways` via the `fileSavePrefs` descriptor and restore it.
- Headless one-script PSDs embed a STALE maximize-compat composite; re-saving does not fix it. Render fixtures pin PS's flatten through the sibling BMP (duplicate + flatten + BMP), never the embedded composite.
- `photoshop-saved-channels.psd`: resource 1053 identifies alpha channels only ([channels.md](channels.md)).
