#include "plugins/gimp/host/image_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace patchy::gimp::host {

PixelRect PixelRect::united(const PixelRect& other) const noexcept {
  if (empty()) return other;
  if (other.empty()) return *this;
  const auto x0 = std::min(x, other.x);
  const auto y0 = std::min(y, other.y);
  const auto x1 = std::max(x + width, other.x + other.width);
  const auto y1 = std::max(y + height, other.y + other.height);
  return {x0, y0, x1 - x0, y1 - y0};
}

PixelRect PixelRect::intersected(const PixelRect& other) const noexcept {
  const auto x0 = std::max(x, other.x);
  const auto y0 = std::max(y, other.y);
  const auto x1 = std::min(x + width, other.x + other.width);
  const auto y1 = std::min(y + height, other.y + other.height);
  if (x1 <= x0 || y1 <= y0) return {};
  return {x0, y0, x1 - x0, y1 - y0};
}

ImageModel::ImageModel(const link::RunRequest& request, std::uint8_t* input, std::uint8_t* current,
                       const std::uint8_t* mask)
    : image_width_(request.image_width),
      image_height_(request.image_height),
      layer_x_(request.layer_x),
      layer_y_(request.layer_y),
      width_(request.layer_width),
      height_(request.layer_height),
      bit_depth_(request.bit_depth),
      channels_(request.channels),
      bytes_per_channel_(request.bit_depth == 8 ? 1u : request.bit_depth == 16 ? 2u : 4u),
      bpp_(request.channels * (request.bit_depth == 8 ? 1u : request.bit_depth == 16 ? 2u : 4u)),
      protect_alpha_(request.protect_alpha),
      layer_name_(request.layer_name),
      image_name_(request.image_name),
      resolution_x_(request.resolution_x),
      resolution_y_(request.resolution_y),
      input_(input),
      current_(current),
      mask_(request.has_mask ? mask : nullptr) {
  selection_ = mask_ != nullptr ? PixelRect{request.mask_x, request.mask_y, request.mask_width, request.mask_height}
                                      .intersected({0, 0, width_, height_})
                                : PixelRect{0, 0, width_, height_};
  const auto columns = (static_cast<std::uint32_t>(width_) + tile_width_ - 1) / tile_width_;
  const auto rows = (static_cast<std::uint32_t>(height_) + tile_height_ - 1) / tile_height_;
  shadow_written_.assign(static_cast<std::size_t>(columns) * rows, 0);
  (void)input_;
}

std::string ImageModel::format_name() const {
  const bool alpha = channels_ == 4;
  if (bit_depth_ == 32) return alpha ? "RGBA float" : "RGB float";
  if (bit_depth_ == 16) return alpha ? "R'G'B'A u16" : "R'G'B' u16";
  return alpha ? "R'G'B'A u8" : "R'G'B' u8";
}

std::int32_t ImageModel::precision() const noexcept {
  if (bit_depth_ == 32) return 600;  // GIMP_PRECISION_FLOAT_LINEAR
  if (bit_depth_ == 16) return 250;  // GIMP_PRECISION_U16_NON_LINEAR
  return 150;                        // GIMP_PRECISION_U8_NON_LINEAR
}

std::int32_t ImageModel::drawable_width(std::int32_t id) const noexcept { return id == kSelectionId ? image_width_ : width_; }
std::int32_t ImageModel::drawable_height(std::int32_t id) const noexcept { return id == kSelectionId ? image_height_ : height_; }
std::uint32_t ImageModel::drawable_bpp(std::int32_t id) const noexcept { return id == kSelectionId ? 1u : bpp_; }
std::string ImageModel::drawable_format(std::int32_t id) const { return id == kSelectionId ? std::string("Y u8") : format_name(); }

bool ImageModel::tile_rect(std::int32_t drawable, std::uint32_t tile, PixelRect& rect) const {
  const auto w = drawable_width(drawable);
  const auto h = drawable_height(drawable);
  const auto columns = (static_cast<std::uint32_t>(w) + tile_width_ - 1) / tile_width_;
  const auto rows = (static_cast<std::uint32_t>(h) + tile_height_ - 1) / tile_height_;
  if (columns == 0 || tile >= columns * rows) return false;
  const auto col = tile % columns;
  const auto row = tile / columns;
  rect.x = static_cast<std::int32_t>(col * tile_width_);
  rect.y = static_cast<std::int32_t>(row * tile_height_);
  rect.width = std::min<std::int32_t>(static_cast<std::int32_t>(tile_width_), w - rect.x);
  rect.height = std::min<std::int32_t>(static_cast<std::int32_t>(tile_height_), h - rect.y);
  return true;
}

