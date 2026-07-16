#pragma once

#include "core/layer.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace patchy {

inline constexpr const char* kLayerMetadataAdjustmentType = "patchy.adjustment.type";
inline constexpr const char* kLayerMetadataAdjustmentLevelsBlackInput = "patchy.adjustment.levels.black_input";
inline constexpr const char* kLayerMetadataAdjustmentLevelsWhiteInput = "patchy.adjustment.levels.white_input";
inline constexpr const char* kLayerMetadataAdjustmentLevelsGammaPercent = "patchy.adjustment.levels.gamma_percent";
inline constexpr const char* kLayerMetadataAdjustmentLevelsBlackOutput = "patchy.adjustment.levels.black_output";
inline constexpr const char* kLayerMetadataAdjustmentLevelsWhiteOutput = "patchy.adjustment.levels.white_output";
inline constexpr const char* kLayerMetadataAdjustmentLevelsChannel = "patchy.adjustment.levels.channel";
inline constexpr const char* kLayerMetadataAdjustmentLevelsRedBlackInput = "patchy.adjustment.levels.red.black_input";
inline constexpr const char* kLayerMetadataAdjustmentLevelsRedWhiteInput = "patchy.adjustment.levels.red.white_input";
inline constexpr const char* kLayerMetadataAdjustmentLevelsRedGammaPercent =
    "patchy.adjustment.levels.red.gamma_percent";
inline constexpr const char* kLayerMetadataAdjustmentLevelsRedBlackOutput =
    "patchy.adjustment.levels.red.black_output";
inline constexpr const char* kLayerMetadataAdjustmentLevelsRedWhiteOutput =
    "patchy.adjustment.levels.red.white_output";
inline constexpr const char* kLayerMetadataAdjustmentLevelsGreenBlackInput =
    "patchy.adjustment.levels.green.black_input";
inline constexpr const char* kLayerMetadataAdjustmentLevelsGreenWhiteInput =
    "patchy.adjustment.levels.green.white_input";
inline constexpr const char* kLayerMetadataAdjustmentLevelsGreenGammaPercent =
    "patchy.adjustment.levels.green.gamma_percent";
inline constexpr const char* kLayerMetadataAdjustmentLevelsGreenBlackOutput =
    "patchy.adjustment.levels.green.black_output";
inline constexpr const char* kLayerMetadataAdjustmentLevelsGreenWhiteOutput =
    "patchy.adjustment.levels.green.white_output";
inline constexpr const char* kLayerMetadataAdjustmentLevelsBlueBlackInput =
    "patchy.adjustment.levels.blue.black_input";
inline constexpr const char* kLayerMetadataAdjustmentLevelsBlueWhiteInput =
    "patchy.adjustment.levels.blue.white_input";
inline constexpr const char* kLayerMetadataAdjustmentLevelsBlueGammaPercent =
    "patchy.adjustment.levels.blue.gamma_percent";
inline constexpr const char* kLayerMetadataAdjustmentLevelsBlueBlackOutput =
    "patchy.adjustment.levels.blue.black_output";
inline constexpr const char* kLayerMetadataAdjustmentLevelsBlueWhiteOutput =
    "patchy.adjustment.levels.blue.white_output";
inline constexpr const char* kLayerMetadataAdjustmentCurvesShadowOutput = "patchy.adjustment.curves.shadow_output";
inline constexpr const char* kLayerMetadataAdjustmentCurvesMidtoneOutput = "patchy.adjustment.curves.midtone_output";
inline constexpr const char* kLayerMetadataAdjustmentCurvesHighlightOutput =
    "patchy.adjustment.curves.highlight_output";
inline constexpr const char* kLayerMetadataAdjustmentCurvesRgbPoints = "patchy.adjustment.curves.rgb.points";
inline constexpr const char* kLayerMetadataAdjustmentCurvesRedPoints = "patchy.adjustment.curves.red.points";
inline constexpr const char* kLayerMetadataAdjustmentCurvesGreenPoints = "patchy.adjustment.curves.green.points";
inline constexpr const char* kLayerMetadataAdjustmentCurvesBluePoints = "patchy.adjustment.curves.blue.points";
inline constexpr const char* kLayerMetadataAdjustmentHueSaturationHueShift =
    "patchy.adjustment.hue_saturation.hue_shift";
inline constexpr const char* kLayerMetadataAdjustmentHueSaturationSaturationDelta =
    "patchy.adjustment.hue_saturation.saturation_delta";
inline constexpr const char* kLayerMetadataAdjustmentHueSaturationLightnessDelta =
    "patchy.adjustment.hue_saturation.lightness_delta";
inline constexpr const char* kLayerMetadataAdjustmentHueSaturationColorize =
    "patchy.adjustment.hue_saturation.colorize";
