#include "core/layer_render_utils.hpp"

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/pixel_buffer.hpp"
#include "core/vector_shape.hpp"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <list>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace patchy {

namespace {

constexpr int kExpensiveStylePadding = 96;

class VisibleAlphaBoundsCache {
public:
  [[nodiscard]] std::optional<Rect> fetch_or_compute(const Layer& layer) {
    const auto revision = layer.pixel_revision();
    {
      std::unique_lock lock(mutex_);
      while (true) {
        if (const auto found = index_.find(revision); found != index_.end()) {
          entries_.splice(entries_.begin(), entries_, found->second);
          return found->second->bounds;
        }
        if (in_flight_.insert(revision).second) {
          break;
        }
        in_flight_done_.wait(lock);
      }
    }

    std::optional<Rect> result;
    try {
      result = visible_alpha_local_bounds(layer.pixels());
    } catch (...) {
      {
        const std::lock_guard lock(mutex_);
        in_flight_.erase(revision);
      }
      in_flight_done_.notify_all();
      throw;
    }

    {
      const std::lock_guard lock(mutex_);
      in_flight_.erase(revision);
      constexpr std::size_t kMaxEntries = 4096U;
      if (entries_.size() >= kMaxEntries) {
        index_.erase(entries_.back().revision);
        entries_.pop_back();
      }
      entries_.push_front(Node{revision, result});
      index_[revision] = entries_.begin();
    }
    in_flight_done_.notify_all();
    return result;
  }

private:
  struct Node {
    std::uint64_t revision{0};
    std::optional<Rect> bounds;
  };

  std::mutex mutex_;
  std::condition_variable in_flight_done_;
  std::list<Node> entries_;
  std::unordered_map<std::uint64_t, std::list<Node>::iterator> index_;
  std::unordered_set<std::uint64_t> in_flight_;
};

VisibleAlphaBoundsCache& visible_alpha_bounds_cache() {
  static VisibleAlphaBoundsCache cache;
  return cache;
}

int layer_style_falloff_radius(float size) noexcept {
  return std::max(0, static_cast<int>(std::ceil(std::max(0.0F, size))));
}

}  // namespace

std::optional<PixelBuffer> document_alpha_rgba8(const Document& document) {
  if (document.layers().size() != 1) {
    return std::nullopt;
  }
  const Layer& layer = document.layers().front();
  if (layer.kind() != LayerKind::Pixel || !layer.children().empty() || !layer_mask_is_document_alpha(layer) ||
      layer_has_enabled_vector_mask(layer)) {
    return std::nullopt;
  }
  const auto& mask = layer.mask();
  if (!mask.has_value() || mask->disabled || mask->pixels.empty() ||
      mask->pixels.format() != PixelFormat::gray8()) {
    return std::nullopt;
  }
  const auto width = document.width();
  const auto height = document.height();
  const PixelBuffer& source = layer.pixels();
  if (source.width() != width || source.height() != height) {
    return std::nullopt;
  }
  const auto source_format = source.format();
  if (source_format.bit_depth != BitDepth::UInt8 ||
      (source_format != PixelFormat::rgb8() && source_format != PixelFormat::rgba8())) {
    return std::nullopt;
  }

  PixelBuffer output(width, height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const std::uint8_t* src = source.pixel(x, y);
      std::uint8_t* dst = output.pixel(x, y);
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      dst[3] = mask->default_color;
    }
  }

  const LayerMask& layer_mask = *mask;
  for (std::int32_t my = 0; my < layer_mask.pixels.height(); ++my) {
    const std::int32_t doc_y = layer_mask.bounds.y + my;
    if (doc_y < 0 || doc_y >= height) {
      continue;
    }
    for (std::int32_t mx = 0; mx < layer_mask.pixels.width(); ++mx) {
      const std::int32_t doc_x = layer_mask.bounds.x + mx;
      if (doc_x < 0 || doc_x >= width) {
        continue;
      }
      output.pixel(doc_x, doc_y)[3] = layer_mask.pixels.pixel(mx, my)[0];
    }
  }
  return output;
}

Rect outset_rect(Rect rect, int amount) noexcept {
  return Rect{rect.x - amount, rect.y - amount, rect.width + amount * 2, rect.height + amount * 2};
}

