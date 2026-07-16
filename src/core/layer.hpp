#pragma once

#include "core/pixel_buffer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace patchy {

struct SmartFilterStack;
struct VectorShapeContent;
struct LayerVectorMask;

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
  // Position among document-global tagged blocks. Layer-level blocks and
  // newly-authored globals leave this at SIZE_MAX. Tracking unknown globals as
  // well as parsed stores keeps surviving blocks in their original order when
  // a Smart Object or Smart Filter record is removed.
  std::size_t original_global_index{static_cast<std::size_t>(-1)};
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

  friend bool operator==(const RgbColor&, const RgbColor&) = default;
};

// Photoshop Blend If stores the two halves of each black and white triangle
// directly. Joined handles have equal values; split handles describe the
// partially blended transition between the two values.
struct BlendIfThresholds {
  std::uint8_t black_low{0};
  std::uint8_t black_high{0};
  std::uint8_t white_low{255};
  std::uint8_t white_high{255};

  bool operator==(const BlendIfThresholds&) const = default;
};

struct BlendIfChannelRanges {
  BlendIfThresholds this_layer;
  BlendIfThresholds underlying_layer;

  bool operator==(const BlendIfChannelRanges&) const = default;
};

enum class BlendIfChannel : std::uint8_t {
  Gray,
  Red,
  Green,
  Blue,
};

struct LayerBlendIf {
  // Composite Gray, Red, Green, and Blue, in Photoshop's native record order.
  std::array<BlendIfChannelRanges, 4> channels{};

  bool operator==(const LayerBlendIf&) const = default;
};

enum class BlendIfPayloadStatus : std::uint8_t {
  Empty,
  Supported,
  Unsupported,
};

struct DecodedLayerBlendIf {
  LayerBlendIf settings;
  BlendIfPayloadStatus status{BlendIfPayloadStatus::Empty};
};

[[nodiscard]] bool blend_if_thresholds_are_valid(const BlendIfThresholds& thresholds) noexcept;
[[nodiscard]] bool blend_if_is_identity(const LayerBlendIf& settings) noexcept;
[[nodiscard]] DecodedLayerBlendIf decode_layer_blend_if(std::span<const std::uint8_t> payload) noexcept;
[[nodiscard]] bool blend_if_payload_has_non_identity_or_unsupported(
    std::span<const std::uint8_t> payload) noexcept;
[[nodiscard]] std::vector<std::uint8_t> encode_layer_blend_if(
    const LayerBlendIf& settings, std::span<const std::uint8_t> original_payload = {});

struct GradientColorStop {
  float location{0.0F};
  RgbColor color{};
  // Relative position between the previous stop and this destination stop where
  // the blend reaches 50%. Photoshop stores this as Mdpn (0..100); the first
  // stop's value is unused and 0.5 is the identity path.
  float midpoint{0.5F};
  enum class Kind { User, Foreground, Background };
  // Photoshop GRD presets may defer a stop to the current foreground or
  // background color. Layer effects normally contain resolved User colors,
  // while the application preset library preserves these roles.
  Kind kind{Kind::User};

  friend bool operator==(const GradientColorStop&, const GradientColorStop&) = default;
};

struct GradientAlphaStop {
  float location{0.0F};
  float opacity{1.0F};
  // Same destination-stop convention as GradientColorStop::midpoint.
  float midpoint{0.5F};

  friend bool operator==(const GradientAlphaStop&, const GradientAlphaStop&) = default;
};

enum class LayerStyleGradientType {
  Linear,
  Radial,
  Angle,
  Reflected,
  Diamond
};

enum class GradientDefinitionForm { Solid, Noise };

enum class GradientNoiseColorModel { RGB, HSB, Lab };

enum class GradientInterpolationMethod { Classic, Perceptual, Linear };

struct GradientNoiseSettings {
  std::uint32_t seed{0};
  // Photoshop stores roughness in the descriptor's Smth field on a 0..4096
  // scale. Keeping the native integer avoids needless preset round-trip drift.
  std::uint16_t roughness{2048};
  bool add_transparency{false};
  bool restrict_colors{true};
  GradientNoiseColorModel color_model{GradientNoiseColorModel::RGB};
  std::array<std::uint16_t, 4> minimum{0, 0, 0, 0};
  std::array<std::uint16_t, 4> maximum{100, 100, 100, 100};

  friend bool operator==(const GradientNoiseSettings&, const GradientNoiseSettings&) = default;
};

struct GradientDefinition {
  std::string name{"Custom"};
  GradientDefinitionForm form{GradientDefinitionForm::Solid};
  // Photoshop descriptor Intr, 0..4096. 4096 is the normal 100% smoothness.
  std::uint16_t smoothness{4096};
  std::vector<GradientColorStop> color_stops;
  std::vector<GradientAlphaStop> alpha_stops;
  GradientNoiseSettings noise;

  friend bool operator==(const GradientDefinition&, const GradientDefinition&) = default;
};

