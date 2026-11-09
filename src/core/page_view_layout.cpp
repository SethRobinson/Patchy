#include "core/page_view_layout.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace patchy {

namespace {

constexpr PageSizePoints kLetterPoints{612.0, 792.0};

PageSizePoints usable_size(std::span<const PageSizePoints> sizes, int page) {
  if (page < 0 || page >= static_cast<int>(sizes.size())) {
    return kLetterPoints;
  }
  const PageSizePoints size = sizes[static_cast<std::size_t>(page)];
  if (!(size.width > 0.0) || !(size.height > 0.0) || !std::isfinite(size.width) || !std::isfinite(size.height)) {
    return kLetterPoints;
  }
  return size;
}

constexpr std::array<double, 18> kZoomSteps{10.0,  25.0,  33.33, 50.0,  66.67, 75.0,  100.0,  125.0,  150.0,
                                            200.0, 300.0, 400.0, 500.0, 600.0, 800.0, 1000.0, 1200.0, 1600.0};

}  // namespace

std::vector<PageSpread> page_spreads(int page_count, PageSpreadMode mode, PageReadingDirection direction) {
  std::vector<PageSpread> spreads;
  if (page_count <= 0) {
    return spreads;
  }
  const auto add_pair = [&spreads, direction](int first, int second) {
    PageSpread spread;
    if (direction == PageReadingDirection::RightToLeft) {
      spread.pages = {second, first};
    } else {
      spread.pages = {first, second};
    }
    spreads.push_back(std::move(spread));
  };
  int page = 0;
  if (mode == PageSpreadMode::TwoPagesCoverAlone || mode == PageSpreadMode::Single) {
    spreads.push_back(PageSpread{{0}});
    page = 1;
  }
  while (page < page_count) {
    if (mode == PageSpreadMode::Single || page + 1 >= page_count) {
      spreads.push_back(PageSpread{{page}});
      ++page;
      continue;
    }
    add_pair(page, page + 1);
    page += 2;
  }
  return spreads;
}

int spread_index_for_page(std::span<const PageSpread> spreads, int page) {
  for (std::size_t index = 0; index < spreads.size(); ++index) {
    const auto& pages = spreads[index].pages;
    if (std::find(pages.begin(), pages.end(), page) != pages.end()) {
      return static_cast<int>(index);
    }
  }
  return 0;
}

PageSizePoints turned_page_size(PageSizePoints size, int quarter_turns) {
  if ((((quarter_turns % 4) + 4) % 4) % 2 == 1) {
    return {size.height, size.width};
  }
  return size;
}

PageSizePoints spread_size_points(std::span<const PageSizePoints> sizes, const PageSpread& spread,
                                  int quarter_turns) {
  PageSizePoints total{0.0, 0.0};
  for (const int page : spread.pages) {
    const PageSizePoints size = turned_page_size(usable_size(sizes, page), quarter_turns);
    total.width += size.width;
    total.height = std::max(total.height, size.height);
  }
  return total;
}

PageViewLayout layout_page_view(std::span<const PageSizePoints> sizes, std::span<const PageSpread> spreads,
                                const PageViewLayoutOptions& options, double pixels_per_point,
                                double viewport_width, double viewport_height, std::optional<int> only_row) {
  PageViewLayout layout;
  if (spreads.empty() || !(pixels_per_point > 0.0)) {
    return layout;
  }
  int first = 0;
  int last = static_cast<int>(spreads.size()) - 1;
  if (only_row.has_value()) {
    first = std::clamp(*only_row, 0, last);
    last = first;
  }
  layout.first_row = first;
  double widest = 0.0;
  for (int row = first; row <= last; ++row) {
    const PageSizePoints size = spread_size_points(sizes, spreads[static_cast<std::size_t>(row)], options.quarter_turns);
    widest = std::max(widest, size.width * pixels_per_point);
  }
  layout.width = std::max(viewport_width, widest + 2.0 * options.margin);
  double y = options.margin;
  for (int row = first; row <= last; ++row) {
    const PageSpread& spread = spreads[static_cast<std::size_t>(row)];
    const PageSizePoints size = spread_size_points(sizes, spread, options.quarter_turns);
    PageViewRow view_row;
    view_row.rect.width = size.width * pixels_per_point;
    view_row.rect.height = size.height * pixels_per_point;
    view_row.rect.x = (layout.width - view_row.rect.width) / 2.0;
    view_row.rect.y = y;
    double x = view_row.rect.x;
    for (const int page : spread.pages) {
      const PageSizePoints page_size = turned_page_size(usable_size(sizes, page), options.quarter_turns);
      PageViewSlot slot;
      slot.page = page;
      slot.row = row;
      slot.rect.width = page_size.width * pixels_per_point;
      slot.rect.height = page_size.height * pixels_per_point;
      slot.rect.x = x;
      slot.rect.y = y + (view_row.rect.height - slot.rect.height) / 2.0;
      x += slot.rect.width;
      view_row.slot_indices.push_back(static_cast<int>(layout.pages.size()));
      layout.pages.push_back(slot);
    }
    y += view_row.rect.height + options.row_gap;
    layout.rows.push_back(std::move(view_row));
  }
  layout.height = y - options.row_gap + options.margin;
  if (only_row.has_value() && layout.height < viewport_height) {
    // One row at a time: centre it in the window, the way a reader shows a page.
    const double shift = (viewport_height - layout.height) / 2.0;
    for (auto& slot : layout.pages) {
      slot.rect.y += shift;
    }
    for (auto& row : layout.rows) {
      row.rect.y += shift;
    }
    layout.height = viewport_height;
  }
  return layout;
}

double fit_pixels_per_point(std::span<const PageSizePoints> sizes, const PageSpread& spread,
                            const PageViewLayoutOptions& options, PageFitMode mode, double viewport_width,
                            double viewport_height) {
  const PageSizePoints size = spread_size_points(sizes, spread, options.quarter_turns);
  const double usable_width = viewport_width - 2.0 * options.margin;
  const double usable_height = viewport_height - 2.0 * options.margin;
  if (!(size.width > 0.0) || !(size.height > 0.0) || usable_width <= 1.0) {
    return 0.0;
  }
  const double across = usable_width / size.width;
  if (mode == PageFitMode::Width) {
    return across;
  }
  if (usable_height <= 1.0) {
    return 0.0;
  }
  return std::min(across, usable_height / size.height);
}

std::span<const double> page_view_zoom_steps() {
  return kZoomSteps;
}

double next_page_view_zoom(double percent, int direction) {
  // A step within half a percent of the current zoom counts as the current one, so the
  // first Zoom In from 99.8% (a fit) goes to 125%, not to 100%.
  constexpr double kSlack = 0.5;
  if (direction > 0) {
    for (const double step : kZoomSteps) {
      if (step > percent + kSlack) {
        return step;
      }
    }
    return kZoomSteps.back();
  }
  for (auto it = kZoomSteps.rbegin(); it != kZoomSteps.rend(); ++it) {
    if (*it < percent - kSlack) {
      return *it;
    }
  }
  return kZoomSteps.front();
}

}  // namespace patchy
