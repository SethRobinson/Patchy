# Moves a locked executable out of the linker's way. A running process keeps its
# image file open on Windows, so relinking it fails with LNK1104, but the file
# can still be RENAMED and the process keeps running the renamed copy. The
# patchy-mcp target runs this PRE_LINK because MCP clients (the Codex app) keep
# one connector alive per thread for as long as the thread exists, and killing
# them is never allowed (AGENTS.md). Stale copies left by earlier builds are
# deleted once nothing runs them; one still in use stays for a later sweep.
#
# Usage: cmake -DPATCHY_LOCKED_EXECUTABLE=<path> -P cmake/unlock_locked_executable.cmake
if(NOT PATCHY_LOCKED_EXECUTABLE)
  message(FATAL_ERROR "PATCHY_LOCKED_EXECUTABLE was not passed with -D.")
endif()
get_filename_component(_patchy_unlock_dir "${PATCHY_LOCKED_EXECUTABLE}" DIRECTORY)
get_filename_component(_patchy_unlock_stem "${PATCHY_LOCKED_EXECUTABLE}" NAME_WE)
get_filename_component(_patchy_unlock_ext "${PATCHY_LOCKED_EXECUTABLE}" EXT)

# Sweep stale copies that nothing runs any more. file(REMOVE) fails silently on
# a file that is still mapped by a process, so "still exists" means "still in use".
file(GLOB _patchy_unlock_stale
     "${_patchy_unlock_dir}/${_patchy_unlock_stem}.stale-*${_patchy_unlock_ext}")
foreach(_patchy_unlock_old IN LISTS _patchy_unlock_stale)
  file(REMOVE "${_patchy_unlock_old}")
  if(EXISTS "${_patchy_unlock_old}")
    message(STATUS "${_patchy_unlock_old} is still running; keeping it for a later sweep")
  endif()
endforeach()

if(NOT EXISTS "${PATCHY_LOCKED_EXECUTABLE}")
  return()
endif()

# Probe the lock the way the linker's own overwrite would: an unlocked file is
# simply removed (the link recreates it), a locked one survives and moves aside.
file(REMOVE "${PATCHY_LOCKED_EXECUTABLE}")
if(NOT EXISTS "${PATCHY_LOCKED_EXECUTABLE}")
  return()
endif()
string(TIMESTAMP _patchy_unlock_stamp "%Y%m%d-%H%M%S")
set(_patchy_unlock_aside
    "${_patchy_unlock_dir}/${_patchy_unlock_stem}.stale-${_patchy_unlock_stamp}${_patchy_unlock_ext}")
file(RENAME "${PATCHY_LOCKED_EXECUTABLE}" "${_patchy_unlock_aside}" RESULT _patchy_unlock_result)
if(NOT _patchy_unlock_result EQUAL 0)
  message(WARNING "${PATCHY_LOCKED_EXECUTABLE} is locked and could not be moved aside "
                  "(${_patchy_unlock_result}); the link will fail with LNK1104")
else()
  message(STATUS "Moved the running ${_patchy_unlock_stem}${_patchy_unlock_ext} aside as "
                 "${_patchy_unlock_aside} so the link can proceed")
endif()
