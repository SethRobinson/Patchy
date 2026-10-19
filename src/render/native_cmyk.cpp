#include "render/native_cmyk.hpp"
#include "core/artboard.hpp"

#include "color/color_management.hpp"
#include "core/psd_source_colors.hpp"
#include "render/layer_compositor.hpp"

#include <algorithm>
#include <atomic>
#include <list>
#include <mutex>
#include <unordered_map>

namespace patchy {
namespace {

std::atomic<std::uint64_t> transform_builds{0};
std::atomic<std::uint64_t> separated_pixels{0};

// CMYK's stored inverted channels use the same component blend kernels as RGB.
// K is a separate neutral triplet. Hue/Saturation/Color keep backdrop K;
// Luminosity takes source K. Whole-color selection couples all four channels.
struct ChannelPixels {
  PixelBuffer cmy;
  PixelBuffer black;
  Rect bounds;
};

bool supported_layers(const std::vector<Layer>& layers) {
  const auto supported_fill = [](const VectorFill& fill) {
    return fill.kind != VectorFillKind::Pattern &&
        (fill.kind != VectorFillKind::Gradient || fill.gradient.form == GradientDefinitionForm::Solid);
  };
  for (const auto& layer : layers) {
    if (layer.kind() == LayerKind::Adjustment) {
      const auto settings = adjustment_settings_from_layer(layer);
      if (!settings || settings->kind != AdjustmentKind::Threshold ||
          layer.blend_mode() != BlendMode::Normal) return false;
    }
    if (layer.clipped() ||
        render_detail::layer_knockout_mode(layer) != 0 ||
        (!layer.layer_style().empty() || !layer.layer_style().satins.empty()) ||
        render_detail::layer_has_rendered_blend_if(layer) ||
        layer.restricted_channels() != 0 || !supported_layers(layer.children())) return false;
    // Noise generators and compound/pattern paints produce RGB, not separable
    // ink planes. Non-Normal strokes also need a coupled four-channel bake.
    if (const auto* shape = layer.vector_shape(); shape != nullptr &&
        (!supported_fill(shape->fill) || !supported_fill(shape->stroke.content) || !shape->parts.empty() ||
         (shape->stroke.enabled && shape->stroke.blend_mode != BlendMode::Normal))) return false;
  }
  return true;
}

class NativeTarget {
public:
  explicit NativeTarget(Rect clip) : cmy(clip), black(clip) {}
  render_detail::IsolatedClipGroupTarget cmy;
  render_detail::IsolatedClipGroupTarget black;
  std::vector<const Layer*> group_masks;
  const Layer* black_source{nullptr};

