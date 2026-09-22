# GPU troubleshooting and fallback guide

Patchy uses one desktop binary. GPU availability is decided in two stages:

1. CMake decides which optional code paths can be compiled.
2. The running process checks the graphics API, adapter, document capabilities, and current interaction state.

A failure at either stage is expected to select a lower tier. The final fallback is the existing QWidget canvas and CPU compositor. The fallback does not modify the document and does not require a second executable.

## Read the diagnostic in order

When investigating a graphics report, capture the complete configure output and the complete runtime log. Do not infer the active backend from `PATCHY_ENABLE_GPU_CANVAS=ON` alone.

At configure time, check these values:

```sh
grep -E 'Qt6_DIR|PATCHY_ENABLE_GPU_CANVAS|PATCHY_ENABLE_WEBGPU' \
  build/linux-release/CMakeCache.txt
```

The `*_AVAILABLE` names are configure variables and compile definitions, not guaranteed cache entries. If the command above does not show them, inspect the generated target files:

```sh
rg -n 'PATCHY_GPU_CANVAS|PATCHY_GPU_SHADER_COMPOSITOR|PATCHY_WEBGPU_AVAILABLE' \
  build/linux-release/build.ninja \
  build/linux-release/CMakeFiles 2>/dev/null
```

At runtime, use a real desktop platform and a document with visible content:

```sh
PATCHY_NO_SINGLE_INSTANCE=1 \
PATCHY_RENDER_BACKEND=auto \
QSG_INFO=1 \
QT_LOGGING_RULES='qt.scenegraph.general=true;qt.rhi.general=true' \
./build/linux-release/patchy \
  test-fixtures/af/tiny-rgba8.png 2>&1 | tee /tmp/patchy-gpu-runtime.log
```

The diagnostic line for the selected Qt path has this shape:

```text
Patchy graphics backend: <API>, adapter: <vendor and renderer>, hardware acceleration: yes
```

The diagnostic line for an active Dawn path has this shape:

```text
Patchy WebGPU document compositor: <adapter>, native API: <API>
```

A document-level fallback has this shape:

```text
Patchy GPU document compositor unavailable; using CPU compositor: <reason>
```

## Configure-time problems

### The application target is missing

If the build produces `patchy_core_tests` but no `patchy` or `patchy_ui_visual_tests`, CMake did not find the base Qt component set. The GPU option is not the cause. Inspect `Qt6_DIR` and the CMake configure output:

```sh
cmake --preset linux-release \
  -DQt6_DIR="$HOME/Qt/6.8.3/gcc_64/lib/cmake/Qt6" \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/gcc_64"
```

Remove the build directory before changing from a system Qt to a local Qt installation. CMake keeps package discovery in the cache.

### Qt Quick is not found

The application can still build with CPU rendering when Qt Widgets is available. CMake reports:

```text
Qt QuickWidgets was not found; using the automatic CPU canvas backend
```

Install a Qt distribution containing Qt Quick and Qt Quick Widgets, then configure again. Do not add `PATCHY_GPU_CANVAS=1` manually to compiler flags. CMake owns the feature definition.

### Qt ShaderTools is not found

The application can still build. CMake reports:

```text
Qt ShaderTools was not found; using the texture-only GPU canvas tier
```

Normal/source-over texture composition can remain available. Documents requiring the QSB shader tier use the complete CPU compositor. A standalone `.qsb` file is not a reliable test for this feature because `qt6_add_shaders` can place generated shader data in Qt resources. Check the configure message and the `PATCHY_GPU_SHADER_COMPOSITOR=1` definition in the generated UI target instead.

### Dawn/WebGPU is not found

The application can still build. CMake reports:

```text
Dawn/WebGPU was not found; automatic WebGPU document composition is disabled
```

When `PATCHY_DAWN_PREFIX` or `PATCHY_DAWN_ROOT` was supplied, the message names the configured local prefixes instead. Both messages mean that no supported linkable target was found; the Qt RHI and CPU paths remain available.

This means the configured CMake search did not find a linkable Dawn target. A WebGPU header by itself is insufficient. The build needs a package configuration file and a target named `dawn::webgpu_dawn`, `Dawn::webgpu_dawn`, or `webgpu_dawn`. This is an expected configuration state when Dawn was not requested.

The default GPU helper does not download Dawn. To obtain the pinned revision and verify its install package, run:

