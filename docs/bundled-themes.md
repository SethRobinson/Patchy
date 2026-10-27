# Bundled themes

Read this before adding or changing a theme that ships with Patchy. The theme formats themselves are in [theme-files.md](theme-files.md).

Every bundled theme is compiled into every build, wasm included, and lists under Included themes in Preferences. Its id is `kBundledThemeIdPrefix` (`bundled:`) plus its name in `themes/bundled/`; ids are persisted (`preferences/customThemeId`, script trust, theme command hotkeys), so a shipped name never changes. `bundled_theme_file_names()` in `theme_file.cpp` is the authored list and order, `theme_file_path_for_id` maps an id to its resource, and `load_theme_by_id` is the one read path for ThemeManager and the dialog. `ui_bundled_themes_load_and_list_in_preferences` loads each one and fails on any load warning.

## Single-file themes

`themes/bundled/*.patchytheme` go into `src/ui/themes.qrc` (resource prefix `/patchy/themes`). `scripts/dev/generate-bundled-themes.py` writes them from the authored Dark palette in `theme_palette.cpp`: neutral roles go through a per-theme tone ramp, blue-family accents take the theme's accent hue, every other hue is kept. Rerun it after a palette change and commit the result; an unknown-role warning in the test above is how drift shows. A new one is added to the generator, the qrc and the list. The generator writes only its own files, never a package folder.

## Theme packages

A package is a folder, `themes/bundled/<name>/`, laid out like a user package. CMake writes every file in it except its `README.txt` into the generated `theme_packages.qrc` (build folder) under `/patchy/themes/<name>/`; `CONFIGURE_DEPENDS` regenerates the list when a file is added or removed and rcc rebuilds on any edit, so the folder is the only source. It loads through `load_theme_package_directory` on the resource folder, with every check a user folder gets. Adding one: the folder, its name in the list, and its source-to-resource comparison in `ui_bundled_theme_package_loads_from_resources_and_source`.

- Scripts: a bundled theme's scripts ship with Patchy, so `stored_theme_script_trust` reports them Allowed with no prompt until the user clears Run this theme's scripts; that stored deny holds whatever the digest, so an update does not turn them back on. A user copy of the same package, under its own id, still asks.
- Text: theme scripts have no `tr`, so a package's command titles and status lines stay English like bundled script text ([localization.md](localization.md#deliberately-english)). Keep them few and short.
- Size: every byte is in every build. Photographic images are JPEG, a splash a 256-color PNG, sounds 22.05 kHz 16-bit mono WAV.
- Editing without a rebuild: start Patchy with `PATCHY_THEME_SOURCE_DIR` set to the source `themes/bundled` folder. A bundled package id then reads from `<that folder>/<name>/` (when it holds `theme.json`), and ThemeManager watches and reloads it like a user folder ([Authoring loop](theme-files.md#authoring-loop)), under the id it ships with, so script trust and the theme menu's hotkeys behave as they will for users. Rebuild before committing: the test compares the resource with the source.

## Dink Smallwood

`themes/bundled/dink-smallwood/` (id `bundled:dink-smallwood`) uses Dink Smallwood art: the original game data (RTDink LICENSE.md 5a, zlib-style) and HD media (5b), by Seth A. Robinson, who permits the name (clause 4). Its sounds are his own recordings (his homemade sets, and the bin/dink sounds he made himself) or synthesized for the theme. Sounds of unclear origin (5c) must never be added: 5d covers them only inside Dink Smallwood HD itself. The package README records the source and processing of every file; a new or changed asset updates it. The generator scripts for its sounds and seamless stone read Seth's private source recordings and stay outside the repository. NOTICE-THIRD-PARTY.md ("Bundled UI themes") carries the attribution.
