# WebAssembly memory

Deep reference for wasm memory: how the shared memory is constructed, what
the in-app numbers mean, the telemetry publisher, the in-app budgets, and
the open Safari 26 tab-kill investigation. Build, toolchain, and threading
rules live in [wasm.md](wasm.md); the measurement harness and the studiomac
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
this). The row shows three numbers: used (the allocator's live claim,
`emmalloc_dynamic_heap_size()` minus `emmalloc_free_dynamic_memory()`;
`-sMALLOC=mimalloc` layers mimalloc on emmalloc, and emmalloc's free-list
bookkeeping is the coherent number where mimalloc's own mi_process_info
stats wrap negative in the emscripten build), heap
(`emscripten_get_heap_size()`, the linear-memory buffer browser tab
accounting sees, which only ratchets), and the cap.

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
floor 3 states/session) and the style caches shrink to 96/48 MB under
`Q_OS_WASM` (image_document_io.cpp).

## Known issue: Safari 26 kills the tab within minutes (August 2026)

Measured on studiomac (macOS 26.3.1, Safari 26.x) with the memtest harness
(see [performance.md](performance.md)): the app's WebContent process grows
about 150 MB/s at IDLE with 400-1200% CPU and is killed by WebKit at
roughly 2.5 minutes (footprint plateaued at 16 GB, ps rss reached 24 GB).
The wasm side is innocent: patchyMemStats stays flat (512 MB heap, ~100 MB
used), and the `footprint` category breakdown puts the growth in "WebKit
malloc" (2.7 GB dirty 6 seconds after load), not the JS GC heap or JIT-code
regions. Chrome on the same machine with the same page holds flat at
~900 MB. The signature (concurrent compile threads burning CPU while
allocating unboundedly, other browsers unaffected) matches public
Safari/WebKit 26 reports against large wasm modules, e.g. onnxruntime issue
26827, where sampling showed JSC::Wasm::parseAndCompileOMG looping in
allocateStackByGraphColoring. A launchctl-env JSC_useOMGJIT=false test did
not change the behavior, but env propagation into WebContent XPC was
unverified, so tier attribution is open. iOS Safari deaths ~2 s after load
are consistent with the same compile-side growth against a phone's jetsam
budget and would be knob-independent (the memory-ladder and pool URL knobs
cannot dodge it). Leads: shrink/split the 66 MB module or the pathological
function(s) that blow up JSC's compiler, and file a WebKit bug
(rtsoft.com/patchy is a clean public repro).
