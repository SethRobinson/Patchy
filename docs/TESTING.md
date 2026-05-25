# Testing

Photoslop uses CTest for automated core coverage.

Current coverage includes:

- Pixel buffer shape and row access.
- Document layer add/remove behavior.
- CPU compositing.
- Flat PSD read/write round trips.
- Layered PSD read/write round trips for 8-bit RGB/RGBA pixel layers.
- Pixel editing tools: brush, eraser, line, rectangle, ellipse, fill bucket, selection fill/clear, and layer flips.
- Filter, format, plug-in, tile-cache, and color-manager basics.

Run locally on Windows:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run-tests.ps1
```

The GitHub Actions workflow builds the dependency-light core on Windows, macOS, and Linux. The Qt app target is exercised locally when `.deps/Qt/6.8.3/msvc2022_64` is present.

After any code change that affects app behavior, rebuild the main Qt executable before handing off for manual testing:

```powershell
cmake --build build/app --target photoslop --config Debug
```

Tool tests write visual BMP artifacts under `build/app/test-artifacts/`. These are intentionally simple 24-bit BMP files so the tests do not depend on Qt image codecs or external PNG libraries.

PSD support must grow through compatibility fixtures. Every newly supported PSD feature should get a round-trip test before UI work depends on it.
