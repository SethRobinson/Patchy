# Raven (https://github.com/mirkobrombin/raven, GPL-3.0-or-later), the runtime that runs the
# Win32 .8bf helper on Linux (docs/plugins.md, Linux). Its source is the git submodule
# third_party/raven, pinned to one commit and never modified; it is built here as an
# external project of the Patchy build, and staged in ${CMAKE_BINARY_DIR}/raven beside
# patchy: the runtime files Raven's own packager ships (raven, raven-bootstrap, guest
# components, system programs, x86/), LICENSE, LICENSES.md, LICENSES/ and RAVEN-SOURCE.txt (the source offer for the GPL).
# Raven is a separate program Patchy starts, never linked (NOTICE-THIRD-PARTY.md).
#
#   PATCHY_RAVEN            AUTO (build when the prerequisites are found), ON (require),
#                           OFF (do not bundle; the runner looks for a raven the user installed)
#   PATCHY_RAVEN_DIR        a Raven already staged elsewhere, bundled instead of building one
#   PATCHY_RAVEN_WORK_DIR   where the source, the sysroot and the build tree go
#   PATCHY_RAVEN_MINGW_SYSROOT  a folder holding x86_64-w64-mingw32/sys-root and
#                           i686-w64-mingw32/sys-root with MinGW-w64 13+ headers and CRT, used
#                           when the system's are older (the AppImage container sets it);
#                           without it podman fetches Fedora 45's
#
# Needs Python 3.11+, x86_64-w64-mingw32-gcc and the Wayland, xkbcommon, pixman and FreeType
# development packages (Vulkan and OpenSSL are optional for Raven). Raven needs MinGW-w64 headers 13 or newer; when
# the system's are older (EL10 ships 12, Ubuntu 22.04 ships 8), the system compilers use
# newer ones as their sysroot: PATCHY_RAVEN_MINGW_SYSROOT, or Fedora 45's copied into the
# work folder by podman. Raven is built with Patchy's own host compiler and a static
# libstdc++, so it needs no newer libstdc++ than the system has (the AppImage ABI audit).

get_filename_component(PATCHY_RAVEN_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../third_party/raven" ABSOLUTE)
set(PATCHY_RAVEN_URL "https://github.com/mirkobrombin/raven")
set(PATCHY_RAVEN_FEDORA_IMAGE "registry.fedoraproject.org/fedora:45")

set(PATCHY_RAVEN "AUTO" CACHE STRING "Bundle Raven for .8bf plug-ins: AUTO, ON or OFF")
set_property(CACHE PATCHY_RAVEN PROPERTY STRINGS AUTO ON OFF)
set(PATCHY_RAVEN_DIR "" CACHE PATH "A staged Raven to bundle instead of building one")
set(PATCHY_RAVEN_WORK_DIR "${CMAKE_BINARY_DIR}/raven-build" CACHE PATH
    "Work folder for the Raven build (source, sysroot, build tree)")
set(PATCHY_RAVEN_MINGW_SYSROOT "$ENV{PATCHY_RAVEN_MINGW_SYSROOT}" CACHE PATH
    "MinGW-w64 13+ sysroots (<arch>-w64-mingw32/sys-root) for Raven when the system's headers are older")

set(PATCHY_BUNDLED_RAVEN OFF)
set(_patchy_raven_stage "${CMAKE_BINARY_DIR}/raven")

function(_patchy_raven_unavailable reason)
  if(PATCHY_RAVEN STREQUAL "ON")
    message(FATAL_ERROR "Raven for .8bf plug-ins: ${reason}")
  endif()
  message(STATUS "Raven for .8bf plug-ins: not bundled (${reason})")
endfunction()

if(PATCHY_RAVEN STREQUAL "OFF")
  message(STATUS "Raven for .8bf plug-ins: not bundled (PATCHY_RAVEN=OFF)")
  return()
endif()

# A Raven staged elsewhere (a container build, a release download): copy it in.
if(PATCHY_RAVEN_DIR)
  if(NOT EXISTS "${PATCHY_RAVEN_DIR}/raven")
    _patchy_raven_unavailable("${PATCHY_RAVEN_DIR}/raven does not exist")
    return()
  endif()
  file(GLOB_RECURSE _patchy_raven_files CONFIGURE_DEPENDS "${PATCHY_RAVEN_DIR}/*")
  add_custom_command(
    OUTPUT "${_patchy_raven_stage}/raven"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${_patchy_raven_stage}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PATCHY_RAVEN_DIR}" "${_patchy_raven_stage}"
    DEPENDS ${_patchy_raven_files}
    COMMENT "Bundling Raven from ${PATCHY_RAVEN_DIR}"
    VERBATIM)
  add_custom_target(patchy_bundle_raven ALL DEPENDS "${_patchy_raven_stage}/raven")
  set(PATCHY_BUNDLED_RAVEN ON)
  message(STATUS "Raven for .8bf plug-ins: bundled from ${PATCHY_RAVEN_DIR}")
  return()
