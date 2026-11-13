# Repository Instructions

Repository-wide policy: read before any project action at task start. Reread only if the repository or this file changes, or these instructions leave context.

Read `agents_local.md` at the repository root too (checkout- and developer-specific knowledge: build hosts, ssh targets); secrets live in `agents_secret.md` beside it, read only when a task needs it. Both are gitignored. This repository is public: machine names, users, addresses, local paths and credentials belong in those files, never in tracked files. A worktree has no untracked files: look in the main checkout (the parent of `git rev-parse --path-format=absolute --git-common-dir`).

## Documentation

- This file and every `docs/*.md` stay at or below 30,000 bytes. Run `python scripts/check-docs.py` after doc edits: it checks sizes, links, anchors, the labels code comments cite, and code that names a missing doc.
- Past 27,000 bytes (check-docs warns there), split a doc by subtopic into `<stem>-<subtopic>.md` children that the parent lists under "Related documents", one line each; never squeeze prose to fit the cap. This file moves detail into the doc that owns it instead.
- Implementation knowledge belongs in `docs/<topic>.md`: read the linked doc before working in its area, update it when behavior changes, never duplicate its details here.
- Keep docs dense and current-state: cut narrative history, experiment logs and restated code constants; never cut normative rules, calibration facts recorded only in the doc, or headings and labels code comments cite.

## Repository-wide rules

