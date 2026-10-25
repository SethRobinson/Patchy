# G'MIC filter engine (src/filters/gmic, docs/gmic.md, GitHub issue 87).
#
# patchy_gmic always exists: the catalog parser (no engine dependency) plus either
# the engine wrapper or a stub that reports the engine as unavailable. That keeps
# the G'MIC Filter Library's interface code and its translations in every build,
# with or without the interpreter.
#
# PATCHY_ENABLE_GMIC (ON by default; OFF for the web build, which has only been
# compile-checked) adds patchy_gmic_vendor, the vendored
# interpreter (gmic.cpp + CImg.h), isolated like patchy_libraw: its own language
# standard, warnings off. It is built with no display, network library, FFTW,
# image codecs, external programs (cimg_no_system_calls, gmic_noexec) or
# patch-based algorithms (patchy_gmic_no_patch_algorithms; see
# docs/legal-constraints.md). Its FFT is the vendored pocketfft
# (src/filters/gmic/pocketfft, BSD-3-Clause; patchy_gmic_use_pocketfft).
# A build directory configured before the default changed keeps its cached value:
# pass -UPATCHY_ENABLE_GMIC once to pick up the default.
if(EMSCRIPTEN)
  set(PATCHY_GMIC_DEFAULT OFF)
else()
  set(PATCHY_GMIC_DEFAULT ON)
endif()
option(PATCHY_ENABLE_GMIC "Build the vendored G'MIC filter engine" ${PATCHY_GMIC_DEFAULT})

add_library(patchy_gmic STATIC src/filters/gmic/gmic_catalog.cpp)

