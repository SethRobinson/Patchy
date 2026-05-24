# Photoslop Architecture

Photoslop is organized around a small set of boundaries that should stay stable as features grow.

## Modules

- `core`: document, layer, pixel-buffer, metadata, color-state, and PSD preservation primitives.
- `render`: deterministic CPU compositor and tiled render cache. GPU paths must match CPU golden output.
- `psd`: owned PSD/PSB parsing and writing. Unknown blocks are modeled for future byte-preserving round trips.
- `filters`: built-in and plug-in filter registration.
- `plugins`: Photoslop C ABI plus compatibility adapters for foreign plug-in systems.
- `formats`: file-format registry that routes reads and writes through owned or third-party handlers.
- `color`: ICC/OpenColorIO integration boundary.
- `ui`: Qt 6 Widgets desktop shell.

## Correctness Rules

- The CPU compositor is the reference implementation.
- PSD/PSB is the primary persistence target, but the first writer intentionally emits flat RGB8 PSD files until layered writing is implemented.
- Unknown PSD resources and layer blocks must be carried in model types before layered round-trip writing is considered complete.
- Plug-ins must never run in-process by default once binary execution support is added.

## Dependency Rules

- Commercial-friendly dependencies only in shipping binaries.
- GPL tools may be used for local development or test generation only when they are not linked or distributed.
- Optional integrations should be behind CMake options and clear module boundaries.
