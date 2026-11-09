#pragma once

#include <memory>

// Overlaps a Qt cost of the first top-level window with building it
// (docs/startup-performance.md, "Real window").
//
// Qt 6.8's Windows plugin places every new top-level window through a QTBUG-50371
// workaround (calcPosition, forcedScreenForGLWindow) that, once per process, creates a
// Direct3D 9 object and lists the display adapters (GpuDescription::detect). That loads
// the GPU's user-mode driver and checks its signature: 95 to 140 ms on the GUI thread,
// inside the first window's creation, in an app that never draws with the GPU. start()
// creates one Direct3D 9 object on a tracked worker while MainWindow is built; holding it
// keeps the driver loaded, so Qt's own call returns in a few milliseconds. main() calls
// release() once startup has settled (unloading the driver takes the loader lock, which
// the first event-loop turns need), so the driver is not held for the session.
namespace patchy::ui {

class FirstWindowGpuWarmup {
 public:
  FirstWindowGpuWarmup() = default;
  ~FirstWindowGpuWarmup() { release(); }
  FirstWindowGpuWarmup(const FirstWindowGpuWarmup&) = delete;
  FirstWindowGpuWarmup& operator=(const FirstWindowGpuWarmup&) = delete;

  // Starts the worker on Qt's native Windows platform; does nothing elsewhere (the
  // offscreen platform of --headless and the tests creates no native window) or when
  // already started. Call after the QApplication exists.
  void start();
  // The worker releases its object (at once, or as soon as it has one) and ends; it
  // ends by itself 10 s after starting. Call before waiting for the tracked workers.
  void release();

  struct State;

 private:
  std::shared_ptr<State> state_;
};

}  // namespace patchy::ui
