# GPU canvas and document composition

For dependency setup and platform-specific build commands, see [GPU build and dependency guide](gpu-build.md). For runtime diagnostics and fallback recovery, see [GPU troubleshooting](gpu-troubleshooting.md).

Patchy has one desktop binary with a native Qt Quick/RHI graphics path and an authoritative CPU compositor. The graphics path has two distinct responsibilities: it can compose a capability-supported document from layer textures on the GPU, and it can present the established CPU result when the document or platform requires it.

The GPU document path currently supports a complete top-level stack of 8-bit RGB/RGBA pixel layers. The source-over tier handles Normal blend mode, ordinary opacity, and fill opacity through the native Qt Quick scene graph. The shader tier adds the separable colour modes (`Multiply`, `Screen`, `Overlay`, `Darken`, `Lighten`, `Color Dodge`, `Color Burn`, `Hard Light`, `Soft Light`, `Difference`, `Linear Burn`, `Pin Light`, `Exclusion`, `Linear Dodge`, `Subtract`, `Divide`, `Vivid Light`, `Linear Light`, and `Hard Mix`) plus unfeathered grayscale raster masks, mask density, and supported RGB Blend If thresholds. Each layer is uploaded as a texture and composed in a portable QSB pass that Qt RHI translates for OpenGL, Vulkan, Metal, or Direct3D. When an external Dawn installation is available, the same capability tiers can instead be composed by a WebGPU compute pipeline and read back as one complete frame for presentation by Qt. The CPU path remains authoritative for every feature that is not yet shader-equivalent.

This is intentionally all-or-nothing per document. Patchy never mixes an approximate GPU layer with CPU-composed siblings. A document containing a group, a non-separable blend mode, a feathered mask, adjustment, vector or text content, layer style, smart filter, an unsupported Blend If payload, channel restriction, clipped layer, or unsupported pixel format stays on the CPU compositor until an equivalent GPU implementation is available.

## Build

Desktop builds enable the graphics path by default:

```sh
cmake --preset qt-local -DPATCHY_ENABLE_GPU_CANVAS=ON
cmake --build --preset qt-local -j6
```

For a repeatable local build that also runs the core suite and the deterministic
backend-selection test, use the repository helper:

```sh
scripts/build-gpu.sh qt-local
```

The helper accepts `--skip-tests`, reads `PATCHY_BUILD_JOBS`, and accepts
`PATCHY_QT_PREFIX` and `PATCHY_DAWN_PREFIX` when Qt or Dawn is installed outside
the preset's default prefix. It always requests the GPU canvas, but it never
turns an absent optional dependency into a configure error.

The optional modules are `Qt Quick`, `Qt Quick Widgets`, and `Qt ShaderTools`. If Qt Quick is unavailable, CMake keeps the application target and compiles the CPU surface implementation from the same source tree. If ShaderTools is unavailable, Normal/source-over GPU presentation remains available, while shader-tier documents use the CPU compositor. `-DPATCHY_ENABLE_GPU_CANVAS=OFF` forces the CPU-only build. Dawn is also optional: `-DPATCHY_ENABLE_WEBGPU=ON` asks CMake to discover an installed Dawn package, but a missing package never fails the application build. WebAssembly keeps its existing browser rendering path and does not enable this desktop surface.

The output is one desktop binary. A machine without a usable graphics device does not need a second executable: the scene-graph failure or software-adapter check returns the process to the QWidget/CPU path, and a document outside the current GPU capability matrix also returns to the CPU compositor.

## Runtime graphics API selection

`PATCHY_RENDER_BACKEND` controls the preference for the current process. The default is `auto`.

