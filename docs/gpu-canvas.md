# GPU canvas presentation

Patchy remains CPU-authoritative for document compositing, PSD compatibility, export, and byte-identity tests. One desktop binary can use Qt's `QOpenGLWidget` as the canvas surface so `QPainter` presents the existing render cache, zooms, pans, images, and canvas overlays through the system OpenGL paint engine.

This is a **presentation-layer acceleration**, not yet a GPU compositor. The expensive layer compositor continues to run on the CPU and retains its existing multithreaded strip renderer. The application probes an offscreen OpenGL context before creating the GPU surface. If the probe fails, if Qt cannot create the widget context, or if the context is later lost, the same `CanvasWidget` continues painting through its CPU `paintEvent` path.

## Build

Desktop builds include the backend by default. Enable it explicitly with:

```sh
cmake --preset qt-local -DPATCHY_ENABLE_GPU_CANVAS=ON
cmake --build --preset qt-local
```

The option adds the Qt `OpenGLWidgets` component. If that optional module is missing, CMake keeps the native application target and builds the automatic CPU backend instead. `-DPATCHY_ENABLE_GPU_CANVAS=OFF` forces the CPU-only build. WebAssembly keeps its existing Qt/WebGL integration path and does not use this desktop backend.

## Runtime selection

| Runtime condition | Canvas surface | Document pixels and file output |
|---|---|---|
| Desktop Qt with a valid OpenGL context | `QOpenGLWidget` and GPU-backed `QPainter` presentation | CPU compositor remains authoritative |
| Desktop Qt without a usable OpenGL context, including `offscreen`, `minimal`, and `minimalegl` test platforms | Ordinary `QWidget` painting | CPU compositor remains authoritative |
| Desktop build without the `OpenGLWidgets` module | Ordinary `QWidget` painting | CPU compositor remains authoritative |
| WebAssembly | Existing browser/WebGL integration | Existing wasm behavior |

## Scope and fallback

The CPU compositor still creates the `QImage` render cache. The OpenGL canvas accelerates the subsequent painting of that cache and of the canvas overlays, including scaled image presentation during zoom and pan. Document edits, filters, masks, blend modes, layer styles, PSD saving, and export continue to use the established CPU path. A machine or driver without OpenGL support does not need a separate package: runtime selection uses the CPU surface automatically.

The default build remains a single binary with runtime selection. To compare presentation paths, compile once with `PATCHY_ENABLE_GPU_CANVAS=OFF` and once with it enabled, then use the existing stress harness and visual tests. The GPU mode should be treated as a display optimization: it must not change document data or compatibility behavior.

The UI test `ui_canvas_renderer_selects_a_safe_runtime_backend` verifies that the offscreen test platform selects the CPU surface and that a visible canvas always reports a valid backend enum. Run it with the normal UI test binary using the existing name filter. A desktop smoke run must use the real display backend rather than `QT_QPA_PLATFORM=offscreen`, because offscreen intentionally selects CPU and has no OpenGL window surface.

## Next GPU phase

The next step would be a separate GPU preview compositor with an explicit capability matrix and automatic CPU fallback. It should begin with ordinary RGBA8 layers, opacity, transforms, and a small set of blend modes. The CPU compositor must remain authoritative for final renders until GPU output is covered by the existing corpus and visual-equivalence tests. Metal and Vulkan-specific compositor paths are intentionally not claimed by this phase; Qt's OpenGL widget is the portable presentation backend currently implemented.
