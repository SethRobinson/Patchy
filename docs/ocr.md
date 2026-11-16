# Text recognition (OCR)

Reading text out of pixels. This doc owns the engine library (`src/ocr`, target `patchy_ocr`) and the rules every entry point follows. Status: the library, its fake engine and its tests are in; the Windows engine, the downloadable model engine and the commands that use them are being added on top of it, each with its own section here as it lands.

## Rules that bind every entry point

The binding entry is "Text recognition" in [legal-constraints-scanning.md](legal-constraints-scanning.md#text-recognition) (claim comparisons: [patent-research-text-recognition.md](patent-research-text-recognition.md)). The rules every caller meets:

- **Recognition runs only on an explicit user action** (a command, a ticked option, a script call). Nothing recognizes in the background, and nothing downloads unasked.
- **The engine and the language are the user's picks**, read from settings (`ocr/engine`, `ocr/language`), never derived from the image. No code tries several recognizers and keeps the best.
- **No typeface identification.** Nothing names or matches a font from pixels, and recognition never uses a detected font.
- **Results never leave their feature.** Nothing from recognition feeds Divide Scanned Photos, book page detection, cutting, straightening or tone choices.
- **Engines never run on the GUI thread** (below).

## Library (`src/ocr`, Qt-free)

`ocr/ocr_engine.hpp` is the whole public surface:

- `ImageView` (pixels, size, stride, `PixelLayout` of Gray8, Rgb8, Rgba8 or Bgra8; straight alpha, read over white).
- `Result` = `lines` plus an optional page text angle. A `Line` has UTF-8 `text`, an axis-aligned `box`, a `quad` (its corners in the text's own frame: top left, top right, bottom right, bottom left), `angle_degrees` (clockwise, y down), `vertical` (characters run top to bottom) and `words` (text, box, quad). Everything is in the pixels of the image handed in. `Result::text()` joins lines with `\n`.
- `Engine`: `id()` (a permanent token persisted in settings and used by scripts: `windows`, `ppocr`, `fake`), `display_name()` (English fallback; the UI translates the ids it knows), `languages()` (asked fresh on every call: a `Language` is a tag, BCP-47 for an operating system engine, plus an English name), `default_language()` (from the user's system or app language, never from an image), `unavailable()` (`None`, `NoLanguages`, `NotDownloaded`, `Failed`) with `unavailable_reason()` (an English diagnostic), and `open(language_tag)`.
- `Recognizer::recognize(image, cancel)`: one engine opened for one language, held for a batch of images. One caller thread at a time, **never the GUI thread**: an engine may block for seconds, and the Windows engine waits on an asynchronous operation, which a single-threaded apartment does not allow. It throws `RecognitionCancelled` when its `Cancel` was requested and `std::runtime_error` (an English diagnostic) when the engine fails. An image with no text is an empty result, never an error.
- `engines()` lists every engine this build and machine has, available or not: the operating system's (`detail::platform_engines`, `ocr_engine_stub.cpp` where there is none), then whatever `add_engine_provider` registered (how an engine outside this library joins). `find_engine(id)` looks one up. Both are thread-safe.

### Oversized images

`ocr_bands.{hpp,cpp}`, shared by every engine with a size limit M (`recognize_in_bands`, `plan_bands`). The rule is fixed geometry:

1. An image wider than M is scaled so its width is M (height in proportion, rounded to nearest).
2. If the scaled height still exceeds M it is read as full-width bands of M rows that overlap by V = max(16, M / 8) rows (at most M / 2); every band but the last starts at a multiple of M - V and the last ends at the bottom.
3. A line read in two neighbouring bands is kept once: by the band on whose side of the overlap's midpoint the line's vertical centre falls.

Lines come back in the original image's pixels, in band order then engine order. No text is compared and no pixel is looked at to decide anything. A Letter page at 300 ppi against a limit of 2600 is two unscaled bands; at 600 ppi it is scaled to 2600 wide first. `ocr_image.{hpp,cpp}` makes each band straight from its source rows (`scaled_rows_bgra`: opaque BGRA over white, each scaled pixel the rounded mean of whole source pixels, integer math only), so a page is never copied whole at the scaled size.

### Blocks

`ocr_layout.{hpp,cpp}`: `group_lines_into_blocks` turns lines into blocks, what becomes one multi-line text layer; `lines_as_blocks` is the one-layer-per-line choice. Two lines of the same orientation join when their thicknesses are within 30% of each other, the gap between them across the lines is at most 0.9 of the thinner one (overlapping by no more than 0.3 of it), and along the lines they overlap by at least half the shorter one. Lines rotated more than 2 degrees stay alone. A `Block` carries its lines in reading order (top to bottom; vertical text right to left), its box, `alignment` (the edge its lines share most closely: `Start`, `Center` or `End` along the reading direction), `line_size` (median thickness) and `line_pitch` (median centre-to-centre distance). Blocks come back horizontal ones first, top to bottom then left to right, then vertical ones right to left.

**The only inputs are the spacing, overlap and thickness of two lines at a time.** No line is classified as starting or continuing a paragraph, and nothing about the page (type, margins, indentation, layout) enters. Two paragraphs with no gap between them are one block: that is this grouping's stated limit, not a bug to fix with indentation tests.

## Testing

- **In-process**: `make_fake_engine(FakeEngineConfig)` (`ocr_fake_engine.hpp`) builds a scripted engine: lines with boxes and text, languages (none makes it `NoLanguages`), a size limit (it goes through the real banding), a per-band delay for cancel tests, a page angle, an unavailable state, a failure message. It records each call (language, image size, the tiles it was handed). `set_engines_for_testing(factory)` replaces the whole list; an empty list is "no engine on this system", an empty function restores the real one. Tests install it under a guard that restores on exit.
- **A real build without an engine** (`--run-script`, visual QA): `PATCHY_FAKE_OCR="x,y,w,h=text;v:x,y,w,h=text"` makes the list the fake alone (boxes in image pixels, `v:` for a vertical line, `\uXXXX` escapes; an empty value is an engine that finds nothing), with `PATCHY_FAKE_OCR_LANGUAGES` (`tag=Name;...` or `none`), `PATCHY_FAKE_OCR_MAX_DIMENSION`, `PATCHY_FAKE_OCR_DELAY_MS` and `PATCHY_FAKE_OCR_ANGLE`. The spec is inline, so no file entry point exists.
- Core tests (`tests/core/ocr_tests.cpp`, prefix `ocr_`): the registry and its hooks, the pinned band plan, lines in an overlap reported once, a wide image scaled and mapped back, the area average, alpha over white, cancel before and during a read, the environment spec, and block grouping (spacing, alignment, vertical order).