inline constexpr const char* kLayerMetadataAdjustmentHueSaturationColorizeHue =
    "patchy.adjustment.hue_saturation.colorize_hue";
inline constexpr const char* kLayerMetadataAdjustmentHueSaturationColorizeSaturation =
    "patchy.adjustment.hue_saturation.colorize_saturation";
inline constexpr const char* kLayerMetadataAdjustmentHueSaturationColorizeLightness =
    "patchy.adjustment.hue_saturation.colorize_lightness";
inline constexpr const char* kLayerMetadataAdjustmentColorBalanceCyanRed =
    "patchy.adjustment.color_balance.cyan_red";
inline constexpr const char* kLayerMetadataAdjustmentColorBalanceMagentaGreen =
    "patchy.adjustment.color_balance.magenta_green";
inline constexpr const char* kLayerMetadataAdjustmentColorBalanceYellowBlue =
    "patchy.adjustment.color_balance.yellow_blue";
inline constexpr const char* kLayerMetadataAdjustmentPosterizeLevels = "patchy.adjustment.posterize.levels";
inline constexpr const char* kLayerMetadataAdjustmentThresholdLevel = "patchy.adjustment.threshold.level";
inline constexpr const char* kLayerMetadataAdjustmentBrightnessContrastBrightness =
    "patchy.adjustment.brightness_contrast.brightness";
inline constexpr const char* kLayerMetadataAdjustmentBrightnessContrastContrast =
    "patchy.adjustment.brightness_contrast.contrast";

enum class AdjustmentKind {
  Levels,
  Curves,
  HueSaturation,
  ColorBalance,
  Invert,
  Posterize,
  Threshold,
  BrightnessContrast
};

enum class LevelsChannel {
  Rgb,
  Red,
  Green,
  Blue
};

struct LevelsRecord {
  int black_input{0};
  int white_input{255};
  int gamma_percent{100};
  int black_output{0};
  int white_output{255};
};

struct LevelsAdjustment {
  int black_input{0};
  int white_input{255};
  int gamma_percent{100};
  int black_output{0};
  int white_output{255};
  LevelsChannel channel{LevelsChannel::Rgb};
  LevelsRecord red{};
  LevelsRecord green{};
  LevelsRecord blue{};
};

enum class CurvesChannel {
  Rgb,
  Red,
  Green,
  Blue
};

struct CurveControlPoint {
  int input{0};
  int output{0};

  friend bool operator==(const CurveControlPoint&, const CurveControlPoint&) = default;
};

using CurveControlPoints = std::vector<CurveControlPoint>;

struct CurvesAdjustment {
  CurveControlPoints rgb{{0, 0}, {255, 255}};
  CurveControlPoints red{{0, 0}, {255, 255}};
  CurveControlPoints green{{0, 0}, {255, 255}};
  CurveControlPoints blue{{0, 0}, {255, 255}};

  friend bool operator==(const CurvesAdjustment&, const CurvesAdjustment&) = default;
};

struct CurvesEyedropperSamples {
  std::optional<RgbColor> black;
  std::optional<RgbColor> gray;
  std::optional<RgbColor> white;
};

struct HueSaturationAdjustment {
  int hue_shift{0};
  int saturation_delta{0};
  int lightness_delta{0};
  // Photoshop's Colorize mode: recolors from per-pixel lightness using the
  // colorize triple below; the master sliders above are ignored while active.
  bool colorize{false};
  int colorize_hue{0};          // 0..360 (UI convention; PSD stores -180..180)
  int colorize_saturation{25};  // 0..100 (Photoshop's colorize default is 25)
  int colorize_lightness{0};    // -100..100
};

struct ColorBalanceAdjustment {
  int cyan_red{0};
  int magenta_green{0};
  int yellow_blue{0};
};

// Levels 2..255: wider than the destructive dialog's 2..16 so Photoshop
// 'post' files with any legal value round-trip.
struct PosterizeAdjustment {
  int levels{4};
};

// Level 1..255 (Photoshop's 'thrs' range; the destructive filter keeps its
// historical 0..255 clamp).
struct ThresholdAdjustment {
  int level{128};
};

// Photoshop legacy-mode ranges (-100..100 both). Values from modern-mode
// CgEd blocks (-150..150 / -50..100) clamp into this model on import.
struct BrightnessContrastAdjustment {
  int brightness{0};
  int contrast{0};
};

struct AdjustmentSettings {
  AdjustmentKind kind{AdjustmentKind::Levels};
  LevelsAdjustment levels{};
  CurvesAdjustment curves{};
  HueSaturationAdjustment hue_saturation{};
  ColorBalanceAdjustment color_balance{};
  PosterizeAdjustment posterize{};
  ThresholdAdjustment threshold{};
  BrightnessContrastAdjustment brightness_contrast{};
};

