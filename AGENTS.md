# Repository Instructions

Repository-wide policy: read before any project action at task start. Reread only if the repository or this file changes, or these instructions leave context.

If `agents_local.md` exists at the repository root, read it too: checkout- and developer-specific knowledge (build hosts, ssh targets). Anything sensitive goes in `agents_secret.md` beside it, read only when a task needs it. Both are gitignored, never checked in. This repository is public: machine names, users, addresses, local paths, and credentials belong in those files, never in tracked files. A git worktree has no untracked files: look in the main checkout, the parent of `git rev-parse --path-format=absolute --git-common-dir`.

Keep this file at or below 30,000 bytes. Implementation knowledge belongs in `docs/<topic>.md`; read the linked document before working in that area, update it when behavior changes, and do not duplicate its details here. Every file under `docs/` must also stay at or below 30,000 bytes: keep docs dense and current-state only. Cut narrative history, experiment logs, and restatements of constants that live in code; never cut normative rules, calibration facts recorded only in the doc, or headings cited from code comments. `python scripts/check-docs.py` checks the sizes and every relative link and anchor; run it after doc edits.

## Repository-wide rules

- Every PSD/PSB Patchy writes, including script and MCP output, must open in Adobe Photoshop without warnings or errors. Custom metadata is allowed only when Photoshop accepts the file without warning, repair, or data-discard prompts. Follow the compatibility contract in [docs/ps-compat.md](docs/ps-compat.md).
- When adding or changing user-facing text, make it extractable (`tr()`, a literal-context `translate`, `QT_TR_NOOP`/`QT_TRANSLATE_NOOP` for bound text, `PATCHY_TRANSLATE_NOOP` in Qt-free code), run `scripts\update-translations.ps1`, and fill the new entries in every `translations/patchy_<code>.ts` in the same change. Never hand-edit catalog structure. See [docs/localization.md](docs/localization.md).
- Tests that need files outside the project must first copy them into `local-test-fixtures`; never hardcode external paths (`C:\temp`, `D:\projects`) in tests.
- Commit automatically only finished, verified work with its handoff done, never failing or half-finished states. Never push unless Seth explicitly asks in the current request. A push go-ahead covers only that commit. A release request covers build and test only: stop for an explicit go-ahead before any upload, push, publish, or update announcement.
- Never post or comment on GitHub issues, PRs, or discussions unless asked in the current request; offer a draft reply. Read the whole thread first (`gh issue view N --json body,comments`; piped `--comments` drops the body): continue from any reply Seth already made.
- Never add AI attribution, generated-with text, or an OpenAI/Codex/Claude co-author to commits or pull requests. Keep commit messages to a concise subject and at most one short supporting line. Cite issues as `(#NN)`, never after a closing keyword (any form of fix, close, resolve) in a commit or PR: a push to main closes the issue, but issues close only when the fix ships; label it `fixed in main`.
- Automation confined to Patchy is authorized: scripted clicks, typing, and direct captures of its windows and hosted plug-in windows (including KPT) need no further permission. Desktop screenshots (even cropped afterward), global mouse/keyboard input (`SendInput`), Computer Use, and control of other apps require explicit authorization in the current request. Adobe Photoshop COM (`DoJavaScript`, Action Manager) is always authorized for capture, verification, and acceptance. Use task-owned instances for unattended work; protect unsaved documents and unrelated processes. See [docs/testing.md](docs/testing.md).
- Build/test housekeeping needs no further confirmation: create, rename, replace, or delete generated build artifacts, temporary executable backups, test-owned socket files, logs, and scratch files inside this repository's build or test-output directories after verifying exact paths and ownership. Stopping processes launched for the current run is authorized. Not covered: user documents, fixtures, sources, unrelated files/processes, or a running user app/connector.
- **Destructive deletes.** No recursive or wildcard delete (`rm -rf`, `rmdir /s`, `del /s`, `Remove-Item -Recurse`, `remove_all`, `removeRecursively`) anywhere may target a bare variable or rely on the current directory. Spell a literal part of the path at the delete (`${ROOT:?}/build/flatpak-publish`, `%REPO%\build\package\installer`, a `patchy-` mktemp prefix) after checking each variable is set and right (the repo root holds `CMakeLists.txt`). `QDir("")` is the cwd. Tests delete folders only through `remove_test_scratch_dir`/`remove_test_scratch_tree` (tests/test_scratch_remove.hpp). Agents `rm` only a checked full literal path. Stop processes by PID, not name.
- Supported Windows, macOS, and Linux Debug and Release builds must have zero compiler, linker, and `lrelease` warnings. Keep warnings non-fatal. Fix Patchy-owned code explicitly. For vendored sources compiled into Patchy-owned targets, scope a suppression to one source and diagnostic. See [docs/platform.md](docs/platform.md).
- Patchy's interface is its own design. It follows widely shared image-editing conventions and reads other editors' files, but never reproduces another product's icons, artwork, or screen layouts; each interface change must stand on its own usability merits.
- User-facing documentation must not use em dashes. Write plain, direct prose without hype, emoji headings, "not just X, but Y" constructions, or stock AI phrasing such as "seamlessly", "robust", "comprehensive", and "delve".
- When a test needs a capability Patchy lacks (an assertion surface, a file or state probe, a way to drive a flow without the desktop UI), prefer adding it to the JavaScript scripting API as a documented first-class `patchy.*` function over a test-only hook: the scripting system improves, and the same test can drive the real build through `--run-script`. Follow the API rules in [docs/scripting.md](docs/scripting.md) (d.ts, guide, change log, permanent identifiers).

