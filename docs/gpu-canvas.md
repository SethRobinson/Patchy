# GPU canvas presentation

Patchy remains CPU-authoritative for document compositing, PSD compatibility, export, and byte-identity tests. Desktop builds can optionally use Qt's `QOpenGLWidget` as the canvas surface so `QPainter` presents the existing render cache, zooms, pans, images, and canvas overlays through the system OpenGL paint engine.

This is a **presentation-layer acceleration**, not yet a GPU compositor. The expensive layer compositor continues to run on the CPU and retains its existing multithreaded strip renderer. The normal build remains the compatibility fallback for systems where the Qt OpenGL widget cannot be used.

## Build

The feature is off by default. Enable it on desktop Qt builds with:

```sh
cmake --preset qt-local -DPATCHY_ENABLE_GPU_CANVAS=ON
cmake --build --preset qt-local
```

The option adds the Qt `OpenGLWidgets` component and compiles `CanvasWidget` as a `QOpenGLWidget`. WebAssembly builds reject this option for now; the browser build keeps its existing Qt/WebGL integration path.

## Scope and fallback

The CPU compositor still creates the `QImage` render cache. The OpenGL canvas accelerates the subsequent painting of that cache and of the canvas overlays, including scaled image presentation during zoom and pan. Document edits, filters, masks, blend modes, layer styles, PSD saving, and export continue to use the established CPU path. If a machine or driver cannot provide the required Qt OpenGL context, use the default build with `PATCHY_ENABLE_GPU_CANVAS=OFF`.

The default build remains unchanged. To compare builds, compile once with `PATCHY_ENABLE_GPU_CANVAS=OFF` and once with it enabled, then use the existing stress harness and visual tests. The GPU mode should be treated as a display optimization: if a driver or platform has an OpenGL problem, rebuild without the option rather than changing document data or compatibility behavior.

## Next GPU phase

The next step would be a separate GPU preview compositor with an explicit capability matrix and automatic CPU fallback. It should begin with ordinary RGBA8 layers, opacity, transforms, and a small set of blend modes. The CPU compositor must remain authoritative for final renders until GPU output is covered by the existing corpus and visual-equivalence tests.