endif()

# Build it: check the prerequisites first, so AUTO degrades to "not bundled" cleanly.
if(NOT EXISTS "${PATCHY_RAVEN_SOURCE}/scripts/package-native.py")
  _patchy_raven_unavailable("the third_party/raven submodule is not checked out (git submodule update --init third_party/raven)")
  return()
endif()
# The pinned commit, for RAVEN-SOURCE.txt (the GPL source offer).
find_package(Git QUIET)
set(PATCHY_RAVEN_COMMIT "unknown")
if(GIT_EXECUTABLE)
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PATCHY_RAVEN_SOURCE}" rev-parse HEAD
                  OUTPUT_VARIABLE _patchy_raven_head OUTPUT_STRIP_TRAILING_WHITESPACE
                  RESULT_VARIABLE _patchy_raven_git ERROR_QUIET)
  if(_patchy_raven_git EQUAL 0)
    set(PATCHY_RAVEN_COMMIT "${_patchy_raven_head}")
  endif()
endif()
find_package(Python3 3.11 COMPONENTS Interpreter QUIET)
find_program(PATCHY_RAVEN_MINGW_GCC x86_64-w64-mingw32-gcc)
find_program(PATCHY_RAVEN_MINGW_GXX x86_64-w64-mingw32-g++)
find_program(PATCHY_RAVEN_STRIP strip)
if(NOT Python3_Interpreter_FOUND)
  _patchy_raven_unavailable("Python 3.11+ is missing")
  return()
endif()
if(NOT PATCHY_RAVEN_MINGW_GCC)
  _patchy_raven_unavailable("x86_64-w64-mingw32-gcc is missing (dnf install mingw64-gcc)")
  return()
endif()
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(_patchy_raven_host QUIET wayland-client xkbcommon pixman-1 freetype2)
endif()
if(NOT _patchy_raven_host_FOUND)
  _patchy_raven_unavailable("missing development packages (wayland-client, xkbcommon, pixman-1, freetype2)")
  return()
endif()

execute_process(
  COMMAND "${PATCHY_RAVEN_MINGW_GCC}" -E -dM -include _mingw.h -x c /dev/null
  OUTPUT_VARIABLE _patchy_mingw_defines ERROR_QUIET)
string(REGEX MATCH "#define __MINGW64_VERSION_MAJOR ([0-9]+)" _ "${_patchy_mingw_defines}")
set(_patchy_mingw_major "${CMAKE_MATCH_1}")

