#pragma once

#include "core/pixel_buffer.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace photoslop {

using LayerId = std::uint64_t;

struct Rect {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};

  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] bool contains(std::int32_t px, std::int32_t py) const noexcept;

  static Rect from_size(std::int32_t width, std::int32_t height);
};

enum class BlendMode {
  PassThrough,
  Normal,
  Multiply,
  Screen,
  Overlay,
  Darken,
  Lighten,
  ColorDodge,
  ColorBurn,
  HardLight,
  Difference
};

enum class LayerKind {
  Pixel,
  Group,
  Adjustment,
  Text,
  Vector,
  SmartObject
};

struct UnknownPsdBlock {
  std::string key;
  std::vector<std::uint8_t> payload;
};

class Layer {
public:
  Layer() = default;
  Layer(LayerId id, std::string name, PixelBuffer pixels);
  Layer(LayerId id, std::string name, LayerKind kind);

  [[nodiscard]] LayerId id() const noexcept;
  [[nodiscard]] const std::string& name() const noexcept;
  [[nodiscard]] LayerKind kind() const noexcept;
  [[nodiscard]] bool visible() const noexcept;
  [[nodiscard]] float opacity() const noexcept;
  [[nodiscard]] BlendMode blend_mode() const noexcept;
  [[nodiscard]] Rect bounds() const noexcept;
  [[nodiscard]] PixelBuffer& pixels() noexcept;
  [[nodiscard]] const PixelBuffer& pixels() const noexcept;
  [[nodiscard]] std::vector<Layer>& children() noexcept;
  [[nodiscard]] const std::vector<Layer>& children() const noexcept;
  [[nodiscard]] std::map<std::string, std::string>& metadata() noexcept;
  [[nodiscard]] const std::map<std::string, std::string>& metadata() const noexcept;
  [[nodiscard]] std::vector<UnknownPsdBlock>& unknown_psd_blocks() noexcept;
  [[nodiscard]] const std::vector<UnknownPsdBlock>& unknown_psd_blocks() const noexcept;

  void set_name(std::string name);
  void set_visible(bool visible) noexcept;
  void set_opacity(float opacity);
  void set_blend_mode(BlendMode mode) noexcept;
  void set_bounds(Rect bounds) noexcept;
  void set_pixels(PixelBuffer pixels);
  void add_child(Layer child);

private:
  LayerId id_{0};
  std::string name_{"Layer"};
  LayerKind kind_{LayerKind::Pixel};
  bool visible_{true};
  float opacity_{1.0F};
  BlendMode blend_mode_{BlendMode::Normal};
  Rect bounds_{};
  PixelBuffer pixels_{};
  std::vector<Layer> children_{};
  std::map<std::string, std::string> metadata_{};
  std::vector<UnknownPsdBlock> unknown_psd_blocks_{};
};

}  // namespace photoslop
