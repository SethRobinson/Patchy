#include "render/layer_style_mask_ops.hpp"

#include "core/pixel_depth.hpp"
#include "core/vector_shape.hpp"
#include "render/layer_render_facade.hpp"
#include "render/style_mask_cache.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace patchy::render_detail {

void max_filter_row(const std::vector<float>& input, std::vector<float>& output,
                           std::vector<int>& candidates, int width, int source_y, int radius) {
  const auto row_offset = static_cast<std::size_t>(source_y) * static_cast<std::size_t>(width);
  candidates.clear();
  std::size_t first_candidate = 0;
  int next_x = 0;
  for (int x = 0; x < width; ++x) {
    const auto add_until = std::min(width - 1, x + radius);
    while (next_x <= add_until) {
      const auto value = input[row_offset + static_cast<std::size_t>(next_x)];
      while (candidates.size() > first_candidate &&
             input[row_offset + static_cast<std::size_t>(candidates.back())] <= value) {
        candidates.pop_back();
      }
      candidates.push_back(next_x);
      ++next_x;
    }

    const auto remove_before = x - radius;
    while (first_candidate < candidates.size() && candidates[first_candidate] < remove_before) {
      ++first_candidate;
    }
    output[static_cast<std::size_t>(x)] =
        first_candidate >= candidates.size()
            ? 0.0F
            : input[row_offset + static_cast<std::size_t>(candidates[first_candidate])];
  }
}

std::vector<float> dilate_mask(const std::vector<float>& input, int width, int height, int radius) {
  if (radius <= 0) {
    return input;
  }
  const auto radius_squared = radius * radius;
  std::vector<float> output(input.size(), 0.0F);
  std::vector<float> row_max(static_cast<std::size_t>(std::max(0, width)), 0.0F);
  std::vector<int> candidates;
  candidates.reserve(static_cast<std::size_t>(std::max(0, width)));
  for (int dy = -radius; dy <= radius; ++dy) {
    const auto row_radius = static_cast<int>(std::floor(std::sqrt(static_cast<float>(radius_squared - dy * dy))));
    const auto target_start_y = std::max(0, -dy);
    const auto target_end_y = std::min(height, height - dy);
    for (int target_y = target_start_y; target_y < target_end_y; ++target_y) {
      const auto source_y = target_y + dy;
      max_filter_row(input, row_max, candidates, width, source_y, row_radius);
      auto* output_row = output.data() + static_cast<std::size_t>(target_y) * static_cast<std::size_t>(width);
      for (int x = 0; x < width; ++x) {
        output_row[x] = std::max(output_row[x], row_max[static_cast<std::size_t>(x)]);
      }
    }
  }
  return output;
}

// 1D squared-distance lower-envelope pass (Felzenszwalb & Huttenlocher). `f` is the
// per-sample seed cost, `d` receives min over q' of (q - q')^2 + f[q']. `v`/`z` are
// scratch of size n / n+1. Envelope intersections are computed in double so the
// integer-valued inputs stay exact; no RNG, no toolchain-dependent math.
void squared_distance_transform_1d(const float* f, float* d, int* v, double* z, int n, int* arg) {
  int k = 0;
  v[0] = 0;
  z[0] = -1.0e30;
  z[1] = 1.0e30;
  for (int q = 1; q < n; ++q) {
    const auto fq = static_cast<double>(f[q]) + static_cast<double>(q) * static_cast<double>(q);
    auto intersection =
        (fq - (static_cast<double>(f[v[k]]) + static_cast<double>(v[k]) * static_cast<double>(v[k]))) /
        (2.0 * q - 2.0 * v[k]);
    while (intersection <= z[k]) {
      --k;
      intersection =
          (fq - (static_cast<double>(f[v[k]]) + static_cast<double>(v[k]) * static_cast<double>(v[k]))) /
          (2.0 * q - 2.0 * v[k]);
    }
    ++k;
    v[k] = q;
    z[k] = intersection;
    z[k + 1] = 1.0e30;
  }
  k = 0;
  for (int q = 0; q < n; ++q) {
    while (z[k + 1] < static_cast<double>(q)) {
      ++k;
    }
    const auto dx = static_cast<float>(q - v[k]);
    d[q] = dx * dx + f[v[k]];
    if (arg != nullptr) {
      arg[q] = v[k];
    }
  }
}

void exact_squared_distance_transform(std::vector<float>& field, int width, int height,
                                      std::vector<int>* sources) {
  if (width <= 0 || height <= 0) {
    return;
  }
  const auto n = std::max(width, height);
  std::vector<float> f(static_cast<std::size_t>(n), 0.0F);
  std::vector<float> d(static_cast<std::size_t>(n), 0.0F);
  std::vector<int> v(static_cast<std::size_t>(n), 0);
  std::vector<double> z(static_cast<std::size_t>(n) + 1U, 0.0);
  std::vector<int> arg;
  // Column pass: each pixel learns the row of its nearest source within its
  // own column; the row pass then picks the column whose column-distance plus
  // horizontal offset is smallest, so that column's recorded row completes the
  // source. The lower envelope yields the argmin for free.
  std::vector<int> source_rows;
  if (sources != nullptr) {
    arg.assign(static_cast<std::size_t>(n), 0);
    source_rows.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    sources->assign(source_rows.size(), -1);
  }
  for (int x = 0; x < width; ++x) {
    for (int y = 0; y < height; ++y) {
      f[static_cast<std::size_t>(y)] = field[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x];
    }
    squared_distance_transform_1d(f.data(), d.data(), v.data(), z.data(), height,
                                  sources != nullptr ? arg.data() : nullptr);
    for (int y = 0; y < height; ++y) {
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x;
      field[index] = d[static_cast<std::size_t>(y)];
      if (sources != nullptr) {
        source_rows[index] = arg[static_cast<std::size_t>(y)];
      }
    }
  }
  for (int y = 0; y < height; ++y) {
    auto* row = field.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    std::copy(row, row + width, f.data());
    squared_distance_transform_1d(f.data(), d.data(), v.data(), z.data(), width,
                                  sources != nullptr ? arg.data() : nullptr);
    std::copy(d.data(), d.data() + width, row);
    if (sources != nullptr) {
      for (int x = 0; x < width; ++x) {
        const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x;
        if (row[x] >= kEdtUnreached * 0.5F) {
          continue;
        }
        const auto source_x = arg[static_cast<std::size_t>(x)];
        const auto source_y = source_rows[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                          static_cast<std::size_t>(source_x)];
        (*sources)[index] = source_y * width + source_x;
      }
    }
  }
}

void exact_squared_distance_transform(std::vector<float>& field, int width, int height) {
  exact_squared_distance_transform(field, width, height, nullptr);
}

void stroke_distance_field_with_edge_alpha(const std::vector<float>& base, int width, int height,
                                           bool sources_are_painted, std::vector<float>& distance,
                                           std::vector<float>& edge_alpha) {
  distance.assign(base.size(), kEdtUnreached);
  for (std::size_t index = 0; index < base.size(); ++index) {
    const bool source = sources_are_painted ? base[index] > 0.0F : base[index] < 1.0F;
    if (source) {
      distance[index] = 0.0F;
    }
  }
  std::vector<int> sources;
  exact_squared_distance_transform(distance, width, height, &sources);
  edge_alpha.assign(base.size(), sources_are_painted ? 0.0F : 1.0F);
  for (std::size_t index = 0; index < base.size(); ++index) {
    distance[index] = std::sqrt(distance[index]);
    if (sources[index] >= 0) {
      edge_alpha[index] = base[static_cast<std::size_t>(sources[index])];
    }
  }
}

// Distance in pixels from every pixel to the nearest painted (`alpha > 0`) or
// unpainted (`alpha == 0`) pixel of `base`, per `sources_are_painted`. Pixels with
// no source anywhere read as a huge distance (band coverage 0).
std::vector<float> stroke_distance_field(const std::vector<float>& base, int width, int height,
                                                bool sources_are_painted) {
  std::vector<float> field(base.size(), kEdtUnreached);
  for (std::size_t index = 0; index < base.size(); ++index) {
    if ((base[index] > 0.0F) == sources_are_painted) {
      field[index] = 0.0F;
    }
  }
  exact_squared_distance_transform(field, width, height);
  for (auto& value : field) {
    value = std::sqrt(value);
  }
  return field;
}