Rect clipped_mask_bounds(Rect full_bounds, Rect draw_rect, int sample_padding) noexcept {
  return intersect_rect(full_bounds, outset_rect(draw_rect, std::max(0, sample_padding)));
}

Rect layer_pixel_bounds(const Layer& layer) {
  const auto& source = layer.pixels();
  return layer.bounds().empty() ? Rect::from_size(source.width(), source.height()) : layer.bounds();
}

std::optional<Rect> visible_alpha_local_bounds(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8) {
    return std::nullopt;
  }
  if (pixels.format().channels < 4) {
    return Rect::from_size(pixels.width(), pixels.height());
  }

  const auto width = pixels.width();
  const auto height = pixels.height();
  std::int32_t min_x = width;
  std::int32_t min_y = height;
  std::int32_t max_x = -1;
  std::int32_t max_y = -1;
  const auto* bytes = pixels.data().data();
  const auto stride = pixels.stride_bytes();
  const auto channels = pixels.format().channels;
  for (std::int32_t y = 0; y < height; ++y) {
    const auto* alpha_row = bytes + static_cast<std::size_t>(y) * stride + 3U;
    std::int32_t first = -1;
    for (std::int32_t x = 0; x < width; ++x) {
      if (alpha_row[static_cast<std::size_t>(x) * channels] != 0U) {
        first = x;
        break;
      }
    }
    if (first < 0) {
      continue;
    }
    auto last = first;
    for (std::int32_t x = width - 1; x > first; --x) {
      if (alpha_row[static_cast<std::size_t>(x) * channels] != 0U) {
        last = x;
        break;
      }
    }
    min_x = std::min(min_x, first);
    min_y = std::min(min_y, y);
    max_x = std::max(max_x, last);
    max_y = y;
  }
  if (max_x < min_x || max_y < min_y) {
    return std::nullopt;
  }
  return Rect{min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
}

std::optional<Rect> visible_alpha_local_bounds(const Layer& layer) {
  // This scan is reachable from painting through layer effects. Cache the local
  // result by the app-globally-unique pixel revision so moves and style edits
  // can reuse it while pixel edits and undo snapshots cannot serve stale bounds.
  // The per-revision in-flight latch prevents parallel render strips from
  // repeating a cold scan without blocking hits or scans for unrelated layers.
  return visible_alpha_bounds_cache().fetch_or_compute(layer);
}

std::optional<Rect> layer_visible_alpha_bounds(const PixelBuffer& pixels, Rect bounds) {
  const auto local_bounds = visible_alpha_local_bounds(pixels);
  if (!local_bounds.has_value()) {
    return std::nullopt;
  }
  return Rect{bounds.x + local_bounds->x, bounds.y + local_bounds->y, local_bounds->width, local_bounds->height};
}

std::optional<Rect> layer_visible_alpha_bounds(const Layer& layer, Rect bounds) {
  const auto local_bounds = visible_alpha_local_bounds(layer);
  if (!local_bounds.has_value()) {
    return std::nullopt;
  }
  return Rect{bounds.x + local_bounds->x, bounds.y + local_bounds->y, local_bounds->width, local_bounds->height};
}

std::optional<Rect> layer_visible_alpha_bounds(const Layer& layer, const PixelBuffer& pixels, Rect bounds) {
  // Bounds overrides can supply a transient pixel buffer. Only the layer's own
  // pixels are safe to look up by its pixel revision.
  if (&pixels == &layer.pixels()) {
    return layer_visible_alpha_bounds(layer, bounds);
  }
  return layer_visible_alpha_bounds(pixels, bounds);
}

