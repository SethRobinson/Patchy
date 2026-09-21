# GPU canvas and document composition

Patchy has one desktop binary with a native Qt Quick/RHI graphics path and an authoritative CPU compositor. The graphics path has two distinct responsibilities: it can compose a capability-supported document from layer textures on the GPU, and it can present the established CPU result when the document or platform requires it.

The GPU document path currently supports a complete top-level stack of 8-bit RGB/RGBA pixel layers using source-over alpha and Normal blend mode. It uploads each layer as a texture and lets the Qt Quick scene graph position, scale, filter, and composite those layers. The CPU path remains authoritative for every feature that is not yet shader-equivalent.

This is intentionally all-or-nothing per document. Patchy never mixes an approximate GPU layer with CPU-composed siblings. A document containing a group, non-Normal blend mode, mask, adjustment, vector or text content, layer style, smart filter, Blend If rule, channel restriction, clipped layer, or unsupported pixel format stays on the CPU compositor until an equivalent GPU implementation is available.

## Build

Desktop builds enable the graphics path by default:

```sh
cmake --preset qt-local -DPATCHY_ENABLE_GPU_CANVAS=ON
cmake --build --preset qt-local -j6
```

The optional modules are `Qt Quick` and `Qt Quick Widgets`. If they are unavailable, CMake keeps the application target and compiles the CPU surface implementation from the same source tree. `-DPATCHY_ENABLE_GPU_CANVAS=OFF` forces the CPU-only build. WebAssembly keeps its existing browser rendering path and does not enable this desktop surface.

The output is one desktop binary. A machine without a usable graphics device does not need a second executable: the scene-graph failure or software-adapter check returns the process to the QWidget/CPU path, and a document outside the current GPU capability matrix also returns to the CPU compositor.

## Runtime graphics API selection

`PATCHY_RENDER_BACKEND` controls the preference for the current process. The default is `auto`.

| Value | Requested Qt Quick API | Behavior |
|---|---|---|
| `auto` | `Unknown` | Lets Qt choose the platform-native RHI backend, then accepts it only if it is hardware accelerated |
| `cpu` | No Qt Quick surface | Uses the ordinary QWidget canvas and CPU compositor |
| `opengl` | OpenGL | Uses OpenGL when the context is available and the renderer is not a known software implementation |
| `vulkan` | Vulkan | Uses Vulkan when the scene graph and physical device are available |
| `metal` | Metal | Uses Metal on macOS when available |
| `d3d11` | Direct3D 11 | Uses Direct3D 11 on Windows when available |
| `d3d12` | Direct3D 12 | Uses Direct3D 12 on Qt versions that expose that scene-graph backend |

The older `PATCHY_GPU_CANVAS=auto` and `PATCHY_GPU_CANVAS=cpu` spellings remain accepted as compatibility aliases. An unknown value is treated as `auto` and is reported in the diagnostic log.

On supported desktop platforms, automatic selection follows Qt's native scene-graph policy. Linux normally chooses an available Vulkan or OpenGL path, Windows normally chooses Direct3D, and macOS normally chooses Metal. The exact choice remains a Qt and driver decision rather than a compile-time preprocessor branch.

## Capability and fallback policy

The surface starts in an initializing state. After Qt Quick initializes its scene graph, Patchy reads `QSGRendererInterface::graphicsApi()`. Qt Quick's software scene graph, null backend, unknown APIs, scene-graph errors, and lost graphics devices select the CPU surface instead.

For OpenGL, Patchy reads the renderer and vendor strings through the scene-graph context. For Vulkan, it reads the physical-device properties. For Direct3D, it checks the DXGI software-adapter flag. Known software implementations such as `llvmpipe`, `softpipe`, `swrast`, `lavapipe`, SwiftShader, WARP, and Microsoft Basic Render Driver are rejected. Software rendering remains useful for test infrastructure, but it is not reported as hardware GPU acceleration.

After the graphics device is accepted, a separate document capability check decides whether the GPU compositor may be used. The decision is conservative and all-or-nothing:

- `PixelStackSourceOver`: visible top-level 8-bit RGB/RGBA pixel layers, Normal blend mode, ordinary opacity, matching bounds, and no masks or effects are composed as GPU textures;
- `Unsupported`: the complete document is rendered by the existing CPU compositor, with a diagnostic explaining the first unsupported feature.

A typical GPU-composition diagnostic is:

```text
Patchy graphics backend: Vulkan, adapter: Intel Iris Xe, hardware acceleration: yes
```

A typical document fallback is:

```text
Patchy GPU document compositor unavailable; using CPU compositor: document contains a non-Normal blend mode
```

The fallback is in-process and does not change the document. The same `CanvasWidget` continues to own input, scrollbars, selection geometry, tool state, and overlays. During GPU composition, a transparent QWidget overlay keeps those controls and guides above the Qt Quick layer tree.

## Testing

The core capability tests accept simple pixel stacks and require unsupported blend modes to select CPU without mixing rendering paths. The UI backend test accepts the initializing state and every supported native RHI backend, while requiring CPU on `offscreen`, `minimal`, and `minimalegl` platforms.

A real desktop smoke test must run with the native Qt platform plugin. The offscreen test platform intentionally selects CPU because it has no display surface and is not a hardware-acceleration test. The CPU compositor corpus remains the authority for pixel and file-output validation in every mode. GPU-composed documents must gain explicit CPU/GPU equivalence coverage before additional feature classes are enabled.

## Scope and expansion boundary

This stage is a real GPU document compositor for the supported pixel-stack capability, not merely a presentation of a pre-composed `QImage`. It is not yet a GPU implementation of every Patchy feature. The next capability tiers require backend-portable shaders and equivalence tests for blend modes, masks, groups, adjustment layers, layer styles, filters, vectors, text, smart objects, color management, and Photoshop-specific rounding rules. Until each tier is validated, its documents continue to use the byte-authoritative CPU compositor.
