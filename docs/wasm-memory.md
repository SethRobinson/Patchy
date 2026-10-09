# WebAssembly memory

Deep reference for wasm memory: how the shared memory is constructed, what
the in-app numbers mean, the telemetry publisher, the in-app budgets, and
the resolved Safari 26 tab-kill record. Build, toolchain, and threading
rules live in [wasm.md](wasm.md); the measurement harness and the mac-host
workflow live in [performance.md](performance.md).

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
additionally logs each sample to the console, and the harness page opts in
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
(see [performance.md](performance.md)); `wasm-release-st` still builds for
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
