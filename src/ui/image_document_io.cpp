#include "ui/image_document_io.hpp"

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "formats/bmp_document_io.hpp"
#include "core/rect_utils.hpp"
#include "render/layer_compositor.hpp"
#include "support/string_utils.hpp"

#include <QColor>
#include <QImageWriter>
#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

bool render_profile_enabled() noexcept;
double render_profile_min_ms() noexcept;
std::uint64_t rect_area_u64(Rect rect) noexcept;

class QImageCompositeTarget {
public:
  QImageCompositeTarget(QImage& destination, bool preserve_alpha, std::int32_t origin_x, std::int32_t origin_y)
      : destination_(destination), preserve_alpha_(preserve_alpha), origin_x_(origin_x), origin_y_(origin_y) {}

  void set_profile_layer_path(std::string path) {
    profile_layer_path_ = std::move(path);
  }

  void composite_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha, BlendMode mode) {
    alpha = clamp_unit(alpha);
    const auto image_x = x - origin_x_;
    const auto image_y = y - origin_y_;
    if (alpha <= 0.0F || image_x < 0 || image_y < 0 || image_x >= destination_.width() ||
        image_y >= destination_.height()) {
      return;
    }

    auto* dst = destination_.scanLine(image_y) + static_cast<std::size_t>(image_x) * (preserve_alpha_ ? 4U : 3U);
    const auto da = preserve_alpha_ ? static_cast<float>(dst[3]) / 255.0F : 1.0F;
    const auto out_a = preserve_alpha_ ? (alpha + da * (1.0F - alpha)) : 1.0F;
    const std::array<std::uint8_t, 3> src = {color.red, color.green, color.blue};
    const std::array<std::uint8_t, 3> dst_rgb = {dst[0], dst[1], dst[2]};
    const auto blended = composite_blended_rgb(src, dst_rgb, mode, alpha, da);
    dst[0] = blended[0];
    dst[1] = blended[1];
    dst[2] = blended[2];
    if (preserve_alpha_) {
      dst[3] = clamp_byte(out_a * 255.0F);
    }
  }

  void composite_source_row(std::int32_t x, std::int32_t y, const std::uint8_t* source_row, std::int32_t width,
                            std::uint16_t channels, float opacity) {
    if (source_row == nullptr || width <= 0 || channels < 3) {
      return;
    }

    auto image_x = x - origin_x_;
    const auto image_y = y - origin_y_;
    if (image_y < 0 || image_y >= destination_.height() || image_x >= destination_.width()) {
      return;
    }
    if (image_x < 0) {
      const auto skip = -image_x;
      if (skip >= width) {
        return;
      }
      source_row += static_cast<std::size_t>(skip) * channels;
      width -= skip;
      image_x = 0;
    }
    width = std::min(width, destination_.width() - image_x);
    if (width <= 0) {
      return;
    }

    const auto opacity_alpha = std::clamp(static_cast<int>(std::lround(clamp_unit(opacity) * 255.0F)), 0, 255);
    if (opacity_alpha <= 0) {
      return;
    }

    auto* dst = destination_.scanLine(image_y) + static_cast<std::size_t>(image_x) * (preserve_alpha_ ? 4U : 3U);
    for (std::int32_t index = 0; index < width; ++index) {
      const auto* src = source_row + static_cast<std::size_t>(index) * channels;
      auto source_alpha = channels >= 4 ? static_cast<int>(src[3]) : 255;
      source_alpha = (source_alpha * opacity_alpha + 127) / 255;
      if (source_alpha <= 0) {
        dst += preserve_alpha_ ? 4U : 3U;
        continue;
      }

      if (!preserve_alpha_) {
        if (source_alpha >= 255) {
          dst[0] = src[0];
          dst[1] = src[1];
          dst[2] = src[2];
        } else {
          dst[0] = static_cast<std::uint8_t>((static_cast<int>(src[0]) * source_alpha +
                                              static_cast<int>(dst[0]) * (255 - source_alpha) + 127) /
                                             255);
          dst[1] = static_cast<std::uint8_t>((static_cast<int>(src[1]) * source_alpha +
                                              static_cast<int>(dst[1]) * (255 - source_alpha) + 127) /
                                             255);
          dst[2] = static_cast<std::uint8_t>((static_cast<int>(src[2]) * source_alpha +
                                              static_cast<int>(dst[2]) * (255 - source_alpha) + 127) /
                                             255);
        }
        dst += 3U;
        continue;
      }

      const auto destination_alpha = static_cast<int>(dst[3]);
      const auto output_alpha = (source_alpha * 255 + destination_alpha * (255 - source_alpha) + 127) / 255;
      if (source_alpha >= 255) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = 255;
      } else if (destination_alpha <= 0) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = static_cast<std::uint8_t>(source_alpha);
      } else if (output_alpha > 0) {
        const auto denominator = output_alpha * 255;
        const auto inverse_source_alpha = 255 - source_alpha;
        for (int channel = 0; channel < 3; ++channel) {
          const auto numerator = static_cast<int>(src[channel]) * source_alpha * 255 +
                                 static_cast<int>(dst[channel]) * destination_alpha * inverse_source_alpha;
          dst[channel] = static_cast<std::uint8_t>((numerator + denominator / 2) / denominator);
        }
        dst[3] = static_cast<std::uint8_t>(output_alpha);
      }
      dst += 4U;
    }
  }

  void composite_image(const QImage& image, Rect bounds, Rect clip) {
    if (image.isNull() || image.format() != QImage::Format_RGBA8888) {
      return;
    }
    const auto draw_rect = intersect_rect(clip, bounds);
    if (draw_rect.empty()) {
      return;
    }
    for (std::int32_t y = draw_rect.y; y < draw_rect.y + draw_rect.height; ++y) {
      const auto source_y = y - bounds.y;
      const auto source_x = draw_rect.x - bounds.x;
      const auto* row = image.constScanLine(source_y) + static_cast<std::size_t>(source_x) * 4U;
      composite_source_row(draw_rect.x, y, row, draw_rect.width, 4, 1.0F);
    }
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentSettings& settings, float amount) {
    amount = clamp_unit(amount);
    const auto image_x = x - origin_x_;
    const auto image_y = y - origin_y_;
    if (amount <= 0.0F || image_x < 0 || image_y < 0 || image_x >= destination_.width() ||
        image_y >= destination_.height()) {
      return;
    }

    auto* dst = destination_.scanLine(image_y) + static_cast<std::size_t>(image_x) * (preserve_alpha_ ? 4U : 3U);
    if (preserve_alpha_ && dst[3] == 0) {
      return;
    }
    const auto adjusted = apply_adjustment_to_color(RgbColor{dst[0], dst[1], dst[2]}, settings);
    dst[0] = clamp_byte(static_cast<float>(adjusted.red) * amount + static_cast<float>(dst[0]) * (1.0F - amount));
    dst[1] = clamp_byte(static_cast<float>(adjusted.green) * amount + static_cast<float>(dst[1]) * (1.0F - amount));
    dst[2] = clamp_byte(static_cast<float>(adjusted.blue) * amount + static_cast<float>(dst[2]) * (1.0F - amount));
  }

  void profile_compositor_step(const char* step, const Layer& layer, Rect rect, double elapsed_ms) const {
    if (!render_profile_enabled() || elapsed_ms < render_profile_min_ms()) {
      return;
    }
    const auto& path = profile_layer_path_.empty() ? layer.name() : profile_layer_path_;
    std::cerr << "PATCHY_RENDER_PROFILE_STEP ms=" << elapsed_ms
              << " step=" << (step != nullptr ? step : "unknown")
              << " area=" << rect_area_u64(rect)
              << " rect=" << rect.x << "," << rect.y << "," << rect.width << "," << rect.height
              << " layer=\"" << path << "\"\n";
  }

