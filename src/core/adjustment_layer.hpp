#pragma once

#include "core/ink_space.hpp"
#include "core/layer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
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
// Adjustments read from a CMYK document (see InkSpace): the space's id, and the black
// ink's Levels record and curve, which have no RGB counterpart.
inline constexpr const char* kLayerMetadataAdjustmentInkSpace = "patchy.adjustment.ink_space";
inline constexpr const char* kLayerMetadataAdjustmentCurvesBlackPoints = "patchy.adjustment.curves.black.points";
inline constexpr const char* kLayerMetadataAdjustmentLevelsBlackInkBlackInput =
    "patchy.adjustment.levels.black_ink.black_input";
inline constexpr const char* kLayerMetadataAdjustmentLevelsBlackInkWhiteInput =
    "patchy.adjustment.levels.black_ink.white_input";
inline constexpr const char* kLayerMetadataAdjustmentLevelsBlackInkGammaPercent =
    "patchy.adjustment.levels.black_ink.gamma_percent";
inline constexpr const char* kLayerMetadataAdjustmentLevelsBlackInkBlackOutput =
    "patchy.adjustment.levels.black_ink.black_output";
inline constexpr const char* kLayerMetadataAdjustmentLevelsBlackInkWhiteOutput =
    "patchy.adjustment.levels.black_ink.white_output";
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
// Per-hue-range bands. One key per band index 0..5 (reds, yellows, greens,
// cyans, blues, magentas) holding "r0;r1;r2;r3;hue;saturation;lightness".
inline constexpr const char* kLayerMetadataAdjustmentHueSaturationBandPrefix =
    "patchy.adjustment.hue_saturation.band.";
inline constexpr const char* kLayerMetadataAdjustmentColorBalanceCyanRed =
    "patchy.adjustment.color_balance.cyan_red";
inline constexpr const char* kLayerMetadataAdjustmentColorBalanceMagentaGreen =
    "patchy.adjustment.color_balance.magenta_green";
inline constexpr const char* kLayerMetadataAdjustmentColorBalanceYellowBlue =
    "patchy.adjustment.color_balance.yellow_blue";
// The three keys above hold the midtones triple (they predate tone ranges). The shadows and
// highlights triples are "cyan_red;magenta_green;yellow_blue"; absent keys read as zero.
inline constexpr const char* kLayerMetadataAdjustmentColorBalanceShadows =
    "patchy.adjustment.color_balance.shadows";
inline constexpr const char* kLayerMetadataAdjustmentColorBalanceHighlights =
    "patchy.adjustment.color_balance.highlights";
inline constexpr const char* kLayerMetadataAdjustmentColorBalancePreserveLuminosity =
    "patchy.adjustment.color_balance.preserve_luminosity";
// A Photoshop adjustment kind Patchy cannot apply yet imports as an empty placeholder layer
// carrying its kind token here; the native block is kept and written back unchanged (#250).
inline constexpr const char* kLayerMetadataAdjustmentPlaceholderKind = "patchy.adjustment.placeholder_kind";
inline constexpr const char* kLayerMetadataAdjustmentPosterizeLevels = "patchy.adjustment.posterize.levels";
// Channel Mixer rows as "red;green;blue;constant", plus the monochrome flag.
inline constexpr const char* kLayerMetadataAdjustmentChannelMixerRed = "patchy.adjustment.channel_mixer.red";
inline constexpr const char* kLayerMetadataAdjustmentChannelMixerGreen = "patchy.adjustment.channel_mixer.green";
inline constexpr const char* kLayerMetadataAdjustmentChannelMixerBlue = "patchy.adjustment.channel_mixer.blue";
inline constexpr const char* kLayerMetadataAdjustmentChannelMixerGray = "patchy.adjustment.channel_mixer.gray";
inline constexpr const char* kLayerMetadataAdjustmentChannelMixerMonochrome =
    "patchy.adjustment.channel_mixer.monochrome";
// Photo Filter color as "L;a;b" in hundredths (CIELAB D50, as Photoshop stores it).
inline constexpr const char* kLayerMetadataAdjustmentPhotoFilterLab = "patchy.adjustment.photo_filter.lab";
inline constexpr const char* kLayerMetadataAdjustmentPhotoFilterDensity = "patchy.adjustment.photo_filter.density";
inline constexpr const char* kLayerMetadataAdjustmentPhotoFilterPreserveLuminosity =
    "patchy.adjustment.photo_filter.preserve_luminosity";