int layer_style_effect_padding(const LayerStyle& style) noexcept {
  if (!style.effects_visible || style.empty()) {
    return 0;
  }

  int padding = 0;
  constexpr double kRadiansPerDegree = 3.14159265358979323846 / 180.0;
  for (const auto& shadow : style.drop_shadows) {
    if (!shadow.enabled || shadow.opacity <= 0.0F) {
      continue;
    }
    const auto radians = (180.0 - static_cast<double>(shadow.angle_degrees)) * kRadiansPerDegree;
    const auto offset_x = static_cast<int>(std::lround(std::cos(radians) * shadow.distance));
    const auto offset_y = static_cast<int>(std::lround(std::sin(radians) * shadow.distance));
    padding = std::max(padding, std::abs(offset_x) + std::abs(offset_y) + layer_style_falloff_radius(shadow.size) + 2);
  }
  for (const auto& glow : style.outer_glows) {
    if (!glow.enabled || glow.opacity <= 0.0F || glow.size <= 0.0F) {
      continue;
    }
    padding = std::max(padding, layer_style_falloff_radius(glow.size) + 2);
  }
  for (const auto& satin : style.satins) {
    if (satin.enabled && satin.opacity > 0.0F && satin.size > 0.0F) {
      padding = std::max(padding, std::max(1, static_cast<int>(std::ceil(satin.size))) + 1);
    }
  }
  for (const auto& stroke : style.strokes) {
    if (stroke.enabled && stroke.opacity > 0.0F && stroke.size > 0.0F) {
      padding = std::max(padding, std::max(1, static_cast<int>(std::ceil(stroke.size))) + 1);
    }
  }
  for (const auto& bevel : style.bevels) {
    if (!bevel.enabled || bevel.size <= 0.0F ||
        (bevel.highlight_opacity <= 0.0F && bevel.shadow_opacity <= 0.0F)) {
      continue;
    }
    if (bevel.style == BevelEmbossStyleKind::OuterBevel || bevel.style == BevelEmbossStyleKind::Emboss ||
        bevel.style == BevelEmbossStyleKind::PillowEmboss) {
      padding = std::max(padding, layer_style_falloff_radius(bevel.size + bevel.soften) + 2);
    }
  }
  return padding;
}

int layer_effect_padding(const Layer& layer) noexcept {
  int padding = 0;
  if (layer.kind() == LayerKind::Group) {
    for (const auto& child : layer.children()) {
      padding = std::max(padding, layer_effect_padding(child));
    }
    // The group's OWN style pads too (rendered since July 2026); zero for
    // style-less groups, so existing documents keep their exact rects.
    return std::max(padding, layer_style_effect_padding(layer.layer_style()));
  }
  return layer_style_effect_padding(layer.layer_style());
}

int document_effect_padding(const Document& document) noexcept {
  int padding = 0;
  for (const auto& layer : document.layers()) {
    padding = std::max(padding, layer_effect_padding(layer));
  }
  return padding;
}

Rect layer_bounds_with_effects(const Layer& layer, Rect bounds) noexcept {
  const auto padding = layer_effect_padding(layer);
  return bounds.empty() || padding <= 0 ? bounds : outset_rect(bounds, padding);
}

Rect layer_render_bounds(const Layer& layer) noexcept {
  if (layer.kind() == LayerKind::Group) {
    Rect bounds;
    for (const auto& child : layer.children()) {
      bounds = unite_rect(bounds, layer_render_bounds(child));
    }
    // The group's own effects (shadows/glows) extend past the children;
    // style-less groups keep their exact historical rects (padding 0).
    const auto own_padding = layer_style_effect_padding(layer.layer_style());
    return bounds.empty() || own_padding <= 0 ? bounds : outset_rect(bounds, own_padding);
  }
  return layer_bounds_with_effects(layer, layer.bounds());
}

bool group_style_renders(const Layer& layer) noexcept {
  // A group whose style should render (July 2026: group layer effects render;
  // COM-calibrated rules in docs/ps-compat.md). Style-less groups take the
  // exact historical composite paths.
  return layer.kind() == LayerKind::Group && layer.layer_style().effects_visible &&
         !layer.layer_style().empty();
}

namespace {

void collect_ancestor_group_style_info_recursive(const std::vector<Layer>& layers, int inherited_padding,
                                                 bool inherited_styled,
                                                 std::unordered_map<LayerId, AncestorGroupStyleInfo>& info) {
  for (const auto& layer : layers) {
    if (layer.kind() == LayerKind::Group) {
      const auto styled = group_style_renders(layer);
      const auto padding = inherited_padding + (styled ? layer_style_effect_padding(layer.layer_style()) : 0);
      collect_ancestor_group_style_info_recursive(layer.children(), padding, inherited_styled || styled, info);
      continue;
    }
    if (inherited_styled) {
      info.emplace(layer.id(), AncestorGroupStyleInfo{inherited_padding, true});
    }
  }
}

}  // namespace