private:
  QImage& destination_;
  bool preserve_alpha_{false};
  std::int32_t origin_x_{0};
  std::int32_t origin_y_{0};
  std::string profile_layer_path_;
};

struct RenderTraceStats {
  int layers{0};
  int visible_layers{0};
  int styled_layers{0};
  std::uint64_t clipped_pixel_area{0};
};

struct RenderProfileEntry {
  std::string path;
  std::string kind;
  std::string action;
  Rect draw_rect{};
  std::uint64_t area{0};
  double elapsed_ms{0.0};
};

struct RenderProfile {
  int visited_layers{0};
  int skipped_invisible{0};
  int skipped_zero_opacity{0};
  int skipped_empty_clip{0};
  int skipped_unsupported{0};
  int groups{0};
  int pixel_layers{0};
  int adjustment_layers{0};
  std::uint64_t rendered_area{0};
  std::vector<RenderProfileEntry> entries;
};

bool render_trace_enabled() noexcept {
  static const bool enabled = qEnvironmentVariableIsSet("PATCHY_RENDER_TRACE");
  return enabled;
}

bool render_profile_enabled() noexcept {
  static const bool enabled = qEnvironmentVariableIsSet("PATCHY_RENDER_PROFILE");
  return enabled;
}

int render_profile_top_count() noexcept {
  bool ok = false;
  const auto value = qEnvironmentVariableIntValue("PATCHY_RENDER_PROFILE_TOP", &ok);
  return ok ? std::clamp(value, 1, 100) : 16;
}

double render_profile_min_ms() noexcept {
  bool ok = false;
  const auto value = qEnvironmentVariableIntValue("PATCHY_RENDER_PROFILE_MIN_MS", &ok);
  return ok ? std::max(0, value) : 0;
}

std::string sanitized_profile_text(std::string text) {
  for (auto& ch : text) {
    if (ch == '\n' || ch == '\r' || ch == '\t') {
      ch = ' ';
    }
  }
  if (text.size() > 140U) {
    text.resize(140U);
  }
  return text;
}

