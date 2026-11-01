# GIMP 3 plug-ins

Patchy runs the user's installed GIMP 3 plug-ins on one pixel layer: the plug-in's own dialog opens over Patchy with its preview working, results it merges show on the canvas as they arrive, and OK lands the result as one undo step limited to the selection. GIMP 2.10 plug-ins are not supported. Legal boundary (binding): [legal-licensing.md](legal-licensing.md#gimp-3-plug-in-hosting); every source consulted: [gimp-plugins-provenance.md](gimp-plugins-provenance.md).

## Parts

| Part | Where | What |
|---|---|---|
| Protocol | `src/plugins/gimp/gimp_protocol.hpp`, `gimp_wire_codec.{hpp,cpp}` | Patchy's own declarations and codec of GIMP's pipe messages (protocol 0x0117, GIMP 3.2.x) |
| Install and files | `src/plugins/gimp/gimp_install.{hpp,cpp}` | Finding GIMP 3, its plug-in folders, `interpreters/*.interp`, `environ/*.env`, `gimprc` |
| Query model | `src/plugins/gimp/gimp_procedure.{hpp,cpp}` | What a plug-in registers, the classification with reason codes, default arguments |
| Link | `src/plugins/gimp/gimp_host_link.hpp` | Patchy's own frames between Patchy and the helper |
| Helper | `src/plugins/gimp/host/` (`patchy-gimp-host`) | Starts plug-ins, the message loop (`session.cpp`), the one-layer tile server (`image_model.cpp`), the PDB table (`pdb_*.cpp`, one file per group) |
| Patchy side | `src/ui/gimp_plugin_catalog.*`, `gimp_plugin_runner.*`, `gimp_plugin_run_dialog.*`, `main_window_gimp_plugins.cpp`, `script_gimp_plugins.cpp` | Scan and cache, running a procedure, the run box, the menu, Preferences, scripting |

All of `src/plugins/gimp/` is Qt-free and shared by Patchy, the helper and the core tests. No Patchy process loads a GIMP, GLib, GTK, GEGL or babl library.

## Finding GIMP

- Windows: the GIMP installer's uninstall key `Software\Microsoft\Windows\CurrentVersion\Uninstall\GIMP-3*_is1` under HKCU, then HKLM (64 and 32-bit views): `InstallLocation` and `DisplayVersion`. Then `%LOCALAPPDATA%\Programs\GIMP 3` and `%ProgramFiles%\GIMP 3`. Plug-ins in `<install>\lib\gimp\3.0\plug-ins`; the user's own in `%APPDATA%\GIMP\<3.N>\plug-ins` (the series from the version, or from a `gimp-3.N.exe` in `bin`); extra folders from that folder's `gimprc` `(plug-in-path "...")` with `${gimp_dir}` and `${gimp_plug_in_dir}` expanded.
- Linux: `/usr`, `/usr/local`, `/opt/gimp` with `lib/gimp/3.0`, `lib64/gimp/3.0` or a multiarch `lib/<triplet>/gimp/3.0`; user folder `$XDG_CONFIG_HOME/GIMP/3.N`. GIMP's Flatpak (`~/.local/share/flatpak` or `/var/lib/flatpak`, `app/org.gimp.GIMP/current/active/files`) is detected and reported as not supported yet (it needs `flatpak-spawn --host` into GIMP's sandbox).
- macOS: `/Applications/GIMP.app` and `~/Applications/GIMP.app` (`Contents/Resources`), user folder `~/Library/Application Support/GIMP/3.N`.
- Preferences > Plug-ins can name the install folder by hand (`gimpPlugins/installFolder`), add plug-in folders (`gimpPlugins/userFolders`) and turn the feature off (`gimpPlugins/enabled`, default on). Folders are searched most specific first (added, user, gimprc, system); a plug-in folder name seen earlier wins.
- A GIMP 3 plug-in lives in its own subfolder named after it: `<name>/<name>.exe` (Windows), `<name>/<name>` (Linux, macOS) or `<name>/<name>.<ext>` for an extension an `.interp` file maps (`.py`, `.scm`).

