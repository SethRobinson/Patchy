# Hardware-agnostic GPU render graph

Patchy now has a small render-graph contract that can be validated without a display, a graphics driver, Qt Quick, or Dawn. This is an architectural foundation for later GPU tiers; it does **not** claim that every document is already rendered by the GPU.

## Why this layer exists

The application has one authoritative CPU compositor and optional GPU display paths. Advancing those paths safely requires a representation of resources, passes, dependencies, dirty regions, and device state that is independent of OpenGL, Vulkan, Metal, Direct3D, and WebGPU. The render graph provides that representation before a native backend is allowed to record commands.

A graph contains named resources and ordered pass descriptors. Resources have document bounds, a pixel format, and an optional external flag for imported or presentation-owned data. Passes declare reads, writes, a pass type, and a tile coordinate. Resource references are explicit, and a resource has at most one writer in a graph. The graph validator rejects unknown resources, duplicate references, empty resources, invalid pass shapes, multiple writers, and negative mip levels.

The execution order is a stable topological sort. Independent passes retain insertion order, so a CPU test, a fake backend, and a native backend receive the same deterministic order. Cycles are rejected instead of being silently linearized. This prevents a future backend from hiding an incorrect dependency behind driver-specific scheduling.

## Dirty regions and tiles

`DirtyRegionSet` stores document-space rectangles with their mip level. Empty rectangles are ignored. Regions at the same mip that overlap or touch are coalesced into one deterministic bounding rectangle; regions at different mips remain independent because they represent different derived resolutions.

`TileCache` now supports `invalidate_region()` for one mip and `invalidate_all_mips()` for a document-space region. Tile coordinates are interpreted using the cache tile size and the mip scale. These operations only remove intersecting cached tiles; they do not render, allocate GPU resources, or mutate document pixels. A later render scheduler can therefore turn a layer revision or effect-bounds change into a bounded set of graph passes without making the cache policy part of Qt or Dawn code.

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

The render graph is not permission to replace the CPU authority prematurely. The next native backend work must compare `render_cpu()` and `render_gpu()` on the same bounded scene and format, with a declared tolerance for display previews and exact byte identity for export paths. A graph pass may be promoted only after its inputs, blend equations, color space, alpha convention, clipping behavior, and invalidation bounds have an equivalence test.

Zero-copy presentation, shader implementations of all Photoshop filters, HDR/16-bit output, tiled scheduling, and complete device-loss recovery remain later milestones. They can now be developed against this contract and a fake backend first. Native validation on Intel, AMD, NVIDIA, macOS, and Windows is still required before those paths are advertised as production capabilities.

## Validation

The complete CPU core test executable includes the following hardware-free checks:

- dirty rectangles coalesce deterministically and ignore empty input;
- tile invalidation removes only intersecting tiles at one mip or across all mips;
- graph passes receive a stable dependency order;
- multiple writers and dependency cycles are rejected;
- a fake device rejects submission before initialization, reports loss, recovers, and accepts the graph again.

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
```

These commands do not download Dawn, open a window, or require a hardware adapter. The existing full CTest suite remains authoritative for document bytes, UI behavior, and export compatibility.
