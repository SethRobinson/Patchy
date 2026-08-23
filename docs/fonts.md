# Bundled fonts and user-added fonts

Font inventory, licensing rules, the wasm family aliases, and the drag-a-font
feature. Read this before adding a bundled font, changing the alias table, or
touching `src/ui/user_fonts.*`. The text tool's font resolution pipeline lives
in [text-tool.md](text-tool.md); the wasm platform rules this feature obeys
live in [wasm.md](wasm.md).

## Bundled fonts

Two trees, one staging target:

- `third_party/fonts/` ships on every platform (today: Noto Naskh Arabic
  Regular and Bold, for Photoshop text-layer compatibility).
- `third_party/fonts-web/` ships only in the wasm build, because a browser
  exposes no system fonts to the app. Families: Liberation Sans/Serif/Mono
  (R/B/I/BI, metric-compatible with Arial, Times New Roman, and Courier New),
  Carlito (R/B/I/BI, Calibri-compatible), Noto Sans and Noto Serif (R/B/I/BI),
  Noto Sans JP (Regular and Bold, the Japanese UI and CJK text fallback),
  Montserrat, Oswald, Caveat (Regular and Bold each), and Abril Fatface,
  Pacifico, Lobster (Regular each). About 23 MB total.

The `patchy_bundled_fonts` CMake target cleans and rebuilds
`${CMAKE_BINARY_DIR}/fonts` from `third_party/fonts`, and under EMSCRIPTEN
additionally merges `third_party/fonts-web` into the same tree. The wasm app
preloads that tree at `/fonts` (packed into `patchy.data`), and the app target
carries a `LINK_DEPENDS` on the fonts stamp so a fonts-only change repacks
`patchy.data` instead of shipping stale bytes. `load_bundled_fonts`
(src/app/main.cpp) registers every `*.ttf`/`*.otf`/`*.ttc` under the staged
tree recursively at startup on all platforms.

Licensing rules for bundled fonts (binding):

- Open licenses only; every current family is SIL OFL 1.1. Each family
  directory keeps its own `OFL.txt`, which ships with the package.
- Never bundle an Adobe-created font (Source Sans and friends are OFL but
  Adobe-created; [legal-constraints.md](legal-constraints.md) bans
  Adobe-created assets in the repository or a binary).
- Every family needs a `NOTICE-THIRD-PARTY.md` entry with its source and
  fetch date. Fetch static instances, not variable fonts.

## Application fonts cannot be embedded in a PDF

Qt embeds a font in a PDF only when the font engine's `QFontEngine::FaceId` carries a file
name. On Windows an APPLICATION font (anything registered with
`QFontDatabase::addApplicationFont`) has none, so `QPdfEngine` silently falls back to
drawing every glyph as a filled path: the page still looks right, but the PDF holds no
text at all for that family, and re-importing it (in Patchy, Affinity, anywhere) yields
shape layers instead of text.

The rule that follows: **never register a font file for a family the system already
installs.** `application_font()` (src/app/main.cpp) used to register
`C:/Windows/Fonts/{arial,segoeui,calibri}*.ttf` unconditionally just to pick a UI font,
which quietly turned every Arial / Segoe UI / Calibri text layer into outlines on editable
PDF export while unregistered families (Consolas, Tahoma, ...) exported as real text.
It now takes the first installed candidate family and registers nothing;
`src/ui/ui_font.{hpp,cpp}` owns that decision and
`ui_font_bootstrap_never_registers_installed_families` pins it. Only a Windows install
carrying none of the three registers files, where having a UI font at all wins.

Bundled fonts (`load_bundled_fonts`) and user-added fonts are application fonts by
nature - they are not installed - so text in those families still exports to PDF as
outlines. That is a Qt limitation with no workaround short of writing the font
programme into the file ourselves; see [pdf.md](pdf.md).

## Wasm family aliases and UI font

