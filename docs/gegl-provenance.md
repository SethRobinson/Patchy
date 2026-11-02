# GEGL filters: provenance log

Evidence that Patchy's GEGL helper and bundle were built from interface facts only. The binding rule is [legal-licensing.md](legal-licensing.md#gegl-operations-through-a-helper); the design is [gegl-filters.md](gegl-filters.md). Add a dated entry for every new source, tool or decision, in the same change that uses it. Each entry names the source, its version and licence, and the facts taken from it. (The removed GIMP 3 plug-in bridge kept its own log, `docs/gimp-plugins-provenance.md`, in the branch history.)

## Never read

None of these were opened, searched or excerpted at any point, by a person or a tool working on Patchy: GEGL's or babl's `.c` files or operation sources, GIMP's source (its `app/` tree, libgimp and siblings, `plug-ins/`), any GIMP plug-in's source, GLib's or GTK's source. The helper is written from the facts below.

## Log

### 2026-11-02: GEGL helper (GEGL 0.4, babl 0.1, GLib 2.0 as installed with GIMP 3.2.6, Windows)

- Rule: [legal-licensing.md](legal-licensing.md#gegl-operations-through-a-helper) (Seth, November 2, 2026).
- Installed public headers read for declarations only: `gegl-init.h`, `gegl-operations-util.h`, `gegl-node.h`, `gegl-buffer.h`, `gegl-buffer-enums.h`, `gegl-types.h`, `gegl-paramspecs.h`, `gegl-color.h` (GEGL); `babl.h` (babl); `gobject/gparam.h`, `gparamspecs.h`, `genums.h`, `gvalue.h`, `gtype.h` (GLib). Taken: function signatures, the layouts of `GParamSpec`, `GParamSpecInt`, `GParamSpecUInt`, `GParamSpecFloat`, `GParamSpecDouble`, `GParamSpecEnum`, `GeglParamSpecInt`, `GeglParamSpecDouble`, `GeglParamSpecSeed`, `GEnumClass`, `GEnumValue`, `GValue`, `GeglRectangle`, the fundamental type numbers, `GEGL_BLIT_DEFAULT`, and the doc comment that `gegl_list_operations`' array is freed with `g_free` (the names are GEGL's).
- Published API documentation: GeglConfig's `application-license` property ("a list of additional licenses to allow for operations", valadoc.org Gegl-0.4 `Gegl.Config`), and GIMP's 2014 commit message setting it to "GPL3" (lists.gnome.org commits-list). The environment variable `GEGL_THREADS`.
- Run-time self-description: `gegl --list-all` and `gegl --info <op>` of GIMP's own `gegl.exe`, then the helper's own `--list` (operation keys and property specs).
- No GEGL or babl `.c` file or operation source was opened.

### 2026-11-02: Patchy's own GEGL

- Built unmodified from gimp.org's `gegl-0.4.72.tar.xz` and `babl-0.1.128.tar.xz` (SHA-256 checked against gimp.org's `SHA256SUMS`) with Meson. Read: the option names in each tarball's `meson_options.txt` (build switches only), the license files it ships, and the build's own output. No `.c` file or operation source was opened; the legal review in [gegl-filters.md](gegl-filters.md#legal-review) works from each operation's own description.
- macOS also builds GLib 2.84.4 and json-glib 1.10.8 (download.gnome.org, SHA-256 checked against GNOME's `.sha256sum` files) and gettext 0.26's runtime (ftp.gnu.org; GNU publishes a PGP signature, not a checksum, so the pinned SHA-256 is that of the copy first fetched on November 2, 2026), unmodified, configured by their documented options only.

### 2026-11-02: GPL-3 module, removed operations, source archive

- GEGL's operation build lists (`operations/common/meson.build`, `operations/common-cxx/meson.build`, `operations/common-gpl3+/meson.build`) are touched only by `scripts/dev/gegl/common.sh`, which counts and deletes the one line naming each removed operation's source file (`'median-blur.c'` and the others in `PATCHY_GEGL_DROPPED`) without printing the files; the operation sources themselves were never opened. Which operation lives in which module came from loading each module alone through the helper (`--list`).
- That GEGL's Drop Shadow builds Median Blur and Translate internally came from the helper's run-time warnings ("Failed to set operation type"), running every offered operation once.
- GLib 2.84.4's `subprojects/pcre2.wrap` (GLib, LGPL; not covered by the never-read rule) was read for the PCRE2 archive and wrap patch URLs and hashes; MSYS2's `repo.msys2.org/mingw/sources/` listing for source package names and retention.
