# Pinned, private JPEG XL codec: libjxl 0.12.0 with its bundled Highway, Brotli and skcms.
# One static archive, built from upstream's own source lists (lib/jxl_lists.cmake) instead of
# upstream's CMake, so no tools, plug-ins, install rules or system codec discovery come along
# (docs/legal-licensing.md, JPEG XL conditions 1 and 2). Upstream sources are unmodified;
# src/formats/libjxl/PATCHY.md records what was trimmed and how to upgrade.
function(patchy_add_jxl)
  set(root "${PROJECT_SOURCE_DIR}/src/formats/libjxl")
  set(gen "${CMAKE_CURRENT_BINARY_DIR}/libjxl-generated")
  include("${root}/lib/jxl_lists.cmake")

  set(JPEGXL_MAJOR_VERSION 0)
  set(JPEGXL_MINOR_VERSION 12)
  set(JPEGXL_PATCH_VERSION 0)
  configure_file("${root}/lib/jxl/version.h.in" "${gen}/jxl/version.h")
  # Static build: the export macros upstream generates with GenerateExportHeader are empty.
  foreach(prefix JXL JXL_CMS JXL_THREADS)
    string(TOLOWER "${prefix}" lower)
    file(CONFIGURE OUTPUT "${gen}/jxl/${lower}_export.h" CONTENT
"#ifndef ${prefix}_EXPORT_H
#define ${prefix}_EXPORT_H
#define ${prefix}_EXPORT
#define ${prefix}_NO_EXPORT
#define ${prefix}_DEPRECATED
#define ${prefix}_DEPRECATED_EXPORT
#define ${prefix}_DEPRECATED_NO_EXPORT
#endif
" @ONLY)
  endforeach()

  set(sources)
  foreach(item IN LISTS JPEGXL_INTERNAL_DEC_SOURCES JPEGXL_INTERNAL_DEC_BOX_SOURCES
      JPEGXL_INTERNAL_DEC_JPEG_SOURCES JPEGXL_INTERNAL_ENC_SOURCES
      JPEGXL_INTERNAL_CMS_SOURCES JPEGXL_INTERNAL_THREADS_SOURCES)
    if(item MATCHES "\\.cc$")
      list(APPEND sources "${root}/lib/${item}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES sources)
  foreach(item abort aligned_allocator nanobenchmark per_target print targets timer)
    list(APPEND sources "${root}/third_party/highway/hwy/${item}.cc")
  endforeach()
  file(GLOB brotli_sources
    "${root}/third_party/brotli/c/common/*.c"
    "${root}/third_party/brotli/c/dec/*.c"
    "${root}/third_party/brotli/c/enc/*.c")
  list(APPEND sources ${brotli_sources}
    "${root}/third_party/skcms/skcms.cc"
    "${root}/third_party/skcms/src/skcms_TransformBaseline.cc")

  add_library(patchy_jxl STATIC ${sources})
  # Upstream builds as C++17; vendored code keeps its own standard (as patchy_libraw does).
  set_target_properties(patchy_jxl PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
  target_include_directories(patchy_jxl
    PUBLIC "${gen}" "${root}/lib/include"
    PRIVATE "${root}" "${root}/third_party/highway" "${root}/third_party/brotli/c/include"
      "${root}/third_party/skcms")
  target_compile_definitions(patchy_jxl PRIVATE
    JXL_INTERNAL_LIBRARY_BUILD
    JPEGXL_ENABLE_TRANSCODE_JPEG=1
    JPEGXL_ENABLE_BOXES=1
    JPEGXL_ENABLE_SKCMS=1
    # skcms's AVX2/AVX-512 kernels need per-file ISA flags; the baseline path is enough for
    # the occasional ICC conversion.
    SKCMS_DISABLE_HSW
    SKCMS_DISABLE_SKX
    # Upstream's default Highway target set (its JPEGXL_HWY_TARGETS_OFF_BY_DEFAULT).
    "HWY_DISABLED_TARGETS=(HWY_AVX3|HWY_AVX3_SPR|HWY_AVX3_ZEN4|HWY_RVV|HWY_SSSE3|HWY_SVE|HWY_SVE2|HWY_SVE_256|HWY_SVE2_128)"
    FJXL_ENABLE_AVX512=0)
  # Global C/C++ entry points get a patchy_jxl_ prefix so a system libjxl, Brotli or skcms
  # loaded into the process (KDE image plug-ins in the Flatpak runtime, Qt's own Brotli)
  # never binds to ours or the reverse. C++ internals already live in jxl:: and hwy::.
  if(MSVC)
    target_compile_definitions(patchy_jxl PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(patchy_jxl PRIVATE /w /bigobj
      "/FI${PROJECT_SOURCE_DIR}/src/formats/jxl_symbols.hpp")
  else()
    target_compile_options(patchy_jxl PRIVATE -w
      "SHELL:-include ${PROJECT_SOURCE_DIR}/src/formats/jxl_symbols.hpp")
  endif()
  if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(patchy_jxl PRIVATE m)
  endif()
  find_package(Threads REQUIRED)
  target_link_libraries(patchy_jxl PUBLIC Threads::Threads)
endfunction()
patchy_add_jxl()
