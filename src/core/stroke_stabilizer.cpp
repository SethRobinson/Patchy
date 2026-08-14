#include "core/stroke_stabilizer.hpp"

#include <cmath>

namespace patchy {

void StrokeStabilizer::begin(double x, double y, const StrokeStabilizerConfig& config) {
  config_ = config;
  raw_ = Point{x, y};
  output_ = raw_;
  begun_ = true;
}

StrokeStabilizer::Point StrokeStabilizer::move(double x, double y) {
  raw_ = Point{x, y};
  if (!active()) {
    // Exact pass-through: hand back the identical doubles (no arithmetic), so
    // a smoothing-0 stroke is byte-identical to one that never came here.
    output_ = raw_;
    return output_;
  }
  const auto dx = raw_.x - output_.x;
  const auto dy = raw_.y - output_.y;
  const auto distance = std::hypot(dx, dy);
  if (distance > config_.leash_radius) {
    // Taut leash: the output advances along (raw - output) by the overshoot so
    // it sits exactly leash_radius behind the raw point.
    const auto pull = (distance - config_.leash_radius) / distance;
    output_.x += dx * pull;
    output_.y += dy * pull;
  }
  // Inside the radius the string is slack and the output holds still: the dead
  // zone, identical in both modes.
  return output_;
}

std::optional<StrokeStabilizer::Point> StrokeStabilizer::tick(double dt_seconds) {
  // Pulled-string mode is Photoshop's: slack movement never paints and the
  // string does not creep while the pointer rests.
  if (!begun_ || !active() || !config_.catch_up || config_.pulled_string) {
    return std::nullopt;
  }
  const auto dx = raw_.x - output_.x;
  const auto dy = raw_.y - output_.y;
  const auto factor = 1.0 - std::exp(-config_.catch_up_rate * dt_seconds);
  const auto step_x = dx * factor;
  const auto step_y = dy * factor;
  if (std::hypot(step_x, step_y) <= 1e-9) {
    return std::nullopt;
  }
  output_.x += step_x;
  output_.y += step_y;
  return output_;
}

StrokeStabilizer::Point StrokeStabilizer::finish(double x, double y) {
  raw_ = Point{x, y};
  // Inactive keeps move()'s pass-through contract; active releases at the raw
  // point only when Catch-up on Stroke End is on.
  if (!active() || config_.catch_up_on_end) {
    output_ = raw_;
  }
  return output_;
}

bool StrokeStabilizer::active() const noexcept {
  return config_.leash_radius > 0.0;
}

StrokeStabilizer::Point StrokeStabilizer::output() const noexcept {
  return output_;
}

StrokeStabilizer::Point StrokeStabilizer::raw() const noexcept {
  return raw_;
}

}  // namespace patchy
