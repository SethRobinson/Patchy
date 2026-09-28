#include "ui/qt_geometry.hpp"

#include <QString>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace patchy::ui {

QRect to_qrect(Rect rect) {
  return QRect(rect.x, rect.y, rect.width, rect.height);
}

Rect to_core_rect(QRect rect) {
  rect = rect.normalized();
  return Rect{rect.x(), rect.y(), rect.width(), rect.height()};
}

namespace {

// Radii above this dilate a coverage mask; smaller ones keep the historical
// region unions (cheap at small radii, and pinned bit-for-bit by tests).
constexpr int kRegionUnionMaximumRadius = 16;

// Square (Chebyshev) dilation of `region` by `pixels` through a byte mask:
// separable window counts make each pass O(mask area) whatever the radius.
// Sources farther than `pixels` outside `bounds` cannot reach it, so the mask
// only spans the region's reachable part plus the dilation apron.
QRegion expanded_region_by_mask(const QRegion& region, int pixels, QRect bounds) {
  const auto reachable = region.boundingRect().intersected(bounds.adjusted(-pixels, -pixels, pixels, pixels));
  if (reachable.isEmpty()) {
    return {};
  }
  const auto domain = reachable.adjusted(-pixels, -pixels, pixels, pixels);
  const auto width = domain.width();
  const auto height = domain.height();
  const auto stride = static_cast<std::size_t>(width);
  std::vector<std::uint8_t> source(stride * static_cast<std::size_t>(height), 0U);
  for (const auto& rect : region.intersected(reachable)) {
    const auto local = rect.translated(-domain.topLeft());
    for (int y = local.top(); y <= local.bottom(); ++y) {
      auto* row = source.data() + static_cast<std::size_t>(y) * stride;
      std::fill(row + local.left(), row + local.right() + 1, static_cast<std::uint8_t>(1));
    }
  }

  // Horizontal pass: a pixel is covered when any source lies within
  // [x - pixels, x + pixels] on its row.
  std::vector<std::uint8_t> horizontal(source.size(), 0U);
  for (int y = 0; y < height; ++y) {
    const auto* in = source.data() + static_cast<std::size_t>(y) * stride;
    auto* out = horizontal.data() + static_cast<std::size_t>(y) * stride;
    int count = 0;
    for (int x = 0; x < std::min(width, pixels + 1); ++x) {
      count += in[x];
    }
    for (int x = 0; x < width; ++x) {
      out[x] = count > 0 ? 1U : 0U;
      if (x + pixels + 1 < width) {
        count += in[x + pixels + 1];
      }
      if (x - pixels >= 0) {
        count -= in[x - pixels];
      }
    }
  }

  // Vertical pass with per-column window counts, row by row (cache friendly),
  // collecting the covered runs inside `bounds` as it goes. Consecutive rows
  // with identical runs merge into one band: QRegion compares its band list,
  // so this must be the canonical (maximally coalesced) form the union path
  // produces.
  const auto clip = bounds.translated(-domain.topLeft()).intersected(QRect(0, 0, width, height));
  std::vector<int> counts(stride, 0);
  for (int y = 0; y < std::min(height, pixels + 1); ++y) {
    const auto* row = horizontal.data() + static_cast<std::size_t>(y) * stride;
    for (int x = 0; x < width; ++x) {
      counts[static_cast<std::size_t>(x)] += row[x];
    }
  }
  std::vector<QRect> runs;
  std::vector<std::pair<int, int>> band;
  std::vector<std::pair<int, int>> row_runs;
  int band_top = 0;
  const auto flush_band = [&](int band_bottom) {
    for (const auto& [left, right] : band) {
      runs.push_back(QRect(left + domain.left(), band_top + domain.top(), right - left, band_bottom - band_top));
    }
  };
  for (int y = 0; y < height; ++y) {
    row_runs.clear();
    if (y >= clip.top() && y <= clip.bottom()) {
      int run_start = -1;
      for (int x = clip.left(); x <= clip.right() + 1; ++x) {
        const bool covered = x <= clip.right() && counts[static_cast<std::size_t>(x)] > 0;
        if (covered && run_start < 0) {
          run_start = x;
        } else if (!covered && run_start >= 0) {
          row_runs.emplace_back(run_start, x);
          run_start = -1;
        }
      }
    }
    if (row_runs != band) {
      flush_band(y);
      band.swap(row_runs);
      band_top = y;
    }
    if (y + pixels + 1 < height) {
      const auto* entering = horizontal.data() + static_cast<std::size_t>(y + pixels + 1) * stride;
      for (int x = 0; x < width; ++x) {
        counts[static_cast<std::size_t>(x)] += entering[x];
      }
    }
    if (y - pixels >= 0) {
      const auto* leaving = horizontal.data() + static_cast<std::size_t>(y - pixels) * stride;
      for (int x = 0; x < width; ++x) {
        counts[static_cast<std::size_t>(x)] -= leaving[x];
      }
    }
  }
  flush_band(height);
  QRegion expanded;
  if (!runs.empty()) {
    expanded.setRects(runs.data(), static_cast<int>(runs.size()));
  }
  return expanded;
}

}  // namespace

