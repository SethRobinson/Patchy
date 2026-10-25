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

The dialog (layout, parameter panel, preview pipeline, object names) is in [gmic-dialog.md](gmic-dialog.md).

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