float stroke_band_coverage(float distance, float band) noexcept {
  return band <= 0.0F ? 0.0F : clamp_unit(band + kStrokeContourOffset - distance);
}

void stroke_subpixel_distance_fields(const std::vector<float>& matte, int width, int height,
                                     bool need_outside, bool need_inside,
                                     std::vector<float>& outside, std::vector<float>& inside) {
  constexpr int kScale = 3;
  // Bound the two 9x float planes. Large mattes retain the native-resolution
  // contour rather than multiplying a full-size layer's memory by nine.
  constexpr std::size_t kMaxSupersampledMattePixels = 1024U * 1024U;
  if (matte.size() > kMaxSupersampledMattePixels) {
    std::vector<float> contour(matte.size());
    std::transform(matte.begin(), matte.end(), contour.begin(),
                   [](float alpha) { return alpha >= 0.5F ? 1.0F : 0.0F; });
    if (need_outside) {
      outside = stroke_distance_field(contour, width, height, true);
    }
    if (need_inside) {
      inside = stroke_distance_field(contour, width, height, false);
    }
    return;
  }
  const auto fine_width = width * kScale;
  const auto fine_height = height * kScale;
  std::vector<float> fine(static_cast<std::size_t>(fine_width) * static_cast<std::size_t>(fine_height),
                          0.0F);
  for (int fine_y = 0; fine_y < fine_height; ++fine_y) {
    // Coarse pixel-center coordinates; fine centers land at -1/3, 0, +1/3
    // around each coarse center, so fy = 3y+1 samples the coarse value
    // exactly (bilinear at a sample point).
    const auto cy = (static_cast<float>(fine_y) + 0.5F) / kScale - 0.5F;
    const auto y0 = std::clamp(static_cast<int>(std::floor(cy)), 0, height - 1);
    const auto y1 = std::min(y0 + 1, height - 1);
    const auto ty = std::clamp(cy - static_cast<float>(y0), 0.0F, 1.0F);
    auto* row = fine.data() + static_cast<std::size_t>(fine_y) * fine_width;
    const auto* row0 = matte.data() + static_cast<std::size_t>(y0) * width;
    const auto* row1 = matte.data() + static_cast<std::size_t>(y1) * width;
    for (int fine_x = 0; fine_x < fine_width; ++fine_x) {
      const auto cx = (static_cast<float>(fine_x) + 0.5F) / kScale - 0.5F;
      const auto x0 = std::clamp(static_cast<int>(std::floor(cx)), 0, width - 1);
      const auto x1 = std::min(x0 + 1, width - 1);
      const auto tx = std::clamp(cx - static_cast<float>(x0), 0.0F, 1.0F);
      const auto top = row0[x0] + (row0[x1] - row0[x0]) * tx;
      const auto bottom = row1[x0] + (row1[x1] - row1[x0]) * tx;
      const auto alpha = top + (bottom - top) * ty;
      row[fine_x] = alpha >= 0.5F ? 1.0F : 0.0F;
    }
  }
  constexpr float kCenterCompensation = 0.5F - 0.5F / kScale;
  const auto read_back = [&](const std::vector<float>& fine_distance, std::vector<float>& out) {
    out.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0F);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto fine_index =
            static_cast<std::size_t>(y * kScale + 1) * fine_width + static_cast<std::size_t>(x * kScale + 1);
        const auto distance = fine_distance[fine_index];
        out[static_cast<std::size_t>(y) * width + x] =
            distance <= 0.0F ? 0.0F : distance / kScale + kCenterCompensation;
      }
    }
  };
  if (need_outside) {
    read_back(stroke_distance_field(fine, fine_width, fine_height, true), outside);
  }
  if (need_inside) {
    read_back(stroke_distance_field(fine, fine_width, fine_height, false), inside);
  }
}

void box_blur_mask_into(const std::vector<float>& input, std::vector<float>& horizontal,
                               std::vector<float>& output, int width, int height, int radius) {
  for (int y = 0; y < height; ++y) {
    float sum = 0.0F;
    int count = 0;
    for (int x = -radius; x <= radius; ++x) {
      if (x >= 0 && x < width) {
        sum += input[static_cast<std::size_t>(y * width + x)];
        ++count;
      }
    }
    for (int x = 0; x < width; ++x) {
      horizontal[static_cast<std::size_t>(y * width + x)] = sum / static_cast<float>(std::max(1, count));
      const auto remove_x = x - radius;
      const auto add_x = x + radius + 1;
      if (remove_x >= 0 && remove_x < width) {
        sum -= input[static_cast<std::size_t>(y * width + remove_x)];
        --count;
      }
      if (add_x >= 0 && add_x < width) {
        sum += input[static_cast<std::size_t>(y * width + add_x)];
        ++count;
      }
    }
  }

  for (int x = 0; x < width; ++x) {
    float sum = 0.0F;
    int count = 0;
    for (int y = -radius; y <= radius; ++y) {
      if (y >= 0 && y < height) {
        sum += horizontal[static_cast<std::size_t>(y * width + x)];
        ++count;
      }
    }
    for (int y = 0; y < height; ++y) {
      output[static_cast<std::size_t>(y * width + x)] = sum / static_cast<float>(std::max(1, count));
      const auto remove_y = y - radius;
      const auto add_y = y + radius + 1;
      if (remove_y >= 0 && remove_y < height) {
        sum -= horizontal[static_cast<std::size_t>(remove_y * width + x)];
        --count;
      }
      if (add_y >= 0 && add_y < height) {
        sum += horizontal[static_cast<std::size_t>(add_y * width + x)];
        ++count;
      }
    }
  }
}

void blur_mask_in_place(std::vector<float>& mask, int width, int height, int radius, int passes) {
  if (radius <= 0 || passes <= 0 || mask.empty()) {
    return;
  }
  std::vector<float> horizontal(mask.size(), 0.0F);
  std::vector<float> output(mask.size(), 0.0F);
  for (int pass = 0; pass < passes; ++pass) {
    box_blur_mask_into(mask, horizontal, output, width, height, radius);
    mask.swap(output);
  }
}

int layer_style_falloff_radius(float size) noexcept {
  return std::max(0, static_cast<int>(std::ceil(std::max(0.0F, size))));
}

void blur_layer_style_mask_in_place(std::vector<float>& mask, int width, int height, float size) {
  const auto support = layer_style_falloff_radius(size);
  if (support <= 0 || mask.empty()) {
    return;
  }

  const auto passes = std::min(3, support);
  const auto base_radius = support / passes;
  const auto extra_radius_passes = support % passes;
  std::vector<float> horizontal(mask.size(), 0.0F);
  std::vector<float> output(mask.size(), 0.0F);
  for (int pass = 0; pass < passes; ++pass) {
    const auto radius = base_radius + (pass < extra_radius_passes ? 1 : 0);
    if (radius <= 0) {
      continue;
    }
    box_blur_mask_into(mask, horizontal, output, width, height, radius);
    mask.swap(output);
  }
}

void expand_layer_style_mask_in_place(std::vector<float>& mask, int width, int height, float radius,
                                             float pixels_per_unit);

int layer_style_mask_supersample_scale(int width, int height, float size) noexcept {
  if (size <= 0.0F || width <= 0 || height <= 0) {
    return 1;
  }
  constexpr std::int64_t kMaxSupersampledPixels = 8'000'000;
  constexpr int kScale = 2;
  const auto high_res_pixels = static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height) * kScale * kScale;
  return high_res_pixels <= kMaxSupersampledPixels ? kScale : 1;
}

float mask_sample_or_zero(const std::vector<float>& mask, int width, int height, int x, int y) noexcept {
  if (x < 0 || y < 0 || x >= width || y >= height) {
    return 0.0F;
  }
  return mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
}

float bilinear_mask_sample(const std::vector<float>& mask, int width, int height, float x, float y) noexcept {
  const auto x0 = static_cast<int>(std::floor(x));
  const auto y0 = static_cast<int>(std::floor(y));
  const auto tx = x - static_cast<float>(x0);
  const auto ty = y - static_cast<float>(y0);
  const auto x1 = x0 + 1;
  const auto y1 = y0 + 1;

  const auto top = mask_sample_or_zero(mask, width, height, x0, y0) * (1.0F - tx) +
                   mask_sample_or_zero(mask, width, height, x1, y0) * tx;
  const auto bottom = mask_sample_or_zero(mask, width, height, x0, y1) * (1.0F - tx) +
                      mask_sample_or_zero(mask, width, height, x1, y1) * tx;
  return top * (1.0F - ty) + bottom * ty;
}

