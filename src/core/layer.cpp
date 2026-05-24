#include "core/layer.hpp"

#include <algorithm>
#include <stdexcept>

namespace photoslop {

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

std::vector<UnknownPsdBlock>& Layer::unknown_psd_blocks() noexcept {
  return unknown_psd_blocks_;
}

const std::vector<UnknownPsdBlock>& Layer::unknown_psd_blocks() const noexcept {
  return unknown_psd_blocks_;
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

void Layer::add_child(Layer child) {
  children_.push_back(std::move(child));
  kind_ = LayerKind::Group;
}

}  // namespace photoslop
