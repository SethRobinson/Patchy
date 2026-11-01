# GEGL filters

Patchy offers the image filters of the user's own GEGL (the library GIMP 3 builds most of its Filters menu from) under Plugins > GEGL Filters, one submenu per GEGL category. They run in Patchy's own filter dialog, with its canvas preview and in-dialog preview, progress and Cancel, and as one undo step. Legal boundary (binding): [legal-licensing.md](legal-licensing.md#gegl-operations-through-a-helper); sources consulted: [gimp-plugins-provenance.md](gimp-plugins-provenance.md). Status: on the local branch `desk/i567-gimp3-plugins`, not in main.

## Parts

| Part | Where | What |
|---|---|---|
| Helper | `src/plugins/gegl/host/main.cpp` (`patchy-gegl-host`) | Loads the user's GEGL, babl and GLib at run time; `--list` prints every operation and its settings as JSON, `--run` runs one operation on the pixels from stdin |
| Patchy side | `src/ui/gegl_filters.{hpp,cpp}` | Finding the library, the offered set (`gegl_operation_offered`), GEGL settings as Patchy filter parameters, a run through the helper |
| MainWindow | `src/ui/main_window_gegl_filters.cpp`, `apply_filter` in `main_window_filters.cpp` | The scan at startup (and with Rescan GIMP Plug-ins), the menu, the registry `apply_filter` takes for `gegl.*` ids |

Only the helper loads GEGL. Its declarations of GEGL, babl and GLib functions and structs are its own, from the installed public headers (`NOTICE-THIRD-PARTY.md`); nothing is linked at build time.

## Finding GEGL

From the GIMP 3 install the GIMP plug-in settings select (`select_gimp_install`, Preferences > Plug-ins): `bin/libgegl-0.4-0.dll` (Windows), `lib/libgegl-0.4.0.dylib` in GIMP.app's `Contents` (macOS), `lib`, `lib64` or the multiarch folder's `libgegl-0.4.so.0` (Linux, untested). Turning GIMP plug-ins off in Preferences turns these off too. `gegl_helper_command` builds the helper's command line for the install (`GeglHelperCommand`).

GIMP's Flatpak (Linux): the helper runs inside GIMP's own sandbox, like its plug-ins: `flatpak run --user|--system --die-with-parent --filesystem=<helper folder>:ro --command=<helper> org.gimp.GIMP --gegl /app/lib/libgegl-0.4.so.0`. Flatpak refuses to share a folder under `/usr`, so a helper installed there is first copied to `<AppData>/gegl-helper/`. The helper is built in Patchy's own SDK and needs only the C and C++ runtime libraries, which GIMP's runtime has (checked with GIMP 3.2.6 from Flathub: 259 operations, a native Patchy build passes `ui_gegl_`). From Patchy's own Flatpak GIMP's Flatpak is not used: the helper would have to reach GIMP's sandbox through `flatpak-spawn --host` and a host path to itself (not done; the menu says GEGL filters are not available from GIMP's Flatpak).

macOS: GIMP's dylibs name each other through an `@rpath` only GIMP's own programs set, so Patchy starts the helper with `DYLD_LIBRARY_PATH` at GIMP's `lib` folder (`prepare_helper`). A hardened-runtime helper ignores that variable and refuses libraries signed by another team, so `make-dmg.sh` signs `patchy-gegl-host` alone with `packaging/macos/patchy-gegl-host.entitlements` (`disable-library-validation`, `allow-dyld-environment-variables`) and then seals the app again over it. Checked with an ad-hoc hardened signature: refused without the entitlements, loads with them.

On Windows the helper puts GIMP's `bin` first on its DLL search path and `PATH` before loading, since GEGL loads its operation modules (`lib/gegl-0.4`) later and they find GIMP's other DLLs there. `GEGL_THREADS` is set to the core count unless already set.

## GPL-3 operations

GEGL loads its GPL-3 operations (Cartoon, Cubism, Oilify, Emboss, Ripple and about 30 more) only for a program that sets GeglConfig's `application-license` to "GPL3", as GIMP does. Patchy passes `--gpl3` and the helper sets it before `gegl_init` (afterwards GEGL reloads every module and warns about duplicate types). Without the flag the list drops from 258 to 207 operations on GIMP 3.2.6.

## What is offered

