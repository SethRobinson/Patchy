# GPU build and dependency guide

This guide describes how to configure, build, and verify Patchy's single desktop binary with the optional Qt Quick/RHI and Dawn/WebGPU document-composition paths. The normal build remains valid when an optional graphics dependency is absent.

The implementation has four build-time layers:

1. **Qt Widgets** is required for the native desktop application. If the base Qt component set is not found, CMake builds the core libraries and tests but skips the native application target.
2. **Qt Quick and Qt Quick Widgets** enable the automatic Qt RHI canvas. If they are absent while Qt Widgets is available, the application keeps the ordinary QWidget/CPU canvas.
3. **Qt ShaderTools** enables the portable QSB shader tier. If it is absent, the texture-only GPU tier remains available and documents that need shader passes stay on the CPU compositor.
4. **Dawn/WebGPU** is an external optional dependency. If it is absent, the same binary uses the Qt RHI and CPU paths. Dawn is never required for the application to configure or run.

The CPU compositor remains authoritative for exports, byte-identity tests, and every document feature that does not have a validated GPU implementation. See [GPU canvas and document composition](gpu-canvas.md) for the capability matrix.

## Prerequisites

Use CMake 3.26 or newer and Ninja. Desktop presets in `CMakePresets.json` use Qt 6.8.3 installed under `.deps/Qt`:

| Platform | Presets | Qt prefix used by the preset |
|---|---|---|
| Windows | `dev`, `debug`, `qt-local`, `release` | `.deps/Qt/6.8.3/msvc2022_64` |
| macOS | `mac-dev`, `mac-release` | `.deps/Qt/6.8.3/macos` |
| Linux | `linux-dev`, `linux-release`, `linux-asan` | `.deps/Qt/6.8.3/gcc_64` |

The Qt installation must provide the base modules required by Patchy, including Widgets, Svg, Network, Qml, LinguistTools, and the desktop modules selected by the platform. Qt Quick, Qt Quick Widgets, and Qt ShaderTools are discovered separately so that a minimal Qt installation can still use the CPU application path.

For Linux remote builds, `scripts/remote/setup-linux.sh` provisions the system libraries, Ninja, and Qt 6.8.3 under `.deps/Qt`. The script does not install Dawn. Dawn must be provided separately when WebGPU composition is required.

## Quick start with the repository helper

On Linux, macOS, or another POSIX shell, the repository helper is the shortest repeatable path. The default does not download Dawn:

```sh
PATCHY_BUILD_JOBS=6 scripts/build-gpu.sh linux-release
```

Use the matching preset on another desktop platform:

```sh
PATCHY_BUILD_JOBS=6 scripts/build-gpu.sh mac-release
PATCHY_BUILD_JOBS=6 scripts/build-gpu.sh release
```

The helper performs these operations:

- configures with `PATCHY_ENABLE_GPU_CANVAS=ON`;
- configures with `PATCHY_ENABLE_WEBGPU=ON` unless `PATCHY_ENABLE_WEBGPU=OFF` is supplied through the environment;
- builds with the selected number of parallel jobs;
- runs the CTest suite when the selected build produced the core test executable;
- runs `ui_canvas_renderer_selects_a_safe_runtime_backend` when the UI test executable exists.

The optional Dawn build is a separate, explicit step. Use `--with-dawn` when the WebGPU document compositor is desired:

```sh
PATCHY_BUILD_JOBS=6 scripts/build-gpu.sh linux-release --with-dawn
```

Use `--dawn-only` to fetch, build, install, and verify Dawn without configuring Patchy:

```sh
PATCHY_BUILD_JOBS=6 scripts/build-gpu.sh linux-release --dawn-only
```

The normal Patchy build remains unchanged when neither flag nor `PATCHY_BUILD_DAWN=ON` is present. This is intentional: a missing network connection, compiler, graphics driver, or Dawn dependency never makes Dawn mandatory for the application.

Use `--skip-tests` when only the binary or CMake configuration is needed:

```sh
PATCHY_BUILD_JOBS=6 scripts/build-gpu.sh linux-release --skip-tests
```

The helper accepts two dependency-prefix variables:

```sh
PATCHY_QT_PREFIX="$HOME/Qt/6.8.3/gcc_64" \
PATCHY_DAWN_PREFIX="$HOME/.local/dawn" \
PATCHY_BUILD_JOBS=6 \
scripts/build-gpu.sh linux-release
```