std::vector<float> supersampled_layer_style_mask(const std::vector<float>& mask, int width, int height,
                                                        int scale) {
  const auto scaled_width = width * scale;
  const auto scaled_height = height * scale;
  std::vector<float> scaled(static_cast<std::size_t>(scaled_width) * static_cast<std::size_t>(scaled_height), 0.0F);
  for (int y = 0; y < scaled_height; ++y) {
    const auto source_y = (static_cast<float>(y) + 0.5F) / static_cast<float>(scale) - 0.5F;
    auto* row = scaled.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(scaled_width);
    for (int x = 0; x < scaled_width; ++x) {
      const auto source_x = (static_cast<float>(x) + 0.5F) / static_cast<float>(scale) - 0.5F;
      row[x] = bilinear_mask_sample(mask, width, height, source_x, source_y);
    }
  }
  return scaled;
}

void downsample_layer_style_mask(const std::vector<float>& scaled, std::vector<float>& mask, int width,
                                        int height, int scale) {
  const auto scaled_width = width * scale;
  const auto divisor = static_cast<float>(scale * scale);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      float sum = 0.0F;
      for (int sub_y = 0; sub_y < scale; ++sub_y) {
        const auto* row = scaled.data() +
                          static_cast<std::size_t>(y * scale + sub_y) * static_cast<std::size_t>(scaled_width) +
                          static_cast<std::size_t>(x * scale);
        for (int sub_x = 0; sub_x < scale; ++sub_x) {
          sum += row[sub_x];
        }
      }
      mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
          clamp_unit(sum / divisor);
    }
  }
}

namespace {

// The shared Photoshop soft-effect falloff (COM-calibrated July 2026 for BOTH
// the drop shadow and the outer glow; the two probe suites matched this one
// pipeline byte-for-byte on straight edges):
// 1. Spread expands the matte by the INTEGER radius lround(spread% x size) as a
//    GRAYSCALE dilation: max of alpha x area-sampled-disc coverage with exact
//    Euclidean distances. On binary mattes this is the familiar hard band; a
//    binarizing component-strength band overshoots antialiased text by ~20%.
// 2. The remaining size blurs with Satin's exact separable tent kernel,
//    N = max(2, lround(size)) - spread radius; spread 100 or size 0 means no
//    blur at all, while sizes 1-2 still blur with the N=2 tent.
// Do not reimplement spread as a post-blur gain: saturating a blur's tail
// exposes the kernel's support (per-glyph boxes jutting out of
// qual_rca_pinout.psd's spread-100 label plates) and turns float dust into
// pixels.
// Bounded exact grayscale dilation by an integer radius: max of value x
// area-sampled-disc coverage with exact Euclidean distances. Beyond the limit
// (rare: a spread/choke over ~30% of a large size) the chamfer band keeps the
// historical behavior; its binarization error is proportionally small at such
// radii. Shared by the exterior spread and the interior choke, arithmetic and
// iteration order verbatim from the pinned exterior pipeline.
void dilate_layer_style_mask_by_radius(std::vector<float>& mask, int width, int height, int radius) {
  if (radius <= 0 || width <= 0 || height <= 0 || mask.empty()) {
    return;
  }
  constexpr int kExactDilationRadiusLimit = 8;
  if (radius <= kExactDilationRadiusLimit) {
    const int reach = radius + 1;
    std::vector<float> weights;
    std::vector<int> offsets_x;
    std::vector<int> offsets_y;
    for (int dy = -reach; dy <= reach; ++dy) {
      for (int dx = -reach; dx <= reach; ++dx) {
        if (dx == 0 && dy == 0) {
          continue;
        }
        const auto distance = std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy);
        const auto coverage = std::clamp(static_cast<double>(radius) + 1.0 - distance, 0.0, 1.0);
        if (coverage > 0.0) {
          weights.push_back(static_cast<float>(coverage));
          offsets_x.push_back(dx);
          offsets_y.push_back(dy);
        }
      }
    }
    std::vector<float> dilated(mask);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto index =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
        auto value = dilated[index];
        for (std::size_t tap = 0; tap < weights.size(); ++tap) {
          const auto sx = x + offsets_x[tap];
          const auto sy = y + offsets_y[tap];
          if (sx < 0 || sy < 0 || sx >= width || sy >= height) {
            continue;
          }
          const auto candidate =
              mask[static_cast<std::size_t>(sy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(sx)] *
              weights[tap];
          value = std::max(value, candidate);
        }
        dilated[index] = value;
      }
    }
    mask.swap(dilated);
  } else {
    expand_layer_style_mask_in_place(mask, width, height, static_cast<float>(radius), 1.0F);
  }
}

void prepare_photoshop_soft_effect_mask(std::vector<float>& mask, int width, int height, float size,
                                        float spread) {
  const auto rounded_size = std::lround(std::max(0.0F, size));
  const auto spread_radius =
      static_cast<int>(std::lround(std::max(0.0F, size) * clamp_unit(spread / 100.0F)));
  dilate_layer_style_mask_by_radius(mask, width, height, spread_radius);
  if (rounded_size > 0) {
    const auto tent_peak = std::max<long>(2, rounded_size) - spread_radius;
    if (tent_peak >= 2) {
      blur_satin_tent_mask_in_place(mask, width, height, static_cast<float>(tent_peak));
    }
  }
}

}  // namespace

