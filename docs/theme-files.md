# User-importable theme files

Read this before changing the `.patchytheme` format, the custom-palette runtime override, or `ThemeManager`'s custom-theme apply path. See [ui-conventions.md](ui-conventions.md) for the built-in Dark/Light system this overlays.

## Format

A `.patchytheme` file is JSON:

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

`user_themes_directory()` (`src/ui/theme_file.cpp`) mirrors `user_fonts_directory()`: `AppDataLocation/themes/` (`%APPDATA%\RTsoft\Patchy\themes` on Windows), empty on wasm (the theme buttons are hidden there too). `PATCHY_THEMES_DIR` overrides it for test isolation, the same pattern other per-user directories use. Only `*.patchytheme` files are scanned. `themes/example-high-contrast.patchytheme` in the repository is the sample users start from; keep it loading ([features.md](features.md) points users at it).

## Bundled themes

`themes/bundled/*.patchytheme` are compiled into every build through `src/ui/themes.qrc` (resource prefix `/patchy/themes`), so they exist on wasm too. `scripts/dev/generate-bundled-themes.py` writes them from the authored Dark palette in `theme_palette.cpp`: neutral roles go through a per-theme tone ramp, blue-family accents take the theme's accent hue, every other hue is kept. Rerun it after a palette change and commit the result; `ui_bundled_themes_load_and_list_in_preferences` fails on an unknown-role warning, which is how drift shows. `bundled_theme_file_names()` in `theme_file.cpp` is the authored list and order; a new theme is added there, in the qrc, and in the generator. Their ids carry `kBundledThemeIdPrefix` (`bundled:`), `theme_file_path_for_id` maps an id to the resource or user path, and `load_theme_by_id` is the one read path for ThemeManager and the dialog. NOTICE-THIRD-PARTY.md records the palette sources.

## Preferences UI

The color-scheme combo lists the three built-in entries, a separator, the bundled themes, then (when any exist) a separator and one entry per file in `user_themes_directory()`, each theme carrying a `"custom:" + id` data token. `rescan_custom_themes` (a lambda in `show_preferences`) drops every custom entry and the separator and re-reads the folder; it runs at open, after Delete, and from Reload Themes. `apply_combo_selection` applies whatever the combo shows as a live preview and is shared by the combo's change handler, Reload, and Delete. Import copies a chosen file into `user_themes_directory()` and adds its entry; Export writes `serialize_theme_to_json(theme(), active_color_scheme(), name)` to a chosen path, opening in `user_themes_directory()` with the shown theme's name suggested (`exportThemeFileDialog`), and a file saved inside that folder is rescanned and selected at once (the "copy a built-in, then edit it" path); bundled entries show as "Name (built-in)" so the copy and its source stay distinguishable; Reload Themes rescans and re-applies the selected entry even when the selection did not move (the authoring loop: edit the JSON, click Reload); Delete Theme... (enabled only on a user-folder entry, never a bundled one; `preferencesDeleteThemeConfirm` confirms) removes the file, rescans, and selects the first built-in entry; Open Themes Folder creates the folder and opens it in the file manager. All five buttons are `#ifndef Q_OS_WASM`. Reload and Delete block the combo's signals around the rescan, because removing the current item moves the selection. The dialog's revert-on-cancel guard branches on the `"custom:"` prefix, calling `set_custom_theme`/`clear_custom_theme` instead of `set_preference` for a custom entry.

## Known limitation

The QSS `url()` icon assets in `icon_theme.cpp` (`scroll-dither.svg` and its siblings, see the scroll-bar rule in [ui-conventions.md](ui-conventions.md)) are selected by base scheme and authored around the built-in `canvas_backdrop` value, not recolored per role. A custom theme with a very different backdrop can show a mismatched scrollbar-gutter texture. The same applies to a user pasteboard color chosen from the canvas right-click menu (`view/canvasBackdropColor`, [tools.md](tools.md)): it recolors only the pasteboard, never the scroll bars. Everything else, every painted widget and every hand-authored icon glyph, follows the custom palette exactly, because both read `theme()` at paint time.