  float group_coverage(int x, int y) const {
    float coverage = 1.0F;
    for (const auto* group : group_masks) coverage *= layer_mask_alpha_at(*group, x, y);
    return coverage;
  }
  RgbColor source_black(int x, int y) const {
    const auto bounds = layer_pixel_bounds(*black_source);
    if (!bounds.contains(x, y)) return {255, 255, 255};
    const auto* pixel = black_source->pixels().pixel(x - bounds.x, y - bounds.y);
    return {pixel[0], pixel[1], pixel[2]};
  }
  void paint(int x, int y, RgbColor color, RgbColor k, float alpha, BlendMode mode) {
    alpha = clamp_unit(alpha * group_coverage(x, y));
    if (mode == BlendMode::Dissolve) {
      alpha = dissolve_coverage(x, y, alpha);
      mode = BlendMode::Normal;
    }
    if (mode != BlendMode::DarkerColor && mode != BlendMode::LighterColor) {
      cmy.composite_color(x, y, color, alpha, mode);
      black.composite_color(x, y, k, alpha, mode);
      return;
    }
    const auto dst = cmy.sample_color(x, y);
    const auto dst_k = black.sample_color(x, y);
    // Photoshop compares the simple CMY luminance times inverted K, before
    // the ICC display conversion. 512 native-channel COM probes pin this rule.
    const auto lightness = [](RgbColor ink, RgbColor key) {
      return (30 * ink.red + 59 * ink.green + 11 * ink.blue) * key.red;
    };
    const auto source_lightness = lightness(color, k);
    const auto destination_lightness = lightness(dst.color, dst_k.color);
    const bool source_wins = mode == BlendMode::DarkerColor ? source_lightness < destination_lightness
                                                           : source_lightness > destination_lightness;
    const auto output_alpha = alpha + dst.alpha * (1.0F - alpha);
    if (output_alpha <= 0.0F) return;
    const auto mix = [&](RgbColor source, RgbColor destination) {
      const auto component = [&](std::uint8_t s, std::uint8_t d) {
        const auto blended = source_wins ? s : d;
        return clamp_byte((s * alpha * (1.0F - dst.alpha) + blended * alpha * dst.alpha +
                           d * dst.alpha * (1.0F - alpha)) / output_alpha);
      };
      return RgbColor{component(source.red, destination.red), component(source.green, destination.green),
                      component(source.blue, destination.blue)};
    };
    cmy.store_color(x, y, mix(color, dst.color), output_alpha);
    black.store_color(x, y, mix(k, dst_k.color), output_alpha);
  }
  void composite_color(int x, int y, RgbColor color, float alpha, BlendMode mode) {
    paint(x, y, color, source_black(x, y), alpha, mode);
  }
  void composite_special_fill_color(int x, int y, RgbColor color, float coverage, float fill,
                                    float opacity, BlendMode mode) {
    coverage *= group_coverage(x, y);
    cmy.composite_special_fill_color(x, y, color, coverage, fill, opacity, mode);
    black.composite_special_fill_color(x, y, source_black(x, y), coverage, fill, opacity, mode);
  }
  render_detail::CompositeSample sample_color(int x, int y) const { return cmy.sample_color(x, y); }
  void store_color(int x, int y, RgbColor color, float alpha) {
    cmy.store_color(x, y, color, alpha);
    black.store_color(x, y, black.sample_color(x, y).color, alpha);
  }
};

void composite_native_layers(NativeTarget& target, const std::vector<Layer>& cmy,
                             const std::vector<Layer>& black, Rect clip) {
  for (std::size_t i = 0; i < cmy.size(); ++i) {
    const auto& layer = cmy[i];
    if (!layer.visible() || layer.opacity() <= 0.0F) continue;
    if (layer.kind() == LayerKind::Adjustment) {
      const auto settings = adjustment_settings_from_layer(layer);
      auto rect = clip;
      if (!layer.bounds().empty()) rect = intersect_rect(rect, layer.bounds());
      for (int y = rect.y; y < rect.y + rect.height; ++y) for (int x = rect.x; x < rect.x + rect.width; ++x) {
        const auto color = target.cmy.sample_color(x, y);
        const auto key = target.black.sample_color(x, y);
        // Photoshop thresholds a rounded CMY luminance, multiplied by inverted
        // K and rounded again. Black output is K-only, never four full inks.
        // Five levels over 4096 native colors pin both rounding stages.
        const auto gray = (30 * color.color.red + 59 * color.color.green + 11 * color.color.blue + 50) / 100;
        const auto value = (gray * key.color.red + 127) / 255 >= settings->threshold.level ? 255 : 0;
        const auto amount = layer_mask_alpha_at(layer, x, y) * layer.opacity() *
            render_detail::layer_fill_opacity_for_render(layer) * target.group_coverage(x, y);
        const auto mix = [amount](std::uint8_t before, int after) {
          return clamp_byte(before + (after - before) * amount);
        };
        target.cmy.store_color(x, y, {mix(color.color.red, 255), mix(color.color.green, 255),
                                     mix(color.color.blue, 255)}, color.alpha);
        const auto k = mix(key.color.red, value);
        target.black.store_color(x, y, {k, k, k}, key.alpha);
      }
    } else if (layer.kind() != LayerKind::Group) {
      target.black_source = &black[i];
      render_detail::composite_pixel_layer(target, layer, clip, nullptr, true, nullptr);
    } else if (layer.blend_mode() == BlendMode::PassThrough &&
               render_detail::group_fill_factor_for_render(layer) == 1.0F) {
      std::optional<render_detail::CompositeSnapshot> before_cmy, before_black;
      if (layer.opacity() < 1.0F) {
        before_cmy.emplace(target.cmy, clip); before_black.emplace(target.black, clip);
      }
      target.group_masks.push_back(&layer);
      composite_native_layers(target, layer.children(), black[i].children(), clip);
      target.group_masks.pop_back();
      if (before_cmy) {
        render_detail::fade_toward_snapshot(target.cmy, *before_cmy, clip, layer.opacity());
        render_detail::fade_toward_snapshot(target.black, *before_black, clip, layer.opacity());
      }
    } else {
      const auto rect = intersect_rect(clip, layer_render_bounds(layer));
      if (rect.empty()) continue;
      NativeTarget isolated(rect);
      composite_native_layers(isolated, layer.children(), black[i].children(), rect);
      for (int y = rect.y; y < rect.y + rect.height; ++y) for (int x = rect.x; x < rect.x + rect.width; ++x) {
        const auto color = isolated.cmy.sample_color(x, y);
        const auto alpha = color.alpha * layer_mask_alpha_at(layer, x, y) * layer.opacity() *
                           render_detail::group_fill_factor_for_render(layer);
        target.paint(x, y, color.color, isolated.black.sample_color(x, y).color, alpha, layer.blend_mode());
      }
    }
  }
}

// The document profile's transforms, built once per imported color space: building one
// from a table-based press profile costs tens of milliseconds, and a brush stroke renders
// once per dab. Entries keep the color space alive, so a pointer key is never reused.
struct InkTransforms {
  std::optional<CmykToRgbTransform> to_rgb;
  std::optional<RgbToCmykTransform> to_ink;
};

std::shared_ptr<const InkTransforms> ink_transforms(const std::shared_ptr<const PsdNativeColorSpace>& space,
                                                    std::span<const std::uint8_t> profile) {
  static std::mutex mutex;
  static std::list<std::pair<std::shared_ptr<const PsdNativeColorSpace>, std::shared_ptr<const InkTransforms>>> entries;
  // Held while building, so parallel strips of the first render wait instead of building twice.
  const std::lock_guard lock(mutex);
  const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.first == space; });
  if (found != entries.end()) {
    entries.splice(entries.begin(), entries, found);
    return entries.front().second;
  }
  auto built = std::make_shared<InkTransforms>();
  built->to_rgb = CmykToRgbTransform::from_icc_profile(profile);
  built->to_ink = RgbToCmykTransform::from_icc_profile(profile);
  transform_builds.fetch_add(2, std::memory_order_relaxed);
  entries.emplace_front(space, built);
  if (entries.size() > 8U) entries.pop_back();
  return built;
}