std::string layer_kind_name(LayerKind kind) {
  switch (kind) {
    case LayerKind::Pixel:
      return "pixel";
    case LayerKind::Group:
      return "group";
    case LayerKind::Adjustment:
      return "adjustment";
    case LayerKind::Text:
      return "text";
    case LayerKind::Vector:
      return "vector";
    case LayerKind::SmartObject:
      return "smart_object";
  }
  return "unknown";
}

std::string profile_layer_path(std::string_view parent_path, const Layer& layer) {
  auto name = sanitized_profile_text(layer.name());
  if (name.empty()) {
    name = "<unnamed>";
  }
  if (parent_path.empty()) {
    return name;
  }
  std::string path(parent_path);
  path += '/';
  path += name;
  return path;
}

std::uint64_t rect_area_u64(Rect rect) noexcept {
  if (rect.empty()) {
    return 0;
  }
  return static_cast<std::uint64_t>(rect.width) * static_cast<std::uint64_t>(rect.height);
}

void collect_render_trace_stats(const Layer& layer, Rect clip,
                                const std::vector<render_detail::LayerBoundsOverride>* overrides,
                                RenderTraceStats& stats) {
  ++stats.layers;
  if (!layer.visible() || layer.opacity() <= 0.0F) {
    return;
  }
  ++stats.visible_layers;
  if (!layer.layer_style().empty()) {
    ++stats.styled_layers;
  }
  if (layer.kind() == LayerKind::Group) {
    for (const auto& child : layer.children()) {
      collect_render_trace_stats(child, clip, overrides, stats);
    }
    return;
  }
  if (layer.kind() == LayerKind::Pixel) {
    stats.clipped_pixel_area +=
      rect_area_u64(intersect_rect(clip, render_detail::layer_bounds_for_render(layer, overrides)));
  } else if (layer.kind() == LayerKind::Adjustment) {
    auto draw_rect = clip;
    const auto bounds = render_detail::adjustment_bounds_for_render(layer, overrides);
    if (!bounds.empty()) {
      draw_rect = intersect_rect(draw_rect, bounds);
    }
    stats.clipped_pixel_area += rect_area_u64(draw_rect);
  }
}

bool has_enabled_mask(const Layer& layer) noexcept {
  return layer.mask().has_value() && !layer.mask()->disabled;
}

bool has_enabled_backdrop_dependent_style(const LayerStyle& style) noexcept {
  const auto enabled_drop_shadow = std::any_of(style.drop_shadows.begin(), style.drop_shadows.end(),
                                               [](const LayerDropShadow& shadow) {
                                                 return shadow.enabled && shadow.opacity > 0.0F;
                                               });
  const auto enabled_inner_shadow = std::any_of(style.inner_shadows.begin(), style.inner_shadows.end(),
                                                [](const LayerInnerShadow& shadow) {
                                                  return shadow.enabled && shadow.opacity > 0.0F &&
                                                         shadow.size > 0.0F;
                                                });
  const auto enabled_outer_glow = std::any_of(style.outer_glows.begin(), style.outer_glows.end(),
                                              [](const LayerOuterGlow& glow) {
                                                return glow.enabled && glow.opacity > 0.0F && glow.size > 0.0F;
                                              });
  const auto enabled_inner_glow = std::any_of(style.inner_glows.begin(), style.inner_glows.end(),
                                              [](const LayerInnerGlow& glow) {
                                                return glow.enabled && glow.opacity > 0.0F && glow.size > 0.0F;
                                              });
  const auto enabled_gradient = std::any_of(style.gradient_fills.begin(), style.gradient_fills.end(),
                                            [](const LayerGradientFill& fill) {
                                              return fill.enabled && fill.opacity > 0.0F;
                                            });
  const auto enabled_pattern = std::any_of(style.pattern_overlays.begin(), style.pattern_overlays.end(),
                                           [](const LayerPatternOverlay& pattern) {
                                             return pattern.enabled && pattern.opacity > 0.0F;
                                           });
  const auto enabled_bevel = std::any_of(style.bevels.begin(), style.bevels.end(), [](const LayerBevelEmboss& bevel) {
    return bevel.enabled && bevel.size > 0.0F && (bevel.highlight_opacity > 0.0F || bevel.shadow_opacity > 0.0F);
  });
  const auto enabled_satin = std::any_of(style.satins.begin(), style.satins.end(), [](const LayerSatin& satin) {
    return satin.enabled && satin.opacity > 0.0F && satin.size > 0.0F;
  });
  return enabled_drop_shadow || enabled_inner_shadow || enabled_outer_glow || enabled_inner_glow || enabled_gradient ||
         enabled_pattern || enabled_bevel || enabled_satin;
}

