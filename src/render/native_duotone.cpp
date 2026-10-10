#include "render/native_duotone.hpp"

#include "core/psd_source_colors.hpp"
#include "core/smart_object.hpp"
#include "render/layer_compositor.hpp"

#include <list>
#include <mutex>

namespace patchy {
namespace {

struct GrayPixels {
  PixelBuffer pixels;
  Rect bounds;
};

std::optional<GrayPixels> prepare_gray(const Layer& layer, Rect canvas, const PatternStore& patterns,
                                      const PsdNativeColorSpace& space) {
  if (layer.kind() == LayerKind::Adjustment || layer_is_smart_object(layer) ||
      !layer.layer_style().empty() || !layer.layer_style().satins.empty()) return std::nullopt;
  if (const auto* shape = layer.vector_shape()) {
    if (!shape->parts.empty()) return std::nullopt;
    const auto gray_fill = [](const VectorFill& fill) -> std::optional<VectorFill> {
      if (fill.kind == VectorFillKind::None) return fill;
      if (!fill.native_gray || fill.kind == VectorFillKind::Pattern ||
          (fill.kind == VectorFillKind::Gradient && fill.gradient.form != GradientDefinitionForm::Solid))
        return std::nullopt;
      auto reference = fill;
      reference.native_gray.reset();
      if (reference != fill.native_gray->reference) return std::nullopt;
      return fill.native_gray->gray;
    };
    auto content = *shape;
    if (content.stroke.fill_enabled) {
      const auto fill = gray_fill(content.fill);
      if (!fill) return std::nullopt;
      content.fill = *fill;
    }
    if (content.stroke.enabled) {
      const auto stroke = gray_fill(content.stroke.content);
      if (!stroke) return std::nullopt;
      content.stroke.content = *stroke;
    }
    auto proxy = layer;
    proxy.set_vector_shape(std::move(content));
    update_vector_shape_raster(proxy, canvas, &patterns);
    return GrayPixels{proxy.pixels(), proxy.bounds()};
  }
  const auto& pixels = layer.pixels();
  if (pixels.empty()) return GrayPixels{pixels, layer.bounds()};
  const auto& source = layer.psd_native_colors();
  if (!source || source->space.get() != &space || !source->imported ||
      pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3 ||
      source->width != pixels.width() || source->height != pixels.height() ||
      source->planes[0].size() != static_cast<std::size_t>(pixels.width()) * pixels.height()) return std::nullopt;
  const auto& original = source->imported->pixels();
  if (original.format() != pixels.format() || original.byte_size() != pixels.byte_size() ||
      (original.data().data() != pixels.data().data() &&
       !std::equal(pixels.data().begin(), pixels.data().end(), original.data().begin()))) return std::nullopt;
  auto gray = pixels;
  auto bytes = gray.data();
  for (std::size_t i = 0; i < source->planes[0].size(); ++i) {
    auto* pixel = bytes.data() + i * pixels.format().channels;
    pixel[0] = pixel[1] = pixel[2] = source->planes[0][i];
  }
  return GrayPixels{std::move(gray), layer.bounds()};
}

class GrayCache {
public:
  std::optional<GrayPixels> get(const Layer& layer, const Document& document) {
    const std::lock_guard lock(mutex_);
    const auto& space = document.metadata().psd_native_color_space;
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
      return entry.revision == layer.render_revision() && entry.space == space &&
          entry.width == document.width() && entry.height == document.height();
    });
    if (found != entries_.end()) {
      entries_.splice(entries_.begin(), entries_, found);
      return entries_.front().gray;
    }
    auto gray = prepare_gray(layer, Rect::from_size(document.width(), document.height()),
                             document.metadata().patterns, *space);
    const auto bytes = gray ? gray->pixels.byte_size() : 0U;
    constexpr std::size_t budget = 64U * 1024U * 1024U;
    while (!entries_.empty() && (bytes_ + bytes > budget || entries_.size() >= 1024U)) {
      if (entries_.back().gray) bytes_ -= entries_.back().gray->pixels.byte_size();
      entries_.pop_back();
    }
    if (bytes <= budget) {
      entries_.push_front({layer.render_revision(), space, document.width(), document.height(), gray});
      bytes_ += bytes;
    }
    return gray;
  }
private:
  struct Entry {
    std::uint64_t revision;
    std::shared_ptr<const PsdNativeColorSpace> space;
    int width;
    int height;
    std::optional<GrayPixels> gray;
  };
  std::list<Entry> entries_;
  std::mutex mutex_;
  std::size_t bytes_{0};
};

bool prepare_layers(std::vector<Layer>& target, const std::vector<Layer>& source, const Document& document) {
  static GrayCache cache;
  target = source;
  for (std::size_t i = 0; i < source.size(); ++i) {
    if (source[i].kind() == LayerKind::Group) {
      if (!source[i].layer_style().empty() || !source[i].layer_style().satins.empty() ||
          !prepare_layers(target[i].children(), source[i].children(), document)) return false;
      continue;
    }
    const auto gray = cache.get(source[i], document);
    if (!gray) return false;
    target[i].clear_vector_shape();
    target[i].set_pixels(gray->pixels);
    target[i].set_bounds(gray->bounds);
  }
  return true;
}

}  // namespace

std::optional<PixelBuffer> render_native_duotone8(
    const Document& document, Rect clip,
    const std::vector<render_detail::LayerBoundsOverride>* overrides, bool display_colors) {
  const auto& space = document.metadata().psd_native_color_space;
  if (!space || space->mode != 8 || space->depth != BitDepth::UInt8 || !space->duotone_colors ||
      document.color_state().bit_depth != BitDepth::UInt8 ||
      !document.color_state().embedded_icc_profile.empty() ||
      render_detail::raster_view_context != nullptr || (overrides && !overrides->empty())) return std::nullopt;
  clip = intersect_rect(clip, Rect::from_size(document.width(), document.height()));
  if (clip.empty()) return PixelBuffer{};
  std::vector<Layer> layers;
  if (!prepare_layers(layers, document.layers(), document)) return std::nullopt;
  render_detail::IsolatedClipGroupTarget target(clip);
  render_detail::composite_layers(target, layers, clip, nullptr, false, nullptr, &document.metadata().patterns);
  auto output = target.to_pixel_buffer();
  if (display_colors) {
    auto pixels = output.data();
    for (std::size_t i = 0; i < pixels.size(); i += 4U) {
      const auto color = (*space->duotone_colors)[pixels[i]];
      pixels[i] = color.red; pixels[i + 1U] = color.green; pixels[i + 2U] = color.blue;
    }
  }
  return output;
}

}  // namespace patchy
