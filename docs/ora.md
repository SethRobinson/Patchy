# OpenRaster (.ora)

Layered read and write on every platform (GitHub issue 375): `src/formats/ora_document_io.{hpp,cpp}`,
Qt-free. The registry row is `patchy.formats.ora`; the filter row "OpenRaster Image" is a Save As
format (not Export Flat Image), and `.ora` is in `save_extension_preserves_layers`, so Save stays on
the file.

## Clean-room rule

The format comes only from the published spec ([openraster.org](https://www.openraster.org), the
layer stack and file layout pages; link it, never copy its text) and from files GIMP, Krita and
Patchy write. Krita, GIMP and libkra are GPL: never read, copy or search their source. GIMP and Krita
are run only as tools, through their command lines, to make fixtures and check interchange. Format
and app names only; no app logos or icons ([legal-constraints.md](legal-constraints.md)). Krita's .kra
and GIMP's .xcf open through their own importers ([kra.md](kra.md), [xcf.md](xcf.md)).

## Container

- A zip. Write order: `mimetype` (first, stored, `image/openraster` with no newline, so its text sits
  at byte 38), `stack.xml` (deflated), `data/layer<N>.png` (stored: already deflated), then
  `mergedimage.png` and `Thumbnails/thumbnail.png`. The thumbnail is the merged image area-averaged
  to fit 256 x 256 without upscaling, 8-bit.
- The zip writer is Patchy's own (`ora::detail::zip_archive`): miniz's writer stamps local time, so
  bytes would depend on the time zone. Every entry is dated 1980-01-01 00:00; no extra fields except
  Zip64 records, which appear only where a size or offset needs them.
- Reading goes through miniz's reader (Zip64 included), names matched case-sensitively. A missing
  `mimetype` is tolerated when `stack.xml` exists; any other mimetype, or no `stack.xml`, is "not an
  OpenRaster image". `sniff` (registry) and `sniff_openable_image_format` (open by content) look for
  `mimetypeimage/openraster` at byte 30.

### Limits

Refused before allocation: a canvas or layer side over 300000, an image over 2^30 pixels, more than
2^31 decoded pixels in a file, more than 4 GiB extracted, `stack.xml` over 64 MiB, more than 200000
entries, an entry claiming more than deflate can expand to (1032:1) or a stored entry whose sizes
differ, encrypted entries, groups nested past 64. A layer `src` with `..`, a leading `/`, a
backslash or a colon is never looked up; that layer opens empty with a notice, as do a missing or
undecodable file. Layer files are extracted and size-checked in file order, then decoded in parallel
(one worker per hardware thread); notes and the depth are gathered in file order afterwards. Bad XML, a truncated zip and damaged entries throw plain errors.

## stack.xml model

- `<image w h xres yres>`: size and pixels per inch (default 72). The root `<stack>` carries no
  attributes we use. The first child of a stack is the uppermost, so every sibling list reverses
  against Patchy's bottom-first `layers()`.
- `<layer src name x y opacity visibility composite-op selected>`; `<stack>` takes the same minus
  `src`/`x`/`y`, plus `isolation`. Missing names become "Layer N" / "Group N". Stack `x`/`y` were
  dropped in 0.0.6; older files that set them still offset the stack's layers on read.
- `selected="true"` (GIMP writes it) becomes the active layer; the open path keeps it unless it is a
  group. Patchy writes it on the active layer.
- `<text>` and other elements are skipped with a notice.
- Opacity is written as the shortest classic-locale decimal that reads back as the same float, so
  127/255 round-trips exactly.

### Locks

- `edit-locked` (the spec's Layer Edit Locking Status extension: an xsd:boolean on `<layer>`,
  default false, whose meaning the spec leaves to each app) maps to Lock Image Pixels. Reading:
  `true` or `1` on a layer or a stack adds that lock. Writing: a layer or group with Lock Image
  Pixels, alone or within Lock All, gets `edit-locked="true"`; nothing is written otherwise, so
  files without locks keep their bytes. GIMP 3.2 writes it for its content lock (and for its Lock
  All, on layers and groups), and Krita 5.3 reads it and saves it back on layers and stacks.
- Lock Transparency and Lock Position have no OpenRaster attribute and are not written; Lock All
  therefore reopens as Lock Image Pixels. A lock is not content, so the save warning does not list
  it. GIMP writes nothing for its alpha and position locks either.
- `alpha-preserve` (a proposal; Krita implements it) is not a lock: the layer blends without
  changing the backdrop's alpha. Krita renders it like source-atop and rewrites `svg:src-over` plus
  `alpha-preserve` as `svg:src-atop`, so Patchy opens it as a clipped layer with its own blend
  mode, under the `svg:src-atop` base rule in "Blend modes". Patchy never writes it.
- `scripts/dev/ora/check_real_apps.py` has both apps re-save Patchy's lock file and checks the same
  layers and groups are still edit-locked.

### Isolation and groups

`isolation="auto"` is a pass-through group (the stack's op is ignored, per spec);
`isolate` or no attribute (the spec default) is an isolated group with the stack's op as its blend
mode. Writes: Pass Through as `isolation="auto"` with `svg:src-over`, other groups `isolate`.
What the apps write: Krita 5.3 writes `isolate` for ordinary groups and `auto` for pass-through
groups; GIMP 3.2 writes no `isolation`, so its pass-through groups open isolated (and GIMP's own
merged image, rendered pass-through, then differs from every spec reader).

### Blend modes

| composite-op | Patchy |
|---|---|
| `svg:src-over`, `multiply`, `screen`, `overlay`, `darken`, `lighten`, `color-dodge`, `color-burn`, `hard-light`, `soft-light`, `difference`, `color`, `luminosity`, `hue`, `saturation` | the same-named mode |
| `svg:plus` | Linear Dodge (Add) |
| `svg:src-atop` | clipped onto the layer below when that layer is the bottom of its stack (then it equals Photoshop clipping); otherwise unclipped with a notice |
| `svg:dst-in`, `dst-out`, `dst-atop` | Normal with a notice |
| `krita:soft_light` (Krita's Photoshop soft light), `krita:exclusion`, `linear_burn`, `linear_dodge`, `vivid_light`, `linear light` (with a space), `pin_light`, `hard_mix_photoshop`, `subtract`, `divide`, `dissolve` | the matching mode (names seen in Krita 5.3's own files) |
| `svg:-dp-divide`, `svg:-dp-minus` | Divide, Subtract (Drawpile's names, seen in its test recordings) |
| anything else | Normal, with a notice naming layer and op (MyPaint 2's `mypaint:spectral-wgm` pigment mode included: no equivalent) |

Writes use the spec name where one exists (Linear Dodge as `svg:plus`), Krita's name for Exclusion,
Linear Burn, Vivid, Linear and Pin Light, Hard Mix, Subtract, Divide, Dissolve, Darker Color and
Lighter Color (only Krita reads those; the save warning says so). `krita:darker color` and
`krita:lighter color` (with spaces) were not seen in a Krita file; Krita 5.3 renders Patchy's file
with them as those modes (the interchange check below). GIMP writes its own extra modes as
`svg:src-over`.

## Depth

Any 16-bit layer PNG makes a 16-bit document (8-bit PNGs widen exactly) while deep editing is on;
with it off the file opens at 8 bits with a notice. 8-bit documents write 8-bit PNGs, 16-bit write
16-bit layer PNGs and a 16-bit merged image; 32-bit documents convert to 16 (`convert_document_depth`)
and say so. GIMP 3.2 and Krita 5.3 both write 8-bit PNGs only: Krita re-saves Patchy's 16-bit file
at 8 bits without asking, and GIMP opens any .ora at 8-bit precision.

### Colour profile

Every PNG the writer makes (layers, merged image, thumbnail, at both depths) carries an sRGB ICC
profile in an iCCP chunk right after IHDR (`ora::detail::srgb_icc_profile`). Krita 5.3 asks which
profile to use, once per layer, when it opens a 16-bit PNG with no profile, and its command-line
export waits on that question forever. An sRGB chunk, with or without gAMA and cHRM, does not stop
it; an iCCP profile does. The profile is Patchy's own, built from the published sRGB numbers: ICC
v4 display class, D50 white, the Bradford D65-to-D50 `chad` matrix, the D50-adapted sRGB colorants
and the sRGB curve as parametric curve type 3 (no third-party profile file), with fixed header
fields so the bytes never change. The reader ignores these chunks, so PNGs with or without them
open as sRGB, as before.

## Writing

- The walk follows the SVG writer's plan ([svg.md](svg.md)), through `vector_export::build_units`:
  per sibling list, the last barrier unit (an adjustment layer, a blend mode with no name, or a
  pass-through group holding one or not a plain container) and everything below it merge into one
  layer named "Merged". Above it: a plain pixel layer writes its own pixels at its bounds (off-canvas
  parts kept); a plain group becomes a `<stack>`; anything else (text, shape, smart object, layer
  style, raster or vector mask, fill opacity, Blend If, knockout, channel restrictions, a clipping
  run, a group with style or mask) renders alone at Normal and full opacity through the compositor,
  cropped to its visible pixels, and keeps its own opacity, visibility and blend mode. An empty
  layer writes one transparent pixel, as Krita does.
- A mask carrying the document-alpha marker bakes into alpha without counting as a loss.
- Artboards become plain groups first. Guides, paths, the palette and a non-RGB working mode are not
  stored; saved channels drop (the generic saved-channels prompt covers them).
- `lost_content(document)` is the dry run: the same walk without rendering. The Save path asks
  `oraLostContentMessageBox` (Save / Cancel, default Cancel) when it is not empty, on every
  interactive save, naming up to six items; scripted and unattended saves skip it, as they skip the
  Aseprite Fill Opacity prompt. The written notices go to the status bar.
- The merged image is `flatten_document_rgba8` or `Compositor::flatten_rgba_deep` of the written
  document. Layer PNGs, the merged image and the thumbnail encode in parallel (one worker per
  hardware thread, from 2^21 total pixels) through `ora::detail::encode_png`: RGBA, the per-row filter with the smallest
  sum of absolute signed bytes, miniz level 6, 1 MiB IDAT chunks. Workers only read their buffers
  through const access (the copy-on-write rule in AGENTS.md).
- Determinism: fixed entry order and dates, layer files numbered in document order, classic-locale
  numbers. Two writes of a document, and a write of what was read back, are byte-identical
  (`ora_rewrite_is_byte_identical`); `ora_writer_bytes_are_stable` is the canary.

## Interchange with GIMP and Krita

Checked on Windows with GIMP 3.2.6 (`gimp-console-3.exe`, batch Python) and Krita 5.3.2
(`krita.com --export`), driven only through their command lines.

- Fixtures: `scripts/dev/ora/make_ora_fixtures.py` draws layer PNGs, builds documents in GIMP
  (groups, a pass-through group, offsets, opacity, a hidden layer, a Unicode name, an empty layer,
  one tile per GIMP mode, 8 and 16-bit), saves them as .ora and .psd, and has Krita re-export each
  as .ora and .png. They live in `local-test-fixtures/ora/`. The small checked-in files in
  `test-fixtures/ora/` come from Python's zipfile (`make_committed_fixtures.py`), not Patchy.
- `ora_reads_gimp_and_krita_files_if_available` checks their trees and modes and compares Patchy's
  composite of Krita's files with Krita's own PNG export.
- `scripts/dev/ora/check_real_apps.py` (run through `run-throttled.bat`, after
  `patchy_core_tests ora_writes_inspection_artifacts`) has GIMP and Krita open Patchy's files and
  export PNGs, and compares them with Patchy's merged image per blend mode; it skips an app that is
  not installed. Tolerances and why are below.

### Tolerances

Differences are 8-bit units on premultiplied RGBA, per blend-mode tile (mean, worst pixel), from the
first run on October 24, 2026; the script holds the table.

- Krita, every mode: mean 0.5, worst 3. Krita blends in the same encoded sRGB space with the same
  formulas; measured means 0 to 0.3, worst 2. Exceptions: Darker and Lighter Color allow a worst of
  128 (a pixel whose two colors have nearly equal luminosity picks the other one; means 0.15 and
  0.25), Hard Mix a worst of 255 (pixels exactly on its threshold flip), and Dissolve is reported
  only (each app draws its own random pattern).
- Krita and 16 bits: Patchy's 16-bit file renders within 1 level (mean 0.23), now that its PNGs
  carry the sRGB profile; before that, Krita's export hung on it.
- GIMP, by default: mean 6, worst 130. GIMP 3 composites layers in linear light, so every partly
  covered pixel differs, even at Normal (measured mean 4.5, worst 63 at soft edges). Addition,
  hue, saturation, color and luminosity differ more (linear light plus GIMP's own color models
  for the last four): those modes allow means up to 38 and worst pixels up to 150. Krita's mode names open as Normal in GIMP: reported, not failed.
- The apps' files in Patchy against each app's export: Krita mean 0.00, worst 1 for every fixture;
  GIMP mean 0.45, worst 63 (the same linear-light edges).

## Corpus

`scripts/dev/ora/corpus_check.py CORPUS_DIR` (numpy and Pillow; through `run-throttled.bat`) opens
every .ora headless in Patchy (`--headless --run-script`, reading `doc.importNotes`), compares the
composite with the file's `mergedimage.png` and with Krita's and GIMP's exports, saves and reopens it
(tree and pixels must match exactly), and has both apps open and re-save Patchy's file. It writes
`corpus.csv` and `summary.md` under `build/ora-corpus-report/`. `make_corpus_files.py` adds GIMP and
Krita files (a 4000x3000 canvas with 48 layers on and off the canvas, 9 nested groups alternating
pass-through, odd and non-ASCII names, partial-opacity modes). The corpus itself
(`local-test-fixtures/ora-corpus/`, sources and licences in its README.txt) is not committed.

The October 25, 2026 sweep: 62 files (MyPaint's and Drawpile's test files, about 20 from public
GitHub art and library repositories, two CC0 MyPaint paintings from OpenGameArt, an npm sample, the
GIMP and Krita fixtures). All open, and every save and reopen is identical. Patchy's render matches
the file's merged image or Krita's export on 60; the two others are Krita's quirk below. Found and
fixed: Drawpile's `-dp-` mode names opened as Normal, and large files decoded their layers one at a
time (a 3600x3600 file with 21 layers now opens in 0.9 s instead of 3.2 s; its save takes about 4 s
for 1.1 GB of layer pixels). MyPaint and Pinta have no scripted save, so writing files with them would
need desktop control; their output here comes only from files found online.

Known differences that are the other app's:

- GIMP's merged image follows GIMP, not its own stack.xml: its extra modes saved as `svg:src-over`,
  its pass-through groups saved as isolated, and linear-light blending.
- GIMP renames repeated layer names (" #1", " #2") when it re-saves.
- Krita, when a 16-bit layer PNG has no ICC profile (files saved by an older GIMP carry only gAMA), asks
  for a profile per layer and then renders the samples differently (mean difference 39 to 53);
  GIMP and Patchy read them as sRGB. Its command-line export waits on that question.
- Krita reads Drawpile's `-dp-` names as Normal (Patchy's re-save writes Krita's names instead).
- Krita's import of a GIMP PSD turns group end markers into "</Layer group>" layers.

## Tests

Core (`tests/core/ora_tests.cpp`, filter `ora_`): round trips at 8 and 16 bits with exact pixels and
attributes, byte-identical rewrites, the canary, the spec layout and thumbnail size, the Unicode
path, hostile and damaged input (truncation and mutation sweeps, a size-claim bomb, a huge PNG
header, unsafe paths), group and operator mapping, the merged-image fallback for an empty stack,
Zip64 read-back, what a save bakes, and the app fixtures. UI (`tests/ui/ora_ui_tests.cpp`, filter
`ui_ora_`): opening keeps the layers and selected layer without a flat-file alpha mask; the save
warning names what bakes, Save stays on the .ora, and a plain document saves without a question.
