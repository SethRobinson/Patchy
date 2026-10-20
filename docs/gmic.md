# G'MIC filter engine

Patchy vendors the G'MIC interpreter and its standard filter library so that G'MIC's
filters can run inside Patchy with Patchy's own interface (GitHub issue 87). The user-installed
`.8bf` route in [plugins.md](plugins.md) is separate and unaffected.

Status: shipped in desktop builds (`PATCHY_ENABLE_GMIC` ON; OFF for the web build, which still
compiles the dialog code and its translations but has no engine or Filter menu command).
Remaining gaps are under "Known gaps".

## What is vendored

`src/filters/gmic/` holds G'MIC 4.0.5 (`gmic_4.0.5.tar.gz` from the GitHub release, sha256
`c6771c48693ed615b2e85d1db7a9626358d4606eb2c75dcf009b2924f396da59`): `gmic.h`, `gmic.cpp`,
`CImg.h`, `gmic_stdlib.gmic` and `COPYING`. All four sources are dual-licensed CeCILL-C or
CeCILL 2.1; Patchy uses them under CeCILL-C (LGPL-like: linking is allowed, changes to the
library are published, which this repository does). Nothing from G'MIC-Qt (GPL 3) is used,
including its filter translations.

Not vendored: the community filters and upstream's compressed `gmic_stdlib_community.h`;
`patchy_stub/` replaces it with an empty array and the engine installs the text itself.

`src/filters/gmic/pocketfft/` holds `pocketfft_hdronly.h` and `LICENSE.md` from
mreineck/pocketfft, cpp branch, commit `c90e55b3d529f8efa40ed01a20de22405f45fc65` (BSD-3-Clause;
header sha256 `3e9a05318d8e3b1446bda1c4617e6a103cdd23599ae0a776a92a6e8800e92fdc`), unmodified.