## Build, test, and release handoff

For code changes, ALWAYS finish with `build\opt-dev\patchy.exe` freshly built from the final working tree. Development uses the `opt-dev` preset (no `/GL` or `/LTCG`); the LTCG `release` preset is for releases only (Seth, October 23, 2026). For a docs-only change or one that cannot affect runtime behavior, skip the build/test handoff and say so.

Required handoff steps:

1. Build the opt-dev preset:

   ```powershell
   cmd /s /c 'scripts\vs-env.bat -arch=x64 -host_arch=x64 >nul && scripts\run-throttled.bat "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset opt-dev -j 20'
   ```

   Rules (reasons and details: "Local Windows release build" in [docs/building.md](docs/building.md)):
   - Builds, tests and batch files run only through `scripts\run-throttled.bat`, builds with `-j 20`. It refuses a second `cmake --build` on the box (exit 3): wait, or offload with `scripts\remote\remote-build.ps1 -Target windows -Preset opt-dev`.
   - Run from the repository root in PowerShell or real cmd, never Git Bash. Require compile/link lines or `ninja: no work to do`, not just exit 0.
   - Iterate with `--target <what you run>` (remote: `-BuildTarget`, `-Preset opt-dev`); build everything once, at handoff.
   - A worktree configures once with `--preset opt-dev -DCMAKE_PREFIX_PATH=<main-checkout>/.deps/Qt/6.8.3/msvc2022_64`.
   - Never kill a running `patchy.exe` or `patchy-mcp.exe` (the link renames it aside), never package a `.stale-` file.

2. Run the test binaries from `build\opt-dev`, scoped to the change:

   ```powershell
   cmd /s /c 'cd /d build\opt-dev && ..\..\scripts\run-throttled.bat .\patchy_core_tests.exe <filter>'
   $env:QT_QPA_PLATFORM='offscreen'; cmd /s /c 'cd /d build\opt-dev && ..\..\scripts\run-throttled.bat .\patchy_ui_visual_tests.exe <filter>'
   ```

   - **Per-change verification runs only the tests for what the change touches**. Both binaries take a name-substring filter first (the UI suite also reads `PATCHY_UI_TEST_FILTER`). Name the feature's own tests and those of the functions and files the diff changes, by full name or a narrow prefix, and report the filters. A family prefix (`ai_`, `hotkey`) only when the change reaches the whole family; never a full suite "to be safe".
   - Cross-cutting checks only for their surface: theme-token checks for QSS, theme roles or styled object names; `ui_offscreen_dialog_sweep` for a new or changed dialog; `ui_hotkey_defaults_have_no_conflicts` for actions and shortcuts; byte canaries for their writers. Rerun tests only after an edit that can change them.
   - Widen to the full core suite only when the change reaches core-wide surfaces: `src/core`, shared helpers (`main_window_shared`, `canvas_widget_shared`, `psd_io_common`), PSD or other serialization, byte-pinned/canary paths, or refactors and file moves whose blast radius cannot be filtered.
   - Widen to the full UI visual suite only for changes that can affect rendering or UI behavior application-wide: compositing/rendering, application-wide QSS/theme or hotkeys, or the visual test harness itself. Never run it for build-system or other non-rendering changes.
   - **A real release (preparing release builds for final packaging and upload) always runs both full suites.** Filtered runs miss ordered cross-test state such as QSettings and artifact dependencies, so that is the one time the whole suite is mandatory.
   - **Judge a suite by its `[FAIL]` lines as well as its exit code** (both trustworthy only through `run-throttled.bat`; see step 1), and report its `[SUMMARY]` skip count: a `[SKIP]` test checked nothing. A filter that selects no test exits 2; `--list` shows the names ([docs/testing.md](docs/testing.md)).

