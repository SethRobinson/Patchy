#pragma once

#include <functional>

namespace patchy::ui {

// Detached render/preview workers tracked for shutdown. The async preview
// machinery captures a raw QCoreApplication* and invokeMethods on it from
// worker threads; nothing joined them at shutdown, so quitting mid-render
// could call into a destroyed QApplication or race static-cache teardown
// (the July 2026 refactor-backlog shutdown race). Every former
// std::thread(...).detach() site now runs through run_tracked_background_worker,
// and main() waits for the live count to reach zero after the event loop
// exits, before the QApplication is destroyed.
void run_tracked_background_worker(std::function<void()> work);

// Blocks until every tracked worker has finished. Call after
// QApplication::exec returns and before the QApplication is destroyed (the
// UI test runner does the same at teardown).
void wait_for_tracked_background_workers();

}  // namespace patchy::ui