Packages carry the license texts: `licenses\gmic\` in the Windows stage
(`build-release.bat`, checked by `packaging/package-selftest.js`), `Resources/licenses` in
the macOS bundle and `share/licenses/com.rtsoft.patchy` on Linux (both CMakeLists.txt).

### Patchy's changes to the vendored sources

Every change carries a `PATCHY` marker; `git diff` against the tarball shows exactly these:

- `CImg.h`, `_matchpatch`: the patch correspondence search (PatchMatch) body is compiled out
  under `patchy_gmic_no_patch_algorithms` and throws "Not available in this build".
- `gmic.cpp`, `inpaint_patch`: patch-based inpainting, the same.
- `gmic.cpp`, `add_commands`: `gmic_command_policy.hpp` replaces restricted definitions
  with error stubs, including continuation lines. Built-in `denoise` is refused at dispatch.
- `CImg.h`: the four `popen` call sites go through `cimg::patchy_popen`, which starts nothing
  when `cimg_no_system_calls` is defined.
- `CImg.h`, FFT: under `patchy_gmic_use_pocketfft` both `CImg<T>::FFT` entry points (per axis
  and N-dimensional) call the added `_patchy_pocketfft`, and the pocketfft header is included
  after the FFTW include block. See "FFT".

To update G'MIC: replace the five files from the new tarball, reapply these changes
(`scripts/dev/gmic/apply-patchy-changes.ps1 -Dir src\filters\gmic`, then
`apply-patchy-fft.ps1` and `apply-command-policy.ps1` with the same argument; all refuse when a line
they expect has moved), rerun
`scripts/dev/gmic/build_allowlist.py` and the `gmic_` tests, and compare the regenerated
lists with the committed ones.

## Build

`cmake/patchy_gmic.cmake`:

- `patchy_gmic` exists in every build: `gmic_catalog.cpp` plus either `gmic_engine.cpp` or
  `gmic_engine_stub.cpp` (`engine_available()` is false, `run` only reports that). Keeping the
  target unconditional keeps the dialog sources and their strings in every build, which the
  translation catalog gate needs: lupdate reads all of `src/`, so strings cannot depend on an
  option. Never put a `tr()` behind `PATCHY_HAVE_GMIC`; gate at run time on `engine_available()`.
- Option `PATCHY_ENABLE_GMIC` (ON; OFF under Emscripten) adds `patchy_gmic_vendor`: `gmic.cpp`
  alone, isolated like `patchy_libraw` (C++17, warnings off for that target only). A build
  directory configured while the default was OFF keeps that cached value until it is
  configured once with `-UPATCHY_ENABLE_GMIC`. Definitions: `gmic_core`,
  `cimg_display=0`, `cimg_use_abort`, `cimg_use_zlib`, `gmic_noexec`, `cimg_no_system_calls`,
  `patchy_gmic_no_patch_algorithms`, `patchy_gmic_use_pocketfft`. No FFTW, curl, OpenCV, PNG,
  JPEG or TIFF.
- **Threads.** `PATCHY_GMIC_THREADS` (AUTO, ON or OFF; the release builds pass ON) adds
  `cimg_use_openmp` to `patchy_gmic_vendor` alone, so CImg's loops and the math parser run
  on every core. Windows: MSVC `/openmp`, and `build-release.bat` (`:CopyMsvcRuntimeDlls`)
  stages `vcomp140.dll` from the redist's `Microsoft.VC*.OpenMP` folder beside the CRT.
  Linux: `libgomp`, part of the Flatpak runtime. macOS: Apple's compiler ships no OpenMP
  runtime and Homebrew's `libomp` targets the build host's macOS, not 12.0, so `scripts/setup-openmp-mac.sh` (run once per build host;
  `setup-mac.sh` calls it) builds libomp from the pinned LLVM release source for the
  deployment target into `.deps/libomp-<version>-macos<target>/`. The engine compiles with
  `-Xclang -fopenmp` and links `libomp.dylib`, which the bundle carries in
  `Contents/Frameworks` (rpath `@executable_path/../Frameworks`; the test binaries use the
  `.deps` folder). Never link libomp statically: a static libomp does not unregister a
  thread that ends, and the next thread to enter a critical section crashes (each run
  has a thread of its own). `make-dmg.sh` refuses a build
  whose cache lacks `PATCHY_GMIC_OPENMP_ENABLED=TRUE`. G'MIC compiles its in-loop abort
  checks out on Apple platforms (`gmic.h`), so a stale run there stops at the next command,
  which also drops CImg's try/catch in OpenMP loops (a formula error in a parallel fill
  ended the process); force-included `patchy_stub/patchy_gmic_openmp_catch.h` restores it.
  The thread count stays level across runs: each run's
  interpreter thread ends and its team with it.
  `patchy_stub/zlib.h` maps zlib onto the vendored miniz (G'MIC embeds palettes, shapes and
  fonts as compressed data), so the target links `patchy_psd`, which compiles miniz. It also
  defines `PATCHY_HAVE_GMIC=1` and adds `src/ui/gmic.qrc`, which embeds the library text and
  the allowlist under `:/gmic` (registered by `Q_INIT_RESOURCE` in `gmic_library.cpp`, since
  `patchy_ui` is a static library).
- `gmic.cpp` and `patchy_stub/` are excluded from the lupdate source list.

4.0.5: the engine adds 5 to 6 MB to a binary; `gmic.cpp` takes about a minute and up to
2.1 GB of compiler memory (MSVC, GCC 13, Apple clang 21) and compiles under Emscripten.

## Code

- `gmic_catalog.{hpp,cpp}` (Qt-free, no engine dependency): `parse_catalog` reads the `#@gui`
  declarations into `Filter` and `Parameter` values; `command_line` builds the apply or preview
  call from parameter values; `parse_allowlist` / `retain_allowed` limit a catalog.
  Declaration rules that are easy to get wrong:
  - A line ending in a backslash continues on the following plain `#` comment lines.
  - A typedef's arguments may run over several `#@gui :` lines, and typedefs are separated by
    commas or by white space alone.
  - A typedef ends at the first closing character matching its opener (`()`, `[]` or `{}`);
    quotes inside note text are not balanced, so quote tracking cannot find the end.
  - Folder lines: each leading `_` leaves one folder, then the name (if any) opens one.
  - `_none_` as the command marks an information page (skipped); as the preview it means none.
  - A `value(...)` parameter is ONE argument whatever it holds: `value(50,50)` is passed quoted
    and the filter splits it itself (`...=${1-11}` then yields twelve values). Passing it
    unquoted shifts every later argument by one; Stereographic Projection and Heightfield
    Warp failed that way until October 2026.
  - `#@gui_xx` (per-language) lines are ignored.
  - `Filter::id` is the apply command, with `#2`, `#3` for later filters on the same command.
    It becomes a persisted identifier once scripts or presets can name a filter.
