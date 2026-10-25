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

Run the standard local test script (configure, build and ctest of `build/app`; the build
and ctest go through `scripts\run-throttled.bat` and its build lock, `-Jobs 20` by default):

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

### Qt version status

The desktop presets pin Qt 6.8.3, the last open-source 6.8 release (6.8.4 and later
are commercial-only). The wasm presets use 6.10.3 and the Flatpak builds against the
KDE 6.11 runtime, so the code already compiles on 6.10 and 6.11. Decision (October 9,
2026): stay on 6.8.3 and go straight to 6.12 once the separate Qt PDF 6.140.x package is
downloadable for Windows, mac and Linux; skip 6.11. Facts for that bump:

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
- The refusal is a lock, not a look: `build-busy-check.ps1` takes the named mutex `Global\PatchyLocalBuild` without waiting and holds it for the whole build, running the build itself (it calls the batch file again with `PATCHY_BUILD_LOCK_HELD=1`; the argument line travels in `PATCHY_THROTTLED_ARGS`). Of two builds started in the same instant exactly one runs; a killed holder leaves an abandoned mutex the next build takes. With the mutex held it still refuses when a ninja, MSBuild or `cmake --build` started outside the lock (an IDE, an older checkout's wrapper) is running. A mutex, not a lock file: cl.exe's lingering mspdbsrv.exe can inherit a file handle and keep the lock after the build. Tests and other commands without `--build` take no lock.
- Git Bash's nested quoting can drop the inner command and still exit 0, which is why the build runs from PowerShell or real cmd and why only compile/link lines or `ninja: no work to do` prove it built.
- Call `scripts\vs-env.bat`, never VsDevCmd.bat directly ([release-process.md](release-process.md)).
- The version comes from the generated `patchy_version.hpp` (`cmake/patchy_version.hpp.in`), included only where used. Never make it a target-wide define: that recompiles all of `patchy_ui` on every version change.
- A running `patchy.exe` or `patchy-mcp.exe` in any build folder does not block the link: a PRE_LINK step renames it aside as `<name>.stale-<stamp>.exe`. Never kill either process (Seth may have unsaved work) and never package a `.stale-` file.
- Batch files compare exit codes against 0, never `if errorlevel 1`: a failed link exits negative.
- A git worktree has no `.deps`: configure its preset once with `--preset opt-dev -DCMAKE_PREFIX_PATH=<main-checkout>/.deps/Qt/6.8.3/msvc2022_64` (`--preset release` the same way for a release build); the build command is unchanged.

## MSVC Release codegen

CMakeLists.txt owns the MSVC Release codegen flags (`/Zi /GL` on compiles, `/DEBUG:FULL /INCREMENTAL:NO /LTCG` on links), so every configure emits `patchy.pdb` (for symbolizing WER dumps from `%LOCALAPPDATA%\CrashDumps`) and link-time optimized binaries. Never hand-edit `build\release\CMakeCache.txt`. To symbolize a dump from an older build, rebuild that commit in a temporary worktree; full links reproduce the binary layout.

Executables that never ship link with `/LTCG:INCREMENTAL`: the translation checker (`patchy` and `patchy-mcp` link only after it has linked and passed, [localization.md](localization.md)) and the test programs `patchy_core_tests`, `patchy_ui_visual_tests`, `patchy_perf_tests` and `patchy_curves_clipping_preview_tests`. Their objects stay `/GL`, so the tests still run link-time optimized code. The linker keeps its codegen state in `<target>.iobj`/`.ipdb` (0.4 to 0.9 GB each beside the exe) and regenerates only changed functions; a missing `.iobj` or changed link flags mean one full link. On the dev box, `--target patchy` after a one-line `patchy_ui` edit dropped from about 224 s to 166 s (checker link 103 s to 42 s). Shipped binaries (`patchy`, `patchy-mcp`, the plug-in hosts) keep full `/LTCG`. `/DEBUG:FASTLINK` on the checker saved only about 4 s and was not kept.

## Incremental development builds

Development builds use the `opt-dev` preset: `RelWithDebInfo` in `build/opt-dev`,
optimized MSVC code and debug symbols without Release's `/GL` and `/LTCG`. It is the
edit/build cycle and the handoff build (AGENTS.md), and the desk merge queue builds it
too; the `release` preset is built only for releases (packaging, both full suites), where
LTCG-only warnings and failures surface (Seth, October 23, 2026). Use the same Visual
Studio environment and throttling wrapper as AGENTS.md's handoff command, and build only
the targets you will run while iterating: `--target patchy_ui_visual_tests`,
`--target patchy_core_tests`, or `--target patchy` (several `--target` flags combine).
`opt-dev-no-pch` uses its own directory and sets `PATCHY_USE_PCH=OFF` for include
hygiene checks and timing comparisons.

Why not `release` for development: its links, not the compiles, dominate. With full LTCG a
header change recompiles about 130 files in 90 s and then links the five executables in
parallel for about 12 minutes (`patchy_ui_visual_tests.exe` alone about 10 minutes, and a
`patchy.exe` LTCG link can take many minutes and about 10 GB); incremental LTCG on the test
programs (above) roughly halves their relinks but a one-line `patchy_ui` edit still relinks
the UI suite in about 8 minutes on the offload host.

### Compile-time profile

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
- `render/layer_compositor.hpp` (about 4,600 lines of templates over a `Target` type) is
  instantiated in full by each of its 13 includers: `ui/image_document_io.cpp` 121 s,
  `render/compositor.cpp` 68 s, `render/native_duotone.cpp` 57 s for 147 lines. The fix
  (`extern template`, merged Target classes) is in [refactor-backlog.md](refactor-backlog.md).
- `ui/appearance_properties.cpp` takes about 100 s for 440 lines of generic-lambda field
  registration; also in the backlog.
- Incremental cost: `main_window.hpp` reaches 161 TUs and `canvas_widget.hpp` 173, and they
  change in a fifth and a tenth of all commits. The growth rule in
  [code-organization.md](code-organization.md) keeps new state out of them.

### Remote iteration: one target, opt-dev

`remote-build.ps1` takes the same two levers on every target ("Remote build machinery"
below). `-BuildTarget a,b` builds only those CMake targets, and the remote
half then runs only the suites it built (`patchy_core_tests`, `patchy_ui_visual_tests`, and on
macOS `patchy_mac_a11y_tests`), never a stale binary left from an earlier run. `-Preset`
replaces the default preset (`release`, `mac-release`, `linux-flatpak-qt`). `-CoreTestFilter`
works on all three targets. A worker's loop on a Windows host:

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

## Remote build machinery

Build machines are developer-specific and never named in the repository. Their ssh targets live in `scripts/remote/hosts.local.json` (gitignored; copy `hosts.example.json`), read through `scripts/remote/remote-hosts.ps1`; hardware notes and quirks go in `agents_local.md`. Both live in the main checkout (worktree runs fall back to it). Below, `<mac-build-host>`, `<linux-build-host>` and `<windows-build-host>` are those targets.

macOS (arm64, preset `mac-release`, Qt at `.deps/Qt/6.8.3/macos`) and Linux (preset `linux-flatpak-qt` inside the Flatpak SDK, see [linux-testing.md](linux-testing.md); `-NativeQt` for `linux-release` on `.deps/Qt/6.8.3/gcc_64`) build remotely via `scripts\remote\remote-build.ps1 -Target mac|linux`, which snapshots the working tree (uncommitted changes included; no commits, branches or index changes) to a bare repo (`~/patchy.git`) on the build host, builds there, and runs the suites with output streamed back (`-BuildTarget`, `-Preset` and `-CoreTestFilter` work on every target: "Remote iteration" above). One-time provisioning: `scripts/remote/setup-mac.sh` / `setup-linux.sh` (idempotent: venv tools + Qt via aqtinstall + apt deps).

`remote-build.ps1` checks out over the one shared `~/patchy/src`, so it takes the host's lock and then probes it (pgrep for compilers, linkers, ninja, cmake and the suites); a busy host stops the run (exit 75). Neither wait on nor kill that run: for a build beside it, push the snapshot to a private ref (`refs/snapshots/<topic>`), clone `~/patchy.git` into `~/patchy-<topic>/src`, symlink `.deps` to `~/patchy/src/.deps`, configure and build the preset there with the environment lines from `build-and-test.sh`, then delete the clone and the ref.

### Windows offload build (with Seth's permission)

`remote-build.ps1 -Target windows` builds on another Windows machine when Seth asks for it or has given a standing permission (recorded in `agents_local.md`): `-Preset opt-dev` for development, the default `release` preset only for release checks. The local `build\release` stays the release gate and the only packaging source.

- **Hosts.** The `windows` config entry may be an ordered list. The script probes each over ssh: unreachable is skipped, one running cl/link/ninja/cmake or a suite is busy, the first idle one builds. All reachable hosts busy: exit 75, never a build beside them (`-WaitMinutes N` waits). `-WindowsHost <ssh>` picks one (still probed). Runs hold a per-host lock (remote-lock.ps1; locked: exit 75). A leftover lock from this machine is taken over only when its run's local process is gone AND the host probes idle: a remote build outlives a killed ssh client. Any other lock needs `-BreakLock` after checking its holder is gone. Never kill another session's build there.
- **Layout.** Bare repo `~\patchy.git`; work tree = the entry's `workTree`. Qt is the dev box's `.deps\Qt\6.8.3\msvc2022_64` copied to the same relative path, so the unmodified `release` preset resolves it.
- **Toolchain.** VS 2026 Build Tools (MSVC x64, SDK 10.0.26100, VS-bundled CMake and Ninja), Git for Windows, Python 3.8+ (the WindowsApps `python.exe` is only a Store alias). `vs-env.bat` and `setup-windows.ps1` take VS 18+ only through vswhere (`-version 18.0`), so an older VS with VC tools on the box never wins. Bootstrapper: `https://aka.ms/vs/18/stable/vs_buildtools.exe` (the `release` URL redirects to Bing).
- **Provisioning.** First, on the host: `setup-windows-ssh.cmd` beside its `.ps1` and the dev box's `.pub` in a local folder (its header has the details). It forces 64-bit Windows PowerShell 5.1, self-elevates, installs sshd, sets the ssh default shell to Windows PowerShell 5.1 (Windows OpenSSH starts `cmd.exe` otherwise, breaking every remote command), authorizes the key, and logs PASS/FAIL. Then over ssh: `setup-windows.ps1 -Root <dir>` (idempotent; admin account) installs the toolchain, bare repo and work tree, and prints the Qt copy command.
- **Remote half.** `build-and-test.ps1` configures, builds throttled at `-j 8` (or the entry's `jobs`) under the host's own run-throttled build lock, counts the warning lines, reports `patchy.exe`, and runs the suites it built (`-TestFilter` UI, `-CoreTestFilter` core; scoped-filter rule as on the dev box).
- **Shell quirks.** Remote commands run in Windows PowerShell 5.1: no `&&`; send ad-hoc scripts as a file (local PowerShell expands `$_` and mangles embedded double quotes). `Get-Process a,b` exits 1 when any name is absent.
- **Smart App Control must be off.** Otherwise Code Integrity randomly blocks the fresh unsigned executables (events 3033/3077). It has no exclusions, and turning it back on means reinstalling Windows: the owner's decision. Check `(Get-MpComputerStatus).SmartAppControlState`.

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
