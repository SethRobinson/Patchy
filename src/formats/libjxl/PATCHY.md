# Vendored libjxl

libjxl 0.12.0 with the Highway, Brotli and skcms revisions that release pins (URLs and
SHA-256 values in NOTICE-THIRD-PARTY.md). Patchy builds one private static archive,
`patchy_jxl`, from upstream's own source lists (`lib/jxl_lists.cmake`) in
`cmake/patchy_jxl.cmake`; upstream's CMake files are not used. Read docs/jxl.md and the
JPEG XL conditions in docs/legal-licensing.md before changing anything here.

## What is here

- `LICENSE`, `PATENTS`, `AUTHORS`, `CONTRIBUTORS`: upstream, unchanged.
- `lib/jxl`, `lib/threads`, `lib/include`, `lib/jxl_lists.cmake`: the library sources,
  unchanged. Removed: every `*_test.cc`, `*_gbench.cc` and `*_testonly.h`, the files of
  upstream's test library (`JPEGXL_INTERNAL_TESTLIB_FILES`), Bazel `BUILD` files,
  pkg-config templates and linker symbol lists.
- `third_party/highway`: `hwy/` without `tests/`, `examples/` and `contrib/`, plus both
  licenses (Patchy uses the BSD-3-Clause option).
- `third_party/brotli`: `c/common`, `c/dec`, `c/enc`, `c/include` and the license.
- `third_party/skcms`: `skcms.cc`, `skcms.h`, `src/` and the license. Only the baseline
  transform is compiled (`SKCMS_DISABLE_HSW`, `SKCMS_DISABLE_SKX`).

Not vendored: tools, tests, test data, benchmarks, plug-ins, docs, the JNI and fuzzing
code, and the other `third_party` entries (libpng, zlib, sjpeg, libjpeg-turbo, lcms,
googletest), none of which the library needs.

## Source changes

None. The legal conditions forbid algorithmic changes to the codec. Global C symbols are
renamed at compile time instead: `cmake/patchy_jxl.cmake` force-includes
`../jxl_symbols.hpp`, which `scripts/dev/generate-jxl-symbols.py` generates from this tree
(every `Jxl*`, `Brotli*`, `kBrotli*` and `skcms_*` name gets a `patchy_jxl_` prefix, plus
the script's audited extra list: Brotli tables without the prefix and the histogram functions
its macros paste together).
Patchy code that calls libjxl includes the same header before any libjxl header.

## Upgrading

1. Replace the tree from the new release and the dependency commits it pins, removing the
   same files as above. Keep this file.
2. Update the version numbers in `cmake/patchy_jxl.cmake` (they feed `jxl/version.h`) and
   NOTICE-THIRD-PARTY.md (URLs, SHA-256 values, license texts).
3. Rerun `python scripts/dev/generate-jxl-symbols.py`.
4. Build the desktop and wasm presets and audit the archive's defined globals
   (`dumpbin /linkermember:1 patchy_jxl.lib`, since LTCG objects hide `/symbols`, or
   `llvm-nm --defined-only`): nothing outside the
   `jxl::`, `hwy::` and `jpegxl::` namespaces may remain without the prefix, apart from CRT and
   standard library inline helpers. Add any newcomer to the script's `extra` list.
5. A release whose `PATENTS` file changed, or one that exposes a new encoder feature, needs
   a new legal review before it ships (docs/legal-licensing.md, condition 5).
6. Re-pin `jxl_writer_bytes_are_stable` only after checking the new output decodes the
   same pixels.
