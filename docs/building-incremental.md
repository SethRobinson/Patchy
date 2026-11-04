# Compile time and incremental rebuilds

Split from [building.md](building.md): where opt-dev compile time goes and what the build does about it, one-target remote iteration with `remote-build.ps1`, and the rules that keep rebuilds incremental (configure-time generated files, remote warning counts, PCH, the build date, cached translation gates, `patchy_build_graph_tests`).

## Compile-time profile

Measured from `build\opt-dev\.ninja_log` on October 25, 2026 (last entry per output, end
minus start; a full opt-dev build is 1,508 outputs and about 143 CPU-minutes). Compare
files with it, not absolute times: 20 jobs ran in parallel.

| Group | TUs | CPU s | per TU |
|---|---|---|---|
| tests/ui | 129 | 2550 | 19.8 |
| src/ui | 259 | 1973 | 7.6 |
| tests/core (no PCH before this date) | 75 | 1685 | 22.5 |
| vendored libjxl | 182 | 404 | 2.2 |
| src/formats (own) | 60 | 382 | 6.4 |
| src/psd | 29 | 299 | 10.3 |
| src/render | 6 | 206 | 34.3 |

Where the time goes, and what the build does about it:

- Precompiled headers: `patchy_enable_ui_pch` (stable Qt and standard headers) covers
  `patchy_ui`, the UI test targets, the shared test objects and `patchy_perf_tests`;
  `patchy_enable_core_pch` (standard library only) covers `patchy_core_tests`. Project
  headers never go in a PCH: an edit would invalidate every TU.
- `patchy_ui_test_shared` is an OBJECT library of the test TUs both `patchy_translation_checks`
  and `patchy_ui_visual_tests` need (`ui_test_support.cpp` alone is about 50 s), so they
  compile once. `tests/test_harness.cpp` holds the one `<windows.h>` the harness needs.
- `tests/ui/ui_test_access.cpp` holds the `MainWindowTestAccess` bodies: the header is
  included by 103 UI test TUs, so an inline accessor edit used to recompile all of them.
- AUTOUIC is off (no `.ui` files) and AUTOMOC is off for the Qt-free core suite. The two
  largest Q_OBJECT classes include their own `moc_<name>.cpp` at the end of their source
  file, so a `main_window.hpp` or `canvas_widget.hpp` edit no longer rebuilds the shared
  `mocs_compilation.cpp` (36 s opt-dev, 64 s release) as well.
- `render/layer_compositor.hpp`'s templates are instantiated once per Target
  (`PATCHY_COMPOSITOR_TARGET_INSTANTIATIONS`; "Compositor targets are instantiated once each"
  in [code-organization-modules.md](code-organization-modules.md#other-deliberate-splits)); a TU-local Target
  class still pays the full instantiation.
- `ui/appearance_properties.cpp` registers fields through type-erased locators, never a
  generic lambda per field, which made it a 100 s unit
  ([batch-appearance.md](batch-appearance.md#implementation-boundaries)).
- Incremental cost: `main_window.hpp` and `canvas_widget.hpp` each reach about 200 TUs and
  change in a fifth and a tenth of all commits. The growth rule in
  [code-organization.md](code-organization.md#growth-rule-for-mainwindow-and-canvaswidget)
  keeps new state out of them and holds the current counts.

## Remote iteration: one target, opt-dev

`remote-build.ps1` takes the same two levers on every target but `wasm` ("Remote build machinery"
in [building.md](building.md#remote-build-machinery)). `-BuildTarget a,b` builds only those CMake targets, and the remote
half then runs only the suites it built (`patchy_core_tests`, `patchy_ui_visual_tests`, and on
macOS `patchy_mac_a11y_tests`), never a stale binary left from an earlier run. `-Preset`
replaces the default preset (`release`, `windows-arm64-release`, `mac-release`, `linux-flatpak-qt`).
`-CoreTestFilter` works on the same targets; `-Target wasm` refuses filters, `-BuildTarget` and
`-Preset` (it always builds the web release and runs the full wasm core suite). A worker's loop
on a Windows host:

```powershell
scripts\remote\remote-build.ps1 -Target windows -WindowsHost <host> -Preset opt-dev `
  -BuildTarget patchy_ui_visual_tests -TestFilter ui_palette
```

On the 16-thread offload host a one-line `patchy_ui` change costs about 20 minutes end to
end as a full `release` build and about 80 s as `-Preset opt-dev -BuildTarget
patchy_ui_visual_tests` (the first opt-dev build in a host's work tree compiles the whole
suite, about 8 minutes). The default preset stays `release`, so pass `-Preset opt-dev` for
development runs; LTCG-only warnings and failures are a release-day concern.

Every remote run reconfigures (`cmake --preset`), so a file the configure step generates
must only be rewritten when its content changes: `file(WRITE)` always touches it, and the
vendored aom, dav1d and OpenJPEG config headers once made each remote run recompile about
220 C sources. Configure-time generated sources go through `patchy_write_if_different`
(cmake/patchy_incremental.cmake) or `configure_file`, never a bare `file(WRITE)`.

Both remote halves log configure and build output to `build/<preset>/remote-build.log` and
print the number of warning lines the build printed. Ninja prints only what it compiled or
linked in that run, so the count covers exactly the targets built and the sources that
changed; it is a quick signal, not the zero-warning verification, which still needs a clean
build directory and the default target ([platform.md](platform.md), "Warning verification").

`PATCHY_USE_PCH` defaults on for native `patchy_ui`, the translation checker and UI
tests. Their private precompiled headers contain stable Qt/STL headers only, never
Patchy headers. Sources with their own flags, defines or include directories skip
the PCH. WebAssembly and Objective-C++ do not use it. When changing includes, verify
the affected translation units without PCH as well so cached includes cannot hide
missing direct dependencies. A build folder last built before a Visual Studio update
fails with C1853 (the `.pch` is "from a different version of the compiler"): ninja never
rebuilds a PCH for a new compiler, so build the `clean` target once, then build.

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