namespace {

// Floor division for the digital-line intercepts (C++ integer division
// truncates toward zero, which would bend lines with negative slopes).
constexpr std::int64_t floor_div(std::int64_t numerator, std::int64_t denominator) noexcept {
  const auto quotient = numerator / denominator;
  const auto remainder = numerator % denominator;
  return (remainder != 0 && ((remainder < 0) != (denominator < 0))) ? quotient - 1 : quotient;
}

// One digital line family: `major` runs 0..major_extent-1 in the sweep
// direction, `minor(major)` is the rounded cross-axis position of the line
// through the origin, and every pixel belongs to exactly one intercept.
struct SweepAxes {
  int major_extent{0};
  int minor_extent{0};
  bool major_is_x{true};
  bool reverse_major{false};
  int minor_step{0};  // signed cross-axis offset over the full sweep
};

std::size_t sweep_index(const SweepAxes& axes, int width, int major, int minor) noexcept {
  const auto along = axes.reverse_major ? axes.major_extent - 1 - major : major;
  const auto x = axes.major_is_x ? along : minor;
  const auto y = axes.major_is_x ? minor : along;
  return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

}  // namespace

void sweep_layer_style_mask_in_place(std::vector<float>& mask, int width, int height, int offset_x,
                                     int offset_y, float fade_percent) {
  const auto length = std::max(std::abs(offset_x), std::abs(offset_y));
  if (length <= 0 || width <= 0 || height <= 0) {
    return;
  }
  SweepAxes axes;
  axes.major_is_x = std::abs(offset_x) >= std::abs(offset_y);
  axes.major_extent = axes.major_is_x ? width : height;
  axes.minor_extent = axes.major_is_x ? height : width;
  const auto major_offset = axes.major_is_x ? offset_x : offset_y;
  axes.minor_step = axes.major_is_x ? offset_y : offset_x;
  axes.reverse_major = major_offset < 0;

  // Cross-axis position of the line through the origin after u major steps
  // in the sweep direction, rounded half up: floor((2 u step + L) / (2 L)).
  // u already counts along the offset's sign, so the minor axis follows the
  // offset's own sign unchanged.
  std::vector<int> line_minor(static_cast<std::size_t>(axes.major_extent));
  int min_minor = 0;
  int max_minor = 0;
  for (int u = 0; u < axes.major_extent; ++u) {
    // 64-bit: a 30000 px sweep across a wide domain overflows 2 * u * step.
    const auto value = static_cast<int>(floor_div(
        std::int64_t{2} * u * axes.minor_step + length, std::int64_t{2} * length));
    line_minor[static_cast<std::size_t>(u)] = value;
    min_minor = std::min(min_minor, value);
    max_minor = std::max(max_minor, value);
  }

  const auto fade = std::clamp(fade_percent, 0.0F, 100.0F) / 100.0F;
  struct Sample {
    int position;
    float value;
  };
  std::vector<Sample> deque;
  deque.reserve(static_cast<std::size_t>(axes.major_extent));
  for (int intercept = -max_minor; intercept < axes.minor_extent - min_minor; ++intercept) {
    deque.clear();
    std::size_t front = 0;
    int since_hit = length + 1;
    for (int u = 0; u < axes.major_extent; ++u) {
      const auto minor = intercept + line_minor[static_cast<std::size_t>(u)];
      // Off the buffer the matte is zero; the zero still enters the window so
      // deque positions and the fade counter stay aligned with u.
      const bool in_range = minor >= 0 && minor < axes.minor_extent;
      const auto index = in_range ? sweep_index(axes, width, u, minor) : std::size_t{0};
      const auto sample = in_range ? mask[index] : 0.0F;
      while (deque.size() > front && deque.back().value <= sample) {
        deque.pop_back();
      }
      deque.push_back(Sample{u, sample});
      while (front < deque.size() && deque[front].position < u - length) {
        ++front;
      }
      if (sample > 0.0F) {
        since_hit = 0;
      } else if (since_hit <= length) {
        ++since_hit;
      }
      if (!in_range) {
        continue;
      }
      auto swept = deque[front].value;
      if (fade > 0.0F && swept > 0.0F) {
        const auto factor =
            1.0F - fade * (static_cast<float>(std::min(since_hit, length)) / static_cast<float>(length));
        swept *= std::max(0.0F, factor);
      }
      mask[index] = swept;
    }
  }
}

// Photoshop's drop-shadow falloff is the shared spread-expand + tent pipeline
// above (re-calibrated July 2026: 'dsdw' probes at sizes 5/10/12/17 and
// spreads 0/8/18 matched the tent byte-for-byte where the historical
// triple-box blur was visibly hotter near the contour on thin shapes).
void prepare_layer_style_soft_mask(std::vector<float>& mask, int width, int height, float size, float spread) {
  prepare_photoshop_soft_effect_mask(mask, width, height, size, spread);
}

// Photoshop's outer-glow "Softer" technique (COM-probed July 2026 with square,
// bar, dot, and band renders at sizes 1-40, spreads 0-100, and Range 25-100):
// the shared spread-expand + tent pipeline above, then the Quality > Range
// ('Inpr') gain: mask = min(1, blur x 100/range), the Linear-contour case.
// PS's UI default is 50, so typical files double the raw blur; the Range 25
// probe pinned the x4 saturation and Range 100 the identity. Straight-edge
// probes matched within 1.4/255; a hard spread-100 band's corner arc differs
// by up to ~1px of arc (chamfer-vs-area-sampling convention).
void prepare_outer_glow_softer_mask(std::vector<float>& mask, int width, int height, float size, float spread,
                                    float range) {
  prepare_photoshop_soft_effect_mask(mask, width, height, size, spread);
  const auto gain = 100.0F / std::clamp(range, 1.0F, 100.0F);
  if (gain > 1.0F) {
    for (auto& value : mask) {
      value = std::min(1.0F, value * gain);
    }
  }
}

// The interior mirror of the exterior pipeline above (COM-probed July 2026 with
// square, bar, dot, hole, and small-square renders at sizes 1-40 and chokes
// 0-100; straight edges, the choke sweep, and the 5x5 dot's 2D corners matched
// Photoshop byte-for-byte): the INVERSE matte expands by the integer choke
// radius lround(choke% x size) and the remaining size blurs with the tent
// N = max(2, lround(size)) - choke radius. Choke 100 or size 0 leaves the hard
// Euclidean band; sizes 1-2 still blur with the N=2 tent. Turns the shape's
// alpha matte into the interior falloff field, 1 at the contour fading to 0
// inside. Inner shadows use this raw field; inner glows add Range below.
void prepare_photoshop_interior_soft_mask(std::vector<float>& mask, int width, int height, float size,
                                          float choke) {
  const auto rounded_size = std::lround(std::max(0.0F, size));
  const auto choke_radius =
      static_cast<int>(std::lround(std::max(0.0F, size) * clamp_unit(choke / 100.0F)));
  for (auto& value : mask) {
    value = 1.0F - clamp_unit(value);
  }
  dilate_layer_style_mask_by_radius(mask, width, height, choke_radius);
  if (rounded_size > 0) {
    const auto tent_peak = std::max<long>(2, rounded_size) - choke_radius;
    if (tent_peak >= 2) {
      blur_satin_tent_mask_in_place(mask, width, height, static_cast<float>(tent_peak));
    }
  }
}

// Photoshop's inner-glow "Softer" technique: the interior falloff above, then
// the Quality > Range ('Inpr') gain min(1, v x 100/range) - identical to the
// outer glow's, applied AFTER the blur (the choke-50 x Range-50 probe pinned
// the order). The Center source is the exact COMPLEMENT of the gained Edge
// field (Center probes at choke 0/50 and Range 25-100 matched 255 - edge byte
// for byte), which also reproduces the historical blurred-matte look at choke 0
// and the hard geometric erosion at choke 100. A descriptor that omits 'Inpr'
// renders at Range 100; the UI writes 50, so typical files double the raw blur.
void prepare_inner_glow_softer_mask(std::vector<float>& mask, int width, int height, float size,
                                    float choke, float range, bool center_source) {
  prepare_photoshop_interior_soft_mask(mask, width, height, size, choke);
  const auto gain = 100.0F / std::clamp(range, 1.0F, 100.0F);
  if (center_source) {
    for (auto& value : mask) {
      value = 1.0F - std::min(1.0F, value * gain);
    }
  } else if (gain > 1.0F) {
    for (auto& value : mask) {
      value = std::min(1.0F, value * gain);
    }
  }
}

// The interior effects' historical blur: 3 box passes of half the size each.
int interior_style_blur_radius(float size) noexcept {
  return std::max(0, static_cast<int>(std::lround(size * 0.5F)));
}

// Photoshop's inner-shadow/inner-glow Choke is the interior mirror of the
// drop-shadow Spread (COM-probed July 2026 with choke 0/50/100 renders): the
// inverse matte expands with rounded Euclidean corners to choke% x size and only
// the remaining (1 - choke%) x size is blurred, so choke 100 leaves a hard
// Euclidean band exactly `size` deep. Do not reimplement choke as a post-blur
// gain ((1 - blur) / (1 - choke)): amplifying the box blur's tail exposes the
// kernel's square support (a small transparent hole radiates a ~1.5 x size
// rounded box of half-tone dust instead of a size-radius disc). Turns the
// shape's alpha mask into the interior falloff field, 1 at the contour fading to
// 0 inside; choke 0 keeps the historical blur-and-invert bit for bit.
void prepare_layer_style_interior_falloff_mask(std::vector<float>& mask, int width, int height, float size,
                                                      float choke) {
  const auto choke_unit = clamp_unit(choke / 100.0F);
  if (choke_unit <= 0.0F) {
    blur_mask_in_place(mask, width, height, interior_style_blur_radius(size), 3);
    for (auto& value : mask) {
      value = clamp_unit(1.0F - value);
    }
    return;
  }

  for (auto& value : mask) {
    value = 1.0F - clamp_unit(value);
  }
  expand_layer_style_mask_in_place(mask, width, height, std::max(0.0F, size) * choke_unit, 1.0F);
  blur_mask_in_place(mask, width, height, interior_style_blur_radius(size * (1.0F - choke_unit)), 3);
  for (auto& value : mask) {
    value = clamp_unit(value);
  }
}

float smoothstep_unit(float value) noexcept {
  value = clamp_unit(value);
  return value * value * (3.0F - 2.0F * value);
}

float layer_style_falloff_alpha(float distance, float size, float spread) noexcept {
  const auto radius = std::max(0.0F, size);
  if (radius <= 0.0F) {
    return distance <= 0.0F ? 1.0F : 0.0F;
  }
  if (distance > radius) {
    return 0.0F;
  }

  const auto spread_unit = clamp_unit(spread / 100.0F);
  const auto solid_radius = radius * spread_unit;
  if (distance <= solid_radius || spread_unit >= 0.999F) {
    return 1.0F;
  }

  const auto fade_width = std::max(0.001F, radius - solid_radius);
  return 1.0F - smoothstep_unit((distance - solid_radius) / fade_width);
}

void relax_distance(float& distance, float& strength, float candidate_distance,
                           float candidate_strength) noexcept {
  if (candidate_strength <= 0.0F) {
    return;
  }
  constexpr float kEqualDistanceTolerance = 0.001F;
  if (candidate_distance + kEqualDistanceTolerance < distance ||
      (std::abs(candidate_distance - distance) <= kEqualDistanceTolerance && candidate_strength > strength)) {
    distance = candidate_distance;
    strength = candidate_strength;
  }
}

std::vector<float> layer_style_source_strengths(const std::vector<float>& input, int width, int height) {
  std::vector<float> strengths(input.size(), 0.0F);
  std::vector<std::uint8_t> visited(input.size(), 0U);
  std::vector<std::size_t> stack;
  std::vector<std::size_t> component;
  stack.reserve(256);
  component.reserve(256);

  for (std::size_t start = 0; start < input.size(); ++start) {
    if (visited[start] != 0U || input[start] <= 0.0F) {
      continue;
    }

    stack.clear();
    component.clear();
    stack.push_back(start);
    visited[start] = 1U;
    float component_strength = clamp_unit(input[start]);
    while (!stack.empty()) {
      const auto index = stack.back();
      stack.pop_back();
      component.push_back(index);
      component_strength = std::max(component_strength, clamp_unit(input[index]));

      const auto x = static_cast<int>(index % static_cast<std::size_t>(width));
      const auto y = static_cast<int>(index / static_cast<std::size_t>(width));
      for (int ny = std::max(0, y - 1); ny <= std::min(height - 1, y + 1); ++ny) {
        for (int nx = std::max(0, x - 1); nx <= std::min(width - 1, x + 1); ++nx) {
          if (nx == x && ny == y) {
            continue;
          }
          const auto neighbor =
              static_cast<std::size_t>(ny) * static_cast<std::size_t>(width) + static_cast<std::size_t>(nx);
          if (visited[neighbor] != 0U || input[neighbor] <= 0.0F) {
            continue;
          }
          visited[neighbor] = 1U;
          stack.push_back(neighbor);
        }
      }
    }

    for (const auto index : component) {
      strengths[index] = component_strength;
    }
  }
  return strengths;
}

// Chamfer (1 / sqrt(2)) distance to the nearest painted (alpha > 0) pixel of `input`,
// carrying the per-component source strength alongside. Deterministic scan-order float
// relaxation shared by the outer-glow falloff and the drop-shadow spread expansion.
void chamfer_distance_and_strengths(const std::vector<float>& input, int width, int height,
                                           std::vector<float>& distances, std::vector<float>& strengths) {
  constexpr float kInfinity = 1.0e20F;
  constexpr float kDiagonalDistance = 1.41421356237F;
  const auto source_strengths = layer_style_source_strengths(input, width, height);
  distances.assign(input.size(), kInfinity);
  strengths.assign(input.size(), 0.0F);
  for (std::size_t index = 0; index < input.size(); ++index) {
    if (input[index] > 0.0F) {
      distances[index] = 0.0F;
      strengths[index] = clamp_unit(source_strengths[index]);
    }
  }

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
      auto& distance = distances[index];
      auto& strength = strengths[index];
      if (x > 0) {
        const auto candidate = index - 1U;
        relax_distance(distance, strength, distances[candidate] + 1.0F, strengths[candidate]);
      }
      if (y > 0) {
        const auto candidate = index - static_cast<std::size_t>(width);
        relax_distance(distance, strength, distances[candidate] + 1.0F, strengths[candidate]);
      }
      if (x > 0 && y > 0) {
        const auto candidate = index - static_cast<std::size_t>(width) - 1U;
        relax_distance(distance, strength, distances[candidate] + kDiagonalDistance, strengths[candidate]);
      }
      if (x + 1 < width && y > 0) {
        const auto candidate = index - static_cast<std::size_t>(width) + 1U;
        relax_distance(distance, strength, distances[candidate] + kDiagonalDistance, strengths[candidate]);
      }
    }
  }

  for (int y = height - 1; y >= 0; --y) {
    for (int x = width - 1; x >= 0; --x) {
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
      auto& distance = distances[index];
      auto& strength = strengths[index];
      if (x + 1 < width) {
        const auto candidate = index + 1U;
        relax_distance(distance, strength, distances[candidate] + 1.0F, strengths[candidate]);
      }
      if (y + 1 < height) {
        const auto candidate = index + static_cast<std::size_t>(width);
        relax_distance(distance, strength, distances[candidate] + 1.0F, strengths[candidate]);
      }
      if (x + 1 < width && y + 1 < height) {
        const auto candidate = index + static_cast<std::size_t>(width) + 1U;
        relax_distance(distance, strength, distances[candidate] + kDiagonalDistance, strengths[candidate]);
      }
      if (x > 0 && y + 1 < height) {
        const auto candidate = index + static_cast<std::size_t>(width) - 1U;
        relax_distance(distance, strength, distances[candidate] + kDiagonalDistance, strengths[candidate]);
      }
    }
  }
}

