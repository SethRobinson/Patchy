# Vendored libheif

libheif 1.23.1 (https://github.com/strukturag/libheif), license in `COPYING`, recorded in
NOTICE-THIRD-PARTY.md. Patchy builds it only for the WebAssembly app (`CMakeLists.txt`,
"libheif (web build only)"), with `BUILD_TESTING`, `WITH_EXAMPLES`, `WITH_FUZZERS` and
`WITH_GDK_PIXBUF` off and `EXCLUDE_FROM_ALL`; native builds decode HEIF through the
platform codecs ([docs/heif.md](../../../docs/heif.md)).

## What is here

- `libheif/`, `heifio/`, `gnome/`, `cmake/`, `extra/`, `third-party/`: upstream, unchanged
  (`heifio` and `gnome` are added unconditionally by upstream's CMakeLists.txt).
- Removed on October 25, 2026 because nothing compiles them: `examples/`, `fuzzing/`,
  `tests/` (with `catch_amalgamated.cpp` and its data), `go/`, `scripts/`, `logos/` and
  `gdk-pixbuf/` (about 345 files, 4.3 MB). Each sat behind an option the build sets off,
  or was never referenced; they also sat in the build-stamp glob over `src/*`.
- Nothing under this directory is edited for warnings: the target keeps upstream's
  warning policy ([docs/code-organization.md](../../../docs/code-organization.md)).
