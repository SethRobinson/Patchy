# Photoshop Plug-in Test Fixtures

These `.8bf` files are downloaded test fixtures for the legacy Photoshop plug-in support (docs/plugins.md): the property-list reader parses them on every platform, and on Windows the test suites run them through the out-of-process host (`patchy-8bf-host32.exe` / `patchy-8bf-host64.exe`). They are test data only: never linked into Patchy and not shipped by the app target (developer builds copy them next to the binary, where the plug-in scan picks them up).

- `Greyscale.8bf` (32-bit) and `Greyscale64.8bf` (64-bit)
- `White to Transparent.8bf` (32-bit) and `White to Transparent64.8bf` (64-bit)

Source: <https://misc.daniel-marschall.de/photoshop_filters/>

The source page describes these as Photoshop filters created with Filter Foundry and provides 64-bit Photoshop downloads plus source files.
