#include "core/layer.hpp"
#include "core/smart_filter.hpp"
#include "core/vector_shape.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace patchy {

namespace {

constexpr std::array<std::uint8_t, 8> kIdentityBlendIfEntry{0, 0, 255, 255, 0, 0, 255, 255};

BlendIfThresholds decode_blend_if_thresholds(std::span<const std::uint8_t, 4> bytes) noexcept {
  return BlendIfThresholds{bytes[0], bytes[1], bytes[2], bytes[3]};
}

void encode_blend_if_thresholds(std::vector<std::uint8_t>& payload, std::size_t offset,
                                const BlendIfThresholds& thresholds) {
  payload[offset + 0U] = thresholds.black_low;
  payload[offset + 1U] = thresholds.black_high;
  payload[offset + 2U] = thresholds.white_low;
  payload[offset + 3U] = thresholds.white_high;
}

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

bool blend_if_thresholds_are_valid(const BlendIfThresholds& thresholds) noexcept {
  return thresholds.black_low <= thresholds.black_high && thresholds.black_high <= thresholds.white_low &&
         thresholds.white_low <= thresholds.white_high;
}

bool blend_if_is_identity(const LayerBlendIf& settings) noexcept {
  const BlendIfThresholds identity;
  return std::all_of(settings.channels.begin(), settings.channels.end(), [&](const BlendIfChannelRanges& channel) {
    return channel.this_layer == identity && channel.underlying_layer == identity;
  });
}

DecodedLayerBlendIf decode_layer_blend_if(std::span<const std::uint8_t> payload) noexcept {
  DecodedLayerBlendIf decoded;
  if (payload.empty()) {
    return decoded;
  }
  // Photoshop RGB records use four editable entries (Gray, R, G, B), followed
  // by one identity transparency entry. Other sizes/color-mode shapes stay raw.
  if (payload.size() != 40U ||
      !std::equal(kIdentityBlendIfEntry.begin(), kIdentityBlendIfEntry.end(), payload.begin() + 32U)) {
    decoded.status = BlendIfPayloadStatus::Unsupported;
    return decoded;
  }
  for (std::size_t channel = 0; channel < decoded.settings.channels.size(); ++channel) {
    const auto offset = channel * 8U;
    auto& ranges = decoded.settings.channels[channel];
    ranges.this_layer = decode_blend_if_thresholds(
        std::span<const std::uint8_t, 4>(payload.data() + offset, 4U));
    ranges.underlying_layer = decode_blend_if_thresholds(
        std::span<const std::uint8_t, 4>(payload.data() + offset + 4U, 4U));
    if (!blend_if_thresholds_are_valid(ranges.this_layer) ||
        !blend_if_thresholds_are_valid(ranges.underlying_layer)) {
      decoded.settings = {};
      decoded.status = BlendIfPayloadStatus::Unsupported;
      return decoded;
    }
  }
  decoded.status = BlendIfPayloadStatus::Supported;
  return decoded;
}

bool blend_if_payload_has_non_identity_or_unsupported(std::span<const std::uint8_t> payload) noexcept {
  if (payload.empty()) {
    return false;
  }
  const auto decoded = decode_layer_blend_if(payload);
  return decoded.status == BlendIfPayloadStatus::Unsupported || !blend_if_is_identity(decoded.settings);
}

std::vector<std::uint8_t> encode_layer_blend_if(const LayerBlendIf& settings,
                                                std::span<const std::uint8_t> original_payload) {
  if (blend_if_is_identity(settings) && original_payload.empty()) {
    return {};
  }
  if (!std::all_of(settings.channels.begin(), settings.channels.end(), [](const BlendIfChannelRanges& channel) {
        return blend_if_thresholds_are_valid(channel.this_layer) &&
               blend_if_thresholds_are_valid(channel.underlying_layer);
      })) {
    throw std::invalid_argument("Blend If thresholds must remain ordered");
  }

  std::vector<std::uint8_t> encoded;
  if (original_payload.size() == 40U &&
      std::equal(kIdentityBlendIfEntry.begin(), kIdentityBlendIfEntry.end(), original_payload.begin() + 32U)) {
    encoded.assign(original_payload.begin(), original_payload.end());
  } else {
    encoded.resize(40U);
    for (std::size_t offset = 0; offset < encoded.size(); offset += kIdentityBlendIfEntry.size()) {
      std::copy(kIdentityBlendIfEntry.begin(), kIdentityBlendIfEntry.end(), encoded.begin() + offset);
    }
  }
  for (std::size_t channel = 0; channel < settings.channels.size(); ++channel) {
    const auto offset = channel * 8U;
    encode_blend_if_thresholds(encoded, offset, settings.channels[channel].this_layer);
    encode_blend_if_thresholds(encoded, offset + 4U, settings.channels[channel].underlying_layer);
  }
  return encoded;
}

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
  pixel_revision_ = next_layer_revision();
}