set(_patchy_raven_tool_path "$ENV{PATH}")
set(_patchy_raven_sysroot_step "")
if(NOT _patchy_mingw_major OR _patchy_mingw_major LESS 13)
  if(PATCHY_RAVEN_MINGW_SYSROOT)
    if(NOT EXISTS "${PATCHY_RAVEN_MINGW_SYSROOT}/x86_64-w64-mingw32/sys-root/mingw/include/_mingw.h")
      _patchy_raven_unavailable("PATCHY_RAVEN_MINGW_SYSROOT has no x86_64-w64-mingw32/sys-root/mingw/include/_mingw.h")
      return()
    endif()
    set(_patchy_raven_sysroot "${PATCHY_RAVEN_MINGW_SYSROOT}")
  else()
    find_program(PATCHY_RAVEN_PODMAN podman)
    if(NOT PATCHY_RAVEN_PODMAN)
      _patchy_raven_unavailable("MinGW-w64 headers ${_patchy_mingw_major} are older than 13 and podman is missing to fetch newer ones")
      return()
    endif()
    set(_patchy_raven_sysroot "${PATCHY_RAVEN_WORK_DIR}/sysroot")
    file(MAKE_DIRECTORY "${_patchy_raven_sysroot}")
  endif()
  # Wrappers that point the system compilers at the newer headers and CRT.
  set(_patchy_raven_wrap "${PATCHY_RAVEN_WORK_DIR}/wrap")
  file(MAKE_DIRECTORY "${_patchy_raven_wrap}")
  # The wrappers must exec the real compiler: a ccache shim (/usr/lib64/ccache) looks the
  # compiler up in PATH again, finds the wrapper first and loops forever. ccache, when
  # present, is called with the real compiler's absolute path instead, which it never
  # looks up.
  find_program(PATCHY_RAVEN_CCACHE ccache)
  string(REPLACE ":" ";" _patchy_path_dirs "$ENV{PATH}")
  foreach(_arch x86_64 i686)
    foreach(_tool gcc g++)
      set(_real "")
      foreach(_dir IN LISTS _patchy_path_dirs)
        set(_candidate "${_dir}/${_arch}-w64-mingw32-${_tool}")
        if(_dir STREQUAL _patchy_raven_wrap OR NOT EXISTS "${_candidate}" OR IS_DIRECTORY "${_candidate}")
          continue()
        endif()
        file(REAL_PATH "${_candidate}" _resolved)
        get_filename_component(_resolved_name "${_resolved}" NAME)
        if(_resolved_name MATCHES "ccache")
          continue()
        endif()
        set(_real "${_candidate}")
        break()
      endforeach()
      if(_real)
        set(_launch "${_real}")
        if(PATCHY_RAVEN_CCACHE)
          set(_launch "${PATCHY_RAVEN_CCACHE} ${_real}")
        endif()
        file(WRITE "${_patchy_raven_wrap}/${_arch}-w64-mingw32-${_tool}"
             "#!/bin/sh\nexec ${_launch} --sysroot=${_patchy_raven_sysroot}/${_arch}-w64-mingw32/sys-root \"$@\"\n")
        file(CHMOD "${_patchy_raven_wrap}/${_arch}-w64-mingw32-${_tool}"
             PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
      endif()
    endforeach()
  endforeach()
  set(_patchy_raven_tool_path "${_patchy_raven_wrap}:$ENV{PATH}")
  if(PATCHY_RAVEN_MINGW_SYSROOT)
    set(_patchy_raven_headers "MinGW-w64 headers from ${PATCHY_RAVEN_MINGW_SYSROOT} (system has ${_patchy_mingw_major})")
  else()
    set(_patchy_raven_sysroot_step
      COMMAND sh -c "test -d '${_patchy_raven_sysroot}/x86_64-w64-mingw32/sys-root' || '${PATCHY_RAVEN_PODMAN}' run --rm -v '${_patchy_raven_sysroot}:/out:Z' '${PATCHY_RAVEN_FEDORA_IMAGE}' bash -c 'dnf install -y -q --setopt=install_weak_deps=False mingw64-headers mingw64-crt mingw64-winpthreads mingw64-winpthreads-static mingw32-headers mingw32-crt mingw32-winpthreads mingw32-winpthreads-static >/dev/null && cp -a /usr/x86_64-w64-mingw32 /usr/i686-w64-mingw32 /out/'")
    set(_patchy_raven_headers "Fedora 45's MinGW-w64 headers (system has ${_patchy_mingw_major})")
  endif()
else()
  set(_patchy_raven_headers "system MinGW-w64 ${_patchy_mingw_major} headers")
endif()

include(ExternalProject)
# Every stage uses all logical CPUs: Raven's provisioning scripts are pinned to two
# compiler threads upstream, so they run through patchy_raven_parallel.py, which sizes their
# thread pool to the CPU count without touching the submodule; its ninja gets an explicit -j.
cmake_host_system_information(RESULT _patchy_raven_jobs QUERY NUMBER_OF_LOGICAL_CORES)
set(_patchy_raven_env ${CMAKE_COMMAND} -E env "PATH=${_patchy_raven_tool_path}")
set(_patchy_raven_components "${PATCHY_RAVEN_WORK_DIR}/components")
# Patchy's host compilers (gcc-12 in the AppImage container), and a static libstdc++ where
# the toolchain has one (Ubuntu's libstdc++-dev does; EL10 needs libstdc++-static), so a
# Raven for the AppImage needs no newer libstdc++ than 22.04's. Elsewhere it links the
# system libstdc++, which is fine for a build that runs where it was made.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_LINK_OPTIONS -static-libstdc++ -static-libgcc)
check_cxx_source_compiles("#include <string>\nint main() { return static_cast<int>(std::string(\"x\").size()) - 1; }"
                          PATCHY_RAVEN_STATIC_LIBSTDCXX)
unset(CMAKE_REQUIRED_LINK_OPTIONS)
set(_patchy_raven_host_args "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
if(PATCHY_RAVEN_STATIC_LIBSTDCXX)
  list(APPEND _patchy_raven_host_args "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc")
endif()
if(CMAKE_C_COMPILER)
  list(APPEND _patchy_raven_host_args "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
endif()
ExternalProject_Add(patchy_raven
  PREFIX "${PATCHY_RAVEN_WORK_DIR}"
  SOURCE_DIR "${PATCHY_RAVEN_SOURCE}"
  BINARY_DIR "${PATCHY_RAVEN_WORK_DIR}/build"
  DOWNLOAD_COMMAND ""
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND
    ${_patchy_raven_sysroot_step}
    COMMAND ${_patchy_raven_env} "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/patchy_raven_parallel.py"
            <SOURCE_DIR>/scripts/provision-rtl.py
            --output "${_patchy_raven_components}/rtl"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_patchy_raven_components}"
    # dlltool writes temporary files into the working folder: never the (read-only) source.
    COMMAND ${CMAKE_COMMAND} -E chdir "${_patchy_raven_components}"
            ${_patchy_raven_env} "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/patchy_raven_parallel.py"
            <SOURCE_DIR>/scripts/provision-controls.py
            --output "${_patchy_raven_components}/controls"
    COMMAND ${_patchy_raven_env} ${CMAKE_COMMAND} -S <SOURCE_DIR> -B <BINARY_DIR> -G Ninja
            -DCMAKE_BUILD_TYPE=Release
            ${_patchy_raven_host_args}
            # Unused by Raven: a new submodule commit changes this command, so the configure,
            # build and staging steps run again.
            "-DPATCHY_RAVEN_PINNED_COMMIT=${PATCHY_RAVEN_COMMIT}" --no-warn-unused-cli
            "-DPython3_EXECUTABLE=${Python3_EXECUTABLE}"
            "-DRAVEN_RTL_COMPONENT_PATH=${_patchy_raven_components}/rtl/raven-rtl.dll"
            "-DRAVEN_CONTROLS_COMPONENT_DIR=${_patchy_raven_components}/controls"
  BUILD_COMMAND ${_patchy_raven_env} ${CMAKE_COMMAND} --build <BINARY_DIR> --target raven -j ${_patchy_raven_jobs}
  INSTALL_COMMAND ${CMAKE_COMMAND}
    "-DBUILD=<BINARY_DIR>" "-DSRC=<SOURCE_DIR>" "-DDEST=${_patchy_raven_stage}"
    "-DPYTHON=${Python3_EXECUTABLE}"
    "-DSTRIP=${PATCHY_RAVEN_STRIP}" "-DCOMMIT=${PATCHY_RAVEN_COMMIT}" "-DURL=${PATCHY_RAVEN_URL}"
    -P "${CMAKE_CURRENT_LIST_DIR}/patchy_raven_stage.cmake"
  BUILD_BYPRODUCTS "${_patchy_raven_stage}/raven"
  USES_TERMINAL_CONFIGURE ON
  USES_TERMINAL_BUILD ON)
set(PATCHY_BUNDLED_RAVEN ON)
message(STATUS "Raven for .8bf plug-ins: built from ${PATCHY_RAVEN_COMMIT} with ${_patchy_raven_headers} in ${PATCHY_RAVEN_WORK_DIR}")