`PATCHY_QT_PREFIX` sets both `CMAKE_PREFIX_PATH` and `Qt6_DIR`. `PATCHY_DAWN_PREFIX` adds the prefix to `CMAKE_PREFIX_PATH` and supplies the common `Dawn_DIR` and `webgpu_dawn_DIR` hints when those files exist. The helper does not download or compile either dependency unless `--with-dawn` or `PATCHY_BUILD_DAWN=ON` is supplied.

## Direct CMake builds

The helper is optional. The equivalent direct commands are useful when diagnosing CMake discovery.

### Linux

```sh
rm -rf build/linux-release
cmake --preset linux-release \
  -DPATCHY_ENABLE_GPU_CANVAS=ON \
  -DPATCHY_ENABLE_WEBGPU=ON
cmake --build --preset linux-release --parallel 6
```

If Qt is installed outside `.deps/Qt`, point CMake at the intended installation. `Qt6_DIR` is the most direct way to prevent CMake from selecting a system Qt installation by accident:

```sh
rm -rf build/linux-release
cmake --preset linux-release \
  -DPATCHY_ENABLE_GPU_CANVAS=ON \
  -DPATCHY_ENABLE_WEBGPU=ON \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/gcc_64" \
  -DQt6_DIR="$HOME/Qt/6.8.3/gcc_64/lib/cmake/Qt6"
cmake --build --preset linux-release --parallel 6
```

### macOS

```sh
rm -rf build/mac-release
cmake --preset mac-release \
  -DPATCHY_ENABLE_GPU_CANVAS=ON \
  -DPATCHY_ENABLE_WEBGPU=ON
cmake --build --preset mac-release --parallel 6
```

### Windows

Run these commands from PowerShell or a Developer Command Prompt with the MSVC environment loaded:

```powershell
Remove-Item -Recurse -Force build\release -ErrorAction SilentlyContinue
cmake --preset release `
  -DPATCHY_ENABLE_GPU_CANVAS=ON `
  -DPATCHY_ENABLE_WEBGPU=ON
cmake --build --preset release --parallel 6
```

For a Qt installation outside the preset default, add its prefix and Qt package directory:

```powershell
cmake --preset release `
  -DPATCHY_ENABLE_GPU_CANVAS=ON `
  -DCMAKE_PREFIX_PATH='D:\Qt\6.8.3\msvc2022_64' `
  -DQt6_DIR='D:\Qt\6.8.3\msvc2022_64\lib\cmake\Qt6'
```

The repository release handoff has additional Windows requirements, including the Visual Studio environment and build throttling. Follow `AGENTS.md` and [platform notes](platform.md) for release work.

## CPU-only configuration

Use a clean build directory when changing graphics options. CMake caches package discovery and compile definitions.

```sh
rm -rf build/linux-release
cmake --preset linux-release \
  -DPATCHY_ENABLE_GPU_CANVAS=OFF \
  -DPATCHY_ENABLE_WEBGPU=OFF