// Separates `region` (buffer-local) of RGB or RGBA pixels into inverted CMYK. Without a
// usable profile, the profile-free inverse of rgb_to_native_color_space: no black ink.
PixelBuffer ink_pixels(const PixelBuffer& pixels, Rect region, const RgbToCmykTransform* to_ink) {
  PixelBuffer inks(region.width, region.height, PixelFormat{ColorMode::CMYK, BitDepth::UInt8, 4});
  std::vector<std::uint8_t> rgb(static_cast<std::size_t>(region.width) * 3U);
  const auto channels = static_cast<std::size_t>(pixels.format().channels);
  for (int y = 0; y < region.height; ++y) {
    const auto* src = pixels.row(region.y + y).data() + static_cast<std::size_t>(region.x) * channels;
    for (int x = 0; x < region.width; ++x, src += channels) std::copy_n(src, 3, rgb.data() + x * 3);
    auto* dst = inks.row(y).data();
    if (to_ink != nullptr) {
      to_ink->convert(rgb.data(), dst, static_cast<std::size_t>(region.width));
    } else {
      for (int x = 0; x < region.width; ++x) {
        std::copy_n(rgb.data() + x * 3, 3, dst + x * 4);
        dst[x * 4 + 3] = 255;
      }
    }
  }
  separated_pixels.fetch_add(static_cast<std::uint64_t>(region.width) * static_cast<std::uint64_t>(region.height),
                             std::memory_order_relaxed);
  return inks;
}

