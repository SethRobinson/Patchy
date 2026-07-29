// Injected into the wasm test runner via --pre-js (see the wasm-core preset).
// Emscripten's default environ is synthetic (USER=web_user and friends) and
// ignores the host environment, so Patchy's environment escape hatches such as
// PATCHY_RENDER_SINGLE_THREADED and PATCHY_AF_TRACE would silently do nothing
// under node. Forward the real environment before main() runs, matching how
// the native test binaries see it. No effect outside node.
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
  if (typeof ENV !== 'undefined' && typeof process === 'object' && process.env) {
    for (var key in process.env) {
      ENV[key] = process.env[key];
    }
  }
});
