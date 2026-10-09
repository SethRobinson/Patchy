# Repository Instructions

Repository-wide policy: read before any project action at task start. Reread only if the repository or this file changes, or these instructions leave context.

If `agents_local.md` exists at the repository root, open and read it too. It holds knowledge specific to this checkout and developer, such as which systems it can ssh into and use for building. Anything sensitive goes in `agents_secret.md` beside it, read only when a task needs it. Both are optional and gitignored, and must never be checked in. This repository is public, so machine names, users, addresses, local paths, and credentials belong in those files, never in tracked files. A git worktree has no untracked files: look for them in the main checkout, the parent of `git rev-parse --path-format=absolute --git-common-dir`.

Keep this file at or below 30,000 bytes. Detailed implementation knowledge belongs in `docs/<topic>.md`; read the relevant linked document before working in that area, update it when behavior changes, and do not duplicate its details here. Every file under `docs/` must also stay at or below 30,000 bytes: keep docs dense and current-state only. Cut narrative history, experiment logs, and restatements of constants that live in code; never cut normative rules, calibration facts recorded only in the doc, or headings cited from code comments.

## Repository-wide rules

- Every PSD/PSB Patchy writes, including script and MCP output, must open in Adobe Photoshop without warnings or errors. Custom metadata is allowed only when Photoshop accepts the file without warning, repair, or data-discard prompts. Follow the compatibility contract in [docs/ps-compat.md](docs/ps-compat.md).
- When adding or changing user-facing text, make it extractable (`tr()`, a literal-context `translate`, `QT_TR_NOOP`/`QT_TRANSLATE_NOOP` for bound text, `PATCHY_TRANSLATE_NOOP` in Qt-free code), run `scripts\update-translations.ps1`, and fill the new entries in every `translations/patchy_<code>.ts` in the same change. Never hand-edit catalog structure. See [docs/localization.md](docs/localization.md).
- Tests that need files outside the project must first copy them into `local-test-fixtures`; never add hardcoded external paths such as `C:\temp` or `D:\projects` to test code.
- Commit automatically only after a finished piece of work is verified and its required handoff is complete. Do not commit failing or half-finished states. Never push unless Seth explicitly asks in the current request. Asking to do or continue a release covers build and test only: stop for an explicit go-ahead before any upload, push, publish, or update announcement.
- Never post, reply, or comment on GitHub issues, pull requests, or discussions unless the user explicitly asks in the current request; offer a draft reply instead. Read the whole thread first (`gh issue view N --comments`): Seth may already have replied; continue from it, never repeat it.
- Never add AI attribution, generated-with text, or an OpenAI/Codex/Claude co-author to commits or pull requests. Keep commit messages to a concise subject and at most one short supporting line. Cite issues as `#NN` (GitHub links it); `Fixes #NN` also closes the issue once it reaches main, so use it only for a complete fix.
- Automation confined to Patchy is authorized: scripted clicks, typing, and direct captures of its windows and hosted plug-in windows (including KPT) need no additional permission. Desktop screenshots (even cropped afterward), global mouse/keyboard input (`SendInput`), Computer Use, and control of other apps require explicit authorization in the current request. Adobe Photoshop COM (`DoJavaScript`, Action Manager) remains always authorized for capture, verification, and acceptance. Use task-owned instances for unattended work; protect unsaved documents and unrelated processes. See [docs/testing.md](docs/testing.md).
- Build/test housekeeping needs no further confirmation: create, rename, replace, or delete generated build artifacts, temporary executable backups, test-owned socket files, logs, and scratch files inside this repository's build or test-output directories after verifying exact paths and ownership. Stopping processes launched for the current run is authorized. Not covered: user documents, fixtures, sources, unrelated files/processes, or a running user app/connector.
- **Destructive deletes.** No recursive or wildcard delete (`rm -rf`, `rmdir /s`, `del /s`, `Remove-Item -Recurse`, `remove_all`, `removeRecursively`) anywhere may target a bare variable or rely on the current directory. Spell a literal part of the path at the delete (`${ROOT:?}/build/flatpak-publish`, `%REPO%\build\package\installer`, a `patchy-` mktemp prefix) after checking each variable is set and right (the repo root holds `CMakeLists.txt`). `QDir("")` is the cwd. Tests delete folders only through `remove_test_scratch_dir`/`remove_test_scratch_tree` (tests/test_scratch_remove.hpp). Agents `rm` only a checked full literal path. Stop processes by PID, not name.
- Supported Windows, macOS, and Linux Debug and Release builds must have zero compiler, linker, and `lrelease` warnings. Keep warnings non-fatal. Fix Patchy-owned code explicitly. For vendored sources compiled into Patchy-owned targets, scope a suppression to one source and diagnostic. See [docs/platform.md](docs/platform.md).
- User-facing documentation must not use em dashes. Write plain, direct prose without hype, emoji headings, "not just X, but Y" constructions, or stock AI phrasing such as "seamlessly", "robust", "comprehensive", and "delve".
- When a test needs a capability Patchy lacks (an assertion surface, a file or state probe, a way to drive a flow without the desktop UI), prefer adding it to the JavaScript scripting API as a documented first-class `patchy.*` function over a test-only hook: the scripting system improves, and the same test can drive the real build through `--run-script`. Follow the API rules in [docs/scripting.md](docs/scripting.md) (d.ts, guide, change log, permanent identifiers).
- When wasm work is finished, stop every local wasm server you started (`powershell -NoProfile -ExecutionPolicy Bypass -File scripts\wasm\free-server-port.ps1 -Port <port>` for each port used). Seth restarts one himself when he wants it.