Layer::Layer(LayerId id, std::string name, LayerKind kind)
    : id_(id), name_(std::move(name)), kind_(kind) {
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
  pixel_revision_ = next_layer_revision();
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

float Layer::fill_opacity() const noexcept {
  return fill_opacity_;
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
  pixel_revision_ = next_layer_revision();
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

LayerBlendIf Layer::blend_if() const noexcept {
  return decode_layer_blend_if(raw_psd_blending_ranges_).settings;
}

BlendIfPayloadStatus Layer::blend_if_payload_status() const noexcept {
  auto decoded = decode_layer_blend_if(raw_psd_blending_ranges_);
  if (!blend_if_rgb_compatible_ && decoded.status == BlendIfPayloadStatus::Supported &&
      !blend_if_is_identity(decoded.settings)) {
    return BlendIfPayloadStatus::Unsupported;
  }
  return decoded.status;
}

bool Layer::blend_if_rgb_compatible() const noexcept {
  return blend_if_rgb_compatible_;
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

const SmartFilterStack* Layer::smart_filter_stack() const noexcept {
  return smart_filter_stack_.get();
}

std::uint64_t Layer::render_revision() const noexcept {
  return render_revision_;
}

std::uint64_t Layer::content_revision() const noexcept {
  return content_revision_;
}

std::uint64_t Layer::pixel_revision() const noexcept {
  return pixel_revision_;
}

Layer Layer::clone_with_id(LayerId id) const {
  auto cloned = *this;
  cloned.id_ = id;
  return cloned;
}

std::optional<std::uint32_t>
photoshop_layer_id(const Layer& layer) noexcept {
  for (const auto& block : layer.unknown_psd_blocks()) {
    if (block.key != "lyid" || block.payload.size() != 4U) {
      continue;
    }
    const auto id = (static_cast<std::uint32_t>(block.payload[0]) << 24U) |
                    (static_cast<std::uint32_t>(block.payload[1]) << 16U) |
                    (static_cast<std::uint32_t>(block.payload[2]) << 8U) |
                    static_cast<std::uint32_t>(block.payload[3]);
    if (id != 0U) {
      return id;
    }
  }
  return std::nullopt;
}

void set_photoshop_layer_id(Layer& layer, std::uint32_t id) {
  if (id == 0U) {
    throw std::invalid_argument("Photoshop layer id 0 is reserved");
  }
  auto& blocks = layer.unknown_psd_blocks();
  const auto first = std::find_if(blocks.begin(), blocks.end(),
                                  [](const UnknownPsdBlock& block) {
                                    return block.key == "lyid";
                                  });
  const auto index = first == blocks.end()
                         ? blocks.size()
                         : static_cast<std::size_t>(first - blocks.begin());
  auto replacement = first == blocks.end() ? UnknownPsdBlock{} : *first;
  std::erase_if(blocks, [](const UnknownPsdBlock& block) {
    return block.key == "lyid";
  });
  replacement.key = "lyid";
  replacement.payload = {
      static_cast<std::uint8_t>((id >> 24U) & 0xFFU),
      static_cast<std::uint8_t>((id >> 16U) & 0xFFU),
      static_cast<std::uint8_t>((id >> 8U) & 0xFFU),
      static_cast<std::uint8_t>(id & 0xFFU),
  };
  blocks.insert(blocks.begin() +
                    static_cast<std::ptrdiff_t>(std::min(index, blocks.size())),
                std::move(replacement));
}

std::uint32_t
next_photoshop_layer_id(const std::vector<Layer>& layers) {
  std::vector<std::uint32_t> ids;
  const auto collect = [&ids](const auto& self,
                              const std::vector<Layer>& siblings) -> void {
    for (const auto& layer : siblings) {
      if (const auto id = photoshop_layer_id(layer); id.has_value()) {
        ids.push_back(*id);
      }
      self(self, layer.children());
    }
  };
  collect(collect, layers);
  const auto maximum = ids.empty()
                           ? 0U
                           : *std::max_element(ids.begin(), ids.end());
  if (maximum != std::numeric_limits<std::uint32_t>::max()) {
    return maximum + 1U;
  }
  for (std::uint32_t candidate = 1U; candidate != 0U; ++candidate) {
    if (std::find(ids.begin(), ids.end(), candidate) == ids.end()) {
      return candidate;
    }
  }
  return 1U;
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

void Layer::set_fill_opacity(float opacity) {
  if (opacity < 0.0F || opacity > 1.0F) {
    throw std::out_of_range("Layer fill opacity must be in the inclusive range [0, 1]");
  }
  fill_opacity_ = opacity;
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
  pixel_revision_ = next_layer_revision();
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

bool Layer::set_blend_if(const LayerBlendIf& settings, bool replace_unsupported) {
  if (blend_if_payload_status() == BlendIfPayloadStatus::Unsupported && !replace_unsupported) {
    return false;
  }
  auto encoded = encode_layer_blend_if(settings, replace_unsupported ? std::span<const std::uint8_t>{}
                                                                       : std::span<const std::uint8_t>{
                                                                             raw_psd_blending_ranges_});
  if (encoded == raw_psd_blending_ranges_ && blend_if_rgb_compatible_) {
    return true;
  }
  raw_psd_blending_ranges_ = std::move(encoded);
  blend_if_rgb_compatible_ = true;
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
  return true;
}

void Layer::set_blend_if_payload(std::vector<std::uint8_t> payload, bool rgb_compatible) {
  if (payload == raw_psd_blending_ranges_ && rgb_compatible == blend_if_rgb_compatible_) {
    return;
  }
  raw_psd_blending_ranges_ = std::move(payload);
  blend_if_rgb_compatible_ = rgb_compatible;
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
}

void Layer::set_blend_if_rgb_compatible(bool compatible) noexcept {
  blend_if_rgb_compatible_ = compatible;
}

void Layer::set_smart_filter_stack(SmartFilterStack stack) {
  smart_filter_stack_ = std::make_shared<const SmartFilterStack>(std::move(stack));
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
}

const VectorShapeContent* Layer::vector_shape() const noexcept {
  return vector_shape_.get();
}

const LayerVectorMask* Layer::vector_mask() const noexcept {
  return vector_mask_.get();
}

void Layer::set_vector_shape(VectorShapeContent content) {
  vector_shape_ = std::make_shared<const VectorShapeContent>(std::move(content));
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
}

void Layer::clear_vector_shape() noexcept {
  if (!vector_shape_) {
    return;
  }
  vector_shape_.reset();
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
}

void Layer::set_vector_mask(LayerVectorMask mask) {
  vector_mask_ = std::make_shared<const LayerVectorMask>(std::move(mask));
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
}

void Layer::clear_vector_mask() noexcept {
  if (!vector_mask_) {
    return;
  }
  vector_mask_.reset();
  render_revision_ = next_layer_revision();
  content_revision_ = next_layer_revision();
}

void Layer::clear_smart_filter_stack() noexcept {
  if (!smart_filter_stack_) {
    return;
  }
  smart_filter_stack_.reset();
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