// Whether a layer's pixels are still its imported samples, so its own ink planes apply. A
// painted layer keeps differing from its import, usually at the same byte stroke after
// stroke, so the offset of the last difference found per layer is checked first and a
// full comparison runs only when that byte matches again.
bool pixels_match_import(LayerId layer, std::span<const std::uint8_t> pixels,
                         std::span<const std::uint8_t> imported) {
  if (pixels.data() == imported.data()) return true;
  static std::mutex mutex;
  static std::unordered_map<LayerId, std::size_t> last_difference;
  {
    const std::lock_guard lock(mutex);
    const auto found = last_difference.find(layer);
    if (found != last_difference.end() && found->second < pixels.size() &&
        pixels[found->second] != imported[found->second]) return false;
  }
  const auto difference = std::mismatch(pixels.begin(), pixels.end(), imported.begin()).first;
  if (difference == pixels.end()) return true;
  const std::lock_guard lock(mutex);
  if (last_difference.size() >= 4096U) last_difference.clear();
  last_difference[layer] = static_cast<std::size_t>(difference - pixels.begin());
  return false;
}

VectorFill channel_fill(const VectorFill& fill, bool black, const RgbToCmykTransform* to_ink) {
  if (fill.native_cmyk) {
    auto reference = fill;
    reference.native_cmyk.reset();
    if (reference == fill.native_cmyk->reference) {
      return black ? fill.native_cmyk->black : fill.native_cmyk->cmy;
    }
  }
  auto converted = fill;
  converted.native_cmyk.reset();
  std::vector<RgbColor> colors{fill.color};
  for (const auto& stop : fill.gradient.color_stops) colors.push_back(stop.color);
  PixelBuffer rgb(static_cast<int>(colors.size()), 1, PixelFormat::rgb8());
  for (std::size_t i = 0; i < colors.size(); ++i) {
    auto* p = rgb.pixel(static_cast<int>(i), 0);
    p[0] = colors[i].red; p[1] = colors[i].green; p[2] = colors[i].blue;
  }
  const auto inks = ink_pixels(rgb, Rect::from_size(rgb.width(), rgb.height()), to_ink);
  for (std::size_t i = 0; i < colors.size(); ++i) {
    const auto* p = inks.pixel(static_cast<int>(i), 0);
    const auto color = black ? RgbColor{p[3], p[3], p[3]} : RgbColor{p[0], p[1], p[2]};
    if (i == 0) converted.color = color;
    else converted.gradient.color_stops[i - 1].color = color;
  }
  return converted;
}

