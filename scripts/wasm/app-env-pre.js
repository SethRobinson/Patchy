// Injected into the wasm app via --pre-js (see the app link block in
// CMakeLists.txt). Emscripten's environ is synthetic and a browser has no
// process environment at all, so Patchy's environment escape hatches such as
// PATCHY_RENDER_SINGLE_THREADED would silently do nothing in the web build.
// Forward PATCHY_* keys from the page URL's query string before main() runs:
// http://localhost:8973/patchy.html?PATCHY_RENDER_SINGLE_THREADED=1
// Only PATCHY_-prefixed keys are forwarded, so the cache-busting ?v= tag and
// any other page parameters never leak into the app environment. Inert inside
// pthread workers (no window there); workers share the main thread's environ.
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
  if (typeof ENV === 'undefined' || typeof window === 'undefined' ||
      !window.location || typeof URLSearchParams === 'undefined') {
    return;
  }
  // Page-supplied baseline first (the memtest harness sets
  // globalThis.patchyExtraEnv to turn diagnostics on without dirtying its
  // URL); explicit URL query keys override it.
  var extra = window.patchyExtraEnv;
  if (extra && typeof extra === 'object') {
    for (var key in extra) {
      if (Object.prototype.hasOwnProperty.call(extra, key) &&
          key.lastIndexOf('PATCHY_', 0) === 0) {
        ENV[key] = String(extra[key]);
      }
    }
  }
  var params = new URLSearchParams(window.location.search);
  params.forEach(function (value, key) {
    if (key.lastIndexOf('PATCHY_', 0) === 0) {
      ENV[key] = value;
    }
  });
});