std::uint8_t ImageModel::selection_value(std::int32_t image_x, std::int32_t image_y) const noexcept {
  const auto lx = image_x - layer_x_;
  const auto ly = image_y - layer_y_;
  if (lx < 0 || ly < 0 || lx >= width_ || ly >= height_) return 0;
  if (mask_ == nullptr) return 255;
  return mask_[static_cast<std::size_t>(ly) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(lx)];
}

bool ImageModel::read_tile(std::int32_t drawable, std::uint32_t tile, bool shadow, TileData& out) const {
  if (!valid_drawable(drawable)) return false;
  PixelRect r;
  if (!tile_rect(drawable, tile, r)) return false;
  const auto bpp = drawable_bpp(drawable);
  out.drawable_id = drawable;
  out.tile_num = tile;
  out.shadow = shadow ? 1 : 0;
  out.bpp = bpp;
  out.width = static_cast<std::uint32_t>(r.width);
  out.height = static_cast<std::uint32_t>(r.height);
  out.use_shm = 0;
  out.data.assign(static_cast<std::size_t>(r.width) * static_cast<std::size_t>(r.height) * bpp, 0);
  if (drawable == kSelectionId) {
    for (std::int32_t y = 0; y < r.height; ++y) {
      for (std::int32_t x = 0; x < r.width; ++x) {
        out.data[static_cast<std::size_t>(y) * static_cast<std::size_t>(r.width) + static_cast<std::size_t>(x)] =
            shadow ? 0 : selection_value(r.x + x, r.y + y);
      }
    }
    return true;
  }
  const std::size_t stride = static_cast<std::size_t>(width_) * bpp_;
  const std::size_t row_bytes = static_cast<std::size_t>(r.width) * bpp_;
  const std::uint8_t* source = shadow ? shadow_.data() : current_;
  if (shadow && shadow_.empty()) return true;  // never written: zeros, as GIMP serves it
  for (std::int32_t y = 0; y < r.height; ++y) {
    std::memcpy(out.data.data() + static_cast<std::size_t>(y) * row_bytes,
                source + static_cast<std::size_t>(r.y + y) * stride + static_cast<std::size_t>(r.x) * bpp_, row_bytes);
  }
  return true;
}

bool ImageModel::write_tile(const TileData& tile) {
  if (tile.drawable_id != kLayerId) return false;  // the selection is read-only here
  PixelRect r;
  if (!tile_rect(tile.drawable_id, tile.tile_num, r)) return false;
  if (tile.bpp != bpp_ || tile.width != static_cast<std::uint32_t>(r.width) ||
      tile.height != static_cast<std::uint32_t>(r.height) || tile.data.size() != std::size_t{tile.width} * tile.height * tile.bpp) {
    return false;
  }
  const std::size_t stride = static_cast<std::size_t>(width_) * bpp_;
  const std::size_t row_bytes = static_cast<std::size_t>(r.width) * bpp_;
  if (tile.shadow != 0) {
    if (shadow_.empty()) shadow_.assign(stride * static_cast<std::size_t>(height_), 0);
    for (std::int32_t y = 0; y < r.height; ++y) {
      std::memcpy(shadow_.data() + static_cast<std::size_t>(r.y + y) * stride + static_cast<std::size_t>(r.x) * bpp_,
                  tile.data.data() + static_cast<std::size_t>(y) * row_bytes, row_bytes);
    }
    shadow_written_[tile.tile_num] = 1;
    return true;
  }
  // A direct write to the drawable (no shadow): still limited to the selection.
  PixelRect area = r.intersected(selection_);
  for (std::int32_t y = area.y; y < area.y + area.height; ++y) {
    for (std::int32_t x = area.x; x < area.x + area.width; ++x) {
      const auto cov = mask_ ? mask_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)] : 255;
      if (cov == 0) continue;
      auto* dst = current_ + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * bpp_;
      const auto* src = tile.data.data() + static_cast<std::size_t>(y - r.y) * row_bytes + static_cast<std::size_t>(x - r.x) * bpp_;
      if (cov == 255 && !protect_alpha_) {
        std::memcpy(dst, src, bpp_);
      } else {
        // Same blend as merge_shadow, one pixel.
        const float t = static_cast<float>(cov) / 255.0f;
        for (std::uint32_t c = 0; c < channels_; ++c) {
          if (protect_alpha_ && channels_ == 4 && c == 3) continue;
          if (bytes_per_channel_ == 1) {
            dst[c] = static_cast<std::uint8_t>(std::lround(dst[c] + (src[c] - dst[c]) * t));
          } else if (bytes_per_channel_ == 2) {
            std::uint16_t a, b;
            std::memcpy(&a, dst + c * 2, 2);
            std::memcpy(&b, src + c * 2, 2);
            const auto v = static_cast<std::uint16_t>(std::lround(a + (b - a) * t));
            std::memcpy(dst + c * 2, &v, 2);
          } else {
            float a, b;
            std::memcpy(&a, dst + c * 4, 4);
            std::memcpy(&b, src + c * 4, 4);
            const float v = a + (b - a) * t;
            std::memcpy(dst + c * 4, &v, 4);
          }
        }
      }
    }
  }
  changed_ = changed_.united(area);
  mark_update(area);
  return true;
}

