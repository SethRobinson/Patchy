#include "core/smart_filter.hpp"

namespace patchy {

bool operator==(const AdjustmentSmartFilter& a, const AdjustmentSmartFilter& b) {
  const auto& left = a.settings;
  const auto& right = b.settings;
  if (left.kind != right.kind) {
    return false;
  }
  switch (left.kind) {
    case AdjustmentKind::Levels:
      return left.levels == right.levels;
    case AdjustmentKind::Curves:
      return left.curves == right.curves;
    case AdjustmentKind::HueSaturation: {
      const auto& l = left.hue_saturation;
      const auto& r = right.hue_saturation;
      return l.hue_shift == r.hue_shift && l.saturation_delta == r.saturation_delta &&
             l.lightness_delta == r.lightness_delta && l.colorize == r.colorize &&
             l.colorize_hue == r.colorize_hue && l.colorize_saturation == r.colorize_saturation &&
             l.colorize_lightness == r.colorize_lightness && l.bands == r.bands;
    }
    case AdjustmentKind::ColorBalance:
      return left.color_balance == right.color_balance;
    case AdjustmentKind::Invert:
      return true;
    case AdjustmentKind::Posterize:
      return left.posterize.levels == right.posterize.levels;
    case AdjustmentKind::Threshold:
      return left.threshold.level == right.threshold.level;
    case AdjustmentKind::BrightnessContrast:
      return left.brightness_contrast.brightness == right.brightness_contrast.brightness &&
             left.brightness_contrast.contrast == right.brightness_contrast.contrast &&
             left.brightness_contrast.use_legacy == right.brightness_contrast.use_legacy;
    case AdjustmentKind::Exposure:
      return left.exposure.exposure_hundredths == right.exposure.exposure_hundredths &&
             left.exposure.offset_ten_thousandths == right.exposure.offset_ten_thousandths &&
             left.exposure.gamma_hundredths == right.exposure.gamma_hundredths;
    case AdjustmentKind::ChannelMixer:
      return left.channel_mixer == right.channel_mixer;
    case AdjustmentKind::PhotoFilter:
      return left.photo_filter == right.photo_filter;
    case AdjustmentKind::GradientMap:
      return left.gradient_map == right.gradient_map;
    case AdjustmentKind::BlackWhite:
      return left.black_white == right.black_white;
    case AdjustmentKind::ColorLookup:
      return left.color_lookup == right.color_lookup;
    case AdjustmentKind::SelectiveColor:
      return left.selective_color == right.selective_color;
    case AdjustmentKind::Vibrance:
      return left.vibrance == right.vibrance;
  }
  return false;
}

}  // namespace patchy
