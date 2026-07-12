#include "core/gradient_presets.hpp"

#include <algorithm>
#include <initializer_list>
#include <vector>

namespace patchy {
namespace {

RgbColor rgb(unsigned value) {
  return RgbColor{static_cast<std::uint8_t>((value >> 16U) & 0xffU),
                  static_cast<std::uint8_t>((value >> 8U) & 0xffU),
                  static_cast<std::uint8_t>(value & 0xffU)};
}

GradientColorStop
stop(float location, unsigned color,
     GradientColorStop::Kind kind = GradientColorStop::Kind::User) {
  return GradientColorStop{location, rgb(color), 0.5F, kind};
}

GradientDefinition solid(std::initializer_list<GradientColorStop> colors,
                         std::initializer_list<GradientAlphaStop> alpha = {}) {
  GradientDefinition result;
  result.color_stops.assign(colors.begin(), colors.end());
  result.alpha_stops.assign(alpha.begin(), alpha.end());
  if (result.alpha_stops.empty())
    result.alpha_stops = {{0.0F, 1.0F}, {1.0F, 1.0F}};
  return result;
}

const std::vector<BuiltinGradientPreset> &presets() {
  static const std::vector<BuiltinGradientPreset> result = {
      {"6b31e700-0001-4f15-9d31-200000000001", "Foreground to Background",
       "Essentials", 1,
       solid({stop(0.0F, 0x000000, GradientColorStop::Kind::Foreground),
              stop(1.0F, 0xffffff, GradientColorStop::Kind::Background)})},
      {"6b31e700-0002-4f15-9d31-200000000002", "Foreground to Transparent",
       "Essentials", 1,
       solid({stop(0.0F, 0x000000, GradientColorStop::Kind::Foreground),
              stop(1.0F, 0x000000, GradientColorStop::Kind::Foreground)},
             {{0.0F, 1.0F}, {1.0F, 0.0F}})},
      {"6b31e700-0003-4f15-9d31-200000000003", "Black to White", "Essentials",
       1, solid({stop(0.0F, 0x000000), stop(1.0F, 0xffffff)})},
      {"6b31e700-0004-4f15-9d31-200000000004", "White to Transparent",
       "Essentials", 1,
       solid({stop(0.0F, 0xffffff), stop(1.0F, 0xffffff)},
             {{0.0F, 1.0F}, {1.0F, 0.0F}})},
      {"6b31e700-0005-4f15-9d31-200000000005", "Neutral Shine", "Essentials", 1,
       solid({stop(0.0F, 0x20242b), stop(0.48F, 0xf8faff),
              stop(0.55F, 0x6b7280), stop(1.0F, 0x111318)})},

      {"6b31e700-0006-4f15-9d31-200000000006", "Teal Shadows, Warm Highlights",
       "Photo Toning", 1,
       solid(
           {stop(0.0F, 0x0e3a46), stop(0.5F, 0x756b62), stop(1.0F, 0xf2b56b)})},
      {"6b31e700-0007-4f15-9d31-200000000007", "Blue Shadows, Gold Highlights",
       "Photo Toning", 1,
       solid({stop(0.0F, 0x172a5c), stop(0.52F, 0x746b70),
              stop(1.0F, 0xf5d06f)})},
      {"6b31e700-0008-4f15-9d31-200000000008", "Sepia Wash", "Photo Toning", 1,
       solid({stop(0.0F, 0x2b1b12), stop(0.48F, 0x8c5a32),
              stop(1.0F, 0xf1dfc0)})},
      {"6b31e700-0009-4f15-9d31-200000000009", "Cyanotype", "Photo Toning", 1,
       solid({stop(0.0F, 0x081c33), stop(0.55F, 0x1f6b8c),
              stop(1.0F, 0xd9f3f2)})},
      {"6b31e700-0010-4f15-9d31-200000000010", "Faded Film", "Photo Toning", 1,
       solid({stop(0.0F, 0x1b2636), stop(0.45F, 0x7c7169),
              stop(0.78F, 0xd8b49a), stop(1.0F, 0xfff1d3)})},

      {"6b31e700-0011-4f15-9d31-200000000011", "Golden Hour",
       "Light & Atmosphere", 1,
       solid({stop(0.0F, 0x5a2148), stop(0.42F, 0xd76738),
              stop(0.72F, 0xffc76b), stop(1.0F, 0xfff0bd)})},
      {"6b31e700-0012-4f15-9d31-200000000012", "Sunset Sky",
       "Light & Atmosphere", 1,
       solid({stop(0.0F, 0x142a5e), stop(0.4F, 0x6c3f87), stop(0.72F, 0xe76655),
              stop(1.0F, 0xffc66d)})},
      {"6b31e700-0013-4f15-9d31-200000000013", "Dawn Mist",
       "Light & Atmosphere", 1,
       solid({stop(0.0F, 0x576b8c), stop(0.4F, 0xb7a6c9), stop(0.72F, 0xf5c6b6),
              stop(1.0F, 0xeef5f2)})},
      {"6b31e700-0014-4f15-9d31-200000000014", "Night Sky",
       "Light & Atmosphere", 1,
       solid({stop(0.0F, 0x050816), stop(0.55F, 0x142a5e),
              stop(1.0F, 0x4b3f8f)})},
      {"6b31e700-0015-4f15-9d31-200000000015", "Firelight",
       "Light & Atmosphere", 1,
       solid({stop(0.0F, 0x3a0604), stop(0.38F, 0xb32612),
              stop(0.68F, 0xf2761d), stop(1.0F, 0xffe083)})},

      {"6b31e700-0016-4f15-9d31-200000000016", "Comic Pop", "Illustration", 1,
       solid({stop(0.0F, 0x00c8ff), stop(0.35F, 0x6d4cff),
              stop(0.68F, 0xff3d9a), stop(1.0F, 0xffd84a)})},
      {"6b31e700-0017-4f15-9d31-200000000017", "Neon Spectrum", "Illustration",
       1,
       solid({stop(0.0F, 0x32105c), stop(0.25F, 0x006cff), stop(0.5F, 0x00f0d0),
              stop(0.72F, 0xb7ff3c), stop(1.0F, 0xff3cac)})},
      {"6b31e700-0018-4f15-9d31-200000000018", "Metal Shine", "Illustration", 1,
       solid({stop(0.0F, 0x111318), stop(0.28F, 0x6e7681),
              stop(0.48F, 0xf7faff), stop(0.6F, 0x7a838e),
              stop(1.0F, 0x181b20)})},
      {"6b31e700-0019-4f15-9d31-200000000019", "Cel Shade", "Illustration", 1,
       solid({stop(0.0F, 0x1d2a44), stop(0.49F, 0x1d2a44), stop(0.5F, 0x5c86d6),
              stop(0.74F, 0x5c86d6), stop(0.75F, 0xd8e7ff),
              stop(1.0F, 0xd8e7ff)})},
      {"6b31e700-0020-4f15-9d31-200000000020", "Jewel Tone", "Illustration", 1,
       solid({stop(0.0F, 0x0b3d2e), stop(0.33F, 0x00a37a),
              stop(0.66F, 0x1d70c9), stop(1.0F, 0x6e36b8)})},
  };
  return result;
}

} // namespace

std::span<const BuiltinGradientPreset> builtin_gradient_presets() {
  return presets();
}

const BuiltinGradientPreset *find_builtin_gradient_preset(std::string_view id) {
  const auto &values = presets();
  const auto found =
      std::find_if(values.begin(), values.end(),
                   [id](const auto &value) { return id == value.id; });
  return found == values.end() ? nullptr : &*found;
}

} // namespace patchy