// `origin` places the pixels (the layer's bounds, or the override's). `region`, in buffer
// coordinates, limits the work to the part a render needs; the result is then placed
// there. Without it the whole layer is prepared.
ChannelPixels prepare_pixels(const Layer& layer, const PixelBuffer* replacement, Rect origin,
                             std::optional<Rect> region, Rect canvas, const PatternStore& patterns,
                             const PsdNativeColorSpace& space, const RgbToCmykTransform* to_ink) {
  if (const auto* shape = layer.vector_shape(); shape != nullptr && replacement == nullptr &&
      shape->fill.kind != VectorFillKind::Pattern && shape->stroke.content.kind != VectorFillKind::Pattern &&
      shape->parts.empty()) {
    const auto raster = [&](bool black) {
      auto content = *shape;
      content.fill = channel_fill(content.fill, black, to_ink);
      content.stroke.content = channel_fill(content.stroke.content, black, to_ink);
      auto proxy = layer;
      proxy.set_vector_shape(std::move(content));
      update_vector_shape_raster(proxy, canvas, &patterns);
      return proxy;
    };
    const auto cmy = raster(false);
    const auto black = raster(true);
    return {cmy.pixels(), black.pixels(), cmy.bounds()};
  }
  const auto& pixels = replacement != nullptr ? *replacement : layer.pixels();
  const auto& native = layer.psd_native_colors();
  const auto* imported = native && native->imported ? &native->imported->pixels() : nullptr;
  const auto size = static_cast<std::size_t>(pixels.width()) * static_cast<std::size_t>(pixels.height());
  const bool original = replacement == nullptr && native && native->space.get() == &space &&
      native->width == pixels.width() && native->height == pixels.height() && imported != nullptr &&
      imported->format() == pixels.format() && imported->byte_size() == pixels.byte_size() &&
      std::all_of(native->planes.begin(), native->planes.end(), [size](const auto& p) { return p.size() == size; }) &&
      pixels_match_import(layer.id(), pixels.data(), imported->data());
  const auto area = region.value_or(Rect::from_size(pixels.width(), pixels.height()));
  std::optional<PixelBuffer> converted;
  if (!original) converted = ink_pixels(pixels, area, to_ink);
  // Keep RGB versus RGBA here: the compositor uses that distinction to recognize
  // an actual Background for knockout, not merely an opaque ordinary layer.
  ChannelPixels result{PixelBuffer(area.width, area.height, pixels.format()),
                       PixelBuffer(area.width, area.height, pixels.format()),
                       region ? Rect{origin.x + area.x, origin.y + area.y, area.width, area.height} : origin};
  const auto channels = static_cast<std::size_t>(pixels.format().channels);
  for (int y = 0; y < area.height; ++y) {
    const auto* src = pixels.row(area.y + y).data() + static_cast<std::size_t>(area.x) * channels;
    auto* cmy = result.cmy.row(y).data();
    auto* black = result.black.row(y).data();
    for (int x = 0; x < area.width; ++x) {
      const auto i = static_cast<std::size_t>(area.y + y) * pixels.width() + area.x + x;
      const auto* ink = converted ? converted->pixel(x, y) : nullptr;
      for (int c = 0; c < 3; ++c) cmy[c] = original ? native->planes[c][i] : ink[c];
      black[0] = black[1] = black[2] = original ? native->planes[3][i] : ink[3];
      if (pixels.format().channels == 4) cmy[3] = black[3] = src[3];
      src += pixels.format().channels; cmy += pixels.format().channels; black += pixels.format().channels;
    }
  }
  return result;
}

// Per-layer channel preparation is cached, not performed during every repaint.
// Entries retain the source color-space owner so pointer keys cannot be reused.
class ChannelCache {
public:
  // The whole layer's channels at its current revision, if prepared before.
  std::optional<ChannelPixels> find(const Layer& layer, Rect canvas,
                                    const std::shared_ptr<const PsdNativeColorSpace>& space) {
    const std::lock_guard lock(mutex_);
    return find_locked(layer, canvas, space);
  }
  ChannelPixels get(const Layer& layer, Rect canvas, const PatternStore& patterns,
                    const std::shared_ptr<const PsdNativeColorSpace>& space, const RgbToCmykTransform* to_ink) {
    const std::lock_guard lock(mutex_);
    if (auto found = find_locked(layer, canvas, space)) return std::move(*found);
    auto pixels = prepare_pixels(layer, nullptr, layer.bounds(), std::nullopt, canvas, patterns, *space, to_ink);
    const auto bytes = pixels.cmy.byte_size() + pixels.black.byte_size();
    constexpr std::size_t budget = 128U * 1024U * 1024U;
    while (!entries_.empty() && (bytes_ + bytes > budget || entries_.size() >= 1024U)) {
      bytes_ -= entries_.back().pixels.cmy.byte_size() + entries_.back().pixels.black.byte_size();
      entries_.pop_back();
    }
    if (bytes <= budget) {
      entries_.push_front(Entry{layer.render_revision(), space, canvas.width, canvas.height, pixels});
      bytes_ += bytes;
    }
    return pixels;
  }
private:
  std::optional<ChannelPixels> find_locked(const Layer& layer, Rect canvas,
                                           const std::shared_ptr<const PsdNativeColorSpace>& space) {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
      return entry.revision == layer.render_revision() && entry.space == space &&
             entry.width == canvas.width && entry.height == canvas.height;
    });
    if (found == entries_.end()) return std::nullopt;
    entries_.splice(entries_.begin(), entries_, found);
    return entries_.front().pixels;
  }
  struct Entry {
    std::uint64_t revision;
    std::shared_ptr<const PsdNativeColorSpace> space;
    int width;
    int height;
    ChannelPixels pixels;
  };
  std::mutex mutex_;
  std::list<Entry> entries_;
  std::size_t bytes_{0};
};