bool layer_style_cache_eligible(const Layer& layer, const PixelBuffer& source) {
  if (layer.kind() != LayerKind::Pixel || layer.blend_mode() != BlendMode::Normal || has_enabled_mask(layer) ||
      source.empty() || source.format().bit_depth != BitDepth::UInt8 || source.format().channels < 3) {
    return false;
  }
  const auto& style = layer.layer_style();
  if (!style.effects_visible || style.empty() || has_enabled_backdrop_dependent_style(style)) {
    return false;
  }

  bool has_supported_style = false;
  for (const auto& overlay : style.color_overlays) {
    if (!overlay.enabled || overlay.opacity <= 0.0F) {
      continue;
    }
    if (overlay.blend_mode != BlendMode::Normal) {
      return false;
    }
    has_supported_style = true;
  }
  for (const auto& stroke : style.strokes) {
    if (!stroke.enabled || stroke.opacity <= 0.0F || stroke.size <= 0.0F) {
      continue;
    }
    if (stroke.blend_mode != BlendMode::Normal || stroke.uses_gradient) {
      return false;
    }
    has_supported_style = true;
  }
  return has_supported_style;
}

bool has_layer_render_override(const Layer& layer, const std::vector<render_detail::LayerBoundsOverride>* overrides) {
  if (overrides == nullptr) {
    return false;
  }
  return std::any_of(overrides->begin(), overrides->end(), [&layer](const render_detail::LayerBoundsOverride& override) {
    return override.layer_id == layer.id();
  });
}

struct StyleCacheKey {
  const Layer* layer{nullptr};
  const std::uint8_t* source_data{nullptr};
  std::uint64_t revision{0};
  std::int32_t source_width{0};
  std::int32_t source_height{0};
  std::int32_t render_width{0};
  std::int32_t render_height{0};

  [[nodiscard]] bool operator==(const StyleCacheKey& other) const noexcept {
    return layer == other.layer && source_data == other.source_data && revision == other.revision &&
           source_width == other.source_width && source_height == other.source_height && render_width == other.render_width &&
           render_height == other.render_height;
  }
};

struct StyleCacheKeyHash {
  [[nodiscard]] std::size_t operator()(const StyleCacheKey& key) const noexcept {
    auto seed = std::hash<const Layer*>{}(key.layer);
    const auto mix = [&seed](auto value) {
      seed ^= std::hash<decltype(value)>{}(value) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    };
    mix(key.source_data);
    mix(key.revision);
    mix(key.source_width);
    mix(key.source_height);
    mix(key.render_width);
    mix(key.render_height);
    return seed;
  }
};

class LayerStyleRenderCache {
public:
  [[nodiscard]] std::optional<QImage> find(const StyleCacheKey& key) const {
    const std::lock_guard lock(mutex_);
    const auto found = entries_.find(key);
    if (found == entries_.end()) {
      return std::nullopt;
    }
    return found->second;
  }

  void put(const StyleCacheKey& key, QImage image) {
    if (image.isNull()) {
      return;
    }
    constexpr std::size_t kMaxCacheBytes = 768U * 1024U * 1024U;
    const auto image_bytes = static_cast<std::size_t>(image.sizeInBytes());
    if (image_bytes > kMaxCacheBytes) {
      return;
    }
    const std::lock_guard lock(mutex_);
    if (total_bytes_ + image_bytes > kMaxCacheBytes) {
      entries_.clear();
      total_bytes_ = 0;
    }
    total_bytes_ += image_bytes;
    entries_[key] = std::move(image);
  }

private:
  mutable std::mutex mutex_;
  std::unordered_map<StyleCacheKey, QImage, StyleCacheKeyHash> entries_;
  std::size_t total_bytes_{0};
};

LayerStyleRenderCache& layer_style_render_cache() {
  static LayerStyleRenderCache cache;
  return cache;
}

bool layer_can_affect_clip(const Layer& layer, Rect clip,
                           const std::vector<render_detail::LayerBoundsOverride>* overrides, Rect* draw_rect) {
  if (layer.kind() == LayerKind::Pixel) {
    const auto render_bounds = layer_bounds_with_effects(layer, render_detail::layer_bounds_for_render(layer, overrides));
    const auto clipped = intersect_rect(clip, render_bounds);
    if (draw_rect != nullptr) {
      *draw_rect = clipped;
    }
    return !clipped.empty();
  }
  if (layer.kind() == LayerKind::Adjustment) {
    const auto bounds = render_detail::adjustment_bounds_for_render(layer, overrides);
    if (!bounds.empty()) {
      const auto clipped = intersect_rect(clip, bounds);
      if (draw_rect != nullptr) {
        *draw_rect = clipped;
      }
      return !clipped.empty();
    }
  }
  if (draw_rect != nullptr) {
    *draw_rect = clip;
  }
  return true;
}

