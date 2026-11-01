# GIMP 3 plug-in host: provenance log

Evidence that Patchy's GIMP 3 plug-in host was built from interface facts only. The binding rule is [legal-licensing.md](legal-licensing.md#gimp-3-plug-in-hosting); the design is [gimp-plugins.md](gimp-plugins.md). Add a dated entry for every new source, tool or decision, in the same change that uses it. Each entry names the source, its version and licence, and the facts taken from it.

## Never read

None of these were opened, searched or excerpted at any point, by a person or a tool working on Patchy: GIMP's `app/` tree (including `app/plug-in`, `app/pdb`, `app/core`), any `.c` file of libgimp, libgimpbase, libgimpui, libgimpwidgets, libgimpcolor or libgimpconfig, any plug-in's source (GIMP's own `plug-ins/` tree and third-party plug-ins), GEGL, babl, GLib's or GTK's source. Patchy's codec, PDB table and tile server are written from the facts below and checked against observed traffic.

## Log

### 2026-11-01: the rule

- Decided: hosting GIMP 3 plug-ins is allowed under the rule in [legal-licensing.md](legal-licensing.md#gimp-3-plug-in-hosting). Reason: plug-ins are separate programs; interoperating with them over their documented interface uses interface facts (message layouts, constants, procedure names), and LGPL-3 section 3 expressly allows using a header's numerical parameters and data structure layouts. GIMP 2.10 plug-ins are out of scope.
- `LICENSE` of GIMP at tag `GIMP_3_2_6` (gitlab.gnome.org/GNOME/gimp, the licence summary itself): read in full. Taken: the core is GPL, libgimp and the other GIMP libraries are LGPL, and the GIMP developers' stated position that a program invoking or providing methods through libgimp "or another implementation of the 'procedural database' (pdb) serial protocol" is a "mere aggregation" with the program on the other side.

### 2026-11-01: protocol headers

- `libgimpbase/gimpprotocol.h` and `libgimpbase/gimpwire.h` at tag `GIMP_3_2_6`, LGPL-3-or-later, downloaded from gitlab.gnome.org to a scratch folder outside the repository (never committed). Taken, under LGPL-3 section 3: `GIMP_PROTOCOL_VERSION` (0x0117); the message type numbers (`GP_QUIT` 0 to `GP_HAS_INIT` 12); the `GPParamDefType` (0 to 14) and `GPParamType` (0 to 15) numbers; the field order and widths of `GPConfig`, `GPTileReq`, `GPTileData`, `GPParamDef` and its per-type metadata, `GPParam` and its per-type data, `GPProcRun`, `GPProcReturn`, `GPProcInstall` and `GPProcUninstall`; the existence of the `gp_*_write` and `gimp_wire_*` entry points in libgimpbase. Patchy's declarations are its own (C++ types, its own names) in `src/plugins/gimp/`; no header text was copied. Notice: `NOTICE-THIRD-PARTY.md`.
- The headers say nothing about byte order or how strings and arrays are framed on the pipe; those come only from observed traffic (entries below).

### 2026-11-01: installed files on the test machine (GIMP 3.2.6, Windows installer)

- `lib/gimp/3.0/interpreters/*.interp` and `lib/gimp/3.0/environ/*.env`: read. Taken: the `name=program` lines, the binfmt-style extension lines (`:Python:E::py::python3:`, `,ScriptFu,E,,scm,,<program>,`), and `name=value` environment lines with `${gimp_installation_dir}` expansion. Their format is taken from the files as installed.
- The plug-in folder layout (`lib/gimp/3.0/plug-ins/<name>/<name>[.exe|.py]`, 132 folders) and the per-user folder `%APPDATA%\GIMP\3.2\plug-ins`; the Inno Setup uninstall key `GIMP-3_is1` with `InstallLocation` and `DisplayVersion`.

### 2026-11-01: observed traffic (GIMP 3.2.6, Windows)

- Tool: `scripts/dev/gimp/wire_tap.cpp` (Patchy's own, MIT), copied in place of a plug-in in a scratch plug-in folder named by a scratch `gimprc` (`plug-in-path`), with `GIMP3_DIRECTORY` pointing at a scratch profile, under `gimp-console-3.exe -i` batch runs. It logged its arguments, environment and inherited descriptors, started the real plug-in the same way and recorded both directions. Plug-ins logged: `blinds`, `despeckle`, `file-png`, `script-fu` (query, a non-interactive Blinds run over a selection, PDB error returns). Logs stayed in a scratch folder outside the repository.
- Taken: the command line (`-gimp 279 <fd> <fd> -query|-init|-run 0`) and that the descriptors are C runtime descriptors handed over through the inherited-descriptor table (`cbReserved2` 580: 64 entries); the environment GIMP sets for plug-ins (`GSETTINGS_SCHEMA_DIR`, `LANG`/`LANGUAGE`, `PYTHONIOENCODING`, the `environ` files, `PATH` replaced); big-endian numbers; string, array and color framing; the `CONFIG` field order on the wire (check colors before the flag bytes); the `CHOICE` and `GEGL_COLOR` definition framing; that a query run sends `PROC_INSTALL` and then calls `gimp-pdb-set-proc-image-types`, `-menu-label`, `-sensitivity-mask`, `gimp-pdb-add-proc-menu-path`, `-documentation`, `-attribution` and ends with `QUIT`; the tile read and write handshakes, 128 x 128 tiles numbered row by row, zero-filled shadow tiles; the PDB calls a run makes (drawable geometry and format, `gimp-drawable-mask-intersect`, `gimp-drawable-merge-shadow`, `gimp-drawable-update`, progress, context colors); that an error return is the status alone. The procedure names in these logs are GIMP's PDB names (interface facts).
- `scripts/dev/gimp/wire_dump.py` parses those logs with the grammar above; every log parsed without a gap. The same grammar is `src/plugins/gimp/gimp_wire_codec.cpp`, and the core tests decode and re-encode three logged messages byte for byte.
- Parameter records not present in any log (`PARASITE`, `ARRAY`, `BABL_FORMAT`, `COLOR_ARRAY`, `VALUE_ARRAY` values; `UNIT`, `DEFAULT`, `EXPORT_OPTIONS`, `CURVE` definitions) follow the header's field order with the framing observed for the others; `CURVE` values are refused outright. A decode error ends the run with a clear message, never a guess passed on.

### 2026-11-01: Patchy's test plug-in

- `test-fixtures/gimp-plugins/patchy-test/patchy-test.py`, MIT, written for this work from GIMP 3's public Python API (the `Gimp`, `GimpUi` and `Gegl` introspection modules as documented on developer.gimp.org). Checked in GIMP 3.2.6 first (batch run over a PNG with a selection), then used as Patchy's reference: the same input gives the same output.

### 2026-11-01: behavior checked against GIMP's output

- Method: the same PNG and selection run through GIMP 3.2.6 (`gimp-console-3.exe` Script-Fu batch calls of each procedure with its registered defaults) and through Patchy (`scripts/dev/gimp/sweep.js`), outputs compared pixel by pixel. No source was consulted; only the two programs' outputs.
- Taken: object arguments nobody set travel as no object (-1), not as the run's own layer (Depth Merge, Lighting Effects and Sample Colorize matched GIMP only that way); arrays without a definition kind default to empty; plug-ins crash when a color in `CONFIG` carries no ICC profile (found by replaying the logged `CONFIG` message and changing one field at a time), so Patchy sends an sRGB profile it builds with Little CMS; which bundled procedures need new layers, images, GIMP's clipboard, painting tools or resources (the GIMP functions they call, named in Patchy's error, are interface facts).
- Not taken: no GIMP data file (presets, gradients, brushes) is copied or shipped; plug-ins read their own data from the user's GIMP install at run time.

### 2026-11-01: macOS and Linux installs (GIMP 3.2.6: Homebrew cask, Flathub Flatpak)

- Installed files read, as on Windows: the layout of `GIMP.app/Contents` (`lib/gimp/3.0`, `share/gimp/3.0`, `MacOS` with `python3`), its `Info.plist` (`CFBundleShortVersionString`), the `.interp` and `.env` files; the Flatpak's `files/` tree, `share/metainfo/org.gimp.GIMP.metainfo.xml` (release versions) and the app's metadata as `flatpak info -m org.gimp.GIMP` prints it (sandbox permissions: `filesystems=host`, `/tmp`, the `org.gimp.GIMP.Plugin` extension point with `merge-dirs=plug-ins`).
- Code signatures as `codesign -d -vvvv` prints them (Apple's tool): GIMP.app's `python3` has a parent launch constraint naming `org.gimp.gimp` and GIMP's team; native plug-ins and the Script-Fu interpreter have none. The kill reason ("Launch Constraint Violation") is from macOS's crash report. Patchy does not work around the constraint.
- Flatpak behavior observed with throwaway shell commands in both sandboxes (`ls -l /proc/self/fd` under `flatpak run` and `flatpak-spawn --host --forward-fd`): descriptors 3 and 4 arrive unchanged; the options `--cwd`, `--env`, `--filesystem`, `--die-with-parent` from `flatpak run --help`.
- Third-party plug-ins used only as installed programs, never read: Fourier 0.4.5 and Resynthesizer 3.0.1 (Flathub extensions for GIMP 3). Results come from running them through Patchy.
- No protocol traffic was logged on these platforms; the Windows grammar decoded every run.

### 2026-11-01: Patchy's settings dialog

- Built only from what each procedure registers at query time (argument kinds, names, nicks, blurbs, ranges, defaults, choice nicks and labels; already logged above). No new source. The canvas preview runs the plug-in non-interactively with those arguments, as scripts do.

### 2026-11-02: GEGL helper (GEGL 0.4, babl 0.1, GLib 2.0 as installed with GIMP 3.2.6, Windows)

- Rule: [legal-licensing.md](legal-licensing.md#gegl-operations-through-a-helper) (Seth, November 2, 2026).
- Installed public headers read for declarations only: `gegl-init.h`, `gegl-operations-util.h`, `gegl-node.h`, `gegl-buffer.h`, `gegl-buffer-enums.h`, `gegl-types.h`, `gegl-paramspecs.h`, `gegl-color.h` (GEGL); `babl.h` (babl); `gobject/gparam.h`, `gparamspecs.h`, `genums.h`, `gvalue.h`, `gtype.h` (GLib). Taken: function signatures, the layouts of `GParamSpec`, `GParamSpecInt`, `GParamSpecUInt`, `GParamSpecFloat`, `GParamSpecDouble`, `GParamSpecEnum`, `GeglParamSpecInt`, `GeglParamSpecDouble`, `GeglParamSpecSeed`, `GEnumClass`, `GEnumValue`, `GValue`, `GeglRectangle`, the fundamental type numbers, `GEGL_BLIT_DEFAULT`, and the doc comment that `gegl_list_operations`' array is freed with `g_free` (the names are GEGL's).
- Published API documentation: GeglConfig's `application-license` property ("a list of additional licenses to allow for operations", valadoc.org Gegl-0.4 `Gegl.Config`), and GIMP's 2014 commit message setting it to "GPL3" (lists.gnome.org commits-list). The environment variable `GEGL_THREADS`.
- Run-time self-description: `gegl --list-all` and `gegl --info <op>` of GIMP's own `gegl.exe`, then the helper's own `--list` (operation keys and property specs).
- No GEGL or babl `.c` file or operation source was opened.

### 2026-11-02: protocol version history

- `GIMP_PROTOCOL_VERSION` read (the one constant only) from `libgimpbase/gimpprotocol.h` at tags `GIMP_2_10_0` (0x0016), `GIMP_2_10_38` (0x001A), `GIMP_2_99_2` and `GIMP_2_99_10` (0x010E), `GIMP_2_99_18` (0x0111), `GIMP_3_0_0_RC1` (0x0114), `GIMP_3_0_0` through `GIMP_3_0_6` and `GIMP_3_1_2`, `GIMP_3_1_4`, `GIMP_3_2_0_RC1` (0x0115), `GIMP_3_2_0_RC3` through `GIMP_3_2_6` (0x0117), and from master's `libgimpbase/gimpprotocol-private.h` (0x0119, November 2, 2026; the header was renamed private on September 18, 2026), plus the GitLab commit titles touching those files. Nothing else from them was read; recorded in [gimp-plugins.md](gimp-plugins.md#protocol).

### 2026-11-02: Patchy's own GEGL

- Built unmodified from gimp.org's `gegl-0.4.72.tar.xz` and `babl-0.1.128.tar.xz` (SHA-256 checked against gimp.org's `SHA256SUMS`) with Meson. Read: the option names in each tarball's `meson_options.txt` (build switches only), the license files it ships, and the build's own output. No `.c` file or operation source was opened; the legal review in [gegl-filters.md](gegl-filters.md#legal-review) works from each operation's own description.
- macOS also builds GLib 2.84.4 and json-glib 1.10.8 (download.gnome.org, SHA-256 checked against GNOME's `.sha256sum` files) and gettext 0.26's runtime (ftp.gnu.org; GNU publishes a PGP signature, not a checksum, so the pinned SHA-256 is that of the copy first fetched on November 2, 2026), unmodified, configured by their documented options only.

### 2026-11-02: GPL-3 module, removed operations, source archive

- GEGL's operation build lists (`operations/common/meson.build`, `operations/common-cxx/meson.build`, `operations/common-gpl3+/meson.build`) are touched only by `scripts/dev/gegl/common.sh`, which counts and deletes the one line naming each removed operation's source file (`'median-blur.c'` and the others in `PATCHY_GEGL_DROPPED`) without printing the files; the operation sources themselves were never opened. Which operation lives in which module came from loading each module alone through the helper (`--list`).
- That GEGL's Drop Shadow builds Median Blur and Translate internally came from the helper's run-time warnings ("Failed to set operation type"), running every offered operation once.
- GLib 2.84.4's `subprojects/pcre2.wrap` (GLib, LGPL; not covered by the never-read rule) was read for the PCRE2 archive and wrap patch URLs and hashes; MSYS2's `repo.msys2.org/mingw/sources/` listing for source package names and retention.
