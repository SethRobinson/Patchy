# Qt version and the patched macOS cocoa plugin

Split from [building.md](building.md): the Qt version the desktop presets pin, the decision and the facts for the 6.12 bump, and the patched Qt cocoa platform plugin the macOS app ships (QTBUG-134784, GitHub issue 95).

## Qt version status

The desktop presets pin Qt 6.8.3, the last open-source 6.8 release (6.8.4 and later
are commercial-only). The wasm presets use 6.10.3 and the Flatpak builds against the
KDE 6.11 runtime, so the code already compiles on 6.10 and 6.11. Decision (October 9,
2026): stay on 6.8.3 and go straight to 6.12 once the separate Qt PDF 6.140.x package is
downloadable for Windows, mac and Linux; skip 6.11. Facts for that bump:

- Qt 6.11 (patches end around March 2027) needs macOS 13+; Qt 6.12 LTS (open-source
  patches only until 6.13, about April 2027) needs macOS 14.4+ and Xcode 16, and is the
  last Qt that supports Windows 10. The README, `CMAKE_OSX_DEPLOYMENT_TARGET`,
  `Info.plist.in`, `scripts/setup-openmp-mac.sh` and `scripts/dev/gegl/build-gegl-macos.sh`
  pin macOS 12, so a bump raises all five together (CMake looks for
  `libomp-<version>-macos<target>`: a mismatch leaves G'MIC single-threaded and `make-dmg.sh`
  refuses the build).
- Qt 6.12 no longer ships `qtpdf`: Qt PDF and WebEngine became a separately versioned
  product (first release 6.140.0, Chromium 140). Without it the desktop build falls back to
  `pdf_import_stub.cpp` and loses PDF import, so confirm `qtpdf@6.140.x` installs for the
  target kit before bumping to 6.12.
- Released aqtinstall (3.3.0) cannot install any Windows desktop kit from 6.11 on (new
  per-toolchain repository layout; fix only in aqtinstall master, PR 1000, and PR 1048 for
  6.12's extensions). macOS and Linux kits are unaffected.
- A new Qt bundles newer FreeType/HarfBuzz, so expect offscreen text pins (text re-edit
  rasters, area-text render digests) to need re-verification, not blind re-pinning.
- The bump retires the patched cocoa plugin below (6.9.1+ carries the upstream fix).
  Check QTBUG-149612 first: it was an open crash in that fixed code on 6.11 (October 2026).

## Patched Qt cocoa plugin (macOS)

The macOS app ships a `libqcocoa.dylib` built from qtbase 6.8.3 with
`packaging/macos/qt-patches/*.patch`, in place of the aqt kit's own. Stock 6.8.3 has
QTBUG-134784: a table cell element that an accessibility client (VoiceOver, Accessibility
Inspector, Computer Use agents) has read is cached without a reference, and the next model
reset frees it while its row still holds it. Every Layers panel refresh is a
`QListWidget::clear()`, so the next layer edit crashed (GitHub issue 95). The upstream fix
is in 6.8.4 (commercial-only) and also changes QtGui; the patch fixes the reference count in
the plugin alone, so the kit's frameworks stay as shipped.

- `bash scripts/setup-qt-cocoa-patch-mac.sh` builds it into
  `.deps/qt-cocoa-6.8.3-patched/platforms/` (about 90 s; reruns are a no-op until a patch
  changes). It needs a macOS 15 SDK, which the Command Line Tools carry: 6.8.3 links AGL,
  which the macOS 26 SDK dropped. The script refuses a build whose QtCore/QtGui feature
  headers, rpaths, or linked Qt libraries differ from the kit's, or that imports a symbol
  the kit's frameworks do not export. `setup-mac.sh` and the remote `build-and-test.sh` run it.
- `make-dmg.sh` refuses to package without it and swaps it in after `macdeployqt`, before
  codesign. Local dev runs from `build/mac-release` still load the kit's plugin.
- `patchy_mac_a11y_tests <plugin folder>` (tests/macos) drives the cocoa accessibility
  elements of a `QListWidget` in-process, so it needs no Accessibility permission, only
  the logged-in console session. `--stock` runs the kit's plugin, which must fail.
