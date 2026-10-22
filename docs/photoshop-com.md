# Driving Photoshop over COM

Canonical COM workflow for capturing and verifying against Adobe Photoshop (PS 2026, the ground truth). The PSD write rules those captures pin live in [ps-compat.md](ps-compat.md). Driving Photoshop over COM is always authorized; clicking its dialogs is desktop automation and needs Seth's explicit permission.

Drive PS from PowerShell: `(New-Object -ComObject Photoshop.Application).DoJavaScript($jsx)`.
With several versions installed, `Photoshop.Application` binds to the newest; the versioned ProgIDs pick one (`Photoshop.Application.190` = 2025, `.200` = 2026). COM needs the logged-on desktop session: from an ssh session it fails with CO_E_SERVER_EXEC_FAILURE.

- Learn an encoding: save two PSDs differing in one UI toggle and byte-diff (mask flags bit 0 = unlinked; "use global light" = `uglg` + resources 1037/1049, psd_document_io.cpp).
- Read back interpretation with Action Manager getters (`executeActionGet` on a layer reference). Compare renders by diffing PS's exported flatten against `Compositor::flatten_rgb8`.
- `doc.saveAs`/`duplicate` fail with a fake "disk error (-1)" when smart-object layers reference missing global 'lnk2' data; workaround: selectAll, `selection.copy(true)`, paste into a fresh document, flatten, save BMP. `doc.colorSamplers` probes pixels (max 4; add/read/remove in a loop) but reads stale values on unflattened documents; sample a flattened duplicate.
- **Never run a filter with an empty or partial descriptor.** A plug-in filter given an empty `ActionDescriptor` opens its settings dialog even under `DialogModes.NO`, which blocks the COM call until someone clicks it (Ripple, October 2026). Pass every key Photoshop's own recorded action carries; record one first when the key list is unknown.
- Hygiene: `app.displayDialogs = DialogModes.NO`; close only documents you opened, with `SaveOptions.DONOTSAVECHANGES`; set `rulerUnits = Units.PIXELS` before coordinate APIs; never name a JSX top-level variable `name` or `fonts` (read-only app globals, silent failure).
- Scratch tools that link Patchy's release libs (flattening a PSD through the reader outside the suites): see [testing.md](testing.md).
- Unknown-data prompt checks: `& scripts\dev\photoshop-open-check.ps1 -Files a.psd, b.psd` opens
  each file over COM while a watcher polls Photoshop's windows for the `PSDialogBox` whose text
  says "unknown data", printing CLEAN or UNKNOWN-DATA per file; `-InventoryDir` records what
  Photoshop kept after Keep Layers (a discarded fill layer comes back as an empty NORMAL layer).
  `-DismissUnknownData` clicks Keep Layers, which is desktop UI automation and needs Seth's
  explicit permission for the session; reading window text needs none.
- Flatten checks: `& scripts\dev\photoshop-flatten-check.ps1 -Files a.psd, b.psb -OutDir <dir>`
  opens each file with a watcher that logs ANY Photoshop dialog (CLEAN, DIALOG with its text,
  or ERROR), then saves Photoshop's flatten as `<name>-<ext>.ps-flat.psd`;
  `python scripts\dev\psd-merged-diff.py <patchy.psd> <ps-flat.psd> ...` compares merged
  images per channel (mattes merged transparency over white). The CMYK Proof PSD acceptance
  in [cmyk.md](cmyk.md) uses both.
- Opening checks: `DialogModes.ALL` can show the file picker even with an explicit `app.open(File(...))` argument; complete that picker for warning-enabled validation. `DialogModes.ERROR` opens without the picker and still surfaces error alerts (a COM call that returns promptly saw none). `DialogModes.NO` can still block on a corrupt-layer composite-fallback prompt; a pending COM call is not proof that Photoshop is still loading.
- **A crash in a long batch is not yet a file bug.** Over hundreds of opens in one Photoshop 2025 session, 8 of 324 files crashed it once and all 8 opened clean on a rerun in both 2025 and 2026 (October 2026). Reopen every crashing file in a fresh instance before blaming the file; a dialog, by contrast, reproduced every time.
- **`maximizeCompatibility = true` is silently overridden by the app-level File Handling preference** (set to Never on the reference install), leaving a fake all-white merged composite. Fixture scripts must force `queryAlways` via the `fileSavePrefs` descriptor and restore it.
- Headless one-script PSDs embed a STALE maximize-compat composite; re-saving does not fix it. Render fixtures pin PS's flatten through the sibling BMP (duplicate + flatten + BMP), never the embedded composite.
- `photoshop-saved-channels.psd`: resource 1053 identifies alpha channels only ([channels.md](channels.md)).
- 16/32-bit captures: `scripts\dev\deep\make_deep_fixtures.py` builds each scene directly at 8, 16 and 32 bits into `local-test-fixtures/deep/`. A 16-bit flatten saves as a 16-bit PNG; a 32-bit flatten needs an uncompressed float TIFF (linear light), since PNG cannot hold it. PS refuses some blend modes and adjustments at 32 bits; record the refusal instead of retrying ([high-bit-depth.md](high-bit-depth.md)).
- Placing over COM: the `Plc ` event takes `null` (path), `FTcs`/`Ofst`, optional `Wdth`/`Hght` percents, and `Lnkd` true for a linked placement; `placedLayerRelinkToFile` takes a path, `placedLayerUpdateAllModified` no arguments (`placedLayerUpdateModified` is refused until Photoshop itself has noticed the change). Read a layer's `smartObject` (linked, link, linkMissing, linkChanged) and `smartObjectMore` (ID, placed, type, transform, size) properties through `executeActionGet`. `scripts\dev\smart-objects\ps-capture-linked.ps1` is the reference capture driver ([smart-objects.md](smart-objects.md), "Place Linked ground truth").