namespace {

// Reads one channel as a float in 0..1 (integers) or as stored (float).
inline float load(const std::uint8_t* p, std::uint32_t bytes) {
  if (bytes == 1) return static_cast<float>(*p) / 255.0f;
  if (bytes == 2) {
    std::uint16_t v;
    std::memcpy(&v, p, 2);
    return static_cast<float>(v) / 65535.0f;
  }
  float v;
  std::memcpy(&v, p, 4);
  return v;
}

inline void store(std::uint8_t* p, std::uint32_t bytes, float v) {
  if (bytes == 1) {
    *p = static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
  } else if (bytes == 2) {
    const auto u = static_cast<std::uint16_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 65535.0f));
    std::memcpy(p, &u, 2);
  } else {
    std::memcpy(p, &v, 4);
  }
}

}  // namespace

PixelRect ImageModel::merge_shadow() {
  if (shadow_.empty()) return {};
  const std::size_t stride = static_cast<std::size_t>(width_) * bpp_;
  PixelRect merged{};
  for (std::uint32_t tile = 0; tile < shadow_written_.size(); ++tile) {
    if (shadow_written_[tile] == 0) continue;
    PixelRect r;
    if (!tile_rect(kLayerId, tile, r)) continue;
    const auto area = r.intersected(selection_);
    if (area.empty()) continue;
    for (std::int32_t y = area.y; y < area.y + area.height; ++y) {
      for (std::int32_t x = area.x; x < area.x + area.width; ++x) {
        const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
        const int cov = mask_ ? mask_[index] : 255;
        if (cov == 0) continue;
        auto* dst = current_ + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * bpp_;
        const auto* src = shadow_.data() + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * bpp_;
        if (cov == 255 && !protect_alpha_) {
          std::memcpy(dst, src, bpp_);
          continue;
        }
        const float t = static_cast<float>(cov) / 255.0f;
        const auto b = bytes_per_channel_;
        if (channels_ == 4 && !protect_alpha_) {
          // Alpha-weighted mix, so colors under transparent pixels do not bleed in.
          const float a0 = load(dst + 3 * b, b);
          const float a1 = load(src + 3 * b, b);
          const float a = a0 + (a1 - a0) * t;
          for (std::uint32_t c = 0; c < 3; ++c) {
            const float c0 = load(dst + c * b, b);
            const float c1 = load(src + c * b, b);
            store(dst + c * b, b, a > 0 ? (c0 * a0 * (1 - t) + c1 * a1 * t) / a : c0 + (c1 - c0) * t);
          }
          store(dst + 3 * b, b, a);
        } else {
          const std::uint32_t color_channels = channels_ == 4 ? 3 : channels_;
          for (std::uint32_t c = 0; c < color_channels; ++c) {
            const float c0 = load(dst + c * b, b);
            const float c1 = load(src + c * b, b);
            store(dst + c * b, b, c0 + (c1 - c0) * t);
          }
        }
      }
    }
    merged = merged.united(area);
  }
  changed_ = changed_.united(merged);
  mark_update(merged);
  return merged;
}

