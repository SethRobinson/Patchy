# User-importable theme files

Read this before changing the `.patchytheme` format, theme packages (icons, backdrop, sounds, events, effects, theme scripts), the custom-palette runtime override, or `ThemeManager`'s custom-theme apply path. See [ui-conventions.md](ui-conventions.md) for the built-in Dark/Light system this overlays.

Related documents:
- [theme-files-scripts.md](theme-files-scripts.md): a package's sounds and the looping sound channel, the theme events with their sounds and effects, and theme scripts (engine, API, limits, per-theme trust).
- [theme-files-preferences.md](theme-files-preferences.md): Preferences > Colors and Themes (theme list, live preview, details panel, the theme file buttons, the sound and effect switches).

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
- Optional theme information (`ThemeInfo`), shown in Preferences: `"version"` (the theme's own version, free text up to 32 characters), `"author"` (up to 100), `"description"` (up to 2000 characters; line breaks stay) and `"minPatchyVersion"` (dotted digits such as `"1.20"`, compared with `QVersionNumber` against `PATCHY_VERSION` by `theme_needs_newer_patchy`). A non-string value, or a `minPatchyVersion` that is not dotted digits, is a load warning and the key stays empty. A theme for a newer Patchy still loads: the check only warns ([Preferences UI](theme-files-preferences.md#preferences-ui)). Export Theme Package carries them; Export Theme (a fresh copy) does not. The key names are permanent.

`load_theme_from_json` seeds the working palette from `theme(base)` (a copy) and overlays parsed roles by walking the same `theme_palette_roles()` table, so a round-tripped export always reproduces its source exactly. `serialize_theme_to_json` is the Export side: it walks the same table and writes every role, `#RRGGBB` normally or `#RRGGBBAA` when `alpha() < 255`.

## Runtime override

`theme_palette.cpp` holds an optional custom palette (`g_custom_palette`) alongside the two built-in ones. `set_active_custom_palette(palette, base)` installs it, keeps `g_active_scheme = base` (so `active_color_scheme()` still reports dark/light for the light-SVG-variant and QSS-cache-index logic), and bumps `theme_generation()`. `clear_active_custom_palette()` reverts and bumps generation only if a custom palette was active. `theme()` returns the custom palette when one is set, otherwise the built-in palette for the active scheme.

Anything that caches a `theme()`-derived value across calls must key that cache on `theme_generation()`, not on the active scheme: a custom theme can carry the same declared `base` as the scheme already active while holding entirely different colors, so a cache keyed on light/dark alone serves stale colors. `main_window_theme.cpp`'s `photoshop_style()` is the reference (`static int cached_generation; static QString cached;`).

## ThemeManager

`ThemeManager::set_custom_theme(id, theme, persist)` applies a loaded theme. It never routes through `apply_resolved_scheme()`: that function early-returns when the resolved scheme already equals the active one, which would swallow a custom-theme switch that shares its `base` with the scheme already showing. It always calls `set_active_custom_palette`, mirrors `base` onto Qt via `QStyleHints::setColorScheme` (skipped under the test override), and always emits `color_scheme_changed(base)`.

`clear_custom_theme(persist)` reverts to the built-in scheme selected by the current preference, with the same always-emit guard in reverse (the built-in scheme being reverted to can share the custom theme's `base`). `set_preference` (the built-in path) calls it automatically whenever a custom theme is active, since a built-in choice and a custom theme are mutually exclusive.

`active_custom_theme_id()` is empty when a built-in scheme is active, otherwise the theme file's name within `user_themes_directory()`. It persists as `preferences/customThemeId`, a key additive to and independent of `preferences/colorScheme`: the built-in preference stays the fallback if the custom theme is ever cleared or its backing file goes missing. `load_saved_preference()` applies the built-in preference first, then tries to load and apply the saved custom theme; a missing or invalid file leaves the built-in preference in place rather than failing startup. While a custom theme is active, an OS light/dark flip under Follow System is ignored rather than silently reverting to a built-in palette.

Every apply runs inside a `ThemeApplyScope` (`theme_apply_trace.hpp`): a wait cursor for its length, and with `PATCHY_THEME_TRACE=1` its phases and restyle event counts on stderr. The window sheet's repolish is most of the cost; `apply_color_scheme` restyles a window parented into MainWindow (Preferences) only through that sheet, never again as a top-level (issue 490).

`user_themes_directory()` (`src/ui/theme_file.cpp`) mirrors `user_fonts_directory()`: `AppDataLocation/themes/` (`%APPDATA%\RTsoft\Patchy\themes` on Windows), empty on wasm (the theme buttons are hidden there too). `PATCHY_THEMES_DIR` overrides it for test isolation, the same pattern other per-user directories use. It holds `*.patchytheme` files and theme packages (below). `themes/example-high-contrast.patchytheme` in the repository is the single-file sample; keep it loading.

## Theme packages

A theme package is a folder in `user_themes_directory()` holding `theme.json`, or that folder zipped as a `.zip` (`theme.json` at the zip root, or inside exactly one top folder, which is what zipping a folder produces). `theme_package.hpp` loads both:

```
My Theme/
  theme.json          the .patchytheme JSON, plus the package keys below
  icons/<stem>.svg    icon overrides: .svg, .png or .bmp; <stem>@2x.png for HiDPI
  images/bg.png       anything "backdrop" names (PNG, JPEG or BMP)
  sounds/<event>.wav  theme sounds, one per event id
  scripts/main.js     optional theme script (theme-files-scripts.md)
```

- Package keys, all optional and parsed by `load_theme_from_json` (so a single `.patchytheme` may carry them; its "backdrop" has nothing to point at and is ignored): `"recolorIcons"` (bool, default true), `"iconScaling"` (`"smooth"` default, or `"nearest"` for pixel art), `"backdrop": {"image": "images/bg.png", "mode": "tile" | "stretch" | "center"}` (mode defaults to tile). A bad value is a warning and the key keeps its default.
- The format stays 1. An older build ignores unknown top-level keys (`load_theme_from_json` reads only `format`, `name`, `base` and `roles`), so a `.patchytheme` with package keys still loads its colors there, and an older build never scans folders or zips at all.
- Ids: a folder's id is its folder name, a zip's its file name (`My Theme.zip`). `user_theme_ids()` lists `*.patchytheme`, `*.zip` and every folder holding `theme.json`, sorted; `load_theme_by_id` picks the loader from what the id names, so `preferences/customThemeId` keeps working for every kind.
- Everything is read into memory once at load (`ThemeAssets`: decoded icons and backdrop, WAV bytes, and every raw file for Export Theme Package), so a folder and a zip behave the same and nothing reads the disk at paint time. A folder reads only `theme.json` and the four subfolders; a zip reads only the entries under them (anything else, a README for example, is skipped).
- Safety: a package holds JSON, SVG, PNG, BMP, JPEG, WAV and theme JavaScript only; the scripts run only in the locked-down theme engine and only once trusted ([Theme scripts](theme-files-scripts.md#theme-scripts)), and images decode by their extension, never by content. An SVG icon must pass `is_safe_theme_svg` or it is a load warning and the shipped icon stays: QtSvg loads an `<image>` href as a file path, and a network path sends the Windows login hash to that server, so a theme SVG may hold no href but `#id`, no `<image>` but embedded PNG or JPEG data, no `url()` but `url(#id)`, no `@import`, DTD or processing instruction (the shipped icons all pass). A WAV must pass `is_well_formed_wav` (sound_effects.hpp), which every `play_wav_bytes` also checks: winmm reads as many bytes as the RIFF size claims, so every chunk and that size must lie inside the bytes. Every zip entry name must pass `formats::safe_zip_entry_path` and `is_safe_theme_package_path` (no `..`, no absolute or drive path, no backslash), or the whole package is refused; a folder file whose canonical path leaves the folder (a symlink) is refused the same way. The zip goes through `formats::ZipEntryReader` (the OpenRaster and Krita reader), in memory.
- Limits (`ThemePackageLimits`): the zip file 64 MB, one file 16 MB, all files 64 MB, 2048 files; past any of them the package does not load. A bitmap icon over 1024 px a side, or a backdrop over 8192, is skipped with a warning.
- Unreadable or unknown parts (an icon that matches no built-in stem, a sound that names no event, a non-WAV sound, a missing backdrop) are warnings; the theme loads without them.

### Icons

`icons/<stem>.<ext>` replaces the built-in icon `<stem>`: the resource stems in `src/ui/icons/` (`builtin_icon_stems()`), minus the QSS `url()` assets (`stylesheet_referenced_icon_names()`), which no icon engine draws. `overridable_icon_stems()` is that list, and Export Icon Template (`export_icon_template`) writes each of those SVGs as authored into a chosen folder, which is how an author gets the names. Procedural glyph icons (`themed_glyph_icon`, the `simple_icon` fallbacks with no SVG) cannot be replaced.

The override goes through `ThemedIconEngine` in `icon_theme.cpp`: `load_icon_face` checks `active_theme_assets()` first and falls back to the shipped SVG, and the engine reloads its faces when `theme_generation()` moves, so a live theme switch repaints existing QActions with no `setIcon`. An SVG override is recolored like the built-ins (`recolor_icon_svg`, the ten-color vocabulary) when `"recolorIcons"` is true and drawn as authored when false; PNG and BMP are never recolored. A bitmap is fitted to the requested size with its aspect kept, in device pixels; `<stem>@2x.png` is used once the target is larger than the 1x image. `"iconScaling": "nearest"` scales with `Qt::FastTransformation`, so pixel art stays hard-edged. An invalid SVG falls back to the shipped icon.

## Backdrop

`theme_backdrop.hpp`. A package's backdrop image paints the canvas pasteboard (`CanvasWidget::fill_pasteboard`, used for the paint-event fill, the pasteboard between artboards and an artboard drag's vacated rect) and the empty workspace behind the start panel (`StartPanel::paintEvent`). It is anchored to the top-level window, so it stays put while the document pans and zooms and lines up across canvases. `canvas_backdrop` is filled under it (transparent pixels, the margin around a centered image), and a user pasteboard color (`view/canvasBackdropColor`) still wins over the image. The tile pixmap, and stretched copies per target size (at most four), are cached on `theme_generation()`; a repaint only fills its exposed area.

The canvas scroll-bar gutter follows a custom theme (`themed_scroll_gutter_url`, consulted by `themed_icon_url("scroll-dither")`): a tiled backdrop's tile, made opaque over `canvas_scrollbar_track`, or otherwise a two-pixel dither of that track and a tone 14 levels away (lighter on a dark track, darker on a light one), the shape of the authored `scroll-dither.svg`. The PNG is written once to `<temp>/patchy-theme-cache/gutter-<sha1>.png` (QSS `url()` reads only files and resources). `theme_cache_png_url` writes that folder for every theme image: at most `kThemeCacheSessionBytes` (64 MB) per process, a reused file gets a fresh modification time, and a process's first write prunes PNGs older than a week (`prune_theme_cache`, by the literal folder name). A custom theme whose track equals its base scheme's keeps the authored SVG; so do the built-in schemes.

## Authoring loop

While a folder package is the active theme (applied or only previewed), `ThemeManager` watches it with a `QFileSystemWatcher` (the folder, `theme.json`, the four subfolders and their files) and reloads it `kPackageReloadDelayMs` (250 ms) after the last change, unpersisted; a reload that fails (half-written JSON) keeps the theme already showing. Each reload re-arms every path, since editors that save by replacing a file drop it from the watch. Switching away drops every watched path, which also releases the Windows directory handles that would block deleting the folder. Reload Themes still rereads every theme. `themes/example-package/` in the repository is the sample package (two colored SVG icons, a pixel-art PNG with `@2x`, a tiled backdrop, a save sound, sparkles on new documents and a one-command scripts/main.js); keep it loading with no warnings (`ui_theme_package_icon_template_and_example_package`).

## Bundled themes

Themes compiled into every build (`bundled:` ids): the generated `.patchytheme` palettes and theme packages such as Dink Smallwood, their resources, script trust and the `PATCHY_THEME_SOURCE_DIR` editing override are in [bundled-themes.md](bundled-themes.md).

## Known limitation

The QSS `url()` icon assets other than `scroll-dither` (the checkmark, spin arrows and flyout corner, see [ui-conventions.md](ui-conventions.md)) are selected by base scheme, not recolored per role, and a package cannot replace them. The scroll-bar gutter follows a custom theme ([Backdrop](#backdrop)) but not a user pasteboard color chosen from the canvas right-click menu (`view/canvasBackdropColor`, [tools.md](tools.md)), which recolors only the pasteboard. Everything else, every painted widget and every hand-authored icon glyph, follows the custom palette exactly, because both read `theme()` at paint time.
