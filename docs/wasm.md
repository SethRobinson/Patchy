# WebAssembly (Emscripten) build

Deep reference for the wasm build. Read this before touching the `wasm-core`
preset, the emsdk provisioning, or anything under `scripts/wasm/`.

## What exists today

Step 1 of the wasm effort: the Qt-free engine libraries (`patchy_core`,
`patchy_render`, `patchy_psd`, `patchy_filters`, `patchy_formats`,
`patchy_color`, plus `patchy_plugins`, `patchy_lcms2`, `patchy_libraw`) and
`patchy_core_tests` compile with Emscripten and the suite runs under node.
There is no Qt, no browser page, and no app target in this configuration;
`PATCHY_BUILD_APP=OFF` keeps the whole Qt block out of the configure. Desktop
builds are unaffected: the preset and the files under `scripts/wasm/` are the
only wasm surface, and existing presets do not change behavior.

## Toolchain setup

```powershell
pwsh -File scripts\wasm\setup-emsdk.ps1
```

Idempotent. Clones emsdk into `.deps\emsdk` (gitignored, same idea as
`.deps\Qt`) and installs + activates Emscripten 3.1.56. That version is pinned
because it is what the Qt 6.8 documentation lists as supported for Qt for
WebAssembly, so the later Qt-for-wasm work can reuse this toolchain unchanged.
Activation only writes emsdk's local config; nothing touches the user or
system environment. The SDK bundles its own node (22.16.0), which is what runs
the test suite.

## Configure and build

Same wrapper pattern as the Windows release preset: `scripts\vs-env.bat`
supplies cmake and ninja, and `emsdk_env.bat` supplies emcc and its python.
Run from the repository root in PowerShell or cmd, never a POSIX shell:

```powershell
cmd /s /c 'call .deps\emsdk\emsdk_env.bat >nul 2>&1 && call scripts\vs-env.bat -arch=x64 -host_arch=x64 >nul && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --preset wasm-core'
```

```powershell
cmd /s /c 'call .deps\emsdk\emsdk_env.bat >nul 2>&1 && call scripts\vs-env.bat -arch=x64 -host_arch=x64 >nul && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset wasm-core'
```

Output lands in `build\wasm-core` (`patchy_core_tests.js` + `.wasm`). The
zero-warning rule applies to this build like any other; vendored-source
suppressions stay scoped to one file and one diagnostic (the only one so far:
miniz's `-Wno-#pragma-messages` in the root CMakeLists, beside its MSVC
`/wd4132`).

## Running the suite

```powershell
pwsh -File scripts\wasm\run-core-tests.ps1
```

Accepts the usual name-substring filter as the first argument. The wrapper
runs the emsdk-bundled node from `build\wasm-core`, so the relative
`test-artifacts/` output lands there, mirroring how the native binaries run
from their build directory. The raw form is:

```powershell
cd build\wasm-core; & ..\..\.deps\emsdk\node\22.16.0_64bit\bin\node.exe patchy_core_tests.js
```

`ctest` also works from `build\wasm-core` because the preset pins
`CMAKE_CROSSCOMPILING_EMULATOR` to the bundled node.

## Preset decisions (all in CMakePresets.json, nothing per-site)

- `-fwasm-exceptions`: the format readers throw `std::runtime_error` as their
  error contract and the test runner catches per test, so exception catching
  must be on (Emscripten disables it by default). Native wasm exceptions are
  used rather than the slower JS-based `-fexceptions`; the bundled node 22
  supports them without flags.
- `-pthread` with `-sPTHREAD_POOL_SIZE=32` and `-sPTHREAD_POOL_SIZE_STRICT=2`:
  the engine has exactly two `std::async` sites (`compositor.cpp` strip
  flatten, `psd_channel_data.cpp` CMYK conversion). Building the runner with
  pthreads keeps them on the production code path with zero source changes;
  output is byte-identical regardless of strip count. The pool is pre-spawned
  and must stay at or above the machine's logical core count, because the CMYK
  site spawns `hardware_concurrency()` workers while the main thread blocks on
  the results; a lazily spawned worker would deadlock there. STRICT=2 turns
  pool exhaustion into a hard error instead of that silent hang, so on a
  machine with more than about 30 logical cores, raise the pool size.