- `gmic_engine.{hpp,cpp}`: `Engine::run(command_line, images, control)` runs one command line
  on planar float images and returns the output images, their names and the status string.
  - Each run uses a fresh interpreter on a thread with a 32 MB stack: G'MIC recurses deeply and
    a default 1 MB stack overflows on ordinary filters.
  - The interpreter keeps one process-wide standard library, which interpreters it creates
    internally (the math parser's `run()`) also start from. The engine installs its text in
    `gmic::stdlib` before a run, so every `Engine` should be given the same text.
  - `RunControl` carries abort and progress as atomics; a watcher thread mirrors them to the
    plain `bool` and `float` the interpreter's API takes.
  - Patchy names input images `name(<layer>)` as G'MIC-Qt does (`gmic_input_name`): a bare
    name reads "[unnamed]", an empty one fails some filters. No `pos()` (see centring).

## The Filter Library dialog

Filter > "G'MIC Filter Library..." (`filterGmicLibraryAction`, hotkey id `filter.gmic_library`,
a document action, created only when `gmic::engine_available()`).

- `src/ui/gmic_library.{hpp,cpp}`: `gmic_library()` loads the embedded text once and keeps the
  allowlisted catalog.
- `src/ui/gmic_parameter_panel.{hpp,cpp}`: `GmicParameterPanel::set_filter` generates one form
  row per declared parameter. Float and int are a slider with a fixed-width spin box (float
  decimals follow the range and are never coarser than the declaration writes its numbers),
  choice a combo, bool a check box, color a swatch through `request_patchy_color` (each pick previews live, picker Cancel restores) plus an
  Alpha spin when declared, text a line or multi-line edit, point two percent fields, button
  a one-shot flag (1 for the run after its press; `release_buttons` puts it back once a
  run has read it, and `gmic::release_button_values` does the same for remembered values),
  value hidden. Notes are rich text (`gmic_note_rich_text` turns Pango
  `<span color=...>` and the literal `\n` into Qt markup), links open externally, separators
  are a themed rule. `values()` returns one G'MIC argument per value-carrying parameter.
  Parameter names, choices and notes are G'MIC's English text, deliberately untranslated;
  Patchy's own labels around them are translated.
- `src/ui/gmic_filter_library_dialog.{hpp,cpp}`: search field (every word must match the name
  or a folder; Enter picks the first match, and `keyPressEvent` keeps that Enter from the
  default OK button), folder tree in library order, the panel in a scroll area, Reset, Preview,
  About G'MIC, OK and Cancel, plus an Input layers combo (tooltip explains the layer rules)
  that starts from the filter's declared `input_mode` and is part of the `GmicFilterChoice`.
  About G'MIC (`gmicAboutDialog`) credits the GREYC team, the pronunciation (the author says
  "gimmick" and accepts any, on discuss.pixls.us), the license and gmic.eu. It never runs the engine: it reports a
  `GmicPreviewRequest` (the chosen filter, values and input layers, or none, and whether
  Preview is checked) through `set_preview_callback` whenever
  the filter, a value or the Preview box changes. Slider drags are coalesced
  (`CoalescedPreviewEmitter`); a parameter declared `_typedef` does not request a preview.
  `apply_status` feeds a run's returned status back into the panel (`{v1}{v2}...` with optional
  `_0/_1/_2` visibility per value), and `set_preview_message` shows a failed preview's reason
  under the filter name.
