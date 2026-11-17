#include "core/pattern_sampler.hpp"

#include <algorithm>
#include <mutex>

namespace patchy {

namespace {

// Floor division for a positive divisor.
std::int64_t floor_div(std::int64_t value, std::int64_t divisor) noexcept {
  const auto quotient = value / divisor;
  return (value % divisor != 0 && value < 0) ? quotient - 1 : quotient;
}

}  // namespace

PatternTileSums::PatternTileSums(const PixelBuffer& tile) : width_(tile.width()), height_(tile.height()) {
  if (width_ <= 0 || height_ <= 0) {
    width_ = 0;
    height_ = 0;
    table_.assign(4, 0U);
    channels_ = 4;
    return;
  }
  const auto tile_channels = tile.format().channels;
  bool opaque = true;
  if (tile_channels >= 4) {
    for (std::int32_t y = 0; y < height_ && opaque; ++y) {
      for (std::int32_t x = 0; x < width_; ++x) {
        if (tile.pixel(x, y)[3] != 255U) {
          opaque = false;
          break;
        }
      }
    }
  }
  channels_ = opaque ? 3 : 4;
  const auto columns = static_cast<std::size_t>(width_) + 1U;
  const auto channels = static_cast<std::size_t>(channels_);
  table_.assign(columns * (static_cast<std::size_t>(height_) + 1U) * channels, 0U);
  // Texels read like PatternTileSampler::texel(): the first bytes of each pixel.
  const auto stride = bytes_per_pixel(tile.format());
  for (std::int32_t y = 0; y < height_; ++y) {
    const auto* source = tile.pixel(0, y);
    std::array<std::uint32_t, 4> running{};
    const auto* above = table_.data() + static_cast<std::size_t>(y) * columns * channels;
    auto* out = table_.data() + (static_cast<std::size_t>(y) + 1U) * columns * channels;
    for (std::int32_t x = 0; x < width_; ++x) {
      const auto* px = source + static_cast<std::size_t>(x) * stride;
      const auto cell = (static_cast<std::size_t>(x) + 1U) * channels;
      for (std::size_t channel = 0; channel < channels; ++channel) {
        running[channel] += px[channel];  // modulo 2^32 (see the class comment)
        out[cell + channel] = above[cell + channel] + running[channel];
      }
    }
  }
}

void PatternTileSums::prefix(std::int64_t x, std::int64_t y, std::array<std::uint32_t, 4>& sums) const noexcept {
  if (width_ <= 0 || height_ <= 0) {
    sums = {};
    return;
  }
  const auto periods_x = floor_div(x, width_);
  const auto periods_y = floor_div(y, height_);
  const auto rest_x = static_cast<std::int32_t>(x - periods_x * width_);
  const auto rest_y = static_cast<std::int32_t>(y - periods_y * height_);
  // Unsigned (modulo 2^32) products: negative period counts wrap consistently.
  const auto qx = static_cast<std::uint32_t>(static_cast<std::uint64_t>(periods_x));
  const auto qy = static_cast<std::uint32_t>(static_cast<std::uint64_t>(periods_y));
  const auto* whole = entry(width_, height_);
  const auto* full_rows = entry(rest_x, height_);
  const auto* full_columns = entry(width_, rest_y);
  const auto* corner = entry(rest_x, rest_y);
  for (int channel = 0; channel < channels_; ++channel) {
    sums[static_cast<std::size_t>(channel)] = qx * qy * whole[channel] + qx * full_columns[channel] +
                                              qy * full_rows[channel] + corner[channel];
  }
}

std::shared_ptr<const PatternTileSums> shared_pattern_tile_sums(const PixelBuffer& tile) {
  // Each entry keeps a copy of the tile it was built from: the copy shares the
  // storage, so the storage address cannot be reused by another tile while the
  // entry lives, and an in-place edit of the original detaches it to a new
  // address (a cache miss), never a stale hit.
  struct Entry {
    PixelBuffer tile;
    std::shared_ptr<const PatternTileSums> sums;
  };
  static std::mutex mutex;
  static std::vector<Entry> entries;  // most recent last
  constexpr std::size_t kMaxEntries = 2;
  const auto same_tile = [&](const Entry& entry) {
    return entry.tile.data().data() == tile.data().data() && entry.tile.width() == tile.width() &&
           entry.tile.height() == tile.height() && entry.tile.format() == tile.format();
  };
  const std::lock_guard lock(mutex);
  const auto found = std::find_if(entries.begin(), entries.end(), same_tile);
  if (found != entries.end()) {
    auto entry = std::move(*found);
    entries.erase(found);
    entries.push_back(std::move(entry));
    return entries.back().sums;
  }
  // Built under the lock: parallel strips of one render wait for the first
  // build instead of each building their own copy.
  auto sums = std::make_shared<const PatternTileSums>(tile);
  if (entries.size() >= kMaxEntries) {
    entries.erase(entries.begin());
  }
  entries.push_back(Entry{tile, sums});
  return sums;
}

}  // namespace patchy
