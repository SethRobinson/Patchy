# User-importable theme files

Read this before changing the `.patchytheme` format, theme packages (icons, backdrop, sounds, events, effects, theme scripts), the custom-palette runtime override, or `ThemeManager`'s custom-theme apply path. See [ui-conventions.md](ui-conventions.md) for the built-in Dark/Light system this overlays.

## Format

A `.patchytheme` file is JSON (a theme package's `theme.json` is the same, plus the keys in [Theme packages](#theme-packages)):

```json
{ "format": 1, "name": "Solarized", "base": "dark", "roles": { "window_bg": "#002b36", "accent": "#268bd2" } }
```

- `format` is optional and defaults to 1 (`kThemeFileFormat` in `src/ui/theme_file.hpp`). Any other value is a hard load error: a newer build wrote keys this one cannot read. Bump it only for a change an older build would misread; new roles never need it. Export always writes it.
- `base` is `"dark"` or `"light"` (the same tokens `ThemeManager` already persists); anything else is a hard load error.
- `roles` is optional. Each key is a `ThemePalette` role name from `theme_palette_roles()` (`src/ui/theme_palette.cpp`); each value is `#RRGGBB` or `#RRGGBBAA`. Parsing requires exactly that shape (`parse_theme_color` in `src/ui/theme_file.cpp`) and never calls `QColor::fromString`, which also accepts SVG names and `#RGB`.
- A role omitted from `roles` keeps the `base` scheme's built-in value.
- An unknown role name is a load warning, not an error, so a theme authored against a newer build still loads on an older one.
- Invalid hex is a hard error naming the offending role.
- Alpha round-trips through the file, but most chrome roles resolve to an opaque QSS fill, so it is usually invisible outside `QPainter`-blended overlays.

`load_theme_from_json` seeds the working palette from `theme(base)` (a copy) and overlays parsed roles by walking the same `theme_palette_roles()` table, so a round-tripped export always reproduces its source exactly. `serialize_theme_to_json` is the Export side: it walks the same table and writes every role, `#RRGGBB` normally or `#RRGGBBAA` when `alpha() < 255`.

## Runtime override

`theme_palette.cpp` holds an optional custom palette (`g_custom_palette`) alongside the two built-in ones. `set_active_custom_palette(palette, base)` installs it, keeps `g_active_scheme = base` (so `active_color_scheme()` still reports dark/light for the light-SVG-variant and QSS-cache-index logic), and bumps `theme_generation()`. `clear_active_custom_palette()` reverts and bumps generation only if a custom palette was active. `theme()` returns the custom palette when one is set, otherwise the built-in palette for the active scheme.

Anything that caches a `theme()`-derived value across calls must key that cache on `theme_generation()`, not on the active scheme: a custom theme can carry the same declared `base` as the scheme already active while holding entirely different colors, so a cache keyed on light/dark alone serves stale colors. `main_window_theme.cpp`'s `photoshop_style()` is the reference (`static int cached_generation; static QString cached;`).

## ThemeManager

`ThemeManager::set_custom_theme(id, theme, persist)` applies a loaded theme. It never routes through `apply_resolved_scheme()`: that function early-returns when the resolved scheme already equals the active one, which would swallow a custom-theme switch that shares its `base` with the scheme already showing. It always calls `set_active_custom_palette`, mirrors `base` onto Qt via `QStyleHints::setColorScheme` (skipped under the test override), and always emits `color_scheme_changed(base)`.

`clear_custom_theme(persist)` reverts to the built-in scheme selected by the current preference, with the same always-emit guard in reverse (the built-in scheme being reverted to can share the custom theme's `base`). `set_preference` (the built-in path) calls it automatically whenever a custom theme is active, since a built-in choice and a custom theme are mutually exclusive.

`active_custom_theme_id()` is empty when a built-in scheme is active, otherwise the theme file's name within `user_themes_directory()`. It persists as `preferences/customThemeId`, a key additive to and independent of `preferences/colorScheme`: the built-in preference stays the fallback if the custom theme is ever cleared or its backing file goes missing. `load_saved_preference()` applies the built-in preference first, then tries to load and apply the saved custom theme; a missing or invalid file leaves the built-in preference in place rather than failing startup. While a custom theme is active, an OS light/dark flip under Follow System is ignored rather than silently reverting to a built-in palette.

`user_themes_directory()` (`src/ui/theme_file.cpp`) mirrors `user_fonts_directory()`: `AppDataLocation/themes/` (`%APPDATA%\RTsoft\Patchy\themes` on Windows), empty on wasm (the theme buttons are hidden there too). `PATCHY_THEMES_DIR` overrides it for test isolation, the same pattern other per-user directories use. It holds `*.patchytheme` files and theme packages (below). `themes/example-high-contrast.patchytheme` in the repository is the single-file sample; keep it loading.

## Theme packages

A theme package is a folder in `user_themes_directory()` holding `theme.json`, or that folder zipped as a `.zip` (`theme.json` at the zip root, or inside exactly one top folder, which is what zipping a folder produces). `theme_package.hpp` loads both:

```
My Theme/
  theme.json          the .patchytheme JSON, plus the package keys below
  icons/<stem>.svg    icon overrides: .svg, .png or .bmp; <stem>@2x.png for HiDPI
  images/bg.png       anything "backdrop" names (PNG, JPEG or BMP)
  sounds/<event>.wav  theme sounds, one per event id
  scripts/main.js     optional theme script (Theme scripts below)
```

- Package keys, all optional and parsed by `load_theme_from_json` (so a single `.patchytheme` may carry them; its "backdrop" has nothing to point at and is ignored): `"recolorIcons"` (bool, default true), `"iconScaling"` (`"smooth"` default, or `"nearest"` for pixel art), `"backdrop": {"image": "images/bg.png", "mode": "tile" | "stretch" | "center"}` (mode defaults to tile). A bad value is a warning and the key keeps its default.
- The format stays 1. An older build ignores unknown top-level keys (`load_theme_from_json` reads only `format`, `name`, `base` and `roles`), so a `.patchytheme` with package keys still loads its colors there, and an older build never scans folders or zips at all.
- Ids: a folder's id is its folder name, a zip's its file name (`My Theme.zip`). `user_theme_ids()` lists `*.patchytheme`, `*.zip` and every folder holding `theme.json`, sorted; `load_theme_by_id` picks the loader from what the id names, so `preferences/customThemeId` keeps working for every kind.
- Everything is read into memory once at load (`ThemeAssets`: decoded icons and backdrop, WAV bytes, and every raw file for Export Theme Package), so a folder and a zip behave the same and nothing reads the disk at paint time. A folder reads only `theme.json` and the four subfolders; a zip reads only the entries under them (anything else, a README for example, is skipped).
- Safety: a package holds JSON, SVG, PNG, BMP, JPEG, WAV and theme JavaScript only; the scripts run only in the locked-down theme engine and only once trusted ([Theme scripts](#theme-scripts)), and images decode by their extension, never by content. Every zip entry name must pass `formats::safe_zip_entry_path` and `is_safe_theme_package_path` (no `..`, no absolute or drive path, no backslash), or the whole package is refused; a folder file whose canonical path leaves the folder (a symlink) is refused the same way. The zip goes through `formats::ZipEntryReader` (the OpenRaster and Krita reader), in memory.
- Limits (`ThemePackageLimits`): the zip file 64 MB, one file 16 MB, all files 64 MB, 2048 files; past any of them the package does not load. A bitmap icon over 1024 px a side, or a backdrop over 8192, is skipped with a warning.
- Unreadable or unknown parts (an icon that matches no built-in stem, a sound that names no event, a non-WAV sound, a missing backdrop) are warnings; the theme loads without them.

### Icons

`icons/<stem>.<ext>` replaces the built-in icon `<stem>`: the resource stems in `src/ui/icons/` (`builtin_icon_stems()`), minus the QSS `url()` assets (`stylesheet_referenced_icon_names()`), which no icon engine draws. `overridable_icon_stems()` is that list, and Export Icon Template (`export_icon_template`) writes each of those SVGs as authored into a chosen folder, which is how an author gets the names. Procedural glyph icons (`themed_glyph_icon`, the `simple_icon` fallbacks with no SVG) cannot be replaced.

The override goes through `ThemedIconEngine` in `icon_theme.cpp`: `load_icon_face` checks `active_theme_assets()` first and falls back to the shipped SVG, and the engine reloads its faces when `theme_generation()` moves, so a live theme switch repaints existing QActions with no `setIcon`. An SVG override is recolored like the built-ins (`recolor_icon_svg`, the ten-color vocabulary) when `"recolorIcons"` is true and drawn as authored when false; PNG and BMP are never recolored. A bitmap is fitted to the requested size with its aspect kept, in device pixels; `<stem>@2x.png` is used once the target is larger than the 1x image. `"iconScaling": "nearest"` scales with `Qt::FastTransformation`, so pixel art stays hard-edged. An invalid SVG falls back to the shipped icon.

## Backdrop

`theme_backdrop.hpp`. A package's backdrop image paints the canvas pasteboard (`CanvasWidget::fill_pasteboard`, used for the paint-event fill, the pasteboard between artboards and an artboard drag's vacated rect) and the empty workspace behind the start panel (`StartPanel::paintEvent`). It is anchored to the top-level window, so it stays put while the document pans and zooms and lines up across canvases. `canvas_backdrop` is filled under it (transparent pixels, the margin around a centered image), and a user pasteboard color (`view/canvasBackdropColor`) still wins over the image. The tile pixmap, and stretched copies per target size (at most four), are cached on `theme_generation()`; a repaint only fills its exposed area.

The canvas scroll-bar gutter follows a custom theme (`themed_scroll_gutter_url`, consulted by `themed_icon_url("scroll-dither")`): a tiled backdrop's tile, made opaque over `canvas_scrollbar_track`, or otherwise a two-pixel dither of that track and a tone 14 levels away (lighter on a dark track, darker on a light one), the shape of the authored `scroll-dither.svg`. The PNG is written once to `<temp>/patchy-theme-cache/gutter-<sha1>.png` (QSS `url()` reads only files and resources). A custom theme whose track equals its base scheme's keeps the authored SVG; so do the built-in schemes.

## Sounds

`theme_sounds.hpp`. Patchy has no sounds of its own; a package's `sounds/<event>.wav` (the shorthand) or an `"events"` entry's `"sound"` (which wins) plays for an event. Any other WAV the `"events"` entries or scripts name goes in a subfolder (`sounds/fx/hurt.wav`): a top-level `sounds/` file that names no event is a load warning, which catches typos. Playback is through `play_wav_bytes` (sound_effects.hpp: winmm, NSSound or a Linux player process, no Qt Multimedia). Preferences' "Play theme sounds" (`preferences/playThemeSounds`, default on) turns every theme sound off; `PATCHY_NO_SOUND` silences the player itself (tests, headless runs).

## Events and effects

Every event goes through `fire_theme_event` (theme_script_host.hpp): the sound, the effect, then the theme scripts. The ids are persisted identifiers (file names and `"events"` keys themes ship), append-only (`ThemeSoundEvent`):

| Event id | Fires when |
|---|---|
| `save-done` | a save or saved copy finishes (`save_document_to_path`) |
| `export-done` | an export finishes (Export As, Quick Export, animation, sprite sheet, PDF and folder exports) |
| `long-operation-done` | a progress dialog that stayed up 3 s or more closes |
| `error`, `warning` | an error or warning box opens (`show_critical_message`, `show_warning_message`) |
| `script-alert` | a user script's `alert()` box opens |
| `layer-deleted` | Delete Layer removes layers (`delete_layers`; payload `{count}`) |
| `document-opened` | a file opens (`open_document_path`; payload `{width, height}`) |
| `document-new`, `document-closed` | New Document creates one; a document tab closes |
| `undo`, `redo` | an Undo or Redo step applies |
| `startup` | the main window is up (main.cpp; not for stress or unattended runs) |
| `tool-changed` | the active tool changes (payload `{tool}`, the tool id below) |
| `idle` | every 12 s while the empty start screen shows |

The long-operation watcher is an application event filter timing `QProgressDialog` Show to Hide, installed only while the active theme has a sound, effect or scripts.

theme.json `"events": {"document-opened": {"sound": "sounds/x.wav", "effect": "fireworks"}}`. `"effect"` is a name or an object: `{"name": "fireworks" | "sparkles" | "confetti" | "sprite", "x", "y" (0..1 of the document area, default 0.5, 0.4), "duration" (ms, 100..10000)}`; fireworks take `"bursts"` (1..8), sparkles `"radius"` and `"count"`, confetti `"count"`; a sprite takes `"image"` (a horizontal or grid sheet in the package), `"frames"`, `"frameWidth"`, `"frameHeight"`, `"fps"`, `"loop"` (default true), `"scale"`, `"flip"`, `"toX"`, `"toY"` (it moves there over the duration, drawn feet-down at its position) and `"where": "start-screen"` (plays only while the empty start screen shows and ends when it hides, so it never crosses a document). Unknown events, effects and missing files are load warnings.

`ThemeEffectsOverlay` (theme_effects.hpp) draws them: a child of the document tab widget over the canvas and start panel, `WA_TransparentForMouseEvents`, never touching a document. With nothing playing it is hidden and its 33 ms timer stopped (no repaint, `ui_theme_effect_overlay_costs_nothing_while_idle`); while playing, a frame repaints only the union of the last and current particle bounds, under caps of 8 effects, 1500 particles and 10 s. Randomness is splitmix64; effect colors are generated hues, content rather than chrome, so they sit outside the theme roles like the transparency checkerboard. "Play theme effects" (`preferences/playThemeEffects`, default on) turns them off.

## Theme scripts

`theme_script_host.hpp`. A package's `scripts/main.js` (other files under `scripts/` are read but only main.js runs) executes in `ThemeScriptHost`'s own `QJSEngine`, never the user scripting engine: no `installExtensions`, so the globals are the ECMAScript built-ins plus `patchy.theme`, `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval` and `console` (to the log). There is no `patchy.io`, no `app` or documents, no file paths (every path is package-relative and checked by `is_safe_theme_package_path`), no process, network or user-script globals (`ui_theme_script_engine_is_isolated`). The bridge QObject lives only in the prelude's closure and `patchy` is frozen.

API (types: `scripts/bundled/patchy-theme.d.ts`; changes go in [scripting-api-changes.md](scripting-api-changes.md)):
- `patchy.theme.id`, `.name`; `on(event, fn(payload))` for every event id above; an unknown id throws.
- `playSound(path)` (a package WAV), `playEffect(name, options)` or `playEffect(spec)` (the effect specs above), `showStatus(text, ms)`, `log(...)`.
- `addCommand({id, title, hotkey}, fn)`: at most 32, ids `[A-Za-z0-9_-]`. They fill the theme menu, a top-level menu right of Help titled by theme.json `"menuTitle"` (default the theme name) and shown only while the theme has commands. Each is a HotkeyRegistry command `theme.<percent-encoded theme id>.<command id>` (persisted, category `theme`), listed in the Hotkeys page under the menu's title; `QAction::NoRole` throughout. A theme hotkey loses to a built-in default like a script's `@hotkey`.
- `setToolCursor(tool, image, hotX, hotY)` (an empty image clears; at most 128 px). The tool id is the tool's hotkey id without `tools.` (`crop`, `polygon`, `custom_shape`, `red_eye`, `type`). A theme cursor replaces only a tool's plain pointer (Crop's pointer inside the crop box and with no box, the shape tools' cross, the Type I-beam); handle, rotate, modifier and brush-footprint cursors stay, they are feedback.
- `showSplash(image, ms)` (500..6000 ms, at most every 5 s): a frameless window centered over the main window, never focused, closed by a click.
- `setPanelBackground(panel, image, mode, opacity)`: `panel` is `layers`, `channels`, `paths`, `history`, `properties`, `info`, `palette`, `color`, `tools` or `dialogs`; `mode` tile, stretch or center. The image is drawn at `opacity` (0.05..0.6, default 0.3) into a cached PNG (`theme_cache_png_url`) over the panel's own color, so text stays readable, and appended to `photoshop_style()` (keyed on `theme_panel_background_revision()`).
- The idle sprite: `on('idle', ...)` with `playEffect({name: 'sprite', where: 'start-screen', ...})`.

Declarative equivalents in theme.json, applied with or without scripts: `"cursors": {"crop": {"image", "hotX", "hotY"}}`, `"panelBackgrounds": {"layers": {"image", "mode", "opacity"}}`, `"splash": {"image", "ms"}` (on startup).

Limits: every call into theme code (main.js, an event, a command, a timer) runs under `ScriptWatchdog` with a 250 ms budget (main.js 500 ms); an overrun is interrupted, and the third stops the scripts with a status message (`ui_theme_script_watchdog_stops_a_runaway_loop`). Calls never nest (an event raised during one is queued). Rate limits per second: 4 sounds, 4 effects, 4 status messages, 8 cursor and 4 background changes; at most 16 timers, repeating ones no faster than 30 ms. Errors go to `log()`, `qWarning` and at most one status message per 3 s, never a dialog.

Trust: the first time a theme whose scripts are unknown is applied, the window asks once (`MainWindow::confirm_theme_scripts`, `themeScriptTrustPrompt`); Import asks for a zip with scripts before listing it. The answer is stored under `themeScripts/trust` per theme id with `theme_scripts_digest` (SHA-256 of the scripts), so changed scripts ask again. A denied or disabled theme still applies everything but its scripts. "Run theme scripts" (`preferences/runThemeScripts`, default on) turns them all off. Unattended runs never ask and run no unknown scripts.

## Authoring loop

While a folder package is the active theme (applied or only previewed), `ThemeManager` watches it with a `QFileSystemWatcher` (the folder, `theme.json`, the four subfolders and their files) and reloads it `kPackageReloadDelayMs` (250 ms) after the last change, unpersisted; a reload that fails (half-written JSON) keeps the theme already showing. Each reload re-arms every path, since editors that save by replacing a file drop it from the watch. Switching away drops every watched path, which also releases the Windows directory handles that would block deleting the folder. Reload Themes still rereads every theme. `themes/example-package/` in the repository is the sample package (two colored SVG icons, a pixel-art PNG with `@2x`, a tiled backdrop, a save sound, sparkles on new documents and a one-command scripts/main.js); keep it loading with no warnings (`ui_theme_package_icon_template_and_example_package`).

## Bundled themes

`themes/bundled/*.patchytheme` are compiled into every build through `src/ui/themes.qrc` (resource prefix `/patchy/themes`), so they exist on wasm too. `scripts/dev/generate-bundled-themes.py` writes them from the authored Dark palette in `theme_palette.cpp`: neutral roles go through a per-theme tone ramp, blue-family accents take the theme's accent hue, every other hue is kept. Rerun it after a palette change and commit the result; `ui_bundled_themes_load_and_list_in_preferences` fails on an unknown-role warning, which is how drift shows. `bundled_theme_file_names()` in `theme_file.cpp` is the authored list and order; a new theme is added there, in the qrc, and in the generator. Their ids carry `kBundledThemeIdPrefix` (`bundled:`), `theme_file_path_for_id` maps an id to the resource or user path, and `load_theme_by_id` is the one read path for ThemeManager and the dialog. NOTICE-THIRD-PARTY.md records the palette sources.

## Preferences UI

The color-scheme combo lists the three built-in entries, a separator, the bundled themes, then (when any exist) a separator and one entry per `user_theme_ids()` id (files, zips and folders), each theme carrying a `"custom:" + id` data token. `rescan_custom_themes` (a lambda in `show_preferences`) drops every custom entry and the separator and re-reads the folder; it runs at open, after Delete, and from Reload Themes. `apply_combo_selection` applies whatever the combo shows as a live preview and is shared by the combo's change handler, Reload, and Delete. Import copies a chosen `.patchytheme` or `.zip` (validated by loading it first) into `user_themes_directory()` and adds its entry; Export writes `serialize_theme_to_json(theme(), active_color_scheme(), name)` to a chosen path, opening in `user_themes_directory()` with the shown theme's name suggested (`exportThemeFileDialog`), and a file saved inside that folder is rescanned and selected at once (the "copy a built-in, then edit it" path); bundled entries show as "Name (built-in)" so the copy and its source stay distinguishable; Reload Themes rescans and re-applies the selected entry even when the selection did not move (the authoring loop: edit the JSON, click Reload); Delete Theme... (enabled only on a user-folder entry, never a bundled one; `preferencesDeleteThemeConfirm` confirms) removes the file, or moves a package folder to the trash (`QFile::moveToTrash`, never a recursive delete) after clearing the theme if it is showing, rescans, and selects the first built-in entry; Open Themes Folder creates the folder and opens it in the file manager; Export Theme Package... writes `serialize_theme_package_zip` (theme.json from the live palette plus the shown package's options and files) to a chosen `.zip`; Export Icon Template... writes the overridable SVGs into a chosen folder. The Play theme sounds checkbox (`preferencesPlayThemeSoundsCheck`) commits on OK. All seven buttons and the checkbox are `#ifndef Q_OS_WASM`. Reload and Delete block the combo's signals around the rescan, because removing the current item moves the selection. The dialog's revert-on-cancel guard branches on the `"custom:"` prefix, calling `set_custom_theme`/`clear_custom_theme` instead of `set_preference` for a custom entry.

## Known limitation

The QSS `url()` icon assets other than `scroll-dither` (the checkmark, spin arrows and flyout corner, see [ui-conventions.md](ui-conventions.md)) are selected by base scheme, not recolored per role, and a package cannot replace them. The scroll-bar gutter follows a custom theme ([Backdrop](#backdrop)) but not a user pasteboard color chosen from the canvas right-click menu (`view/canvasBackdropColor`, [tools.md](tools.md)), which recolors only the pasteboard. Everything else, every painted widget and every hand-authored icon glyph, follows the custom palette exactly, because both read `theme()` at paint time.
