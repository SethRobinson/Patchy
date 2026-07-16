#include "ui/background_workers.hpp"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace patchy::ui {

namespace {

std::mutex worker_mutex;
std::condition_variable worker_finished;
int live_workers = 0;

}  // namespace

void run_tracked_background_worker(std::function<void()> work) {
  {
    const std::lock_guard<std::mutex> lock(worker_mutex);
    ++live_workers;
  }
  std::thread([work = std::move(work)] {
    try {
      work();
    } catch (...) {
      // Workers handle their own errors; the count must balance regardless.
    }
    {
      const std::lock_guard<std::mutex> lock(worker_mutex);
      --live_workers;
    }
    worker_finished.notify_all();
  }).detach();
}

void wait_for_tracked_background_workers() {
  std::unique_lock<std::mutex> lock(worker_mutex);
  worker_finished.wait(lock, [] { return live_workers == 0; });
}

}  // namespace patchy::ui