struct LayerStyleGradient : GradientDefinition {
  LayerStyleGradientType type{LayerStyleGradientType::Linear};
  float angle_degrees{90.0F};
  float scale{1.0F};
  bool reverse{false};
  bool dither{false};
  GradientInterpolationMethod interpolation{GradientInterpolationMethod::Classic};
  bool align_with_layer{true};
  float offset_x_percent{0.0F};
  float offset_y_percent{0.0F};

  friend bool operator==(const LayerStyleGradient&, const LayerStyleGradient&) = default;
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

// A layer-effect contour curve (the descriptor ShpC/CrPt shape shared by the
// bevel gloss contour, the bevel Contour sub-option, and — preservation-only —
// the shadow/glow/satin contours). Empty points mean the 2-point Linear
// identity. Coordinates are 0..255 doubles in the file; corner points break the
// smooth spline (Photoshop's contour editor "Corner" checkbox, stored per point
// as Cnty=false). build_style_contour_lut (core/style_contour.hpp) renders it.
struct StyleContourPoint {
  float x{0.0F};
  float y{0.0F};
  bool corner{false};

  friend bool operator==(const StyleContourPoint&, const StyleContourPoint&) = default;
};

struct StyleContour {
  // Persisted display name (PS writes "Linear" for the default; imported names
  // are kept verbatim so an untouched curve round-trips its label).
  std::string name{"Linear"};
  std::vector<StyleContourPoint> points;  // empty == Linear (0,0)-(255,255)

  friend bool operator==(const StyleContour&, const StyleContour&) = default;
};

// Bevel & Emboss "Contour" sub-option (ebbl useShape/MpgS/AntA/Inpr).
struct LayerBevelContour {
  bool enabled{false};
  StyleContour contour;
  bool anti_aliased{false};
  float range{0.5F};  // Inpr percent / 100; Photoshop default 50%
};

// Bevel & Emboss "Texture" sub-option (ebbl useTexture + pattern keys). The
// pattern pixels live in the document's PatternStore, referenced by id.
struct LayerBevelTexture {
  bool enabled{false};
  std::string pattern_name;
  std::string pattern_id;
  float scale{1.0F};           // Scl percent / 100
  float depth{1.0F};           // textureDepth percent / 100; signed, -10..10
  bool invert{false};          // InvT
  bool link_with_layer{true};  // Algn: anchor to the layer's fxrp reference point
  float phase_x{0.0F};         // phase Pnt, document pixels
  float phase_y{0.0F};
};

// Append-only: combo item data and the PSD enum maps ride the existing order.
enum class BevelEmbossStyleKind {
  InnerBevel,   // InrB
  OuterBevel,   // OtrB
  Emboss,       // Embs
  PillowEmboss, // PlEb
  StrokeEmboss  // strokeEmboss (stringID-only)
};

enum class BevelTechnique {
  Smooth,     // SfBL
  ChiselHard, // PrBL
  ChiselSoft  // Slmt
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
  // Persisted PSD enum/value fields and Layer Style dialog controls.
  BevelEmbossStyleKind style{BevelEmbossStyleKind::InnerBevel};
  BevelTechnique technique{BevelTechnique::Smooth};
  float soften{0.0F};  // Sftn pixels
  StyleContour gloss_contour;      // TrnS (main-page Gloss Contour)
  bool gloss_anti_aliased{false};  // antialiasGloss
  LayerBevelContour contour;
  LayerBevelTexture texture;
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
  // Untouched Photoshop lfx2 data preserves custom contour points and AntA.
  // Patchy's modeled renderer/editor uses a non-anti-aliased Linear contour.
  bool unsupported_contour_options{false};
};

struct LayerPatternOverlay {
  bool enabled{false};
  BlendMode blend_mode{BlendMode::Normal};
  float opacity{1.0F};
  float scale{1.0F};
  std::string pattern_name;
  std::string pattern_id;
  float angle_degrees{0.0F};   // Angl (PS 2026 writes it; rotation of the tile grid)
  bool link_with_layer{true};  // Algn: anchor to the layer's fxrp reference point
  float phase_x{0.0F};         // phase Pnt, document pixels
  float phase_y{0.0F};
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
  [[nodiscard]] bool clipped() const noexcept;
  [[nodiscard]] float opacity() const noexcept;
  [[nodiscard]] float fill_opacity() const noexcept;
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
  // Original payloads from the layer-record blending-ranges field. Semantic RGB
  // edits go through set_blend_if(); these raw accessors remain no-bump PSD
  // preservation plumbing. The group-boundary payload belongs to the synthetic
  // closing record folded into its corresponding group and is always raw-only.
  [[nodiscard]] std::vector<std::uint8_t>& raw_psd_blending_ranges() noexcept;
  [[nodiscard]] const std::vector<std::uint8_t>& raw_psd_blending_ranges() const noexcept;
  [[nodiscard]] std::vector<std::uint8_t>& raw_psd_group_boundary_blending_ranges() noexcept;
  [[nodiscard]] const std::vector<std::uint8_t>& raw_psd_group_boundary_blending_ranges() const noexcept;
  [[nodiscard]] LayerBlendIf blend_if() const noexcept;
  [[nodiscard]] BlendIfPayloadStatus blend_if_payload_status() const noexcept;
  [[nodiscard]] bool blend_if_rgb_compatible() const noexcept;
  [[nodiscard]] std::vector<UnknownPsdBlock>& unknown_psd_blocks() noexcept;
  [[nodiscard]] const std::vector<UnknownPsdBlock>& unknown_psd_blocks() const noexcept;
  [[nodiscard]] LayerStyle& layer_style() noexcept;
  [[nodiscard]] const LayerStyle& layer_style() const noexcept;
  // Smart Filter semantics are immutable through Layer: PSD import and later
  // editors replace the whole stack through set_smart_filter_stack(), so a
  // read can never silently bypass revision tracking.
  [[nodiscard]] const SmartFilterStack* smart_filter_stack() const noexcept;
  // Vector shape content / vector mask follow the same immutable-through-Layer
  // pattern (replace whole via the setters; shared across undo snapshots).
  [[nodiscard]] const VectorShapeContent* vector_shape() const noexcept;
  [[nodiscard]] const LayerVectorMask* vector_mask() const noexcept;
  [[nodiscard]] std::uint64_t render_revision() const noexcept;
  [[nodiscard]] std::uint64_t content_revision() const noexcept;
  // Changes only when the pixel buffer may have changed. Alpha-bound caches
  // use this instead of content_revision, which also changes for style edits.
  [[nodiscard]] std::uint64_t pixel_revision() const noexcept;
  [[nodiscard]] Layer clone_with_id(LayerId id) const;

