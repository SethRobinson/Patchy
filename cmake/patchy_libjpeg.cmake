# Pinned, private JPEG decoder for the samples Qt's JPEG plug-in cannot read: libjpeg-turbo
# 3.1.4.1's decompression side (8, 12 and 16-bit builds of the per-precision sources, as
# upstream compiles them through src/wrapper) as one static archive, built from the
# vendored sources in src/formats/libjpeg-turbo instead of upstream's CMake, so no
# compressor, tools, TurboJPEG API or SIMD code come along. The three configuration
# headers upstream generates at configure time are written here from the same facts.
# Every global is renamed through patchy_libjpeg_symbols.h (scripts/dev/
# generate-libjpeg-symbols.py), so the JPEG library Qt's plug-in uses, shared or linked
# statically in the browser build, never meets this copy. 8-bit JPEGs still open through
# Qt; this copy runs only for 12-bit and lossless files (docs/file-formats.md,
# "12-bit and lossless JPEG").
function(patchy_add_libjpeg)
  set(root "${PROJECT_SOURCE_DIR}/src/formats/libjpeg-turbo")
  set(gen "${CMAKE_CURRENT_BINARY_DIR}/libjpeg-generated")
  file(MAKE_DIRECTORY "${gen}")

  set(VERSION 3.1.4.1)
  set(LIBJPEG_TURBO_VERSION_NUMBER 3001004)
  set(JPEG_LIB_VERSION 62)
  set(COPYRIGHT_YEAR "1991-2026")
  set(BUILD "patchy")
  set(CMAKE_PROJECT_NAME libjpeg-turbo)
  set(C_ARITH_CODING_SUPPORTED 0)
  set(D_ARITH_CODING_SUPPORTED 1)
  set(WITH_SIMD 0)
  set(RIGHT_SHIFT_IS_UNSIGNED 0)
  set(SIZE_T ${CMAKE_SIZEOF_VOID_P})
  if(MSVC)
    set(HIDDEN "")
    set(INLINE "__forceinline")
    set(THREAD_LOCAL "__declspec(thread)")
    set(HAVE_BUILTIN_CTZL 0)
    set(HAVE_INTRIN_H 1)
  else()
    set(HIDDEN "__attribute__((visibility(\"hidden\")))")
    set(INLINE "inline __attribute__((always_inline))")
    set(THREAD_LOCAL "__thread")
    # unsigned long and size_t have the same width on every non-Windows target Patchy builds.
    set(HAVE_BUILTIN_CTZL 1)
    set(HAVE_INTRIN_H 0)
  endif()
  configure_file("${root}/src/jconfig.h.in" "${gen}/jconfig.h")
  configure_file("${root}/src/jconfigint.h.in" "${gen}/jconfigint.h")
  configure_file("${root}/src/jversion.h.in" "${gen}/jversion.h")

  set(sources)
  foreach(name jaricom jcomapi jdapimin jdarith jdatasrc jdhuff jdicc jdinput jdlhuff jdmarker jdmaster
               jdphuff jerror jmemmgr jmemnobs)
    list(APPEND sources "${root}/src/${name}.c")
  endforeach()
  foreach(name jdapistd jdcolor jddiffct jdlossls jdmainct jdpostct jdsample jutils)
    foreach(bits 8 12 16)
      list(APPEND sources "${root}/src/wrapper/${name}-${bits}.c")
    endforeach()
  endforeach()
  foreach(name jdcoefct jddctmgr jdmerge jidctflt jidctfst jidctint jidctred jquant1 jquant2)
    foreach(bits 8 12)
      list(APPEND sources "${root}/src/wrapper/${name}-${bits}.c")
    endforeach()
  endforeach()

  add_library(patchy_libjpeg STATIC ${sources})
  set_target_properties(patchy_libjpeg PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON
                                                  C_VISIBILITY_PRESET hidden)
  target_include_directories(patchy_libjpeg PUBLIC "${root}" "${root}/src" "${gen}")
  if(MSVC)
    target_compile_definitions(patchy_libjpeg PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(patchy_libjpeg PRIVATE /w "/FI${root}/patchy_libjpeg_symbols.h")
  else()
    target_compile_options(patchy_libjpeg PRIVATE -w "SHELL:-include ${root}/patchy_libjpeg_symbols.h")
  endif()
endfunction()

patchy_add_libjpeg()