std::vector<float> distance_falloff_mask(const std::vector<float>& input, int width, int height,
                                                float size, float spread) {
  std::vector<float> distances;
  std::vector<float> strengths;
  chamfer_distance_and_strengths(input, width, height, distances, strengths);
  for (std::size_t index = 0; index < distances.size(); ++index) {
    distances[index] = strengths[index] * layer_style_falloff_alpha(distances[index], size, spread);
  }
  return distances;
}

// Expands the matte for the drop-shadow Spread: full component strength out to
// `radius`, then a 1px anti-aliasing ramp (the stroke-band contour convention).
// `radius` is in output pixels; `pixels_per_unit` maps them to mask pixels so the
// supersampled path expands in scaled space while keeping the 1px ramp width.
void expand_layer_style_mask_in_place(std::vector<float>& mask, int width, int height, float radius,
                                             float pixels_per_unit) {
  if (radius <= 0.0F || mask.empty()) {
    return;
  }
  std::vector<float> distances;
  std::vector<float> strengths;
  chamfer_distance_and_strengths(mask, width, height, distances, strengths);
  for (std::size_t index = 0; index < mask.size(); ++index) {
    const auto coverage = clamp_unit(radius + 1.0F - distances[index] / pixels_per_unit);
    mask[index] = std::max(mask[index], strengths[index] * coverage);
  }
}

// A bevel technique produces one continuous height field: 0 on the exterior,
// 1 on the interior. Styles decide which side is visible (or reshape it into a
// pillow) without changing the lighting math. Keeping the fractional matte in
// this stage is important: treating every non-zero edge pixel as a binary EDT
// seed creates the one-pixel zipper normals that Smooth is meant to avoid.
std::vector<float> bevel_technique_height_mask(const std::vector<float>& alpha_mask, int width, int height,
                                                      const LayerBevelEmboss& bevel) {
  std::vector<float> height_mask(alpha_mask.size(), 0.0F);
  const auto size = std::max(0.01F, bevel.size);
  if (bevel.technique == BevelTechnique::Smooth) {
    // Smooth's height field is Satin's exact tent blur of the matte, the same
    // engine the shadow/glow falloffs use (COM-probed July 2026: the recovered
    // slope profile of PS's smooth inner bevel is exactly the tent's linear
    // ramp; the old triple-box spread the shading wider with dimmer slopes).
    height_mask = alpha_mask;
    blur_satin_tent_mask_in_place(height_mask, width, height, size);
  } else {
    const auto distance_to_painted = stroke_distance_field(alpha_mask, width, height, true);
    const auto distance_to_clear = stroke_distance_field(alpha_mask, width, height, false);
    for (std::size_t index = 0; index < alpha_mask.size(); ++index) {
      const auto alpha = clamp_unit(alpha_mask[index]);
      const auto inside = 0.5F + 0.5F * clamp_unit(distance_to_clear[index] / size);
      const auto outside = 0.5F - 0.5F * clamp_unit(distance_to_painted[index] / size);
      height_mask[index] = outside * (1.0F - alpha) + inside * alpha;
    }
    if (bevel.technique == BevelTechnique::ChiselSoft) {
      // Chisel Soft retains the exact-distance roof but rounds its pixel-scale
      // facets. It is deliberately much narrower than Smooth's size-wide blur.
      blur_mask_in_place(height_mask, width, height, 1, 1);
    }
  }
  if (bevel.soften > 0.0F) {
    blur_layer_style_mask_in_place(height_mask, width, height, bevel.soften);
  }
  for (auto& value : height_mask) {
    value = clamp_unit(value);
  }
  return height_mask;
}

