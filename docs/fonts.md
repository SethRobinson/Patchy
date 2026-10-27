# Bundled fonts and user-added fonts

Font inventory, licensing rules, the wasm family aliases, and the drag-a-font
feature. Read this before adding a bundled font, changing the alias table, or
touching `src/ui/user_fonts.*`. The text tool's font resolution pipeline lives
in [font-resolution.md](font-resolution.md); the wasm platform rules this feature obeys
live in [wasm.md](wasm.md).

## Bundled fonts

Two trees, one staging target:

- `third_party/fonts/` ships on every platform (today: Noto Naskh Arabic
  Regular and Bold, for Photoshop text-layer compatibility).
- `third_party/fonts-web/` ships only in the wasm build, because a browser
  exposes no system fonts to the app. Families: Liberation Sans/Serif/Mono
  (R/B/I/BI, metric-compatible with Arial, Times New Roman, and Courier New),
  Carlito (R/B/I/BI, Calibri-compatible), Noto Sans and Noto Serif (R/B/I/BI),
  Noto Sans JP, Noto Sans SC, and Noto Sans TC (Regular and Bold each, the
  CJK UI and text fallback), Nanum Gothic (Regular and Bold, Korean fallback),
  Montserrat, Oswald, Caveat (Regular and Bold each), and Abril Fatface, Pacifico,
  Lobster (Regular each). About 62 MiB
  total, of which the three CJK families are about 45 MB.

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
- Never bundle an Adobe-BRANDED font: Source Sans, Source Serif, Source Code
  Pro and the rest of the Source family are OFL but carry Adobe's name, and
  [legal-constraints.md](legal-constraints.md) bans Adobe-created assets in
  the repository or a binary. The Noto CJK families (Noto Sans JP/SC/TC) are
  the deliberate exception (Seth, September 2026): they are Source Han Sans
  rebuilds and their copyright string still reads "(c) 2014-2021 Adobe, with
  Reserved Font Name 'Source'", but they ship under Google's Noto name, the
  OFL grant is irrevocable, and no comparable non-Adobe open Han sans exists.
  Without them a browser build has no CJK glyphs at all.
- Every family needs a `NOTICE-THIRD-PARTY.md` entry with its source and
  fetch date. Fetch static instances, not variable fonts.