if(PATCHY_ENABLE_GMIC)
  add_library(patchy_gmic_vendor STATIC src/filters/gmic/gmic.cpp)
  # patchy_stub first: its gmic_stdlib_community.h replaces the generated one.
  # It also holds zlib.h, a shim onto the vendored miniz that patchy_psd compiles.
  target_include_directories(patchy_gmic_vendor PRIVATE
    ${PROJECT_SOURCE_DIR}/src/filters/gmic/patchy_stub ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(patchy_gmic_vendor PUBLIC patchy_psd)
  target_include_directories(patchy_gmic_vendor SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/src/filters/gmic)
  target_compile_definitions(patchy_gmic_vendor PRIVATE
    gmic_core cimg_use_abort cimg_use_zlib gmic_noexec cimg_no_system_calls patchy_gmic_no_patch_algorithms
    patchy_gmic_use_pocketfft)
  target_compile_definitions(patchy_gmic_vendor PUBLIC cimg_display=0)
  # G'MIC is developed against C++11 through C++17, so gmic.cpp compiles as C++17
  # (patchy_configure_target keeps its cxx_std_20 feature PRIVATE for this reason;
  # linking patchy_psd here only reaches the miniz symbols behind the zlib.h shim).
  set_target_properties(patchy_gmic_vendor PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  if(MSVC)
    target_compile_definitions(patchy_gmic_vendor PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    target_compile_options(patchy_gmic_vendor PRIVATE /w /bigobj)
  else()
    target_compile_options(patchy_gmic_vendor PRIVATE -w)
  endif()

  # CImg's loops run in parallel with OpenMP (docs/gmic.md, "Threads"). Only this target is
  # compiled with it. PATCHY_GMIC_THREADS: AUTO uses OpenMP where it is available, ON
  # requires it (release builds), OFF leaves the engine single-threaded.
  #   Windows / Linux: the compiler's own OpenMP (MSVC vcomp, GCC libgomp), whose runtime
  #   the package ships or the Flatpak runtime provides.
  #   macOS: Apple's compiler has no OpenMP runtime, so it links the libomp dylib that
  #   scripts/setup-openmp-mac.sh builds for the deployment target into .deps; the app
  #   bundle carries it in Contents/Frameworks (CMakeLists.txt, bundle resources).
  set(PATCHY_GMIC_THREADS AUTO CACHE STRING "Run the G'MIC engine's loops in parallel with OpenMP: AUTO, ON or OFF")
  set_property(CACHE PATCHY_GMIC_THREADS PROPERTY STRINGS AUTO ON OFF)
  set(PATCHY_LIBOMP_VERSION 21.1.8)  # keep in sync with scripts/setup-openmp-mac.sh
  set(patchy_gmic_openmp_found FALSE)
  if(NOT PATCHY_GMIC_THREADS STREQUAL "OFF")
    if(APPLE)
      execute_process(COMMAND git -C "${PROJECT_SOURCE_DIR}" rev-parse --path-format=absolute --git-common-dir
                      OUTPUT_VARIABLE patchy_git_common_dir OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
      set(patchy_libomp_candidates "${PROJECT_SOURCE_DIR}/.deps")
      if(patchy_git_common_dir)
        get_filename_component(patchy_main_checkout "${patchy_git_common_dir}" DIRECTORY)
        list(APPEND patchy_libomp_candidates "${patchy_main_checkout}/.deps")
      endif()
      set(PATCHY_LIBOMP_ROOT "" CACHE PATH "libomp built by scripts/setup-openmp-mac.sh (empty: look in .deps)")
      set(patchy_libomp_root "${PATCHY_LIBOMP_ROOT}")
      if(NOT patchy_libomp_root)
        foreach(candidate IN LISTS patchy_libomp_candidates)
          set(dir "${candidate}/libomp-${PATCHY_LIBOMP_VERSION}-macos${CMAKE_OSX_DEPLOYMENT_TARGET}")
          if(EXISTS "${dir}/lib/libomp.dylib" AND EXISTS "${dir}/include/omp.h")
            set(patchy_libomp_root "${dir}")
            break()
          endif()
        endforeach()
      endif()
      if(patchy_libomp_root AND EXISTS "${patchy_libomp_root}/lib/libomp.dylib")
        set(patchy_gmic_openmp_found TRUE)
        set(PATCHY_LIBOMP_DYLIB "${patchy_libomp_root}/lib/libomp.dylib" CACHE INTERNAL "")
        target_compile_options(patchy_gmic_vendor PRIVATE -Xclang -fopenmp)
        target_include_directories(patchy_gmic_vendor SYSTEM PRIVATE "${patchy_libomp_root}/include")
        target_link_libraries(patchy_gmic_vendor PUBLIC "${PATCHY_LIBOMP_DYLIB}")
        # @rpath/libomp.dylib: Contents/Frameworks inside the app bundle. CMake adds the .deps
        # folder itself (its build rpath for a linked dylib), which the test binaries use.
        target_link_options(patchy_gmic_vendor INTERFACE "LINKER:-rpath,@executable_path/../Frameworks")
      endif()
    else()
      find_package(OpenMP COMPONENTS CXX)
      if(OpenMP_CXX_FOUND)
        set(patchy_gmic_openmp_found TRUE)
        target_link_libraries(patchy_gmic_vendor PUBLIC OpenMP::OpenMP_CXX)
      endif()
    endif()
  endif()
  if(PATCHY_GMIC_THREADS STREQUAL "ON" AND NOT patchy_gmic_openmp_found)
    if(APPLE)
      message(FATAL_ERROR "PATCHY_GMIC_THREADS is ON but libomp ${PATCHY_LIBOMP_VERSION} for macOS "
                          "${CMAKE_OSX_DEPLOYMENT_TARGET} is missing: run bash scripts/setup-openmp-mac.sh")
    else()
      message(FATAL_ERROR "PATCHY_GMIC_THREADS is ON but no OpenMP was found for the C++ compiler.")
    endif()
  endif()
  # Read by packaging/macos/make-dmg.sh, which refuses a single-threaded release build.
  set(PATCHY_GMIC_OPENMP_ENABLED ${patchy_gmic_openmp_found} CACHE INTERNAL "")
  if(patchy_gmic_openmp_found)
    target_compile_definitions(patchy_gmic_vendor PRIVATE cimg_use_openmp)
    if(APPLE)
      # Keeps an error inside a parallel loop from ending the process (the header says why).
      target_compile_options(patchy_gmic_vendor PRIVATE
        -include "${PROJECT_SOURCE_DIR}/src/filters/gmic/patchy_stub/patchy_gmic_openmp_catch.h")
    endif()
    message(STATUS "G'MIC engine: multithreaded (OpenMP)")
  else()
    message(STATUS "G'MIC engine: single-threaded (no OpenMP)")
  endif()

  target_sources(patchy_gmic PRIVATE src/filters/gmic/gmic_engine.cpp)
  target_link_libraries(patchy_gmic PRIVATE patchy_gmic_vendor)
  target_compile_definitions(patchy_gmic PUBLIC PATCHY_HAVE_GMIC=1)
  if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(patchy_gmic PUBLIC Threads::Threads)
  endif()
else()
  target_sources(patchy_gmic PRIVATE src/filters/gmic/gmic_engine_stub.cpp)
endif()
patchy_configure_target(patchy_gmic)