## Starting a plug-in

- Command line, as observed from GIMP 3.2.6: `<program> [script] -gimp <protocol, decimal 279> <read fd> <write fd> <-query|-init|-run> <stack-trace mode 0>`. The two pipes are C runtime descriptors 3 and 4: on Windows through the C runtime's inherited-descriptor table in `STARTUPINFO` (`plugin_process_win.cpp`), elsewhere by `dup2` (`plugin_process_posix.cpp`). stdin is the null device; stdout and stderr go to one pipe the helper keeps (the last 32 KB end up in crash reports, everything in the trace).
- Interpreted plug-ins: the extension rule (`:Python:E::py::python3:`) or the script's `#!` line names an interpreter, the `.interp` name lines map it to a program (later files win: `pythonw.exe` over `python.exe` on Windows), relative programs are found on the PATH the `environ` files give plus GIMP's `bin`.
- Environment: the helper's own, then the `environ/*.env` lines (`${gimp_installation_dir}` expanded; `PATH=` replaces PATH), `GIMP3_DIRECTORY` = Patchy's own profile (`<AppData>/gimp-plugins/profile`, so plug-ins never write GIMP's settings), `GSETTINGS_SCHEMA_DIR` when GIMP ships schemas, `LANGUAGE`/`LANG` = Patchy's language (plug-ins translate their own labels and dialogs), `PYTHONIOENCODING=utf-8`; `PYTHONHOME`, `PYTHONPATH`, `PYTHONSTARTUP`, `GI_TYPELIB_PATH`, `GTK_THEME`, `GTK_PATH` are removed. Working directory: the plug-in's folder.
- Process tree: Windows starts the plug-in suspended inside a kill-on-close job object; Linux and macOS make it a process-group leader (Linux also `PR_SET_PDEATHSIG`). Every outcome ends the whole tree once the run is over; the helper ending (or Patchy's link closing) ends it too.

## Protocol

Messages carry no length: a reader follows the grammar. Big-endian numbers; a string is a u32 byte count including its NUL (0 = NULL) and the bytes; arrays are a u32 count and the elements. Grammar facts that are not in the header layouts, all observed: `CONFIG` puts the two check colors (bytes, encoding, ICC) right after `check_type`, ahead of the nine flag bytes; a color is its pixel bytes, then its babl encoding, then its profile; a `CHOICE` definition is its default nick, then the count, then {nick, id, label, help}; a `GEGL_COLOR` definition is `has_alpha` then a color with no presence flag; an `INT` definition is three i64. Tile reads: `TILE_REQ {drawable, tile, shadow}`, the host answers `TILE_DATA` with the bytes, the plug-in sends `TILE_ACK`. Tile writes: `TILE_REQ {-1, 0, 0}`, the host answers an empty `TILE_DATA`, the plug-in sends `TILE_DATA` with the bytes, the host sends `TILE_ACK`. Tiles are 128 x 128, numbered row by row; shadow tiles start as zeros. Patchy sends `shm_id -1`, so tile bytes always travel in the messages. A PDB error return is the status alone. Records never seen in traffic (curves) are refused with a clear error rather than guessed.

## Query and the menu

- The scan (`scan_gimp_plugins`, a worker at startup and on Rescan) lists plug-in files, reuses the cache (`<AppData>/gimp-plugins/query-cache.json`, keyed by path, size and file time, invalidated by a cache version, GIMP's version and Patchy's language) and queries the rest with up to four helpers in parallel. Each query records `PROC_INSTALL` and the `gimp-pdb-set-proc-*` / `gimp-pdb-add-proc-menu-path` calls; failed queries are cached too, so a broken plug-in is not restarted on every launch.
- Classification (`classify_procedure`, reason codes in `gimp_procedure.hpp`, user text in `MainWindow::gimp_plugin_reason_text`): file load/export handlers, batch interpreters, temporary procedures and procedures without an `<Image>` menu path are hidden; the rest are runnable only when they take GIMP's image-procedure arguments (run mode, image, drawables), accept one drawable (the sensitivity mask), accept RGB, sit under Filters, Colors or Layer, and do not come from Script-Fu. Refusals keep their reason and show in Preferences > Plug-ins and `patchy.gimpPlugins.list()`.
- Plugins > GIMP 3 Plug-ins (`gimpPluginsMenu`) holds one submenu per menu path (a leading "Filters" dropped, `[placement]` hints skipped), `gimpPluginAction` entries (data = `gimp.<procedure>`, the persisted id) and Rescan GIMP Plug-ins (`gimpPluginsRescanAction`, hotkey id `plugins.gimp_rescan`, which re-queries every plug-in). The entries are data-built (`kCommandSearchDataEntryProperty`). Notes while scanning, when GIMP is missing, or when nothing runs.

## Running

`MainWindow::run_gimp_plugin` applies the legacy plug-in gates (Quick Mask, channel views, one selected layer, the rasterize prompt for text and shapes, locked pixels) and `apply_gimp_plugin` does the run, shared with scripts:

- The layer: 8, 16 or 32-bit RGB or RGBA, as is (formats `R'G'B'[A] u8`, `R'G'B'[A] u16`, `RGB[A] float`; GIMP precisions 150, 250, 600). Other color modes are refused. A layer with locked transparency keeps its alpha. The selection's coverage (`selection_mask_pixels`) and its bounds limit the merge.
- The helper gets the pixels through a temporary file both map (`patchy-gimp-<uuid>.pixels`: input, current, mask) and sends `CONFIG` (display 1, Patchy's name, check colors) and `PROC_RUN` with the procedure's registered defaults, or a script's arguments. Interactive runs (menu) pass run mode 0, so the plug-in shows its dialog and restores its own last values.
- The image a plug-in sees: image 1 (Patchy's document size), layer 2 (the layer, at its offset), selection channel 3 (image-sized, `Y u8`), display 1. `gimp-display-get-window-handle` and `gimp-progress-get-window-handle` return Patchy's window (HWND bytes on Windows), so dialogs open over it.
- `gimp-drawable-merge-shadow` merges the shadow tiles the plug-in wrote into the current pixels through the selection (alpha-weighted where coverage is partial); merged areas reach Patchy as updates on `gimp-displays-flush` or every 50 ms, and Patchy writes them into the layer live. Cancel or an error puts the original pixels back; OK puts them back too, pushes the undo snapshot, then writes the result.
- The run box (`gimpPluginRunDialog`) shows after a second or when the plug-in's window appears (Windows: the helper reports the plug-in's largest visible window); it never takes focus. "Stop Plug-in" sends Cancel; the helper ends the plug-in tree, and after five seconds Patchy kills the helper.
- Results: Applied, NoChange, Cancelled (the plug-in returned `GIMP_PDB_CANCEL` or the user stopped it), Error with a reason: `unsupported-procedure` names the first PDB procedure Patchy lacks; `crashed` carries the exit description and the plug-in's last output; `timeout` (scripts default to 120 s); `start`; `unsupported-argument`. Data stored with `gimp-pdb-set-data` is kept for the session and handed back on the next run.

## PDB procedures

One file per group in `src/plugins/gimp/host/`: registration (query time), image, item and layer, drawable (geometry, formats, mask bounds, shadow merge, updates, thumbnails, pixels), selection (read-only), context (Patchy's colors, plain defaults), progress, misc (messages, displays, gimprc data paths, stored data, introspection). Anything else returns an execution error and is reported. Not provided: creating or removing layers, images, channels or paths; changing the selection; GEGL operations and drawable filters; brushes, patterns, gradients, fonts and palettes; parasites; undo control beyond no-op groups; running other plug-ins.

## Scripting

`patchy.gimpPlugins`: `list()` (`{id, procedure, name, menu, supported, hidden, reason, reasonCode, file, blurb, imageTypes, params}`), `rescan({force})`, `install()` (`{found, folder, version, series, source, flatpak, pluginFolders, problem}`), `folders`, `enabled`, `installFolder` (persisted; setting rescans). `layer.applyGimpPlugin(id, {dialog, lastValues, args, timeoutMs, trace, wireLog, captureDialog, answerDialog})`: non-interactive by default with a 120 s limit; `dialog: true` is interactive (no time limit unless the dialog is answered). Windows: once the dialog has been up 1.5 s (`gimp_plugin_window.cpp`: the plug-in process's largest window) `captureDialog` saves it with `PrintWindow` and `answerDialog` posts Enter (OK) or Escape (Cancel) to that window, again every 3 s up to three times; unattended runs answer OK by themselves; `args` sets arguments by registered name, checked against their type and range (choices by nick, colors as `"#rrggbb"` or `[r, g, b, a]`). See [scripting.md](scripting.md).

## Results

GIMP 3.2.6 (Windows installer, 132 plug-in folders), November 1, 2026, with `scripts/dev/gimp/query_sweep.py` and `scripts/dev/gimp/sweep.js` (non-interactive, then interactive with every dialog captured and answered). Every plug-in registered without a crash. "Same as GIMP" means byte-identical output to GIMP's own batch run on the same image and selection (`plug-in-jigsaw` is random in GIMP too; `plug-in-hot` differs in GIMP between runs).

| Result | Procedures |
|---|---|
| Works, same as GIMP | Blinds, Checkerboard (legacy), Despeckle, Destripe, Fractal Explorer, Lighting Effects, Map Object, Palette Map, Retinex, Small Tiles, Sparkle |
| Works (interactive, as in GIMP) | CML Explorer, GIMPressionist, IFS Fractal, Qbist, Sample Colorize, Van Gogh (LIC), Jigsaw, Hot; Sphere Designer's dialog and preview work, but the automated run could not press its OK |
| Runs, no layer change by design | Border Average (sets Patchy's foreground color), Export Histogram (writes a file), Image Map (an editor for HTML maps), Grid (legacy) and NL Filter with their default values |
| Refused before running | Animation filters (4), Curve Bend, Decompose, Depth Merge, Film, Flame, Gfig, Gradient Flare, Gradient Map, Pagecurl, Smooth Palette, Spyrogimp, Tile, Warp, Wavelet-decompose, Fog: each needs new layers or images, several layers, GIMP's clipboard or painting tools, or GIMP's gradients; Colormap Remap, Compose, Recompose (indexed or grayscale); Align Layers, Guillotine, Zealous Crop, Metadata Editor and Viewer, Print, Screenshot, scanner import (image or file commands); the consoles, browsers and Unit Editor (no image); Script-Fu scripts |
| Not applicable (hidden) | About 70 file load and export plug-ins, batch interpreters and helpers without a menu entry |

Not tested yet: Linux and macOS runs (no GIMP 3 on the build hosts; both build and pass the core tests), GIMP's Flatpak (detected and refused), third-party plug-ins.

## Development tools

`scripts/dev/gimp/wire_tap.cpp` (a relay that stands in for a plug-in under real GIMP and logs both directions), `wire_dump.py` (prints a log as messages), `query_sweep.py` (queries every plug-in of an install through the helper). `PATCHY_GIMP_TRACE=1` logs every message and PDB call through Patchy's stderr; `PATCHY_GIMP_WIRE_LOG=<file>` (or `wireLog`) writes the tap's log format from the helper.

## Tests

- Core (`tests/core/gimp_plugin_tests.cpp`, filter `gimp_`): GIMP 3.2.6 messages decoded and re-encoded byte for byte, round trips of every message and parameter kind, broken-stream refusals, classification reasons, menu paths, the installed files under Unicode paths, default arguments, the link frames.
- Test plug-in: `test-fixtures/gimp-plugins/patchy-test/patchy-test.py` (MIT, Patchy's own; also checked in real GIMP): Invert Bands (tiles in and out, selection bounds, progress, a dialog with a drawable preview), Paint Context (image, context and message calls), Needs a New Layer (a procedure Patchy refuses).