QRegion expanded_region(const QRegion& region, int pixels, QRect bounds) {
  if (region.isEmpty() || pixels <= 0) {
    return region.intersected(bounds);
  }

  pixels = std::clamp(pixels, 0, kMaxSelectionModifyRadius);
  if (pixels > kRegionUnionMaximumRadius) {
    return expanded_region_by_mask(region, pixels, bounds);
  }
  // A square dilation is separable: sweeping the horizontal union vertically gives the same
  // result as the full (2r+1)^2 translation grid in 2(2r+1) unions.
  QRegion horizontal;
  for (int dx = -pixels; dx <= pixels; ++dx) {
    horizontal = horizontal.united(region.translated(dx, 0));
  }
  QRegion expanded;
  for (int dy = -pixels; dy <= pixels; ++dy) {
    expanded = expanded.united(horizontal.translated(0, dy));
  }
  return expanded.intersected(bounds);
}

const char* selection_stroke_location_token(SelectionStrokeLocation location) {
  switch (location) {
    case SelectionStrokeLocation::Inside:
      return "inside";
    case SelectionStrokeLocation::Outside:
      return "outside";
    case SelectionStrokeLocation::Center:
      break;
  }
  return "center";
}

SelectionStrokeLocation selection_stroke_location_from_token(const QString& token,
                                                             SelectionStrokeLocation fallback) {
  if (token == QLatin1String("inside")) {
    return SelectionStrokeLocation::Inside;
  }
  if (token == QLatin1String("outside")) {
    return SelectionStrokeLocation::Outside;
  }
  if (token == QLatin1String("center")) {
    return SelectionStrokeLocation::Center;
  }
  return fallback;
}

namespace {

// Selected pixels with at least one 4-neighbor outside the selection (the one-pixel inner rim).
[[nodiscard]] QRegion inner_rim(const QRegion& selection) {
  QRegion rim;
  rim = rim.united(selection.subtracted(selection.translated(1, 0)));
  rim = rim.united(selection.subtracted(selection.translated(-1, 0)));
  rim = rim.united(selection.subtracted(selection.translated(0, 1)));
  rim = rim.united(selection.subtracted(selection.translated(0, -1)));
  return rim;
}

[[nodiscard]] QRegion inside_band(const QRegion& selection, int width, QRect bounds) {
  if (width <= 0) {
    return {};
  }
  // Growing the rim never leaves the selection's own bounding rect, so the rect is a tight
  // clip that keeps the dilation cheap.
  return expanded_region(inner_rim(selection), width - 1, selection.boundingRect())
      .intersected(selection)
      .intersected(bounds);
}

[[nodiscard]] QRegion outside_band(const QRegion& selection, int width, QRect bounds) {
  if (width <= 0) {
    return {};
  }
  return expanded_region(selection, width, bounds).subtracted(selection);
}

}  // namespace

QRegion selection_stroke_region(const QRegion& selection, int width, SelectionStrokeLocation location,
                                QRect bounds) {
  if (selection.isEmpty() || width <= 0) {
    return {};
  }
  width = std::clamp(width, 1, 250);
  switch (location) {
    case SelectionStrokeLocation::Inside:
      return inside_band(selection, width, bounds);
    case SelectionStrokeLocation::Outside:
      return outside_band(selection, width, bounds);
    case SelectionStrokeLocation::Center:
      break;
  }
  const int inside = (width + 1) / 2;
  const int outside = width / 2;
  return inside_band(selection, inside, bounds).united(outside_band(selection, outside, bounds));
}

}  // namespace patchy::ui
