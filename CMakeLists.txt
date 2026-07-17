cmake_minimum_required(VERSION 3.26)

project(
  Patchy
  VERSION 0.20
  DESCRIPTION "A native, PSD-oriented image editor foundation"
  LANGUAGES CXX
)

if(APPLE)
  # Objective-C++ translation units (per-OS *_mac.mm files, e.g. dialog_utils_mac.mm).
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 20)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

# The vendored miniz (src/formats/miniz/miniz.c, zlib inflate/deflate for the Aseprite
# format) and Little CMS (src/color/lcms2/, ICC color management for CMYK PSD import)
# are plain C.
enable_language(C)

include(CTest)
include(GNUInstallDirs)

option(PATCHY_BUILD_APP "Build the Qt desktop application when Qt 6 is available" ON)
option(PATCHY_BUILD_TESTS "Build unit tests" ON)
option(PATCHY_ENABLE_STRICT_WARNINGS "Enable strict compiler warnings" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(PATCHY_BUNDLED_FONTS_DIR "${PROJECT_SOURCE_DIR}/third_party/fonts")

function(patchy_configure_target target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  target_include_directories(${target} PUBLIC ${PROJECT_SOURCE_DIR}/src)

  if(PATCHY_ENABLE_STRICT_WARNINGS)
    if(MSVC)
      target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
      target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()
endfunction()

add_library(patchy_core
  src/core/gradient_presets.cpp
  src/core/adjustment_layer.cpp
  src/core/blend_math.cpp
  src/core/brush_dynamics.cpp
  src/core/brush_tip.cpp
  src/core/contour_presets.cpp
  src/core/document.cpp
  src/core/document_channel.cpp
  src/core/document_geometry.cpp
  src/core/document_path.cpp
  src/core/layer.cpp
  src/core/layer_metadata.cpp
  src/core/layer_render_utils.cpp
  src/core/layer_tree.cpp
  src/core/liquify.cpp
  src/core/magnetic_lasso.cpp
  src/core/palette.cpp
  src/core/palette_presets.cpp
  src/core/path_fit.cpp
  src/core/pattern_presets.cpp
  src/core/pattern_resource.cpp
  src/core/pixel_buffer.cpp
  src/core/pixel_tools.cpp
  src/core/quick_select.cpp
  src/core/rect_utils.cpp
  src/core/smart_filter_effects.cpp
  src/core/smart_object.cpp
  src/core/style_contour.cpp
  src/core/style_presets.cpp
  src/core/text_warp.cpp
  src/core/vector_live_shapes.cpp
  src/core/vector_raster.cpp
  src/core/vector_shape.cpp
  src/core/warp_mesh.hpp
  src/core/warp_mesh.cpp
)
patchy_configure_target(patchy_core)

add_library(patchy_render
  src/render/compositor.cpp
  src/render/tile_cache.cpp
)
# flatten_rgb8 composites large documents in parallel strips (std::async).
find_package(Threads REQUIRED)
target_link_libraries(patchy_render PUBLIC patchy_core Threads::Threads)
patchy_configure_target(patchy_render)

# Vendored Little CMS 2.17 core (MIT), used to convert CMYK PSDs through their embedded
# ICC profile on import. Core library ONLY: the optional lcms2 speed plugins (fast_float,
# threaded) are GPL3-or-commercial and must never be vendored. A separate target skipped
# by patchy_configure_target keeps the strict-warning flags off the vendored C sources.
add_library(patchy_lcms2 STATIC
  src/color/lcms2/cmsalpha.c
  src/color/lcms2/cmscam02.c
  src/color/lcms2/cmscgats.c
  src/color/lcms2/cmscnvrt.c
  src/color/lcms2/cmserr.c
  src/color/lcms2/cmsgamma.c
  src/color/lcms2/cmsgmt.c
  src/color/lcms2/cmshalf.c
  src/color/lcms2/cmsintrp.c
  src/color/lcms2/cmsio0.c
  src/color/lcms2/cmsio1.c
  src/color/lcms2/cmslut.c
  src/color/lcms2/cmsmd5.c
  src/color/lcms2/cmsmtrx.c
  src/color/lcms2/cmsnamed.c
  src/color/lcms2/cmsopt.c
  src/color/lcms2/cmspack.c
  src/color/lcms2/cmspcs.c
  src/color/lcms2/cmsplugin.c
  src/color/lcms2/cmsps2.c
  src/color/lcms2/cmssamp.c
  src/color/lcms2/cmssm.c
  src/color/lcms2/cmstypes.c
  src/color/lcms2/cmsvirt.c
  src/color/lcms2/cmswtpnt.c
  src/color/lcms2/cmsxform.c
)
target_include_directories(patchy_lcms2 PUBLIC ${PROJECT_SOURCE_DIR}/src/color/lcms2)
if(MSVC)
  target_compile_definitions(patchy_lcms2 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(patchy_lcms2 PUBLIC m)
endif()

# Vendored LibRaw 0.22.1 (dual LGPL-2.1/CDDL-1.0; Patchy elects CDDL-1.0 — see
# NOTICE-THIRD-PARTY.md), the camera raw decoder + dcraw-style develop pipeline behind
# formats/raw_document_io. Stock tarball contents only: the separate LibRaw "demosaic pack"
# repositories are GPL and must never be vendored (DCB/DHT/AAHD here come from the stock
# tarball and are BSD/LGPL/CDDL-clean). Built WITHOUT jpeg/zlib/lcms (USE_* undefined), so
# lossy- and deflate-compressed DNGs report clear errors instead of adding dependencies, and
# WITHOUT LIBRAW_NOTHREADS (per-instance decoder state; instances may be used on different
# threads concurrently, which the raw develop preview relies on). A separate target skipped
# by patchy_configure_target keeps the strict-warning flags off the vendored sources.
add_library(patchy_libraw STATIC
  src/formats/libraw/src/decoders/canon_600.cpp
  src/formats/libraw/src/decoders/crx.cpp
  src/formats/libraw/src/decoders/decoders_dcraw.cpp
  src/formats/libraw/src/decoders/decoders_libraw.cpp
  src/formats/libraw/src/decoders/decoders_libraw_dcrdefs.cpp
  src/formats/libraw/src/decoders/dng.cpp
  src/formats/libraw/src/decoders/fp_dng.cpp
  src/formats/libraw/src/decoders/fuji_compressed.cpp
  src/formats/libraw/src/decoders/generic.cpp
  src/formats/libraw/src/decoders/kodak_decoders.cpp
  src/formats/libraw/src/decoders/load_mfbacks.cpp
  src/formats/libraw/src/decoders/olympus14.cpp
  src/formats/libraw/src/decoders/pana8.cpp
  src/formats/libraw/src/decoders/smal.cpp
  src/formats/libraw/src/decoders/sonycc.cpp
  src/formats/libraw/src/decoders/unpack.cpp
  src/formats/libraw/src/decoders/unpack_thumb.cpp
  src/formats/libraw/src/decompressors/losslessjpeg.cpp
  src/formats/libraw/src/demosaic/aahd_demosaic.cpp
  src/formats/libraw/src/demosaic/ahd_demosaic.cpp
  src/formats/libraw/src/demosaic/dcb_demosaic.cpp
  src/formats/libraw/src/demosaic/dht_demosaic.cpp
  src/formats/libraw/src/demosaic/misc_demosaic.cpp
  src/formats/libraw/src/demosaic/xtrans_demosaic.cpp
  src/formats/libraw/src/integration/dngsdk_glue.cpp
  src/formats/libraw/src/integration/rawspeed_glue.cpp
  src/formats/libraw/src/libraw_c_api.cpp
  src/formats/libraw/src/libraw_datastream.cpp
  src/formats/libraw/src/metadata/adobepano.cpp
  src/formats/libraw/src/metadata/canon.cpp
  src/formats/libraw/src/metadata/ciff.cpp
  src/formats/libraw/src/metadata/cr3_parser.cpp
  src/formats/libraw/src/metadata/epson.cpp
  src/formats/libraw/src/metadata/exif_gps.cpp
  src/formats/libraw/src/metadata/fuji.cpp
  src/formats/libraw/src/metadata/hasselblad_model.cpp
  src/formats/libraw/src/metadata/identify.cpp
  src/formats/libraw/src/metadata/identify_tools.cpp
  src/formats/libraw/src/metadata/kodak.cpp
  src/formats/libraw/src/metadata/leica.cpp
  src/formats/libraw/src/metadata/makernotes.cpp
  src/formats/libraw/src/metadata/mediumformat.cpp
  src/formats/libraw/src/metadata/minolta.cpp
  src/formats/libraw/src/metadata/misc_parsers.cpp
  src/formats/libraw/src/metadata/nikon.cpp
  src/formats/libraw/src/metadata/normalize_model.cpp
  src/formats/libraw/src/metadata/olympus.cpp
  src/formats/libraw/src/metadata/p1.cpp
  src/formats/libraw/src/metadata/pentax.cpp
  src/formats/libraw/src/metadata/samsung.cpp
  src/formats/libraw/src/metadata/sony.cpp
  src/formats/libraw/src/metadata/tiff.cpp
  src/formats/libraw/src/postprocessing/aspect_ratio.cpp
  src/formats/libraw/src/postprocessing/dcraw_process.cpp
  src/formats/libraw/src/postprocessing/mem_image.cpp
  src/formats/libraw/src/postprocessing/postprocessing_aux.cpp
  src/formats/libraw/src/postprocessing/postprocessing_utils.cpp
  src/formats/libraw/src/postprocessing/postprocessing_utils_dcrdefs.cpp
  src/formats/libraw/src/preprocessing/ext_preprocess.cpp
  src/formats/libraw/src/preprocessing/raw2image.cpp
  src/formats/libraw/src/preprocessing/subtract_black.cpp
  src/formats/libraw/src/tables/cameralist.cpp
  src/formats/libraw/src/tables/colorconst.cpp
  src/formats/libraw/src/tables/colordata.cpp
  src/formats/libraw/src/tables/wblists.cpp
  src/formats/libraw/src/utils/curves.cpp
  src/formats/libraw/src/utils/decoder_info.cpp
  src/formats/libraw/src/utils/init_close_utils.cpp
  src/formats/libraw/src/utils/open.cpp
  src/formats/libraw/src/utils/phaseone_processing.cpp
  src/formats/libraw/src/utils/read_utils.cpp
  src/formats/libraw/src/utils/thumb_utils.cpp
  src/formats/libraw/src/utils/utils_dcraw.cpp
  src/formats/libraw/src/utils/utils_libraw.cpp
  src/formats/libraw/src/write/apply_profile.cpp
  src/formats/libraw/src/write/file_write.cpp
  src/formats/libraw/src/write/tiff_writer.cpp
  src/formats/libraw/src/x3f/x3f_parse_process.cpp
  src/formats/libraw/src/x3f/x3f_utils_patched.cpp
)
target_include_directories(patchy_libraw PUBLIC ${PROJECT_SOURCE_DIR}/src/formats/libraw)
# LibRaw's own code predates C++20; pin the standard it is developed against.
set_target_properties(patchy_libraw PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if(MSVC)
  target_compile_definitions(patchy_libraw PRIVATE _CRT_SECURE_NO_WARNINGS WIN32)
  target_compile_options(patchy_libraw PRIVATE /w)
else()
  target_compile_options(patchy_libraw PRIVATE -w)
endif()
if(WIN32)
  # Static build: without LIBRAW_NODLL the headers declare every entry point dllimport.
  # PUBLIC because consumers include libraw/libraw.h through this target too.
  target_compile_definitions(patchy_libraw PUBLIC LIBRAW_NODLL)
  # ntohs/htons in the datastream and metadata parsers.
  target_link_libraries(patchy_libraw PUBLIC ws2_32)
endif()

add_library(patchy_psd
  src/formats/acv_curves_io.cpp
  src/psd/abr_reader.cpp
  src/psd/asl_io.cpp
  src/psd/grd_io.cpp
  src/psd/pat_reader.cpp
  src/psd/psd_adjustments.cpp
  src/psd/psd_binary.cpp
  src/psd/psd_channel_data.cpp
  src/psd/psd_descriptor.cpp
  src/psd/psd_document_io.cpp
  src/psd/psd_filter_effects.cpp
  src/psd/psd_image_resources.cpp
  src/psd/psd_io_common.cpp
  src/psd/psd_io_internal.hpp
  src/psd/psd_layer_records.cpp
  src/psd/psd_layer_styles.cpp
  src/psd/psd_patterns.cpp
  src/psd/psd_smart_objects.cpp
  src/psd/psd_text_read.cpp
  src/psd/psd_text_write.cpp
  src/psd/psd_vector.cpp
)
target_link_libraries(patchy_psd PUBLIC patchy_core patchy_render patchy_color)
if(WIN32)
  target_link_libraries(patchy_psd PUBLIC dwrite gdi32)
endif()
patchy_configure_target(patchy_psd)

add_library(patchy_plugins
  src/plugins/legacy_photoshop_adapter.cpp
  src/plugins/plugin_host.cpp
)
target_link_libraries(patchy_plugins PUBLIC patchy_core)
patchy_configure_target(patchy_plugins)

add_library(patchy_filters
  src/filters/builtin_filters.cpp
  src/filters/filter_engine.cpp
  src/filters/filter_registry.cpp
  src/filters/filter_support.hpp
  src/filters/rgba_filter_staging.hpp
  src/filters/smart_filter_recipe_mapping.cpp
  src/filters/smart_filter_renderer.cpp
)
target_link_libraries(patchy_filters PUBLIC patchy_core)
patchy_configure_target(patchy_filters)

add_library(patchy_formats
  src/formats/aseprite_document_io.cpp
  src/formats/bmp_document_io.cpp
  src/formats/document_flatten.cpp
  src/formats/format_file_io.cpp
  src/formats/format_registry.cpp
  src/formats/gif_document_io.cpp
  src/formats/heif_document_io.cpp
  src/formats/ico_document_io.cpp
  src/formats/image_density_probe.cpp
  src/formats/ilbm_document_io.cpp
  src/formats/palette_io.cpp
  src/formats/pcx_document_io.cpp
  src/formats/raw_document_io.cpp
  src/formats/raw_tone.cpp
  src/formats/raw_white_balance.cpp
  src/formats/svg_document_read.cpp
  src/formats/svg_document_write.cpp
  src/formats/svg_xml.cpp
  src/formats/tga_document_io.cpp
  src/formats/miniz/miniz.c
)
# patchy_libraw stays PRIVATE: only raw_document_io.cpp touches LibRaw types; the public
# raw_document_io.hpp is LibRaw-free like the rest of the formats API.
target_link_libraries(patchy_formats PUBLIC patchy_core patchy_render patchy_psd PRIVATE patchy_libraw)
if(WIN32)
  # HEIF/HEIC decodes through Windows Imaging Component (the Microsoft Store HEIF/HEVC
  # codec packages carry the HEVC patent licensing -- Patchy must never vendor an HEVC
  # decoder). macOS/Linux decode HEIC via Qt image plugins instead, so they compile only
  # the stub in heif_document_io.cpp.
  target_sources(patchy_formats PRIVATE src/formats/heif_document_io_win.cpp)
  target_link_libraries(patchy_formats PRIVATE windowscodecs ole32)
endif()
patchy_configure_target(patchy_formats)

add_library(patchy_color
  src/color/color_management.cpp
)
target_link_libraries(patchy_color PUBLIC patchy_core PRIVATE patchy_lcms2)
patchy_configure_target(patchy_color)

if(PATCHY_BUILD_APP)
  find_package(Qt6 QUIET COMPONENTS Widgets Svg PrintSupport Network LinguistTools Test)
  if(Qt6_FOUND)
    if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
      # Qt 6.8's FindWrapOpenGL.cmake links the long-dead AGL framework: the OS still
      # ships /System/Library/Frameworks/AGL.framework (so find_library finds it) but the
      # Xcode 26 SDK removed it (so the linker cannot), failing every link. The entry can
      # therefore be either an absolute framework path or a literal "-framework AGL"
      # fallback; OpenGL.framework is all Qt actually needs, so drop both forms.
      get_target_property(PATCHY_WRAP_OPENGL_LIBS WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
      if(PATCHY_WRAP_OPENGL_LIBS)
        list(FILTER PATCHY_WRAP_OPENGL_LIBS EXCLUDE REGEX "AGL")
        set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES
          INTERFACE_LINK_LIBRARIES "${PATCHY_WRAP_OPENGL_LIBS}")
      endif()
    endif()
    qt_standard_project_setup()
    set(CMAKE_AUTORCC ON)
    set(PATCHY_TRANSLATION_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/translations")
    set(PATCHY_TS_FILES
      "${PROJECT_SOURCE_DIR}/translations/patchy_ja.ts"
    )
    set_source_files_properties(${PATCHY_TS_FILES}
      PROPERTIES OUTPUT_LOCATION "${PATCHY_TRANSLATION_OUTPUT_DIR}"
    )
    qt_add_lrelease(patchy_lrelease
      TS_FILES ${PATCHY_TS_FILES}
      QM_FILES_OUTPUT_VARIABLE PATCHY_QM_FILES
    )
    add_custom_target(patchy_translations DEPENDS ${PATCHY_QM_FILES})
    # Qt's own install variables handle every layout (aqt/official: <prefix>/plugins;
    # distro/Flatpak KDE runtime: multiarch subdirs). Fall back to walking up from
    # Qt6_DIR (<prefix>/lib/cmake/Qt6) for older Qt packages that don't export them.
    if(DEFINED QT6_INSTALL_PREFIX AND DEFINED QT6_INSTALL_PLUGINS)
      set(PATCHY_QT_PLUGIN_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}")
      set(PATCHY_QT_TRANSLATION_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
    else()
      get_filename_component(PATCHY_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
      set(PATCHY_QT_PLUGIN_DIR "${PATCHY_QT_PREFIX}/plugins")
      set(PATCHY_QT_TRANSLATION_DIR "${PATCHY_QT_PREFIX}/translations")
    endif()
    # Runtime asset copies are shared copy-once custom targets rather than
    # per-target POST_BUILD commands: all executables share one output
    # directory, and concurrent copies of the same destination files race
    # under parallel Ninja (intermittent sharing violations on Windows).
    if(PATCHY_QT_TRANSLATION_DIR AND EXISTS "${PATCHY_QT_TRANSLATION_DIR}/qtbase_ja.qm")
      add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/translations/qtbase_ja.qm"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_BINARY_DIR}/translations"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PATCHY_QT_TRANSLATION_DIR}/qtbase_ja.qm"
                "${CMAKE_BINARY_DIR}/translations/qtbase_ja.qm"
        DEPENDS "${PATCHY_QT_TRANSLATION_DIR}/qtbase_ja.qm"
        COMMENT "Copying Qt Japanese base translation"
      )
      add_custom_target(patchy_qt_base_translations
        DEPENDS "${CMAKE_BINARY_DIR}/translations/qtbase_ja.qm"
      )
    endif()
    function(patchy_copy_translations target)
      # lrelease already writes the app .qm files to
      # ${CMAKE_BINARY_DIR}/translations (OUTPUT_LOCATION above), which is the
      # runtime location next to the executables, so no copy step is needed.
      add_dependencies(${target} patchy_translations)
      if(TARGET patchy_qt_base_translations)
        add_dependencies(${target} patchy_qt_base_translations)
      endif()
    endfunction()
    set(PATCHY_REQUIRED_QT_IMAGE_FORMAT_PLUGINS
      qgif
      qico
      qjpeg
      qsvg
      qtiff
      qwebp
    )
    # Per-platform Qt plugin file naming and the platform's TLS backend. The existence
    # checks fail at configure time (not first HTTPS use / first TIFF open at runtime)
    # when the Qt install is missing qtimageformats or its TLS plugin.
    if(WIN32)
      set(PATCHY_REQUIRED_QT_TLS_PLUGINS qschannelbackend)
      set(PATCHY_QT_PLUGIN_PREFIX "")
      set(PATCHY_QT_PLUGIN_SUFFIX ".dll")
    elseif(APPLE)
      set(PATCHY_REQUIRED_QT_TLS_PLUGINS qsecuretransportbackend)
      set(PATCHY_QT_PLUGIN_PREFIX "lib")
      set(PATCHY_QT_PLUGIN_SUFFIX ".dylib")
    else()
      set(PATCHY_REQUIRED_QT_TLS_PLUGINS qopensslbackend)
      set(PATCHY_QT_PLUGIN_PREFIX "lib")
      set(PATCHY_QT_PLUGIN_SUFFIX ".so")
    endif()
    foreach(plugin IN LISTS PATCHY_REQUIRED_QT_IMAGE_FORMAT_PLUGINS)
      if(NOT EXISTS "${PATCHY_QT_PLUGIN_DIR}/imageformats/${PATCHY_QT_PLUGIN_PREFIX}${plugin}${PATCHY_QT_PLUGIN_SUFFIX}")
        message(FATAL_ERROR
          "Required Qt image format plugin was not found: "
          "${PATCHY_QT_PLUGIN_DIR}/imageformats/${PATCHY_QT_PLUGIN_PREFIX}${plugin}${PATCHY_QT_PLUGIN_SUFFIX}. "
          "Install the Qt qtimageformats module for TIFF/WebP support.")
      endif()
    endforeach()
    foreach(plugin IN LISTS PATCHY_REQUIRED_QT_TLS_PLUGINS)
      if(NOT EXISTS "${PATCHY_QT_PLUGIN_DIR}/tls/${PATCHY_QT_PLUGIN_PREFIX}${plugin}${PATCHY_QT_PLUGIN_SUFFIX}")
        message(FATAL_ERROR
          "Required Qt TLS plugin was not found: "
          "${PATCHY_QT_PLUGIN_DIR}/tls/${PATCHY_QT_PLUGIN_PREFIX}${plugin}${PATCHY_QT_PLUGIN_SUFFIX}.")
      endif()
    endforeach()
    if(WIN32)
      set(PATCHY_QT_IMAGE_FORMAT_PLUGIN_FILES)
      foreach(plugin IN LISTS PATCHY_REQUIRED_QT_IMAGE_FORMAT_PLUGINS)
        list(APPEND PATCHY_QT_IMAGE_FORMAT_PLUGIN_FILES
          "${PATCHY_QT_PLUGIN_DIR}/imageformats/${plugin}$<$<CONFIG:Debug>:d>.dll")
      endforeach()
      set(PATCHY_QT_TLS_PLUGIN_FILES)
      foreach(plugin IN LISTS PATCHY_REQUIRED_QT_TLS_PLUGINS)
        list(APPEND PATCHY_QT_TLS_PLUGIN_FILES
          "${PATCHY_QT_PLUGIN_DIR}/tls/${plugin}$<$<CONFIG:Debug>:d>.dll")
      endforeach()
      set(PATCHY_QT_RUNTIME_STAMP "${CMAKE_BINARY_DIR}/CMakeFiles/patchy_qt_runtime.stamp")
      add_custom_command(
        OUTPUT "${PATCHY_QT_RUNTIME_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:Qt6::Core>
                $<TARGET_FILE:Qt6::Gui>
                $<TARGET_FILE:Qt6::Widgets>
                $<TARGET_FILE:Qt6::Svg>
                $<TARGET_FILE:Qt6::PrintSupport>
                $<TARGET_FILE:Qt6::Network>
                $<TARGET_FILE:Qt6::Test>
                "${CMAKE_BINARY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_BINARY_DIR}/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PATCHY_QT_PLUGIN_DIR}/platforms/qwindows$<$<CONFIG:Debug>:d>.dll"
                "${PATCHY_QT_PLUGIN_DIR}/platforms/qoffscreen$<$<CONFIG:Debug>:d>.dll"
                "${CMAKE_BINARY_DIR}/platforms"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_BINARY_DIR}/iconengines"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PATCHY_QT_PLUGIN_DIR}/iconengines/qsvgicon$<$<CONFIG:Debug>:d>.dll"
                "${CMAKE_BINARY_DIR}/iconengines"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_BINARY_DIR}/imageformats"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${PATCHY_QT_IMAGE_FORMAT_PLUGIN_FILES}
                "${CMAKE_BINARY_DIR}/imageformats"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_BINARY_DIR}/tls"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${PATCHY_QT_TLS_PLUGIN_FILES}
                "${CMAKE_BINARY_DIR}/tls"
        COMMAND ${CMAKE_COMMAND} -E touch "${PATCHY_QT_RUNTIME_STAMP}"
        DEPENDS
          $<TARGET_FILE:Qt6::Core>
          $<TARGET_FILE:Qt6::Gui>
          $<TARGET_FILE:Qt6::Widgets>
          $<TARGET_FILE:Qt6::Svg>
          $<TARGET_FILE:Qt6::PrintSupport>
          $<TARGET_FILE:Qt6::Network>
          $<TARGET_FILE:Qt6::Test>
          "${PATCHY_QT_PLUGIN_DIR}/platforms/qwindows$<$<CONFIG:Debug>:d>.dll"
          "${PATCHY_QT_PLUGIN_DIR}/platforms/qoffscreen$<$<CONFIG:Debug>:d>.dll"
          "${PATCHY_QT_PLUGIN_DIR}/iconengines/qsvgicon$<$<CONFIG:Debug>:d>.dll"
          ${PATCHY_QT_IMAGE_FORMAT_PLUGIN_FILES}
          ${PATCHY_QT_TLS_PLUGIN_FILES}
        COMMENT "Copying Qt runtime into the build directory"
      )
      add_custom_target(patchy_qt_runtime DEPENDS "${PATCHY_QT_RUNTIME_STAMP}")
    endif()
    function(patchy_copy_qt_runtime target)
      if(TARGET patchy_qt_runtime)
        add_dependencies(${target} patchy_qt_runtime)
      endif()
    endfunction()
    if(EXISTS "${PATCHY_BUNDLED_FONTS_DIR}")
      file(GLOB_RECURSE PATCHY_BUNDLED_FONT_FILES CONFIGURE_DEPENDS
        "${PATCHY_BUNDLED_FONTS_DIR}/*")
      set(PATCHY_FONTS_STAMP "${CMAKE_BINARY_DIR}/CMakeFiles/patchy_bundled_fonts.stamp")
      add_custom_command(
        OUTPUT "${PATCHY_FONTS_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PATCHY_BUNDLED_FONTS_DIR}"
                "${CMAKE_BINARY_DIR}/fonts"
        COMMAND ${CMAKE_COMMAND} -E touch "${PATCHY_FONTS_STAMP}"
        DEPENDS ${PATCHY_BUNDLED_FONT_FILES}
        COMMENT "Copying bundled fonts into the build directory"
      )
      add_custom_target(patchy_bundled_fonts DEPENDS "${PATCHY_FONTS_STAMP}")
    endif()
    function(patchy_copy_bundled_fonts target)
      if(TARGET patchy_bundled_fonts)
        add_dependencies(${target} patchy_bundled_fonts)
      endif()
    endfunction()
    add_library(patchy_ui
      src/ui/action_icons.cpp
      src/ui/app_settings.hpp
      src/ui/app_settings.cpp
      src/ui/background_workers.hpp
      src/ui/background_workers.cpp
      src/ui/gradient_library.cpp
      src/ui/gradient_manager_dialog.cpp
      src/ui/blend_mode_ui.cpp
      src/ui/brush_dynamics_popup.hpp
      src/ui/brush_dynamics_popup.cpp
      src/ui/brush_presets.cpp
      src/ui/brush_tip_library.hpp
      src/ui/brush_tip_library.cpp
      src/ui/brush_tip_manager_dialog.hpp
      src/ui/brush_tip_manager_dialog.cpp
      src/ui/brush_tip_picker.hpp
      src/ui/brush_tip_picker.cpp
      src/ui/canvas_widget.hpp
      src/ui/canvas_widget.cpp
      src/ui/canvas_widget_shared.hpp
      src/ui/canvas_widget_shared.cpp
      src/ui/canvas_widget_brush.cpp
      src/ui/canvas_widget_cursors.cpp
      src/ui/canvas_widget_draw_tools.cpp
      src/ui/canvas_widget_events.cpp
      src/ui/canvas_widget_guides.cpp
      src/ui/canvas_widget_move.cpp
      src/ui/canvas_widget_pen.cpp
      src/ui/canvas_widget_render.cpp
      src/ui/canvas_widget_selection.cpp
      src/ui/canvas_widget_selection_engines.cpp
      src/ui/canvas_widget_transform.cpp
      src/ui/canvas_widget_vector_tools.cpp
      src/ui/canvas_widget_view.cpp
      src/ui/channel_panel.hpp
      src/ui/channel_panel.cpp
      src/ui/color_panel.hpp
      src/ui/default_brush_tips.hpp
      src/ui/default_brush_tips.cpp
      src/ui/color_panel.cpp
      src/ui/compatibility_report.cpp
      src/ui/coalesced_preview_emitter.hpp
      src/ui/dialog_utils.cpp
      src/ui/document_float_window.hpp
      src/ui/document_float_window.cpp
      src/ui/edit_conversions.cpp
      src/ui/curves_clipping_preview.hpp
      src/ui/curves_clipping_preview.cpp
      src/ui/curves_editor.hpp
      src/ui/curves_editor.cpp
      src/ui/curves_presets.hpp
      src/ui/curves_presets.cpp
      src/ui/filter_workflows.cpp
      src/ui/adjustment_dialogs.cpp
      src/ui/filter_look_library.hpp
      src/ui/filter_look_library.cpp
      src/ui/visual_filter_gallery_dialog.hpp
      src/ui/visual_filter_gallery_dialog.cpp
      src/ui/filter_gallery_controls.hpp
      src/ui/filter_gallery_controls.cpp
      src/ui/filter_overlay_sync.hpp
      src/ui/filter_overlay_sync.cpp
      src/ui/filter_parameter_panel.hpp
      src/ui/filter_parameter_panel.cpp
      src/ui/filter_preview_proxy.hpp
      src/ui/filter_preview_proxy.cpp
      src/ui/zoomable_image_preview.hpp
      src/ui/zoomable_image_preview.cpp
      src/ui/font_picker.hpp
      src/ui/font_picker.cpp
      src/ui/blend_if_range_editor.hpp
      src/ui/blend_if_range_editor.cpp
      src/ui/gradient_stops_editor.hpp
      src/ui/gradient_stops_editor.cpp
      src/ui/hotkey_editor.hpp
      src/ui/hotkey_editor.cpp
      src/ui/hotkey_registry.hpp
      src/ui/hotkey_registry.cpp
      src/ui/image_document_io.cpp
      src/ui/image_save_options_dialog.cpp
      src/ui/layer_style_dialog.cpp
      src/ui/layer_list_widget.cpp
      src/ui/liquify_dialog.hpp
      src/ui/liquify_dialog.cpp
      src/ui/localization.cpp
      src/ui/main_window.hpp
      src/ui/main_window.cpp
      src/ui/main_window_actions.cpp
      src/ui/main_window_adjustments.cpp
      src/ui/main_window_channels.cpp
      src/ui/main_window_chrome.cpp
      src/ui/main_window_docks.cpp
      src/ui/main_window_document_dialogs.cpp
      src/ui/main_window_files.cpp
      src/ui/main_window_history.cpp
      src/ui/main_window_layer_ops.cpp
      src/ui/main_window_layer_panel.cpp
      src/ui/main_window_palette.cpp
      src/ui/main_window_plugins.cpp
      src/ui/main_window_preferences.cpp
      src/ui/main_window_sessions.cpp
      src/ui/main_window_shared.hpp
      src/ui/main_window_shared.cpp
      src/ui/main_window_smart_objects.cpp
      src/ui/main_window_stress_test.cpp
      src/ui/main_window_theme.cpp
      src/ui/main_window_paths.cpp
      src/ui/main_window_tool_options.cpp
      src/ui/main_window_vector.cpp
      src/ui/paths_panel.hpp
      src/ui/paths_panel.cpp
      src/ui/custom_shape_library.hpp
      src/ui/custom_shape_library.cpp
      src/ui/default_custom_shapes.hpp
      src/ui/default_custom_shapes.cpp
      src/ui/shape_appearance_dialog.hpp
      src/ui/shape_appearance_dialog.cpp
      src/ui/measurement_units.hpp
      src/ui/measurement_units.cpp
      src/ui/new_document_dialog.hpp
      src/ui/new_document_dialog.cpp
      src/ui/stress_test.hpp
      src/ui/palette_convert_dialog.hpp
      src/ui/palette_convert_dialog.cpp
      src/ui/palette_panel.hpp
      src/ui/palette_panel.cpp
      src/ui/pattern_library.hpp
      src/ui/pattern_library.cpp
      src/ui/pattern_manager_dialog.hpp
      src/ui/pattern_manager_dialog.cpp
      src/ui/photo_pattern_presets.hpp
      src/ui/photo_pattern_presets.cpp
      src/ui/preset_library.hpp
      src/ui/preset_library.cpp
      src/ui/print_dialog.cpp
      src/ui/qt_geometry.cpp
      src/ui/raw_develop_dialog.hpp
      src/ui/raw_develop_dialog.cpp
      src/ui/scanner_import.hpp
      src/ui/selection_outline.hpp
      src/ui/selection_outline.cpp
      src/ui/start_panel.hpp
      src/ui/start_panel.cpp
      src/ui/smart_object_render.hpp
      src/ui/smart_object_render.cpp
      src/ui/sprite_sheet_dialog.hpp
      src/ui/sprite_sheet_dialog.cpp
      src/ui/style_browser.hpp
      src/ui/style_browser.cpp
      src/ui/style_library.hpp
      src/ui/style_library.cpp
      src/ui/style_manager_dialog.hpp
      src/ui/style_manager_dialog.cpp
      src/ui/tile_preview_window.hpp
      src/ui/tile_preview_window.cpp
      src/ui/splash_dialog.cpp
      src/ui/tool_cursors.hpp
      src/ui/tool_cursors.cpp
      src/ui/update_checker.hpp
      src/ui/update_checker.cpp
      src/ui/warp_text_dialog.hpp
      src/ui/warp_text_dialog.cpp
      src/ui/zoom_status_bar.hpp
      src/ui/zoom_status_bar.cpp
      src/ui/icons.qrc
      src/ui/textures.qrc
    )
    target_link_libraries(patchy_ui PUBLIC
      Qt6::Widgets
      Qt6::Svg
      Qt6::PrintSupport
      Qt6::Network
      patchy_core
      patchy_render
      patchy_psd
      patchy_plugins
      patchy_filters
      patchy_formats
      patchy_color
      $<$<PLATFORM_ID:Windows>:dwmapi>
    )
    target_compile_definitions(patchy_ui PRIVATE PATCHY_VERSION="${PROJECT_VERSION}")
    # main_window.cpp exceeds the default COFF section limit under MSVC.
    target_compile_options(patchy_ui PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)
    if(APPLE)
      target_sources(patchy_ui PRIVATE
        src/ui/dialog_utils_mac.mm
        src/ui/scanner_import_mac.mm
      )
      # The scanner controller uses weak delegates and an asynchronous AppKit sheet;
      # keep its Objective-C object lifetime explicit through ARC without changing the
      # existing dialog utility translation unit.
      set_source_files_properties(src/ui/scanner_import_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
      target_link_libraries(patchy_ui PUBLIC
        "-framework AppKit"
        "-framework Quartz"
        "-framework ImageCaptureCore"
      )
    endif()
    if(WIN32)
      # WIA scanner/camera acquisition (File > Import > From Scanner or Camera...).
      target_sources(patchy_ui PRIVATE src/ui/scanner_import_win.cpp)
      target_link_libraries(patchy_ui PUBLIC wiaguid)
    endif()
    patchy_configure_target(patchy_ui)

    add_executable(patchy WIN32 MACOSX_BUNDLE
      src/app/main.cpp
    )
    target_link_libraries(patchy PRIVATE patchy_ui)
    target_compile_definitions(patchy PRIVATE PATCHY_VERSION="${PROJECT_VERSION}")
    patchy_configure_target(patchy)
    patchy_copy_translations(patchy)
    patchy_copy_qt_runtime(patchy)
    patchy_copy_bundled_fonts(patchy)
    if(WIN32)
      target_sources(patchy PRIVATE src/app/patchy.rc)
    endif()
    if(APPLE)
      set_target_properties(patchy PROPERTIES
        OUTPUT_NAME "Patchy"
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Patchy"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.rtsoft.patchy"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "patchy.icns"
      )
      if(EXISTS "${PROJECT_SOURCE_DIR}/packaging/macos/patchy.icns")
        target_sources(patchy PRIVATE "${PROJECT_SOURCE_DIR}/packaging/macos/patchy.icns")
        set_source_files_properties("${PROJECT_SOURCE_DIR}/packaging/macos/patchy.icns"
          PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
      endif()
      # Stage runtime resources inside the bundle (the executable lives in
      # Contents/MacOS; fonts/translations are loaded from Contents/Resources). This is a
      # per-target POST_BUILD, not a shared copy-once target, because the bundle directory
      # belongs to the patchy target alone -- no other executable writes here.
      add_custom_command(TARGET patchy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_BINARY_DIR}/fonts"
                "$<TARGET_BUNDLE_CONTENT_DIR:patchy>/Resources/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_BINARY_DIR}/translations"
                "$<TARGET_BUNDLE_CONTENT_DIR:patchy>/Resources/translations"
        COMMENT "Staging fonts and translations into the Patchy app bundle"
      )
    endif()
    add_custom_command(TARGET patchy POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
              ${PROJECT_SOURCE_DIR}/test-fixtures
              $<TARGET_FILE_DIR:patchy>/test-fixtures
      COMMENT "Copying Patchy test fixtures next to the app binary"
    )
    install(TARGETS patchy
      BUNDLE DESTINATION .
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    if(UNIX AND NOT APPLE)
      # Linux install layout (what the Flatpak build uses): binary in bin/, resources
      # under share/patchy/ (probed by load_bundled_fonts / translation_directories),
      # and the freedesktop integration files under share/.
      install(DIRECTORY "${PATCHY_BUNDLED_FONTS_DIR}/"
              DESTINATION "${CMAKE_INSTALL_DATADIR}/patchy/fonts")
      install(FILES ${PATCHY_QM_FILES}
              DESTINATION "${CMAKE_INSTALL_DATADIR}/patchy/translations")
      if(TARGET patchy_qt_base_translations)
        install(FILES "${CMAKE_BINARY_DIR}/translations/qtbase_ja.qm"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/patchy/translations")
      endif()
      install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/com.rtsoft.patchy.desktop"
              DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
      install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/com.rtsoft.patchy.metainfo.xml"
              DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
      foreach(icon_size 16 32 48 64 128 256)
        install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/icons/hicolor/${icon_size}x${icon_size}/apps/com.rtsoft.patchy.png"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${icon_size}x${icon_size}/apps")
      endforeach()
    endif()
  else()
    message(STATUS "Qt 6 Widgets was not found; skipping the native Patchy app target.")
  endif()
endif()

if(PATCHY_BUILD_TESTS)
  # One binary, one TU per test group (tests/core/test_groups.hpp lists the groups;
  # main() concatenates the group vectors in a fixed order — never static self-registration).
  add_executable(patchy_core_tests
    tests/core/main.cpp
    tests/core/core_test_support.cpp
    tests/core/psd_test_support.cpp
    tests/core/adjustments_curves_tests.cpp
    tests/core/brush_engine_tests.cpp
    tests/core/compositor_blend_if_tests.cpp
    tests/core/compositor_layer_styles_tests.cpp
    tests/core/document_model_tests.cpp
    tests/core/document_ops_filters_tests.cpp
    tests/core/flat_formats_bmp_tests.cpp
    tests/core/flat_formats_misc_tests.cpp
    tests/core/gradients_interior_effects_tests.cpp
    tests/core/infra_selection_tests.cpp
    tests/core/layer_metadata_tests.cpp
    tests/core/palette_tests.cpp
    tests/core/pat_asl_abr_tests.cpp
    tests/core/pattern_styles_fixtures_tests.cpp
    tests/core/pixel_tools_tests.cpp
    tests/core/psd_core_io_tests.cpp
    tests/core/psd_structure_tests.cpp
    tests/core/psd_text_tests.cpp
    tests/core/psd_vector_fixtures_tests.cpp
    tests/core/psd_writer_stability_tests.cpp
    tests/core/raw_heif_tests.cpp
    tests/core/smart_filter_descriptors_tests.cpp
    tests/core/smart_filter_pixels_tests.cpp
    tests/core/smart_objects_warp_tests.cpp
    tests/core/stroke_mask_effects_tests.cpp
    tests/core/svg_tests.cpp
    tests/core/vector_raster_tests.cpp
    tests/core/vector_shape_tests.cpp
  )
  # The split TUs live in tests/core/ but repeat the historical test_main.cpp
  # include block ("test_harness.hpp" etc.), so tests/ must be on the include path.
  target_include_directories(patchy_core_tests PRIVATE tests)
  target_link_libraries(patchy_core_tests PRIVATE
    patchy_core
    patchy_render
    patchy_psd
    patchy_plugins
    patchy_filters
    patchy_formats
    patchy_color
  )
  patchy_configure_target(patchy_core_tests)
  target_compile_definitions(patchy_core_tests PRIVATE PATCHY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  add_test(NAME patchy_core_tests COMMAND patchy_core_tests)

  if(PATCHY_BUILD_APP AND Qt6_FOUND)
    # One binary; per-group TUs live in tests/ui/ (tests/ui/ui_test_groups.hpp lists
    # the groups; main() concatenates the group vectors in a fixed order — never
    # static self-registration).
    add_executable(patchy_ui_visual_tests
      tests/ui/main.cpp
      tests/ui/ui_test_support.cpp
      tests/ui/app_shell_tests.cpp
      tests/ui/filter_catalog_dialog_tests.cpp
      tests/ui/layer_style_gradient_tests.cpp
      tests/ui/destructive_filters_gallery_tests.cpp
      tests/ui/pickers_notices_hotkeys_tests.cpp
      tests/ui/canvas_view_tools_tests.cpp
      tests/ui/layer_context_lifecycle_tests.cpp
      tests/ui/brush_pattern_palette_tests.cpp
      tests/ui/layer_panel_organization_tests.cpp
      tests/ui/move_tool_processing_overlay_tests.cpp
      tests/ui/selection_marquee_lasso_tests.cpp
      tests/ui/clipboard_free_transform_tests.cpp
      tests/ui/channels_panel_tests.cpp
      tests/ui/camera_raw_heif_tests.cpp
      tests/ui/layer_mask_tests.cpp
      tests/ui/pen_tablet_input_tests.cpp
      tests/ui/brush_engine_stroke_tests.cpp
      tests/ui/text_editor_font_picker_tests.cpp
      tests/ui/psd_text_import_tests.cpp
      tests/ui/text_transform_commit_tests.cpp
      tests/ui/flat_image_format_tests.cpp
      tests/ui/smart_filter_tests.cpp
      tests/ui/smart_object_tests.cpp
      tests/ui/warp_tests.cpp
      tests/ui/import_print_resolution_tests.cpp
      tests/ui/image_adjustments_curves_tests.cpp
      tests/ui/selection_engines_tests.cpp
      tests/ui/misc_visuals_outline_stress_tests.cpp
      tests/ui/float_window_tests.cpp
      tests/ui/vector_shape_tool_tests.cpp
      tests/ui/svg_ui_tests.cpp
      tests/ui/readme_screenshot_tests.cpp
    )
    # The split TUs live in tests/ui/ but repeat the historical monolith include
    # block ("test_harness.hpp" etc.), so tests/ must be on the include path.
    target_include_directories(patchy_ui_visual_tests PRIVATE tests)
    # Qt6::Test provides QTest::keyClick for tests that must go through real event
    # dispatch (QShortcutMap contention) instead of QApplication::sendEvent.
    target_link_libraries(patchy_ui_visual_tests PRIVATE patchy_ui Qt6::Test)
    patchy_configure_target(patchy_ui_visual_tests)
    target_compile_options(patchy_ui_visual_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)
    target_compile_definitions(patchy_ui_visual_tests PRIVATE PATCHY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    patchy_copy_translations(patchy_ui_visual_tests)
    patchy_copy_qt_runtime(patchy_ui_visual_tests)
    patchy_copy_bundled_fonts(patchy_ui_visual_tests)
    add_test(NAME patchy_ui_visual_tests COMMAND patchy_ui_visual_tests)
    set_tests_properties(patchy_ui_visual_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(patchy_curves_clipping_preview_tests
      tests/curves_clipping_preview_tests.cpp
    )
    target_link_libraries(patchy_curves_clipping_preview_tests PRIVATE patchy_ui)
    patchy_configure_target(patchy_curves_clipping_preview_tests)
    patchy_copy_translations(patchy_curves_clipping_preview_tests)
    patchy_copy_qt_runtime(patchy_curves_clipping_preview_tests)
    patchy_copy_bundled_fonts(patchy_curves_clipping_preview_tests)
    add_test(NAME patchy_curves_clipping_preview_tests
             COMMAND patchy_curves_clipping_preview_tests)

    add_executable(patchy_perf_tests
      tests/perf_tests.cpp
    )
    target_link_libraries(patchy_perf_tests PRIVATE patchy_ui)
    patchy_configure_target(patchy_perf_tests)
    target_compile_definitions(patchy_perf_tests PRIVATE PATCHY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    patchy_copy_translations(patchy_perf_tests)
    patchy_copy_qt_runtime(patchy_perf_tests)
    patchy_copy_bundled_fonts(patchy_perf_tests)
    add_test(NAME patchy_perf_tests COMMAND patchy_perf_tests)
  endif()
endif()