// Gradient Map gradient: "method;reverse;dither;smoothness|loc,mid,r,g,b;...|loc,mid,opacity;..."
// with locations, midpoints and opacities in Photoshop's integer units (0..4096, 0..100, 0..255).
inline constexpr const char* kLayerMetadataAdjustmentGradientMapGradient = "patchy.adjustment.gradient_map.gradient";
inline constexpr const char* kLayerMetadataAdjustmentGradientMapName = "patchy.adjustment.gradient_map.name";
inline constexpr const char* kLayerMetadataAdjustmentThresholdLevel = "patchy.adjustment.threshold.level";
inline constexpr const char* kLayerMetadataAdjustmentExposureValue = "patchy.adjustment.exposure.value";
inline constexpr const char* kLayerMetadataAdjustmentExposureOffset = "patchy.adjustment.exposure.offset";
inline constexpr const char* kLayerMetadataAdjustmentExposureGamma = "patchy.adjustment.exposure.gamma";
inline constexpr const char* kLayerMetadataAdjustmentBrightnessContrastBrightness =
    "patchy.adjustment.brightness_contrast.brightness";
inline constexpr const char* kLayerMetadataAdjustmentBrightnessContrastContrast =
    "patchy.adjustment.brightness_contrast.contrast";
// Absent on pre-July-2026 documents, which were always legacy-mode; the reader
// therefore defaults to 1 (legacy) so old files keep their render.
inline constexpr const char* kLayerMetadataAdjustmentBrightnessContrastUseLegacy =
    "patchy.adjustment.brightness_contrast.use_legacy";

enum class AdjustmentKind {
  Levels,
  Curves,
  HueSaturation,
  ColorBalance,
  Invert,
  Posterize,
  Threshold,
  BrightnessContrast,
  Exposure,
  ChannelMixer,
  PhotoFilter,
  GradientMap
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

  friend bool operator==(const LevelsRecord&, const LevelsRecord&) = default;
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
  // The fifth record of a CMYK document's Levels (the black ink). Only an adjustment
  // with an ink space reads it; there red, green and blue hold cyan, magenta and yellow.
  LevelsRecord black_ink{};

  friend bool operator==(const LevelsAdjustment&, const LevelsAdjustment&) = default;
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
  // The fifth curve of a CMYK document's Curves (the black ink); see LevelsAdjustment.
  CurveControlPoints black_ink{{0, 0}, {255, 255}};

  friend bool operator==(const CurvesAdjustment&, const CurvesAdjustment&) = default;
};

struct CurvesEyedropperSamples {
  std::optional<RgbColor> black;
  std::optional<RgbColor> gray;
  std::optional<RgbColor> white;
};

// One of Photoshop's six per-hue-range bands. The four stops are hue degrees in
// wheel order and may wrap past 360 (the reds default is 315/345/15/45); the
// effect ramps in between the outer and inner start, holds between the inner
// stops, and ramps out between the inner and outer end.
struct HueSaturationBand {
  int outer_start{0};
  int inner_start{0};
  int inner_end{0};
  int outer_end{0};
  int hue_shift{0};          // -180..180
  int saturation_delta{0};   // -100..100
  int lightness_delta{0};    // -100..100

  [[nodiscard]] bool has_effect() const {
    return hue_shift != 0 || saturation_delta != 0 || lightness_delta != 0;
  }
  friend bool operator==(const HueSaturationBand&, const HueSaturationBand&) = default;
};

// Photoshop's fresh-layer hextants, the same ranges `kPhotoshopHueSaturationDefaultTail` writes.
inline constexpr std::array<std::array<int, 4>, 6> kHueSaturationDefaultBandRanges{
    {{315, 345, 15, 45},
     {15, 45, 75, 105},
     {75, 105, 135, 165},
     {135, 165, 195, 225},
     {195, 225, 255, 285},
     {255, 285, 315, 345}}};

[[nodiscard]] std::array<HueSaturationBand, 6> default_hue_saturation_bands();

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
  // Per-hue-range bands, in Photoshop's order: reds, yellows, greens, cyans,
  // blues, magentas. Ignored while colorize is on.
  std::array<HueSaturationBand, 6> bands{default_hue_saturation_bands()};

  [[nodiscard]] bool any_band_has_effect() const {
    return std::any_of(bands.begin(), bands.end(), [](const auto& band) { return band.has_effect(); });
  }
};

