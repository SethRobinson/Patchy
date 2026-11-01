# GEGL filters

Patchy offers GEGL's image filters (GEGL is the Generic Graphics Library GIMP builds most of its Filters menu from) under Filter > GEGL Filters, one submenu per GEGL category. They come from Patchy's own GEGL where the package ships one ("Bundled GEGL"), else from the user's GIMP 3. They run in Patchy's own filter dialog, with its canvas preview and in-dialog preview, progress and Cancel, and as one undo step. Legal boundary (binding): [legal-licensing.md](legal-licensing.md#gegl-operations-through-a-helper); sources consulted: [gimp-plugins-provenance.md](gimp-plugins-provenance.md). Status: on the local branch `desk/i567-gimp3-plugins`, not in main.

## Parts

| Part | Where | What |
|---|---|---|
| Helper | `src/plugins/gegl/host/main.cpp` (`patchy-gegl-host`) | Loads the user's GEGL, babl and GLib at run time; `--list` prints every operation and its settings as JSON, `--run` runs one operation on the pixels from stdin |
| Patchy side | `src/ui/gegl_filters.{hpp,cpp}` | Finding the library, the offered set (`gegl_operation_offered`), GEGL settings as Patchy filter parameters, a run through the helper |
| MainWindow | `src/ui/main_window_gegl_filters.cpp`, `apply_filter` in `main_window_filters.cpp` | The scan at startup (and with Rescan GIMP Plug-ins), the menu, the registry `apply_filter` takes for `gegl.*` ids |

Only the helper loads GEGL. Its declarations of GEGL, babl and GLib functions and structs are its own, from the installed public headers (`NOTICE-THIRD-PARTY.md`); nothing is linked at build time.

## Bundled GEGL

Decided November 3, 2026 (Seth asked for Patchy's own GEGL, independent of GIMP and its versions). `bundled_gegl_library` looks beside the program (`<program folder>/gegl` on Windows and in build trees, `Contents/Frameworks/gegl` in the macOS bundle, `../lib/patchy/gegl` or the Flatpak's `/app/lib` on Linux); when it is there it is used whatever the GIMP plug-in settings say, and GIMP's GEGL is not consulted.

- Built from gimp.org's GEGL 0.4.72 and babl 0.1.128 release tarballs (SHA-256 pinned) with every optional dependency off (`-Dauto_features=disabled`: no PDF, raw, SVG, text, video or TIFF code), by `scripts/dev/gegl/build-gegl-msys2.sh` on Windows (MSYS2 CLANG64 for x64, CLANGARM64 for ARM64). It stages `.deps/gegl/0.4.72/<platform>/{bin,lib/gegl-0.4,lib/babl-0.1,share/locale,licenses}`; `cmake/patchy_gegl.cmake` finds it and copies it once into `<build>/gegl` (`patchy_gegl_bundle`); `build-release.bat` refuses a package without it and `package-selftest.js` runs Gaussian Blur from the packaged app.
- Shipped: `libgegl`, `libbabl`, the libraries they import (followed with `ldd`: GLib's five, json-glib, libffi, PCRE2, libiconv, libintl, zlib), the operation modules `gegl-common`, `gegl-common-cxx`, `gegl-generated` (with their `-x86_64-v2`/`-v3` builds, which GEGL picks on newer CPUs), `gegl-core`, babl's extensions minus `cairo`, GEGL's catalogs for Patchy's languages, and every license with `licenses/SOURCES.txt` (versions, tarball URLs, MSYS2 package versions). Not shipped: file loaders and savers (and with them libjpeg and libpng), transforms, seamless clone, vector fill, the meta JSON operations, and the GPL-3 module (`gegl-common-gpl3`; `--gpl3` on the script adds it, see "Legal review").
- Windows x64: 116 files, 17.4 MB, 7.3 MB in a zip (6.7 MB without the catalogs); 77 filters offered.
- Windows ARM64 (built natively under CLANGARM64): 74 files, 11.7 MB, 4.8 MB zipped (no CPU-variant modules). MSYS2's `ldd` hangs there, so the script follows the import tables with `objdump`.
- Linux: `scripts/dev/gegl/build-gegl-linux.sh` builds the same way inside the Flatpak SDK of Patchy's manifest (`org.kde.Sdk`, whose GLib and json-glib the bundle uses, so they are not staged) into `.deps/gegl/0.4.72/linux-x64` for build trees: 92 files, 11 MB, 4.3 MB compressed. Patchy's own Flatpak builds babl and GEGL as manifest modules from the same tarballs (`--libdir=lib`, the licenses into `/app/share/licenses/com.rtsoft.patchy`, `cleanup` removing what the Windows bundle leaves out) and finds `/app/lib/libgegl-0.4.so.0`.
- macOS: `scripts/dev/gegl/build-gegl-macos.sh` builds everything shipped from source for macOS 12.0 (Homebrew's libraries target the macOS they were built on): gettext's libintl, GLib (PCRE2 from its subproject; libffi and libiconv are the system's), json-glib, babl, GEGL; every library is renamed `@rpath/<file>` with `@loader_path` rpaths and checked to name no Homebrew path. `.deps/gegl/0.4.72/osx-arm64`: 60 files, 10 MB, 3.6 MB zipped. The app bundle carries it in `Contents/Frameworks/gegl`; `make-dmg.sh` signs each library there before sealing the app.
- Off Windows a bundled GEGL is started with `LD_LIBRARY_PATH` (macOS: `DYLD_LIBRARY_PATH`) at its library folder and `GEGL_PATH`/`BABL_PATH` at its module folders (`prepare_helper`, `GeglHelperCommand::bundled`); Windows GEGL finds its modules relative to its DLL.

## Finding GIMP's GEGL

When the build has no GEGL of its own, from the GIMP 3 install the GIMP plug-in settings select (`select_gimp_install`, Preferences > Plug-ins): `bin/libgegl-0.4-0.dll` (Windows), `lib/libgegl-0.4.0.dylib` in GIMP.app's `Contents` (macOS), `lib`, `lib64` or the multiarch folder's `libgegl-0.4.so.0` (Linux, untested). Turning GIMP plug-ins off in Preferences turns GIMP's GEGL off too (not Patchy's own). `gegl_helper_command` builds the helper's command line for the install (`GeglHelperCommand`).

GIMP's Flatpak (Linux): the helper runs inside GIMP's own sandbox, like its plug-ins: `flatpak run --user|--system --die-with-parent --filesystem=<helper folder>:ro --command=<helper> org.gimp.GIMP --gegl /app/lib/libgegl-0.4.so.0`. Flatpak refuses to share a folder under `/usr`, so a helper installed there is first copied to `<AppData>/gegl-helper/`. The helper is built in Patchy's own SDK and needs only the C and C++ runtime libraries, which GIMP's runtime has (checked with GIMP 3.2.6 from Flathub: 259 operations, a native Patchy build passes `ui_gegl_`). From Patchy's own Flatpak GIMP's Flatpak is not used: the helper would have to reach GIMP's sandbox through `flatpak-spawn --host` and a host path to itself (not done; the menu says GEGL filters are not available from GIMP's Flatpak).

macOS: GIMP's dylibs name each other through an `@rpath` only GIMP's own programs set, so Patchy starts the helper with `DYLD_LIBRARY_PATH` at GIMP's `lib` folder (`prepare_helper`). A hardened-runtime helper ignores that variable and refuses libraries signed by another team, so `make-dmg.sh` signs `patchy-gegl-host` alone with `packaging/macos/patchy-gegl-host.entitlements` (`disable-library-validation`, `allow-dyld-environment-variables`) and then seals the app again over it. Checked with an ad-hoc hardened signature: refused without the entitlements, loads with them.

Windows on ARM: GIMP's installer puts a native ARM64 GIMP there, and the ARM64 helper loads it (checked with GIMP 3.2.6 on a Snapdragon X Elite, November 3, 2026: `ui_gegl_` passes). An x64 GIMP installed on ARM64 Windows cannot be loaded by the ARM64 helper (a process loads only its own architecture's DLLs); the menu then says GIMP's GEGL could not be loaded, while that GIMP's plug-ins, separate programs, still run. On Windows the helper puts GIMP's `bin` first on its DLL search path and `PATH` before loading, since GEGL loads its operation modules (`lib/gegl-0.4`) later and they find GIMP's other DLLs there. `GEGL_THREADS` is set to the core count unless already set.

## Legal review

Binding rule: [legal-licensing.md](legal-licensing.md#gegl-operations-through-a-helper). Of the 124 operations "What is offered" admits on GIMP 3.2.6's GEGL, the review of November 3, 2026 applied the recorded patent families by algorithm class, as the G'MIC triage did ([patent-research-gmic.md](patent-research-gmic.md#family-triage-of-the-offered-filters-2026-10-20)), from each operation's own description (GEGL's source is never read):

- Left out (`excluded_by_name`): Median Blur (the Surface Blur rule allows only one window histogram updated one value at a time; GEGL's method cannot be checked), Focus Blur (a per-pixel blur-radius mask, the Blur Gallery family), SLIC and Waterpixels (superpixel methods, not reviewed), Shadows-Highlights (the Shadow/Highlight adjustment family, not reviewed), with the earlier inpainting, seamless clone, red eye and healing names.
- Within recorded boundaries: Linear Motion Blur (one straight user kernel), Circular and Zoom Motion Blur (one polar transform), Bilateral Filter (direct range weights, no histogram), Unsharp Mask and High Pass, Noise Reduction, DCT Denoise and SNN Mean (no patch search), the tone mappers (Fattal 2002, Reinhard 2005, Mantiuk 2006), Stretch Contrast and Color Enhance (whole-image statistics), the rest plain point, convolution or geometric operations.
- Not verified: Domain Transform (edge-aware smoothing, the guided-filter family the G'MIC record lists as unread), Mantiuk 2006's patent status, Bloom and Long Shadow by assignee sweep. A release needs no more than this list's decisions; adding an operation (a new GEGL) needs the same check.
- GPL-3 operations (43 of the 124: Cartoon, Oilify, Emboss, Ripple, Plasma and others) are legal to ship but bring GPL-3 duties (their license and complete source with every release); the bundle leaves them out until Seth decides. GIMP's GEGL still offers them where it is used.

## GPL-3 operations

GEGL loads its GPL-3 operations (Cartoon, Cubism, Oilify, Emboss, Ripple and about 30 more) only for a program that sets GeglConfig's `application-license` to "GPL3", as GIMP does. Patchy passes `--gpl3` and the helper sets it before `gegl_init` (afterwards GEGL reloads every module and warns about duplicate types). Without the flag the list drops from 258 to 207 operations on GIMP 3.2.6.

## The scan

At startup (not offscreen), on Rescan GIMP Plug-ins and on a language change, a worker lists the operations (`scan_gegl_filters`). The helper's list is cached in `<AppData>/gegl-filters/operations.json` under a key of the cache version, GEGL's library file (path, size, time; inside GIMP's Flatpak the host file, `library_file`), the helper file and the language, so a launch with nothing changed starts no helper. Bump `kCacheVersion` when the list's shape changes.

## What is offered

`gegl_operation_offered`: a `gegl:` operation with an output pad and no aux pad, and either an input pad (a one-layer filter) or the `render` category (Plasma, Solid Noise, Perlin, Simplex and Cell Noise, Sinus, Spiral, Diffraction Patterns, gradients, Grid, Bayer Matrix, GEGL's Fractal Explorer: the helper links no input and the result replaces the layer inside the selection, as GIMP's Render filters do), at least one category, none of the excluded categories (core, programming, hidden, meta, json, video, compositors, blend, transform, vector, svgfilter, input, output, debug), not excluded by name (inpainting, seamless clone, red eye, healing: the records in [legal-constraints.md](legal-constraints.md)), and no setting of a kind Patchy cannot hold (text, paths, curves, buffers, pointers): text settings are file paths, matrices, the graph language of `gegl:gegl` (which could load files) or `gegl:text`'s words. GIMP 3.2.6 on Windows: 258 operations, 124 offered (43 GPL-3) in 18 categories.

Ids are `gegl.` plus the operation name without `gegl:` (`gegl.gaussian-blur`). They live in a registry of their own (`GeglFilterScanResult::registry`), never in `filters_`: Smart Filters, Saved Looks, recipes and PSD output never see them, because another machine may lack the operation. Last Filter repeats one in the session.

## Settings

The helper reports each property from GEGL's own description of it (`gegl_operation_list_properties`): name, label, help, default, limits, the UI range of GEGL's own param specs, `unit` and choices. `parameter_for` maps them:

- double and float: Double, the limits clamped to plus or minus one million, the UI range as the slider range, the step from GEGL's digits.
- int, uint and seed: Integer; a seed gets the Randomize button.
- bool: Boolean. enum: Option, the choice nick as the value and GEGL's label as the text.
- `unit` pixel-distance or pixel-coordinate: pixels that scale with the in-dialog preview (`FilterSpatialScale::Pixels`); degree: degrees.
- color: an Option of Default (GEGL's own color, nothing sent), Foreground Color, Background Color, Black and White (`kColorChoices`, Patchy's existing data texts); the colors are the invocation's, captured when the filter starts. 18 offered operations have one (Drop Shadow, Vignette, Color to Alpha, Color Overlay, Supernova and others).
- text and anything else: the operation is not offered (see What is offered).

An operation without a shown setting applies at once, without a dialog. Titles, setting labels, help and choice labels are GEGL's own, translated from GEGL's catalog (`gegl-0.4.mo`, shipped with GIMP): Patchy starts the scan with `LANGUAGE` set to its language and the helper asks libintl's `dgettext` (GLib's `g_dgettext` declines when the program has no catalog of its own). Three things make that work everywhere: the helper sets `LC_MESSAGES` from the environment (falling back to `en_US.UTF-8`, then `C.UTF-8`) because gettext ignores `LANGUAGE` in the "C" locale a program starts in, and only that category so the numbers it parses keep their notation; off Windows it binds `gegl-0.4` to `<library folder>/../share/locale`, since GIMP.app's GEGL is bound to its build machine's folder; and it asks for UTF-8 (`bind_textdomain_codeset`), since Windows libintl otherwise answers in the ANSI code page. A language change lists the operations again. GEGL's categories are not in its catalog: `gegl_category_label` names the 18 that GIMP 3.2's GEGL offers through Patchy's own catalog (context `GeglFilters`) and capitalizes any other.

## A run

`run_operation` sends `op`, `size`, `format`, one `set` line per shown setting and `end`, then the pixels tightly packed, and reads the same bytes back. Formats: `R'G'B'[A] u8`, `R'G'B'[A] u16` and `RGB[A] float` for Patchy's linear 32-bit; other color modes are refused. Every depth runs natively: `deep_filter_support` reports `gegl.` ids as Native. Every run starts the helper (about 0.3 s on Windows before the filter itself) on the worker that asked; the workers have no event loop, so the write and the wait go through `waitForBytesWritten` and `waitForFinished` in 100 ms steps, checking Cancel each time (Cancel kills the helper). `PATCHY_GEGL_TRACE=1` logs each run's steps on stderr.

## Scripting

`patchy.gimpPlugins.geglFilters()` (`ScriptEngineHost::gegl_filter_list`, scans on first use) lists `{id, name, category, description, parameters}`, each parameter `{name, label, type, default, minimum, maximum, choices}` with the permanent type tokens `integer`, `number`, `boolean`, `choice`. `layer.applyFilter` takes a `gegl.` id and routes it to the GEGL registry (`apply_filter_to_layer`); parameters are keyed by GEGL's property name, a color by its choice value. Record: [scripting-api-changes.md](scripting-api-changes.md).

## Tests

`ui_gegl_filters_list_and_apply_if_available` (tests/ui/gimp_plugin_tests.cpp; uses Patchy's own GEGL, else GIMP's, and skips only without both; a build with its own GEGL must offer it): the scan, Gaussian Blur's mapped settings, Cartoon offered only from GIMP's GEGL (the bundle has no GPL-3 operation), the legal and kind exclusions (Median Blur, Focus Blur, SLIC, Waterpixels, Shadows-Highlights, Seamless Clone, Red Eye Removal, Crop, Over, Text, PNG Load), Simplex Noise as a render operation, runs straight through the registry at 8, 16 and 32 bits, Color Overlay in the foreground color, German labels, the menu entry, a run through `apply_filter` and its undo, script listing and `applyFilter`, the Unsharp Mask and Vignette dialogs (saved to `test-artifacts/gegl-unsharp-mask-dialog.png` and `gegl-vignette-dialog.png`; Vignette shows a color choice) cancelled with the layer unchanged, the menu and its Blur submenu (`gegl-filters-menu.png`, `gegl-filters-blur-menu.png`), and Search Commands finding Bloom.

## Not done

Any color beyond the five choices, translated category names, a persistent helper for faster previews, GIMP's Flatpak from Patchy's own Flatpak, a per-operation patent review before release, operations with an aux input (displace, bump map).