| Value | Requested Qt Quick API | Behavior |
|---|---|---|
| `auto` | `Unknown` | Lets Qt choose the platform-native RHI backend, then accepts it only if it is hardware accelerated |
| `webgpu` | Automatic Dawn probe plus Qt presentation | Tries the optional Dawn/WebGPU document compositor first; if Dawn, its adapter, or a document pass is unavailable, continues with the ordinary Qt RHI/CPU fallback |
| `cpu` | No Qt Quick surface | Uses the ordinary QWidget canvas and CPU compositor |
| `opengl` | OpenGL | Uses OpenGL when the context is available and the renderer is not a known software implementation |
| `vulkan` | Vulkan | Uses Vulkan when the scene graph and physical device are available |
| `metal` | Metal | Uses Metal on macOS when available |
| `d3d11` | Direct3D 11 | Uses Direct3D 11 on Windows when available |
| `d3d12` | Direct3D 12 | Uses Direct3D 12 on Qt versions that expose that scene-graph backend |

The older `PATCHY_GPU_CANVAS=auto` and `PATCHY_GPU_CANVAS=cpu` spellings remain accepted as compatibility aliases. An unknown value is treated as `auto` and is reported in the diagnostic log. In `auto`, Dawn is attempted before the Qt document path only when it was found at configure time; no environment variable is required to activate it.

On supported desktop platforms, automatic selection follows Qt's native scene-graph policy. Linux normally chooses an available Vulkan or OpenGL path, Windows normally chooses Direct3D, and macOS normally chooses Metal. The exact choice remains a Qt and driver decision rather than a compile-time preprocessor branch.

## Capability and fallback policy

The surface starts in an initializing state. After Qt Quick initializes its scene graph, Patchy reads `QSGRendererInterface::graphicsApi()`. Qt Quick's software scene graph, null backend, unknown APIs, scene-graph errors, and lost graphics devices select the CPU surface instead.

For OpenGL, Patchy reads the renderer and vendor strings through the scene-graph context. For Vulkan, it reads the physical-device properties. For Direct3D, it checks the DXGI software-adapter flag. Known software implementations such as `llvmpipe`, `softpipe`, `swrast`, `lavapipe`, SwiftShader, WARP, and Microsoft Basic Render Driver are rejected. Software rendering remains useful for test infrastructure, but it is not reported as hardware GPU acceleration.

After the graphics device is accepted, a separate document capability check decides whether the GPU compositor may be used. The decision is conservative and all-or-nothing:

- `PixelStackSourceOver`: visible top-level 8-bit RGB/RGBA pixel layers, Normal blend mode, ordinary/fill opacity, matching bounds, and no enabled masks or effects are composed as GPU textures;
- `PixelStackShader`: the same stack may additionally use the supported separable blend modes, an unfeathered gray8 raster mask, and an RGB Blend If payload with ordered 8-bit thresholds; each layer is evaluated by a QSB fragment pass that samples the accumulated backdrop texture and applies the source and underlying-layer factors before blending;
- `Unsupported`: the complete document is rendered by the existing CPU compositor, with a diagnostic explaining the first unsupported feature.

Blend If support is deliberately limited to the RGB Photoshop record shape
already modeled by `LayerBlendIf`. The shader evaluates Gray, Red, Green, and
Blue in that order. Split-handle transitions use the same inclusive endpoint
rule as the CPU helper. Empty or identity ranges do not force the shader tier;
malformed, non-RGB, or otherwise unsupported payloads keep the document on CPU.

A typical GPU-composition diagnostic is:

```text
Patchy graphics backend: Vulkan, adapter: Intel Iris Xe, hardware acceleration: yes
```

A typical document fallback is:

```text
Patchy GPU document compositor unavailable; using CPU compositor: document contains a non-separable or unsupported blend mode
```

When Dawn is present and the adapter is hardware-backed, the log also identifies the WebGPU implementation and its native API:

```text
Patchy WebGPU document compositor active on Intel(R) Iris(R) Xe Graphics via Vulkan ; render-graph passes: <N>
```

The WebGPU route is a separate Dawn document compositor, not a claim that WebGPU is a Qt RHI backend. Qt Quick still owns the desktop widget, input surface, overlays, and final presentation. Dawn is rejected when it reports a CPU/software adapter, and any initialization, shader, queue, or readback error discards the incomplete GPU frame and keeps the complete document on the Qt RHI or CPU path. The Dawn kernel uses the same Blend If threshold contract as the QSB path; the two implementations are display paths, not export authorities.

