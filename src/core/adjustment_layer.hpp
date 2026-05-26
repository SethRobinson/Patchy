#pragma once

#include "core/layer.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace photoslop {

inline constexpr const char* kLayerMetadataAdjustmentType = "photoslop.adjustment.type";
inline constexpr const char* kLayerMetadataAdjustmentLevelsBlackInput = "photoslop.adjustment.levels.black_input";
inline constexpr const char* kLayerMetadataAdjustmentLevelsWhiteInput = "photoslop.adjustment.levels.white_input";
inline constexpr const char* kLayerMetadataAdjustmentLevelsGammaPercent = "photoslop.adjustment.levels.gamma_percent";
inline constexpr const char* kLayerMetadataAdjustmentCurvesShadowOutput = "photoslop.adjustment.curves.shadow_output";
inline constexpr const char* kLayerMetadataAdjustmentCurvesMidtoneOutput = "photoslop.adjustment.curves.midtone_output";
inline constexpr const char* kLayerMetadataAdjustmentCurvesHighlightOutput =
    "photoslop.adjustment.curves.highlight_output";
inline constexpr const char* kLayerMetadataAdjustmentHueSaturationHueShift =
    "photoslop.adjustment.hue_saturation.hue_shift";
inline constexpr const char* kLayerMetadataAdjustmentHueSaturationSaturationDelta =
    "photoslop.adjustment.hue_saturation.saturation_delta";
inline constexpr const char* kLayerMetadataAdjustmentHueSaturationLightnessDelta =
    "photoslop.adjustment.hue_saturation.lightness_delta";
inline constexpr const char* kLayerMetadataAdjustmentColorBalanceCyanRed =
    "photoslop.adjustment.color_balance.cyan_red";
inline constexpr const char* kLayerMetadataAdjustmentColorBalanceMagentaGreen =
    "photoslop.adjustment.color_balance.magenta_green";
inline constexpr const char* kLayerMetadataAdjustmentColorBalanceYellowBlue =
    "photoslop.adjustment.color_balance.yellow_blue";

enum class AdjustmentKind {
  Levels,
  Curves,
  HueSaturation,
  ColorBalance
};

struct LevelsAdjustment {
  int black_input{0};
  int white_input{255};
  int gamma_percent{100};
};

struct CurvesAdjustment {
  int shadow_output{0};
  int midtone_output{128};
  int highlight_output{255};
};

struct HueSaturationAdjustment {
  int hue_shift{0};
  int saturation_delta{0};
  int lightness_delta{0};
};

struct ColorBalanceAdjustment {
  int cyan_red{0};
  int magenta_green{0};
  int yellow_blue{0};
};

struct AdjustmentSettings {
  AdjustmentKind kind{AdjustmentKind::Levels};
  LevelsAdjustment levels{};
  CurvesAdjustment curves{};
  HueSaturationAdjustment hue_saturation{};
  ColorBalanceAdjustment color_balance{};
};

[[nodiscard]] bool layer_is_adjustment(const Layer& layer);
[[nodiscard]] std::string adjustment_kind_key(AdjustmentKind kind);
[[nodiscard]] std::string adjustment_display_name(AdjustmentKind kind);
[[nodiscard]] std::optional<AdjustmentKind> adjustment_kind_from_key(std::string_view key);
[[nodiscard]] std::optional<AdjustmentSettings> adjustment_settings_from_layer(const Layer& layer);
void configure_adjustment_layer(Layer& layer, const AdjustmentSettings& settings);
[[nodiscard]] RgbColor apply_adjustment_to_color(RgbColor color, const AdjustmentSettings& settings);
void apply_adjustment_to_pixels(PixelBuffer& pixels, const AdjustmentSettings& settings);
[[nodiscard]] bool adjustment_has_effect(const AdjustmentSettings& settings);

}  // namespace photoslop
