# Photoslop Roadmap

## Phase 0: Foundation

- CMake/vcpkg project structure.
- Core document/layer/pixel model.
- CPU compositor and tile cache.
- Flat RGB8 PSD reader/writer plus layered RGB/RGBA pixel-layer round trips.
- Qt 6 app shell with a tool palette, editable pixel layers, history, and properties panels.
- Plug-in/filter/format registries.
- CI, packaging notes, and tests.

## Phase 1: Professional v1

- Layered PSD/PSB read/write with unknown block preservation.
- Full layer stack UI, masks, selections, transforms, history, and basic tool system.
- ICC display transforms via LittleCMS and scene/view transforms via OpenColorIO.
- Brush engine with tablet pressure and low-latency preview.
- Robust import/export through OpenImageIO and direct codec handlers.
- Out-of-process Photoslop plug-in runtime.

## Phase 2: Advanced Editing

- Adjustment layers, smart object editing, vector/text layers, non-destructive filters.
- GPU acceleration that matches CPU golden tests.
- Classic Photoshop filter/file-format compatibility matrix.
- Python automation, action recording, batch processing.
- Fuzzing, crash recovery, autosave, and performance telemetry.

## Phase 3: Broad Parity

- High-fidelity PSD/PSB edge cases, CMYK/Lab workflows, advanced blend modes.
- Liquify/warp, content-aware tools through optional plug-ins, HDR/EXR workflows.
- UXP/JSX migration layer for common automation and panel workflows.