`patchy::ui::user_fonts::kWasmFamilyAliases` (src/ui/user_fonts.hpp) maps
common system families to bundled stand-ins (Arial and Helvetica to Liberation
Sans, Times New Roman to Liberation Serif, Courier New to Liberation Mono,
Calibri to Carlito, Segoe UI/Tahoma/Verdana to Noto Sans, Georgia to Noto
Serif, the common Japanese system families to Noto Sans JP). The one table is
consumed twice, and the two consumers must stay in sync by construction:

- `QFont::insertSubstitution` at startup (src/app/main.cpp), the
  rendering-level fallback for QFont paths that bypass the text pipeline.
- `available_text_family_match` (src/ui/main_window.cpp), because substitutions
  never appear in `QFontDatabase::families()` and the text tool's matching,
  the missing-font prompt, and the picker canonicalization all consult the
  database. The alias only applies when the bundled target family actually
  exists in the database.

Accepted side effect: editing a text layer whose PSD says "Arial" on wasm
commits the alias family (the same outcome as accepting the desktop
missing-font substitution prompt, but silent and correctly rendered).

The wasm UI font is `{"Noto Sans", "Noto Sans JP"}` (per-glyph fallback keeps
the Japanese UI from rendering tofu), and
`render_text_families_for_display_family` appends "Noto Sans JP" on wasm so
Japanese document text renders through it under any Latin-only face.

## User-added fonts (drag and drop)

Dropping loose `.ttf`/`.otf`/`.ttc` files or a `.zip` containing them onto the
window registers them for immediate use and persists them:

- Shared logic: `src/ui/user_fonts.{hpp,cpp}` (`add_user_fonts`,
  `restore_user_fonts_at_startup`, `clear_user_font_store`). Desktop drop
  routing is in `MainWindow::open_dropped_files`; the wasm route is
  `MainWindow::handle_web_file_drop`. Status feedback funnels through
  `MainWindow::show_user_font_drop_result`.
- Zip extraction: `src/formats/font_zip.cpp` (Qt-free, miniz archive reader).
  Deterministic archive order, skips directories, `__MACOSX/`, dotfiles, and
  non-font extensions, sanitizes to basenames, and enforces allocation caps
  (64 MB/file, 256 MB and 256 entries per archive) as the zip-bomb defense.
- Persistence stores: desktop copies each font into
  `<AppDataLocation>/user-fonts` and registers that copy (persist first, then
  register, so the live font's backing file can never vanish); wasm registers
  a MEMFS copy and fire-and-forgets an IndexedDB put (DB `PatchyUserFonts`,
  store `fonts` keyed by file name, so a same-named font overwrites across
  sessions). Bytes that fail `addApplicationFont` are never persisted.
- Dedupe: a session-wide SHA-256 content-hash set (seeded by the startup
  restore) makes re-drops count as duplicates; on-disk name collisions get a
  numeric suffix instead of overwriting a possibly-live file.
- The wasm restore and store glue (`src/ui/user_fonts_wasm.cpp`) follows the
  pinned poll pattern: page-side JS only writes plain state or the database,
  and a QTimer drains it (see the platform findings in [wasm.md](wasm.md)).
  The IndexedDB put copies bytes out of the wasm heap first; on the
  multithreaded build the heap is a SharedArrayBuffer, whose views IndexedDB
  refuses to store.
- Removal: the "Remove Added Fonts..." button in Preferences empties the
  store. Registered fonts stay usable until restart (desktop) or page reload
  (wasm) because `QFontDatabase::removeApplicationFont` is never called (it
  can crash live font users; see [testing.md](testing.md)).
- The font picker needs no manual refresh: `QFontComboBox` repopulates on
  `QGuiApplication::fontDatabaseChanged`, which `addApplicationFont` emits
  (pinned by `ui_user_fonts_add_persist_and_clear`).

Tests: `tests/core/font_zip_tests.cpp` (extractor) and the two `ui_user_fonts`
/ `ui_font_drop` cases in `tests/ui/text_editor_font_picker_tests.cpp`
(registration, persistence, duplicates, invalid fonts, drop routing).
`ui_bundled_web_fonts_register_and_create_engines` guards the whole
`third_party/fonts-web` inventory but registers it in a child process
(`--bundled-web-fonts-probe`) so the suite's font database stays clean; see
[testing.md](testing.md).
