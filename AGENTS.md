# Repository Instructions

Keep this file current: when a change makes anything in here stale (build steps, test conventions, gotchas), update it in the same change. Multiple different coding agents read this file — it is the shared knowledge channel, so write notes for any AI, not a specific one.

When bumping the release version, update all three places: `CMakeLists.txt` (`project(... VERSION x.y)`), `vcpkg.json` (`version-semver`), and `latest_version.json` (the windows `version` — this is the update-check manifest, served to the app from raw.githubusercontent.com on main, so it only takes effect once pushed).

When adding or changing user-facing English text, make sure it is wired through Patchy's localization system and update the Japanese translation in `translations/patchy_ja.ts` in the same change.

Keyboard shortcuts for QActions must be registered through `MainWindow::register_hotkey(action, "stable.id", default_seq)` (backed by `HotkeyRegistry` in `src/ui/hotkey_registry.hpp`) — never call `setShortcut`/`setShortcuts` directly on an app-level action. The registry applies user customizations from the `hotkeys/` settings group (only deltas are stored; unmodified commands track new defaults automatically) and feeds the Preferences > Hotkeys editor. Command ids are persisted in user settings, so never rename one. Two commands must not share a default shortcut — Qt silently deactivates ambiguous application shortcuts; the `ui_hotkey_defaults_have_no_conflicts` visual test enforces this.

If tests need files from outside the project directory, copy those files into `local-test-fixtures` first and have the tests read them from there. Do not add hardcoded external drive paths such as `C:\temp` or `D:\projects` to test code.

Keep git commit messages to one or two lines — a concise subject, no multi-paragraph body enumerating every change.

## Testing notes