// Prepares what `clip` needs. A layer whose revision has no cached channels (one being
// painted, or an override's pixels) separates only its part inside the clip, so a brush
// dab costs its own rect, not the layer (docs/cmyk.md). Whole layers are cached.
bool prepare_layers(std::vector<Layer>& cmy, std::vector<Layer>& black,
                    const std::vector<Layer>& source, const Document& document, Rect clip,
                    const RgbToCmykTransform* to_ink,
                    const std::vector<render_detail::LayerBoundsOverride>* overrides) {
  static ChannelCache cache;
  cmy = source;
  black = source;
  const auto canvas = Rect::from_size(document.width(), document.height());
  const auto& space = document.metadata().psd_native_color_space;
  for (std::size_t i = 0; i < source.size(); ++i) {
    const auto& layer = source[i];
    const auto* replacement = render_detail::layer_override_for_render(layer, overrides);
    if (replacement) {
      if (replacement->visible) { cmy[i].set_visible(*replacement->visible); black[i].set_visible(*replacement->visible); }
      if (replacement->mask_bounds && layer.mask()) {
        auto mask = *layer.mask(); mask.bounds = *replacement->mask_bounds;
        cmy[i].set_mask(mask); black[i].set_mask(std::move(mask));
      }
    }
    if (layer.kind() == LayerKind::Group) {
      if (!prepare_layers(cmy[i].children(), black[i].children(), layer.children(), document, clip, to_ink,
                          overrides)) return false;
      continue;
    }
    if (layer.kind() == LayerKind::Adjustment) {
      if (replacement) { cmy[i].set_bounds(replacement->bounds); black[i].set_bounds(replacement->bounds); }
      continue;
    }
    const auto* pixels = replacement != nullptr ? replacement->pixels : nullptr;
    const auto& input = pixels != nullptr ? *pixels : layer.pixels();
    if (input.empty()) continue;
    if (input.format() != PixelFormat::rgb8() && input.format() != PixelFormat::rgba8()) return false;
    const auto& patterns = document.metadata().patterns;
    // The part of pixels placed at `origin` that the clip shows: nullopt for all of them. An
    // empty part still replaces the layer's own pixels, which an override may have moved.
    const auto visible_region = [clip](Rect origin, const PixelBuffer& placed) -> std::optional<Rect> {
      const auto visible = intersect_rect(clip, Rect{origin.x, origin.y, placed.width(), placed.height()});
      if (visible.width == placed.width() && visible.height == placed.height()) return std::nullopt;
      return Rect{visible.x - origin.x, visible.y - origin.y, std::max(visible.width, 0), std::max(visible.height, 0)};
    };
    ChannelPixels channels;
    if (pixels != nullptr) {
      const auto region = visible_region(replacement->bounds, *pixels);
      channels = prepare_pixels(layer, pixels, replacement->bounds, region, canvas, patterns, *space, to_ink);
    } else if (auto cached = cache.find(layer, canvas, space)) {
      channels = std::move(*cached);
    } else {
      const auto region = layer.vector_shape() == nullptr ? visible_region(layer.bounds(), input) : std::nullopt;
      channels = region ? prepare_pixels(layer, nullptr, layer.bounds(), region, canvas, patterns, *space, to_ink)
                        : cache.get(layer, canvas, patterns, space, to_ink);
    }
    const auto bounds = channels.bounds;
    cmy[i].clear_vector_shape(); black[i].clear_vector_shape();
    cmy[i].set_pixels(std::move(channels.cmy)); black[i].set_pixels(std::move(channels.black));
    cmy[i].set_bounds(bounds); black[i].set_bounds(bounds);
  }
  return true;
}

}  // namespace