The fallback is in-process and does not change the document. The same `CanvasWidget` continues to own input, scrollbars, selection geometry, tool state, and overlays. During GPU composition, a transparent QWidget overlay keeps those controls and guides above the Qt Quick layer tree.

## Hardware-agnostic render graph and equivalence foundation

The next GPU work starts with a Qt-free render graph rather than another native API
branch. `src/render/gpu_render_graph.hpp` describes named resources and passes,
validates single-writer dependencies, and produces a stable topological order.
`DirtyRegionSet` coalesces document-space invalidations by mip, `GpuTileScheduler`
turns them into bounded graph passes, and `TileCache` invalidates only tiles that
intersect a changed region. `GpuRenderBackend` defines the common submission and
device-loss contract, while the test-only fake renderer assembles CPU-backed tiles
and checks them against the full CPU compositor. See [Hardware-agnostic GPU render
graph](gpu-render-graph.md) for the state machine, comparison policy, validation
commands, and the boundary between this foundation and future native backends.

This foundation is deliberately not a claim of zero-copy presentation, complete
Photoshop filter coverage, or hardware performance. The current equivalence tests
use the CPU compositor to emulate missing tiles; they do not exercise a physical
GPU. A future native pass must first have CPU/GPU equivalence coverage for its
format, alpha convention, color space, clipping, and invalidation bounds. Until
then, the CPU compositor remains the authority for export, byte identity, and
unsupported documents.

When the optional Dawn prefix is found at configure time, `WebGpuRenderBackend`
adds one more gate before the existing `WebGpuDocumentCompositor` publishes a
frame. It validates a full or dirty render graph and executes its mip-0 tiles
with independent compute passes and regional readbacks. When a valid previous
frame exists, clean tiles are reused and only tiles intersecting the document
dirty region are rebuilt. This is real WebGPU tile execution, but it is not
zero-copy presentation: the assembled `QImage` still crosses into the existing
Qt Quick surface. If initialization, graph validation, composition, any tile
readback, or recovery fails, the document remains on the Qt RHI/CPU path.

## Testing

The core capability tests accept simple pixel stacks, separable shader blend modes, unfeathered gray8 masks, and supported Blend If payloads. They also require malformed Blend If records to select CPU without mixing rendering paths. The UI backend test accepts the initializing state and every supported native RHI backend, while requiring CPU on `offscreen`, `minimal`, and `minimalegl` platforms. A native-window smoke test must also exercise at least one QSB pass; an offscreen-only test cannot validate ShaderEffect because the software scene graph intentionally bypasses hardware composition.

A real desktop smoke test must run with the native Qt platform plugin. The offscreen test platform intentionally selects CPU because it has no display surface and is not a hardware-acceleration test. The CPU compositor corpus remains the authority for pixel and file-output validation in every mode. GPU-composed documents must gain explicit CPU/GPU equivalence coverage before additional feature classes are enabled.

## Scope and expansion boundary

This stage is a real GPU document compositor for the supported pixel-stack and shader capability tiers, not merely a presentation of a pre-composed `QImage`. It is not yet a GPU implementation of every Patchy feature, and floating-point shader output is not used for exports or byte-identity decisions. The optional Dawn path executes bounded mip-0 tiles and reads back only tiles selected by the full-frame or dirty-region plan. It still uploads source layer textures for each composition call and copies the assembled `QImage` through the existing Qt Quick surface; portable zero-copy interoperation remains platform-specific. The next capability tiers require backend-portable shaders and equivalence tests for separable-mode rounding, non-separable blend modes, groups, adjustment layers, layer styles, filters, vectors, text, smart objects, color management, and Photoshop-specific rounding rules. Until each tier is validated, its documents continue to use the byte-authoritative CPU compositor.
