#include "core/layer.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace patchy {

namespace {

// Revision values are handed out app-globally and never reused: after an undo,
// a NEW edit must not reproduce a revision number an abandoned redo branch
// already used with different pixels, or revision-keyed caches (layer
// thumbnails) would serve stale entries.
std::atomic<std::uint64_t> g_layer_revision_counter{0};

std::uint64_t next_layer_revision() noexcept {
  return ++g_layer_revision_counter;
}

// Diagnostics (PATCHY_REV_TRACE=1): prints which mutable accessor bumps which
// layer. Revision churn silently defeats every revision-keyed cache (layer
// thumbnails, style masks, the undo render diff) - this trace is how the
// find_layer const-walk bug was found; keep it for the next hunt.
inline void trace_revision_bump(const char* accessor, const std::string& name) noexcept {
  static const bool enabled = std::getenv("PATCHY_REV_TRACE") != nullptr;
  if (enabled) {
    std::fprintf(stderr, "REVBUMP %s %s\n", accessor, name.c_str());
  }
}

}  // namespace

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
      pixels_(std::move(pixels)) {
  // Fresh layers take globally-unique revisions too: with the default {1} every new
  // layer aliased every other new layer in globally revision-keyed caches (the
  // opaque-bounds cache served layer A's rect for brand-new layer B). Copies still
  // share their source's revisions, which is correct: identical content.
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
}

Layer::Layer(LayerId id, std::string name, LayerKind kind)
    : id_(id), name_(std::move(name)), kind_(kind) {
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
}

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

bool Layer::clipped() const noexcept {
  return clipped_;
}

float Layer::opacity() const noexcept {
  return opacity_;
}

BlendMode Layer::blend_mode() const noexcept {
  return blend_mode_;
}

LayerLockFlags Layer::lock_flags() const noexcept {
  return lock_flags_;
}

Rect Layer::bounds() const noexcept {
  return bounds_;
}

PixelBuffer& Layer::pixels() noexcept {
  trace_revision_bump("pixels", name_);
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
  return pixels_;
}

const PixelBuffer& Layer::pixels() const noexcept {
  return pixels_;
}

std::vector<Layer>& Layer::children() noexcept {
  trace_revision_bump("children", name_);
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
  return children_;
}

const std::vector<Layer>& Layer::children() const noexcept {
  return children_;
}

std::map<std::string, std::string>& Layer::metadata() noexcept {
  trace_revision_bump("metadata", name_);
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
  return metadata_;
}

const std::map<std::string, std::string>& Layer::metadata() const noexcept {
  return metadata_;
}

std::optional<LayerMask>& Layer::mask() noexcept {
  trace_revision_bump("mask", name_);
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
  return mask_;
}

const std::optional<LayerMask>& Layer::mask() const noexcept {
  return mask_;
}

std::vector<std::uint8_t>& Layer::raw_psd_blending_ranges() noexcept {
  return raw_psd_blending_ranges_;
}

const std::vector<std::uint8_t>& Layer::raw_psd_blending_ranges() const noexcept {
  return raw_psd_blending_ranges_;
}

std::vector<std::uint8_t>& Layer::raw_psd_group_boundary_blending_ranges() noexcept {
  return raw_psd_group_boundary_blending_ranges_;
}

const std::vector<std::uint8_t>& Layer::raw_psd_group_boundary_blending_ranges() const noexcept {
  return raw_psd_group_boundary_blending_ranges_;
}

std::vector<UnknownPsdBlock>& Layer::unknown_psd_blocks() noexcept {
  trace_revision_bump("unknown_psd_blocks", name_);
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
  return unknown_psd_blocks_;
}

const std::vector<UnknownPsdBlock>& Layer::unknown_psd_blocks() const noexcept {
  return unknown_psd_blocks_;
}

LayerStyle& Layer::layer_style() noexcept {
  trace_revision_bump("layer_style", name_);
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
  return layer_style_;
}

const LayerStyle& Layer::layer_style() const noexcept {
  return layer_style_;
}

std::uint64_t Layer::render_revision() const noexcept {
  return render_revision_;
}

std::uint64_t Layer::content_revision() const noexcept {
  return content_revision_;
}

Layer Layer::clone_with_id(LayerId id) const {
  auto cloned = *this;
  cloned.id_ = id;
  return cloned;
}

void Layer::set_name(std::string name) {
  name_ = std::move(name);
  render_revision_ = next_layer_revision();
}

void Layer::set_visible(bool visible) noexcept {
  visible_ = visible;
}

void Layer::set_clipped(bool clipped) noexcept {
  clipped_ = clipped;
  // Render revision only: clipping changes how this layer composites, not its
  // local content, so thumbnail/style-mask caches (content-revision keyed) stay
  // valid while the undo render diff still repaints.
  render_revision_ = next_layer_revision();
}

void Layer::set_opacity(float opacity) {
  if (opacity < 0.0F || opacity > 1.0F) {
    throw std::out_of_range("Layer opacity must be in the inclusive range [0, 1]");
  }
  opacity_ = opacity;
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
}

void Layer::set_blend_mode(BlendMode mode) noexcept {
  blend_mode_ = mode;
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
}

void Layer::set_lock_flags(LayerLockFlags flags) noexcept {
  lock_flags_ = flags & kLayerLockAll;
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
}

void Layer::set_bounds(Rect bounds) noexcept {
  bounds_ = bounds;
  render_revision_ = next_layer_revision();
}

void Layer::set_pixels(PixelBuffer pixels) {
  bounds_ = Rect::from_size(pixels.width(), pixels.height());
  pixels_ = std::move(pixels);
  kind_ = LayerKind::Pixel;
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
}

void Layer::set_mask(LayerMask mask) {
  if (mask.pixels.format() != PixelFormat::gray8()) {
    throw std::invalid_argument("Layer masks must use 8-bit grayscale pixels");
  }
  if (mask.bounds.width != mask.pixels.width() || mask.bounds.height != mask.pixels.height()) {
    throw std::invalid_argument("Layer mask bounds must match mask pixel dimensions");
  }
  mask_ = std::move(mask);
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
}

void Layer::clear_mask() noexcept {
  mask_.reset();
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
}

void Layer::add_child(Layer child) {
  children_.push_back(std::move(child));
  kind_ = LayerKind::Group;
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
}

void Layer::mark_render_changed() noexcept {
  render_revision_ = next_layer_revision();
}

}  // namespace patchy
