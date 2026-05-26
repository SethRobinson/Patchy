#pragma once

#include "core/pixel_buffer.hpp"

#include <cstdint>
#include <map>
#include <optional>
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
  SoftLight,
  Difference,
  LinearBurn,
  PinLight,
  Saturation,
  Luminosity
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

struct LayerMask {
  Rect bounds{};
  PixelBuffer pixels{};
  std::uint8_t default_color{255};
  bool disabled{false};
};

struct RgbColor {
  std::uint8_t red{0};
  std::uint8_t green{0};
  std::uint8_t blue{0};
};

struct GradientColorStop {
  float location{0.0F};
  RgbColor color{};
};

struct GradientAlphaStop {
  float location{0.0F};
  float opacity{1.0F};
};

enum class LayerStyleGradientType {
  Linear,
  Radial,
  Angle,
  Reflected,
  Diamond
};

struct LayerStyleGradient {
  LayerStyleGradientType type{LayerStyleGradientType::Linear};
  float angle_degrees{90.0F};
  float scale{1.0F};
  bool reverse{false};
  std::vector<GradientColorStop> color_stops;
  std::vector<GradientAlphaStop> alpha_stops;
};

struct LayerDropShadow {
  bool enabled{false};
  BlendMode blend_mode{BlendMode::Multiply};
  RgbColor color{0, 0, 0};
  float opacity{0.75F};
  float angle_degrees{120.0F};
  float distance{5.0F};
  float spread{0.0F};
  float size{5.0F};
};

struct LayerOuterGlow {
  bool enabled{false};
  BlendMode blend_mode{BlendMode::Screen};
  RgbColor color{255, 255, 190};
  float opacity{0.75F};
  float spread{0.0F};
  float size{5.0F};
};

struct LayerColorOverlay {
  bool enabled{false};
  BlendMode blend_mode{BlendMode::Normal};
  RgbColor color{255, 0, 0};
  float opacity{1.0F};
};

struct LayerGradientFill {
  bool enabled{false};
  BlendMode blend_mode{BlendMode::Normal};
  float opacity{1.0F};
  LayerStyleGradient gradient;
};

enum class LayerStrokePosition {
  Outside,
  Inside,
  Center
};

struct LayerStroke {
  bool enabled{false};
  BlendMode blend_mode{BlendMode::Normal};
  RgbColor color{0, 0, 0};
  float opacity{1.0F};
  float size{3.0F};
  LayerStrokePosition position{LayerStrokePosition::Outside};
  bool uses_gradient{false};
  LayerStyleGradient gradient;
};

struct LayerBevelEmboss {
  bool enabled{false};
  BlendMode highlight_blend_mode{BlendMode::Screen};
  RgbColor highlight_color{255, 255, 255};
  float highlight_opacity{0.75F};
  BlendMode shadow_blend_mode{BlendMode::Multiply};
  RgbColor shadow_color{0, 0, 0};
  float shadow_opacity{0.75F};
  float angle_degrees{120.0F};
  float altitude_degrees{30.0F};
  float depth{1.0F};
  float size{5.0F};
  bool direction_up{true};
};

struct LayerStyle {
  bool effects_visible{true};
  std::vector<LayerDropShadow> drop_shadows;
  std::vector<LayerOuterGlow> outer_glows;
  std::vector<LayerColorOverlay> color_overlays;
  std::vector<LayerGradientFill> gradient_fills;
  std::vector<LayerStroke> strokes;
  std::vector<LayerBevelEmboss> bevels;

  [[nodiscard]] bool empty() const noexcept;
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
  [[nodiscard]] std::optional<LayerMask>& mask() noexcept;
  [[nodiscard]] const std::optional<LayerMask>& mask() const noexcept;
  [[nodiscard]] std::vector<UnknownPsdBlock>& unknown_psd_blocks() noexcept;
  [[nodiscard]] const std::vector<UnknownPsdBlock>& unknown_psd_blocks() const noexcept;
  [[nodiscard]] LayerStyle& layer_style() noexcept;
  [[nodiscard]] const LayerStyle& layer_style() const noexcept;

  void set_name(std::string name);
  void set_visible(bool visible) noexcept;
  void set_opacity(float opacity);
  void set_blend_mode(BlendMode mode) noexcept;
  void set_bounds(Rect bounds) noexcept;
  void set_pixels(PixelBuffer pixels);
  void set_mask(LayerMask mask);
  void clear_mask() noexcept;
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
  std::optional<LayerMask> mask_{};
  std::vector<UnknownPsdBlock> unknown_psd_blocks_{};
  LayerStyle layer_style_{};
};

}  // namespace photoslop