- Every PSD/PSB Patchy writes, including script and MCP output, must open in Adobe Photoshop without warnings or errors. Custom metadata is allowed only when Photoshop accepts the file without warning, repair, or data-discard prompts. Follow the compatibility contract in [ps-compat.md](docs/ps-compat.md).
- When adding or changing user-facing text, make it extractable ([forms](docs/localization.md#making-a-string-extractable)), run `scripts\update-translations.ps1`, and fill the new entries in every `translations/patchy_<code>.ts` in the same change. Never hand-edit catalog structure. A new command also gets its Search Commands description ([command-search.md](docs/command-search.md#descriptions)). See [localization.md](docs/localization.md).
- Tests that need files outside the project must first copy them into `local-test-fixtures`; never hardcode external paths (`C:\temp`, `D:\projects`) in tests.
- A new or changed user-facing feature also gets its line in the feature list the README links: `docs/features.md` or the `docs/features-<area>.md` page it indexes.
- Commit automatically only finished, verified work with its handoff done, never failing or half-finished states. Never push unless Seth explicitly asks in the current request; a push go-ahead covers only that commit. A release request covers build and test only: stop for an explicit go-ahead before any upload, push, publish, or update announcement.
- Never post or comment on GitHub issues, PRs, or discussions unless asked in the current request; offer a draft reply. Read the whole thread first (`gh issue view N --json body,comments`; piped `--comments` drops the body) and continue from any reply Seth already made.
- Never add AI attribution, generated-with text, or an OpenAI/Codex/Claude co-author to commits or pull requests. Commit messages are a concise subject and at most one short supporting line. Cite issues as `(#NN)`, never after a closing keyword (any form of fix, close, resolve) in a commit or PR: a push to main closes the issue, but issues close only when the fix ships; label it `fixed in main`.
- Automation confined to Patchy needs no further permission: scripted clicks, typing, and direct captures of its windows and hosted plug-in windows (KPT included). Desktop screenshots (even cropped afterward), global mouse/keyboard input (`SendInput`), Computer Use, and control of other apps need explicit authorization in the current request. Adobe Photoshop COM (`DoJavaScript`, Action Manager) is always authorized for capture, verification, and acceptance. Use task-owned instances for unattended work; protect unsaved documents and unrelated processes ([visual-qa.md](docs/visual-qa.md)).
- Build/test housekeeping needs no further confirmation: after verifying exact paths and ownership, create, rename, replace, or delete generated build artifacts, temporary executable backups, test-owned socket files, logs, and scratch files inside this repository's build or test-output directories, and stop processes launched for the current run. Not covered: user documents, fixtures, sources, unrelated files/processes, or a running user app/connector.
- **Destructive deletes.** No recursive or wildcard delete (`rm -rf`, `rmdir /s`, `del /s`, `Remove-Item -Recurse`, `remove_all`, `removeRecursively`) anywhere may target a bare variable or rely on the current directory. Spell a literal part of the path at the delete (`${ROOT:?}/build/flatpak-publish`, `%REPO%\build\package\installer`, a `patchy-` mktemp prefix) after checking each variable is set and right (the repo root holds `CMakeLists.txt`). `QDir("")` is the cwd. Tests delete folders only through `remove_test_scratch_dir`/`remove_test_scratch_tree` (tests/test_scratch_remove.hpp). Agents `rm` only a checked full literal path. Remove a worktree only with `scripts\dev\remove-worktree-safely.ps1` (`-DeleteBranch` for its merged branch): git and PowerShell follow a junction inside it and delete the target. Stop processes by PID, not name.
- Supported Windows, macOS, and Linux Debug and Release builds must have zero compiler, linker, and `lrelease` warnings. Keep warnings non-fatal. Fix Patchy-owned code explicitly; for vendored sources compiled into Patchy-owned targets, scope a suppression to one source and diagnostic ([platform.md](docs/platform.md#warning-verification)).
- Patchy's interface is its own design: it follows widely shared image-editing conventions and reads other editors' files, but never reproduces another product's icons, artwork, or screen layouts; each interface change must stand on its own usability merits.
- User-facing documentation must not use em dashes. Write plain, direct prose without hype, emoji headings, "not just X, but Y" constructions, or stock AI phrasing such as "seamlessly", "robust", "comprehensive", and "delve".
- When a test needs a capability Patchy lacks (an assertion surface, a file or state probe, a way to drive a flow without the desktop UI), prefer a documented first-class `patchy.*` scripting function over a test-only hook, so the scripting system improves and the same test can drive the real build through `--run-script`. Follow the API rules in [scripting.md](docs/scripting.md) (d.ts, guide, change log, permanent identifiers).

## Build, test, and release handoff

For code changes, ALWAYS finish with `build\opt-dev\patchy.exe` freshly built from the final working tree. Development uses the `opt-dev` preset (no `/GL` or `/LTCG`); the LTCG `release` preset is for releases only. A docs-only change, or one that cannot affect runtime behavior, skips the build/test handoff: say so.

Required handoff steps:

1. Build the opt-dev preset:

   ```powershell
   cmd /s /c 'scripts\vs-env.bat -arch=x64 -host_arch=x64 >nul && scripts\run-throttled.bat "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset opt-dev -j 20'
   ```

   Rules ([reasons](docs/building.md#local-windows-release-build)): builds, tests and batch files run only through `scripts\run-throttled.bat`, builds with `-j 20`. A second `cmake --build` on the box is refused (exit 3): wait, or offload as step 4 allows. Run from the repository root in PowerShell or real cmd, never Git Bash, and require compile/link lines or `ninja: no work to do`, not just exit 0. Iterate with `--target <what you run>` (remote: `-BuildTarget`, `-Preset opt-dev`); build everything once, at handoff. A worktree configures once with `--preset opt-dev -DCMAKE_PREFIX_PATH=<main-checkout>/.deps/Qt/6.8.3/msvc2022_64`. Never kill a running `patchy.exe` or `patchy-mcp.exe` (the link renames it aside), never package a `.stale-` file.

2. Run the test binaries from `build\opt-dev`, scoped to the change:

   ```powershell
   cmd /s /c 'cd /d build\opt-dev && ..\..\scripts\run-throttled.bat .\patchy_core_tests.exe <filter>'
   $env:QT_QPA_PLATFORM='offscreen'; cmd /s /c 'cd /d build\opt-dev && ..\..\scripts\run-throttled.bat .\patchy_ui_visual_tests.exe <filter>'
   ```

   - **Per-change verification runs only the tests for what the change touches** ([filters](docs/testing.md#running-and-filtering)): the feature's own tests and those of the functions and files the diff changes, by full name or a narrow prefix; report the filters. A family prefix (`ai_`, `hotkey`) only when the change reaches the whole family; never a full suite "to be safe".
   - Cross-cutting checks only for their surface: theme-token checks for QSS, theme roles or styled object names; `ui_offscreen_dialog_sweep` for a new or changed dialog; `ui_hotkey_defaults_have_no_conflicts` for actions and shortcuts; byte canaries for their writers. Rerun tests only after an edit that can change them.
   - Widen to the full core suite only for core-wide surfaces: `src/core`, shared helpers (`main_window_shared`, `canvas_widget_shared`, `psd_io_common`), PSD or other serialization, byte-pinned/canary paths, or refactors and file moves whose blast radius cannot be filtered.
   - Widen to the full UI visual suite only for changes that can affect rendering or UI behavior application-wide (compositing/rendering, application-wide QSS/theme or hotkeys, the visual test harness itself), never for build-system or other non-rendering changes.
   - **A real release (preparing release builds for final packaging and upload) always runs both full suites.** Filtered runs miss ordered cross-test state such as QSettings and artifact dependencies, so that is the one time the whole suite is mandatory.
   - **Judge a suite by its `[FAIL]` lines as well as its exit code** (both trustworthy only through `run-throttled.bat`) and report its `[SUMMARY]` skip count: a `[SKIP]` test checked nothing.

3. Explicitly report whether `build\opt-dev\patchy.exe` exists.

4. Changes to platform-guarded code, CMake files/presets, or packaging also need the affected best-effort remote build (`scripts\remote\remote-build.ps1 -Target mac` and/or `-Target linux`; Linux tests on the Flatpak's Qt: [linux-testing.md](docs/linux-testing.md)); report failures, though Windows remains the release gate. When Seth asks (a standing permission in `agents_local.md` counts), `-Target windows -Preset opt-dev` builds on the offload host ([machinery](docs/building.md#remote-build-machinery)).

Releases build the `release` preset into `build\release` (release gate, packaging source); never say a release was created unless that build succeeded.

## Universal engineering invariants

- **Never read a variable in the same call that moves it.** C++ argument evaluation order is unspecified. Compute the read result first, then call with that local and `std::move(value)`.
- **Read revision-bearing objects through const access** (`std::as_const`): mutable Layer accessors bump revisions on access ([performance.md](docs/performance.md#reads-must-not-bump-layer-revisions)).
- **Nothing proportional to all layer pixels may run per repaint.** Cache or bound work reachable from `paintEvent` ([cache keys](docs/performance.md)).
- **Core algorithms must be deterministic across toolchains.** splitmix64 with explicit uniform mapping, never `std::uniform_*_distribution`; integer math or deterministic-double envelopes with fixed tie-breaks for geometry and graph algorithms. Compiler flags: [platform.md](docs/platform.md#floating-point-determinism).
- **Persisted identifiers never change.** Hotkey command ids (`script.` ids included), preset ids, stress-test step ids, New Document preset ids, script/API identifiers (`saveAs` option keys included), settings keys, file-format tokens (the `.patchytheme` keys and `preferences/customThemeId` included), and the organization name that keys the per-user app-data folder ([fonts.md](docs/fonts.md)) are compatibility contracts. `BlendMode` and `BrushDynamicControl` are append-only enums.
- **No hardcoded chrome colors in `src/ui`.** Every UI color is a named role in `src/ui/theme_palette.hpp`: an `@role_name` token in QSS, `theme()` when painted. [Exemptions](docs/ui-conventions.md#color-scheme).
- **Byte-stability canaries change only deliberately.** `psd_layered_writer_bytes_are_stable`, `gif_encoder_bytes_are_stable`, `dds_writer_bytes_are_stable`, and `tool_write_paths_digest_baseline` pin default output. Never re-pin them to make a refactor pass.
- **The CPU compositor is the reference renderer.** Patchy renders on the CPU only ([performance-compositor.md](docs/performance-compositor.md#compositor-row-kernels-mask-plane-and-the-byte-identity-corpus) says why). Optimized paths, and any future GPU path, must match its pinned output.
- **File paths cross the Qt boundary as UTF-16, never as `std::string`.** Convert with `to_filesystem_path`/`to_qstring` (`src/ui/qt_paths.hpp`, never `toStdWString`) and `path_to_utf8` (`src/support/path_utils.hpp`, never `path::string()`). Every new file reading or writing entry point gets a Unicode-path test built on `tests/unicode_path_names.hpp`. Why: [platform.md](docs/platform.md).
- **A path read from a file never reaches a network server unasked.** Check it with `classify_path_location` (core) or `network_link_path_allowed` (UI) before any stat or read: on Windows a stat of `\\host\share` signs in to that host ([network links](docs/smart-object-editing.md#network-links)).
- **Never truncate a user's file in place.** Every document writer goes through `write_file_bytes_atomically`/`AtomicFileReplacement` (src/support/atomic_file_write.hpp) or `QSaveFile` ([document-recovery.md](docs/document-recovery.md#atomic-file-writes)).
- **Serialization is fixed-width and cross-platform.** Never write `size_t`, `long`, `wchar_t`, native structs, or host-endian values into a file format; PSD I/O uses explicit big-endian primitives ([platform.md](docs/platform.md), the format's doc).
- **Detach copy-on-write pixel storage on the launching thread before a parallel write.** Call `pixels.data()` once there and hand the workers spans ([why](docs/performance.md#parallel-strip-rendering)).
- **A 32-bit image is linear floats; 16 and 32-bit pixels are wider than a byte.** Never hand a `Format_RGBA32FPx4` image to QPainter or a Qt 8-bit conversion, or walk deep pixels a byte per channel: encode first (`display_image_from_deep_qimage`, `display_qimage_from_pixel_buffer`) and use the typed rows ([high-bit-depth.md](docs/high-bit-depth.md)).

## Cross-cutting implementation rules

- Runtime assets are shared copy-once CMake targets that executables join through the `patchy_copy_*` helpers; never add per-target POST_BUILD copies into the shared output folder ([code-organization.md](docs/code-organization.md)).
- Session data must outlive canvas event delivery: preserve MainWindow's canvas-detach and session-close destruction orders; references into `SmartObjectStore` do not survive `add_embedded` ([code-organization.md](docs/code-organization.md)).
- Every submenu action under the menubar gets `QAction::NoRole`: a submenu merged into the macOS app menu crashes (issue 29; [platform.md](docs/platform.md)).
- Read modifier state folded from the current event, not `QApplication::keyboardModifiers()` ([ui-conventions.md](docs/ui-conventions.md)). New non-modal dialogs use `run_non_modal_dialog`; closing-sensitive dialogs funnel through `done()` ([ui-conventions-dialogs.md](docs/ui-conventions-dialogs.md)).
- Never rely on a destructor for anything the user could lose: the bounded post-`app.exec()` worker wait can end the process without them. Startup, quit, host lookups, Qt's automatic quit: [code-organization.md](docs/code-organization.md#app-lifecycle-startup-and-quit).
- A drag whose move does more than move an overlay must coalesce its moves (Wayland sends each motion): `coalescing_drag_active` (canvas) or `coalesce_widget_drag_moves` (widgets), every point kept if it needs its path, a queued-moves test ([rule](docs/interactive-previews-drag-coalescing.md#rule-for-new-drags)).
- Bash heredocs rewrite backslash escapes (write a file and run it); Windows Python text mode writes CRLF (use `newline=''`).

## Routing index

*(legal)* marks a feature with a binding entry in [legal-constraints.md](docs/legal-constraints.md), which indexes its child docs, the `docs/patent-research*.md` records and [legal-licensing.md](docs/legal-licensing.md): read that entry before changing the feature, and read legal-constraints.md before any patent, licensing, trademark, dependency or bundled-asset work or a feature near a legal boundary.

**Process**

- [Building](docs/building.md): build scripts, the `run-throttled.bat` lock, presets, the version header, batch exit codes, remote builds and host locks.
- [Testing](docs/testing.md) (offscreen behavior, suite failures); [visual QA, app driving, CLI screenshots, `--headless`](docs/visual-qa.md); [README screenshots](docs/readme-showcase.md); [Photoshop COM runs, unknown-data prompt check](docs/photoshop-com.md).
- [Releases](docs/release-process.md): read it, with the children it names, in full before a version bump or any release batch file.
- [Platform-guarded code, macOS/Linux](docs/platform.md); [Wayland](docs/wayland.md); [WebAssembly](docs/wasm.md) (wasm-core preset, emsdk), [memory, telemetry](docs/wasm-memory.md), [files, threads](docs/wasm-files-threads.md), [input, focus, hotkeys](docs/wasm-input.md).
- [Code organization](docs/code-organization.md): MainWindow/CanvasWidget/PSD splits, function moves, shared helpers, broad refactors; cleanup: [refactor-backlog.md](docs/refactor-backlog.md).
- [UI conventions](docs/ui-conventions.md): QActions, dialogs, options bar, list rows, status messages, QSS, colors, Dark/Light; [theme files](docs/theme-files.md) (`.patchytheme`, packages, ThemeManager) over it; [bundled themes](docs/bundled-themes.md). [Localization](docs/localization.md): catalogs, languages, `LocalizationManager`, unit suffixes. [Hotkey files and shortcut sets](docs/hotkey-sets.md) *(legal)*: sets are Patchy's own tables from published manuals only.
- [Contributor PR review](docs/pr-review.md); [update prompt, one-click and installer updates](docs/auto-update.md).
- [Performance](docs/performance.md), [stress harness](docs/stress-test.md), [Move drag previews](docs/interactive-previews.md) *(legal)*, [cold startup](docs/startup-performance.md) (`patchy_perf_tests startup` baselines; phase names are baseline keys: never rename one), [Testy benchmark](docs/testy.md).
- [OpenAI Image Tools](docs/openai-images.md) *(legal)*: built in, the user's own key; results are plain layers.
- [Scripting](docs/scripting.md); single-instance forwarding: its CLI section. [MCP connector, patchy-control skill](docs/ai-control.md); [companions](docs/companions.md); automation [feedback](docs/automation-feedback.md), [brushes](docs/brush-automation.md), [vectors](docs/vector-automation.md), [batch appearance](docs/batch-appearance.md).
- [Document recovery](docs/document-recovery.md); [Flatpak, update repository](packaging/linux/README.md); [AppImage](docs/appimage.md); logo and icon art: `packaging/branding/`.

**Formats**

- PSD *(legal)*: [contract, descriptors, write and corruption rules](docs/ps-compat.md), [layer blocks](docs/psd-layer-blocks.md), [layer styles](docs/psd-layer-styles.md); Photoshop 5.x `tySh` records (a fixed layout, no EngineData): [psd-legacy-text.md](docs/psd-legacy-text.md).
- [File formats](docs/file-formats.md): PSD adjustment layers and clipping, the open-dialog filter-string contract; [PSB, color modes, flat-image alpha](docs/file-formats-psd.md); [HEIF](docs/heif.md) *(legal)*, [TIFF](docs/tiff.md), [raw](docs/camera-raw.md), [Affinity](docs/af-format.md), [Paint.NET](docs/pdn-format.md), [animation](docs/animation.md), [File Info](docs/file-info.md) ([watermark scan](docs/file-info-watermarks.md) *(legal)*), [TrustMark](docs/watermarks.md) *(legal)*, [PDF](docs/pdf.md) ([timings](docs/pdf-performance.md), [viewer](docs/pdf-viewer.md), [Print as Book](docs/pdf-book-print.md)), [SVG](docs/svg.md), [Export for Web](docs/web-export.md).
- *(legal: codec rules)* [JPEG XR](docs/jxr.md), [JPEG XL](docs/jxl.md), [HDR, EXR, JPEG 2000](docs/hdr-formats.md), [AVIF](docs/avif.md). Clean-room: [.rttex](docs/rttex.md), [DDS](docs/dds.md), [OpenRaster](docs/ora.md); [GIMP .xcf](docs/xcf.md) *(legal)*: clean-room; GIMP-only blend modes are Patchy modes a PSD save bakes; [Krita .kra](docs/kra.md) *(legal)*: clean-room from files Krita writes; never read Krita or libkra source.
- [Import](docs/import.md): scanner, photocopy, Divide Scanned Photos *(legal)*, [Scan a Book to PDF](docs/book-scan.md) *(legal)*, sprite sheets, image sequences, seamless tiling, Files as Layers; [clipboard](docs/clipboard.md) *(legal)*.
- [Artboards](docs/artboards.md) *(legal)* (PSD `artb`/`artd`): they stay top-level groups; nothing infers one from edit history. [Channels](docs/channels.md); [resolution units](docs/resolution-units.md); [resampling](docs/resampling.md); [Background layer](docs/background-layer.md) (padlock, Layer from Background, PSD records).

**Color and filters**

- [Palette mode](docs/palette-mode.md); [CMYK](docs/cmyk.md) *(legal: soft proof)*; [RGB profiles](docs/rgb-profiles.md) (Convert to Color Profile): RGB is always shown as sRGB; sRGB is detected by colorants and curves, never bytes; [16/32-bit](docs/high-bit-depth.md) (on; `PATCHY_DEEP_EDITING=0` off); [blend modes](docs/blend-modes.md).
- Adjustments *(legal)*: [calibration](docs/adjustments-calibration.md) (auto adjustments too), [#250 Photoshop kinds, placeholders](docs/adjustment-kinds.md), [presets](docs/adjustment-presets.md).
- [Smart Objects and Smart Filters](docs/smart-objects.md) *(legal)*: [editing, linked/placed authoring](docs/smart-object-editing.md); [native filters](docs/smart-filters-native.md) (descriptors, FEid cache, render semantics) and their tables: [pixelate/render](docs/smart-filters-pixelate-render.md), [distort/stylize](docs/smart-filters-distort-stylize.md), [Image > Adjustments](docs/smart-filters-adjustments.md).
- [recipes, Saved Looks, visual filters](docs/filters.md) *(legal)*, [Filter Gallery](docs/filters-gallery.md) *(legal)*, [shipped Looks](docs/builtin-looks.md); [layer-effect render calibration](docs/layer-effects-render.md) (Blend If, Satin, Bevel), [Stroke, Shape Burst](docs/layer-effects-render-stroke.md), [shadows, glows](docs/layer-effects-render-shadows-glows.md); [style presets](docs/style-presets.md), [pattern presets, bundled textures](docs/style-presets-patterns.md); [gradients, GRD](docs/gradients.md).
- [8BF plug-ins (Windows)](docs/plugins.md) *(legal: clean-room ABI)*; [G'MIC](docs/gmic.md) *(legal)*; [GEGL](docs/gegl-filters.md) *(legal)*: Patchy's own GEGL, GPL-3 module included; releases publish its source (`PatchyGeglSources.zip`); sources only from legal-licensing.md's list, logged in [gegl-provenance.md](docs/gegl-provenance.md); never GIMP's or GEGL's source.
- [Liquify](docs/liquify.md) and [Warp](docs/warp.md) *(legal)*; [Perspective Planes](docs/perspective-planes.md) *(legal)*: an edit stays on the plane it starts on.

**Tools, text, vectors**

- [Brushes](docs/brushes.md) *(legal)*: tips, dynamics, Flow/Airbrush, Pattern Stamp, ABR; [Mixer Brush, Smoothing](docs/mixer.md) *(legal)*; [retouching](docs/healing.md) *(legal)*: Healing Brush, Spot Healing, Patch, Content-Aware Move, retouch Sample, Clone source; [Remove Object](docs/healing-remove-object.md) *(legal)*; [Red Eye](docs/red-eye.md) *(legal)*.
- [Selection tools](docs/selection-tools.md) *(legal)*; [Select Subject, Object Selection](docs/ai-selection.md): one prompt per solve, no negative points or mask feedback; Select Subject/Background and Remove Background are held back (CMake `PATCHY_ENABLE_SELECT_SUBJECT`, default off) pending patent review of US 12051225.
- [Free Transform](docs/free-transform.md) *(legal)* (Skew/Distort/Perspective, pending-transform steps), [preview](docs/free-transform-preview.md) *(legal)*; [tools.md](docs/tools.md): paste placement, shapes, Fill, Merge Down, Background lock; [transform modifiers, drag readout, pixel-grid snapping](docs/tools-transform-modifiers.md) (`core/pixel_grid.hpp`); [vector merges, Flatten](docs/layer-merging.md); [Crop](docs/crop-tool.md) *(legal)*; [tool palette](docs/tool-palette.md), [flyouts](docs/tool-flyouts.md); [tool state across restarts](docs/tool-options.md).
- [Alignment guides, Align/Distribute](docs/alignment.md) *(legal)*; [Move tool](docs/move-tool.md) (Auto-Select, hit zones, cursor); [Pick tool (Eyedropper), Ruler, Straighten](docs/pick-tool.md): the loupe reads only its lens square per move; [ruler guides](docs/guides.md): guides may sit on the pasteboard, so never clamp a position to the canvas.
- [Text tool](docs/text-tool.md), [its panels](docs/text-tool-panels.md); [area](docs/area-text.md) and [path](docs/path-text.md) text; [Convert to Shape, Create Work Path](docs/text-outlines.md) (glyph outlines, never a trace); [Type Mask tool](docs/type-mask.md): a Type session that commits a selection, no layer; [calibration](docs/text-render-calibration.md). [Txt2](docs/txt2.md): read before any Txt2, text-run or list-style work (a new TySh key needs its Txt2 twin). [Fonts](docs/fonts.md) (bundled, wasm-aliased, user-added); [font resolution](docs/font-resolution.md) (DirectWrite, GDI names), [missing fonts](docs/font-resolution-missing-fonts.md).
- [Vector tools, shape layers, masks, Paths](docs/vector-tools.md) *(legal)* ([PSD fixtures](docs/vector-fixtures.md)); [point editing, vector commands](docs/vector-commands.md); [Dynamic Vector Preview](docs/vector-preview.md); [Trace Image to Shapes](docs/image-trace.md) *(legal: "Vector tracing")*.

**Panels**

- [Layers panel](docs/layer-panel.md); [drags to another document, Alt-drag duplicate, layer export](docs/layer-drag-duplicate.md); [New Document](docs/new-document.md); [panel column](docs/right-docks.md), [tab groups](docs/right-docks-reordering.md); [Options bar](docs/options-bar.md); [zoom and navigation](docs/view-navigation.md), [status-bar zoom and info fields](docs/view-navigation-status-bar.md), [view rotation](docs/view-navigation-rotation.md) (map through `view_transform()`, never `zoom_`/`pan_` by hand); [float windows, document activation](docs/float-windows.md); [document tabs, Rename File, Revert](docs/document-tabs.md): a rename never replaces a file and never marks the document saved.
