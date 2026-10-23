#pragma once

// Mouse positions the windowing system folds into one move event while the
// GUI thread is busy (GitHub issue 165). Windows synthesizes WM_MOUSEMOVE only
// when the queue is read, so a stroke that composites between moves saw one
// sample per frame and fast curves came out as straight segments; the system
// mouse history (GetMouseMovePointsEx) still holds the skipped positions.
// X11 drops queued motion events unless Qt::AA_CompressHighFrequencyEvents is
// off, and AppKit coalesces mouse and tablet drags unless NSEvent's mouse
// coalescing is off; CanvasWidget turns both off for the length of a paint
// stroke.

#include <QPointF>
#include <QtGlobal>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

class QMouseEvent;
class QWidget;

namespace patchy::ui {

// One entry of a native pointer history: screen device pixels plus the
// system tick (milliseconds, wrapping) the position was recorded at.
struct PointerHistoryPoint {
  std::int32_t x{0};
  std::int32_t y{0};
  std::uint32_t time{0};

  friend bool operator==(const PointerHistoryPoint&, const PointerHistoryPoint&) = default;
};

// The history entries recorded after `previous` and before the current point,
// oldest first. `newest_first[0]` is the current point itself; the walk stops
// at `previous` or at the first entry older than it. Without a previous point
// nothing is recovered (the stroke has no anchor to join them to).
[[nodiscard]] std::vector<PointerHistoryPoint> pointer_history_since(
    std::span<const PointerHistoryPoint> newest_first, std::optional<PointerHistoryPoint> previous);

// Per-stroke state for coalesced_mouse_positions.
struct CoalescedMouseState {
  std::optional<PointerHistoryPoint> previous;
};

// Start a stroke at this (press) event: later moves recover the positions
// recorded after it.
void begin_coalesced_mouse_tracking(const QWidget& widget, const QMouseEvent& event, CoalescedMouseState& state);

// The positions folded into `event` since the previous call, in widget
// coordinates, oldest first, without the event's own position. Empty where
// the platform keeps no history (everything but Windows), for synthesized
// events, and whenever the history cannot be matched to the event.
[[nodiscard]] std::vector<QPointF> coalesced_mouse_positions(const QWidget& widget, const QMouseEvent& event,
                                                             CoalescedMouseState& state);

#ifdef Q_OS_MACOS
// [NSEvent setMouseCoalescingEnabled:]; returns the previous setting
// (coalesced_pointer_input_mac.mm).
bool set_mac_mouse_coalescing_enabled(bool enabled);
#endif

// Holds Qt::AA_CompressHighFrequencyEvents (and, on macOS, AppKit's mouse
// coalescing) off while engaged and puts the previous values back on release
// or destruction. Both are read per incoming event, so they can follow a
// stroke's press and release.
class UncompressedPointerInputScope {
 public:
  UncompressedPointerInputScope() = default;
  UncompressedPointerInputScope(const UncompressedPointerInputScope&) = delete;
  UncompressedPointerInputScope& operator=(const UncompressedPointerInputScope&) = delete;
  ~UncompressedPointerInputScope();

  void set_engaged(bool engaged);
  [[nodiscard]] bool engaged() const noexcept { return engaged_; }

 private:
  bool engaged_{false};
  bool saved_compression_{true};
#ifdef Q_OS_MACOS
  bool saved_mac_coalescing_{true};
#endif
};

}  // namespace patchy::ui