std::optional<PixelBuffer> render_native_cmyk8(
    const Document& document, Rect clip,
    const std::vector<render_detail::LayerBoundsOverride>* overrides) {
  const auto& space = document.metadata().psd_native_color_space;
  if (!space || space->mode != 4 || space->depth != BitDepth::UInt8 ||
      document.color_state().bit_depth != BitDepth::UInt8 ||
      !document.color_state().embedded_icc_profile.empty() ||
      render_detail::raster_view_context != nullptr || !supported_layers(document.layers()) ||
      document_has_artboards(document)) return std::nullopt;  // artboard paper is RGB: the RGB path draws it
  // A flat PSD has already converted its composite through the source profile.
  // With no native layer records there is no ink blend to reconstruct; an RGB
  // round trip through the CMYK gamut would only damage that correct preview. Its
  // retained merged-image inks (flat_composite) serve saves only.
  if (document.layers().size() == 1 && document.layers().front().kind() == LayerKind::Pixel &&
      document.layers().front().vector_shape() == nullptr &&
      (!document.layers().front().psd_native_colors() || document.layers().front().psd_native_colors()->flat_composite))
    return std::nullopt;
  clip = intersect_rect(clip, Rect::from_size(document.width(), document.height()));
  if (clip.empty()) return PixelBuffer{};
  const auto& profile = space->profile.empty() ? default_cmyk_profile() : space->profile;
  const auto transforms = ink_transforms(space, profile);
  const auto* to_ink = transforms->to_ink ? &*transforms->to_ink : nullptr;
  std::vector<Layer> cmy_layers, black_layers;
  if (!prepare_layers(cmy_layers, black_layers, document.layers(), document, clip, to_ink, overrides)) return std::nullopt;
  NativeTarget target(clip);
  composite_native_layers(target, cmy_layers, black_layers, clip);
  const auto cmy = target.cmy.to_pixel_buffer();
  const auto black = target.black.to_pixel_buffer();
  const auto& transform = transforms->to_rgb;
  PixelBuffer output(clip.width, clip.height, PixelFormat::rgba8());
  std::vector<std::uint8_t> inks(static_cast<std::size_t>(clip.width) * 4U);
  std::vector<std::uint8_t> rgb(static_cast<std::size_t>(clip.width) * 3U);
  for (int y = 0; y < clip.height; ++y) {
    const auto* source = cmy.row(y).data();
    const auto* k = black.row(y).data();
    auto* dst = output.row(y).data();
    for (int x = 0; x < clip.width; ++x) {
      const auto i = static_cast<std::size_t>(x) * 4U;
      std::copy_n(source + i, 3, inks.data() + i); inks[i + 3] = k[i];
    }
    if (transform) transform->convert(inks.data(), rgb.data(), static_cast<std::size_t>(clip.width));
    for (int x = 0; x < clip.width; ++x) {
      const auto i = static_cast<std::size_t>(x) * 4U;
      for (std::size_t c = 0; c < 3; ++c) {
        dst[i + c] = source[i + 3] == 0 ? 0 : transform ? rgb[static_cast<std::size_t>(x) * 3U + c]
            : static_cast<std::uint8_t>((static_cast<int>(inks[i + c]) * inks[i + 3] + 127) / 255);
      }
      dst[i + 3] = source[i + 3];
    }
  }
  return output;
}

NativeCmykRenderStats native_cmyk_render_stats() noexcept {
  return {transform_builds.load(std::memory_order_relaxed), separated_pixels.load(std::memory_order_relaxed)};
}

}  // namespace patchy
