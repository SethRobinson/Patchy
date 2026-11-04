# Building Patchy

[Back to Patchy](../README.md)

Related documents:

- [building-qt.md](building-qt.md): the Qt version the desktop presets pin, the decision and facts for the 6.12 bump, and the patched Qt cocoa plugin the macOS app ships.
- [building-incremental.md](building-incremental.md): the opt-dev compile-time profile and what the build does about it, one-target remote iteration, and the rules that keep rebuilds incremental (configure-time generated files, PCH, the build date, `patchy_build_graph_tests`).

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

The local Qt app preset writes `patchy.exe` under `build/app`. To run every suite in full
there (configure, build and ctest through `scripts\run-throttled.bat` and its build lock,
`-Jobs 20` by default; not the handoff):

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run-tests.ps1
```

### Automatic selection dependencies

The Object Selection tool and Select Object need ONNX Runtime and the pinned models
([ai-selection-models.md](ai-selection-models.md)); Select Subject is held back
(`PATCHY_ENABLE_SELECT_SUBJECT`, default off) and setup never downloads its model. Provision
them once into the main checkout's `.deps` (worktrees find it there):

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

macOS produces `build/mac-release/Patchy.app`; Linux produces
`build/linux-release/patchy`. `packaging/macos/make-dmg.sh` and
`packaging/linux/make-flatpak.sh` and `packaging/linux/make-appimage.sh`
([appimage.md](appimage.md)) create the distributable artifacts. Both test suites
run offscreen on all three platforms (`QT_QPA_PLATFORM=offscreen`).

Qt version status (the 6.8.3 pin and the facts for the 6.12 bump) and the Patched Qt cocoa plugin (macOS), which `make-dmg.sh` requires, are in [building-qt.md](building-qt.md).

## Local Windows release build

AGENTS.md gives the handoff command and the short rules; these are the details behind them.

- `scripts\run-throttled.bat` runs the build (and tests, and batch files as-is) at below-normal priority and returns the program's real exit code. A bare `start "" /b /wait /belownormal` reports 0 once the program launched, so never use it. `-j 20` caps ninja on the dev box; remote mac/Linux builds use every core but four.
- It refuses a `cmake --build` (exit 3) while another build runs on the box. Wait, or offload with `scripts\remote\remote-build.ps1 -Target windows -Preset opt-dev` if Seth allows ("Windows offload build"; host order in `agents_local.md`). `PATCHY_ALLOW_CONCURRENT_BUILD=1` only when Seth says. Never edit the helper while a build runs: cmd rereads a batch file as it executes.
- The refusal is a lock, not a look: `build-busy-check.ps1` takes the named mutex `Global\PatchyLocalBuild` without waiting and holds it for the whole build, running the build itself (it calls the batch file again with `PATCHY_BUILD_LOCK_HELD=1`; the argument line travels in `PATCHY_THROTTLED_ARGS`). Of two builds started in the same instant exactly one runs; a killed holder leaves an abandoned mutex the next build takes. With the mutex held it still refuses when a ninja, MSBuild or `cmake --build` started outside the lock (an IDE, an older checkout's wrapper) is running. A mutex, not a lock file: cl.exe's lingering mspdbsrv.exe can inherit a file handle and keep the lock after the build. Tests and other commands without `--build` take no lock.
- Git Bash's nested quoting can drop the inner command and still exit 0, which is why the build runs from PowerShell or real cmd and why only compile/link lines or `ninja: no work to do` prove it built.
- Call `scripts\vs-env.bat`, never VsDevCmd.bat directly ([release-process-scripts.md](release-process-scripts.md)).
- The version comes from the generated `patchy_version.hpp` (`cmake/patchy_version.hpp.in`), included only where used. Never make it a target-wide define: that recompiles all of `patchy_ui` on every version change.
- A running `patchy.exe` or `patchy-mcp.exe` in any build folder does not block the link: a PRE_LINK step renames it aside as `<name>.stale-<stamp>.exe`. Never kill either process (Seth may have unsaved work) and never package a `.stale-` file.
- Batch files compare exit codes against 0, never `if errorlevel 1`: a failed link exits negative.
- A git worktree has no `.deps`: configure its preset once with `--preset opt-dev -DCMAKE_PREFIX_PATH=<main-checkout>/.deps/Qt/6.8.3/msvc2022_64` (`--preset release` the same way for a release build); the build command is unchanged. The ARM64 cross presets also name a toolchain file under `.deps`, so give such a worktree a `.deps` junction to the main checkout's instead (remove it with plain `rmdir` before `git worktree remove`).

### Windows ARM64 cross builds

`windows-arm64-cross-release` (packaging) and `windows-arm64-cross-opt-dev` build ARM64 Patchy on the x64 dev box, in `build\<preset>`. Enter `scripts\vs-env.bat -arch=arm64 -host_arch=x64` before configuring and building. One-time setup:

- The VS component `Microsoft.VisualStudio.Component.VC.Tools.ARM64` (the x64-hosted ARM64 compiler, `bin\Hostx64\arm64`). A passive `setup.exe modify --add` must run elevated.
- The Qt kit: `aqt install-qt windows desktop 6.8.3 win64_msvc2022_arm64_cross_compiled -m qtimageformats -O <absolute path to .deps\Qt>` (Qt 6.8.3 has no qtpdf for ARM64: PDF import is the stub). With a relative `-O`, aqt writes the host path into `bin\qtpaths.bat` and `qmake.bat` relative to the current folder; those lines must read `"%~dp0..\..\msvc2022_64\bin\qtpaths6.exe"`. The kit holds target files only: the presets pass `QT_HOST_PATH` (the x64 kit, for moc, lrelease and windeployqt) and the kit's `qt.toolchain.cmake`, which sets `CMAKE_SYSTEM_PROCESSOR=arm64` (`cmake/patchy_ai.cmake` picks `win-arm64` ONNX Runtime from it).
- `scripts\setup-ai-deps.ps1 -Platform win-arm64`.

A cross build cannot run what it builds: it skips the translation runtime check that gates `patchy` and `patchy-mcp` (the x64 build of the same tree runs it). The suites run on the ARM64 host from a native `windows-arm64-opt-dev` build ("Windows ARM64" below). The two plug-in hosts link the static C++ runtime (`src/plugins/host/CMakeLists.txt`), because the app-local `MSVCP140.dll` beside them is ARM64.

## MSVC Release codegen

CMakeLists.txt owns the MSVC Release codegen flags (`/Zi /GL` on compiles, `/DEBUG:FULL /INCREMENTAL:NO /LTCG` on links), so every configure emits `patchy.pdb` (for symbolizing WER dumps from `%LOCALAPPDATA%\CrashDumps`) and link-time optimized binaries. Never hand-edit `build\release\CMakeCache.txt`. To symbolize a dump from an older build, rebuild that commit in a temporary worktree; full links reproduce the binary layout.

Executables that never ship link with `/LTCG:INCREMENTAL`: the translation checker (`patchy` and `patchy-mcp` link only after it has linked and passed, [localization.md](localization.md)) and the test programs `patchy_core_tests`, `patchy_ui_visual_tests`, `patchy_perf_tests` and `patchy_curves_clipping_preview_tests`. Their objects stay `/GL`, so the tests still run link-time optimized code. The linker keeps its codegen state in `<target>.iobj`/`.ipdb` (0.4 to 0.9 GB each beside the exe) and regenerates only changed functions; a missing `.iobj` or changed link flags mean one full link. Shipped binaries (`patchy`, `patchy-mcp`, `patchy-gegl-host`, the plug-in hosts) keep full `/LTCG`. `/DEBUG:FASTLINK` is not used: on the checker it saved only about 4 s.

Every Patchy target compiles with `/utf-8` (`patchy_configure_target`; Qt adds it only to targets that link Qt). Without it a machine whose system code page is not 1252 (932 on a Japanese Windows) reads the sources in that code page: C4819 on every non-ASCII header and mangled literals.

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

The compile-time profile, one-target remote iteration and the rules that keep rebuilds incremental (configure-time generated files, PCH, the build date, cached translation gates) are in [building-incremental.md](building-incremental.md).

## Remote build machinery

Build machines are developer-specific and never named in the repository. Their ssh targets live in `scripts/remote/hosts.local.json` (gitignored; copy `hosts.example.json`), read through `scripts/remote/remote-hosts.ps1`; hardware notes and quirks go in `agents_local.md`. Both live in the main checkout (worktree runs fall back to it). Docs and the remote setup scripts write `<mac-build-host>`, `<linux-build-host>` and `<windows-build-host>` for those targets.

macOS (arm64, preset `mac-release`, Qt at `.deps/Qt/6.8.3/macos`) and Linux (preset `linux-flatpak-qt` inside the Flatpak SDK, see [linux-testing.md](linux-testing.md); `-NativeQt` for `linux-release` on `.deps/Qt/6.8.3/gcc_64`) build remotely via `scripts\remote\remote-build.ps1 -Target mac|linux`, which snapshots the working tree (uncommitted changes included; no commits, branches or index changes) to a bare repo (`~/patchy.git`) on the build host, builds there, and runs the suites with output streamed back (`-BuildTarget`, `-Preset` and `-CoreTestFilter` work on every target but `wasm`: "Remote iteration" in [building-incremental.md](building-incremental.md#remote-iteration-one-target-opt-dev)). One-time provisioning: `scripts/remote/setup-mac.sh` / `setup-linux.sh` (idempotent: venv tools + Qt via aqtinstall + apt deps).

`remote-build.ps1` checks out over the one shared `~/patchy/src`, so it takes the host's lock and then probes it (pgrep for compilers, linkers, ninja, cmake and the suites); a busy host stops the run (exit 75). Neither wait on nor kill that run: for a build beside it, push the snapshot to a private ref (`refs/snapshots/<topic>`), clone `~/patchy.git` into `~/patchy-<topic>/src`, symlink `.deps` to `~/patchy/src/.deps`, configure and build the preset there with the environment lines from `build-and-test.sh`, then delete the clone and the ref.

### Windows offload build (with Seth's permission)

`remote-build.ps1 -Target windows` builds on another Windows machine when Seth asks for it or has given a standing permission (recorded in `agents_local.md`): `-Preset opt-dev` for development, the default `release` preset only for release checks. The local `build\release` stays the release gate and the only packaging source.

- **Hosts.** The `windows` config entry may be an ordered list. The script probes each over ssh: unreachable is skipped, one running cl/link/ninja/cmake or a suite is busy, the first idle one builds. All reachable hosts busy: exit 75, never a build beside them (`-WaitMinutes N` waits). `-WindowsHost <ssh>` picks one (still probed). Runs hold a per-host lock (remote-lock.ps1; locked: exit 75). A leftover lock from this machine is taken over only when its run's local process is gone AND the host probes idle: a remote build outlives a killed ssh client. Any other lock needs `-BreakLock` after checking its holder is gone. Never kill another session's build there.
- **Layout.** Bare repo `~\patchy.git`; work tree = the entry's `workTree`. Qt is the dev box's `.deps\Qt\6.8.3\msvc2022_64` copied to the same relative path, so the unmodified `release` preset resolves it. On an ARM64 host `setup-windows.ps1` instead adds the native ARM64 MSVC tools and installs `.deps\Qt\6.8.3\msvc2022_arm64` through aqtinstall (Qt 6.8.3 has no qtpdf add-on for ARM64, so PDF import builds as the stub).
- **Windows ARM64.** `-Target windows-arm64` takes the config's separate `windows-arm64` host list (so `-Target windows` never auto-picks an ARM64 box) and builds natively there with `windows-arm64-release` (default) or `windows-arm64-opt-dev`; everything else, the `-Target windows` switches included, works as for `windows`. `build-and-test.ps1` enters `vs-env.bat -arch=arm64 -host_arch=arm64` for those presets. Patchy, `patchy-mcp` and the suites are ARM64; the two plug-in hosts stay x64 and x86 ([plugin-host.md](plugin-host.md)). ARM64 executables do not run on an x64 machine, so the suites need the ARM64 host.
- **Web release.** `-Target wasm` takes the config's `wasm` host list (Windows hosts that also give a `share`, the UNC path of the work tree) and runs `wasm-build-and-test.ps1`: the release site through `build-wasm.bat`, then the full wasm core suite. `release-wasm.ps1` wraps it for the release drivers ([release-wasm.md](release-wasm.md#remote-web-build)).
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

With no argument the script packages x64, then Windows ARM64, then writes `build\package\PatchyGeglSources.zip` (the bundled GEGL's source, [gegl-filters.md](gegl-filters.md#sources)); `build-release.bat x64` or `arm64` packages one ([release-process.md](release-process.md#windows-arm64-packages)). For x64 it configures and builds the `release` preset, signs `patchy.exe`, `patchy-mcp.exe`, the two legacy plug-in hosts, `patchy-gegl-host.exe`, the installer helper executables, and the installer (signing is required; `PATCHY_ALLOW_UNSIGNED=1` allows a deliberately unsigned local build), deploys the minimum Qt runtime needed by the current app, stages Patchy's own GEGL in `gegl\` ([gegl-filters.md](gegl-filters.md#bundled-gegl)), copies third-party notices, and creates:

```text
build\package\PatchyWindowsNoInstaller.zip
build\package\PatchyWindowsInstaller.exe
```

The zip contains a top-level `Patchy` folder so it can be dragged anywhere and does not include installer-only helpers. The installer is a local per-user installer that installs to `%LOCALAPPDATA%\Programs\Patchy` by default (the user can choose another folder; see `packaging/windows/README.md`), creates a Start Menu shortcut, offers a desktop shortcut, and registers an uninstall entry.  `latest_version.json` is the update metadata file.