bool composite_cached_style_layer(QImageCompositeTarget& destination, const Layer& layer, Rect clip,
                                  const std::vector<render_detail::LayerBoundsOverride>* overrides) {
  if (has_layer_render_override(layer, overrides)) {
    return false;
  }

  const auto& source = render_detail::layer_pixels_for_render(layer, overrides);
  if (!layer_style_cache_eligible(layer, source)) {
    return false;
  }

  const auto bounds = render_detail::layer_bounds_for_render(layer, overrides);
  const auto render_bounds = layer_bounds_with_effects(layer, bounds);
  if (render_bounds.empty()) {
    return true;
  }
  const auto draw_rect = intersect_rect(clip, render_bounds);
  if (draw_rect.empty()) {
    return true;
  }

  const StyleCacheKey key{&layer,
                          source.data().empty() ? nullptr : source.data().data(),
                          layer.content_revision(),
                          source.width(),
                          source.height(),
                          render_bounds.width,
                          render_bounds.height};
  if (const auto cached = layer_style_render_cache().find(key); cached.has_value()) {
    destination.composite_image(*cached, render_bounds, clip);
    return true;
  }
  if (draw_rect.x != render_bounds.x || draw_rect.y != render_bounds.y ||
      draw_rect.width != render_bounds.width || draw_rect.height != render_bounds.height) {
    return false;
  }

  QImage image(render_bounds.width, render_bounds.height, QImage::Format_RGBA8888);
  image.fill(Qt::transparent);
  QImageCompositeTarget target(image, true, render_bounds.x, render_bounds.y);
  render_detail::composite_pixel_layer(target, layer, render_bounds, overrides, false);
  destination.composite_image(image, render_bounds, clip);
  layer_style_render_cache().put(key, std::move(image));
  return true;
}

void composite_document_layer(QImageCompositeTarget& target, const Layer& layer, Rect clip,
                              const std::vector<render_detail::LayerBoundsOverride>* overrides,
                              RenderProfile* profile = nullptr, std::string_view parent_path = {}) {
  const auto profiling = profile != nullptr;
  const auto started = profiling ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  const auto kind = layer.kind();
  const auto kind_text = profiling ? layer_kind_name(kind) : std::string();
  const auto path = profiling ? profile_layer_path(parent_path, layer) : std::string();
  Rect draw_rect = clip;
  std::string action;
  if (profiling) {
    ++profile->visited_layers;
  }
  if (!layer.visible() || layer.opacity() <= 0.0F) {
    if (profiling) {
      if (!layer.visible()) {
        ++profile->skipped_invisible;
        action = "skip_invisible";
      } else {
        ++profile->skipped_zero_opacity;
        action = "skip_zero_opacity";
      }
      const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
      profile->entries.push_back(RenderProfileEntry{path, kind_text, action, draw_rect, 0, elapsed});
    }
    return;
  }

  if (kind != LayerKind::Group && !layer_can_affect_clip(layer, clip, overrides, &draw_rect)) {
    if (profiling) {
      ++profile->skipped_empty_clip;
      const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
      profile->entries.push_back(RenderProfileEntry{path, kind_text, "skip_empty_clip", draw_rect, 0, elapsed});
    }
    return;
  }

  if (kind == LayerKind::Group) {
    if (profiling) {
      ++profile->groups;
    }
    for (const auto& child : layer.children()) {
      composite_document_layer(target, child, clip, overrides, profile, path);
    }
    action = "group";
  } else if (kind == LayerKind::Adjustment) {
    if (profiling) {
      ++profile->adjustment_layers;
      profile->rendered_area += rect_area_u64(draw_rect);
    }
    render_detail::composite_adjustment_layer(target, layer, clip, overrides);
    action = "adjustment";
  } else if (kind == LayerKind::Pixel) {
    if (profiling) {
      ++profile->pixel_layers;
      profile->rendered_area += rect_area_u64(draw_rect);
      target.set_profile_layer_path(path);
    }
    if (composite_cached_style_layer(target, layer, clip, overrides)) {
      action = "pixel_cached_or_empty";
    } else {
      render_detail::composite_pixel_layer(target, layer, clip, overrides, false);
      action = layer.layer_style().effects_visible && !layer.layer_style().empty() ? "pixel_style" : "pixel";
    }
  } else {
    if (profiling) {
      ++profile->skipped_unsupported;
    }
    action = "skip_unsupported";
  }
  if (profiling) {
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    profile->entries.push_back(RenderProfileEntry{path, kind_text, action, draw_rect, rect_area_u64(draw_rect), elapsed});
  }
}

void print_render_profile(Rect clip, bool preserve_alpha, const RenderProfile& profile, double elapsed_ms) {
  auto entries = profile.entries;
  std::sort(entries.begin(), entries.end(),
            [](const RenderProfileEntry& lhs, const RenderProfileEntry& rhs) {
              return lhs.elapsed_ms > rhs.elapsed_ms;
            });
  const auto top_count = render_profile_top_count();
  const auto min_ms = render_profile_min_ms();
  std::cerr << "PATCHY_RENDER_PROFILE rect=" << clip.x << "," << clip.y << "," << clip.width << "," << clip.height
            << " preserve_alpha=" << (preserve_alpha ? 1 : 0) << " total_ms=" << elapsed_ms
            << " visited_layers=" << profile.visited_layers << " groups=" << profile.groups
            << " pixel_layers=" << profile.pixel_layers << " adjustment_layers=" << profile.adjustment_layers
            << " skipped_invisible=" << profile.skipped_invisible
            << " skipped_zero_opacity=" << profile.skipped_zero_opacity
            << " skipped_empty_clip=" << profile.skipped_empty_clip
            << " skipped_unsupported=" << profile.skipped_unsupported
            << " rendered_area=" << profile.rendered_area << '\n';
  int printed = 0;
  for (const auto& entry : entries) {
    if (printed >= top_count) {
      break;
    }
    if (entry.elapsed_ms < min_ms) {
      continue;
    }
    std::cerr << "PATCHY_RENDER_PROFILE_LAYER rank=" << (printed + 1)
              << " ms=" << entry.elapsed_ms
              << " action=" << entry.action
              << " kind=" << entry.kind
              << " area=" << entry.area
              << " rect=" << entry.draw_rect.x << "," << entry.draw_rect.y << "," << entry.draw_rect.width << ","
              << entry.draw_rect.height
              << " path=\"" << entry.path << "\"\n";
    ++printed;
  }
}