The release process, including version bumps, README author crediting, batch-file order, the GitHub Releases publish, and mandatory `NO_PAUSE=1` for non-interactive runs, lives in [docs/release-process.md](docs/release-process.md). Read it in full before bumping a version or running a release batch file.

## Build, test, and release handoff

For code changes, ALWAYS finish work in this repository by refreshing the local release build - `build\release\patchy.exe` must be freshly built from the final working tree at handoff. If the change is documentation-only or cannot affect runtime behavior, do not run the full release build/test handoff; report that it was skipped because the change is non-code.

Required release handoff steps:

1. Build the release preset:

   ```powershell
   cmd /s /c 'scripts\vs-env.bat -arch=x64 -host_arch=x64 >nul && scripts\run-throttled.bat "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset release -j 20'
   ```

   The throttling is mandatory: `-j 20` caps ninja for local Windows builds (remote builds use every core but four) and `scripts\run-throttled.bat` runs the build at below-normal priority. Run the test binaries through the same helper. Never launch an unthrottled build or substitute a bare `start "" /b /wait /belownormal`: it reports 0 whenever the program launched, while `run-throttled.bat` propagates the child's real exit code, crash codes included. Batch files go through the helper as-is: it wraps `.bat`/`.cmd` in `cmd /c`, because `start` runs a bare batch file under `cmd /K`, which parks at an interactive prompt and reports a failed build as exit 0.

   Run this from the repository root in PowerShell or a real cmd prompt, never Git Bash or another POSIX shell (nested quoting collapses there and cmd exits 0 without building). Trust the build only if the log shows compile/link lines or `ninja: no work to do`, never the exit code alone. Builds with an app target always relink: the build stamp (`cmake/write_build_stamp.cmake`) recompiles `build_info.cpp` so the in-app build date matches the binary. The version is the generated `patchy_version.hpp` (`cmake/patchy_version.hpp.in`), included only where shown or compared; never make it a target-wide define (every bump would recompile `patchy_ui`).

   Call `scripts\vs-env.bat`, never VsDevCmd.bat directly: it is the one place that knows where VsDevCmd lives. See [docs/release-process.md](docs/release-process.md).

   CMakeLists.txt owns the MSVC Release codegen flags (LTCG, `patchy.pdb` for crash dumps); never hand-edit `build\release\CMakeCache.txt`. See [docs/building.md](docs/building.md).

   A git worktree has no `.deps`: configure its release preset once with `--preset release -DCMAKE_PREFIX_PATH=<main-checkout>/.deps/Qt/6.8.3/msvc2022_64` (the main checkout's Qt; `agents_local.md` has the path); the build command is unchanged.

   A running `build\release\patchy.exe` or `patchy-mcp.exe` does not block the link (PRE_LINK renames it aside as `<name>.stale-<stamp>.exe`). Never kill either process (Seth may have unsaved work) and never package a `.stale-` file. Batch files compare exit codes against 0, never `if errorlevel 1` (a failed link exits negative). See [docs/release-process.md](docs/release-process.md).

2. Run release test binaries from `build\release`, scoped to the change:

   ```powershell
   cmd /s /c 'cd /d build\release && ..\..\scripts\run-throttled.bat .\patchy_core_tests.exe'
   $env:QT_QPA_PLATFORM='offscreen'; cmd /s /c 'cd /d build\release && ..\..\scripts\run-throttled.bat .\patchy_ui_visual_tests.exe'
   ```

   - **Per-change verification runs only the tests the change could possibly affect**. Both binaries accept a name-substring filter as the first argument; the UI suite also reads `PATCHY_UI_TEST_FILTER`. Pick filters that cover the feature, the changed tests, and any shared code the change touches, and report the filters used. Never run a full suite "to be safe" for a localized change (a new dialog, menu item, or script API needs its own tests plus the theme-token and hotkey checks).
   - Widen to the full core suite only when the change reaches core-wide surfaces: `src/core`, shared helpers (`main_window_shared`, `canvas_widget_shared`, `psd_io_common`), PSD or other serialization, byte-pinned/canary paths, or refactors and file moves whose blast radius cannot be filtered.
   - Widen to the full UI visual suite only for changes that can affect rendering or UI behavior application-wide: compositing/rendering, application-wide QSS/theme or hotkeys, or the visual test harness itself. Never run it for build-system or other non-rendering changes.
   - **A real release (preparing release builds for final packaging and upload) always runs both full suites.** Filtered runs miss ordered cross-test state such as QSettings and artifact dependencies, so that is the one time the whole suite is mandatory.
   - **Judge a suite by its `[FAIL]` lines as well as its exit code** (both trustworthy only through `run-throttled.bat`; see step 1).

3. Explicitly report whether `build\release\patchy.exe` exists.

4. Changes to platform-guarded code, CMake files/presets, or packaging also require the affected best-effort remote build: `scripts\remote\remote-build.ps1 -Target mac` and/or `-Target linux`. Report failures even though Windows remains the release gate. Separately, when Seth explicitly asks to offload a Windows build because the dev box is busy, `-Target windows` builds the real `release` preset on the Windows offload host; the local `build\release` remains the release gate and packaging source. See [docs/platform.md](docs/platform.md).

Do not say a release was created unless the release preset build succeeded.

## Universal engineering invariants

- **Never read a variable in the same call that moves it.** C++ argument evaluation order is unspecified and MSVC evaluates right-to-left in relevant cases. Compute the read result first, then call with that local and `std::move(value)`. `CanvasWidget::combine_selection_from_mask(bounds, mask)` also provides an overload that derives the value safely.
- **Read revision-bearing objects through const access.** Mutable Layer accessors bump revisions on access, invalidating revision-keyed caches. Use `std::as_const` or another const path. `PATCHY_REV_TRACE=1` traces bumps. See [docs/performance.md](docs/performance.md).
- **Nothing proportional to all layer pixels may run per repaint.** Cache or bound work reachable from `paintEvent`; use content revision for whole-render results and `pixel_revision()` for pixel-buffer-only results. See [docs/performance.md](docs/performance.md).
- **Core algorithms must be deterministic across toolchains.** Use splitmix64 with explicit uniform mapping, never `std::uniform_*_distribution`. Use integer math or deterministic-double envelopes with fixed tie-breaks for geometry and graph algorithms.
- **Persisted identifiers never change.** Hotkey command ids (`script.` ids included), preset ids, stress-test step ids, New Document preset ids, script/API identifiers (`saveAs` option keys included), settings keys, file-format tokens (the `.patchytheme` keys and `preferences/customThemeId` included; see [docs/theme-files.md](docs/theme-files.md)), and the organization name that keys the per-user app-data folder (see [docs/fonts.md](docs/fonts.md)) are compatibility contracts. `BlendMode` and `BrushDynamicControl` are append-only enums.
- **No hardcoded chrome colors in `src/ui`.** Every UI color is a named role in `src/ui/theme_palette.hpp`, written as an `@role_name` token in QSS or read through `theme()` when painted. See the color-scheme section of [docs/ui-conventions.md](docs/ui-conventions.md) for what is deliberately exempt (the transparency checkerboard, marching ants, tool cursors, user grid/guide colors).
- **Byte-stability canaries change only deliberately.** `psd_layered_writer_bytes_are_stable`, `gif_encoder_bytes_are_stable`, `dds_writer_bytes_are_stable`, and `tool_write_paths_digest_baseline` pin default output. Never re-pin them to make a refactor pass.
- **The CPU compositor is the reference renderer.** GPU and optimized paths must match its pinned output.
- **File paths cross the Qt boundary as UTF-16, never as `std::string`.** `QString::toStdString()` is UTF-8 and MSVC's `std::filesystem::path(std::string)` decodes it with the ANSI code page, which mangles non-ASCII names. Convert with `to_filesystem_path`/`to_qstring` from `src/ui/qt_paths.hpp` (never `toStdWString`), and turn path pieces into UTF-8 text with `path_to_utf8` from `src/support/path_utils.hpp`, never `path::string()`. Every new file-reading or file-writing entry point gets a Unicode-path test built on `tests/unicode_path_names.hpp`. See [docs/platform.md](docs/platform.md).
- **Never truncate a user's file in place.** Every document writer goes through `write_file_bytes_atomically` (src/support/atomic_file_write.hpp) or `QSaveFile`: a sibling temporary file and a replace-existing rename, so a crash or a full disk mid-save leaves the old file intact. See [docs/document-recovery.md](docs/document-recovery.md).
- **Serialization is fixed-width and cross-platform.** Never write `size_t`, `long`, `wchar_t`, native structs, or host-endian values into a file format. PSD I/O uses explicit big-endian primitives. See [docs/platform.md](docs/platform.md) and the relevant format document.
- **A font face's natural width is `QFont::AnyStretch`, never `setStretch(100)`.** Qt synthesizes request / face width class, so stretch 100 on a Condensed face is a 133 percent stretch. See [docs/text-tool.md](docs/text-tool.md).
- **Detach copy-on-write pixel storage on the launching thread before a parallel write.** `PixelBuffer` shares its bytes across copies (undo snapshots, render snapshots, `Document` copies) until the first non-const access. Worker strips that call non-const `row()`/`pixel()`/`data()` on a still-shared buffer race to detach: each copies the bytes while another strip's replacement frees them (access violations, or heap corruption long after). Call `pixels.data()` once on the launching thread and hand the workers spans or raw pointers; `apply_row_spans_in_parallel` (src/ui/filter_workflows.cpp) and `heal_mask_from_surroundings` are the references.

## Conditional references

Read these before acting in the named area:

| Work area | Required reference |
|---|---|
| MainWindow/CanvasWidget/PSD splits, function moves, shared helpers, broad refactors | [docs/code-organization.md](docs/code-organization.md), plus [docs/refactor-backlog.md](docs/refactor-backlog.md) for cleanup work |
| QActions, dialogs, options bar, list rows, status messages, shared QSS/UI conventions, colors and the Dark/Light color scheme | [docs/ui-conventions.md](docs/ui-conventions.md) |
| User-facing text, translation catalogs, languages, `LocalizationManager`, unit suffixes | [docs/localization.md](docs/localization.md) |
| Layers panel (rows, thumbnails, click selection, disclosure arrow, visibility eye, drags to another document, Alt-drag duplicate) | [docs/layer-panel.md](docs/layer-panel.md) |
| Tests, offscreen behavior, visual QA, app screenshots, suite failure diagnosis | [docs/testing.md](docs/testing.md) |
| Platform-guarded code, macOS/Linux behavior, remote builds | [docs/platform.md](docs/platform.md) |
| WebAssembly builds, the wasm-core preset, emsdk provisioning | [docs/wasm.md](docs/wasm.md); wasm memory/telemetry in [docs/wasm-memory.md](docs/wasm-memory.md); wasm input/focus/hotkeys in [docs/wasm-input.md](docs/wasm-input.md) |
| Patents, licensing, trademarks, bundled assets, or a feature adjacent to a legal boundary | [docs/legal-constraints.md](docs/legal-constraints.md), with the underlying research record in [docs/patent-research.md](docs/patent-research.md), [docs/patent-research-inpainting.md](docs/patent-research-inpainting.md), [docs/patent-research-alignment.md](docs/patent-research-alignment.md), and [docs/patent-research-formats.md](docs/patent-research-formats.md) |
| PSD descriptors, layer styles, write/corruption rules | [docs/ps-compat.md](docs/ps-compat.md) |
| Photoshop COM captures and acceptance runs, the unknown-data prompt check | [docs/photoshop-com.md](docs/photoshop-com.md) |
| Font resolution (display names, DirectWrite lookup, GDI names, exact sizes) | [docs/font-resolution.md](docs/font-resolution.md) |
| Adjustment/auto-adjustment calibration (Brightness/Contrast, Curves, Hue/Saturation) | [docs/adjustments-calibration.md](docs/adjustments-calibration.md) |
| Layer-effect render calibration (Blend If, Satin, Stroke, shadows/glows, interior effects, Bevel/Emboss, Pattern Overlay, Shape Burst) | [docs/layer-effects-render.md](docs/layer-effects-render.md) |
| Native Smart Filter descriptors, FEid cache, per-filter render semantics | [docs/smart-filters-native.md](docs/smart-filters-native.md) |
| Contributor pull-request review | [docs/pr-review.md](docs/pr-review.md) |
| Release/version/package/upload work | [docs/release-process.md](docs/release-process.md) |

## Cross-cutting implementation rules

- Runtime assets are shared copy-once CMake targets. New executables use existing `patchy_copy_*` helpers; never add per-target POST_BUILD copies into the shared output directory. See [docs/code-organization.md](docs/code-organization.md).
- Session data must outlive canvas event delivery. Preserve MainWindow's canvas-detach and session-close destruction orders; references into `SmartObjectStore` do not survive `add_embedded`. See [docs/code-organization.md](docs/code-organization.md).
- Every submenu action under the menubar gets `QAction::NoRole`: on macOS Qt merges items by translated title into the app menu and a merged submenu crashes on the next window activation (GitHub issue 29). See [docs/platform.md](docs/platform.md).
- Read modifier state folded from the current event, not `QApplication::keyboardModifiers()`. See [docs/ui-conventions.md](docs/ui-conventions.md) and [docs/testing.md](docs/testing.md).
- New non-modal dialogs use `run_non_modal_dialog`; closing-sensitive dialogs funnel through `done()`. See [docs/ui-conventions.md](docs/ui-conventions.md).
- Open-dialog filter strings have a Windows/Qt-specific duplicated-pattern contract. Read [docs/file-formats.md](docs/file-formats.md) before changing them.
- The local PSBtest tent and Content fixtures must never be overwritten. See [docs/smart-objects.md](docs/smart-objects.md).
- Offscreen text on Windows is FreeType; a real window is DirectWrite, which ignores a QFont stretch for glyph images. Text pins pass offscreen and can still be wrong on screen; see the font-engine note in [docs/testing.md](docs/testing.md).
- Photoshop reads a document's text fonts and content from its own `Txt2` block, not the layers, and trusts it over the TySh. Patchy rebuilds that block on every save (an authored object per regenerated type layer, untouched objects kept, no layout caches); a new TySh key needs its Txt2 twin in `psd_text_engine_block.cpp` or Photoshop shows the stale object. Format, key map and what Photoshop accepts: [docs/txt2.md](docs/txt2.md); see also [docs/ps-compat.md](docs/ps-compat.md).
- Every gradient descriptor Patchy writes carries at least two transparency stops, and a file that already has none heals on save. An empty `Trns` list makes Photoshop discard the layer behind its "unknown data" prompt; `scripts\dev\photoshop-open-check.ps1` detects that prompt per file. See [docs/ps-compat.md](docs/ps-compat.md).
- Never leave a Qt host lookup pending at quit. `QNetworkAccessManager` resolves through `QHostInfo`'s thread pool, and `~QCoreApplication` waits for that pool with no timeout (a stalled `getaddrinfo` froze macOS quits, GitHub issue 48). `update_checker.cpp` resolves the host on a detached thread first; any new network client does the same (docs/testing.md). The tracked-worker wait after `app.exec()` is bounded too (`finish_after_event_loop`): a worker still blocked after 10 s ends the process without destructors after dropping the recovery folder, so never rely on a destructor for anything the user could lose.
- Agent tooling: the Claude Code Bash tool rewrites backslash escapes inside heredocs; write it to a file and run it.

## Feature index

Read the linked document before working on the feature; it owns the detailed constraints.

- **Smart Objects and Smart Filters:** [docs/smart-objects.md](docs/smart-objects.md), the native-filter calibration in [docs/smart-filters-native.md](docs/smart-filters-native.md), plus the binding boundaries in [docs/legal-constraints.md](docs/legal-constraints.md).
- **Liquify:** [docs/liquify.md](docs/liquify.md) and [docs/legal-constraints.md](docs/legal-constraints.md).
- **Warp:** [docs/warp.md](docs/warp.md).
- **Brush tips, dynamics, Flow/Airbrush, Pattern Stamp, and ABR:** [docs/brushes.md](docs/brushes.md) and [docs/legal-constraints.md](docs/legal-constraints.md).
- **Mixer Brush pickup engine and stroke Smoothing:** [docs/mixer.md](docs/mixer.md) and [docs/legal-constraints.md](docs/legal-constraints.md).
- **Healing Brush, Spot Healing, Patch tool, Remove Object (dialog, variations, tone match), and retouch Sample All Layers:** [docs/healing.md](docs/healing.md) and [docs/legal-constraints.md](docs/legal-constraints.md).
- **Palette mode:** [docs/palette-mode.md](docs/palette-mode.md).
- **16/32-bit editing (on; `PATCHY_DEEP_EDITING=0` off):** [docs/high-bit-depth.md](docs/high-bit-depth.md).
- **File formats, PSB, HEIF/HEIC, and flat-image alpha:** [docs/file-formats.md](docs/file-formats.md); Camera Raw in [docs/camera-raw.md](docs/camera-raw.md), Affinity in [docs/af-format.md](docs/af-format.md), animated GIF/WebP in [docs/animation.md](docs/animation.md).
- **JPEG XR (.jxr) and the HDR tone map:** [docs/jxr.md](docs/jxr.md), plus the no-vendored-codec rule in [docs/legal-constraints.md](docs/legal-constraints.md).
- **Proton (.rttex) and DDS (.dds) textures:** [docs/rttex.md](docs/rttex.md), [docs/dds.md](docs/dds.md).
- **PDF import/export (editable layers, flat, the image-page writer and quality presets, pass-through of imported pages, `pdfopen`/`pdfsave` profiling):** [docs/pdf.md](docs/pdf.md).
- **Document channels:** [docs/channels.md](docs/channels.md).
- **Resolution and measurement units:** [docs/resolution-units.md](docs/resolution-units.md).
- **Image Size resampling (methods, kernels shared with Free Transform, permanent ids, the remembered method):** [docs/resampling.md](docs/resampling.md).
- **PSD adjustment layers, clipping masks, layer styles, and Photoshop text:** [docs/ps-compat.md](docs/ps-compat.md), [docs/file-formats.md](docs/file-formats.md), [docs/adjustments-calibration.md](docs/adjustments-calibration.md), and the Photoshop text model in [docs/text-render-calibration.md](docs/text-render-calibration.md). Photoshop 5.x `tySh` type records (no EngineData; a fixed layout) are in [docs/psd-legacy-text.md](docs/psd-legacy-text.md).
- **Filter Gallery, recipes, Saved Looks, and visual filters:** [docs/filters.md](docs/filters.md), [docs/smart-objects.md](docs/smart-objects.md), and [docs/legal-constraints.md](docs/legal-constraints.md).
- **Blend modes:** [docs/blend-modes.md](docs/blend-modes.md).
- **User-importable theme files (.patchytheme format, custom-palette override, ThemeManager apply path):** [docs/theme-files.md](docs/theme-files.md), which overlays [docs/ui-conventions.md](docs/ui-conventions.md)'s Dark/Light system.
- **Layer-style and pattern presets:** [docs/style-presets.md](docs/style-presets.md).
- **Gradients and GRD:** [docs/gradients.md](docs/gradients.md).
- **Text tool, Character panel and Paragraph panel:** [docs/text-tool.md](docs/text-tool.md), text inside shapes in [docs/area-text.md](docs/area-text.md), with the Photoshop layout/measurement calibration in [docs/text-render-calibration.md](docs/text-render-calibration.md).
- **Photoshop's Txt2 text engine block (format, numeric key map, the writer, what Photoshop accepts):** [docs/txt2.md](docs/txt2.md). Read it before any Txt2, text-run or list-style work. Vertical type and paragraph direction (Photoshop's vertical text model, the Ornt/WritingDirection encoding, the v4 paragraph column) are in both.
- **Bundled fonts, wasm font aliases, and user-added fonts:** [docs/fonts.md](docs/fonts.md).
- **Selection tools:** [docs/selection-tools.md](docs/selection-tools.md) and [docs/legal-constraints.md](docs/legal-constraints.md).
- **Shape tools, the Fill tool (Paint Bucket tolerance, Contiguous, opacity, soft edge) and Fill command, Free Transform modifiers, pixel-grid snapping (Photoshop's whole-pixel rule, `core/pixel_grid.hpp`), and Merge Down:** [docs/tools.md](docs/tools.md); vector merges in [docs/layer-merging.md](docs/layer-merging.md); the Crop tool in [docs/crop-tool.md](docs/crop-tool.md); tool palette layout, flyouts and icons in [docs/tool-palette.md](docs/tool-palette.md).
- **Zoom tool, Scrubby Zoom, pen and wheel zoom:** [docs/view-navigation.md](docs/view-navigation.md).
- **Move-tool alignment guides (snap targets, the magenta overlay, the Snap checkbox) and Layer > Arrange > Align / Distribute:** [docs/alignment.md](docs/alignment.md) and [docs/legal-constraints.md](docs/legal-constraints.md).
- **Vector tools, shape layers, vector masks, and Paths:** [docs/vector-tools.md](docs/vector-tools.md) (PSD fixtures in [docs/vector-fixtures.md](docs/vector-fixtures.md)) and [docs/legal-constraints.md](docs/legal-constraints.md).
- **Point-editing UI (anchor tools, hints, path context menu) and vector commands:** [docs/vector-commands.md](docs/vector-commands.md).
- **SVG import/export:** [docs/svg.md](docs/svg.md).
- **Trace Image to Shapes (raster to vector):** [docs/image-trace.md](docs/image-trace.md) and the "Vector tracing" boundary in [docs/legal-constraints.md](docs/legal-constraints.md).
- **Float windows and document activation:** [docs/float-windows.md](docs/float-windows.md).
- **Scanner, photocopy, Divide Scanned Photos, sprite-sheet, image-sequence, seamless-tiling import, and Files as Layers (Layers-panel file drop, Import command, Paste of copied files):** [docs/import.md](docs/import.md).
- **Plug-ins and legacy 8BF support (the Windows out-of-process host, 32-bit and 64-bit, the PiPL reader, plug-in folders):** [docs/plugins.md](docs/plugins.md), with the clean-room ABI boundary in [docs/legal-constraints.md](docs/legal-constraints.md).
- **JavaScript scripting and bundled scripts:** [docs/scripting.md](docs/scripting.md). The MCP connector and patchy-control skill: [docs/ai-control.md](docs/ai-control.md).
- **Automatic document recovery and atomic file writes:** [docs/document-recovery.md](docs/document-recovery.md). Recovery copies live per running instance under a QLockFile; every document writer goes through `write_file_bytes_atomically` or `QSaveFile`.
- **Single-instance forwarding, CLI screenshots, and `--headless` runs:** `src/app/main.cpp`, [docs/testing.md](docs/testing.md), and the CLI section of [docs/scripting.md](docs/scripting.md).
- **Logo and icon source art:** `packaging/branding/`.
- **README screenshots and contact sheets:** [docs/testing.md](docs/testing.md).
- **Performance and the stress harness:** [docs/performance.md](docs/performance.md); the Move/Free Transform drag-preview machinery is in [docs/interactive-previews.md](docs/interactive-previews.md).
- **Testy PSD benchmark:** [docs/testy.md](docs/testy.md).
- **Flatpak packaging and its update repository:** [packaging/linux/README.md](packaging/linux/README.md).