std::unordered_map<LayerId, AncestorGroupStyleInfo> collect_ancestor_group_style_info(
    const std::vector<Layer>& layers) {
  std::unordered_map<LayerId, AncestorGroupStyleInfo> info;
  collect_ancestor_group_style_info_recursive(layers, 0, false, info);
  return info;
}

std::int32_t preview_scaled_dimension(std::int32_t value, int level) noexcept {
  for (int i = 0; i < level && value > 1; ++i) {
    value = (value + 1) / 2;
  }
  return value;
}

namespace {

[[nodiscard]] PixelBuffer box_halve_pixel_buffer(const PixelBuffer& source) {
  const auto width = source.width();
  const auto height = source.height();
  const auto out_width = std::max(1, (width + 1) / 2);
  const auto out_height = std::max(1, (height + 1) / 2);
  const auto channels = static_cast<int>(source.format().channels);
  PixelBuffer output(out_width, out_height, source.format());
  const bool has_alpha = channels == 4;
  for (std::int32_t y = 0; y < out_height; ++y) {
    const auto y0 = std::min(2 * y, height - 1);
    const auto y1 = std::min(2 * y + 1, height - 1);
    const auto row0 = source.row(y0);
    const auto row1 = source.row(y1);
    auto out_row = output.row(y);
    for (std::int32_t x = 0; x < out_width; ++x) {
      const auto x0 = std::min(2 * x, width - 1);
      const auto x1 = std::min(2 * x + 1, width - 1);
      const std::uint8_t* samples[4] = {row0.data() + static_cast<std::size_t>(x0) * channels,
                                        row0.data() + static_cast<std::size_t>(x1) * channels,
                                        row1.data() + static_cast<std::size_t>(x0) * channels,
                                        row1.data() + static_cast<std::size_t>(x1) * channels};
      auto* out = out_row.data() + static_cast<std::size_t>(x) * channels;
      if (has_alpha) {
        // Alpha-weighted color average: plain channel averaging bleeds the
        // (often black) color of fully transparent texels into edges.
        const int alpha_sum = samples[0][3] + samples[1][3] + samples[2][3] + samples[3][3];
        for (int channel = 0; channel < 3; ++channel) {
          if (alpha_sum > 0) {
            int weighted = 0;
            for (const auto* sample : samples) {
              weighted += sample[channel] * sample[3];
            }
            out[channel] = static_cast<std::uint8_t>((weighted + alpha_sum / 2) / alpha_sum);
          } else {
            out[channel] = static_cast<std::uint8_t>(
                (samples[0][channel] + samples[1][channel] + samples[2][channel] + samples[3][channel] + 2) / 4);
          }
        }
        out[3] = static_cast<std::uint8_t>((alpha_sum + 2) / 4);
      } else {
        for (int channel = 0; channel < channels; ++channel) {
          out[channel] = static_cast<std::uint8_t>(
              (samples[0][channel] + samples[1][channel] + samples[2][channel] + samples[3][channel] + 2) / 4);
        }
      }
    }
  }
  return output;
}

[[nodiscard]] std::int32_t preview_scaled_position(std::int32_t value, int level) noexcept {
  // Floor division that stays a floor for negative coordinates (layers can
  // hang off the canvas).
  const auto scale = std::int32_t{1} << level;
  return value >= 0 ? value / scale : -((-value + scale - 1) / scale);
}

[[nodiscard]] Rect preview_scaled_bounds_endpoints(Rect bounds, int level) noexcept {
  if (bounds.empty()) {
    return bounds;
  }
  const auto scale = std::int32_t{1} << level;
  const auto x0 = preview_scaled_position(bounds.x, level);
  const auto y0 = preview_scaled_position(bounds.y, level);
  const auto x1 = preview_scaled_position(bounds.x + bounds.width + scale - 1, level);
  const auto y1 = preview_scaled_position(bounds.y + bounds.height + scale - 1, level);
  return Rect{x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
}

void shrink_layer_for_preview(Layer& layer, int level) {
  if (layer.kind() == LayerKind::Group) {
    for (auto& child : layer.children()) {
      shrink_layer_for_preview(child, level);
    }
    layer.set_bounds(preview_scaled_bounds_endpoints(layer.bounds(), level));
  } else if (!layer.pixels().empty()) {
    auto buffer = downscale_pixel_buffer_by_level(layer.pixels(), level);
    const auto bounds = layer.bounds();
    const Rect scaled_bounds{preview_scaled_position(bounds.x, level), preview_scaled_position(bounds.y, level),
                             buffer.width(), buffer.height()};
    layer.set_pixels(std::move(buffer));
    layer.set_bounds(scaled_bounds);
  } else {
    layer.set_bounds(preview_scaled_bounds_endpoints(layer.bounds(), level));
  }

  if (layer.mask().has_value()) {
    auto mask = *layer.mask();
    if (!mask.pixels.empty()) {
      mask.pixels = downscale_pixel_buffer_by_level(mask.pixels, level);
      mask.bounds = Rect{preview_scaled_position(mask.bounds.x, level),
                         preview_scaled_position(mask.bounds.y, level), mask.pixels.width(), mask.pixels.height()};
    } else {
      mask.bounds = preview_scaled_bounds_endpoints(mask.bounds, level);
    }
    layer.set_mask(std::move(mask));
  }
  if (const auto* vector_mask = layer.vector_mask(); vector_mask != nullptr) {
    auto updated = *vector_mask;
    if (!updated.cache.empty()) {
      updated.cache = downscale_pixel_buffer_by_level(updated.cache, level);
      updated.cache_bounds =
          Rect{preview_scaled_position(updated.cache_bounds.x, level),
               preview_scaled_position(updated.cache_bounds.y, level), updated.cache.width(), updated.cache.height()};
    } else {
      updated.cache_bounds = preview_scaled_bounds_endpoints(updated.cache_bounds, level);
    }
    updated.feather /= static_cast<double>(std::int32_t{1} << level);
    layer.set_vector_mask(std::move(updated));
  }

  auto& style = layer.layer_style();
  const auto scale = static_cast<float>(std::int32_t{1} << level);
  for (auto& shadow : style.drop_shadows) {
    shadow.distance /= scale;
    shadow.size /= scale;
  }
  for (auto& shadow : style.inner_shadows) {
    shadow.distance /= scale;
    shadow.size /= scale;
  }
  for (auto& glow : style.outer_glows) {
    glow.size /= scale;
  }
  for (auto& glow : style.inner_glows) {
    glow.size /= scale;
  }
  for (auto& satin : style.satins) {
    satin.distance /= scale;
    satin.size /= scale;
  }
  for (auto& stroke : style.strokes) {
    stroke.size /= scale;
  }
  for (auto& bevel : style.bevels) {
    bevel.size /= scale;
    bevel.soften /= scale;
  }
}

}  // namespace

PixelBuffer downscale_pixel_buffer_by_level(const PixelBuffer& source, int level) {
  if (source.empty() || level <= 0) {
    return source;
  }
  auto result = box_halve_pixel_buffer(source);
  for (int i = 1; i < level; ++i) {
    result = box_halve_pixel_buffer(result);
  }
  return result;
}

Document build_preview_scaled_document(const Document& document, int level) {
  Document scaled(document);
  if (level <= 0) {
    return scaled;
  }
  scaled.resize_canvas(preview_scaled_dimension(document.width(), level),
                       preview_scaled_dimension(document.height(), level));
  for (auto& layer : scaled.layers()) {
    shrink_layer_for_preview(layer, level);
  }
  return scaled;
}

void retarget_preview_scaled_layer_bounds(Layer& scaled, const Layer& real, int level) {
  if (level <= 0) {
    return;
  }
  // Mirror of shrink_layer_for_preview's coordinate math, sourcing positions
  // from the committed full-res layer. Recompute (not translate-by-scaled-
  // delta): a delta that is not a multiple of 2^level floors differently, and
  // only the recompute matches what a fresh build would produce.
  const auto& const_scaled = std::as_const(scaled);
  const auto real_bounds = real.bounds();
  if (scaled.kind() == LayerKind::Group || const_scaled.pixels().empty()) {
    scaled.set_bounds(preview_scaled_bounds_endpoints(real_bounds, level));
  } else {
    scaled.set_bounds(Rect{preview_scaled_position(real_bounds.x, level),
                           preview_scaled_position(real_bounds.y, level), const_scaled.pixels().width(),
                           const_scaled.pixels().height()});
  }

  if (const_scaled.mask().has_value() && real.mask().has_value()) {
    auto mask = *const_scaled.mask();
    const auto& real_mask = *real.mask();
    if (!mask.pixels.empty()) {
      mask.bounds = Rect{preview_scaled_position(real_mask.bounds.x, level),
                         preview_scaled_position(real_mask.bounds.y, level), mask.pixels.width(),
                         mask.pixels.height()};
    } else {
      mask.bounds = preview_scaled_bounds_endpoints(real_mask.bounds, level);
    }
    scaled.set_mask(std::move(mask));
  }

  if (const auto* real_shape = real.vector_shape();
      real_shape != nullptr && const_scaled.vector_shape() != nullptr) {
    scaled.set_vector_shape(*real_shape);
  }
  if (const auto* real_vector_mask = real.vector_mask(); real_vector_mask != nullptr) {
    if (const auto* scaled_vector_mask = const_scaled.vector_mask(); scaled_vector_mask != nullptr) {
      // Keep the downscaled cache buffer and the already-divided feather; take
      // the translated path and recompute the cache position from full-res.
      auto updated = *real_vector_mask;
      updated.cache = scaled_vector_mask->cache;
      updated.feather = scaled_vector_mask->feather;
      if (!updated.cache.empty()) {
        updated.cache_bounds = Rect{preview_scaled_position(real_vector_mask->cache_bounds.x, level),
                                    preview_scaled_position(real_vector_mask->cache_bounds.y, level),
                                    updated.cache.width(), updated.cache.height()};
      } else {
        updated.cache_bounds = preview_scaled_bounds_endpoints(real_vector_mask->cache_bounds, level);
      }
      scaled.set_vector_mask(std::move(updated));
    }
  }
}

bool layer_style_preview_is_expensive(const Layer& layer, Rect document_bounds) noexcept {
  const auto padding = layer_effect_padding(layer);
  if (padding <= 0 || document_bounds.empty()) {
    return false;
  }
  if (padding >= kExpensiveStylePadding) {
    return true;
  }

  const auto clipped_bounds = intersect_rect(document_bounds, layer_render_bounds(layer));
  const auto clipped_area = static_cast<std::int64_t>(clipped_bounds.width) * clipped_bounds.height;
  const auto document_area = static_cast<std::int64_t>(document_bounds.width) * document_bounds.height;
  return document_area > 0 && clipped_area * 4 >= document_area;
}

bool layer_has_enabled_vector_mask(const Layer& layer) noexcept {
  return layer.vector_mask() != nullptr && !layer.vector_mask()->disabled;
}

bool layer_vector_mask_hides_effects(const Layer& layer) noexcept {
  return layer.vector_mask() != nullptr && !layer.vector_mask()->disabled &&
         layer.vector_mask()->hides_effects;
}

float vector_mask_alpha_at(const Layer& layer, std::int32_t x, std::int32_t y) {
  const auto* vector_mask = layer.vector_mask();
  if (vector_mask == nullptr || vector_mask->disabled) {
    return 1.0F;
  }
  float coverage = 0.0F;
  if (!vector_mask->cache_bounds.empty() && !vector_mask->cache.empty() &&
      vector_mask->cache.format() == PixelFormat::gray8() && vector_mask->cache_bounds.contains(x, y)) {
    const auto local_x = x - vector_mask->cache_bounds.x;
    const auto local_y = y - vector_mask->cache_bounds.y;
    coverage = static_cast<float>(*vector_mask->cache.pixel(local_x, local_y)) / 255.0F;
  } else if (vector_mask->path.empty() && !vector_mask->inverted) {
    coverage = 1.0F;  // reveal-all mask with no baked cache
  }
  // Density lifts the hidden floor (Photoshop's vector mask density): at
  // density 255 the mask applies fully, at 0 it hides nothing.
  const float density = static_cast<float>(vector_mask->density) / 255.0F;
  return coverage * density + (1.0F - density);
}

float layer_mask_alpha_at(const Layer& layer, std::int32_t x, std::int32_t y) {
  const auto& mask = layer.mask();
  if (!mask.has_value() || mask->disabled) {
    return vector_mask_alpha_at(layer, x, y);
  }
  return layer_mask_alpha_at(layer, x, y, mask->bounds);
}

float layer_mask_alpha_at(const Layer& layer, std::int32_t x, std::int32_t y, Rect mask_bounds) {
  const auto vector_alpha = vector_mask_alpha_at(layer, x, y);
  const auto& mask = layer.mask();
  if (!mask.has_value() || mask->disabled) {
    return vector_alpha;
  }
  if (mask->pixels.empty() || mask->pixels.format() != PixelFormat::gray8()) {
    return vector_alpha * static_cast<float>(mask->default_color) / 255.0F;
  }
  if (!mask_bounds.contains(x, y)) {
    return vector_alpha * static_cast<float>(mask->default_color) / 255.0F;
  }

  const auto local_x = x - mask_bounds.x;
  const auto local_y = y - mask_bounds.y;
  if (local_x < 0 || local_y < 0 || local_x >= mask->pixels.width() || local_y >= mask->pixels.height()) {
    return vector_alpha * static_cast<float>(mask->default_color) / 255.0F;
  }
  return vector_alpha * static_cast<float>(*mask->pixels.pixel(local_x, local_y)) / 255.0F;
}

std::vector<float> layer_alpha_mask(const PixelBuffer& source, const Layer& layer, Rect bounds, Rect mask_bounds,
                                    std::int32_t sample_offset_x, std::int32_t sample_offset_y,
                                    std::optional<Rect> layer_mask_bounds) {
  if (mask_bounds.empty()) {
    return {};
  }

  const auto width = mask_bounds.width;
  const auto height = mask_bounds.height;
  std::vector<float> mask(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0F);
  if (source.empty()) {
    return mask;
  }

  const auto format = source.format();
  const auto source_left = bounds.x - sample_offset_x;
  const auto source_top = bounds.y - sample_offset_y;
  const auto source_right = bounds.x + source.width() - sample_offset_x;
  const auto source_bottom = bounds.y + source.height() - sample_offset_y;
  const auto draw_left = std::max(mask_bounds.x, source_left);
  const auto draw_top = std::max(mask_bounds.y, source_top);
  const auto draw_right = std::min(mask_bounds.x + mask_bounds.width, source_right);
  const auto draw_bottom = std::min(mask_bounds.y + mask_bounds.height, source_bottom);
  if (draw_left >= draw_right || draw_top >= draw_bottom) {
    return mask;
  }

  if (format.channels < 4) {
    for (std::int32_t y = draw_top; y < draw_bottom; ++y) {
      auto* output = mask.data() + static_cast<std::size_t>(y - mask_bounds.y) * width + (draw_left - mask_bounds.x);
      for (std::int32_t x = draw_left; x < draw_right; ++x) {
        *output++ = layer_mask_bounds.has_value()
                        ? layer_mask_alpha_at(layer, x + sample_offset_x, y + sample_offset_y, *layer_mask_bounds)
                        : layer_mask_alpha_at(layer, x + sample_offset_x, y + sample_offset_y);
      }
    }
    return mask;
  }

  const auto* bytes = source.data().data();
  const auto stride = source.stride_bytes();
  for (std::int32_t y = draw_top; y < draw_bottom; ++y) {
    const auto sy = y + sample_offset_y - bounds.y;
    const auto* source_row = bytes + static_cast<std::size_t>(sy) * stride;
    auto* output = mask.data() + static_cast<std::size_t>(y - mask_bounds.y) * width + (draw_left - mask_bounds.x);
    for (std::int32_t x = draw_left; x < draw_right; ++x) {
      const auto sx = x + sample_offset_x - bounds.x;
      const auto* pixel = source_row + static_cast<std::size_t>(sx) * format.channels;
      const auto mask_alpha =
          layer_mask_bounds.has_value()
              ? layer_mask_alpha_at(layer, x + sample_offset_x, y + sample_offset_y, *layer_mask_bounds)
              : layer_mask_alpha_at(layer, x + sample_offset_x, y + sample_offset_y);
      *output++ = (static_cast<float>(pixel[3]) / 255.0F) * mask_alpha;
    }
  }
  return mask;
}

std::vector<float> layer_alpha_mask(const Layer& layer, Rect bounds, Rect mask_bounds, std::int32_t sample_offset_x,
                                    std::int32_t sample_offset_y, std::optional<Rect> layer_mask_bounds) {
  return layer_alpha_mask(layer.pixels(), layer, bounds, mask_bounds, sample_offset_x, sample_offset_y,
                          layer_mask_bounds);
}

}  // namespace patchy