std::string lower_extension(std::string_view extension) {
  return normalized_extension(extension, false);
}

bool is_jpeg_extension(std::string_view extension) {
  const auto lower = lower_extension(extension);
  return lower == "jpg" || lower == "jpeg";
}

bool is_bmp_extension(std::string_view extension) {
  return lower_extension(extension) == "bmp";
}

double sanitized_ppi(double value) noexcept {
  return std::isfinite(value) && value > 0.0 ? value : 300.0;
}

double ppi_from_dots_per_meter(int dots_per_meter) noexcept {
  return dots_per_meter > 0 ? static_cast<double>(dots_per_meter) * 0.0254 : 300.0;
}

int dots_per_meter_from_ppi(double ppi) noexcept {
  return std::clamp(static_cast<int>(std::lround(sanitized_ppi(ppi) / 0.0254)), 1, 1000000);
}

void apply_document_resolution(QImage& image, const Document& document) {
  if (image.isNull()) {
    return;
  }
  image.setDotsPerMeterX(dots_per_meter_from_ppi(document.print_settings().horizontal_ppi));
  image.setDotsPerMeterY(dots_per_meter_from_ppi(document.print_settings().vertical_ppi));
}

QImage render_document_rect(const Document& document, QRect document_rect, bool preserve_alpha,
                            const std::vector<render_detail::LayerBoundsOverride>* overrides) {
  const auto tracing = render_trace_enabled();
  const auto profiling = render_profile_enabled();
  RenderTraceStats trace_stats;
  if (tracing) {
    const auto normalized = document_rect.normalized();
    const auto requested = Rect{normalized.x(), normalized.y(), normalized.width(), normalized.height()};
    const auto clip = intersect_rect(Rect::from_size(document.width(), document.height()), requested);
    for (const auto& layer : document.layers()) {
      collect_render_trace_stats(layer, clip, overrides, trace_stats);
    }
  }
  const auto timed_render = tracing || profiling;
  const auto trace_start = timed_render ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

  const auto normalized = document_rect.normalized();
  const auto requested = Rect{normalized.x(), normalized.y(), normalized.width(), normalized.height()};
  const auto clip = intersect_rect(Rect::from_size(document.width(), document.height()), requested);
  if (clip.empty()) {
    return {};
  }

  QImage image(clip.width, clip.height, preserve_alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
  if (preserve_alpha) {
    image.fill(Qt::transparent);
  } else {
    image.fill(Qt::white);
  }

  QImageCompositeTarget target(image, preserve_alpha, clip.x, clip.y);
  RenderProfile profile;
  for (const auto& layer : document.layers()) {
    composite_document_layer(target, layer, clip, overrides, profiling ? &profile : nullptr);
  }
  apply_document_resolution(image, document);
  if (timed_render) {
    const auto elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - trace_start);
    if (tracing) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed_ms);
      std::cerr << "PATCHY_RENDER_TRACE rect=" << clip.x << "," << clip.y << "," << clip.width << "," << clip.height
                << " preserve_alpha=" << (preserve_alpha ? 1 : 0) << " layers=" << trace_stats.layers
                << " visible_layers=" << trace_stats.visible_layers << " styled_layers=" << trace_stats.styled_layers
                << " pixel_count=" << trace_stats.clipped_pixel_area << " elapsed_ms=" << elapsed.count() << '\n';
    }
    if (profiling) {
      print_render_profile(clip, preserve_alpha, profile, elapsed_ms.count());
    }
  }
  return image;
}

std::vector<RenderedDocumentPatch> render_document_region(
    const Document& document, const QRegion& document_region, bool preserve_alpha,
    const std::vector<render_detail::LayerBoundsOverride>* overrides) {
  std::vector<RenderedDocumentPatch> patches;
  if (document_region.isEmpty()) {
    return patches;
  }

  const QRect canvas_rect(0, 0, document.width(), document.height());
  for (const auto& rect : document_region.intersected(canvas_rect)) {
    if (rect.isEmpty()) {
      continue;
    }
    auto image = render_document_rect(document, rect, preserve_alpha, overrides);
    if (image.isNull()) {
      continue;
    }
    patches.push_back(RenderedDocumentPatch{rect, std::move(image)});
  }
  return patches;
}

}  // namespace