int satin_tent_peak(float size) noexcept {
  if (size <= 0.0F) {
    return 0;
  }
  return std::max(2, static_cast<int>(std::lround(size)));
}

// Photoshop's Satin blur is the exact separable tent [1..N..1] / N^2,
// N=max(2, Size), rather than the three-box approximation shared by the other
// soft layer effects. Prefix sums keep the exact kernel bounded to O(pixels)
// even at large Size values. Size zero bypasses the blur altogether.
void blur_satin_tent_mask_in_place(std::vector<float>& mask, int width, int height, float size) {
  const auto peak = satin_tent_peak(size);
  if (peak == 0 || width <= 0 || height <= 0 || mask.empty()) {
    return;
  }

  const auto convolve_lines = [peak](const std::vector<float>& input, std::vector<float>& output, int line_count,
                                     int line_length, std::size_t line_step, std::size_t sample_step) {
    std::vector<double> prefix(static_cast<std::size_t>(line_length) + 1U, 0.0);
    std::vector<double> weighted_prefix(static_cast<std::size_t>(line_length) + 1U, 0.0);
    const auto divisor = static_cast<double>(peak) * static_cast<double>(peak);
    for (int line = 0; line < line_count; ++line) {
      const auto base = static_cast<std::size_t>(line) * line_step;
      prefix[0] = 0.0;
      weighted_prefix[0] = 0.0;
      for (int position = 0; position < line_length; ++position) {
        const auto value = static_cast<double>(input[base + static_cast<std::size_t>(position) * sample_step]);
        prefix[static_cast<std::size_t>(position) + 1U] = prefix[static_cast<std::size_t>(position)] + value;
        weighted_prefix[static_cast<std::size_t>(position) + 1U] =
            weighted_prefix[static_cast<std::size_t>(position)] + static_cast<double>(position) * value;
      }

      for (int position = 0; position < line_length; ++position) {
        const auto left = std::max(0, position - peak + 1);
        const auto right = std::min(line_length, position + peak);
        const auto left_sum = prefix[static_cast<std::size_t>(position) + 1U] - prefix[static_cast<std::size_t>(left)];
        const auto left_weighted = weighted_prefix[static_cast<std::size_t>(position) + 1U] -
                                   weighted_prefix[static_cast<std::size_t>(left)];
        const auto right_sum = prefix[static_cast<std::size_t>(right)] -
                               prefix[static_cast<std::size_t>(position) + 1U];
        const auto right_weighted = weighted_prefix[static_cast<std::size_t>(right)] -
                                    weighted_prefix[static_cast<std::size_t>(position) + 1U];
        const auto numerator = (static_cast<double>(peak - position) * left_sum + left_weighted) +
                               (static_cast<double>(peak + position) * right_sum - right_weighted);
        output[base + static_cast<std::size_t>(position) * sample_step] =
            static_cast<float>(numerator / divisor);
      }
    }
  };

  std::vector<float> horizontal(mask.size(), 0.0F);
  std::vector<float> output(mask.size(), 0.0F);
  convolve_lines(mask, horizontal, height, width, static_cast<std::size_t>(width), 1U);
  convolve_lines(horizontal, output, width, height, 1U, static_cast<std::size_t>(width));
  mask.swap(output);
}

std::vector<float> satin_alpha_mask(const PixelBuffer& source, const Layer& layer, Rect bounds,
                                           Rect mask_bounds, int offset_x, int offset_y, float size, bool invert,
                                           std::optional<Rect> layer_mask_bounds) {
  // Two copies of the layer matte move in opposite directions. Photoshop blurs
  // their signed difference before taking its absolute value; folding first
  // would incorrectly join overlapping lobes.
  auto mask = layer_alpha_mask(source, layer, bounds, mask_bounds, offset_x, offset_y, layer_mask_bounds);
  const auto opposite =
      layer_alpha_mask(source, layer, bounds, mask_bounds, -offset_x, -offset_y, layer_mask_bounds);
  for (std::size_t index = 0; index < mask.size(); ++index) {
    mask[index] -= opposite[index];
  }
  blur_satin_tent_mask_in_place(mask, mask_bounds.width, mask_bounds.height, size);
  for (auto& value : mask) {
    value = clamp_unit(std::abs(value));
    if (invert) {
      value = 1.0F - value;
    }
  }
  return mask;
}

PreparedSatin prepare_satin(const Layer& layer, const PixelBuffer& source, Rect draw_rect, Rect bounds,
                                   const LayerSatin& satin, std::optional<Rect> layer_mask_bounds,
                                   StyleMaskProvider* masks, std::uint32_t effect_index) {
  constexpr float kPi = 3.14159265358979323846F;
  const auto radians = (180.0F - satin.angle_degrees) * kPi / 180.0F;
  // Photoshop clamps a requested zero Distance to one pixel. Keeping the
  // offset integral matches its raster descriptor and Patchy's other effects.
  const auto distance = std::max(1.0F, satin.distance);
  const auto offset_x = static_cast<int>(std::lround(std::cos(radians) * distance));
  const auto offset_y = static_cast<int>(std::lround(std::sin(radians) * distance));
  const auto peak = satin_tent_peak(satin.size);
  const auto sample_padding = peak > 0 ? peak - 1 : 0;
  const auto full_domain = outset_rect(bounds, sample_padding);
  const auto legacy_mask_bounds = clipped_mask_bounds(full_domain, draw_rect, sample_padding);
  auto [entry, mask_bounds] = style_mask_for_render(
      masks, layer, StyleMaskKind::Satin, effect_index, full_domain, bounds, legacy_mask_bounds, bounds,
      layer_mask_bounds, [&](Rect domain) {
        StyleMaskEntry computed;
        computed.primary = satin_alpha_mask(source, layer, bounds, domain, offset_x, offset_y, satin.size,
                                            satin.invert, layer_mask_bounds);
        return computed;
      });
  return PreparedSatin{&satin, std::move(entry), mask_bounds};
}