void ImageModel::mark_update(const PixelRect& rect) {
  const auto clipped = rect.intersected({0, 0, width_, height_});
  if (clipped.empty()) return;
  pending_update_ = pending_update_.has_value() ? pending_update_->united(clipped) : clipped;
}

std::optional<PixelRect> ImageModel::take_pending_update() {
  auto out = pending_update_;
  pending_update_.reset();
  return out;
}

namespace {
inline std::uint8_t to_srgb8(float linear) {
  linear = std::clamp(linear, 0.0f, 1.0f);
  const float v = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
  return static_cast<std::uint8_t>(std::lround(v * 255.0f));
}
}  // namespace

ImageModel::Thumbnail ImageModel::thumbnail(std::int32_t drawable, PixelRect source, std::int32_t max_width,
                                            std::int32_t max_height) const {
  Thumbnail t;
  const auto dw = drawable_width(drawable);
  const auto dh = drawable_height(drawable);
  source = source.intersected({0, 0, dw, dh});
  if (source.empty() || max_width <= 0 || max_height <= 0) return t;
  const double scale = std::min({1.0, static_cast<double>(max_width) / source.width, static_cast<double>(max_height) / source.height});
  t.width = std::max(1, static_cast<int>(std::lround(source.width * scale)));
  t.height = std::max(1, static_cast<int>(std::lround(source.height * scale)));
  if (drawable == kSelectionId) {
    t.bpp = 1;
    t.data.resize(static_cast<std::size_t>(t.width) * static_cast<std::size_t>(t.height));
    for (std::int32_t y = 0; y < t.height; ++y)
      for (std::int32_t x = 0; x < t.width; ++x)
        t.data[static_cast<std::size_t>(y) * static_cast<std::size_t>(t.width) + static_cast<std::size_t>(x)] =
            selection_value(source.x + x * source.width / t.width, source.y + y * source.height / t.height);
    return t;
  }
  t.bpp = static_cast<std::int32_t>(channels_);
  t.data.resize(static_cast<std::size_t>(t.width) * static_cast<std::size_t>(t.height) * channels_);
  const std::size_t stride = static_cast<std::size_t>(width_) * bpp_;
  for (std::int32_t y = 0; y < t.height; ++y) {
    // Box filter over the source pixels each thumbnail pixel covers.
    const auto sy0 = source.y + static_cast<std::int32_t>(static_cast<std::int64_t>(y) * source.height / t.height);
    const auto sy1 = std::max(sy0 + 1, source.y + static_cast<std::int32_t>(static_cast<std::int64_t>(y + 1) * source.height / t.height));
    for (std::int32_t x = 0; x < t.width; ++x) {
      const auto sx0 = source.x + static_cast<std::int32_t>(static_cast<std::int64_t>(x) * source.width / t.width);
      const auto sx1 = std::max(sx0 + 1, source.x + static_cast<std::int32_t>(static_cast<std::int64_t>(x + 1) * source.width / t.width));
      float sum[4] = {0, 0, 0, 0};
      int n = 0;
      for (auto sy = sy0; sy < sy1; ++sy) {
        for (auto sx = sx0; sx < sx1; ++sx) {
          const auto* p = current_ + static_cast<std::size_t>(sy) * stride + static_cast<std::size_t>(sx) * bpp_;
          for (std::uint32_t c = 0; c < channels_; ++c) sum[c] += load(p + c * bytes_per_channel_, bytes_per_channel_);
          ++n;
        }
      }
      auto* out = t.data.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(t.width) + static_cast<std::size_t>(x)) * channels_;
      for (std::uint32_t c = 0; c < channels_; ++c) {
        const float v = sum[c] / static_cast<float>(n);
        out[c] = bit_depth_ == 32 && c < 3 ? to_srgb8(v) : static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
      }
    }
  }
  return t;
}

std::vector<std::uint8_t> ImageModel::pixel(std::int32_t x, std::int32_t y) const {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return {};
  const auto* p = current_ + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)) * bpp_;
  return {p, p + bpp_};
}

}  // namespace patchy::gimp::host
