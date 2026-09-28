# The out-of-process host for classic Photoshop filter plug-ins. Qt-free Win32.
# The main build adds this directory for the 64-bit helper and configures it a
# second time, as its own project inside a 32-bit developer environment, for the
# 32-bit helper (see the patchy_8bf_host32 external project in the root
# CMakeLists.txt). PATCHY_8BF_HOST_NAME picks the executable name.

cmake_minimum_required(VERSION 3.24)
project(patchy_8bf_host LANGUAGES CXX)

if(NOT DEFINED PATCHY_8BF_HOST_NAME)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PATCHY_8BF_HOST_NAME patchy-8bf-host64)
  else()
    set(PATCHY_8BF_HOST_NAME patchy-8bf-host32)
  endif()
endif()

add_executable(${PATCHY_8BF_HOST_NAME}
  main.cpp
  filter_runner.cpp
  filter_runner.hpp
  host_protocol.hpp
  legacy_filter_abi.hpp
  screen_shim.cpp
  screen_shim.hpp
)
target_compile_features(${PATCHY_8BF_HOST_NAME} PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(${PATCHY_8BF_HOST_NAME} PRIVATE /W4 /permissive- /utf-8)
  # Console subsystem with wmain; Patchy starts it without a window.
  target_link_options(${PATCHY_8BF_HOST_NAME} PRIVATE /SUBSYSTEM:CONSOLE)
endif()
target_link_libraries(${PATCHY_8BF_HOST_NAME} PRIVATE user32 gdi32 comdlg32 shell32)
set_target_properties(${PATCHY_8BF_HOST_NAME} PROPERTIES OUTPUT_NAME ${PATCHY_8BF_HOST_NAME})
if(DEFINED PATCHY_8BF_HOST_OUTPUT_DIR)
  set_target_properties(${PATCHY_8BF_HOST_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PATCHY_8BF_HOST_OUTPUT_DIR}")
endif()