PixelBuffer pixels_from_image_rgba(const QImage& image) {
  const auto converted = image.convertToFormat(QImage::Format_RGBA8888);
  PixelBuffer pixels(converted.width(), converted.height(), PixelFormat::rgba8());
  for (int y = 0; y < converted.height(); ++y) {
    for (int x = 0; x < converted.width(); ++x) {
      const auto color = converted.pixelColor(x, y);
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(color.red());
      px[1] = static_cast<std::uint8_t>(color.green());
      px[2] = static_cast<std::uint8_t>(color.blue());
      px[3] = static_cast<std::uint8_t>(color.alpha());
    }
  }
  return pixels;
}

Document document_from_qimage(const QImage& image, std::string layer_name) {
  if (image.isNull()) {
    throw std::invalid_argument("Cannot import a null image");
  }

  const auto has_alpha = image.hasAlphaChannel();
  const auto converted = image.convertToFormat(has_alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
  auto pixels = has_alpha ? pixels_from_image_rgba(converted)
                          : PixelBuffer(converted.width(), converted.height(), PixelFormat::rgb8());

  if (!has_alpha) {
    for (int y = 0; y < converted.height(); ++y) {
      for (int x = 0; x < converted.width(); ++x) {
        const auto color = converted.pixelColor(x, y);
        auto* px = pixels.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(color.red());
        px[1] = static_cast<std::uint8_t>(color.green());
        px[2] = static_cast<std::uint8_t>(color.blue());
      }
    }
  }

  if (layer_name.empty()) {
    layer_name = "Imported Image";
  }
  Document document(converted.width(), converted.height(), has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8());
  document.print_settings().horizontal_ppi = ppi_from_dots_per_meter(image.dotsPerMeterX());
  document.print_settings().vertical_ppi = ppi_from_dots_per_meter(image.dotsPerMeterY());
  document.add_pixel_layer(std::move(layer_name), std::move(pixels));
  return document;
}

bool promote_flat_alpha_to_layer_mask(Document& document) {
  // Only flat single-layer imports are eligible. Multi-layer PSDs (and PSDs that already
  // produced a mask) carry their own per-layer transparency/masks and must be left alone.
  if (document.layers().size() != 1) {
    return false;
  }
  Layer& layer = document.layers().front();
  if (layer.kind() != LayerKind::Pixel || !layer.children().empty() || layer.mask().has_value()) {
    return false;
  }

  const PixelBuffer& pixels = layer.pixels();
  const auto format = pixels.format();
  if (format.channels != 4 || format.bit_depth != BitDepth::UInt8) {
    return false;
  }

  const auto width = pixels.width();
  const auto height = pixels.height();
  if (width <= 0 || height <= 0) {
    return false;
  }

  // Scan the alpha channel to decide whether it carries a meaningful mask. A uniformly
  // opaque image has nothing to mask; a uniformly transparent one is treated as opaque
  // padding so we don't import an image that is entirely masked out.
  std::uint8_t min_alpha = 255;
  std::uint8_t max_alpha = 0;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const std::uint8_t alpha = pixels.pixel(x, y)[3];
      min_alpha = std::min(min_alpha, alpha);
      max_alpha = std::max(max_alpha, alpha);
    }
  }
  const bool meaningful = max_alpha > 0 && min_alpha < 255;

  std::optional<PixelBuffer> mask_pixels;
  if (meaningful) {
    PixelBuffer mask(width, height, PixelFormat::gray8());
    for (std::int32_t y = 0; y < height; ++y) {
      for (std::int32_t x = 0; x < width; ++x) {
        mask.pixel(x, y)[0] = pixels.pixel(x, y)[3];
      }
    }
    mask_pixels = std::move(mask);
  }

  // Strip the alpha so the layer pixels become opaque RGB and the original colors stay
  // fully visible (Photoshop shows this as an opaque Background plus an "Alpha 1" channel).
  PixelBuffer rgb(width, height, PixelFormat::rgb8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const std::uint8_t* src = pixels.pixel(x, y);
      std::uint8_t* dst = rgb.pixel(x, y);
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
    }
  }

  // set_pixels resets the layer bounds to the new pixel size, so apply it before the mask
  // (set_mask requires the mask bounds to match).
  layer.set_pixels(std::move(rgb));
  document.set_format(PixelFormat::rgb8());

  if (mask_pixels.has_value()) {
    layer.set_mask(LayerMask{Rect::from_size(width, height), std::move(*mask_pixels), /*default_color*/ 255,
                             /*disabled*/ false});
    set_layer_mask_is_document_alpha(layer, true);
    return true;
  }
  return false;
}

QImage qimage_from_document(const Document& document, bool preserve_alpha) {
  return render_document_rect(document, QRect(0, 0, document.width(), document.height()), preserve_alpha, nullptr);
}

QImage qimage_from_document_rect(const Document& document, QRect document_rect, bool preserve_alpha) {
  return render_document_rect(document, document_rect, preserve_alpha, nullptr);
}