cmake --build --preset linux-release --parallel 6
```

This produces the same application target without the Qt Quick/RHI canvas. `PATCHY_RENDER_BACKEND=cpu` is still useful for testing the runtime fallback in a GPU-capable build, but it does not replace the CPU-only CMake configuration when checking the no-graphics compilation path.

## Optional Dawn/WebGPU integration

Qt RHI and WebGPU are separate layers. Qt Quick owns the application surface and final presentation. Dawn supplies an optional WebGPU document compositor when CMake finds an installed Dawn target. Qt does not provide a WebGPU backend through `QSGRendererInterface` in this project.

### Reproducible local build

The repository pins a complete Dawn commit in `cmake/dawn-version.cmake`. The dedicated helper checks out that commit from the official repository, asks Dawn to fetch the dependency revisions recorded in its own `DEPS` file, installs the result below `.deps/dawn/`, and verifies the generated CMake package. No Dawn source checkout, dependency tree, generated file, or binary is tracked by Git; `.deps/` is ignored by the repository.

Run the helper explicitly:

```sh
PATCHY_BUILD_JOBS=6 scripts/build-dawn.sh
```

The generated directories are:

```text
.deps/dawn/src/                 pinned Dawn checkout
.deps/dawn/build/release/      CMake/Ninja build tree
.deps/dawn/install/release/    local CMake package and libraries
```

The helper uses `DAWN_FETCH_DEPENDENCIES=ON`, `DAWN_ENABLE_INSTALL=ON`, `DAWN_BUILD_MONOLITHIC_LIBRARY=STATIC`, `BUILD_SHARED_LIBS=OFF`, `BUILD_TESTS=OFF`, `DAWN_BUILD_TESTS=OFF`, `BUILD_SAMPLES=OFF`, `DAWN_BUILD_SAMPLES=OFF`, and `DAWN_BUILD_NODE_BINDINGS=OFF`. The platform backend set is deliberately isolated:

| Host | Dawn backends built by the helper | Presentation in Patchy |
|---|---|---|
| Linux | Vulkan, desktop OpenGL, and the null backend | Dawn composes documents; Qt RHI presents the window |
| macOS | Metal and the null backend | Dawn composes documents; Qt RHI presents the window |
| Windows | D3D11, D3D12, Vulkan, and the null backend | Dawn composes documents; Qt RHI presents the window |

The helper does not enable a backend belonging to another operating system. Qt Quick/RHI remains the independent presentation route for OpenGL, Vulkan, Metal, and Direct3D.

`--print-config` validates the lock format and prints the paths without accessing the network:

```sh
scripts/build-dawn.sh --print-config
```

To use a machine-local workspace or a different install prefix, set the variables explicitly. The source checkout must be clean; the helper refuses to reset local changes.

```sh
PATCHY_DAWN_ROOT="$HOME/.cache/patchy-dawn" \
PATCHY_DAWN_PREFIX="$HOME/.cache/patchy-dawn/install/release" \
PATCHY_BUILD_JOBS=6 scripts/build-dawn.sh
```

Update the Dawn revision only as a conscious dependency change. Replace `PATCHY_DAWN_COMMIT` with a complete SHA, run the helper from a clean workspace, verify the CMake package and Patchy build, and record the new date in the lock file. Never use `main`, `HEAD`, a moving tag, or a short hash.

After Dawn is installed, build Patchy and point CMake at the generated prefix:

```sh
PATCHY_DAWN_PREFIX="$PWD/.deps/dawn/install/release" \
PATCHY_BUILD_JOBS=6 scripts/build-gpu.sh linux-release
```

The combined one-command form is:

```sh
PATCHY_BUILD_JOBS=6 scripts/build-gpu.sh linux-release --with-dawn
```

The Patchy discovery accepts these target names because Dawn package layouts can vary by revision:

- `dawn::webgpu_dawn`;
- `Dawn::webgpu_dawn`;
- `webgpu_dawn`.

Do not assume that a WebGPU header alone is sufficient. The build needs a package configuration file, a linkable target, and the runtime libraries that target references.

After changing Dawn locations, remove the build directory or clear the relevant CMake cache before configuring again:

```sh
rm -rf build/linux-release
PATCHY_DAWN_PREFIX="$PWD/.deps/dawn/install/release" \
PATCHY_BUILD_JOBS=6 scripts/build-gpu.sh linux-release
```

The CMake cache records `PATCHY_DAWN_PREFIX` and `PATCHY_DAWN_ROOT` when they were supplied. Inspect the configure output and cache:

```sh
grep -E 'PATCHY_ENABLE_GPU_CANVAS|PATCHY_ENABLE_WEBGPU|PATCHY_DAWN_(PREFIX|ROOT)|Qt6_DIR' \
  build/linux-release/CMakeCache.txt
```

A successful Dawn discovery adds `PATCHY_WEBGPU_AVAILABLE=1` to the `patchy_ui` compile definitions. The `*_AVAILABLE` names are CMake configure variables rather than user cache entries, so they may not appear in `CMakeCache.txt`. Check the generated target commands when the configure messages are not enough:

```sh
rg -n 'PATCHY_GPU_CANVAS|PATCHY_GPU_SHADER_COMPOSITOR|PATCHY_WEBGPU_AVAILABLE' \
  build/linux-release/build.ninja \
  build/linux-release/CMakeFiles 2>/dev/null
