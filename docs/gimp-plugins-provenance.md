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
