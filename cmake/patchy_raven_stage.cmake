# Stages a built Raven beside patchy (cmake/patchy_raven.cmake): run with -P and
# BUILD (Raven's build tree), SRC (its source), DEST, PYTHON, STRIP, COMMIT and URL.
# The file list is Raven's own: runtime_paths() of its scripts/package-native.py, the set
# its release archives ship (raven, raven-bootstrap, the guest components and system
# programs, x86/, root certificates, fonts), so it follows Raven when the pin moves.
foreach(_var BUILD SRC DEST PYTHON COMMIT URL)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "patchy_raven_stage.cmake needs -D${_var}=")
  endif()
endforeach()

execute_process(
  COMMAND "${PYTHON}" -I -c [=[
import importlib.util, sys
from pathlib import Path
spec = importlib.util.spec_from_file_location("package_native", Path(sys.argv[1]) / "scripts" / "package-native.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
for name, path in sorted(module.runtime_paths(Path(sys.argv[2])).items()):
    print(f"{name}\t{path}")
]=] "${SRC}" "${BUILD}"
  OUTPUT_VARIABLE _listing
  RESULT_VARIABLE _result
  ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "Raven's runtime file list failed:\n${_error}")
endif()

file(REMOVE_RECURSE "${DEST}")
string(REPLACE "\n" ";" _lines "${_listing}")
set(_count 0)
foreach(_line IN LISTS _lines)
  if(_line STREQUAL "")
    continue()
  endif()
  string(REPLACE "\t" ";" _pair "${_line}")
  list(GET _pair 0 _name)
  list(GET _pair 1 _path)
  get_filename_component(_dir "${DEST}/${_name}" DIRECTORY)
  file(COPY "${_path}" DESTINATION "${_dir}" USE_SOURCE_PERMISSIONS)
  math(EXPR _count "${_count} + 1")
endforeach()
if(NOT EXISTS "${DEST}/raven" OR NOT EXISTS "${DEST}/raven-bootstrap")
  message(FATAL_ERROR "Raven's runtime list did not include raven and raven-bootstrap")
endif()
if(STRIP)
  execute_process(COMMAND "${STRIP}" --strip-debug "${DEST}/raven" COMMAND_ERROR_IS_FATAL ANY)
endif()

file(COPY "${SRC}/LICENSE" "${SRC}/LICENSES.md" "${SRC}/LICENSES" DESTINATION "${DEST}")
file(WRITE "${DEST}/RAVEN-SOURCE.txt"
     "Raven ${COMMIT}\nSource: ${URL} (commit ${COMMIT})\nBuilt by the Patchy build (cmake/patchy_raven.cmake); see NOTICE-THIRD-PARTY.md.\n")
message(STATUS "Raven staged in ${DEST} (${_count} runtime files)")