// Folds the layer's vector and raster mask factors over draw_rect into one
// float plane, row-walked with raw pointers instead of the per-pixel
// layer_mask_alpha_for_render call chain. The values are bit-identical to that
// chain: the expressions below keep layer_mask_alpha_at's exact float
// operation order (vector cache byte / 255, coverage * density + (1 -
// density), then (vector_alpha * mask byte) / 255 with the division LAST), and
// every disabled / non-gray8 / out-of-bounds edge lands on the same constant
// expression. Pinned compositor bytes depend on this equivalence. The only
// deliberate deviation: a baked cache whose bounds exceed its buffer would
// throw in PixelBuffer::pixel(); here the inside span clips to the buffer so
// the walk stays in memory (that state is unreachable from valid bakes).
std::vector<float> build_mask_coverage_plane(const Layer& layer, Rect draw_rect,
                                                    std::optional<Rect> mask_bounds_override) {
  const auto plane_width = static_cast<std::size_t>(draw_rect.width);
  std::vector<float> plane(plane_width * static_cast<std::size_t>(draw_rect.height), 1.0F);
  const auto row_begin = draw_rect.x;
  const auto row_end = draw_rect.x + draw_rect.width;

  const auto* vector_mask = layer.vector_mask();
  if (vector_mask != nullptr && !vector_mask->disabled) {
    const auto density = static_cast<float>(vector_mask->density) / 255.0F;
    const bool cache_valid = !vector_mask->cache_bounds.empty() && !vector_mask->cache.empty() &&
                             vector_mask->cache.format() == PixelFormat::gray8();
    const auto fallback_coverage = vector_mask->path.empty() && !vector_mask->inverted ? 1.0F : 0.0F;
    const auto fallback_value = fallback_coverage * density + (1.0F - density);
    const auto cache_bounds = vector_mask->cache_bounds;
    const auto cache_rows =
        cache_valid ? std::min(cache_bounds.height, vector_mask->cache.height()) : 0;
    const auto inside_begin = cache_valid ? std::clamp(cache_bounds.x, row_begin, row_end) : row_begin;
    const auto inside_end =
        cache_valid
            ? std::clamp(cache_bounds.x + std::min(cache_bounds.width, vector_mask->cache.width()), row_begin, row_end)
            : row_begin;
    auto* row_out = plane.data();
    for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y, row_out += plane_width) {
      if (!cache_valid || y < cache_bounds.y || y >= cache_bounds.y + cache_rows) {
        std::fill(row_out, row_out + plane_width, fallback_value);
        continue;
      }
      const auto* cache_row = vector_mask->cache.row(y - cache_bounds.y).data();
      std::fill(row_out, row_out + static_cast<std::size_t>(inside_begin - row_begin), fallback_value);
      for (std::int32_t x = inside_begin; x < inside_end; ++x) {
        const auto coverage = static_cast<float>(cache_row[x - cache_bounds.x]) / 255.0F;
        row_out[x - row_begin] = coverage * density + (1.0F - density);
      }
      std::fill(row_out + static_cast<std::size_t>(inside_end - row_begin), row_out + plane_width, fallback_value);
    }
  }

  const auto& mask = layer.mask();
  if (mask.has_value() && !mask->disabled) {
    auto mask_bounds = mask_bounds_override.value_or(mask->bounds);
    // A feathered mask samples its cached blurred plane (wider than the
    // painted one) in place of the painted pixels; see feathered_layer_mask.
    const auto feathered = feathered_layer_mask(layer);
    const auto& mask_pixels = feathered != nullptr ? feathered->pixels : mask->pixels;
    if (feathered != nullptr) {
      mask_bounds = Rect{mask_bounds.x + feathered->offset_x, mask_bounds.y + feathered->offset_y,
                         mask_pixels.width(), mask_pixels.height()};
    }
    const auto default_value = static_cast<float>(mask->default_color);
    if (!mask_pixels.empty() && mask_pixels.format().channels == 1 &&
        mask_pixels.format().bit_depth != BitDepth::UInt8) {
      // A 16/32-bit mask or its 16-bit feather (docs/high-bit-depth.md): the same
      // factors as layer_mask_alpha_at's deep branch.
      const auto density = static_cast<float>(mask->density) / 255.0F;
      auto* row_out = plane.data();
      for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y, row_out += plane_width) {
        for (std::int32_t x = row_begin; x < row_end; ++x) {
          auto& value = row_out[x - row_begin];
          const auto local_x = x - mask_bounds.x;
          const auto local_y = y - mask_bounds.y;
          if (local_x < 0 || local_y < 0 || local_x >= std::min(mask_bounds.width, mask_pixels.width()) ||
              local_y >= std::min(mask_bounds.height, mask_pixels.height())) {
            value = mask->density == 255 ? value * default_value / 255.0F
                                         : value * (default_value / 255.0F * density + (1.0F - density));
            continue;
          }
          const auto coverage = coverage_at(mask_pixels, local_x, local_y);
          value = mask->density == 255 ? value * coverage : value * (coverage * density + (1.0F - density));
        }
      }
      return plane;
    }
    const bool pixels_valid = !mask_pixels.empty() && mask_pixels.format() == PixelFormat::gray8();
    // layer_mask_alpha_at treats pixels beyond the buffer (bounds wider than
    // the allocation) as default-colored, so the inside span clips to both.
    const auto inside_rows = pixels_valid ? std::min(mask_bounds.height, mask_pixels.height()) : 0;
    const auto inside_begin = pixels_valid ? std::clamp(mask_bounds.x, row_begin, row_end) : row_begin;
    const auto inside_end =
        pixels_valid
            ? std::clamp(mask_bounds.x + std::min(mask_bounds.width, mask_pixels.width()), row_begin, row_end)
            : row_begin;
    // Mask density: layer_mask_alpha_at's expressions, default density first
    // (the pinned legacy operation order), then the density-lifted form.
    const bool full_density = mask->density == 255;
    const auto density = static_cast<float>(mask->density) / 255.0F;
    const auto masked = [full_density, density](float value, float mask_value) {
      return full_density ? value * mask_value / 255.0F
                          : value * (mask_value / 255.0F * density + (1.0F - density));
    };
    auto* row_out = plane.data();
    for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y, row_out += plane_width) {
      if (!pixels_valid || y < mask_bounds.y || y >= mask_bounds.y + inside_rows) {
        for (std::size_t index = 0; index < plane_width; ++index) {
          row_out[index] = masked(row_out[index], default_value);
        }
        continue;
      }
      const auto* mask_row = mask_pixels.row(y - mask_bounds.y).data();
      for (std::int32_t x = row_begin; x < inside_begin; ++x) {
        auto& value = row_out[x - row_begin];
        value = masked(value, default_value);
      }
      for (std::int32_t x = inside_begin; x < inside_end; ++x) {
        auto& value = row_out[x - row_begin];
        value = masked(value, static_cast<float>(mask_row[x - mask_bounds.x]));
      }
      for (std::int32_t x = inside_end; x < row_end; ++x) {
        auto& value = row_out[x - row_begin];
        value = masked(value, default_value);
      }
    }
  }
  return plane;
}

