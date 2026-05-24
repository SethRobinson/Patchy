# Third-Party Notice Inventory

Photoslop's commercial-friendly core should avoid GPL-only dependencies. Before release, every dependency must have license text copied into installer/package notices.

Planned dependencies:

- Qt 6 Widgets: commercial license or LGPL dynamic-linking compliance.
- Skia: BSD-style license.
- Halide: MIT/Apache/BSD-style notices depending on bundled components.
- OpenImageIO: Apache-2.0.
- OpenColorIO: BSD-style license.
- LittleCMS 2: MIT.
- libjpeg-turbo, libpng, libtiff, OpenEXR, libwebp, libjxl, libheif, LibRaw: review exact port licenses before enabling release builds.

GPL-only libraries are not allowed in the shipping core.
