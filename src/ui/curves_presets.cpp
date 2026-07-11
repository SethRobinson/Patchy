#include "ui/curves_presets.hpp"

#include <QObject>

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace patchy::ui {

namespace {

CurveControlPoints points(std::initializer_list<CurveControlPoint> values) {
  return CurveControlPoints(values);
}

CurvesAdjustment preset_curves(CurveControlPoints rgb,
                               CurveControlPoints red = {{0, 0}, {255, 255}},
                               CurveControlPoints green = {{0, 0}, {255, 255}},
                               CurveControlPoints blue = {{0, 0}, {255, 255}}) {
  CurvesAdjustment adjustment;
  adjustment.rgb = std::move(rgb);
  adjustment.red = std::move(red);
  adjustment.green = std::move(green);
  adjustment.blue = std::move(blue);
  return adjustment;
}

int thumbnail_source_channel(int x, int y, int width, int height, int channel) {
  const auto tone = width <= 3 ? 128 : ((x - 1) * 255) / (width - 3);
  const auto stripe = std::clamp(((y - 1) * 3) / std::max(1, height - 2), 0, 2);
  const auto tile_offset = (((x - 1) / 9 + (y - 1) / 8) & 1) == 0 ? -11 : 11;
  int value = tone;
  if (stripe == 0) {
    // Blue sky-like band.
    static constexpr std::array<int, 3> kBase{18, 62, 118};
    static constexpr std::array<int, 3> kScale{3, 3, 2};
    value = kBase[static_cast<std::size_t>(channel)] +
            tone * kScale[static_cast<std::size_t>(channel)] / 4;
  } else if (stripe == 1) {
    // Warm skin/earth-like band.
    static constexpr std::array<int, 3> kBase{48, 22, 12};
    static constexpr std::array<int, 3> kScale{4, 3, 2};
    value = kBase[static_cast<std::size_t>(channel)] +
            tone * kScale[static_cast<std::size_t>(channel)] / 4;
  }
  return std::clamp(value + tile_offset, 0, 255);
}

}  // namespace

std::span<const CurvesPreset> builtin_curves_presets() {
  const auto identity = points({{0, 0}, {255, 255}});
  static const std::array<CurvesPreset, 8> presets{
      CurvesPreset{QStringLiteral("curves.default"), QStringLiteral("Default"),
                   preset_curves(identity)},
      CurvesPreset{QStringLiteral("curves.medium_contrast"), QStringLiteral("Medium Contrast"),
                   preset_curves(points({{0, 0}, {64, 52}, {128, 128}, {192, 204}, {255, 255}}))},
      CurvesPreset{QStringLiteral("curves.strong_contrast"), QStringLiteral("Strong Contrast"),
                   preset_curves(points({{0, 0}, {56, 32}, {128, 128}, {200, 226}, {255, 255}}))},
      CurvesPreset{QStringLiteral("curves.lift_shadows"), QStringLiteral("Lift Shadows"),
                   preset_curves(points({{0, 18}, {54, 76}, {128, 143}, {255, 255}}))},
      CurvesPreset{QStringLiteral("curves.recover_highlights"), QStringLiteral("Recover Highlights"),
                   preset_curves(points({{0, 0}, {128, 122}, {205, 190}, {255, 238}}))},
      CurvesPreset{QStringLiteral("curves.matte"), QStringLiteral("Matte"),
                   preset_curves(points({{0, 22}, {48, 47}, {128, 130}, {215, 207}, {255, 238}}))},
      CurvesPreset{QStringLiteral("curves.warm"), QStringLiteral("Warm"),
                   preset_curves(points({{0, 0}, {64, 58}, {128, 132}, {192, 205}, {255, 255}}),
                                 points({{0, 5}, {128, 140}, {255, 255}}), identity,
                                 points({{0, 0}, {128, 116}, {255, 246}}))},
      CurvesPreset{QStringLiteral("curves.cool"), QStringLiteral("Cool"),
                   preset_curves(points({{0, 0}, {64, 58}, {128, 132}, {192, 205}, {255, 255}}),
                                 points({{0, 0}, {128, 117}, {255, 246}}), identity,
                                 points({{0, 6}, {128, 140}, {255, 255}}))},
  };
  return presets;
}

const CurvesPreset* find_curves_preset(const QString& id) {
  for (const auto& preset : builtin_curves_presets()) {
    if (preset.id == id) {
      return &preset;
    }
  }
  return nullptr;
}

const CurvesPreset* find_curves_preset(const CurvesAdjustment& adjustment) {
  for (const auto& preset : builtin_curves_presets()) {
    if (preset.adjustment == adjustment) {
      return &preset;
    }
  }
  return nullptr;
}

QString curves_preset_display_name(const CurvesPreset& preset) {
  if (preset.id == QStringLiteral("curves.default")) {
    return QObject::tr("Default");
  }
  if (preset.id == QStringLiteral("curves.medium_contrast")) {
    return QObject::tr("Medium Contrast");
  }
  if (preset.id == QStringLiteral("curves.strong_contrast")) {
    return QObject::tr("Strong Contrast");
  }
  if (preset.id == QStringLiteral("curves.lift_shadows")) {
    return QObject::tr("Lift Shadows");
  }
  if (preset.id == QStringLiteral("curves.recover_highlights")) {
    return QObject::tr("Recover Highlights");
  }
  if (preset.id == QStringLiteral("curves.matte")) {
    return QObject::tr("Matte");
  }
  if (preset.id == QStringLiteral("curves.warm")) {
    return QObject::tr("Warm");
  }
  if (preset.id == QStringLiteral("curves.cool")) {
    return QObject::tr("Cool");
  }
  return preset.english_name;
}

QImage curves_adjustment_thumbnail(const CurvesAdjustment& adjustment, QSize size) {
  if (!size.isValid() || size.width() < 3 || size.height() < 3) {
    return {};
  }
  QImage image(size, QImage::Format_RGB888);
  const auto lut = build_curves_lut(adjustment);
  for (int y = 0; y < image.height(); ++y) {
    auto* row = image.scanLine(y);
    for (int x = 0; x < image.width(); ++x) {
      auto* pixel = row + x * 3;
      if (x == 0 || y == 0 || x + 1 == image.width() || y + 1 == image.height()) {
        pixel[0] = 31;
        pixel[1] = 35;
        pixel[2] = 42;
        continue;
      }
      const auto red = thumbnail_source_channel(x, y, image.width(), image.height(), 0);
      const auto green = thumbnail_source_channel(x, y, image.width(), image.height(), 1);
      const auto blue = thumbnail_source_channel(x, y, image.width(), image.height(), 2);
      pixel[0] = lut.red[static_cast<std::size_t>(red)];
      pixel[1] = lut.green[static_cast<std::size_t>(green)];
      pixel[2] = lut.blue[static_cast<std::size_t>(blue)];
    }
  }
  return image;
}

}  // namespace patchy::ui