std::vector<RenderedDocumentPatch> qimage_patches_from_document_region(const Document& document,
                                                                       const QRegion& document_region,
                                                                       bool preserve_alpha) {
  return render_document_region(document, document_region, preserve_alpha, nullptr);
}

std::optional<Rect> linked_mask_bounds_for_layer_bounds_override(const Document& document, LayerId layer_id,
                                                                 Rect bounds) {
  const auto* layer = document.find_layer(layer_id);
  if (layer == nullptr || !layer->mask().has_value() || !layer_mask_linked(*layer)) {
    return std::nullopt;
  }
  const auto original_bounds = render_detail::layer_bounds_for_render(*layer, nullptr);
  const auto& mask_bounds = layer->mask()->bounds;
  return Rect{mask_bounds.x + (bounds.x - original_bounds.x), mask_bounds.y + (bounds.y - original_bounds.y),
              mask_bounds.width, mask_bounds.height};
}

QImage qimage_from_document_rect_with_layer_bounds(
    const Document& document, QRect document_rect, bool preserve_alpha,
    const std::vector<std::pair<LayerId, Rect>>& layer_bounds) {
  std::vector<render_detail::LayerBoundsOverride> overrides;
  overrides.reserve(layer_bounds.size());
  for (const auto& [layer_id, bounds] : layer_bounds) {
    overrides.push_back(render_detail::LayerBoundsOverride{
        layer_id, bounds, nullptr, linked_mask_bounds_for_layer_bounds_override(document, layer_id, bounds)});
  }
  return render_document_rect(document, document_rect, preserve_alpha, &overrides);
}

std::vector<RenderedDocumentPatch> qimage_patches_from_document_region_with_layer_bounds(
    const Document& document, const QRegion& document_region, bool preserve_alpha,
    const std::vector<std::pair<LayerId, Rect>>& layer_bounds) {
  std::vector<render_detail::LayerBoundsOverride> overrides;
  overrides.reserve(layer_bounds.size());
  for (const auto& [layer_id, bounds] : layer_bounds) {
    overrides.push_back(render_detail::LayerBoundsOverride{
        layer_id, bounds, nullptr, linked_mask_bounds_for_layer_bounds_override(document, layer_id, bounds)});
  }
  return render_document_region(document, document_region, preserve_alpha, &overrides);
}

QImage qimage_from_document_rect_with_layer_bounds(const Document& document, QRect document_rect, bool preserve_alpha,
                                                   LayerId layer_id, Rect layer_bounds) {
  const std::vector<std::pair<LayerId, Rect>> layer_bounds_overrides{{layer_id, layer_bounds}};
  return qimage_from_document_rect_with_layer_bounds(document, document_rect, preserve_alpha, layer_bounds_overrides);
}

QImage qimage_from_document_rect_with_layer_pixels(const Document& document, QRect document_rect, bool preserve_alpha,
                                                   LayerId layer_id, const PixelBuffer& layer_pixels,
                                                   Rect layer_bounds) {
  const std::vector<render_detail::LayerBoundsOverride> overrides{
      render_detail::LayerBoundsOverride{layer_id, layer_bounds, &layer_pixels}};
  return render_document_rect(document, document_rect, preserve_alpha, &overrides);
}

bool image_format_preserves_alpha(std::string_view extension) noexcept {
  const auto lower = lower_extension(extension);
  return lower == "png" || lower == "tif" || lower == "tiff" || lower == "webp";
}

void write_flat_image_file(const Document& document, const QString& path, const QString& extension,
                           const ImageSaveOptions& options) {
  const auto extension_bytes = extension.toStdString();
  if (is_bmp_extension(extension_bytes)) {
    bmp::DocumentIo::write_file(document, path.toStdString(),
                                bmp::WriteOptions{options.bmp_encoding, options.bmp_palette_mode, true,
                                                  options.bmp_palette_path.toStdString()});
    return;
  }

  QImageWriter writer(path);
  if (is_jpeg_extension(extension_bytes)) {
    writer.setQuality(std::clamp(options.jpeg_quality, 0, 100));
  }
  const auto preserve_alpha = image_format_preserves_alpha(extension_bytes);
  QImage image;
  // A single masked layer is exported non-destructively to alpha-capable formats: keep the
  // original colors and write the mask into the alpha channel (compositing would erase the
  // colors wherever the mask is transparent).
  if (std::optional<PixelBuffer> masked = preserve_alpha ? document_alpha_rgba8(document) : std::nullopt;
      masked.has_value()) {
    image = QImage(masked->width(), masked->height(), QImage::Format_RGBA8888);
    for (std::int32_t y = 0; y < masked->height(); ++y) {
      const auto src_row = masked->row(y);
      std::copy(src_row.begin(), src_row.end(), image.scanLine(y));
    }
  } else {
    image = qimage_from_document(document, preserve_alpha);
  }
  if (!writer.write(image)) {
    throw std::runtime_error(writer.errorString().toStdString());
  }
}

}  // namespace patchy::ui
