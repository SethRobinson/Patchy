#pragma once

// Photoshop-style stroke Smoothing: a taut-leash ("pulled string") pointer
// stabilizer. The raw pointer drags a string of fixed leash_radius; the stroke
// output trails at the string's end, so jitter inside the radius never paints
// and fast turns are rounded off. Pure geometry, no Qt types and no clock
// reads: stationary catch-up advances only through tick(dt) with a
// caller-supplied dt, so runs are deterministic.
//
// Patent boundary (sweep 2026-08-14, docs/patent-research.md): this must stay
// a pure geometric leash. Never assign the cursor a model mass or a
// motion-derived drag/friction factor (Adobe US 9411796), never make the
// smoothing strength speed-adaptive, and never fit incremental Bezier curves
// to the input points (Microsoft US 9508166).

#include <optional>

namespace patchy {

struct StrokeStabilizerConfig {
  double leash_radius{0.0};   // document px; 0 = exact pass-through
  bool pulled_string{false};  // slack movement never paints; no stationary creep
  bool catch_up{true};        // stationary output drifts toward the raw pointer
  bool catch_up_on_end{true};  // finish() releases at the raw point
  double catch_up_rate{12.0};  // 1/seconds: exponential approach rate for tick()
};

class StrokeStabilizer {
 public:
  struct Point {
    double x{0.0};
    double y{0.0};
  };

  // Resets all state; output snaps to the press point (the press dab always
  // lands where the user pressed).
  void begin(double x, double y, const StrokeStabilizerConfig& config);
  // New raw pointer sample -> smoothed output.
  [[nodiscard]] Point move(double x, double y);
  // Stationary catch-up; nullopt when disabled, suppressed, or converged.
  [[nodiscard]] std::optional<Point> tick(double dt_seconds);
  // Release at raw (x, y): returns the raw point when catch_up_on_end, else
  // the current output.
  [[nodiscard]] Point finish(double x, double y);
  [[nodiscard]] bool active() const noexcept;
  [[nodiscard]] Point output() const noexcept;
  [[nodiscard]] Point raw() const noexcept;

 private:
  StrokeStabilizerConfig config_{};
  Point output_{};
  Point raw_{};
  bool begun_{false};
};

}  // namespace patchy
