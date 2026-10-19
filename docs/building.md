# Building Patchy

[Back to Patchy](../README.md)

## Build from source

Build the dependency-light core and tests without the Qt app:

```sh
cmake --preset dev -DPATCHY_BUILD_APP=OFF
cmake --build --preset dev
ctest --preset dev
```

Build the Qt desktop app:

```sh
cmake --preset qt-local
cmake --build --preset qt-local
```

The local Qt app preset writes `patchy.exe` under `build/app`.

Run the standard local test script:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run-tests.ps1
```

### Automatic selection dependencies

Select > Subject and the Object Selection tool need ONNX Runtime and the pinned models
([ai-selection.md](ai-selection.md)). Provision them once into the main checkout's `.deps`
(worktrees find it there):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\setup-ai-deps.ps1
```

On macOS and Linux run `bash scripts/setup-ai-deps.sh`. Without them the build still
succeeds and the feature is left out of the app.

### macOS and Linux

Install Qt 6.8.3 into `.deps/Qt` (for example `pip install aqtinstall && aqt install-qt
mac desktop 6.8.3 -m qtimageformats qtpdf -O .deps/Qt`, or `linux desktop 6.8.3
linux_gcc_64` on Linux), then build the matching preset. The `qtpdf` module is optional:
without it Patchy still exports PDF, it just cannot open one.

```sh
cmake --preset mac-release      # or linux-release
cmake --build --preset mac-release
```

### Qt version status (researched October 9, 2026)

The desktop presets pin Qt 6.8.3, the last open-source 6.8 release (6.8.4 and later
are commercial-only). The wasm presets use 6.10.3 and the Flatpak builds against the
KDE 6.11 runtime, so the code already compiles on 6.10 and 6.11. Facts for the next
desktop bump (decision, October 9, 2026: stay on 6.8.3 and go straight to 6.12 once the
separate Qt PDF 6.140.x package is downloadable for Windows, mac and Linux; skip 6.11):

- Qt 6.11 (patches end around March 2027) needs macOS 13+; Qt 6.12 LTS (open-source
  patches only until 6.13, about April 2027) needs macOS 14.4+ and Xcode 16, and is the
  last Qt that supports Windows 10. The README, `CMAKE_OSX_DEPLOYMENT_TARGET`, and
  `Info.plist.in` advertise macOS 12, so a bump must raise them together.
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

### Patched Qt cocoa plugin (macOS)

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

macOS produces `build/mac-release/Patchy.app`; Linux produces
`build/linux-release/patchy`. `packaging/macos/make-dmg.sh` and
`packaging/linux/make-flatpak.sh` and `packaging/linux/make-appimage.sh`
([appimage.md](appimage.md)) create the distributable artifacts. Both test suites
run offscreen on all three platforms (`QT_QPA_PLATFORM=offscreen`).

## Local Windows release build

AGENTS.md gives the handoff command and the short rules; these are the details behind them.

- `scripts\run-throttled.bat` runs the build (and tests, and batch files as-is) at below-normal priority and returns the program's real exit code. A bare `start "" /b /wait /belownormal` reports 0 once the program launched, so never use it. `-j 20` caps ninja on the dev box; remote builds use every core but four.
- It refuses a `cmake --build` (exit 3) while another build runs on the box. Wait, or offload with `scripts\remote\remote-build.ps1 -Target windows` (host order and the busy check are in `agents_local.md`). `PATCHY_ALLOW_CONCURRENT_BUILD=1` only when Seth says. Never edit the helper while a build runs: cmd rereads a batch file as it executes.
- Git Bash's nested quoting can drop the inner command and still exit 0, which is why the build runs from PowerShell or real cmd and why only compile/link lines or `ninja: no work to do` prove it built.
- Call `scripts\vs-env.bat`, never VsDevCmd.bat directly ([release-process.md](release-process.md)).
- The version comes from the generated `patchy_version.hpp` (`cmake/patchy_version.hpp.in`), included only where used. Never make it a target-wide define: that recompiles all of `patchy_ui` on every version change.
- A running `build\release\patchy.exe` or `patchy-mcp.exe` does not block the link: a PRE_LINK step renames it aside as `<name>.stale-<stamp>.exe`. Never kill either process (Seth may have unsaved work) and never package a `.stale-` file.
- Batch files compare exit codes against 0, never `if errorlevel 1`: a failed link exits negative.
- A git worktree has no `.deps`: configure its release preset once with `--preset release -DCMAKE_PREFIX_PATH=<main-checkout>/.deps/Qt/6.8.3/msvc2022_64`; the build command is unchanged.

## MSVC Release codegen