`gegl_operation_offered`: a `gegl:` operation with an input and an output pad and no aux pad (a one-layer filter), at least one category, none of the excluded categories (core, programming, hidden, meta, json, video, compositors, blend, transform, vector, svgfilter, input, output, debug), not excluded by name (inpainting, seamless clone, red eye, healing: the records in [legal-constraints.md](legal-constraints.md)), and no setting of a kind Patchy cannot hold (paths, curves, buffers, pointers). GIMP 3.2.6 on Windows: 258 operations, 112 offered (35 GPL-3) in 18 categories.

Ids are `gegl.` plus the operation name without `gegl:` (`gegl.gaussian-blur`). They live in a registry of their own (`GeglFilterScanResult::registry`), never in `filters_`: Smart Filters, Saved Looks, recipes and PSD output never see them, because another machine may lack the operation. Last Filter repeats one in the session.

## Settings

The helper reports each property from GEGL's own description of it (`gegl_operation_list_properties`): name, label, help, default, limits, the UI range of GEGL's own param specs, `unit` and choices. `parameter_for` maps them:

- double and float: Double, the limits clamped to plus or minus one million, the UI range as the slider range, the step from GEGL's digits.
- int, uint and seed: Integer; a seed gets the Randomize button.
- bool: Boolean. enum: Option, the choice nick as the value and GEGL's label as the text.
- `unit` pixel-distance or pixel-coordinate: pixels that scale with the in-dialog preview (`FilterSpatialScale::Pixels`); degree: degrees.
- color: an Option of Default (GEGL's own color, nothing sent), Foreground Color, Background Color, Black and White (`kColorChoices`, Patchy's existing data texts); the colors are the invocation's, captured when the filter starts. 18 offered operations have one (Drop Shadow, Vignette, Color to Alpha, Color Overlay, Supernova and others).
- string and anything else: not shown; GEGL's default applies.

An operation without a shown setting applies at once, without a dialog. Titles, setting labels, help and choice labels are GEGL's own, translated from GEGL's catalog (`gegl-0.4.mo`, shipped with GIMP): Patchy starts the scan with `LANGUAGE` set to its language and the helper asks libintl's `dgettext` (GLib's `g_dgettext` declines when the program has no catalog of its own). Three things make that work everywhere: the helper sets `LC_MESSAGES` from the environment (falling back to `en_US.UTF-8`, then `C.UTF-8`) because gettext ignores `LANGUAGE` in the "C" locale a program starts in, and only that category so the numbers it parses keep their notation; off Windows it binds `gegl-0.4` to `<library folder>/../share/locale`, since GIMP.app's GEGL is bound to its build machine's folder; and it asks for UTF-8 (`bind_textdomain_codeset`), since Windows libintl otherwise answers in the ANSI code page. A language change lists the operations again. GEGL's categories are not in its catalog: `gegl_category_label` names the 18 that GIMP 3.2's GEGL offers through Patchy's own catalog (context `GeglFilters`) and capitalizes any other.

## A run

`run_operation` sends `op`, `size`, `format`, one `set` line per shown setting and `end`, then the pixels tightly packed, and reads the same bytes back. Formats: `R'G'B'[A] u8`, `R'G'B'[A] u16` and `RGB[A] float` for Patchy's linear 32-bit; other color modes are refused. Every depth runs natively: `deep_filter_support` reports `gegl.` ids as Native. Every run starts the helper (about 0.3 s on Windows before the filter itself) on the worker that asked; the workers have no event loop, so the write and the wait go through `waitForBytesWritten` and `waitForFinished` in 100 ms steps, checking Cancel each time (Cancel kills the helper). `PATCHY_GEGL_TRACE=1` logs each run's steps on stderr.

## Tests

`ui_gegl_filters_list_and_apply_if_available` (tests/ui/gimp_plugin_tests.cpp; skips without GIMP's GEGL): the scan, Gaussian Blur's mapped settings, Cartoon offered through `--gpl3`, exclusions (Red Eye Removal, Crop, Over), runs straight through the registry at 8, 16 and 32 bits, Color Overlay in the foreground color, German labels, the menu entry, a run through `apply_filter` and its undo, and the Cartoon dialog (saved to `test-artifacts/gegl-cartoon-dialog.png`) cancelled with the layer unchanged.

## Not done

Any color beyond the five choices, translated category names, a persistent helper for faster previews, GIMP's Flatpak from Patchy's own Flatpak, a per-operation patent review before release, operations with an aux input (displace, bump map).