- `src/ui/main_window_gmic.cpp`. Every engine run, pixel conversion included, happens off
  the UI thread; nothing proportional to the layer runs there except the final buffer swap.
  - `render_gmic_filter` (file-local, thread-safe, translates nothing) is the whole run from a
    `GmicRunInput`: layers through `load_rgba_row` in the Encoded domain (every bit depth,
    0..255 floats), the engine, outputs back in layer formats (1, 2, 3 or 4 channels; no
    alpha means opaque).
  - **Layers (decided October 2026): a filter changes every chosen layer it may change,
    like G'MIC-Qt's "In place" output.** The input mode (`.` active, `*` all, `+` active and
    the one below, `-` active and the one above, `v` visible, `i` hidden) picks layers from
    `gmic_layer_snapshots`: top of the stack first, groups opened, adjustment layers skipped,
    "visible" meaning the layer's eye and every enclosing group's. A layer is writable
    (`gmic_layer_writable`) with editable RGB pixels and no pixel lock; text, shape and Smart
    Object layers are read as their pixels and never changed. No layer chosen: "No layer
    matches the chosen input layers"; none writable: refused too. How a run reads them
    (`gmic_reads_layers_together`):
    - **A filter made for one image** (declared input `.`; Sepia, CRT Scanlines) runs once
      per writable chosen layer, alone at its own size (`render_gmic_each_layer`). Its first
      image replaces that layer, the others become new layers. Never pad these onto a
      document-sized image: about 140 fill transparency, so a small layer came back
      canvas-sized.
    - **A filter made for several layers** (declared `*`, `+` and so on: Blend, Morph, Align
      Layers) reading more than one gets them lined up (`render_gmic_lined_up`): each on a
      transparent document-sized image, results cropped to content (`trim_to_content`).
      G'MIC's merge filters resize inputs to the largest and ignore `pos()`, so they need
      equal frames. K read, M returned: M >= K, each writable layer takes the image in its
      place; M < K, the first image replaces the active layer (`merged`); other images become
      new layers (anchored on the lowest read layer, or the active one).
    - **Stacking.** G'MIC lists images top of the stack first (Ball moves its sphere to
      `[0]` over the original), so `insert_gmic_new_layers` puts new layers below their
      anchor, each below the last. A Background stays at the bottom (Photoshop's rule; the PSD
      reader takes a top full-canvas Background as a reversed legacy file): it takes the LAST
      image and the rest go above it in that order (Ball's sphere on top); no new layer is
      named Background.
    - Output names carry layer settings (`gmic::parse_output_layer_name`):
      `mode(...)`, `opacity(...)`, `pos(x,y)`, `name(...)`. New layers take all four
      (`blend_mode_from_gmic` maps G'MIC's blend names; unknown ones, such as grainmerge,
      become Normal); a replaced layer takes only the position, and gains an alpha channel
      when it has none and the result has a transparent pixel (Ball on a JPEG layer was
      black around the sphere; `original_needs_alpha_for`). An output with no `pos` that
      differs in size from what it replaces is centred on it (a frame grows evenly; CRT
      Scanlines multiplies the size by its Upscale Factor, 4 by default, past the canvas).
    - The selection applies to each replaced layer in its own frame (outside restored, edge
      blended by coverage). A result of another size (CRT Phosphors, CRT Scanlines, frames)
      is placed where it lands without a selection and seen through the layer's frame
      (`filter_bounds` keeps the whole result's): the layer keeps its size, uncovered
      selected pixels keep the original, and a result note says so (decided October 2026, as
      Krita masks output by the selection). New layers ignore it.
    - `commit_gmic_render` writes every changed layer and new one under one undo snapshot;
      the status bar gives the count or new size.
  - **Memory.** A run holds float copies (16 bytes a pixel for RGBA): Patchy's planar
    image, the interpreter's copy, and the result. `gmic_run_memory_mb` estimates three
    times the largest layer (or every lined-up one) and refuses a run above 60 percent of
    installed memory before allocating (`PATCHY_GMIC_MEMORY_LIMIT_MB` for tests).
    `render_gmic_filter` never throws (an exception escaping a worker ends the process).
    Worker waits sleep on a future; a bare `processEvents` loop spins a core.
  - **Added layers in the preview.** A full preview inserts them where OK will (ids in
    `Shown::added`); drafts add none. They come out when the shown result changes, Preview
    goes off or the dialog closes, always before the commit inserts its own. The Layers
    panel keeps only its eyes (`preview_lock_allows_layer_visibility_`,
    `LayerListWidget::set_visibility_only`) to inspect them; a preview layer's eye carries
    to the next result with as many layers and to the layer OK adds. Eyes do not change
    which layers "visible" reads: those are fixed when the dialog opens.
  - **Result notes.** Each full preview's `gmic_result_notes` fills the dialog's info banner
    (`gmicResultNotes`): layers changed or combined, a size change (and any part outside
    the canvas), layers added, chosen layers that cannot change. Whether a filter adds
    layers depends on its settings, so it is reported from a run, never guessed.
  - `gmic_target_layer` gates like Liquify (Quick Mask, editable RGB pixel layer, no Smart
    Object, locks, the rasterize prompt for text and shapes). The dialog command gates before
    it opens, since the preview needs the layer.
  - **Preview** (`gmic_filter_library_dialog`) is the filter dialogs' shared pattern: an
    `AsyncPixelPreviewState<GmicPreviewRequest>` whose `start` spawns a
    `run_tracked_background_worker`, brackets it with `CanvasWidget::begin_preview_render` /
    `end_preview_render` (the canvas "Rendering preview..." badge), and whose queued completion
    writes the result into the real layer only when its generation is still current and nothing
    newer is pending. Departures from that pattern, all in the dialog's preview callback:
    - New settings ask the run in flight to stop and start at once beside it: a stale run
      stops only at the interpreter's next check (up to about 10 s on 12 MP). At most two live
      (`kMaxLivePreviewRuns`); beyond that the newest request waits as `pending`. Showing a
      result bumps the generation, so stale runs never replace it.
    - Preview off only puts the original back: the run carries on, and its result (draft
      or full, `Shown::preview`) is kept, so Preview on shows it at once with no new run.
      A run for settings other than the dialog's is stopped while Preview is off.
    - The last full result is kept with the choice it belongs to, so OK commits that very
      result instead of running the filter again, and returning to those settings shows it
      without a run. A dialog timer copies the running preview's `RunControl::progress`
    (G'MIC's `progress`, 0..100, from some filters only) to the canvas badge ("Rendering
    preview... 42%") through `set_preview_render_progress`; the Apply progress box shows it too.
    The preview runs the filter's apply command (never its preview command); OK commits
    only a full-resolution result.
    The snapshots taken when the dialog opens are the originals every run reads (previews
    change the real layers); `Shown::layers` tracks which layers show a preview.
    `lock_preview_dialog_edits` and a scope guard restore them on every exit, and the
    originals always go back before the commit so the undo snapshot is clean.
  - **Draft, then full quality.** On an input above about 1.5 megapixels the preview runs
    twice (`Stage` in `gmic_filter_library_dialog`): a draft with `GmicRunInput::scale`
    below 1 (the float images reduced to about a megapixel in all by `reduced_image`, each
    changed layer's result enlarged back to its full frame by `enlarged_image`, added layers
    left out), shown at once, then the full run with the values the draft returned, under the
    badge "Refining preview...". Sizes a filter takes in pixels are not rescaled, so a
    draft looks somewhat stronger than the result. No draft when no chosen layer is
    writable, or when this filter's last full run this session was quick
    (`gmic_full_ms_per_megapixel_`, under 400 ms estimated). `PATCHY_GMIC_DRAFT_PIXELS`
    overrides the megapixel budget for tests. OK while the full run for the accepted
    settings is in flight waits for that run (`wait_for_gmic_run`) instead of starting
    another; a request that repeats what is already rendering is dropped.
  - Non-modal (`run_non_modal_dialog`): the canvas can be zoomed and panned.
  - **Point handles.** `GmicParameterPanel::points()` lists the visible point parameters in
    percent of the image the filter works on (the active layer, each chosen layer taking the
    same percentages of its own frame, or the document when layers are lined up); the
    dialog reports them through `set_points_callback` whenever the filter, a
    position or the input layers change, and `move_point` is the way back. MainWindow turns
    them into `CanvasWidget::set_preview_point_handles` (canvas_widget_preview_points.cpp):
    markers in document coordinates that win the left press over the active tool within
    10 px, report every move, and are removed when the dialog ends. While markers exist they
    own the cursor (`apply_preview_point_cursor`): open hand over one, closed hand while
    dragging, arrow elsewhere (the Zoom tool keeps its own); no brush ring is drawn. A marker being dragged
    keeps the pointer's position when its owner echoes the move back. A point the
    filter declares hidden is not one, and the two blur filters that are not offered would
    get none
    ([legal-constraints.md](legal-constraints.md)).
  - `MainWindow::apply_gmic_filter(id, values, input_mode)` (the active layer, with prompts
    and status text) and the dialog's OK without a matching preview go through
    `render_gmic_with_progress`: the same
    run on a worker this frame joins while a progress box pumps events (Cancel sets the abort
    flag). `commit_gmic_render` reports failures or pushes one "Filter: <name>" undo step.

## Scripting

`patchy.gmic` (`ScriptGmicObject`, script_api) and `layer.applyGmic` (`ScriptEngineHost::
apply_gmic_to_layer` and `gmic_filter_list`, script_engine.cpp); user docs in the scripting
guide and `patchy.d.ts`, record in [scripting-api-changes.md](scripting-api-changes.md).

- `patchy.gmic.available`, `.version`, `.list()`: every offered filter as `{id, name, folder,
  inputLayers, parameters}`; parameters are the value-carrying ones, in command-line order,
  each `{name, type, default, minimum, maximum, choices}`.
- `layer.applyGmic(id, values, {inputLayers})`: `values` is positional or keyed by G'MIC's
  parameter name. `gmic_argument_from_js` converts by parameter type (a choice takes an
  index or a label, a color `#rrggbb[aa]` or `[r,g,b,a]`, a point `[x,y]`) and a value that
  does not fit throws before anything runs. Returns the number of layers added.
- It runs through `MainWindow::apply_gmic_filter_to_layer`: any open document's layer, no
  prompts or status text, the menu command's gates as errors (text, shape and Smart Object
  layers, locked pixels, Quick Mask), `before_write` calls `prepare_mutation` so the run stays one undo
  entry, a progress box only when the run is attended. While the filter runs the script
  feeds its inactivity watchdog through `keep_running` (a slow filter on a large image is
  not a stuck script), and stopping the script sets the run's abort flag.
- Permanent identifiers: filter ids, the parameter type tokens, and the `inputLayers` tokens
  (`active`, `activeAndBelow`, `activeAndAbove`, `all`, `visible`, `hidden`).

## The allowlist

A filter is offered only when its id is in `src/filters/gmic/patchy_filters_allowlist.txt`.
For 4.0.5 the library declares 440 filters; 394 are allowed and 46 are listed with reasons in
`patchy_filters_excluded.txt`.

- `scripts/dev/gmic/build_allowlist.py` writes both files. A filter is left out when it is
  named (`NAMED`: the legal boundary, plus Custom Code), when it is an About or Testing page,
  when a command it reaches runs an external program or a neural network, or when it does not
  run. Reach analysis can mistake variables for commands; execution decides.
- `patchy_filters_run_failures.txt` (hand-maintained; `scripts/dev/gmic/record-run-failures.ps1
  -Log <test output>` appends what a test run reported and regenerates the lists) holds the
  filters that fail in this build. For 4.0.5: 5 need a window, 4 need data or fonts G'MIC
  would download, 2 are too slow, Export RGB-565 File only writes a file (into the working folder at its default name), and the rest fail for individual reasons given in the file
  (Linify and Tileable Rotation fail on the small test image; cause not yet understood).
- Blur [Motion] and Blur [Depth-of-Field] are named out after the scan
  in [patent-research-gmic.md](patent-research-gmic.md).
- Review scans: `PATCHY_GMIC_SCAN_SIZE=WxH` and `PATCHY_GMIC_SCAN_CHANNELS=3` rerun
  `gmic_allowlisted_filters_run_with_defaults` on another image shape. The 4.0.5 list passes
  at 48x40 RGBA (the default), 131x97 RGB without alpha, and 301x203 RGBA.
- A file or folder parameter is not a reason by itself: Ascii Art only offers to save a text
  file. Such a filter is kept when it runs with the parameter empty; the panel gives the
  parameter a path field and a Browse button, and what the filter then reads or writes is the
  path the user chose.
- The named list is by rule, not a review of each filter. The binding boundary is in
  [legal-constraints.md](legal-constraints.md).
- The catalog is not the execution boundary. `gmic_command_policy.hpp` stubs excluded apply,
  preview, private-helper and algorithm command families at registration, including later
  redefinitions. Nested formula `run()`, aliases and `$$name` body copying get the refusal too.
  Shared non-patch helpers and formulas remain usable; this is no sandbox and no review of user code.

### FFT

CImg has two FFT entry points (`CImg<T>::FFT` per axis and N-dimensional). With
`cimg_use_fftw3` they call FFTW; otherwise the per-axis one is a radix-2 transform that throws
on a size that is not a power of two. FFTW is GPL (or a paid licence), so it stays out, and
Patchy routes both entry points through pocketfft instead (BSD-3-Clause, one header, the FFT
behind NumPy and SciPy, supporting arbitrary sizes in N dimensions).

`_patchy_pocketfft(real, imag, axes_mask, is_inverse)` copies each channel into complex
doubles, transforms along the masked axes (1 = X, 2 = Y, 4 = Z) and copies back, keeping
FFTW's scaling as CImg uses it: forward unscaled, inverse divided by each transformed length.
pocketfft's own thread pool is off (`POCKETFFT_NO_MULTITHREADING`). Results match FFTW except
in the last bits.

## Tests

`tests/core/gmic_tests.cpp` (filter `gmic_`, compiled to an empty group without the option):
the catalog over the whole library with no diagnostics, every declaration form on hand-written
text, the allowlist, Sepia against values pinned from G'MIC-Qt 4.0.4, error and output-name
reporting, that `exec` starts nothing and that `matchpatch` and patch `inpaint` are absent,
abort, the FFT at sizes that are not powers of two (an impulse's flat spectrum and forward and
inverse round trips, whole and per axis), and `gmic_allowlisted_filters_run_with_defaults`, which runs every allowed filter on
a 48x40 image (about 40 s).
`gmic_engine_restricts_excluded_commands_and_aliases` checks every excluded apply/preview
command, helpers, aliases, redefinitions and nested interpreters;
`gmic_engine_custom_formula_cannot_run_excluded_filters` checks Blend formulas and an allowed control.

UI (`tests/ui/gmic_tests.cpp`, filter `ui_gmic`; the panel, tree and render tests also run
without the engine): the panel over all 440 declared filters and every control type; the
dialog's tree, search and Reset; renders into `test-artifacts/gmic/` (five filters, the
notes banner, About G'MIC) for visual QA; Sepia in a selection; the live preview and its
commit (`..._previews_on_the_layer_and_commits`); output layers' names, modes and stacking
(Background included) in one undo (`..._adds_output_layers_in_one_undo_step`); Blend
[Average All] over two layers; the script API and its eight rejections; point handles; the
draft-then-full preview; `ui_gmic_filter_changes_every_chosen_layer` (each layer its own
result, a locked layer read only, a partial layer keeping its size, hidden layers through a
group, memory refusal, script gates); and
`ui_gmic_library_dialog_previews_every_chosen_layer_and_explains_the_result` (multi-layer
preview, previewed added layers below their source, eyes-only Layers panel, Preview off,
Cancel, OK carrying eyes, Ball's sphere on top, the notes, CRT Scanlines, About G'MIC);
`ui_gmic_size_changing_filter_stays_inside_the_selection`; the point-handle test pins the cursor.
`ui_gmic_custom_formula_rejects_excluded_commands_without_editing_layers` pins refusal without
changing layer pixels or count. Core: `gmic_output_layer_names_carry_layer_settings`.

## Known gaps

- Filters that read downloaded data (`input_cached`: color presets, grain, light leaks, fonts)
  need those files shipped or stay excluded; several allowed filters may still depend on them
  for non-default settings.
- The interpreter prints some filter messages to standard output.
- A draft is the whole image reduced, never a 1:1 crop of what the canvas shows, so fine
  detail is only judged once the full run lands. G'MIC's own preview commands (guides,
  split views) are unused; restricted ones are tested for refusal only.
- Of the multi-layer filters only Blend [Average All] has a test of its result; the
  run-everything test and the three-layer sweep check only that the others run.
- Split Colors at its default tolerance makes one layer per color: on a photograph it runs
  for minutes and has to be cancelled. Filters that return animation frames add up to
  about 100 layers.
- The web build leaves the engine out (the option defaults to OFF under Emscripten): only a
  compile of `gmic.cpp` was tried there, and the engine's worker threads and 32 MB stack are
  untested in a browser.
