#pragma once

#include "core/pixel_buffer.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace patchy {

using LayerId = std::uint64_t;
using LayerLockFlags = std::uint32_t;

inline constexpr LayerLockFlags kLayerLockNone = 0U;
inline constexpr LayerLockFlags kLayerLockTransparentPixels = 1U << 0U;
inline constexpr LayerLockFlags kLayerLockImagePixels = 1U << 1U;
inline constexpr LayerLockFlags kLayerLockPosition = 1U << 2U;
inline constexpr LayerLockFlags kLayerLockAll =
    kLayerLockTransparentPixels | kLayerLockImagePixels | kLayerLockPosition;

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
  Luminosity,
  // July 2026 additions (append-only: the values ride combo-box data and PSD/Aseprite maps).
  Exclusion,
  Hue,
  Color,
  LinearDodge,  // Photoshop "Linear Dodge (Add)" / Aseprite "Addition"
  Subtract,
  Divide
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
  // True when the source file stored this tagged block with the '8B64' signature and an
  // 8-byte length (PSB); the writer re-emits the same form so Photoshop's key-based
  // parser stays in sync. False for the common '8BIM' + 4-byte form.
  bool long_length{false};
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
  // Photoshop's "Use Global Light". Only meaningful while importing: the PSD reader
  // resolves the document's global angle into angle_degrees and clears this flag.
  bool use_global_light{false};
};

struct LayerInnerShadow {
  bool enabled{false};
  BlendMode blend_mode{BlendMode::Multiply};
  RgbColor color{0, 0, 0};
  float opacity{0.75F};
  float angle_degrees{120.0F};
  float distance{5.0F};
  float choke{0.0F};
  float size{5.0F};
  bool use_global_light{false};
};

struct LayerOuterGlow {
  bool enabled{false};
  BlendMode blend_mode{BlendMode::Normal};
  RgbColor color{255, 255, 190};
  float opacity{0.75F};
  float spread{0.0F};
  float size{5.0F};
};

enum class LayerInnerGlowSource {
  Center,
  Edge
};

struct LayerInnerGlow {
  bool enabled{false};
  BlendMode blend_mode{BlendMode::Screen};
  RgbColor color{255, 255, 190};
  float opacity{0.75F};
  float choke{0.0F};
  float size{5.0F};
  LayerInnerGlowSource source{LayerInnerGlowSource::Edge};
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
  bool use_global_light{false};
};

struct LayerSatin {
  bool enabled{false};
  BlendMode blend_mode{BlendMode::Multiply};
  RgbColor color{0, 0, 0};
  float opacity{0.5F};
  float angle_degrees{19.0F};
  float distance{11.0F};
  float size{14.0F};
  bool invert{true};
};

struct LayerPatternOverlay {
  bool enabled{false};
  BlendMode blend_mode{BlendMode::Normal};
  float opacity{1.0F};
  float scale{1.0F};
  std::string pattern_name;
  std::string pattern_id;
};

struct LayerStyle {
  bool effects_visible{true};
  // Photoshop's "Layer Mask Hides Effects" blending option: the layer mask also
  // clips effect output where it lands, instead of only shaping effect sources.
  bool layer_mask_hides_effects{false};
  std::vector<LayerDropShadow> drop_shadows;
  std::vector<LayerInnerShadow> inner_shadows;
  std::vector<LayerOuterGlow> outer_glows;
  std::vector<LayerInnerGlow> inner_glows;
  std::vector<LayerColorOverlay> color_overlays;
  std::vector<LayerGradientFill> gradient_fills;
  std::vector<LayerPatternOverlay> pattern_overlays;
  std::vector<LayerStroke> strokes;
  std::vector<LayerBevelEmboss> bevels;
  std::vector<LayerSatin> satins;

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
  [[nodiscard]] LayerLockFlags lock_flags() const noexcept;
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
  [[nodiscard]] std::uint64_t render_revision() const noexcept;
  [[nodiscard]] std::uint64_t content_revision() const noexcept;
  [[nodiscard]] Layer clone_with_id(LayerId id) const;

  void set_name(std::string name);
  void set_visible(bool visible) noexcept;
  void set_opacity(float opacity);
  void set_blend_mode(BlendMode mode) noexcept;
  void set_lock_flags(LayerLockFlags flags) noexcept;
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
  LayerLockFlags lock_flags_{kLayerLockNone};
  Rect bounds_{};
  PixelBuffer pixels_{};
  std::vector<Layer> children_{};
  std::map<std::string, std::string> metadata_{};
  std::optional<LayerMask> mask_{};
  std::vector<UnknownPsdBlock> unknown_psd_blocks_{};
  LayerStyle layer_style_{};
  std::uint64_t render_revision_{1};
  std::uint64_t content_revision_{1};
};

}  // namespace patchy