- `-sNODERAWFS=1`: tests read fixtures straight from the real filesystem via
  the `PATCHY_SOURCE_DIR` compile definition, exactly like native runs. This
  keeps the 2.2 GB `local-test-fixtures/` reachable (a preloaded memory
  filesystem could never carry it) and writes `test-artifacts/` to real disk.
  The cost is that the .js is host-bound (node only, no browser), which is
  exactly this preset's scope.
- Heap and stacks: `-sALLOW_MEMORY_GROWTH=1` up to `-sMAXIMUM_MEMORY=4GB`
  (large PSD fixtures), `-sINITIAL_MEMORY=256MB` to cut regrow churn,
  `-sSTACK_SIZE=8MB` for the main thread (LibRaw's dcraw-derived decoders
  carry large stack locals; Emscripten's 64 KB default is far too small) and
  `-sDEFAULT_PTHREAD_STACK_SIZE=1MB` for workers.
- `-sENVIRONMENT=node,worker` (pthread workers need the worker environment)
  and `-sEXIT_RUNTIME=1` so `main`'s return value becomes the process exit
  code and live pool workers do not keep node alive.
- `-Wno-pthreads-mem-growth`: em++ emits an informational link warning that
  pthreads plus a growable heap slows JS-side heap views. The suite computes
  in wasm, not JS, so the note does not apply; suppressing the one diagnostic
  at the one link that emits it keeps the build at zero warnings.
- `--pre-js scripts/wasm/node-env-pre.js`: Emscripten's environ is synthetic
  (USER=web_user and friends) and ignores the host environment, so
  `PATCHY_RENDER_SINGLE_THREADED`, `PATCHY_AF_TRACE`, and the other
  environment escape hatches would silently do nothing under node. The pre-js
  forwards `process.env` before `main` runs, matching native behavior.

## Source-level status

Zero wasm `#ifdef`s in `src/core`, `src/formats`, `src/psd`, `src/render`,
`src/filters`, `src/color`, `src/plugins`. The engine compiled as-is. The one
wasm guard anywhere is in `tests/core/main.cpp`, which skips installing the
crash-stack reporter (Emscripten has neither `execinfo.h` nor fatal-signal
delivery; node prints a wasm stack for traps on its own). One test,
`af_modern_embeds_are_center_anchored_if_available`, self-skips behind a
compile-time `sizeof(void*) < 8` check rather than a platform ifdef; see below.

## Current status (2026-07)

Full `patchy_core_tests` under the bundled node: 726 pass, 0 fail, about 45
seconds wall on a 24-thread machine, with the 2.2 GB `local-test-fixtures/`
corpus present and exercised. The three byte-stability canaries
(`psd_layered_writer_bytes_are_stable`, `gif_encoder_bytes_are_stable`,
`tool_write_paths_digest_baseline`) pass byte-identically, making wasm/musl
the fourth toolchain (after MSVC, Apple clang, GCC/glibc) to produce the
pinned bytes. The `.wasm` is about 6.4 MB. Four tests print `[SKIP]` and
pass:

- `akiko_cycling_okinawa_with_filters`: that local fixture is absent on this
  machine; unrelated to wasm.
- Two HEIC tests: no platform HEIC decoder outside Windows; the stub path is
  the intended behavior.
- `af_modern_embeds_are_center_anchored_if_available`: the tier-1 import of
  `restaurant-menu-inside.af` (a wild Affinity menu whose spreads hold a
  dozen 75-150 MB scan layers live at once) exceeds a 32-bit address space.
  Under wasm32 the reader throws `std::bad_alloc` and falls back to its
  embedded preview, which has no swash layer to assert on, so the test skips
  on 32-bit targets. This is the first concrete sighting of the wasm32
  document-size ceiling the feasibility work predicted; step 4 turns that
  ceiling into a measured, advertised cap.

## Later steps (not built yet)

- Step 2: Qt for WebAssembly kit in `.deps/Qt`, app links and boots in a
  browser with Asyncify, unsupported modules (PrintSupport, scanner import,
  single-instance) compiled out, fonts/translations/textures moved into
  resources or fetched.
- Step 3: web file open/save at the existing seams (`dialog_utils.hpp`
  open/save helpers, `read_all_file_bytes()` in `main_window_files.cpp`),
  async QSettings audit behind `app_settings()`, preset libraries off the
  filesystem.
- Step 4: memory tuning (per-platform undo cap and byte budget, tile-cache
  eviction), optional threaded rendering behind COOP/COEP hosting, packaging
  and deployment, and the measured document-size cap the web build advertises.
