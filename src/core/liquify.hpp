#pragma once

#include "core/pixel_buffer.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace patchy {

// Manual Liquify tools only. Face detection and landmark-driven deformation are
// deliberately outside this model; see docs/liquify.md for the patent boundary.
enum class LiquifyTool {
  ForwardWarp,
  Reconstruct,
  Smooth,
  TwirlClockwise,
  TwirlCounterClockwise,
  Pucker,
  Bloat,
  FreezeMask,
  ThawMask,
};

// A bounded inverse-displacement field. Nodes use signed 24.8 fixed-point
// offsets so a completed gesture and its rendered pixels stay deterministic
// across toolchains. The field is normalized when rendered, allowing the UI to
// edit a small proxy and apply the same deformation to the full-resolution layer.
class LiquifyMesh {
public:
  using ProgressCallback = std::function<bool(int completed_rows, int total_rows)>;

  LiquifyMesh() = default;
  LiquifyMesh(int width, int height, int maximum_nodes_per_axis = 129);

  [[nodiscard]] int width() const noexcept;
  [[nodiscard]] int height() const noexcept;
  [[nodiscard]] int columns() const noexcept;
  [[nodiscard]] int rows() const noexcept;
  [[nodiscard]] bool is_identity() const noexcept;

  // Applies one sampled gesture. Size is a DIAMETER in mesh pixels; pressure
  // and density use the familiar 1..100 percent brush ranges.
  void apply_stroke(LiquifyTool tool, double from_x, double from_y,
                    double to_x, double to_y, double size,
                    double pressure, double density);
  void reset();

  [[nodiscard]] std::array<double, 2> displacement_at(double x,
                                                       double y) const;
  [[nodiscard]] double freeze_strength_at(double x, double y) const;

  // Returns nullopt when the caller cancels through progress. RGB and RGBA
  // UInt8 inputs retain their exact format and dimensions.
  [[nodiscard]] std::optional<PixelBuffer> render(
      const PixelBuffer& source, ProgressCallback progress = {}) const;

private:
  [[nodiscard]] std::size_t node_index(int column, int row) const noexcept;
  void apply_dab(LiquifyTool tool, double center_x, double center_y,
                 double delta_x, double delta_y, double radius,
                 double pressure, double density);

  int width_{0};
  int height_{0};
  int columns_{0};
  int rows_{0};
  std::vector<std::int32_t> displacement_x_256_;
  std::vector<std::int32_t> displacement_y_256_;
  std::vector<std::uint8_t> freeze_mask_;
};

}  // namespace patchy
