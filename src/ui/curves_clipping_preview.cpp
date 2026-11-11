#include "ui/curves_clipping_preview.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <future>
#include <vector>

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
  return render_curves_clipping_preview(source, mode, active_component, 1);
}

QImage render_curves_clipping_preview(const QImage& source, CurvesClippingMode mode,
                                      std::optional<CurvesChannel> active_component, int workers) {
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

  // One table per component: the output byte for every source byte.
  const auto selected = selected_components(active_component);
  std::array<std::array<std::uint8_t, 256>, 3> tables{};
  for (std::size_t component = 0; component < tables.size(); ++component) {
    for (int value = 0; value < 256; ++value) {
      tables[component][static_cast<std::size_t>(value)] =
          clipping_value(static_cast<std::uint8_t>(value), selected[component], mode);
    }
  }
  const int stride = has_alpha ? 4 : 3;
  const int width = normalized.width();
  // Detach on this thread; the bands only write their own rows.
  auto* result_bits = result.bits();
  const auto result_bytes_per_line = static_cast<std::size_t>(result.bytesPerLine());
  const auto render_rows = [&](int top, int bottom) {
    for (int y = top; y < bottom; ++y) {
      const auto* source_row = normalized.constScanLine(y);
      auto* result_row = result_bits + static_cast<std::size_t>(y) * result_bytes_per_line;
      for (int x = 0; x < width; ++x) {
        const auto offset = static_cast<std::size_t>(x) * static_cast<std::size_t>(stride);
        result_row[offset] = tables[0][source_row[offset]];
        result_row[offset + 1] = tables[1][source_row[offset + 1]];
        result_row[offset + 2] = tables[2][source_row[offset + 2]];
        if (has_alpha) {
          result_row[offset + 3] = source_row[offset + 3];
        }
      }
    }
  };

  const int height = normalized.height();
  const int bands = std::clamp(std::min(workers, height / 64), 1, 16);
  if (bands < 2) {
    render_rows(0, height);
    return result;
  }
  const int rows_per_band = (height + bands - 1) / bands;
  std::vector<std::future<void>> jobs;
  jobs.reserve(static_cast<std::size_t>(bands));
  for (int top = rows_per_band; top < height; top += rows_per_band) {
    const int bottom = std::min(height, top + rows_per_band);
    jobs.push_back(std::async(std::launch::async, [&render_rows, top, bottom] { render_rows(top, bottom); }));
  }
  render_rows(0, std::min(height, rows_per_band));
  for (auto& job : jobs) {
    job.get();
  }
  return result;
}

}  // namespace patchy::ui
