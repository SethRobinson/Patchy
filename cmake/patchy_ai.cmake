# Automatic selection (docs/ai-selection.md): the patchy_ai library, ONNX Runtime, and the
# bundled models. scripts/setup-ai-deps.ps1 / .sh provision both into .deps; a git worktree
# uses its main checkout's .deps. Flatpak and other packagers pass PATCHY_ONNXRUNTIME_DIR
# and PATCHY_AI_MODELS_DIR explicitly. Without them the stub engine builds and the feature
# reports itself unavailable; PATCHY_REQUIRE_AI_SELECTION turns that into a configure error
# (release builds set it).
#
# The threaded wasm app instead runs MobileSAM in the page's onnxruntime-web
# (segmentation_engine_web.cpp plus scripts/wasm/ai-host-pre.js): the npm package and the two
# Object Selection models are staged beside patchy.html, never packed into patchy.data, and
# the page fetches them on first use. wasm-core (no page) and the single-threaded app (no
# worker can wait for the page) keep the stub.

option(PATCHY_ENABLE_AI_SELECTION "Build automatic selection (ONNX Runtime and bundled models)" ON)
option(PATCHY_REQUIRE_AI_SELECTION "Fail the configure when automatic selection cannot be built" OFF)
# Select Subject, Select Background and Remove Background (the BiRefNet model) are held back
# pending patent review (US 12051225; docs/legal-constraints.md). Off: the model is neither
# required, staged nor packaged, and the commands are left out. Object Selection is unaffected.
option(PATCHY_ENABLE_SELECT_SUBJECT "Ship Select Subject, Select Background and Remove Background" OFF)

# Keep in sync with scripts/setup-ai-deps.ps1 and scripts/setup-ai-deps.sh.
set(PATCHY_ONNXRUNTIME_VERSION "1.30.0")
set(PATCHY_ONNXRUNTIME_WEB_VERSION "1.30.0")
# The staged folder carries the version, so a new release never meets a cached older ORT.
set(PATCHY_ONNXRUNTIME_WEB_FOLDER "onnxruntime-web-${PATCHY_ONNXRUNTIME_WEB_VERSION}")
set(PATCHY_ONNXRUNTIME_WEB_FILES ort.wasm.min.mjs ort-wasm-simd-threaded.mjs ort-wasm-simd-threaded.wasm
                                 LICENSE ThirdPartyNotices.txt)
set(PATCHY_AI_MODEL_FILES
  mobile_sam_image_encoder_int8.onnx
  sam_mask_decoder_multi_int8.onnx
)
if(PATCHY_ENABLE_SELECT_SUBJECT)
  list(APPEND PATCHY_AI_MODEL_FILES birefnet_lite_general_1024_int8.onnx)
endif()
# Object Selection's High quality model (EfficientViT-SAM XL0, 234 MB) is never packaged:
# desktop users download it on first use into their app-data folder (docs/ai-selection.md).
# When setup-ai-deps provisioned it, the build stages it into <build>/ai-models-download, the
# stand-in for that folder that tests point the engine at. Keep in sync with
# src/ui/object_selection_model_download.cpp, which pins the same files.
set(PATCHY_AI_DOWNLOAD_MODEL_FILES
  efficientvit_sam_xl0_encoder_fp16.onnx
  efficientvit_sam_xl0_decoder_multi_fp16.onnx
)
# The TrustMark watermark models (134.6 MB, docs/watermarks.md) are the other download, staged
# the same way on their own (all or none). Keep in sync with src/ui/watermark_model_download.cpp
# and trustmark_model_files() in src/ai/trustmark_watermark.cpp.
set(PATCHY_AI_WATERMARK_MODEL_FILES
  trustmark_decoder_Q.onnx
  trustmark_decoder_P.onnx
  trustmark_decoder_C.onnx
  trustmark_encoder_Q.onnx
)

