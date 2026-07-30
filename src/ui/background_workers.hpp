#pragma once

#include <QtGlobal>

#include <functional>
#include <future>
#include <type_traits>
#include <utility>

namespace patchy::ui {

// True when this build runs "background" work inline on the calling thread
// (single-threaded wasm: no worker threads exist). A pthreads-enabled wasm
// build defines __EMSCRIPTEN_PTHREADS__ and gets real threads, exactly like
// the desktop platforms. Callers that would only waste work by deferring to a
// worker that runs inline (for example paintEvent's deferred render refresh)
// branch on this instead of Q_OS_WASM.
#if defined(Q_OS_WASM) && !defined(__EMSCRIPTEN_PTHREADS__)
inline constexpr bool kBackgroundWorkRunsInline = true;
#else
inline constexpr bool kBackgroundWorkRunsInline = false;
#endif

// Detached render/preview workers tracked for shutdown. The async preview
// machinery captures a raw QCoreApplication* and invokeMethods on it from
// worker threads; nothing joined them at shutdown, so quitting mid-render
// could call into a destroyed QApplication or race static-cache teardown
// (the July 2026 refactor-backlog shutdown race). Every former
// std::thread(...).detach() site now runs through run_tracked_background_worker,
// and main() waits for the live count to reach zero after the event loop
// exits, before the QApplication is destroyed.
//
// On single-threaded wasm the work runs inline instead (no worker threads
// exist); callers observe the same ordering because completions are posted
// through queued invokeMethod either way.
void run_tracked_background_worker(std::function<void()> work);

// Blocks until every tracked worker has finished. Call after
// QApplication::exec returns and before the QApplication is destroyed (the
// UI test runner does the same at teardown).
void wait_for_tracked_background_workers();

// std::async(std::launch::async, fn) with a single-threaded-wasm fallback:
// there the work runs inline and the returned future is already ready
// (exceptions land in the future, so .get() rethrows exactly like the async
// path). The await loops of the form
//   while (future.wait_for(...) != std::future_status::ready) processEvents();
// then exit on their first check. Never swap those sites to
// std::launch::deferred instead: a deferred future reports
// future_status::deferred forever, which turns that loop into a spin.
template <typename Fn>
[[nodiscard]] auto launch_async(Fn&& fn) -> std::future<std::invoke_result_t<Fn>> {
#if defined(Q_OS_WASM) && !defined(__EMSCRIPTEN_PTHREADS__)
  std::promise<std::invoke_result_t<Fn>> promise;
  try {
    if constexpr (std::is_void_v<std::invoke_result_t<Fn>>) {
      std::forward<Fn>(fn)();
      promise.set_value();
    } else {
      promise.set_value(std::forward<Fn>(fn)());
    }
  } catch (...) {
    promise.set_exception(std::current_exception());
  }
  return promise.get_future();
#else
  return std::async(std::launch::async, std::forward<Fn>(fn));
#endif
}

}  // namespace patchy::ui
