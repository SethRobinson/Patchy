# Shared incremental build rules. Kept Qt/compiler independent so the build-graph
# tests exercise the same rules with small custom-command fixtures.
include_guard(GLOBAL)

function(patchy_write_input_manifest path)
  set(inputs ${ARGN})
  list(REMOVE_DUPLICATES inputs)
  list(SORT inputs)
  string(JOIN "\n" contents ${inputs})
  file(GENERATE OUTPUT "${path}" CONTENT "${contents}\n")
endfunction()

# Writes a configure-time generated file only when its content changed. file(WRITE) always
# touches the file, so every reconfigure (every remote build runs `cmake --preset`) would
# recompile everything that includes it: about 220 vendored C sources for the codec config
# headers.
function(patchy_write_if_different path content)
  if(EXISTS "${path}")
    file(READ "${path}" existing)
    if(existing STREQUAL content)
      return()
    endif()
  endif()
  file(WRITE "${path}" "${content}")
endfunction()

function(patchy_add_success_check target)
  cmake_parse_arguments(CHECK "" "STAMP;COMMENT;WORKING_DIRECTORY" "COMMAND;DEPENDS" ${ARGN})
  if(NOT CHECK_STAMP OR NOT CHECK_COMMAND)
    message(FATAL_ERROR "${target}: STAMP and COMMAND are required")
  endif()
  if(NOT CHECK_WORKING_DIRECTORY)
    set(CHECK_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  endif()
  add_custom_command(
    OUTPUT "${CHECK_STAMP}"
    # A failed recheck must not leave the previous successful result behind.
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${CHECK_STAMP}"
    COMMAND ${CHECK_COMMAND}
    COMMAND "${CMAKE_COMMAND}" -E touch "${CHECK_STAMP}"
    DEPENDS ${CHECK_DEPENDS}
    WORKING_DIRECTORY "${CHECK_WORKING_DIRECTORY}"
    COMMENT "${CHECK_COMMENT}"
    VERBATIM
  )
  add_custom_target(${target} DEPENDS "${CHECK_STAMP}")
endfunction()

function(patchy_setup_build_stamp)
  # The list itself is an input: removing an input must invalidate the stamp just
  # as adding or editing one does. Do not use Git status or the wall clock here.
  file(GLOB_RECURSE stamp_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
    "${PROJECT_SOURCE_DIR}/src/*"
    "${PROJECT_SOURCE_DIR}/cmake/*"
    "${PROJECT_SOURCE_DIR}/scripts/bundled/*"
    "${PROJECT_SOURCE_DIR}/translations/*.ts")
  # QRC entries can refer outside src (the branding SVG does). Track those
  # source files, and reconfigure if a QRC changes its reference list.
  file(GLOB_RECURSE qrc_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/*.qrc")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${qrc_sources})
  foreach(qrc IN LISTS qrc_sources)
    file(READ "${qrc}" qrc_text)
    string(REGEX MATCHALL "<file[^>]*>[^<]+</file>" qrc_entries "${qrc_text}")
    get_filename_component(qrc_dir "${qrc}" DIRECTORY)
    foreach(entry IN LISTS qrc_entries)
      string(REGEX REPLACE "^<file[^>]*>([^<]+)</file>$" "\\1" resource "${entry}")
      get_filename_component(resource "${resource}" ABSOLUTE BASE_DIR "${qrc_dir}")
      list(APPEND stamp_inputs "${resource}")
    endforeach()
  endforeach()
  if(APPLE)
    file(GLOB platform_resources CONFIGURE_DEPENDS
      "${PROJECT_SOURCE_DIR}/packaging/macos/*.icns"
      "${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in")
  elseif(EMSCRIPTEN)
    file(GLOB_RECURSE platform_resources CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
      "${PROJECT_SOURCE_DIR}/packaging/web/*"
      "${PROJECT_SOURCE_DIR}/scripts/wasm/*.js")
  elseif(UNIX)
    file(GLOB_RECURSE platform_resources CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
      "${PROJECT_SOURCE_DIR}/packaging/linux/icons/*"
      "${PROJECT_SOURCE_DIR}/packaging/linux/*.desktop"
      "${PROJECT_SOURCE_DIR}/packaging/linux/*.metainfo.xml")
  endif()
  if(NOT EMSCRIPTEN)
    file(GLOB_RECURSE bundled_plugins CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
      "${PROJECT_SOURCE_DIR}/test-fixtures/photoshop-plugins/*")
    list(APPEND stamp_inputs ${bundled_plugins})
  endif()
  list(APPEND stamp_inputs "${PROJECT_SOURCE_DIR}/CMakeLists.txt")
  if(EXISTS "${PROJECT_SOURCE_DIR}/CMakePresets.json")
    list(APPEND stamp_inputs "${PROJECT_SOURCE_DIR}/CMakePresets.json")
  endif()
  list(APPEND stamp_inputs ${platform_resources} ${PATCHY_BUNDLED_FONT_FILES}
    ${PATCHY_AGENT_KIT_FILES} ${PATCHY_QT_SOURCE_RUNTIME_INPUTS}
    ${PATCHY_AI_MODEL_SOURCES} ${PATCHY_AI_WEB_MODEL_SOURCES}
    ${PATCHY_AI_WEB_RUNTIME_SOURCES} ${PATCHY_ONNXRUNTIME_RUNTIME_FILES})
  foreach(input IN ITEMS "${PROJECT_SOURCE_DIR}/LICENSE"
      "${PROJECT_SOURCE_DIR}/NOTICE-THIRD-PARTY.md" "${PATCHY_AI_LICENSE_FILE}"
      "${PATCHY_LIBOMP_DYLIB}" "${CMAKE_TOOLCHAIN_FILE}" "${QT_CHAINLOAD_TOOLCHAIN_FILE}")
    if(input AND EXISTS "${input}")
      list(APPEND stamp_inputs "${input}")
    endif()
  endforeach()
  if(PATCHY_AI_SELECTION_BUILT)
    list(APPEND stamp_inputs "${PATCHY_ONNXRUNTIME_DIR}/LICENSE"
      "${PATCHY_ONNXRUNTIME_DIR}/ThirdPartyNotices.txt")
  endif()
  list(REMOVE_DUPLICATES stamp_inputs)
  list(SORT stamp_inputs)
  string(JOIN "\n" stamp_manifest ${stamp_inputs})
  # Flags/options can rebuild binaries without changing a source file. Record
  # their values without depending on CMakeCache.txt's configure-time mtime.
  string(APPEND stamp_manifest "\nbuild_type=${CMAKE_BUILD_TYPE}\ncompiler=${CMAKE_CXX_COMPILER}\ncompiler_version=${CMAKE_CXX_COMPILER_VERSION}\n")
  get_cmake_property(cache_variables CACHE_VARIABLES)
  list(SORT cache_variables)
  foreach(variable IN LISTS cache_variables)
    if(variable MATCHES "^(PATCHY_|CMAKE_(C|CXX|EXE_LINKER|SHARED_LINKER|STATIC_LINKER)_FLAGS|CMAKE_OSX_|CMAKE_(C|CXX)_COMPILER|CMAKE_TOOLCHAIN_FILE$|CMAKE_SYSROOT|CMAKE_SYSTEM_|CMAKE_MSVC_RUNTIME_LIBRARY$|CMAKE_POSITION_INDEPENDENT_CODE$|CMAKE_INTERPROCEDURAL_OPTIMIZATION|CMAKE_PREFIX_PATH$|CMAKE_FIND_ROOT_PATH|QT_HOST_PATH$|QT_CHAINLOAD_TOOLCHAIN_FILE$|Qt6.*_DIR$)")
      string(APPEND stamp_manifest "${variable}=${${variable}}\n")
    endif()
  endforeach()
  set(stamp_dir "${CMAKE_BINARY_DIR}/generated")
  file(MAKE_DIRECTORY "${stamp_dir}")
  set(stamp_manifest_path "${stamp_dir}/patchy_build_inputs.txt")
  set(stamp_refresh "${stamp_dir}/patchy_build_refresh.txt")
  file(GENERATE OUTPUT "${stamp_manifest_path}" CONTENT "${stamp_manifest}")
  file(GENERATE OUTPUT "${stamp_refresh}" CONTENT "Explicit packaging refresh request.\n")
  set(stamp_header "${stamp_dir}/patchy_build_stamp.hpp")
  set(stamp_writer "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/write_build_stamp.cmake")
  add_custom_command(
    OUTPUT "${stamp_header}"
    COMMAND "${CMAKE_COMMAND}" "-DPATCHY_BUILD_STAMP_HEADER=${stamp_header}" -P "${stamp_writer}"
    DEPENDS ${stamp_inputs} "${stamp_manifest_path}" "${stamp_refresh}" "${stamp_writer}"
    COMMENT "Stamping changed build inputs"
    VERBATIM
  )
  add_custom_target(patchy_build_stamp DEPENDS "${stamp_header}")
  # Packagers run this explicitly before their normal build. No BYPRODUCTS:
  # making the always-run target a producer would dirty every ordinary build.
  add_custom_target(patchy_refresh_build_stamp
    COMMAND "${CMAKE_COMMAND}" -E touch "${stamp_refresh}"
    COMMENT "Requesting a fresh release build timestamp"
    VERBATIM
  )
  set(PATCHY_BUILD_STAMP_HEADER "${stamp_header}" PARENT_SCOPE)
endfunction()

# The Qt-free test suite: standard library only. Its 75 TUs averaged 22 s each without a
# PCH, half of them spent in <filesystem>, <iostream> and the containers every test uses.
function(patchy_enable_core_pch target)
  if(NOT PATCHY_USE_PCH OR EMSCRIPTEN)
    return()
  endif()
  target_precompile_headers(${target} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:<algorithm$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<array$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<chrono$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<cmath$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<cstdint$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<cstring$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<filesystem$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<fstream$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<functional$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<iostream$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<map$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<memory$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<optional$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<span$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<sstream$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<string$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<string_view$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<unordered_map$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<utility$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<vector$<ANGLE-R>>"
  )
  get_target_property(sources ${target} SOURCES)
  foreach(source IN LISTS sources)
    foreach(property COMPILE_DEFINITIONS COMPILE_OPTIONS INCLUDE_DIRECTORIES)
      get_source_file_property(value "${source}" ${property})
      if(value)
        set_source_files_properties("${source}" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
      endif()
    endforeach()
  endforeach()
endfunction()

function(patchy_enable_ui_pch target)
  if(NOT PATCHY_USE_PCH OR EMSCRIPTEN)
    return()
  endif()
  # Only stable external headers. Project headers would make a small edit
  # invalidate the PCH and every translation unit using it.
  target_precompile_headers(${target} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:<algorithm$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<array$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<functional$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<memory$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<optional$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<string$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<utility$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<vector$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<QByteArray$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<QString$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<QObject$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<QImage$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<QWidget$<ANGLE-R>>"
  )
  get_target_property(sources ${target} SOURCES)
  foreach(source IN LISTS sources)
    foreach(property COMPILE_DEFINITIONS COMPILE_OPTIONS INCLUDE_DIRECTORIES)
      get_source_file_property(value "${source}" ${property})
      if(value)
        # MSVC requires PCH flags/defines to agree with the consuming source.
        set_source_files_properties("${source}" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
      endif()
    endforeach()
  endforeach()
endfunction()
