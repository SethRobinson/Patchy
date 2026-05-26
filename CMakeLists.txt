cmake_minimum_required(VERSION 3.26)

project(
  Photoslop
  VERSION 0.1.0
  DESCRIPTION "A native, PSD-oriented image editor foundation"
  LANGUAGES CXX
)

include(CTest)
include(GNUInstallDirs)

option(PHOTOSLOP_BUILD_APP "Build the Qt desktop application when Qt 6 is available" ON)
option(PHOTOSLOP_BUILD_TESTS "Build unit tests" ON)
option(PHOTOSLOP_ENABLE_STRICT_WARNINGS "Enable strict compiler warnings" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(photoslop_configure_target target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  target_include_directories(${target} PUBLIC ${PROJECT_SOURCE_DIR}/src)

  if(PHOTOSLOP_ENABLE_STRICT_WARNINGS)
    if(MSVC)
      target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
      target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()
endfunction()

add_library(photoslop_core
  src/core/blend_math.cpp
  src/core/document.cpp
  src/core/layer.cpp
  src/core/layer_metadata.cpp
  src/core/layer_render_utils.cpp
  src/core/layer_tree.cpp
  src/core/pixel_buffer.cpp
  src/core/pixel_tools.cpp
  src/core/rect_utils.cpp
)
photoslop_configure_target(photoslop_core)

add_library(photoslop_render
  src/render/compositor.cpp
  src/render/tile_cache.cpp
)
target_link_libraries(photoslop_render PUBLIC photoslop_core)
photoslop_configure_target(photoslop_render)

add_library(photoslop_psd
  src/psd/psd_binary.cpp
  src/psd/psd_document_io.cpp
)
target_link_libraries(photoslop_psd PUBLIC photoslop_core photoslop_render)
photoslop_configure_target(photoslop_psd)

add_library(photoslop_plugins
  src/plugins/legacy_photoshop_adapter.cpp
  src/plugins/plugin_host.cpp
)
target_link_libraries(photoslop_plugins PUBLIC photoslop_core)
photoslop_configure_target(photoslop_plugins)

add_library(photoslop_filters
  src/filters/builtin_filters.cpp
  src/filters/filter_registry.cpp
)
target_link_libraries(photoslop_filters PUBLIC photoslop_core)
photoslop_configure_target(photoslop_filters)

add_library(photoslop_formats
  src/formats/format_registry.cpp
)
target_link_libraries(photoslop_formats PUBLIC photoslop_core photoslop_psd)
photoslop_configure_target(photoslop_formats)

add_library(photoslop_color
  src/color/color_management.cpp
)
target_link_libraries(photoslop_color PUBLIC photoslop_core)
photoslop_configure_target(photoslop_color)

if(PHOTOSLOP_BUILD_APP)
  find_package(Qt6 QUIET COMPONENTS Widgets)
  if(Qt6_FOUND)
    qt_standard_project_setup()
    add_library(photoslop_ui
      src/ui/blend_mode_ui.cpp
      src/ui/canvas_widget.cpp
      src/ui/color_panel.cpp
      src/ui/dialog_utils.cpp
      src/ui/edit_conversions.cpp
      src/ui/filter_workflows.cpp
      src/ui/image_document_io.cpp
      src/ui/layer_style_dialog.cpp
      src/ui/layer_list_widget.cpp
      src/ui/main_window.cpp
      src/ui/qt_geometry.cpp
    )
    target_link_libraries(photoslop_ui PUBLIC
      Qt6::Widgets
      photoslop_core
      photoslop_render
      photoslop_psd
      photoslop_plugins
      photoslop_filters
      photoslop_formats
      photoslop_color
    )
    target_compile_definitions(photoslop_ui PRIVATE PHOTOSLOP_VERSION="${PROJECT_VERSION}")
    photoslop_configure_target(photoslop_ui)

    add_executable(photoslop WIN32 MACOSX_BUNDLE
      src/app/main.cpp
    )
    target_link_libraries(photoslop PRIVATE photoslop_ui)
    photoslop_configure_target(photoslop)
    add_custom_command(TARGET photoslop POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              ${PROJECT_SOURCE_DIR}/test-fixtures
              $<TARGET_FILE_DIR:photoslop>/test-fixtures
      COMMENT "Copying Photoslop test fixtures next to the app binary"
    )
    install(TARGETS photoslop
      BUNDLE DESTINATION .
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  else()
    message(STATUS "Qt 6 Widgets was not found; skipping the native Photoslop app target.")
  endif()
endif()

if(PHOTOSLOP_BUILD_TESTS)
  add_executable(photoslop_core_tests tests/test_main.cpp)
  target_link_libraries(photoslop_core_tests PRIVATE
    photoslop_core
    photoslop_render
    photoslop_psd
    photoslop_plugins
    photoslop_filters
    photoslop_formats
    photoslop_color
  )
  photoslop_configure_target(photoslop_core_tests)
  target_compile_definitions(photoslop_core_tests PRIVATE PHOTOSLOP_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  add_test(NAME photoslop_core_tests COMMAND photoslop_core_tests)

  if(PHOTOSLOP_BUILD_APP AND Qt6_FOUND)
    add_executable(photoslop_ui_visual_tests
      tests/ui_visual_tests.cpp
    )
    target_link_libraries(photoslop_ui_visual_tests PRIVATE photoslop_ui)
    photoslop_configure_target(photoslop_ui_visual_tests)
    add_test(NAME photoslop_ui_visual_tests COMMAND photoslop_ui_visual_tests)
    set_tests_properties(photoslop_ui_visual_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
endif()