- `patchy_ui_visual_tests.exe` must run with `QT_QPA_PLATFORM=offscreen`. The offscreen platform does **not** enumerate installed Windows fonts; register what a test needs with `QFontDatabase::addApplicationFont("C:/Windows/Fonts/<file>.ttf")`. Never call `removeApplicationFont` to clean up — invalidating an in-use font cache can hard-crash the suite.
- A registered font may not appear under its familiar GDI name: the offscreen FreeType database uses the OpenType *typographic* family, so ariblk.ttf registers as family "Arial" + style "Black", not "Arial Black". Patchy's text code resolves such names via `available_text_family_style_match` (main_window.cpp); tests should not gate on `QFontDatabase::families().contains("Arial Black")`-style checks.
- The test harness `CHECK()` macro throws. A failing CHECK while a `MainWindow` with an open inline text editor is still alive aborts the process during unwind (exit code 3, no `[FAIL]` line printed). Prefer asserting after the editor session is committed/closed, or structure tests so CHECKs that may fail do not unwind past a live editor.
- Tests save PNGs via `save_widget_artifact(...)` into `test-artifacts/` next to the binary — inspect them to visually confirm rendering behavior.
- The shape tools (rectangle/ellipse, pixel layers and masks) honor the brush **Opacity**/**Soft** settings and go through the single-pass signed-distance renderer in `pixel_tools.cpp` (`render_shape`/`shape_pixel_coverage`); a 1px outline keeps a crisp legacy Bresenham path. The **Fill command** (`layerFillForegroundAction`/`fill_active_layer_with_color`) uses its *own* Fill Opacity/Soft settings — `CanvasWidget::fill_opacity()`/`fill_softness()`, controls on the Fill tool's options bar, persisted as `tools/fillOpacity`/`tools/fillSoftness`, defaulting to **100% / 0** so fills are solid by default. Soft feathers the fill inward from the selection edge via `fill_rect`'s `EditOptions::fill_softness_feather` (an inward distance-transform). Tests that use Fill only to lay down a solid setup color call `use_solid_fill_settings(canvas)` (ui_visual_tests.cpp) to force 100/0 against any persisted value.
- A click on a layer-row mask/content thumbnail can rebuild the layer row (the old row widget is deleted), so never reuse a cached row/thumbnail pointer across the press — use `click_layer_row_thumbnail(...)` in ui_visual_tests.cpp, which refetches the widget for press and release. Reusing the stale pointer is a use-after-free that flakes only when the heap reuses the freed block (this was the June 2026 "unreproducible" suite crash).
- If the visual suite dies with an access violation, the log now ends with a symbolized stack (a dbghelp vectored handler in `main`) — read it instead of re-running and hoping.
- Run a subset of visual tests by passing a name substring as the first argument (or `PATCHY_UI_TEST_FILTER`): `.\patchy_ui_visual_tests.exe ui_audio_splitter`. There is no `--test` flag.

## Photoshop compatibility verification

Adobe Photoshop 2026 is installed on this machine and is the ground truth for PSD compatibility work. It is COM-scriptable from PowerShell: `(New-Object -ComObject Photoshop.Application).DoJavaScript($jsx)` (the first call launches Photoshop, ~30s).

- To learn how Photoshop encodes a setting, save two PSDs differing in exactly one UI toggle and byte-diff them. This is how the layer-mask link flag (mask flags bit 0 = unlinked) and the "use global light" handling (`uglg` + image resources 1037/1049) in `src/psd/psd_document_io.cpp` were pinned down in June 2026.
- Photoshop semantics established the same way, encoded in code + tests: layer record flags bit 3 ("Photoshop 5.0 and later") must be written on every layer — without it Photoshop applies legacy semantics and badly misrenders layers that combine an unlinked mask with effects. The layer mask shapes layer effects (shadow/stroke/glow sources) regardless of the link state — the chain toggle affects move behavior only, never rendering. Effect *output* may still spill onto mask-hidden areas unless the "Layer Mask Hides Effects" blending option is on (tagged block 'lmgm', 4 bytes, first byte = bool; modeled as `LayerStyle::layer_mask_hides_effects` and exposed in the layer style dialog's Blending Options page). Beware confounded controls when byte-bisecting: an early conclusion here was wrong because the "control" file lacked bit 3 and went through Photoshop's legacy path.
- To check how Photoshop interprets a Patchy-written file, query Action Manager getters, e.g. `executeActionGet` of a layer reference and read `userMaskLinked` / `userMaskEnabled`.
- To compare renders, export Photoshop's flattened view and diff it against `Compositor::flatten_rgb8` of the same file. Gotcha: Photoshop's `doc.saveAs`/`doc.duplicate` fail with a misleading "disk error (-1)" on documents whose smart-object layers ('PlLd'/'SoLd' blocks) reference missing document-global 'lnk2' data — pre-June-2026 Patchy builds produced such files by dropping the global tagged-block section (now preserved; dangling references are stripped on save). For such damaged files, `doc.selection.selectAll(); doc.selection.copy(true)` (merged), paste into a fresh document, flatten, and save a 24-bit BMP from there. Single composite pixels can be probed without exporting via `doc.colorSamplers` (max 4 exist at once — add/read/remove in a loop).
- Script hygiene: set `app.displayDialogs = DialogModes.NO`, only close documents the script opened, and close with `SaveOptions.DONOTSAVECHANGES`.
- Small Photoshop-authored regression fixtures are committed under `test-fixtures/psd/` (e.g. `photoshop-unlinked-mask.psd`, `photoshop-global-light-shadow.psd`). Generate new ones with a COM script rather than hand-crafting bytes.
- Quick scratch tools that link Patchy's release libs (e.g. to flatten a PSD through Patchy's reader outside the test suite) compile with:

  ```powershell
  cmd /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++20 /EHsc /O2 /MD /I <repo>\src <tool>.cpp /link /LIBPATH:<repo>\build\release patchy_psd.lib patchy_render.lib patchy_core.lib patchy_color.lib patchy_filters.lib gdi32.lib user32.lib advapi32.lib dwrite.lib'
  ```

  `/MD` is required to match the libs; `advapi32`/`dwrite` are needed by `psd_document_io`'s font-resolution code.

For code changes, ALWAYS finish work in this repository by refreshing the local release build — `build\release\patchy.exe` must be freshly built from the final working tree at handoff, never stale. If the change is documentation-only or otherwise cannot affect compiled/runtime behavior (for example README text/images, comments, or agent instructions), do not run the full release build/test handoff just for that change; instead report that the release handoff was skipped because the change is non-code.

Required release handoff steps:

1. Build the release preset:

   ```powershell
   cmd /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset release'
   ```

   `build\release\CMakeCache.txt` carries hand-edited flags the preset does not set: `CMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Zi` and `CMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG:FULL /INCREMENTAL:NO`. They make release builds emit `patchy.pdb`, which is what symbolizes user crash dumps (WER minidumps land in `%LOCALAPPDATA%\CrashDumps`). If you reconfigure from scratch, pass them again with `-D...`; to symbolize a dump from an older build, rebuild that commit in a temp `git worktree` with the same flags (full links reproduce the binary layout) and read the dump against that PDB.

2. Run the release test binaries from `build\release`:

   ```powershell
   .\patchy_core_tests.exe
   $env:QT_QPA_PLATFORM='offscreen'; .\patchy_ui_visual_tests.exe
   ```

3. In the final response, explicitly report whether the release executable exists at:

   ```text
   build\release\patchy.exe
   ```

Do not say a release was created unless the release preset build completed successfully.