struct ColorBalanceTone {
  int cyan_red{0};
  int magenta_green{0};
  int yellow_blue{0};

  [[nodiscard]] bool has_effect() const noexcept {
    return cyan_red != 0 || magenta_green != 0 || yellow_blue != 0;
  }
  friend bool operator==(const ColorBalanceTone&, const ColorBalanceTone&) = default;
};

enum class ColorBalanceToneRange : std::uint8_t { Shadows = 0, Midtones = 1, Highlights = 2 };

// Photoshop's Color Balance: one -100..100 triple per tone range plus Preserve Luminosity.
// The top-level triple is the midtones (its names and metadata keys predate tone ranges).
struct ColorBalanceAdjustment {
  int cyan_red{0};
  int magenta_green{0};
  int yellow_blue{0};
  ColorBalanceTone shadows{};
  ColorBalanceTone highlights{};
  bool preserve_luminosity{false};

  [[nodiscard]] ColorBalanceTone tone(ColorBalanceToneRange range) const noexcept;
  void set_tone(ColorBalanceToneRange range, ColorBalanceTone value) noexcept;
  [[nodiscard]] bool has_effect() const noexcept;
  friend bool operator==(const ColorBalanceAdjustment&, const ColorBalanceAdjustment&) = default;
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

// Photoshop's Exposure adjustment ('expA'), stored at the precision of Photoshop's own
// fields: exposure in hundredths of a stop (-20.00..20.00), offset in ten-thousandths
// (-0.5000..0.5000), gamma correction in hundredths (0.01..9.99).
inline constexpr int kExposureValueRange = 2000;
inline constexpr int kExposureOffsetRange = 5000;
inline constexpr int kExposureGammaMin = 1;
inline constexpr int kExposureGammaMax = 999;
struct ExposureAdjustment {
  int exposure_hundredths{0};
  int offset_ten_thousandths{0};
  int gamma_hundredths{100};
};
[[nodiscard]] ExposureAdjustment clamp_exposure(ExposureAdjustment settings);
// One channel through Photoshop's Exposure: linearize with gamma 2.2, scale by
// 2^exposure, add the offset, apply 1/gamma, encode again. Within 1/255 of Photoshop's
// render of psd-tools' exposure_rgb.psd on all four of its setting triples.
[[nodiscard]] std::uint8_t exposure_channel_value(std::uint8_t value, ExposureAdjustment settings);

// Photoshop's Channel Mixer ('mixr'): each output channel is a percentage mix of the
// red, green and blue inputs (-200..200) plus a constant (-200..200, percent of full
// scale). Monochrome sends the gray row to all three channels.
struct ChannelMixerRow {
  int red{0};
  int green{0};
  int blue{0};
  int constant{0};
  friend bool operator==(const ChannelMixerRow&, const ChannelMixerRow&) = default;
};
struct ChannelMixerAdjustment {
  bool monochrome{false};
  ChannelMixerRow red{100, 0, 0, 0};
  ChannelMixerRow green{0, 100, 0, 0};
  ChannelMixerRow blue{0, 0, 100, 0};
  ChannelMixerRow gray{40, 40, 20, 0};
  [[nodiscard]] bool has_effect() const noexcept;
  friend bool operator==(const ChannelMixerAdjustment&, const ChannelMixerAdjustment&) = default;
};
[[nodiscard]] ChannelMixerRow clamp_channel_mixer_row(ChannelMixerRow row) noexcept;
// A mix percentage on Photoshop's 1/1024 weight scale, truncated toward zero.
[[nodiscard]] int channel_mixer_weight(int percent) noexcept;
// One output channel of the mix on the 0..255 scale, unrounded and unclamped: the
// quantized weights times the inputs plus 255 times the quantized constant, over 1024.
[[nodiscard]] double channel_mixer_transfer(const ChannelMixerRow& row, double red, double green, double blue);

// Photoshop's Photo Filter ('phfl'): the filter color as Photoshop stores it (CIELAB D50 in
// hundredths: L 0..10000, a and b -12800..12700), a density of 1..100 % and Preserve
// Luminosity. The defaults are Photoshop's: its warming color, sRGB (236, 138, 0), at 25 %.
struct PhotoFilterAdjustment {
  int lab_l{6719};
  int lab_a{3314};
  int lab_b{7279};
  int density{25};
  bool preserve_luminosity{true};
  friend bool operator==(const PhotoFilterAdjustment&, const PhotoFilterAdjustment&) = default;
};
[[nodiscard]] PhotoFilterAdjustment clamp_photo_filter(PhotoFilterAdjustment settings) noexcept;
// The filter color in sRGB bytes, and the settings with the filter color set from sRGB.
[[nodiscard]] RgbColor photo_filter_color(const PhotoFilterAdjustment& settings);
[[nodiscard]] PhotoFilterAdjustment photo_filter_with_color(PhotoFilterAdjustment settings, RgbColor color);
// One color through the filter on the 0..255 scale (docs/adjustment-kinds.md, "Photo
// Filter"): each XYZ (D50) component is scaled toward the filter's by the density, and
// Preserve Luminosity puts the source luma back. byte_stages rounds the intermediate values
// as Photoshop's 8-bit path does; the deep path passes false. Unclamped final rounding.
[[nodiscard]] std::array<double, 3> photo_filter_transfer(const PhotoFilterAdjustment& settings, double red,
                                                          double green, double blue, bool byte_stages);

// Photoshop's Gradient Map ('grdm'): each pixel's luma, round((30R + 59G + 11B) / 100),
// picks a color off a solid gradient (opacity stops are kept but unused, as in Photoshop).
// Reverse and the Classic/Perceptual/Linear method apply; Dither is kept for the file only.
struct GradientMapAdjustment {
  LayerStyleGradient gradient{default_gradient_map_gradient()};
  friend bool operator==(const GradientMapAdjustment&, const GradientMapAdjustment&) = default;
  // Photoshop's new Gradient Map: black to white, Classic, 100 % smoothness.
  [[nodiscard]] static LayerStyleGradient default_gradient_map_gradient();
};
// The mapped color for an 8-bit pixel, and the unrounded mapping (0..255 scale) for the deep path.
[[nodiscard]] RgbColor gradient_map_color(const GradientMapAdjustment& settings, RgbColor color);
[[nodiscard]] std::array<double, 3> gradient_map_transfer(const GradientMapAdjustment& settings, double red,
                                                          double green, double blue);

// Both Photoshop algorithms are modeled. Modern mode (Photoshop's default,
// use_legacy false) takes brightness -150..150 and contrast -50..100; legacy
// mode takes -100..100 for both. Old Patchy documents and 'brit'-only PSDs
// load as legacy so their render is unchanged.
inline constexpr int kBrightnessContrastLegacyRange = 100;
inline constexpr int kModernBrightnessRange = 150;
inline constexpr int kModernContrastMin = -50;
inline constexpr int kModernContrastMax = 100;
struct BrightnessContrastAdjustment {
  int brightness{0};
  int contrast{0};
  bool use_legacy{false};
};
// Clamps both sliders into the active mode's Photoshop range.
[[nodiscard]] BrightnessContrastAdjustment clamp_brightness_contrast(BrightnessContrastAdjustment settings);

struct AdjustmentSettings {
  AdjustmentKind kind{AdjustmentKind::Levels};
  LevelsAdjustment levels{};
  CurvesAdjustment curves{};
  HueSaturationAdjustment hue_saturation{};
  ColorBalanceAdjustment color_balance{};
  PosterizeAdjustment posterize{};
  ThresholdAdjustment threshold{};
  BrightnessContrastAdjustment brightness_contrast{};
  ExposureAdjustment exposure{};
  ChannelMixerAdjustment channel_mixer{};
  PhotoFilterAdjustment photo_filter{};
  GradientMapAdjustment gradient_map{};
  // Set for an adjustment layer that came from a CMYK document whose profile could be
  // read: the channel-wise kinds (Levels, Curves, Invert, Posterize, Brightness/Contrast,
  // Exposure) then run on the four inks instead of on RGB. See core/ink_space.hpp.
  std::shared_ptr<const InkSpace> ink_space;
};
// True when `settings` runs in its ink space (it has one and its kind is channel-wise).
[[nodiscard]] bool adjustment_runs_in_ink_space(const AdjustmentSettings& settings) noexcept;

// Levels record math shared by the UI dialogs and the PSD lvls codec: the
// single source of truth for the clamp ranges (black_input 0..254,
// white_input black_input+1..255, gamma_percent 10..999, black_output 0..255,
// white_output black_output..255). NOTE: the per-channel transfer FORMULA is
// deliberately not shared: core's levels_channel rounds through a float
// (clamp_byte) while the UI preview's map_levels_value lrounds the double,
// and the two differ by 1/255 on real inputs (e.g. value 4, record
// {0,45,121%,0,255} -> 35 vs 34).
// The gamma stage every Levels transfer shares: normalized 0..1 in and out, gamma > 0
// (Photoshop's power curve with its near-black toe; see the definition).
[[nodiscard]] double levels_gamma_curve(double normalized, double gamma);
// Photoshop's Color Balance is one Levels record per channel, built from all three tone
// triples (calibrated against Photoshop 2026; docs/adjustments-calibration.md, "Color
// Balance"). It stays per channel with Preserve Luminosity on, so it always has a LUT.
struct ColorBalanceChannelLevels {
  int black_input{0};
  int white_input{255};
  int gamma_exponent{0};  // gamma = 2^(exponent/100), stored to two decimals as Levels does
};
[[nodiscard]] std::array<ColorBalanceChannelLevels, 3> color_balance_channel_levels(
    const ColorBalanceAdjustment& settings);
// byte_points rounds the black/white-point stage to whole levels before the gamma, as
// Photoshop's 8-bit pipeline does; the continuous deep path passes false.
[[nodiscard]] double color_balance_transfer(double value, ColorBalanceChannelLevels levels, bool byte_points);
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
// Photoshop Brightness/Contrast, both algorithms calibrated against PS 2026
// ramp captures (July 2026). DELIBERATELY different from the destructive
// patchy.filters.brightness_contrast formula (which is byte-pinned and keeps
// its historical linear (100+c)/100 slope for positive contrast): the
// adjustment layer round-trips as a native Photoshop record, so it must
// render Photoshop's math, like the Levels dual-formula precedent. The
// modern (use_legacy false) model is the full closed form recovered from
// 300 16-bit ramp captures; see docs/adjustments-calibration.md "Modern Brightness/Contrast".
[[nodiscard]] std::uint8_t brightness_contrast_channel_value(std::uint8_t value, int brightness, int contrast,
                                                             bool use_legacy);

// Continuous forms of the transfers above for 16 and 32-bit documents
// (core/adjustment_deep.hpp): the same formulas on the 0..255 scale without the final
// rounding. The 8-bit functions above do not go through these.
[[nodiscard]] double levels_record_transfer(double value, LevelsRecord record);
[[nodiscard]] double brightness_contrast_transfer(double value, int brightness, int contrast, bool use_legacy);
[[nodiscard]] double exposure_transfer(double value, ExposureAdjustment settings);
// Photoshop's deep (16 and 32-bit) Hue/Saturation as a continuous map of 0..255
// values: apply_hue_saturation's 1530-step wheel and in-gamut chroma limit, with the
// deep rules Photoshop 2026 uses instead of its 8-bit ones (exact lightness percent,
// unrounded hue rotation, its own saturation multipliers; docs/high-bit-depth.md).
// With unbounded set there is no chroma limit and no 0..255 clamp, as in Photoshop's
// 32-bit Hue/Saturation (deep corpus adj-huesat-32). nullopt for Colorize, which has
// no continuous form here.
[[nodiscard]] std::optional<std::array<double, 3>> hue_saturation_transfer(std::array<double, 3> color,
                                                                           const HueSaturationAdjustment& settings,
                                                                           bool unbounded);

[[nodiscard]] bool layer_is_adjustment(const Layer& layer);
// The placeholder kind token ("vibrance", "black_white", "photo_filter", "channel_mixer",
// "color_lookup", "gradient_map", "selective_color") for a Photoshop adjustment block key
// Patchy keeps but cannot apply yet, or nullptr.
[[nodiscard]] const char* photoshop_placeholder_adjustment_kind(std::string_view block_key) noexcept;
// The layer's placeholder kind token, or empty when it is not an adjustment placeholder.
[[nodiscard]] std::string_view adjustment_placeholder_kind(const Layer& layer);
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
