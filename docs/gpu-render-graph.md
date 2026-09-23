# Hardware-agnostic GPU render graph

Patchy now has a small render-graph contract that can be validated without a display, a graphics driver, Qt Quick, or Dawn. This is an architectural foundation for later GPU tiers; it does **not** claim that every document is already rendered by the GPU.

## Why this layer exists

The application has one authoritative CPU compositor and optional GPU display paths. Advancing those paths safely requires a representation of resources, passes, dependencies, dirty regions, and device state that is independent of OpenGL, Vulkan, Metal, Direct3D, and WebGPU. The render graph provides that representation before a native backend is allowed to record commands.

A graph contains named resources and ordered pass descriptors. Resources have document bounds, a pixel format, and an optional external flag for imported or presentation-owned data. Passes declare reads, writes, a pass type, and a tile coordinate. Resource references are explicit, and a resource has at most one writer in a graph. The graph validator rejects unknown resources, duplicate references, empty resources, invalid pass shapes, multiple writers, and negative mip levels.

The execution order is a stable topological sort. Independent passes retain insertion order, so a CPU test, a fake backend, and a native backend receive the same deterministic order. Cycles are rejected instead of being silently linearized. This prevents a future backend from hiding an incorrect dependency behind driver-specific scheduling.

## Dirty regions and tiles

`DirtyRegionSet` stores document-space rectangles with their mip level. Empty rectangles are ignored. Regions at the same mip that overlap or touch are coalesced into one deterministic bounding rectangle; regions at different mips remain independent because they represent different derived resolutions.

`TileCache` now supports `invalidate_region()` for one mip and `invalidate_all_mips()` for a document-space region. `GpuTileScheduler` turns a full or dirty region set into stable tile keys and emits `Clear -> Composite -> Readback` passes for each tile. Tile coordinates are interpreted using the cache tile size and the mip scale. These operations only remove intersecting cached tiles; they do not allocate native GPU resources or mutate document pixels. A native backend can therefore consume bounded graph work without making the cache policy part of Qt or Dawn code.

## Backend contract and device loss

`GpuRenderBackend` is intentionally Qt-free. An adapter must expose initialization, graph submission, recovery, a state, a diagnostic, and static backend information. Submission can report `Submitted`, `InvalidGraph`, `DeviceLost`, or `BackendError`. The expected state machine is:

| State | Meaning | Allowed recovery |
|---|---|---|
| `Uninitialized` | No native device or recording context is ready. | `initialize()` may enter `Ready`. |
| `Ready` | Graphs may be validated and submitted. | A submission may move the adapter to `Lost` after a driver error. |
| `Lost` | Existing native resources must not be reused. | `recover()` recreates device-owned state and returns to `Ready`. |
| `Failed` | Initialization or recreation cannot proceed. | The application selects the CPU compositor. |

The current `FakeGpuBackend` records pass ids and simulates loss/recovery. It exists only in the core test support; it is not a production renderer and it reports `hardware_accelerated=false`. Its purpose is to verify that graph validation, fallback decisions, and resource recreation logic do not require a physical GPU.

## CPU/GPU equivalence boundary

`compare_pixel_buffers()` now provides the first structured comparison contract. It reports dimensions/format comparability, differing pixels and channels, maximum channel error, mean absolute channel error, and the differing-pixel fraction. `PixelComparisonPolicy` makes the acceptance rule explicit: preview paths may declare a small tolerance, while export and byte-identity paths use a zero-delta policy. The comparison is RGB8-specific today because the current authoritative flatten is RGB8; it does not silently coerce formats.

`FakeGpuDocumentRenderer` is the first execution oracle for this contract. It submits the generated graph to the fake backend, renders missing mip-0 tiles with `Compositor::flatten_rgb8_region()`, updates only invalidated tiles, and compares the assembled frame against `Compositor::flatten_rgb8()`. It is deliberately CPU-backed and test-only: it proves graph scheduling, bounded invalidation, cache reuse, and recovery semantics without pretending to measure GPU throughput.

The render graph is not permission to replace the CPU authority prematurely. A native backend must compare `render_cpu()` and `render_gpu()` on the same bounded scene and format, with a declared tolerance for display previews and exact byte identity for export paths. A graph pass may be promoted only after its inputs, blend equations, color space, alpha convention, clipping behavior, and invalidation bounds have an equivalence test.

## Dawn bridge

When `PATCHY_ENABLE_WEBGPU=ON` finds the pinned Dawn package, `WebGpuRenderBackend` connects the existing real WebGPU compositor to this contract. For each fresh document frame it builds the full mip-0 tile plan, validates the graph's dependency order, and records the accepted pass count before invoking Dawn's compute composition. Dawn still performs one complete-document composition and one readback at this stage; the graph is the validated scheduling boundary, not yet a pass-by-pass native executor.

The bridge is intentionally all-or-nothing. Adapter creation, graph validation, queue composition, readback, or device recovery may fail without publishing a partial frame. The caller then keeps the individual Qt RHI layers or the CPU compositor. A successful Dawn frame is presented by the existing Qt Quick surface, so this step does not add a second window, input path, or mandatory WebGPU dependency.

Zero-copy presentation, shader implementations of all Photoshop filters, HDR/16-bit output, native device-loss recovery, and real GPU tile execution remain later milestones. The tile scheduler, comparison policy, and logical recovery path can now be developed against the fake backend first. Native validation on Intel, AMD, NVIDIA, macOS, and Windows is still required before those paths are advertised as production capabilities.

## Validation

The complete CPU core test executable includes the following hardware-free checks:

- dirty rectangles coalesce deterministically and ignore empty input;
- tile invalidation removes only intersecting tiles at one mip or across all mips;
- graph passes receive a stable dependency order;
- multiple writers and dependency cycles are rejected;
- a fake device rejects submission before initialization, reports loss, recovers, and accepts the graph again;
- RGB8 comparison reports exact differences and accepts only an explicitly declared preview tolerance;
- a tiled fake render matches the full CPU compositor, re-renders one dirty tile, and reuses clean tiles;
- logical device loss clears cached resources, rebuilds all tiles, and returns to CPU-equivalent output;
- unsupported documents are rejected before a fake GPU frame can be mixed with the CPU path.

Build and run the checks with:

```sh
cmake -S . -B /tmp/patchy-build -G Ninja \
  -DPATCHY_BUILD_APP=OFF \
  -DPATCHY_BUILD_TESTS=ON \
  -DPATCHY_ENABLE_GPU_CANVAS=OFF \
  -DPATCHY_ENABLE_WEBGPU=OFF
cmake --build /tmp/patchy-build --target patchy_core_tests -j4
/tmp/patchy-build/patchy_core_tests render_graph_
/tmp/patchy-build/patchy_core_tests dirty_regions_
/tmp/patchy-build/patchy_core_tests tile_cache_invalidates_regions
/tmp/patchy-build/patchy_core_tests pixel_comparison
/tmp/patchy-build/patchy_core_tests gpu_tile_renderer
```

These commands do not download Dawn, open a window, or require a hardware adapter. The existing full CTest suite remains authoritative for document bytes, UI behavior, and export compatibility.