```sh
PATCHY_BUILD_JOBS=6 scripts/build-dawn.sh
```

Or let the GPU helper do both stages explicitly:

```sh
PATCHY_BUILD_JOBS=6 scripts/build-gpu.sh linux-release --with-dawn
```

The checkout, build tree, install prefix, and fetched dependency sources are under `.deps/dawn/`, which is ignored by Git. The helper refuses to overwrite a dirty Dawn checkout and verifies that `git rev-parse HEAD` equals the full SHA in `cmake/dawn-version.cmake`.

Find the package files in the Dawn prefix:

```sh
find "$PWD/.deps/dawn/install" -type f \
  \( -name 'DawnConfig.cmake' -o -name 'webgpu_dawnConfig.cmake' \) \
  -print
```

Then configure with the actual package directories:

```sh
PATCHY_DAWN_PREFIX="$PWD/.deps/dawn/install/release" \
PATCHY_BUILD_JOBS=6 scripts/build-gpu.sh linux-release
```

If the prefix does not follow the helper's conventional layout, pass `PATCHY_DAWN_PREFIX`, `Dawn_DIR`, and `webgpu_dawn_DIR` directly. Reconfigure from a clean build directory after changing the prefix. Dawn installation is platform-specific; do not copy a Linux prefix to macOS or Windows.

## Runtime fallback problems

### The program uses CPU even though Qt Quick was compiled

This is expected when the Qt scene graph cannot provide an accepted hardware adapter. Common causes are:

- the process is running with `QT_QPA_PLATFORM=offscreen`, `minimal`, or `minimalegl`;
- the native Qt platform cannot create a window or graphics context;
- the selected renderer is a known software implementation;
- the scene graph reports an error or loses its device;
- the requested API is unavailable on the current platform or Qt build.

Known software adapters include `llvmpipe`, `softpipe`, `swrast`, `lavapipe`, SwiftShader, WARP, and Microsoft Basic Render Driver. Software rendering is useful for tests, but Patchy does not report it as hardware acceleration.

Check the actual renderer outside Patchy when available:

```sh
glxinfo -B | grep -E 'OpenGL vendor|OpenGL renderer|OpenGL version'
```

An Intel, AMD, or NVIDIA renderer does not by itself prove that every document uses the GPU. The document capability check is a separate decision.

### The offscreen UI test says CPU

This is the correct result. The deterministic test intentionally uses `QT_QPA_PLATFORM=offscreen`. It verifies safe backend selection and fallback behavior, not hardware composition.

Run the test as follows:

```sh
QT_QPA_PLATFORM=offscreen \
./build/linux-release/patchy_ui_visual_tests \
  ui_canvas_renderer_selects_a_safe_runtime_backend
```

Use a native desktop session for a graphics smoke test. Do not use the offscreen plugin to prove that QSB or Dawn executed on a hardware device.

### `PATCHY_RENDER_BACKEND=webgpu` still falls back

The `webgpu` value is a preference, not a guarantee. The following outcomes are expected:

- `Dawn/WebGPU was not found at configure time`: the binary was built without Dawn support;
- the Dawn adapter is CPU or software: Patchy rejects it;
- the document dimensions are invalid: the incomplete frame is discarded;
- the document is outside the capability matrix: the complete document stays on CPU;
- an interactive preview or non-content channel is active: Patchy stays on the CPU compositor;
- device, shader, queue, or readback initialization fails: the complete frame falls back to Qt RHI or CPU.

Confirm the build flag before debugging the runtime:

```sh
rg -n 'PATCHY_WEBGPU_AVAILABLE' \
  build/linux-release/build.ninja \
  build/linux-release/CMakeFiles 2>/dev/null
```

`PATCHY_WEBGPU_AVAILABLE=1` is required in the generated UI target before a runtime WebGPU preference can succeed.

### `PATCHY_RENDER_BACKEND=auto` and `QSG_RHI_BACKEND` disagree

`PATCHY_RENDER_BACKEND` is Patchy's policy. `QSG_RHI_BACKEND` is a Qt scene-graph override used for diagnostics. Test one variable at a time:

```sh
PATCHY_NO_SINGLE_INSTANCE=1 \
PATCHY_RENDER_BACKEND=auto \
QSG_INFO=1 \
./build/linux-release/patchy test-fixtures/af/tiny-rgba8.png
```

Then, if needed, force a Qt API separately:

