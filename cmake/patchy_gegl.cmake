# Patchy's own GEGL (docs/gegl-filters.md, "Bundled GEGL"): the libraries, the operation
# modules Patchy offers, babl's extensions, GEGL's translations and their licenses, built by
# scripts/dev/gegl/ into <deps>/gegl/<version>/<platform> and copied once into <build>/gegl,
# where patchy-gegl-host finds them beside the program (bundled_gegl_library). A build
# without the folder offers no GEGL filters (the Filter menu says so).

option(PATCHY_REQUIRE_GEGL "Fail the configure when Patchy's own GEGL bundle is not found (release packaging)" OFF)
set(PATCHY_GEGL_BUNDLE_VERSION "0.4.72")
if(WIN32)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
    set(PATCHY_GEGL_BUNDLE_PLATFORM "win-arm64")
  else()
    set(PATCHY_GEGL_BUNDLE_PLATFORM "win-x64")
  endif()
  set(PATCHY_GEGL_BUNDLE_LIBRARY "bin/libgegl-0.4-0.dll")
elseif(APPLE)
  set(PATCHY_GEGL_BUNDLE_PLATFORM "osx-arm64")
  set(PATCHY_GEGL_BUNDLE_LIBRARY "lib/libgegl-0.4.0.dylib")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(PATCHY_GEGL_BUNDLE_PLATFORM "linux-aarch64")
  set(PATCHY_GEGL_BUNDLE_LIBRARY "lib/libgegl-0.4.so.0")
else()
  set(PATCHY_GEGL_BUNDLE_PLATFORM "linux-x64")
  set(PATCHY_GEGL_BUNDLE_LIBRARY "lib/libgegl-0.4.so.0")
endif()

set(PATCHY_GEGL_BUNDLE_DIR "" CACHE PATH "Patchy's own GEGL bundle (empty: found under .deps/gegl)")
if(NOT PATCHY_GEGL_BUNDLE_DIR AND NOT EMSCRIPTEN)
  foreach(candidate IN LISTS PATCHY_AI_DEPS_CANDIDATES)
    set(dir "${candidate}/gegl/${PATCHY_GEGL_BUNDLE_VERSION}/${PATCHY_GEGL_BUNDLE_PLATFORM}")
    if(EXISTS "${dir}/${PATCHY_GEGL_BUNDLE_LIBRARY}")
      set(PATCHY_GEGL_BUNDLE_DIR_FOUND "${dir}")
      break()
    endif()
  endforeach()
else()
  set(PATCHY_GEGL_BUNDLE_DIR_FOUND "${PATCHY_GEGL_BUNDLE_DIR}")
endif()

if(PATCHY_GEGL_BUNDLE_DIR_FOUND)
  message(STATUS "Patchy's own GEGL: ${PATCHY_GEGL_BUNDLE_DIR_FOUND}")
  file(GLOB_RECURSE PATCHY_GEGL_BUNDLE_FILES LIST_DIRECTORIES false CONFIGURE_DEPENDS
       "${PATCHY_GEGL_BUNDLE_DIR_FOUND}/*")
  set(PATCHY_GEGL_BUNDLE_STAMP "${CMAKE_BINARY_DIR}/gegl-bundle.stamp")
  # One copy into the shared output folder (docs/code-organization.md: copy-once targets);
  # the folder is replaced whole so a file the bundle dropped does not linger.
  add_custom_command(
    OUTPUT "${PATCHY_GEGL_BUNDLE_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/gegl"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PATCHY_GEGL_BUNDLE_DIR_FOUND}" "${CMAKE_BINARY_DIR}/gegl"
    COMMAND ${CMAKE_COMMAND} -E touch "${PATCHY_GEGL_BUNDLE_STAMP}"
    DEPENDS ${PATCHY_GEGL_BUNDLE_FILES}
    COMMENT "Copying Patchy's own GEGL into the build directory"
  )
  add_custom_target(patchy_gegl_bundle DEPENDS "${PATCHY_GEGL_BUNDLE_STAMP}")
elseif(PATCHY_REQUIRE_GEGL)
  message(FATAL_ERROR "Patchy's own GEGL ${PATCHY_GEGL_BUNDLE_VERSION} (${PATCHY_GEGL_BUNDLE_PLATFORM}) was not found, "
                      "and PATCHY_REQUIRE_GEGL is ON: build it with scripts/dev/gegl/ or set PATCHY_GEGL_BUNDLE_DIR")
else()
  message(STATUS "Patchy's own GEGL: not found, so no GEGL filters (build it with scripts/dev/gegl/)")
endif()