std::vector<float> stroke_alpha_mask(const PixelBuffer& source, const Layer& layer, Rect bounds,
                                            Rect mask_bounds, float size, LayerStrokePosition position,
                                            std::optional<Rect> layer_mask_bounds, bool mask_shapes_source,
                                            std::vector<float>* shape_burst_positions, StrokeMaskPlanes* planes) {
  // Photoshop derives the stroke from the layer's pixel coverage, treating any painted
  // pixel as inside the shape: the stroke fills the (dilated) binary shape and the
  // layer's own pixels cover it according to their alpha, so semi-transparent fills let
  // it show through. By default the layer mask reshapes that coverage where it is fully
  // black — the contour follows {alpha > 0 AND mask > 0} and the band lands on
  // mask-hidden ground at full strength. A mask whose "Hides Effects" option is on
  // (layer or vector mask, each on its own) is left out of the contour, and the caller
  // hides the output where it lands instead; mask_shapes_source = false leaves every
  // mask out. Fractional (gray/feathered) mask values deliberately keep
  // the raw pixel alpha: Photoshop folds them into the matte with content-knockout
  // compositing (a gray mask washes the whole interior), the same knockout model this
  // renderer already skips for semi-transparent fills (docs/ps-compat.md). Calibrated
  // against Photoshop 2026 COM renders, July 2026: binary-mask band runs match PS
  // run-for-run at every position, including bands over fully-masked ground.
  //
  // The band is measured with an exact Euclidean distance field from the binary contour:
  // Outside reaches `size` px outward, Inside `size` px inward, Center `size/2` px each
  // way (the legacy dilation used the full size both ways, rendering Center at double
  // width). Coverage is `alpha * in-band + (1 - alpha) * out-band` — the sum keeps the
  // band seamless across anti-aliased contour pixels where alpha is fractional.
  const auto width = mask_bounds.width;
  const auto height = mask_bounds.height;
  std::vector<float> base(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0F);
  if (!source.empty() && source.format().channels >= 4) {
    const auto format = source.format();
    const auto* bytes = source.data().data();
    const auto stride = source.stride_bytes();
    const auto draw_left = std::max(mask_bounds.x, bounds.x);
    const auto draw_top = std::max(mask_bounds.y, bounds.y);
    const auto draw_right = std::min(mask_bounds.x + mask_bounds.width, bounds.x + source.width());
    const auto draw_bottom = std::min(mask_bounds.y + mask_bounds.height, bounds.y + source.height());
    const bool deep_source = format.bit_depth != BitDepth::UInt8;
    for (std::int32_t y = draw_top; y < draw_bottom; ++y) {
      const auto* source_row = deep_source ? nullptr : bytes + static_cast<std::size_t>(y - bounds.y) * stride;
      auto* output = base.data() + static_cast<std::size_t>(y - mask_bounds.y) * width + (draw_left - mask_bounds.x);
      for (std::int32_t x = draw_left; x < draw_right; ++x) {
        auto alpha = deep_source
                         ? pixel_alpha_at(source, x - bounds.x, y - bounds.y)
                         : static_cast<float>(source_row[static_cast<std::size_t>(x - bounds.x) * format.channels + 3U]) /
                               255.0F;
        // The mask's value is part of the content's alpha (a feathered mask makes
        // the content semi-transparent, and the semi-transparent rules apply:
        // psd-tools' feathered-stroke.psd, whose pixels are opaque under a soft
        // mask). Binary masks keep the pinned {alpha > 0 AND mask > 0} contour.
        if (mask_shapes_source && alpha > 0.0F) {
          alpha *= clamp_unit(effect_source_mask_alpha_for_render(layer, x, y, layer_mask_bounds));
        }
        *output++ = alpha;
      }
    }
  } else if (!source.empty()) {
    // Opaque formats: the shape is the layer bounds.
    const auto draw_left = std::max(mask_bounds.x, bounds.x);
    const auto draw_top = std::max(mask_bounds.y, bounds.y);
    const auto draw_right = std::min(mask_bounds.x + mask_bounds.width, bounds.x + source.width());
    const auto draw_bottom = std::min(mask_bounds.y + mask_bounds.height, bounds.y + source.height());
    for (std::int32_t y = draw_top; y < draw_bottom; ++y) {
      auto* output = base.data() + static_cast<std::size_t>(y - mask_bounds.y) * width + (draw_left - mask_bounds.x);
      for (std::int32_t x = draw_left; x < draw_right; ++x) {
        *output++ = mask_shapes_source
                        ? clamp_unit(effect_source_mask_alpha_for_render(layer, x, y, layer_mask_bounds))
                        : 1.0F;
      }
    }
  }

  const auto band_out = position == LayerStrokePosition::Inside   ? 0.0F
                        : position == LayerStrokePosition::Center ? size * 0.5F
                                                                  : size;
  const auto band_in = position == LayerStrokePosition::Outside  ? 0.0F
                       : position == LayerStrokePosition::Center ? size * 0.5F
                                                                 : size;
  // Photoshop's band geometry on anti-aliased and soft edges (25 PS 2026 probes:
  // feathered squares at feather 0/1/2/4/8 px with Outside 2/5/10, Inside 5 and
  // Center 6 strokes, local-test-fixtures/stroke-soft-probes, October 2026,
  // GitHub issue 261). The OUTER band measures from the pixel centers of every
  // painted pixel (alpha > 0, a 1/255 fringe included) and its last ring reads
  // that nearest pixel's alpha: coverage = clamp(band + e - d). The INNER band
  // measures from the pixel centers of every pixel below full alpha and its last
  // ring reads that pixel's missing alpha: coverage = clamp(band + (1 - e) - d).
  // A binary matte keeps the pinned "contour half a pixel past the pixel
  // center plus a 1 px ramp" (e is 1 outside, 0 inside). Every pixel in between
  // (0 < a < 1) is the two-paint case below whatever its distance from the
  // opaque core: the outer band sits under all of them and the inner band
  // replaces them (an 8 px feather shows the stroke under its whole fringe).
  // The earlier half-coverage anchor read the ring from the opaque interior and
  // sat the band about a pixel inside Photoshop's on soft edges.
  std::vector<float> outside_distance;
  std::vector<float> outside_edge_alpha;
  std::vector<float> inside_distance;
  std::vector<float> inside_edge_alpha;
  if (band_out > 0.0F) {
    stroke_distance_field_with_edge_alpha(base, width, height, true, outside_distance, outside_edge_alpha);
  }
  if (band_in > 0.0F) {
    stroke_distance_field_with_edge_alpha(base, width, height, false, inside_distance, inside_edge_alpha);
  }

  std::vector<float> mask(base.size(), 0.0F);
  // Semi-transparent content (a feathered edge, a 50% fill, a gradient fading to
  // nothing; Photoshop 2026 probes on a square whose alpha ramps 1 -> 0 and on a
  // flat 50% square, October 2026, docs/layer-effects-render.md "Strokes on
  // semi-transparent content"): the outer band takes the pixel's missing alpha
  // (1 - a) UNDER the content, as a second paint beside it, and the inner band
  // treats every such pixel as band at coverage a, replacing the content. When the
  // caller asks for the planes, the under-content part moves out of the over-draw
  // mask into `underlay` and the band membership into `knockout`; without them the
  // mask keeps the legacy sum (the band over the content), which every opaque matte
  // renders identically.
  const bool split_planes = planes != nullptr && planes->underlay != nullptr && planes->inside != nullptr &&
                            planes->knockout != nullptr;
  if (split_planes) {
    planes->underlay->assign(base.size(), 0.0F);
    planes->inside->assign(base.size(), 0.0F);
    planes->knockout->assign(base.size(), 0.0F);
    planes->partial_content = false;
  }
  for (std::size_t index = 0; index < base.size(); ++index) {
    const auto center_alpha = base[index];
    const auto outside_coverage =
        outside_distance.empty()
            ? 0.0F
            : clamp_unit(band_out + outside_edge_alpha[index] - outside_distance[index]);
    auto inside_coverage =
        inside_distance.empty()
            ? 0.0F
            : clamp_unit(band_in + (1.0F - inside_edge_alpha[index]) - inside_distance[index]);
    const bool partial = center_alpha > 0.0F && center_alpha < 1.0F;
    if (split_planes && partial && !inside_distance.empty()) {
      inside_coverage = 1.0F;
    }
    if (split_planes && partial) {
      // Both paints of such a pixel fold into the base pass as disjoint shares
      // (the Center probe: (1 - a) under the content plus a replacing it make the
      // whole square the stroke color); the over-draw mask leaves it alone.
      planes->partial_content = true;
      (*planes->underlay)[index] = clamp_unit((1.0F - center_alpha) * outside_coverage);
      (*planes->inside)[index] = clamp_unit(center_alpha * inside_coverage);
      // With Overprint on, opaque content keeps its band (no knockout); the
      // semi-transparent part is replaced all the same.
      (*planes->knockout)[index] = inside_coverage;
      mask[index] = 0.0F;
      continue;
    }
    if (split_planes) {
      (*planes->knockout)[index] = planes->overprint ? 0.0F : inside_coverage;
    }
    mask[index] = clamp_unit(center_alpha * inside_coverage + (1.0F - center_alpha) * outside_coverage);
  }
  if (split_planes && !planes->partial_content) {
    planes->underlay->clear();
    planes->inside->clear();
    planes->knockout->clear();
  }

  if (shape_burst_positions != nullptr) {
    // Photoshop's Shape Burst gradient ramp is LINEAR in the same anchored
    // distance field: position 0 at the band's outer limit, 1 at its inner
    // limit, one continuous span across both halves of a Center stroke. Each
    // side that is a band limit (not the contour itself) extends by the same
    // +1 px the coverage reaches (kStrokeContourOffset), so an Outside or
    // Inside stroke spans size+1 and a Center stroke spans size+2. Pinned by
    // the photoshop-stroke-shapeburst fixtures within +/-1/255 (COM probes,
    // July 2026).
    // The ramp keeps its own anchor: the matte's bilinear half-coverage crossing
    // (stroke_subpixel_distance_fields), pinned by the photoshop-stroke-aa-matte
    // Shape Burst ellipse; a binary matte reads the same fields as the band.
    // A binary matte reads the band's own pixel-center fields, bit for bit.
    const bool has_fringe = std::any_of(base.begin(), base.end(), [](float a) { return a > 0.0F && a < 1.0F; });
    std::vector<float> ramp_outside;
    std::vector<float> ramp_inside;
    if (has_fringe) {
      stroke_subpixel_distance_fields(base, width, height, band_out > 0.0F, band_in > 0.0F, ramp_outside,
                                      ramp_inside);
    } else {
      ramp_outside = outside_distance;
      ramp_inside = inside_distance;
    }
    auto& positions = *shape_burst_positions;
    positions.assign(base.size(), 0.0F);
    const auto outer_reach = band_out > 0.0F ? band_out + kStrokeContourOffset : 0.0F;
    const auto inner_reach = band_in > 0.0F ? band_in + kStrokeContourOffset : 0.0F;
    const auto span = std::max(1.0F, outer_reach + inner_reach);
    for (std::size_t index = 0; index < base.size(); ++index) {
      float along;
      if (base[index] >= 0.5F) {
        along = ramp_inside.empty() ? span : band_out + ramp_inside[index];
      } else {
        along = ramp_outside.empty() ? 0.0F : outer_reach - ramp_outside[index];
      }
      positions[index] = clamp_unit(along / span);
    }
  }
  return mask;
}

}  // namespace patchy::render_detail