// Levels record math shared by the UI dialogs and the PSD lvls codec: the
// single source of truth for the clamp ranges (black_input 0..254,
// white_input black_input+1..255, gamma_percent 10..999, black_output 0..255,
// white_output black_output..255). NOTE: the per-channel transfer FORMULA is
// deliberately not shared: core's levels_channel rounds through a float
// (clamp_byte) while the UI preview's map_levels_value lrounds the double,
// and the two differ by 1/255 on real inputs (e.g. value 4, record
// {0,45,121%,0,255} -> 35 vs 34).
[[nodiscard]] LevelsRecord clamp_levels_record(LevelsRecord record);
[[nodiscard]] LevelsRecord levels_master_record(LevelsAdjustment settings);
void set_levels_master_record(LevelsAdjustment& settings, LevelsRecord record);
[[nodiscard]] LevelsRecord levels_record_for_channel(const LevelsAdjustment& settings, LevelsChannel channel);
void set_levels_record_for_channel(LevelsAdjustment& settings, LevelsChannel channel, LevelsRecord record);

// Curves always carry two to nineteen points per channel, sorted by input.
// Duplicate inputs are resolved in favor of the last supplied point. Inputs
// outside the first/last control point clamp to that endpoint's output.
[[nodiscard]] CurveControlPoints normalized_curve_control_points(CurveControlPoints points);
[[nodiscard]] const CurveControlPoints& curve_points_for_channel(const CurvesAdjustment& curves,
                                                                 CurvesChannel channel) noexcept;
void set_curve_points_for_channel(CurvesAdjustment& curves, CurvesChannel channel, CurveControlPoints points);
[[nodiscard]] CurvesAdjustment curves_adjustment_from_legacy_outputs(int shadow_output, int midtone_output,
                                                                     int highlight_output);
// Rebuilds the component curves from sampled black/neutral/white points. Like
// Photoshop's Curves eyedroppers, this replaces prior hand-drawn geometry
// instead of layering an ambiguous point onto it.
[[nodiscard]] CurvesAdjustment curves_adjustment_from_eyedropper_samples(
    const CurvesEyedropperSamples& samples);

// The single shared formulas between the destructive catalog filters
// (filter_engine.cpp delegates here) and the adjustment layers: rounding is
// lround-based to match the historical filter_clamp_byte semantics, and
// threshold operates on the (30r + 59g + 11b) / 100 integer luminance.
[[nodiscard]] std::uint8_t posterize_channel_value(std::uint8_t value, int levels);
[[nodiscard]] int threshold_luminance(std::uint8_t red, std::uint8_t green, std::uint8_t blue);
// Photoshop legacy-mode Brightness/Contrast, calibrated against PS 2026 ramp
// captures (July 2026). DELIBERATELY different from the destructive
// patchy.filters.brightness_contrast formula (which is byte-pinned and keeps
// its historical linear (100+c)/100 slope for positive contrast): the
// adjustment layer round-trips as a native Photoshop legacy 'brit' record, so
// it must render Photoshop's math, like the Levels dual-formula precedent.
[[nodiscard]] std::uint8_t brightness_contrast_channel_value(std::uint8_t value, int brightness, int contrast);

[[nodiscard]] bool layer_is_adjustment(const Layer& layer);
[[nodiscard]] std::string adjustment_kind_key(AdjustmentKind kind);
[[nodiscard]] std::string adjustment_display_name(AdjustmentKind kind);
[[nodiscard]] std::optional<AdjustmentKind> adjustment_kind_from_key(std::string_view key);
[[nodiscard]] std::optional<AdjustmentSettings> adjustment_settings_from_layer(const Layer& layer);
void configure_adjustment_layer(Layer& layer, const AdjustmentSettings& settings);
[[nodiscard]] RgbColor apply_adjustment_to_color(RgbColor color, const AdjustmentSettings& settings);
void apply_adjustment_to_pixels(PixelBuffer& pixels, const AdjustmentSettings& settings);
[[nodiscard]] bool adjustment_has_effect(const AdjustmentSettings& settings);

// Exact per-channel 256-entry lookup for channel-separable adjustments
// (Levels, Curves, Color Balance): lut.red[v] equals the per-pixel math's red
// output for any pixel whose red input is v, so the LUT path is bit-identical
// at a fraction of the cost. nullopt for Hue/Saturation, whose channels mix
// through HSL.
struct AdjustmentLut {
  std::array<std::uint8_t, 256> red{};
  std::array<std::uint8_t, 256> green{};
  std::array<std::uint8_t, 256> blue{};
};
[[nodiscard]] std::array<std::uint8_t, 256> build_curve_lut(const CurveControlPoints& points);
[[nodiscard]] AdjustmentLut build_curves_lut(const CurvesAdjustment& curves);
[[nodiscard]] std::optional<AdjustmentLut> build_adjustment_lut(const AdjustmentSettings& settings);

}  // namespace patchy
