#include "render/layer_style_mask_ops.hpp"

#include "render/layer_compositor.hpp"

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
void squared_distance_transform_1d(const float* f, float* d, int* v, double* z, int n) {
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
  }
}

void exact_squared_distance_transform(std::vector<float>& field, int width, int height) {
  if (width <= 0 || height <= 0) {
    return;
  }
  const auto n = std::max(width, height);
  std::vector<float> f(static_cast<std::size_t>(n), 0.0F);
  std::vector<float> d(static_cast<std::size_t>(n), 0.0F);
  std::vector<int> v(static_cast<std::size_t>(n), 0);
  std::vector<double> z(static_cast<std::size_t>(n) + 1U, 0.0);
  for (int x = 0; x < width; ++x) {
    for (int y = 0; y < height; ++y) {
      f[static_cast<std::size_t>(y)] = field[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x];
    }
    squared_distance_transform_1d(f.data(), d.data(), v.data(), z.data(), height);
    for (int y = 0; y < height; ++y) {
      field[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x] = d[static_cast<std::size_t>(y)];
    }
  }
  for (int y = 0; y < height; ++y) {
    auto* row = field.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    std::copy(row, row + width, f.data());
    squared_distance_transform_1d(f.data(), d.data(), v.data(), z.data(), width);
    std::copy(d.data(), d.data() + width, row);
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
void prepare_photoshop_soft_effect_mask(std::vector<float>& mask, int width, int height, float size,
                                        float spread) {
  const auto rounded_size = std::lround(std::max(0.0F, size));
  const auto spread_radius =
      static_cast<int>(std::lround(std::max(0.0F, size) * clamp_unit(spread / 100.0F)));
  if (spread_radius > 0 && width > 0 && height > 0 && !mask.empty()) {
    // Bounded exact grayscale dilation. Beyond the limit (rare: spread over
    // ~30% of a large size) the chamfer band keeps the historical behavior;
    // its binarization error is proportionally small at such radii.
    constexpr int kExactDilationRadiusLimit = 8;
    if (spread_radius <= kExactDilationRadiusLimit) {
      const int reach = spread_radius + 1;
      std::vector<float> weights;
      std::vector<int> offsets_x;
      std::vector<int> offsets_y;
      for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
          if (dx == 0 && dy == 0) {
            continue;
          }
          const auto distance = std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy);
          const auto coverage = std::clamp(static_cast<double>(spread_radius) + 1.0 - distance, 0.0, 1.0);
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
      expand_layer_style_mask_in_place(mask, width, height, static_cast<float>(spread_radius), 1.0F);
    }
  }
  if (rounded_size > 0) {
    const auto tent_peak = std::max<long>(2, rounded_size) - spread_radius;
    if (tent_peak >= 2) {
      blur_satin_tent_mask_in_place(mask, width, height, static_cast<float>(tent_peak));
    }
  }
}

}  // namespace

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
    height_mask = alpha_mask;
    blur_layer_style_mask_in_place(height_mask, width, height, size);
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

}  // namespace patchy::render_detail
