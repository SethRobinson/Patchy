# WebAssembly memory

Deep reference for wasm memory: how the shared memory is constructed, what
the in-app numbers mean, the telemetry publisher, the in-app budgets, and
the resolved Safari 26 tab-kill record. Build, toolchain, and threading
rules live in [wasm.md](wasm.md); the measurement harness and the mac-host
workflow are the last two sections of this file.

## Construction: the shell page owns the memory

The shell page (packaging/web/patchy.html.in; the stress harness replicates
it) constructs the shared `WebAssembly.Memory` and passes it to qtLoad as
`wasmMemory` (`buildWasmMemory`). `QT_WASM_INITIAL_MEMORY` (256 MB,
CMakeLists.txt) is the FLOOR baked into the memory import: a smaller
page-supplied initial is a LinkError, so the page's `BAKED_MIN_MB` must stay
in sync (Qt's dev-loop patchy.html just uses the floor). The page picks
initial 512 MB desktop / 256 MB iOS and walks a maximum ladder
(4096/2048/1024 MB; iOS 1536/1024/768), catching the RangeError WebKit
throws when it cannot reserve a shared maximum up front; iOS starts low
because an oversized reservation can also succeed and get the tab killed
later, uncatchably. `-sMAXIMUM_MEMORY=4GB` stays as the declared import
ceiling. The `PATCHY_WASM_INITIAL_MB` / `PATCHY_WASM_MAX_MB` /
`PATCHY_WASM_POOL` URL knobs override the ladder and pool per load,
consumed by the page before the Module exists.

## Reading the numbers (About row and patchyMemStats)

The chosen cap is published as `globalThis.patchyWasmMemoryMaximumBytes`,
read by `ui/memory_info.hpp` for the About screen's live memory row
(`emscripten_get_heap_max()` is baked at link time; never trust it for
this). The row shows three numbers: used (the selected allocator's live claim
from Emscripten's `mallinfo().uordblks`; dlmalloc reports its own allocations,
and the optional mimalloc benchmark build reports its underlying emmalloc
claim), heap
(`emscripten_get_heap_size()`, the linear-memory buffer browser tab
accounting sees, which only ratchets), and the cap.

The shipped allocator is dlmalloc. Mimalloc's modest warm-stress throughput
win is outweighed by retained-segment growth on a real 350 MB, 415-layer PSD:
it reaches the wasm32 4 GB ceiling and throws `std::bad_alloc`, while the same
threaded build with dlmalloc opens the document. The browser transfer path
also streams into MEMFS without a full wasm `QByteArray` and releases the
source after import, removing another source-sized heap allocation and a
session-long JS file backing store.

`ui/wasm_memory_telemetry.cpp` (installed from the MainWindow constructor)
can publish the same picture to `globalThis.patchyMemStats` every second
(heapBytes, usedBytes, peakUsedBytes, limitBytes, historyBytes,
historyBudgetBytes, seq, timestampMs; seq and timestampMs detect staleness
during long synchronous compute) for page JS and the memory test harness.
It is diagnostics OPT-IN and inert for release visitors:
`?PATCHY_MEM_STATS=1` enables the publisher, `?PATCHY_MEM_LOG=1`
implies it and logs each sample to the console, and the harness page opts in
automatically through `globalThis.patchyExtraEnv` (folded into the app
environment by app-env-pre.js, explicit URL keys winning).

## In-app relief (wasm memory never shrinks)

History is byte-budgeted (256 MB on wasm, `history_memory_budget_bytes`,
floor 3 states/session). Under `Q_OS_WASM` the baked styled-layer cache caps
at 96 MB (image_document_io.cpp) and the style-mask LRU at 48 MB
(`style_mask_cache_budget_bytes`, memory_info.cpp).

## Safari 26 tab kill (fixed in Safari 26.6)

Safari 26 before 26.6 killed the threaded build's tab under load. It was
browser-side and is gone in macOS Safari 26.6.2 with the unchanged toolchain
(Qt 6.10.3, emsdk 4.0.7): `stress` `quick` exits cleanly (footprint peak 5.3 GB
during the compile storm, then ~1.9 GB) and a 15-minute `stress` `standard` run
holds flat at ~4.2 GB after a 6.4 GB early peak. Every browser therefore gets
the threaded build, with no WebKit notice or `st/` routing. iOS was not
retested. If the kill returns, rerun `wasm-safari-memtest.ps1 -Mode stress`
(the Safari/iOS section below); `wasm-release-st` still builds for
comparison.

What the investigation established, so it is not repeated:

- **Signature.** The WebContent process grew ~150 MB/s even at IDLE with
  400-1200% CPU until WebKit killed it (~2.5 minutes, footprint ~16 GB), while
  `patchyMemStats` stayed flat (512 MB heap, ~100 MB used) and Chrome held
  ~900 MB on the same page. `footprint` put the growth in "WebKit malloc";
  sample(1) put CPU and allocations in `JSC::B3::Air::Greedy::GreedyAllocator`
  under `parseAndCompileOMG`: Safari's optimizing wasm compiler, not the app
  (the same signature as public WebKit 26 reports against large wasm modules,
  e.g. onnxruntime issue 26827). iOS deaths ~2 s after load fit the same
  compile-side growth against jetsam, which the memory-ladder and pool URL
  knobs cannot dodge.
- **Ruled out.** SIMD (a no-SIMD build dies identically); megafunctions (43k
  functions, largest body 256 KB); link opt level (`-O2` and `-O1` survive
  idle but die under workload, since tier-up is execution-driven;
  `PATCHY_WASM_LINK_OPT` builds such variants, the shipped preset stays
  `-O3`); the compositor row kernels (an optnone build dies too). WebKit
  scrubs `JSC_*` variables from WebContent, so compiler tiers cannot be
  disabled externally.
- **Threading is not the cause.** A current-code `wasm-release-st` build dies
  like the threaded one. Only the older Qt 6.8.3 + emsdk 3.1.56 toolchain pair
  (ABI-locked; embind signatures changed) let the compile storm converge, and
  under stress it then climbed past 55 GB anyway, so an old toolchain only
  delays the pathology.

## Headless wasm stress harness

The harness page is `scripts/wasm/stress-harness.html` (staged by `build-wasm.bat` with the shared cache tag; production uploads exclude it, the beta upload ships it). It shims `requestAnimationFrame` onto `setTimeout`, re-implements the global timers on a Web Worker (Chrome and Safari throttle a hidden tab's main-thread timers to ~1/s; worker messages are exempt), suppresses `<a download>` clicks (overriding `HTMLAnchorElement.prototype.click`, since the save path clicks an unattached anchor), swallows the post-exit ExitStatus unhandled rejection, replicates patchy.html.in's `buildWasmMemory` (same knobs and ladders; keep in sync), and offers four workloads via `?mode=`: `stress` (passes `arguments: ['--stress-test=<preset>', '--stress-report-dir', '/stressout']` to qtLoad and polls the report from MEMFS via the exported `FS`), `soak` (writes `scripts/wasm/memsoak.js` into MEMFS from a `preRun` hook and runs it via `--run-script`; iterations chain through setTimeout so telemetry ticks between rounds), `open` (sends `?file=` through the real browser-drop path and checks open plus source cleanup), and `idle`. The CLI workloads end through qtloader's exit funnel: the CLI completion paths call `exit_cli_application` (ui/cli_exit.hpp), which on wasm shuts the Emscripten runtime down so `qt.onExit` fires and the harness records the `exited` terminal stage with the CLI exit code (crashes still report through the abort funnel; a bare `QCoreApplication::exit` would park the tab and make soak mode look wedged, see the exit rule in [wasm.md](wasm.md)). memsoak.js holds its run open for one trailing settle window so the last iteration's post-close sample lands before the exit. The first run of a new binary (or port) pays V8 tier-up and is discarded as warm-up; configs are compared in interleaved pairs on two ports, never sequentially (machine-load drift beats most effects).

For the exact large-open regression, stage a hard link and the raw harness:

```powershell
New-Item -ItemType HardLink -Path build\wasm-release\wasm-open-fixture.psd -Target 'local-test-fixtures\psd\C2Kyoto Nintendo NES Cartridge Label Template (Front).psd'
Copy-Item scripts\wasm\stress-harness.html build\wasm-release\stress-harness.html
pwsh -File scripts\wasm\serve-app.ps1 8974
```

Open `http://localhost:8974/stress-harness.html?mode=open`. Pass means
`open-passed` with `transferReleased: true`; `?timeout=` overrides 600000 ms.
Remove both staged files and stop port 8974 afterward.

Instrumentation on the page: an on-screen HUD (heap/used/cap/stage), 2 s `POST /memtest-sample` samples that `scripts/wasm/serve.py` appends to a JSONL (silently inert on plain Apache; the path avoids the word "telemetry", which content blockers kill), and a localStorage black box journaled synchronously at each load stage (memory-constructed, fetch, compile, qtload-start, loaded, running+Ns) so a browser memory kill, which fires no catchable event, is reported by the NEXT load as a banner with the death stage and last heap numbers. Three consecutive unclean ends gate the next boot behind a click so Safari's kill-then-reload loop cannot hammer a device. In-app numbers come from `globalThis.patchyMemStats` ([Reading the numbers](#reading-the-numbers-about-row-and-patchymemstats)), which the harness enables automatically.

## Safari/iOS memory diagnostics (mac build host)

`scripts\remote\wasm-safari-memtest.ps1` runs a full measured Safari session on the mac build host from Windows: pushes the staged site plus helpers to `~/patchy-memtest/`, starts `scripts/wasm/serve.py` on port 8993 (COOP/COEP; localhost is a secure context, so the threaded build boots), drives Safari via `scripts/remote/safari-memtest.py`, and copies the results back to `build\memtest-results\<run-id>\` (samples.csv, telemetry JSONL, events.log, summary.json, webkit-log.txt on a kill). Modes: `-Mode stress|soak|idle` (harness page, unattended) and `-Mode interactive` (real patchy.html; Seth drives, the sampler records, Safari and the server stay up). `-Knobs 'PATCHY_WASM_MAX_MB=1024&PATCHY_WASM_POOL=8'` forwards the shell-page memory knobs for A/B runs without rebuilding.

The sampler tracks the number WebKit's kill policy watches: per-process `footprint` of the app's WebContent process (identified as the first new WebContent instance crossing 300 MB), plus Safari and the GPU process (canvas/IOSurface growth lands there, invisible to wasm-side numbers). Reading the curves: used grows while heap stays flat = in-app leak; heap steps up at workload peaks while used returns low = peak ratchet (wasm memory never shrinks); WebContent footprint grows while heap stays flat = browser-side growth; GPU process grows = canvas/IOSurface. The default `open` driver needs no setup and quits Safari via pkill afterwards. `-Driver webdriver` additionally polls `patchyMemStats` on the real patchy.html and detects kills via a page epoch sentinel; it needs a one-time `sudo safaridriver --enable` on the mac build host plus Safari > Develop > Developer Settings > Allow Remote Automation, and if an ssh-launched safaridriver cannot reach the window server, start `safaridriver -p 4723` once in a Terminal on the mac build host (the script attaches to a listener).

iOS device runs use the beta deploy (`scripts\release\upload-wasm-to-rtsoft-beta.bat` publishes the same staged site plus the harness to https://www.rtsoft.com/patchy-beta/; an iPhone needs real https for SharedArrayBuffer, a LAN http server cannot boot the threaded build). Walk the memory knobs on the device against `stress-harness.html`; the black-box banner attributes deaths to a load stage. One-time device step: Settings > Apps > Safari > Advanced > Web Inspector ON (inspect from the mac build host's Safari Develop menu). After a death, fetch jetsam logs from the mac build host: `xcrun devicectl device copy from --device "<name>" --domain-type systemCrashLogs --source . --destination <dir>`, then read the newest JetsamEvent-*.ips: `per-process-limit` with a footprint near the chosen cap means the shared maximum was committed eagerly; `vm-pageshortage` well below limits means a transient spike (module compile or pool spawn).