```sh
PATCHY_NO_SINGLE_INSTANCE=1 \
PATCHY_RENDER_BACKEND=opengl \
QSG_RHI_BACKEND=opengl \
QSG_INFO=1 \
./build/linux-release/patchy test-fixtures/af/tiny-rgba8.png
```

Forcing an unavailable or software API should produce a safe fallback. It should not make the process use software rendering as hardware acceleration.

### No graphics log appears

The application may have exited before constructing a scene graph, forwarded the request to another single-instance process, or never opened a document with visible content. Use:

```sh
PATCHY_NO_SINGLE_INSTANCE=1 \
PATCHY_RENDER_BACKEND=auto \
QSG_INFO=1 \
QT_LOGGING_RULES='qt.scenegraph.general=true;qt.rhi.general=true' \
./build/linux-release/patchy \
  test-fixtures/af/tiny-rgba8.png 2>&1 | tee /tmp/patchy-gpu-runtime.log
```

Check the process separately:

```sh
pgrep -af 'build/linux-release/patchy'
echo $?
```

A clean exit code does not prove that a graphics backend was initialized. The log must contain a Patchy backend or fallback line.

## Fallback policy by layer

| Failure or limitation | Result |
|---|---|
| Missing base Qt Widgets | Native application target is not produced. |
| Missing Qt Quick or Qt Quick Widgets | QWidget canvas and CPU compositor. |
| Missing Qt ShaderTools | Texture-only Qt RHI tier; shader-tier documents use CPU. |
| Missing Dawn | Qt RHI or CPU; WebGPU is unavailable. |
| No accepted hardware scene graph | QWidget canvas and CPU compositor. |
| Software renderer | QWidget canvas and CPU compositor. |
| Unsupported document feature | Complete document uses CPU, without mixing approximate GPU layers. |
| GPU device loss or incomplete frame | Incomplete frame is discarded and the complete document uses a lower tier. |
| Export or byte-identity validation | CPU compositor remains authoritative. |

The all-or-nothing document rule is deliberate. Patchy does not compose some layers approximately on the GPU while sending sibling layers through a different CPU path. That rule protects visual consistency and keeps CPU output authoritative until a feature has an equivalence test.

## Clean recovery procedure

When a graphics configuration appears inconsistent, use this sequence:

```sh
rm -rf build/linux-release
cmake --preset linux-release \
  -DPATCHY_ENABLE_GPU_CANVAS=ON \
  -DPATCHY_ENABLE_WEBGPU=ON
cmake --build --preset linux-release --parallel 6
```

Then inspect the cache, run the offscreen backend-selection test, and perform one native-window smoke test. Do not delete source files, generated test fixtures, or user documents as part of graphics troubleshooting.

## What to report in a bug or PR

Include the following information:

- operating system and architecture;
- Qt version and the value of `Qt6_DIR`;
- CMake preset and all explicit graphics options;
- the configure messages and generated compile definitions for `PATCHY_GPU_CANVAS`, `PATCHY_GPU_SHADER_COMPOSITOR`, and `PATCHY_WEBGPU_AVAILABLE`;
- `PATCHY_RENDER_BACKEND`, `QSG_RHI_BACKEND`, and `QT_QPA_PLATFORM`;
- adapter and renderer information from the runtime log;
- the first document fallback reason;
- whether the run used a native desktop platform or an offscreen platform;
- the exact build and test commands.

Do not report `PATCHY_ENABLE_GPU_CANVAS=ON` as proof of active GPU composition. Report the runtime diagnostic and the document fallback reason separately.

## References

[1]: https://doc.qt.io/qt-6/qrhi.html "Qt QRhi documentation"
[2]: https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html "Qt Quick scene graph and graphics API documentation"
[3]: https://doc.qt.io/qt-6/qtshadertools-build.html "Qt ShaderTools and QSB build documentation"
[4]: https://dawn.googlesource.com/dawn/+/HEAD/docs/quickstart-cmake.md "Dawn CMake quickstart"
[5]: https://dawn.googlesource.com/dawn/+/HEAD/docs/building.md "Dawn build documentation"

The distinction between Qt RHI, Qt Quick scene-graph selection, and the optional Dawn path follows the cited Qt and Dawn documentation.[1] [2] [3] [4] [5]

**Document status:** This guide describes the current fork implementation. Update it whenever a diagnostic string, fallback reason, CMake target, or runtime variable changes.

**License:** This documentation is distributed with the Patchy project under the project's applicable license.