set(PATCHY_AI_DEPS_CANDIDATES "${PROJECT_SOURCE_DIR}/.deps")
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" rev-parse --path-format=absolute --git-common-dir
    OUTPUT_VARIABLE PATCHY_GIT_COMMON_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE PATCHY_GIT_COMMON_DIR_RESULT)
  if(PATCHY_GIT_COMMON_DIR_RESULT EQUAL 0 AND PATCHY_GIT_COMMON_DIR)
    get_filename_component(PATCHY_MAIN_CHECKOUT "${PATCHY_GIT_COMMON_DIR}" DIRECTORY)
    list(APPEND PATCHY_AI_DEPS_CANDIDATES "${PATCHY_MAIN_CHECKOUT}/.deps")
  endif()
endif()

if(WIN32)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
    set(PATCHY_ONNXRUNTIME_PLATFORM "win-arm64")
  else()
    set(PATCHY_ONNXRUNTIME_PLATFORM "win-x64")
  endif()
elseif(APPLE)
  set(PATCHY_ONNXRUNTIME_PLATFORM "osx-arm64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(PATCHY_ONNXRUNTIME_PLATFORM "linux-aarch64")
else()
  set(PATCHY_ONNXRUNTIME_PLATFORM "linux-x64")
endif()

if(NOT PATCHY_ONNXRUNTIME_DIR)
  foreach(candidate IN LISTS PATCHY_AI_DEPS_CANDIDATES)
    set(dir "${candidate}/onnxruntime/${PATCHY_ONNXRUNTIME_VERSION}/${PATCHY_ONNXRUNTIME_PLATFORM}")
    if(EXISTS "${dir}/include/onnxruntime_cxx_api.h")
      set(PATCHY_ONNXRUNTIME_DIR "${dir}")
      break()
    endif()
  endforeach()
endif()
if(EMSCRIPTEN AND NOT PATCHY_ONNXRUNTIME_WEB_DIR)
  foreach(candidate IN LISTS PATCHY_AI_DEPS_CANDIDATES)
    set(dir "${candidate}/onnxruntime-web/${PATCHY_ONNXRUNTIME_WEB_VERSION}")
    if(EXISTS "${dir}/ort.wasm.min.mjs")
      set(PATCHY_ONNXRUNTIME_WEB_DIR "${dir}")
      break()
    endif()
  endforeach()
endif()
if(NOT PATCHY_AI_MODELS_DIR)
  foreach(candidate IN LISTS PATCHY_AI_DEPS_CANDIDATES)
    if(EXISTS "${candidate}/ai-models/mobile_sam_image_encoder_int8.onnx")
      set(PATCHY_AI_MODELS_DIR "${candidate}/ai-models")
      break()
    endif()
  endforeach()
endif()

# PATCHY_AI_SELECTION_BUILT is the native engine only (it drives the bundle and install
# rules, and Emscripten counts as UNIX); PATCHY_AI_WEB_BUILT is the wasm app's engine.
set(PATCHY_AI_SELECTION_BUILT OFF)
set(PATCHY_AI_WEB_BUILT OFF)
set(PATCHY_AI_MISSING "")
if(EMSCRIPTEN)
  if(NOT PATCHY_BUILD_APP)
    set(PATCHY_AI_MISSING "wasm-core has no page to run onnxruntime-web")
  elseif(PATCHY_WASM_SINGLETHREAD)
    set(PATCHY_AI_MISSING "the single-threaded wasm app has no worker to wait for the page")
  elseif(NOT PATCHY_ENABLE_AI_SELECTION)
    set(PATCHY_AI_MISSING "PATCHY_ENABLE_AI_SELECTION is OFF")
  elseif(NOT PATCHY_ONNXRUNTIME_WEB_DIR)
    set(PATCHY_AI_MISSING
        "onnxruntime-web ${PATCHY_ONNXRUNTIME_WEB_VERSION} not found (run scripts/setup-ai-deps -Web)")
  elseif(NOT PATCHY_AI_MODELS_DIR)
    set(PATCHY_AI_MISSING "models not found (run scripts/setup-ai-deps)")
  else()
    foreach(model IN ITEMS mobile_sam_image_encoder_int8.onnx sam_mask_decoder_multi_int8.onnx)
      if(NOT EXISTS "${PATCHY_AI_MODELS_DIR}/${model}")
        set(PATCHY_AI_MISSING "model ${model} missing from ${PATCHY_AI_MODELS_DIR}")
      endif()
    endforeach()
    foreach(web_file IN LISTS PATCHY_ONNXRUNTIME_WEB_FILES)
      if(NOT EXISTS "${PATCHY_ONNXRUNTIME_WEB_DIR}/${web_file}")
        set(PATCHY_AI_MISSING "onnxruntime-web file ${web_file} missing from ${PATCHY_ONNXRUNTIME_WEB_DIR}")
      endif()
    endforeach()
  endif()
elseif(NOT PATCHY_ENABLE_AI_SELECTION)
  set(PATCHY_AI_MISSING "PATCHY_ENABLE_AI_SELECTION is OFF")
elseif(NOT PATCHY_ONNXRUNTIME_DIR)
  set(PATCHY_AI_MISSING "ONNX Runtime ${PATCHY_ONNXRUNTIME_VERSION} not found (run scripts/setup-ai-deps)")
elseif(NOT PATCHY_AI_MODELS_DIR)
  set(PATCHY_AI_MISSING "models not found (run scripts/setup-ai-deps)")
else()
  foreach(model IN LISTS PATCHY_AI_MODEL_FILES)
    if(NOT EXISTS "${PATCHY_AI_MODELS_DIR}/${model}")
      set(PATCHY_AI_MISSING "model ${model} missing from ${PATCHY_AI_MODELS_DIR}")
    endif()
  endforeach()
endif()

if(PATCHY_AI_MISSING STREQUAL "" AND NOT EMSCRIPTEN)
  if(WIN32)
    set(PATCHY_ONNXRUNTIME_RUNTIME_FILES
      "${PATCHY_ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
      "${PATCHY_ONNXRUNTIME_DIR}/lib/onnxruntime_providers_shared.dll")
    set(PATCHY_ONNXRUNTIME_LINK_FILE "${PATCHY_ONNXRUNTIME_DIR}/lib/onnxruntime.lib")
  elseif(APPLE)
    set(PATCHY_ONNXRUNTIME_RUNTIME_FILES
      "${PATCHY_ONNXRUNTIME_DIR}/lib/libonnxruntime.${PATCHY_ONNXRUNTIME_VERSION}.dylib")
    set(PATCHY_ONNXRUNTIME_LINK_FILE "${PATCHY_ONNXRUNTIME_DIR}/lib/libonnxruntime.${PATCHY_ONNXRUNTIME_VERSION}.dylib")
  else()
    set(PATCHY_ONNXRUNTIME_RUNTIME_FILES
      "${PATCHY_ONNXRUNTIME_DIR}/lib/libonnxruntime.so.1"
      "${PATCHY_ONNXRUNTIME_DIR}/lib/libonnxruntime.so.${PATCHY_ONNXRUNTIME_VERSION}")
    set(PATCHY_ONNXRUNTIME_LINK_FILE "${PATCHY_ONNXRUNTIME_DIR}/lib/libonnxruntime.so.${PATCHY_ONNXRUNTIME_VERSION}")
  endif()
  foreach(runtime_file IN LISTS PATCHY_ONNXRUNTIME_RUNTIME_FILES PATCHY_ONNXRUNTIME_LINK_FILE)
    if(NOT EXISTS "${runtime_file}")
      set(PATCHY_AI_MISSING "ONNX Runtime file ${runtime_file} missing")
    endif()
  endforeach()
endif()

add_library(patchy_ai
  src/ai/ai_selection_math.cpp
  src/ai/segmentation_engine_common.cpp
  src/ai/trustmark_watermark.cpp
)
target_link_libraries(patchy_ai PUBLIC patchy_core)
patchy_configure_target(patchy_ai)

if(PATCHY_AI_MISSING STREQUAL "" AND EMSCRIPTEN)
  set(PATCHY_AI_WEB_BUILT ON)
  target_sources(patchy_ai PRIVATE src/ai/segmentation_engine_web.cpp)
  set_source_files_properties(src/ai/segmentation_engine_web.cpp PROPERTIES
    COMPILE_DEFINITIONS "PATCHY_ONNXRUNTIME_WEB_FOLDER=\"${PATCHY_ONNXRUNTIME_WEB_FOLDER}\"")
  target_compile_definitions(patchy_ai PUBLIC PATCHY_HAVE_AI_SELECTION=1)
  message(STATUS "Automatic selection: onnxruntime-web at ${PATCHY_ONNXRUNTIME_WEB_DIR}, "
                 "models at ${PATCHY_AI_MODELS_DIR}")
elseif(PATCHY_AI_MISSING STREQUAL "")
  set(PATCHY_AI_SELECTION_BUILT ON)
  # ONNX Runtime is loaded at run time, never linked (segmentation_engine_onnx.cpp): a
  # machine that cannot load it still runs Patchy. Only its headers are needed here;
  # SYSTEM keeps strict warnings on Patchy's code alone.
  if(WIN32)
    set(PATCHY_ONNXRUNTIME_LIBRARY_NAME "onnxruntime.dll")
  elseif(APPLE)
    set(PATCHY_ONNXRUNTIME_LIBRARY_NAME "libonnxruntime.${PATCHY_ONNXRUNTIME_VERSION}.dylib")
  else()
    set(PATCHY_ONNXRUNTIME_LIBRARY_NAME "libonnxruntime.so.1")
  endif()
  target_sources(patchy_ai PRIVATE src/ai/segmentation_engine_onnx.cpp)
  target_include_directories(patchy_ai SYSTEM PRIVATE "${PATCHY_ONNXRUNTIME_DIR}/include")
  if(PATCHY_ENABLE_SELECT_SUBJECT)
    set(PATCHY_SELECT_SUBJECT_VALUE 1)
  else()
    set(PATCHY_SELECT_SUBJECT_VALUE 0)
  endif()
  set_source_files_properties(src/ai/segmentation_engine_onnx.cpp PROPERTIES
    COMPILE_DEFINITIONS
    "PATCHY_ONNXRUNTIME_LIBRARY=\"${PATCHY_ONNXRUNTIME_LIBRARY_NAME}\";PATCHY_SELECT_SUBJECT=${PATCHY_SELECT_SUBJECT_VALUE}")
  target_link_libraries(patchy_ai PRIVATE ${CMAKE_DL_LIBS})
  target_compile_definitions(patchy_ai PUBLIC PATCHY_HAVE_AI_SELECTION=1)
  message(STATUS "Automatic selection: ONNX Runtime at ${PATCHY_ONNXRUNTIME_DIR}, models at ${PATCHY_AI_MODELS_DIR}, "
                 "Select Subject ${PATCHY_ENABLE_SELECT_SUBJECT}")
else()
  target_sources(patchy_ai PRIVATE src/ai/segmentation_engine_stub.cpp)
  if(PATCHY_REQUIRE_AI_SELECTION)
    message(FATAL_ERROR "Automatic selection is required but cannot be built: ${PATCHY_AI_MISSING}")
  endif()
  message(STATUS "Automatic selection disabled: ${PATCHY_AI_MISSING}")
endif()

# Runtime staging: the ONNX Runtime shared libraries beside the executables and the models
# in <build>/ai-models. One copy-once target shared by every executable (never per-target
# POST_BUILD copies into the shared output directory). Clean first so a renamed model never
# lingers in a long-lived build tree.
if(PATCHY_AI_SELECTION_BUILT)
  set(PATCHY_AI_MODEL_SOURCES "")
  foreach(model IN LISTS PATCHY_AI_MODEL_FILES)
    list(APPEND PATCHY_AI_MODEL_SOURCES "${PATCHY_AI_MODELS_DIR}/${model}")
  endforeach()
  # License texts travel with the models: the tracked model licenses (Apache-2.0 requires
  # its text to ship) and ONNX Runtime's own license and third-party notices.
  set(PATCHY_AI_DOWNLOAD_MODEL_SOURCES "")
  foreach(model IN LISTS PATCHY_AI_DOWNLOAD_MODEL_FILES)
    if(EXISTS "${PATCHY_AI_MODELS_DIR}/${model}")
      list(APPEND PATCHY_AI_DOWNLOAD_MODEL_SOURCES "${PATCHY_AI_MODELS_DIR}/${model}")
    endif()
  endforeach()
  list(LENGTH PATCHY_AI_DOWNLOAD_MODEL_SOURCES download_model_count)
  list(LENGTH PATCHY_AI_DOWNLOAD_MODEL_FILES download_model_expected)
  if(download_model_count EQUAL download_model_expected)
    set(PATCHY_AI_DOWNLOAD_STAGE_COMMAND
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PATCHY_AI_DOWNLOAD_MODEL_SOURCES}
                "${CMAKE_BINARY_DIR}/ai-models-download/")
  else()
    set(PATCHY_AI_DOWNLOAD_MODEL_SOURCES "")
    set(PATCHY_AI_DOWNLOAD_STAGE_COMMAND "")
  endif()
  set(PATCHY_AI_WATERMARK_MODEL_SOURCES "")
  foreach(model IN LISTS PATCHY_AI_WATERMARK_MODEL_FILES)
    if(EXISTS "${PATCHY_AI_MODELS_DIR}/${model}")
      list(APPEND PATCHY_AI_WATERMARK_MODEL_SOURCES "${PATCHY_AI_MODELS_DIR}/${model}")
    endif()
  endforeach()
  list(LENGTH PATCHY_AI_WATERMARK_MODEL_SOURCES watermark_model_count)
  list(LENGTH PATCHY_AI_WATERMARK_MODEL_FILES watermark_model_expected)
  if(watermark_model_count EQUAL watermark_model_expected)
    list(APPEND PATCHY_AI_DOWNLOAD_MODEL_SOURCES ${PATCHY_AI_WATERMARK_MODEL_SOURCES})
    list(APPEND PATCHY_AI_DOWNLOAD_STAGE_COMMAND
         COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PATCHY_AI_WATERMARK_MODEL_SOURCES}
                 "${CMAKE_BINARY_DIR}/ai-models-download/")
  endif()
  set(PATCHY_AI_LICENSE_FILE "${PROJECT_SOURCE_DIR}/third_party/ai-models/MODEL-LICENSES.txt")
  set(PATCHY_AI_RUNTIME_STAMP "${CMAKE_BINARY_DIR}/CMakeFiles/patchy_ai_runtime.stamp")
  set(PATCHY_AI_RUNTIME_INPUT_MANIFEST "${CMAKE_BINARY_DIR}/CMakeFiles/patchy_ai_runtime_inputs.txt")
  patchy_write_input_manifest("${PATCHY_AI_RUNTIME_INPUT_MANIFEST}"
    ${PATCHY_AI_MODEL_SOURCES} ${PATCHY_AI_DOWNLOAD_MODEL_SOURCES} ${PATCHY_ONNXRUNTIME_RUNTIME_FILES})
  add_custom_command(
    OUTPUT "${PATCHY_AI_RUNTIME_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/ai-models"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/ai-models"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/ai-models-download"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/ai-models-download"
    ${PATCHY_AI_DOWNLOAD_STAGE_COMMAND}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PATCHY_AI_MODEL_SOURCES} "${CMAKE_BINARY_DIR}/ai-models/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PATCHY_AI_LICENSE_FILE}" "${CMAKE_BINARY_DIR}/ai-models/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PATCHY_ONNXRUNTIME_DIR}/LICENSE"
            "${CMAKE_BINARY_DIR}/ai-models/onnxruntime-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PATCHY_ONNXRUNTIME_DIR}/ThirdPartyNotices.txt"
            "${CMAKE_BINARY_DIR}/ai-models/onnxruntime-ThirdPartyNotices.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PATCHY_ONNXRUNTIME_RUNTIME_FILES} "${CMAKE_BINARY_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E touch "${PATCHY_AI_RUNTIME_STAMP}"
    DEPENDS ${PATCHY_AI_MODEL_SOURCES} ${PATCHY_AI_DOWNLOAD_MODEL_SOURCES} ${PATCHY_ONNXRUNTIME_RUNTIME_FILES}
            "${PATCHY_AI_LICENSE_FILE}" "${PATCHY_ONNXRUNTIME_DIR}/LICENSE"
            "${PATCHY_ONNXRUNTIME_DIR}/ThirdPartyNotices.txt" "${PATCHY_AI_RUNTIME_INPUT_MANIFEST}"
    COMMENT "Copying ONNX Runtime and the automatic selection models into the build directory"
  )
  add_custom_target(patchy_ai_runtime DEPENDS "${PATCHY_AI_RUNTIME_STAMP}")