  void set_name(std::string name);
  void set_visible(bool visible) noexcept;
  void set_clipped(bool clipped) noexcept;
  void set_opacity(float opacity);
  void set_fill_opacity(float opacity);
  void set_blend_mode(BlendMode mode) noexcept;
  void set_lock_flags(LayerLockFlags flags) noexcept;
  void set_bounds(Rect bounds) noexcept;
  void set_pixels(PixelBuffer pixels);
  void set_mask(LayerMask mask);
  void clear_mask() noexcept;
  // Semantic Blend If edits are revision-aware and regenerate only the known
  // range bytes. Raw access above remains no-bump preservation plumbing for PSD
  // import/export and unknown payload shapes.
  [[nodiscard]] bool set_blend_if(const LayerBlendIf& settings, bool replace_unsupported = false);
  void set_blend_if_payload(std::vector<std::uint8_t> payload, bool rgb_compatible = true);
  void set_blend_if_rgb_compatible(bool compatible) noexcept;
  void set_smart_filter_stack(SmartFilterStack stack);
  void clear_smart_filter_stack() noexcept;
  void set_vector_shape(VectorShapeContent content);
  void clear_vector_shape() noexcept;
  void set_vector_mask(LayerVectorMask mask);
  void clear_vector_mask() noexcept;
  void add_child(Layer child);
  // For composition-affecting state changes that live on ANOTHER layer (e.g. a
  // sibling joining/leaving this layer's clipping group): bumps the render
  // revision so revision-diffing repaint paths notice, without touching the
  // content revision that keys thumbnail/style-mask caches.
  void mark_render_changed() noexcept;

private:
  LayerId id_{0};
  std::string name_{"Layer"};
  LayerKind kind_{LayerKind::Pixel};
  bool visible_{true};
  bool clipped_{false};
  float opacity_{1.0F};
  float fill_opacity_{1.0F};
  BlendMode blend_mode_{BlendMode::Normal};
  LayerLockFlags lock_flags_{kLayerLockNone};
  Rect bounds_{};
  PixelBuffer pixels_{};
  std::vector<Layer> children_{};
  std::map<std::string, std::string> metadata_{};
  std::optional<LayerMask> mask_{};
  std::vector<std::uint8_t> raw_psd_blending_ranges_{};
  std::vector<std::uint8_t> raw_psd_group_boundary_blending_ranges_{};
  bool blend_if_rgb_compatible_{true};
  std::vector<UnknownPsdBlock> unknown_psd_blocks_{};
  LayerStyle layer_style_{};
  std::shared_ptr<const SmartFilterStack> smart_filter_stack_{};
  std::shared_ptr<const VectorShapeContent> vector_shape_{};
  std::shared_ptr<const LayerVectorMask> vector_mask_{};
  std::uint64_t render_revision_{1};
  std::uint64_t content_revision_{1};
  std::uint64_t pixel_revision_{1};
};

// Photoshop's optional `lyid` block is a per-layer identity. Imported blocks
// are preserved, while duplication uses these helpers to assign a distinct id
// without confusing it with Patchy's runtime LayerId.
[[nodiscard]] std::optional<std::uint32_t>
photoshop_layer_id(const Layer& layer) noexcept;
void set_photoshop_layer_id(Layer& layer, std::uint32_t id);
[[nodiscard]] std::uint32_t
next_photoshop_layer_id(const std::vector<Layer>& layers);

}  // namespace patchy
