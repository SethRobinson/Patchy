#include "ui/curves_clipping_preview.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace patchy::ui {
namespace {

constexpr std::uint8_t kShadowBackground = 255;
constexpr std::uint8_t kHighlightBackground = 0;
constexpr std::uint8_t kCombinedBackground = 128;

std::array<bool, 3> selected_components(std::optional<CurvesChannel> active_component) noexcept {
  if (!active_component || *active_component == CurvesChannel::Rgb) {
    return {true, true, true};
  }

  switch (*active_component) {
    case CurvesChannel::Red:
      return {true, false, false};
    case CurvesChannel::Green:
      return {false, true, false};
    case CurvesChannel::Blue:
      return {false, false, true};
    case CurvesChannel::Rgb:
      return {true, true, true};
  }
  return {true, true, true};
}

std::uint8_t background_for_mode(CurvesClippingMode mode) noexcept {
  switch (mode) {
    case CurvesClippingMode::Shadows:
      return kShadowBackground;
    case CurvesClippingMode::Highlights:
      return kHighlightBackground;
    case CurvesClippingMode::Both:
      return kCombinedBackground;
  }
  return kCombinedBackground;
}

std::uint8_t clipping_value(std::uint8_t source, bool selected,
                            CurvesClippingMode mode) noexcept {
  const auto background = background_for_mode(mode);
  if (!selected) {
    return background;
  }

  switch (mode) {
    case CurvesClippingMode::Shadows:
      return source == 0 ? std::uint8_t{0} : background;
    case CurvesClippingMode::Highlights:
      return source == 255 ? std::uint8_t{255} : background;
    case CurvesClippingMode::Both:
      if (source == 0) {
        return 0;
      }
      if (source == 255) {
        return 255;
      }
      return background;
  }
  return background;
}

}  // namespace

QImage render_curves_clipping_preview(const QImage& source, CurvesClippingMode mode,
                                      std::optional<CurvesChannel> active_component) {
  if (source.isNull()) {
    return {};
  }

  const bool has_alpha = source.hasAlphaChannel();
  const auto format = has_alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
  const QImage normalized =
      source.format() == format ? source : source.convertToFormat(format);
  if (normalized.isNull()) {
    return {};
  }

  QImage result(normalized.size(), format);
  if (result.isNull()) {
    return {};
  }

  const auto selected = selected_components(active_component);
  const int stride = has_alpha ? 4 : 3;
  for (int y = 0; y < normalized.height(); ++y) {
    const auto* source_row = normalized.constScanLine(y);
    auto* result_row = result.scanLine(y);
    for (int x = 0; x < normalized.width(); ++x) {
      const auto offset = static_cast<std::size_t>(x) * static_cast<std::size_t>(stride);
      for (std::size_t component = 0; component < selected.size(); ++component) {
        result_row[offset + component] =
            clipping_value(source_row[offset + component], selected[component], mode);
      }
      if (has_alpha) {
        result_row[offset + 3] = source_row[offset + 3];
      }
    }
  }
  return result;
}

}  // namespace patchy::ui