- The missing-font substitute table (`src/core/font_substitutes.cpp`,
  [font-resolution.md](font-resolution.md#substitute-table)) names fonts and links their project
  pages only; it never bundles, downloads or installs one. Font names there identify the font a
  document asks for (referential use); UI text says "suggested" and "same character widths", never
  that two fonts are the same. Each pair is written from the substitute's own documentation, not
  copied from fontconfig, LibreOffice, Photoshop or any other program's list.

## Application fonts cannot be embedded in a PDF

Qt embeds a font in a PDF only when the font engine's `QFontEngine::FaceId` carries a file
name. On Windows an APPLICATION font (anything registered with
`QFontDatabase::addApplicationFont`) has none, so `QPdfEngine` silently falls back to
drawing every glyph as a filled path: the page still looks right, but the PDF holds no
text at all for that family, and re-importing it (in Patchy, Affinity, anywhere) yields
shape layers instead of text.

The rule that follows: **never register a font file for a family the system already
installs.** Registering an installed family's file turns every text layer in that family
into outlines on editable PDF export. `application_font()` (src/app/main.cpp) takes the
first installed UI-font candidate (Arial, Segoe UI, Calibri) and registers nothing;
`src/ui/ui_font.{hpp,cpp}` owns that decision and
`ui_font_bootstrap_never_registers_installed_families` pins it. Only a Windows install
carrying none of them registers the first candidate's files, where having a UI font at all wins.

Bundled fonts (`load_bundled_fonts`) and user-added fonts are application fonts by
nature (they are not installed), so text in those families still exports to PDF as
outlines. That is a Qt limitation with no workaround short of writing the font
programme into the file ourselves; see [pdf.md](pdf.md).

## Wasm family aliases and UI font

`patchy::ui::user_fonts::kWasmFamilyAliases` (src/ui/user_fonts.hpp) maps
common system families to bundled stand-ins (Arial and Helvetica to Liberation
Sans, Times New Roman to Liberation Serif, Courier New to Liberation Mono,
Calibri to Carlito, Segoe UI/Tahoma/Verdana to Noto Sans, Georgia to Noto
Serif, the common Japanese, Simplified Chinese and Traditional Chinese system
families to Noto Sans JP, SC and TC). The one table is
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

The wasm UI font starts with Noto Sans, followed by
`wasm_cjk_fallback_families`. Korean puts NanumGothic first among the fallbacks;
Chinese and Japanese keep their regional Noto order, with NanumGothic last.
The same list supplies document-text fallback and refreshes when the UI language
changes. Nanum Gothic's static Regular and Bold faces cover every modern Hangul
syllable; the browser-only font tree keeps desktop font selection unchanged.

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
  `<AppDataLocation>/user-fonts` (see "Per-user app-data folder" below) and registers that copy (persist first, then
  register, so the live font's backing file can never vanish) through
  `add_application_font_by_windows_names` (`src/ui/font_face_name_index.hpp`): on macOS a
  font whose Macintosh family name differs from its Windows one registers from an in-memory
  copy without the Macintosh name records, so it is listed under the Windows names Photoshop
  and every other platform use ("Futura BdCn BT", not Apple's "Futura"; the stored file is
  untouched). Why: [font-resolution.md](font-resolution.md). Wasm registers
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
- **Desktop removal never deletes a store file while the app runs.**
  `clear_user_font_store` lists the files in `user-fonts/.remove-at-next-launch`
  and the next launch deletes them (`apply_pending_user_font_removals`, the
  first step of `restore_user_fonts_at_startup`, before anything is registered).
  A FreeType font database (Linux, and the offscreen platform everywhere)
  opens the font file again whenever it builds a new engine, so deleting the
  copy turns the font into another family the next time it is asked for at
  a new size. Windows keeps the font data in memory. Adding a removed font again before the restart takes it off
  the list. A file that cannot be deleted stays listed. Tests that register
  fonts from the store must not delete it afterwards for the same reason.
- The font picker needs no manual refresh: `QFontComboBox` repopulates on
  `QGuiApplication::fontDatabaseChanged`, which `addApplicationFont` emits
  (pinned by `ui_user_fonts_add_persist_and_clear`).

Tests: `tests/core/font_zip_tests.cpp` (extractor) and the `ui_user_fonts_*` and
`ui_font_drop_*` cases in `tests/ui/text_editor_font_picker_tests.cpp`
(registration, persistence, duplicates, invalid fonts, store isolation, drop routing).
`ui_bundled_web_fonts_register_and_create_engines` guards the whole
`third_party/fonts-web` inventory but registers it in a child process
(`--bundled-web-fonts-probe`) so the suite's font database stays clean; see
[testing.md](testing.md).

## One store per process

A process keeps every store file it registered open until it exits (the FreeType
database reopens the file for each new engine). Two processes sharing a store therefore
break each other: on Windows the second cannot delete the first one's files, and on Linux
and macOS it deletes fonts the first is still drawing with.

- `PATCHY_USER_FONTS_DIR=<dir>` replaces the store directory (`user_fonts_directory()`).
- The UI suite sets it for every test process (`tests/ui/main.cpp`):
  `test-artifacts/user-fonts/<pid>` with a `store.lock` held for the life of the process.
  At startup it removes the stores whose lock it can take, which are the ones left by
  processes that have exited. Never point tests at QStandardPaths' test-mode app-data
  folder: it is one directory shared by every checkout and worktree on the machine, so
  concurrent suites break each other. `ui_user_fonts_store_is_private_to_the_process`
  pins the isolation and the Unicode read of the override.
- Known limit, not fixed: two running Patchy instances (a second one needs
  `PATCHY_NO_SINGLE_INSTANCE=1`, `--headless`, or the MCP connector's own app) share the
  real store. On Windows a file the other instance holds simply stays on the removal list.
  On Linux and macOS an instance that starts after Remove Added Fonts deletes files the
  other instance may still load new sizes from.

## Per-user app-data folder

Three per-user folders, all named from the organization name set in `src/app/main.cpp`:

| Folder | Holds | Windows | macOS | Linux |
|---|---|---|---|---|
| AppDataLocation | `user-fonts/`, `scripts/` (`MainWindow::user_scripts_directory()`), `themes/`, `plugins/` | `%APPDATA%\RTsoft\Patchy` | `~/Library/Application Support/RTsoft/Patchy` | `~/.local/share/RTsoft/Patchy` |
| Settings (`app_settings_file_path()`) | `Patchy.ini` and the libraries built beside it: `brushes/`, `brush-presets/`, `gradients/`, `looks/`, `patterns/`, `shapes/`, `styles/` | same as AppDataLocation | same as AppDataLocation | `~/.config/RTsoft/Patchy` |
| AppLocalDataLocation | machine-specific data: `ai-models/` (downloads), `AutoRecover/`, `stress-reports/`, `updates/` | `%LOCALAPPDATA%\RTsoft\Patchy` | same as AppDataLocation | same as AppDataLocation |

Local data stays out of Windows' roaming profile: models are hundreds of MB that download again,
recovery and timings belong to one machine. The settings root is `<root>/RTsoft/Patchy`, where
`<root>` is QSettings' user ini root on Windows and Linux (Flatpak:
`~/.var/app/com.rtsoft.patchy/config`) and `~/Library/Application Support` on macOS, whose Qt
ini root (`~/.config`) is no place for a Mac app's files. `set_app_settings_root()` redirects it
(`PATCHY_SETTINGS_DIR`, the test suites); a bare `QSettings::setPath` no longer does on macOS. The
About dialog (`src/ui/splash_dialog.cpp`) lists one "Settings and data folder" row when settings
and data share a folder and separate rows otherwise (Linux, a `PATCHY_SETTINGS_DIR` run).

Startup migrations in `src/ui/app_data_migration.{hpp,cpp}` merge each legacy folder into the
current one without overwriting (identical leftovers are deleted, differing ones stay for the
user), then remove the emptied legacy folder:

- `migrate_legacy_settings()`: through 1.19 `app_settings()` named the organization "Patchy", so
  settings and presets lived in `<Qt ini root>/Patchy` (`%APPDATA%\Patchy`, `~/.config/Patchy`).
  It runs right after the `PATCHY_SETTINGS_DIR` redirect, before the first `app_settings()` read
  (and in connector mode before the shared brush/recent file paths are captured). It never
  removes the ini root.
- `migrate_legacy_app_data()`: releases through 0.98 used the organization name
  "Seth A. Robinson" for AppDataLocation. It runs before the font restore.
- `migrate_local_app_data()`: through 1.19 `ai-models/` and `AutoRecover/` were in
  AppDataLocation and `stress-reports/` beside Patchy.ini. It runs right after the previous one
  and moves only those folders, never their parents; where the folders coincide it does nothing.

Treat the organization name, the settings path and the local-data split as persisted
identifiers; changing any again means another migration step, not an edit to these. Tests:
`ui_app_data_migration_merges_legacy_folder`, `ui_app_settings_migration_moves_legacy_settings_folder`
and `ui_app_data_migration_moves_machine_data_to_local` in
`tests/ui/app_shell_tests_language_chrome.cpp`.
