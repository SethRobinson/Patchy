# GPU canvas presentation

Patchy keeps the CPU compositor as the reference renderer for document pixels, PSD compatibility, export, and byte-identity tests. The desktop binary can now present that CPU-composed frame through Qt Quick Scene Graph and Qt RHI. Qt Quick selects the native graphics API for the platform, while Patchy verifies that the selected scene graph is hardware accelerated before it replaces the ordinary QWidget paint path.

This is presentation-layer acceleration, not GPU document compositing. The current path does not send individual layers or filter kernels to the GPU: the CPU compositor first produces the authoritative `QImage` frame, and Qt Quick/RHI then uploads and presents that frame through the selected backend. Layer blending, masks, filters, adjustment layers, layer styles, PSD saving, and export remain on the established CPU path. Zoom, pan, and canvas overlays therefore use the same pixels and geometry as the CPU path.

## Build

Desktop builds enable the feature by default:

```sh
cmake --preset qt-local -DPATCHY_ENABLE_GPU_CANVAS=ON
cmake --build --preset qt-local
```

The optional modules are `Qt Quick` and `Qt Quick Widgets`. If they are unavailable, CMake keeps the application target and compiles the CPU surface implementation from the same source tree. `-DPATCHY_ENABLE_GPU_CANVAS=OFF` forces the CPU-only build. WebAssembly keeps its existing browser rendering path and does not enable this desktop surface.

The output is one desktop binary. It does not require a second executable for machines without a GPU, a native graphics driver, or the optional Qt Quick modules used at build time.

## Runtime selection

`PATCHY_RENDER_BACKEND` controls the preference for the current process. The default is `auto`.

| Value | Requested Qt Quick API | Behavior |
|---|---|---|
| `auto` | `Unknown` | Lets Qt choose the platform-native RHI backend, then accepts it only if it is hardware accelerated |
| `cpu` | No Qt Quick surface | Uses the ordinary QWidget canvas |
| `opengl` | OpenGL | Uses OpenGL when the context is available and the renderer is not a known software implementation |
| `vulkan` | Vulkan | Uses Vulkan when the scene graph and physical device are available |
| `metal` | Metal | Uses Metal on macOS when available |
| `d3d11` | Direct3D 11 | Uses Direct3D 11 on Windows when available |
| `d3d12` | Direct3D 12 | Uses Direct3D 12 on Qt versions that expose that scene graph backend |

The older `PATCHY_GPU_CANVAS=auto` and `PATCHY_GPU_CANVAS=cpu` spellings remain accepted as compatibility aliases. An unknown value is treated as `auto` and is reported in the diagnostic log.

On the supported desktop platforms, automatic selection follows Qt's native scene graph policy. Linux normally chooses an available Vulkan or OpenGL path, Windows normally chooses Direct3D, and macOS normally chooses Metal. The exact choice remains a Qt and driver decision rather than a compile-time preprocessor branch.

## Capability and fallback policy

The surface starts in an initializing state. After Qt Quick initializes its scene graph, Patchy reads `QSGRendererInterface::graphicsApi()`. Qt Quick's `Software` scene graph, `Null` backend, unknown APIs, scene graph errors, and lost graphics devices all select the CPU surface instead.

For OpenGL, Patchy reads the renderer and vendor strings through the scene graph context. For Vulkan, it reads the physical device properties. For Direct3D, it checks the DXGI software-adapter flag. Known software implementations such as `llvmpipe`, `softpipe`, `swrast`, `lavapipe`, `SwiftShader`, WARP, and Microsoft Basic Render Driver are rejected. A software implementation is still useful for test infrastructure, but it is not reported as GPU acceleration for the canvas.

The fallback is in-process and does not change the document. A typical diagnostic is:

```text
Patchy graphics backend: Vulkan, adapter: Intel Iris Xe, hardware acceleration: yes
```

or:

```text
Patchy GPU presentation unavailable; using CPU canvas: Qt Quick selected its software scene graph
```

The same `CanvasWidget` continues to own input, scrollbars, tool overlays, selection geometry, and document state. The optional surface is transparent to mouse and keyboard input, so it cannot intercept painting, selection, or tool gestures.

## Testing

The UI test `ui_canvas_renderer_selects_a_safe_runtime_backend` accepts the initializing state and every supported native RHI backend, while requiring CPU on `offscreen`, `minimal`, and `minimalegl` platforms. The source is also checked in both modes: with `PATCHY_GPU_CANVAS` enabled, the Qt Quick implementation is compiled; without it, the surface is a no-op CPU fallback.

A real desktop smoke test must run with the native Qt platform plugin. The offscreen test platform intentionally selects CPU because it has no display surface and is not a hardware acceleration test. The CPU compositor corpus remains the authority for pixel and file-output validation in every mode.

## Scope boundary

The patch provides a single presentation architecture for OpenGL, Vulkan, Metal, Direct3D, and CPU fallback. It does not move layer compositing to shaders or replace the CPU compositor. A future GPU compositor would need separate capability coverage, shader equivalence tests, color-management validation, and explicit byte-authoritative fallback rules before it could be used for document rendering.
