cmake_minimum_required(VERSION 3.26)

project(
  Patchy
  VERSION 0.10
  DESCRIPTION "A native, PSD-oriented image editor foundation"
  LANGUAGES CXX
)

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
  src/core/adjustment_layer.cpp
  src/core/blend_math.cpp
  src/core/brush_tip.cpp
  src/core/document.cpp
  src/core/layer.cpp
  src/core/layer_metadata.cpp
  src/core/layer_render_utils.cpp
  src/core/layer_tree.cpp
  src/core/pixel_buffer.cpp
  src/core/pixel_tools.cpp
  src/core/rect_utils.cpp
)
patchy_configure_target(patchy_core)

add_library(patchy_render
  src/render/compositor.cpp
  src/render/tile_cache.cpp
)
target_link_libraries(patchy_render PUBLIC patchy_core)
patchy_configure_target(patchy_render)

add_library(patchy_psd
  src/psd/abr_reader.cpp
  src/psd/psd_binary.cpp
  src/psd/psd_descriptor.cpp
  src/psd/psd_document_io.cpp
)
target_link_libraries(patchy_psd PUBLIC patchy_core patchy_render)
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
  src/filters/filter_registry.cpp
)
target_link_libraries(patchy_filters PUBLIC patchy_core)
patchy_configure_target(patchy_filters)

add_library(patchy_formats
  src/formats/bmp_document_io.cpp
  src/formats/format_registry.cpp
)
target_link_libraries(patchy_formats PUBLIC patchy_core patchy_render patchy_psd)
patchy_configure_target(patchy_formats)

add_library(patchy_color
  src/color/color_management.cpp
)
target_link_libraries(patchy_color PUBLIC patchy_core)
patchy_configure_target(patchy_color)