CMakeLists.txt owns the MSVC Release codegen flags (`/Zi /GL` on compiles, `/DEBUG:FULL /INCREMENTAL:NO /LTCG` on links), so every configure emits `patchy.pdb` (for symbolizing WER dumps from `%LOCALAPPDATA%\CrashDumps`) and link-time optimized binaries. Never hand-edit `build\release\CMakeCache.txt`. To symbolize a dump from an older build, rebuild that commit in a temporary worktree; full links reproduce the binary layout.

The translation checker is the one exception: it never ships, and `patchy` and `patchy-mcp` link only after it has linked and passed ([localization.md](localization.md)), so it links with `/LTCG:INCREMENTAL`. The linker keeps its codegen state in `patchy_translation_checks.iobj`/`.ipdb` (about 1 GB beside the exe) and regenerates only changed functions; a missing `.iobj` means one full link. Measured on the dev box (October 19, 2026), `--target patchy` after a one-line `patchy_ui` edit took 223.7 s before and 166.1 s after (checker link 103 s to 42 s). Shipped binaries keep full `/LTCG`. `/DEBUG:FASTLINK` on the checker saved only about 4 s and was not kept.

## Incremental development builds

Iterate on the `release` preset by building only the targets you will run:
`--target patchy_ui_visual_tests`, `--target patchy_core_tests`, or `--target patchy`
(several `--target` flags combine). A plain `--build --preset release` relinks five
LTCG executables, and the links, not the compiles, dominate: measured October 2026
on the dev box, a change touching `main_window.hpp` and `canvas_widget.hpp` compiled
about 130 files in 90 s, then linked `patchy_ui_visual_tests.exe` in 606 s,
`patchy_perf_tests.exe` 545 s, `patchy-mcp.exe` 433 s, `patchy_translation_checks.exe` (before its incremental LTCG)
199 s and `patchy.exe` 166 s, running in parallel for about 12 minutes. Run the full
release build once, at handoff (AGENTS.md; Seth, October 18, 2026).

`opt-dev` uses `RelWithDebInfo` in `build/opt-dev`: optimized MSVC code and debug
symbols without Release's `/GL` and `/LTCG`. It is for the edit/build cycle; the
`release` preset remains the required handoff and packaging build. Use the same
Visual Studio environment and throttling wrapper as the release build in AGENTS.md.
`opt-dev-no-pch` uses its own directory and sets `PATCHY_USE_PCH=OFF` for include
hygiene checks and timing comparisons.

`PATCHY_USE_PCH` defaults on for native `patchy_ui`, the translation checker and UI
tests. Their private precompiled headers contain stable Qt/STL headers only, never
Patchy headers. Sources with their own flags, defines or include directories skip
the PCH. WebAssembly and Objective-C++ do not use it. When changing includes, verify
the affected translation units without PCH as well so cached includes cannot hide
missing direct dependencies.

The build date changes when runtime sources, headers, resources, catalogs, build
definitions or compilation options change. An input-list manifest catches added
and removed files, including a removed last input. It covers external QRC assets,
platform packaging resources, bundled scripts/fonts/agent-kit, and configured AI
runtime/model sources. Toolchain/Qt paths and platform architecture/deployment settings
are part of the stable configuration identity. Test-only code stays outside the app
date inputs. Resource-copy targets also depend on their source-list manifests, so
removed bundled files are restaged. Ordinary unchanged builds keep
the existing binary and date. The rule does not inspect Git status or hash files
on every invocation. Packaging runs `patchy_refresh_build_stamp` before its normal
build; Flatpak supplies a new `PATCHY_BUILD_STAMP_REFRESH_TOKEN` in its generated
release manifest so its app-module cache cannot reuse an old stamp.

Translation gates cache only successful unchanged inputs; see [localization.md](localization.md).
`patchy_build_graph_tests` exercises these rules in compiler-free Ninja fixtures:
no-op/reconfigure, source/header changes, added/removed inputs, failed/retried
checks, missing outputs and explicit packaging refresh. Fixtures remain under
`build/test-output/build-graph` for diagnosis.

## Windows Release Package

Create local Windows release artifacts:

```bat
scripts\release\build-release.bat
```

The script configures and builds the `release` preset, signs `patchy.exe`, `patchy-mcp.exe`, the two legacy plug-in hosts, the installer helper executables, and the installer (signing is required; `PATCHY_ALLOW_UNSIGNED=1` allows a deliberately unsigned local build), deploys the minimum Qt runtime needed by the current app, copies third-party notices, and creates:

```text
build\package\PatchyWindowsNoInstaller.zip
build\package\PatchyWindowsInstaller.exe
```

The zip contains a top-level `Patchy` folder so it can be dragged anywhere and does not include installer-only helpers. The installer is a local per-user installer that installs to `%LOCALAPPDATA%\Programs\Patchy` by default (the user can choose another folder; see `packaging/windows/README.md`), creates a Start Menu shortcut, offers a desktop shortcut, and registers an uninstall entry.  `latest_version.json` is the update metadata file.