endif()
# The wasm app's runtime: onnxruntime-web in <build>/onnxruntime-web-<version> and the two
# Object Selection models in <build>/ai-models, beside patchy.html (the page fetches them on
# first use; build-wasm.bat stages both folders into the site). Same copy-once shape.
if(PATCHY_AI_WEB_BUILT)
  set(PATCHY_AI_WEB_MODEL_SOURCES
    "${PATCHY_AI_MODELS_DIR}/mobile_sam_image_encoder_int8.onnx"
    "${PATCHY_AI_MODELS_DIR}/sam_mask_decoder_multi_int8.onnx")
  set(PATCHY_AI_WEB_RUNTIME_SOURCES "")
  foreach(web_file IN LISTS PATCHY_ONNXRUNTIME_WEB_FILES)
    list(APPEND PATCHY_AI_WEB_RUNTIME_SOURCES "${PATCHY_ONNXRUNTIME_WEB_DIR}/${web_file}")
  endforeach()
  set(PATCHY_AI_LICENSE_FILE "${PROJECT_SOURCE_DIR}/third_party/ai-models/MODEL-LICENSES.txt")
  set(PATCHY_AI_RUNTIME_STAMP "${CMAKE_BINARY_DIR}/CMakeFiles/patchy_ai_runtime.stamp")
  set(PATCHY_AI_RUNTIME_INPUT_MANIFEST "${CMAKE_BINARY_DIR}/CMakeFiles/patchy_ai_runtime_inputs.txt")
  patchy_write_input_manifest("${PATCHY_AI_RUNTIME_INPUT_MANIFEST}"
    ${PATCHY_AI_WEB_MODEL_SOURCES} ${PATCHY_AI_WEB_RUNTIME_SOURCES})
  add_custom_command(
    OUTPUT "${PATCHY_AI_RUNTIME_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/ai-models"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/ai-models"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PATCHY_AI_WEB_MODEL_SOURCES} "${PATCHY_AI_LICENSE_FILE}"
            "${CMAKE_BINARY_DIR}/ai-models/"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/${PATCHY_ONNXRUNTIME_WEB_FOLDER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/${PATCHY_ONNXRUNTIME_WEB_FOLDER}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PATCHY_AI_WEB_RUNTIME_SOURCES}
            "${CMAKE_BINARY_DIR}/${PATCHY_ONNXRUNTIME_WEB_FOLDER}/"
    COMMAND ${CMAKE_COMMAND} -E touch "${PATCHY_AI_RUNTIME_STAMP}"
    DEPENDS ${PATCHY_AI_WEB_MODEL_SOURCES} ${PATCHY_AI_WEB_RUNTIME_SOURCES} "${PATCHY_AI_LICENSE_FILE}"
            "${PATCHY_AI_RUNTIME_INPUT_MANIFEST}"
    COMMENT "Copying onnxruntime-web and the Object Selection models into the build directory"
  )
  add_custom_target(patchy_ai_runtime DEPENDS "${PATCHY_AI_RUNTIME_STAMP}")
  # build-wasm.bat stages exactly this folder (an older version's folder can linger in a
  # long-lived build tree).
  file(WRITE "${CMAKE_BINARY_DIR}/onnxruntime-web-folder.txt" "${PATCHY_ONNXRUNTIME_WEB_FOLDER}")
endif()

function(patchy_copy_ai_runtime target)
  if(TARGET patchy_ai_runtime)
    # The engine loads the library from beside the executable (build trees, the Windows
    # package), from ../Frameworks (the macOS bundle), or by plain name (the Flatpak's
    # /app/lib), so no rpath is involved.
    add_dependencies(${target} patchy_ai_runtime)
  endif()
  if(PATCHY_AI_WEB_BUILT)
    # The page-side host the web engine talks to (it must live in the page, not a worker).
    set(host_script "${PROJECT_SOURCE_DIR}/scripts/wasm/ai-host-pre.js")
    target_link_options(${target} PRIVATE "SHELL:--pre-js ${host_script}")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${host_script}")
  endif()
endfunction()
