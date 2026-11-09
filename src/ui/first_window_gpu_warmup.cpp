#include "ui/first_window_gpu_warmup.hpp"

#include "ui/background_workers.hpp"

#include <QGuiApplication>
#include <QString>

#include <chrono>
#include <condition_variable>
#include <mutex>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>
#endif

namespace patchy::ui {

struct FirstWindowGpuWarmup::State {
  std::mutex mutex;
  std::condition_variable released_changed;
  bool released = false;
};

#ifdef Q_OS_WIN
namespace {

// Past main()'s release, 2 s after the window shows; the worker never outlives it.
constexpr std::chrono::seconds kLongestHold{10};

}  // namespace
#endif

void FirstWindowGpuWarmup::start() {
#ifdef Q_OS_WIN
  if (state_ != nullptr) {
    return;
  }
  const QString platform = QGuiApplication::platformName();
  if (platform != QLatin1String("windows") && platform != QLatin1String("direct2d")) {
    return;
  }
  state_ = std::make_shared<State>();
  run_tracked_background_worker([state = state_] {
    // The system copy only, as Qt's own import resolves it.
    HMODULE module = LoadLibraryExW(L"d3d9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
      return;
    }
    using Create = IDirect3D9*(WINAPI*)(UINT);
    const auto create = reinterpret_cast<Create>(reinterpret_cast<void*>(GetProcAddress(module, "Direct3DCreate9")));
    IDirect3D9* direct3d = create != nullptr ? create(D3D_SDK_VERSION) : nullptr;
    {
      std::unique_lock lock(state->mutex);
      state->released_changed.wait_for(lock, kLongestHold, [&state] { return state->released; });
    }
    if (direct3d != nullptr) {
      direct3d->Release();
    }
    FreeLibrary(module);
  });
#endif
}

void FirstWindowGpuWarmup::release() {
  if (state_ == nullptr) {
    return;
  }
  {
    const std::lock_guard lock(state_->mutex);
    state_->released = true;
  }
  state_->released_changed.notify_all();
}

}  // namespace patchy::ui