3. Explicitly report whether `build\opt-dev\patchy.exe` exists.

4. Changes to platform-guarded code, CMake files/presets, or packaging also require the affected best-effort remote build: `scripts\remote\remote-build.ps1 -Target mac` and/or `-Target linux` (Linux tests on the Flatpak's Qt: [docs/linux-testing.md](docs/linux-testing.md)). Report failures even though Windows remains the release gate. When Seth asks for it (a standing permission in `agents_local.md` counts), `-Target windows -Preset opt-dev` builds on the offload host. See [docs/building.md](docs/building.md) ("Remote build machinery") and [docs/platform.md](docs/platform.md).

Releases build the `release` preset into `build\release` (release gate, packaging source) and run both full suites. Never say a release was created unless that build succeeded.

## Universal engineering invariants

- **Never read a variable in the same call that moves it.** C++ argument evaluation order is unspecified. Compute the read result first, then call with that local and `std::move(value)`.
- **Read revision-bearing objects through const access.** Mutable Layer accessors bump revisions on access, invalidating revision-keyed caches. Use `std::as_const` or another const path. `PATCHY_REV_TRACE=1` traces bumps. See [docs/performance.md](docs/performance.md).
- **Nothing proportional to all layer pixels may run per repaint.** Cache or bound work reachable from `paintEvent`; use content revision for whole-render results and `pixel_revision()` for pixel-buffer-only results. See [docs/performance.md](docs/performance.md).
- **Core algorithms must be deterministic across toolchains.** Use splitmix64 with explicit uniform mapping, never `std::uniform_*_distribution`. Use integer math or deterministic-double envelopes with fixed tie-breaks for geometry and graph algorithms. Float pixel targets build with `-ffp-contract=off` on GCC/Clang (arm64 FMA contraction; [docs/platform.md](docs/platform.md#floating-point-determinism)).
- **Persisted identifiers never change.** Hotkey command ids (`script.` ids included), preset ids, stress-test step ids, New Document preset ids, script/API identifiers (`saveAs` option keys included), settings keys, file-format tokens (the `.patchytheme` keys and `preferences/customThemeId` included; see [docs/theme-files.md](docs/theme-files.md)), and the organization name that keys the per-user app-data folder (see [docs/fonts.md](docs/fonts.md)) are compatibility contracts. `BlendMode` and `BrushDynamicControl` are append-only enums.
- **No hardcoded chrome colors in `src/ui`.** Every UI color is a named role in `src/ui/theme_palette.hpp`, written as an `@role_name` token in QSS or read through `theme()` when painted. See the color-scheme section of [docs/ui-conventions.md](docs/ui-conventions.md) for what is deliberately exempt (the transparency checkerboard, marching ants, tool cursors, user grid/guide colors).
- **Byte-stability canaries change only deliberately.** `psd_layered_writer_bytes_are_stable`, `gif_encoder_bytes_are_stable`, `dds_writer_bytes_are_stable`, and `tool_write_paths_digest_baseline` pin default output. Never re-pin them to make a refactor pass.
- **The CPU compositor is the reference renderer.** Patchy renders on the CPU only (no GPU path; [docs/performance.md](docs/performance.md) says why). Optimized paths, and any future GPU path, must match its pinned output.
- **File paths cross the Qt boundary as UTF-16, never as `std::string`.** `QString::toStdString()` is UTF-8 and MSVC's `std::filesystem::path(std::string)` decodes it with the ANSI code page, which mangles non-ASCII names. Convert with `to_filesystem_path`/`to_qstring` from `src/ui/qt_paths.hpp` (never `toStdWString`), and turn path pieces into UTF-8 text with `path_to_utf8` from `src/support/path_utils.hpp`, never `path::string()`. Every new file-reading or file-writing entry point gets a Unicode-path test built on `tests/unicode_path_names.hpp`. See [docs/platform.md](docs/platform.md).
- **Never truncate a user's file in place.** Every document writer goes through `write_file_bytes_atomically`/`AtomicFileReplacement` (src/support/atomic_file_write.hpp) or `QSaveFile`: a sibling temporary and a replacing rename, so a crash or full disk mid-save keeps the old file. See [docs/document-recovery.md](docs/document-recovery.md).
- **Serialization is fixed-width and cross-platform.** Never write `size_t`, `long`, `wchar_t`, native structs, or host-endian values into a file format. PSD I/O uses explicit big-endian primitives. See [docs/platform.md](docs/platform.md) and the relevant format document.
- **Detach copy-on-write pixel storage on the launching thread before a parallel write.** `PixelBuffer` shares its bytes across copies (undo, render snapshots, `Document` copies) until the first non-const access. Worker strips calling non-const `row()`/`pixel()`/`data()` on a still-shared buffer race to detach, each copying while another's replacement frees the bytes (access violations, or heap corruption long after). Call `pixels.data()` once on the launching thread and hand the workers spans or raw pointers; `apply_row_spans_in_parallel` (src/ui/filter_workflows.cpp) and `heal_mask_from_surroundings` are the references.
- **A 32-bit image is linear floats; 16 and 32-bit pixels are wider than a byte.** Never hand a `Format_RGBA32FPx4` image to QPainter or a Qt 8-bit conversion, or walk deep pixels a byte per channel: encode first (`display_image_from_deep_qimage`, `display_qimage_from_pixel_buffer`) and go through the typed rows. `tests/ui/deep_operation_audit_tests.cpp` checks each operation at 8/16/32 bits ([high-bit-depth.md](docs/high-bit-depth.md)).

## Conditional references

Read these before acting in the named area:

| Work area | Required reference |
|---|---|
| MainWindow/CanvasWidget/PSD splits, function moves, shared helpers, broad refactors | [docs/code-organization.md](docs/code-organization.md), plus [docs/refactor-backlog.md](docs/refactor-backlog.md) for cleanup work |
| QActions, dialogs, options bar, list rows, status messages, shared QSS/UI conventions, colors, Dark/Light scheme | [docs/ui-conventions.md](docs/ui-conventions.md) |
| User-facing text, translation catalogs, languages, `LocalizationManager`, unit suffixes | [docs/localization.md](docs/localization.md) |
| Layers panel (rows, thumbnails, click selection, disclosure arrow, visibility eye, drags to another document, Alt-drag duplicate) | [docs/layer-panel.md](docs/layer-panel.md) |
| Tests, offscreen behavior, visual QA, app and README screenshots, suite failure diagnosis | [docs/testing.md](docs/testing.md) |
| Platform-guarded code, macOS/Linux behavior | [docs/platform.md](docs/platform.md); Wayland: [docs/wayland.md](docs/wayland.md) |
| WebAssembly builds, the wasm-core preset, emsdk provisioning | [docs/wasm.md](docs/wasm.md); wasm memory/telemetry in [docs/wasm-memory.md](docs/wasm-memory.md); wasm input/focus/hotkeys in [docs/wasm-input.md](docs/wasm-input.md) |
| Patents, licensing, trademarks, bundled assets, or a feature adjacent to a legal boundary | [docs/legal-constraints.md](docs/legal-constraints.md), which links every patent research record (`docs/patent-research*.md`) |
| PSD descriptors, layer styles, write/corruption rules | [docs/ps-compat.md](docs/ps-compat.md) |
| Photoshop COM captures and acceptance runs, the unknown-data prompt check | [docs/photoshop-com.md](docs/photoshop-com.md) |
| Font resolution (display names, DirectWrite lookup, GDI names, exact sizes, missing fonts) | [docs/font-resolution.md](docs/font-resolution.md) |
| Adjustment/auto-adjustment calibration (Brightness/Contrast, Curves, Hue/Saturation); Photoshop kinds added for #250 and placeholders | [docs/adjustments-calibration.md](docs/adjustments-calibration.md), [docs/adjustment-kinds.md](docs/adjustment-kinds.md) |
| Layer-effect render calibration (Blend If, Satin, Stroke, shadows/glows, interior effects, Bevel/Emboss, Pattern Overlay, Shape Burst) | [docs/layer-effects-render.md](docs/layer-effects-render.md) |
| Native Smart Filter descriptors, FEid cache, per-filter render semantics | [docs/smart-filters-native.md](docs/smart-filters-native.md) |
| Contributor pull-request review | [docs/pr-review.md](docs/pr-review.md) |
| Release/version/package/upload work | [docs/release-process.md](docs/release-process.md): read it in full before a version bump or any release batch file (`NO_PAUSE=1` for non-interactive runs) |
| Build scripts, `run-throttled.bat` and its build lock, presets, the version header, batch-file exit codes, remote builds (`remote-build.ps1`, host locks) | [docs/building.md](docs/building.md) |
| Update prompt, one-click update, installer update mode | [docs/auto-update.md](docs/auto-update.md) |

## Cross-cutting implementation rules

- Runtime assets are shared copy-once CMake targets. New executables use existing `patchy_copy_*` helpers; never add per-target POST_BUILD copies into the shared output directory. See [docs/code-organization.md](docs/code-organization.md).
- Session data must outlive canvas event delivery. Preserve MainWindow's canvas-detach and session-close destruction orders; references into `SmartObjectStore` do not survive `add_embedded`. See [docs/code-organization.md](docs/code-organization.md).
- Every submenu action under the menubar gets `QAction::NoRole`: on macOS Qt merges items by translated title into the app menu and a merged submenu crashes on the next window activation (issue 29). See [docs/platform.md](docs/platform.md).
- Read modifier state folded from the current event, not `QApplication::keyboardModifiers()`. See [docs/ui-conventions.md](docs/ui-conventions.md) and [docs/testing.md](docs/testing.md).
- New non-modal dialogs use `run_non_modal_dialog`; closing-sensitive dialogs funnel through `done()`. See [docs/ui-conventions.md](docs/ui-conventions.md).
- Never rely on a destructor for anything the user could lose: the wait for workers after `app.exec()` is bounded and can end the process without destructors. Startup, quit, host lookups and Qt's automatic quit: "App lifecycle" in [docs/code-organization.md](docs/code-organization.md#app-lifecycle-startup-and-quit).
- Bash heredocs rewrite backslash escapes (write a file and run it); Windows Python text mode writes CRLF (use `newline=''`).

## Feature index

Read the linked document before working on a feature; it owns the constraints.

- **Smart Objects and Smart Filters:** [docs/smart-objects.md](docs/smart-objects.md), the native-filter calibration in [docs/smart-filters-native.md](docs/smart-filters-native.md) (table-described filters: [docs/smart-filters-pixelate-render.md](docs/smart-filters-pixelate-render.md); Image > Adjustments entries: [docs/smart-filters-adjustments.md](docs/smart-filters-adjustments.md)), plus the binding boundaries in [docs/legal-constraints.md](docs/legal-constraints.md).
- **Liquify:** [docs/liquify.md](docs/liquify.md) and [docs/legal-constraints.md](docs/legal-constraints.md).
- **Warp:** [docs/warp.md](docs/warp.md).
- **Brush tips, dynamics, Flow/Airbrush, Pattern Stamp, and ABR:** [docs/brushes.md](docs/brushes.md) and [docs/legal-constraints.md](docs/legal-constraints.md).
- **Mixer Brush pickup engine and stroke Smoothing:** [docs/mixer.md](docs/mixer.md) and [docs/legal-constraints.md](docs/legal-constraints.md).
- **Healing Brush, Spot Healing, Patch tool, Remove Object (dialog, variations, tone match), and retouch Sample All Layers:** [docs/healing.md](docs/healing.md) and [docs/legal-constraints.md](docs/legal-constraints.md).
- **Palette mode:** [docs/palette-mode.md](docs/palette-mode.md). CMYK: [docs/cmyk.md](docs/cmyk.md).
- **RGB profiles (convert on open, Convert to Color Profile):** [docs/rgb-profiles.md](docs/rgb-profiles.md). RGB is always shown as sRGB; sRGB is detected by colorants and curves, never bytes.
- **16/32-bit editing (on; `PATCHY_DEEP_EDITING=0` off):** [docs/high-bit-depth.md](docs/high-bit-depth.md).
- **File formats, PSB, flat-image alpha, and the open-dialog filter-string contract:** [docs/file-formats.md](docs/file-formats.md); HEIF in [docs/heif.md](docs/heif.md), layered TIFF in [docs/tiff.md](docs/tiff.md), raw in [docs/camera-raw.md](docs/camera-raw.md), Affinity in [docs/af-format.md](docs/af-format.md), animation in [docs/animation.md](docs/animation.md). File Info metadata: [docs/file-info.md](docs/file-info.md); TrustMark watermarks: [docs/watermarks.md](docs/watermarks.md).
- **JPEG XR, JPEG XL, JPEG 2000, Radiance HDR, OpenEXR, AVIF:** [docs/jxr.md](docs/jxr.md), [docs/jxl.md](docs/jxl.md), [docs/hdr-formats.md](docs/hdr-formats.md), [docs/avif.md](docs/avif.md); codec rules in [docs/legal-constraints.md](docs/legal-constraints.md).
- **Proton (.rttex), DDS (.dds) and OpenRaster (.ora):** [docs/rttex.md](docs/rttex.md), [docs/dds.md](docs/dds.md), [docs/ora.md](docs/ora.md) (clean-room).
- **PDF import/export:** [docs/pdf.md](docs/pdf.md); timings in [docs/pdf-performance.md](docs/pdf-performance.md).
- **Artboards (model, compositor, PSD `artb`/`artd`, tool, exports, scripts):** [docs/artboards.md](docs/artboards.md). Artboards stay top-level groups; nothing infers one from edit history.
- **Document channels:** [docs/channels.md](docs/channels.md).
- **Resolution and measurement units:** [docs/resolution-units.md](docs/resolution-units.md).
- **Image Size resampling:** [docs/resampling.md](docs/resampling.md).
- **PSD adjustment layers, clipping masks, layer styles, and Photoshop text:** [docs/ps-compat.md](docs/ps-compat.md), [docs/file-formats.md](docs/file-formats.md), [docs/adjustments-calibration.md](docs/adjustments-calibration.md), and the Photoshop text model in [docs/text-render-calibration.md](docs/text-render-calibration.md). Photoshop 5.x `tySh` type records (no EngineData; a fixed layout) are in [docs/psd-legacy-text.md](docs/psd-legacy-text.md).
- **Filter Gallery, recipes, Saved Looks, and visual filters:** [docs/filters.md](docs/filters.md), shipped Looks in [docs/builtin-looks.md](docs/builtin-looks.md), [docs/smart-objects.md](docs/smart-objects.md), and [docs/legal-constraints.md](docs/legal-constraints.md).
- **Blend modes:** [docs/blend-modes.md](docs/blend-modes.md).
- **User-importable theme files (.patchytheme format, custom-palette override, ThemeManager apply path):** [docs/theme-files.md](docs/theme-files.md), which overlays [docs/ui-conventions.md](docs/ui-conventions.md)'s Dark/Light system.
- **Layer-style and pattern presets:** [docs/style-presets.md](docs/style-presets.md).
- **Gradients and GRD:** [docs/gradients.md](docs/gradients.md).
- **Text tool, Character panel and Paragraph panel:** [docs/text-tool.md](docs/text-tool.md), text inside shapes in [docs/area-text.md](docs/area-text.md), with the Photoshop layout/measurement calibration in [docs/text-render-calibration.md](docs/text-render-calibration.md).
- **Photoshop's Txt2 text engine block:** [docs/txt2.md](docs/txt2.md). Photoshop trusts Txt2 over the TySh: a new TySh key needs its Txt2 twin. Read it before any Txt2, text-run or list-style work. Vertical type and paragraph direction (Photoshop's vertical text model, the Ornt/WritingDirection encoding, the v4 paragraph column) are in both.
- **Bundled, wasm-aliased and user-added fonts:** [docs/fonts.md](docs/fonts.md).
- **Selection tools:** [docs/selection-tools.md](docs/selection-tools.md) and [docs/legal-constraints.md](docs/legal-constraints.md). ML Select Subject and Object Selection: [docs/ai-selection.md](docs/ai-selection.md); one prompt per solve, no negative points or mask feedback. Select Subject/Background and Remove Background are held back (CMake `PATCHY_ENABLE_SELECT_SUBJECT`, default off) pending patent review of US 12051225.
- **Free Transform Skew/Distort/Perspective (corner offsets, one session delta) and session steps (undo inside a pending transform):** [docs/free-transform.md](docs/free-transform.md).
- **Shape tools, the Fill tool and Fill command, Free Transform modifiers, pixel-grid snapping (Photoshop's whole-pixel rule, `core/pixel_grid.hpp`), Merge Down, and the Background lock:** [docs/tools.md](docs/tools.md); vector merges and Flatten Image in [docs/layer-merging.md](docs/layer-merging.md); the Crop tool in [docs/crop-tool.md](docs/crop-tool.md); tool palette layout, flyouts and icons in [docs/tool-palette.md](docs/tool-palette.md); which tool and options survive a restart in [docs/tool-options.md](docs/tool-options.md).
- **Background layer (bottom of the stack, no layer styles, padlock badge, Layer from Background, PSD records without transparency):** [docs/background-layer.md](docs/background-layer.md).
- **Pick tool (Eyedropper): options, the sample square outline and the sample loupe:** [docs/pick-tool.md](docs/pick-tool.md). The loupe reads only its lens square per move.
- **Zoom tool, Scrubby Zoom, pen and wheel zoom, the status-bar zoom box and document info field:** [docs/view-navigation.md](docs/view-navigation.md).
- **Ruler guides (drawing, drags, New Guide, PSD resource 1032):** [docs/guides.md](docs/guides.md). Guides may sit on the pasteboard: never clamp a position to the canvas.
- **Move-tool alignment guides and Layer > Arrange > Align / Distribute:** [docs/alignment.md](docs/alignment.md) and [docs/legal-constraints.md](docs/legal-constraints.md); options bar, Auto-Select, transform box hit zones, Move cursor: [docs/move-tool.md](docs/move-tool.md).
- **Vector tools, shape layers, vector masks, and Paths:** [docs/vector-tools.md](docs/vector-tools.md) (PSD fixtures in [docs/vector-fixtures.md](docs/vector-fixtures.md)) and [docs/legal-constraints.md](docs/legal-constraints.md).
- **Point-editing UI (anchor tools, hints, path context menu) and vector commands:** [docs/vector-commands.md](docs/vector-commands.md).
- **SVG import/export:** [docs/svg.md](docs/svg.md).
- **Trace Image to Shapes (raster to vector):** [docs/image-trace.md](docs/image-trace.md) and the "Vector tracing" boundary in [docs/legal-constraints.md](docs/legal-constraints.md).
- **Float windows and document activation:** [docs/float-windows.md](docs/float-windows.md).
- **Document tab context menu and Rename File:** [docs/document-tabs.md](docs/document-tabs.md). A rename never replaces a file and never marks the document saved.
- **Scanner, photocopy, Divide Scanned Photos, sprite-sheet, image-sequence, seamless-tiling import, and Files as Layers:** [docs/import.md](docs/import.md).
- **Plug-ins and legacy 8BF support (Windows):** [docs/plugins.md](docs/plugins.md), with the clean-room ABI boundary in [docs/legal-constraints.md](docs/legal-constraints.md).
- **G'MIC Filter Library:** [docs/gmic.md](docs/gmic.md).
- **JavaScript scripting and bundled scripts:** [docs/scripting.md](docs/scripting.md). The MCP connector and patchy-control skill: [docs/ai-control.md](docs/ai-control.md).
- **Automatic document recovery and atomic file writes:** [docs/document-recovery.md](docs/document-recovery.md).
- **Single-instance forwarding, CLI screenshots, and `--headless` runs:** `src/app/main.cpp`, [docs/testing.md](docs/testing.md), and the CLI section of [docs/scripting.md](docs/scripting.md).
- **Logo and icon art:** `packaging/branding/`.
- **Performance and the stress harness:** [docs/performance.md](docs/performance.md); the Move/Free Transform drag-preview machinery is in [docs/interactive-previews.md](docs/interactive-previews.md). Cold-startup phases, `patchy_perf_tests startup` and its baselines: [docs/startup-performance.md](docs/startup-performance.md); phase names are baseline keys, never rename one.
- **Testy PSD benchmark:** [docs/testy.md](docs/testy.md).
- **Flatpak packaging and update repository:** [packaging/linux/README.md](packaging/linux/README.md).
- **Linux AppImage:** [docs/appimage.md](docs/appimage.md).
