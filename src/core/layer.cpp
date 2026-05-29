#include "core/layer.hpp"

#include <algorithm>
#include <stdexcept>

namespace patchy {

bool LayerStyle::empty() const noexcept {
  const auto has_enabled_shadow =
      std::any_of(drop_shadows.begin(), drop_shadows.end(), [](const LayerDropShadow& shadow) {
        return shadow.enabled;
      });
  const auto has_enabled_inner_shadow =
      std::any_of(inner_shadows.begin(), inner_shadows.end(), [](const LayerInnerShadow& shadow) {
        return shadow.enabled;
      });
  const auto has_enabled_outer_glow =
      std::any_of(outer_glows.begin(), outer_glows.end(), [](const LayerOuterGlow& glow) {
        return glow.enabled;
      });
  const auto has_enabled_inner_glow =
      std::any_of(inner_glows.begin(), inner_glows.end(), [](const LayerInnerGlow& glow) {
        return glow.enabled;
      });
  const auto has_enabled_color_overlay =
      std::any_of(color_overlays.begin(), color_overlays.end(), [](const LayerColorOverlay& overlay) {
        return overlay.enabled;
      });
  const auto has_enabled_gradient =
      std::any_of(gradient_fills.begin(), gradient_fills.end(), [](const LayerGradientFill& fill) {
        return fill.enabled;
      });
  const auto has_enabled_pattern =
      std::any_of(pattern_overlays.begin(), pattern_overlays.end(), [](const LayerPatternOverlay& pattern) {
        return pattern.enabled;
      });
  const auto has_enabled_stroke = std::any_of(strokes.begin(), strokes.end(), [](const LayerStroke& stroke) {
    return stroke.enabled;
  });
  const auto has_enabled_bevel = std::any_of(bevels.begin(), bevels.end(), [](const LayerBevelEmboss& bevel) {
    return bevel.enabled;
  });
  const auto has_enabled_satin = std::any_of(satins.begin(), satins.end(), [](const LayerSatin& satin) {
    return satin.enabled;
  });
  return !has_enabled_shadow && !has_enabled_inner_shadow && !has_enabled_outer_glow && !has_enabled_inner_glow &&
         !has_enabled_color_overlay && !has_enabled_gradient && !has_enabled_pattern && !has_enabled_stroke &&
         !has_enabled_bevel && !has_enabled_satin;
}

bool Rect::empty() const noexcept {
  return width <= 0 || height <= 0;
}

bool Rect::contains(std::int32_t px, std::int32_t py) const noexcept {
  return px >= x && py >= y && px < x + width && py < y + height;
}

Rect Rect::from_size(std::int32_t width, std::int32_t height) {
  return Rect{0, 0, width, height};
}

Layer::Layer(LayerId id, std::string name, PixelBuffer pixels)
    : id_(id),
      name_(std::move(name)),
      kind_(LayerKind::Pixel),
      bounds_(Rect::from_size(pixels.width(), pixels.height())),
      pixels_(std::move(pixels)) {}

Layer::Layer(LayerId id, std::string name, LayerKind kind)
    : id_(id), name_(std::move(name)), kind_(kind) {}

LayerId Layer::id() const noexcept {
  return id_;
}

const std::string& Layer::name() const noexcept {
  return name_;
}

LayerKind Layer::kind() const noexcept {
  return kind_;
}

bool Layer::visible() const noexcept {
  return visible_;
}

float Layer::opacity() const noexcept {
  return opacity_;
}

BlendMode Layer::blend_mode() const noexcept {
  return blend_mode_;
}

Rect Layer::bounds() const noexcept {
  return bounds_;
}

PixelBuffer& Layer::pixels() noexcept {
  return pixels_;
}

const PixelBuffer& Layer::pixels() const noexcept {
  return pixels_;
}

std::vector<Layer>& Layer::children() noexcept {
  return children_;
}

const std::vector<Layer>& Layer::children() const noexcept {
  return children_;
}

std::map<std::string, std::string>& Layer::metadata() noexcept {
  return metadata_;
}

const std::map<std::string, std::string>& Layer::metadata() const noexcept {
  return metadata_;
}

std::optional<LayerMask>& Layer::mask() noexcept {
  return mask_;
}

const std::optional<LayerMask>& Layer::mask() const noexcept {
  return mask_;
}

std::vector<UnknownPsdBlock>& Layer::unknown_psd_blocks() noexcept {
  return unknown_psd_blocks_;
}

const std::vector<UnknownPsdBlock>& Layer::unknown_psd_blocks() const noexcept {
  return unknown_psd_blocks_;
}

LayerStyle& Layer::layer_style() noexcept {
  return layer_style_;
}

const LayerStyle& Layer::layer_style() const noexcept {
  return layer_style_;
}

Layer Layer::clone_with_id(LayerId id) const {
  auto cloned = *this;
  cloned.id_ = id;
  return cloned;
}

void Layer::set_name(std::string name) {
  name_ = std::move(name);
}

void Layer::set_visible(bool visible) noexcept {
  visible_ = visible;
}

void Layer::set_opacity(float opacity) {
  if (opacity < 0.0F || opacity > 1.0F) {
    throw std::out_of_range("Layer opacity must be in the inclusive range [0, 1]");
  }
  opacity_ = opacity;
}

void Layer::set_blend_mode(BlendMode mode) noexcept {
  blend_mode_ = mode;
}

void Layer::set_bounds(Rect bounds) noexcept {
  bounds_ = bounds;
}

void Layer::set_pixels(PixelBuffer pixels) {
  bounds_ = Rect::from_size(pixels.width(), pixels.height());
  pixels_ = std::move(pixels);
  kind_ = LayerKind::Pixel;
}

void Layer::set_mask(LayerMask mask) {
  if (mask.pixels.format() != PixelFormat::gray8()) {
    throw std::invalid_argument("Layer masks must use 8-bit grayscale pixels");
  }
  if (mask.bounds.width != mask.pixels.width() || mask.bounds.height != mask.pixels.height()) {
    throw std::invalid_argument("Layer mask bounds must match mask pixel dimensions");
  }
  mask_ = std::move(mask);
}

void Layer::clear_mask() noexcept {
  mask_.reset();
}

void Layer::add_child(Layer child) {
  children_.push_back(std::move(child));
  kind_ = LayerKind::Group;
}

}  // namespace patchy