```

If `PATCHY_WEBGPU_AVAILABLE=1` is absent from the generated UI target, the runtime cannot activate WebGPU even when `PATCHY_RENDER_BACKEND=webgpu` is requested.

## What CMake success means

CMake options describe compiled capabilities, not a promise that a particular machine will use a GPU at runtime.

| Configure result | Application behavior |
|---|---|
| Qt base modules missing | The native application target is skipped. Core libraries and tests can still build. |
| Qt base plus Widgets available, Qt Quick missing | The application uses the QWidget/CPU canvas. |
| Qt Quick available, ShaderTools missing | The texture-only Qt RHI tier remains available. Shader-tier documents use CPU. |
| Qt Quick and ShaderTools available, Dawn missing | Qt RHI and CPU paths are available. WebGPU is unavailable. |
| Qt Quick, ShaderTools, and Dawn available | Qt RHI, shader-tier, WebGPU, and CPU fallback paths are compiled. Runtime capability checks still decide which path is used. |

The presence of `PATCHY_ENABLE_GPU_CANVAS=ON` in `CMakeCache.txt` does not prove that Qt Quick was found. Confirm the target and compile definition in the generated build files when necessary. The shader baker output may be embedded in generated Qt resources instead of appearing as a standalone `.qsb` file in the build directory.

## Build validation

The repository includes a lightweight validation that checks shell syntax, the complete SHA lock, the official repository URL, and the printed path configuration without downloading Dawn:

```sh
scripts/test-build-dawn.sh
```

The deterministic backend test must run with the offscreen platform:

```sh
QT_QPA_PLATFORM=offscreen \
./build/linux-release/patchy_ui_visual_tests \
  ui_canvas_renderer_selects_a_safe_runtime_backend
```

This test is expected to select CPU for `offscreen`, `minimal`, and `minimalegl`. That result is correct. It verifies safe selection and fallback, not hardware acceleration.

For a real native-window smoke test, open a small image with a normal desktop Qt platform. Do not set `QT_QPA_PLATFORM=offscreen`:

```sh
PATCHY_NO_SINGLE_INSTANCE=1 \
PATCHY_RENDER_BACKEND=auto \
QSG_INFO=1 \
QT_LOGGING_RULES='qt.scenegraph.general=true;qt.rhi.general=true' \
./build/linux-release/patchy \
  test-fixtures/af/tiny-rgba8.png 2>&1 | tee /tmp/patchy-gpu-runtime.log
```

A WebGPU run is confirmed only when the log contains a line similar to:

```text
Patchy WebGPU document compositor: Intel(R) Iris(R) Xe Graphics (ADL GT2), native API: Vulkan
```

A Qt RHI run is confirmed by a line similar to:

```text
Patchy graphics backend: OpenGL, adapter: Intel Iris Xe, hardware acceleration: yes
```

A line saying `Dawn/WebGPU was not found at configure time` means that the binary was built without the optional Dawn target. It is not a runtime GPU failure, and the Qt RHI/CPU fallback remains the expected behavior.

## Runtime selection variables

The build options and runtime variables solve different problems:

- `PATCHY_ENABLE_GPU_CANVAS=ON|OFF` controls whether the Qt Quick/RHI path is compiled.
- `PATCHY_ENABLE_WEBGPU=ON|OFF` controls whether CMake searches for Dawn.
- `PATCHY_RENDER_BACKEND=auto|webgpu|cpu|opengl|vulkan|metal|d3d11|d3d12` controls the preference of the running process.
- `PATCHY_GPU_CANVAS=auto|cpu` is the older compatibility alias.
- `QSG_RHI_BACKEND=opengl|vulkan|metal|d3d11|d3d12` is a Qt scene-graph diagnostic override. It does not enable a backend that was not compiled or supported by the Qt installation.
- `QSG_INFO=1` and `QT_LOGGING_RULES='qt.scenegraph.general=true;qt.rhi.general=true'` expose Qt graphics diagnostics.
- `PATCHY_NO_SINGLE_INSTANCE=1` prevents a diagnostic run from being forwarded to an already-running Patchy process.

For the complete capability and fallback policy, see [GPU canvas and document composition](gpu-canvas.md). For symptoms and recovery steps, see [GPU troubleshooting](gpu-troubleshooting.md).

## References

[1]: https://doc.qt.io/qt-6/qrhi.html "Qt QRhi documentation"
[2]: https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html "Qt Quick scene graph and graphics API documentation"
[3]: https://doc.qt.io/qt-6/qtshadertools-build.html "Qt ShaderTools and QSB build documentation"
[4]: https://dawn.googlesource.com/dawn/+/HEAD/docs/quickstart-cmake.md "Dawn CMake quickstart"
[5]: https://dawn.googlesource.com/dawn/+/HEAD/docs/building.md "Dawn build documentation"
[6]: https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html "CMake Presets documentation"

The Qt graphics API and scene-graph statements in this guide follow the Qt documentation.[1] [2] The QSB build integration follows the Qt ShaderTools documentation.[3] Dawn package and build statements follow the official Dawn documentation.[4] [5] Preset behavior is defined by CMake.[6]

**Document status:** This guide describes the current fork implementation. Update it whenever a graphics option, preset, package target, fallback reason, or validation command changes.

**License:** This documentation is distributed with the Patchy project under the project's applicable license.
