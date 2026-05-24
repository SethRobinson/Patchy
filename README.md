# Photoslop

Photoslop is a native C++20 image editor foundation aimed at PSD/PSB-first editing, cross-platform desktop binaries, and a commercial-friendly plug-in architecture.

This repository is the first buildable slice of the roadmap: document/layer primitives, a tiled render path, a minimal PSD/PSB reader/writer foundation, plug-in host abstractions, filter registration, optional Qt 6 shell, packaging notes, and tests.

## Build

The dependency-light core and tests can build without Qt:

```sh
cmake --preset dev -DPHOTOSLOP_BUILD_APP=OFF
cmake --build --preset dev
ctest --preset dev
```

The native application target is built when Qt 6 Widgets is available:

```sh
cmake --preset qt-local
cmake --build --preset qt-local
```

On this workspace, Qt 6.8.3 is installed locally at `.deps/Qt/6.8.3/msvc2022_64`.
After building, the Windows app is at `build/app/photoslop.exe`.

Run the automated test suite:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run-tests.ps1
```

## Current Status

Photoslop is not yet Photoshop-compatible across the full PSD surface. It currently supports a native Qt editing shell, pixel tools, layers, flat PSD export, and layered PSD round trips for common 8-bit RGB/RGBA pixel-layer documents.