if(PATCHY_BUILD_APP)
  find_package(Qt6 QUIET COMPONENTS Widgets Svg PrintSupport Network LinguistTools)
  if(Qt6_FOUND)
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
    get_target_property(PATCHY_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT PATCHY_QT_CORE_LOCATION)
      get_target_property(PATCHY_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION)
    endif()
    if(PATCHY_QT_CORE_LOCATION)
      get_filename_component(PATCHY_QT_BIN_DIR "${PATCHY_QT_CORE_LOCATION}" DIRECTORY)
      get_filename_component(PATCHY_QT_PREFIX "${PATCHY_QT_BIN_DIR}" DIRECTORY)
      set(PATCHY_QT_TRANSLATION_DIR "${PATCHY_QT_PREFIX}/translations")
    endif()
    function(patchy_copy_translations target)
      add_dependencies(${target} patchy_translations)
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:${target}>/translations
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${PATCHY_TRANSLATION_OUTPUT_DIR}
                $<TARGET_FILE_DIR:${target}>/translations
        COMMENT "Copying Patchy translations next to ${target}"
      )
      if(PATCHY_QT_TRANSLATION_DIR AND EXISTS "${PATCHY_QT_TRANSLATION_DIR}/qtbase_ja.qm")
        add_custom_command(TARGET ${target} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  "${PATCHY_QT_TRANSLATION_DIR}/qtbase_ja.qm"
                  $<TARGET_FILE_DIR:${target}>/translations/qtbase_ja.qm
          COMMENT "Copying Qt Japanese base translation next to ${target}"
        )
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
    set(PATCHY_REQUIRED_QT_TLS_PLUGINS
      qschannelbackend
    )
    if(WIN32)
      foreach(plugin IN LISTS PATCHY_REQUIRED_QT_IMAGE_FORMAT_PLUGINS)
        if(NOT EXISTS "${PATCHY_QT_PREFIX}/plugins/imageformats/${plugin}.dll")
          message(FATAL_ERROR
            "Required Qt image format plugin was not found: "
            "${PATCHY_QT_PREFIX}/plugins/imageformats/${plugin}.dll. "
            "Install the Qt qtimageformats module for TIFF/WebP support.")
        endif()
      endforeach()
      foreach(plugin IN LISTS PATCHY_REQUIRED_QT_TLS_PLUGINS)
        if(NOT EXISTS "${PATCHY_QT_PREFIX}/plugins/tls/${plugin}.dll")
          message(FATAL_ERROR
            "Required Qt TLS plugin was not found: "
            "${PATCHY_QT_PREFIX}/plugins/tls/${plugin}.dll.")
        endif()
      endforeach()
    endif()
    function(patchy_copy_qt_runtime target)
      if(WIN32)
        set(PATCHY_QT_IMAGE_FORMAT_PLUGIN_FILES)
        foreach(plugin IN LISTS PATCHY_REQUIRED_QT_IMAGE_FORMAT_PLUGINS)
          list(APPEND PATCHY_QT_IMAGE_FORMAT_PLUGIN_FILES
            "${PATCHY_QT_PREFIX}/plugins/imageformats/${plugin}$<$<CONFIG:Debug>:d>.dll")
        endforeach()
        set(PATCHY_QT_TLS_PLUGIN_FILES)
        foreach(plugin IN LISTS PATCHY_REQUIRED_QT_TLS_PLUGINS)
          list(APPEND PATCHY_QT_TLS_PLUGIN_FILES
            "${PATCHY_QT_PREFIX}/plugins/tls/${plugin}$<$<CONFIG:Debug>:d>.dll")
        endforeach()
        add_custom_command(TARGET ${target} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  $<TARGET_FILE:Qt6::Core>
                  $<TARGET_FILE:Qt6::Gui>
                  $<TARGET_FILE:Qt6::Widgets>
                  $<TARGET_FILE:Qt6::Svg>
                  $<TARGET_FILE:Qt6::PrintSupport>
                  $<TARGET_FILE:Qt6::Network>
                  $<TARGET_FILE_DIR:${target}>
          COMMAND ${CMAKE_COMMAND} -E make_directory
                  $<TARGET_FILE_DIR:${target}>/platforms
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  "${PATCHY_QT_PREFIX}/plugins/platforms/qwindows$<$<CONFIG:Debug>:d>.dll"
                  "${PATCHY_QT_PREFIX}/plugins/platforms/qoffscreen$<$<CONFIG:Debug>:d>.dll"
                  $<TARGET_FILE_DIR:${target}>/platforms
          COMMAND ${CMAKE_COMMAND} -E make_directory
                  $<TARGET_FILE_DIR:${target}>/iconengines
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  "${PATCHY_QT_PREFIX}/plugins/iconengines/qsvgicon$<$<CONFIG:Debug>:d>.dll"
                  $<TARGET_FILE_DIR:${target}>/iconengines
          COMMAND ${CMAKE_COMMAND} -E make_directory
                  $<TARGET_FILE_DIR:${target}>/imageformats
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  ${PATCHY_QT_IMAGE_FORMAT_PLUGIN_FILES}
                  $<TARGET_FILE_DIR:${target}>/imageformats
          COMMAND ${CMAKE_COMMAND} -E make_directory
                  $<TARGET_FILE_DIR:${target}>/tls
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  ${PATCHY_QT_TLS_PLUGIN_FILES}
                  $<TARGET_FILE_DIR:${target}>/tls
          COMMENT "Copying Qt runtime next to ${target}"
        )
      endif()
    endfunction()
    function(patchy_copy_bundled_fonts target)
      if(EXISTS "${PATCHY_BUNDLED_FONTS_DIR}")
        add_custom_command(TARGET ${target} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E make_directory
                  $<TARGET_FILE_DIR:${target}>/fonts
          COMMAND ${CMAKE_COMMAND} -E copy_directory
                  ${PATCHY_BUNDLED_FONTS_DIR}
                  $<TARGET_FILE_DIR:${target}>/fonts
          COMMENT "Copying bundled fonts next to ${target}"
        )
      endif()
    endfunction()
    add_library(patchy_ui
      src/ui/action_icons.cpp
      src/ui/app_settings.hpp
      src/ui/app_settings.cpp
      src/ui/blend_mode_ui.cpp
      src/ui/brush_presets.cpp
      src/ui/brush_tip_library.hpp
      src/ui/brush_tip_library.cpp
      src/ui/brush_tip_manager_dialog.hpp
      src/ui/brush_tip_manager_dialog.cpp
      src/ui/brush_tip_picker.hpp
      src/ui/brush_tip_picker.cpp
      src/ui/canvas_widget.hpp
      src/ui/canvas_widget.cpp
      src/ui/color_panel.hpp
      src/ui/default_brush_tips.hpp
      src/ui/default_brush_tips.cpp
      src/ui/color_panel.cpp
      src/ui/compatibility_report.cpp
      src/ui/dialog_utils.cpp
      src/ui/edit_conversions.cpp
      src/ui/filter_workflows.cpp
      src/ui/hotkey_editor.hpp
      src/ui/hotkey_editor.cpp
      src/ui/hotkey_registry.hpp
      src/ui/hotkey_registry.cpp
      src/ui/image_document_io.cpp
      src/ui/image_save_options_dialog.cpp
      src/ui/layer_style_dialog.cpp
      src/ui/layer_list_widget.cpp
      src/ui/localization.cpp
      src/ui/main_window.hpp
      src/ui/main_window.cpp
      src/ui/print_dialog.cpp
      src/ui/qt_geometry.cpp
      src/ui/selection_outline.hpp
      src/ui/selection_outline.cpp
      src/ui/splash_dialog.cpp
      src/ui/update_checker.hpp
      src/ui/update_checker.cpp
      src/ui/icons.qrc
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
    add_custom_command(TARGET patchy POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              ${PROJECT_SOURCE_DIR}/test-fixtures
              $<TARGET_FILE_DIR:patchy>/test-fixtures
      COMMENT "Copying Patchy test fixtures next to the app binary"
    )
    install(TARGETS patchy
      BUNDLE DESTINATION .
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  else()
    message(STATUS "Qt 6 Widgets was not found; skipping the native Patchy app target.")
  endif()
endif()

if(PATCHY_BUILD_TESTS)
  add_executable(patchy_core_tests tests/test_main.cpp)
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
    add_executable(patchy_ui_visual_tests
      tests/ui_visual_tests.cpp
    )
    target_link_libraries(patchy_ui_visual_tests PRIVATE patchy_ui)
    patchy_configure_target(patchy_ui_visual_tests)
    target_compile_definitions(patchy_ui_visual_tests PRIVATE PATCHY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    patchy_copy_translations(patchy_ui_visual_tests)
    patchy_copy_qt_runtime(patchy_ui_visual_tests)
    patchy_copy_bundled_fonts(patchy_ui_visual_tests)
    add_test(NAME patchy_ui_visual_tests COMMAND patchy_ui_visual_tests)
    set_tests_properties(patchy_ui_visual_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
