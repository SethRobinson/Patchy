#pragma once

#include <QObject>
#include <QTimer>

#include <functional>
#include <optional>
#include <utility>

namespace patchy::ui {

// Rate-limits live-preview callbacks from rapidly changing dialog controls:
// schedule() coalesces bursts of updates into one delivery per interval,
// flush() delivers immediately (commit/cancel paths). The timer is owned by
// `owner`, so the emitter must not outlive it.
//
// A throttle, not a debounce: a change arriving while the timer runs only
// replaces the pending settings, so a slider dragged continuously (a change
// every few milliseconds) still previews about every interval instead of
// only once the drag pauses (GitHub #601).
inline constexpr int kCoalescedPreviewDelayMs = 33;

template <typename Settings>
class CoalescedPreviewEmitter {
public:
  CoalescedPreviewEmitter(QObject& owner, std::function<void(const Settings&)> callback)
      : callback_(std::move(callback)) {
    timer_ = new QTimer(&owner);
    timer_->setSingleShot(true);
    timer_->setInterval(kCoalescedPreviewDelayMs);
    QObject::connect(timer_, &QTimer::timeout, &owner, [this] { deliver(); });
  }

  void schedule(Settings settings) {
    pending_ = std::move(settings);
    if (!timer_->isActive()) {
      timer_->start();
    }
  }

  void flush(Settings settings) {
    timer_->stop();
    pending_ = std::move(settings);
    deliver();
  }

private:
  void deliver() {
    if (!pending_.has_value() || !callback_) {
      return;
    }
    auto settings = std::move(*pending_);
    pending_.reset();
    callback_(settings);
  }

  QTimer* timer_{nullptr};
  std::optional<Settings> pending_;
  std::function<void(const Settings&)> callback_;
};

}  // namespace patchy::ui
