#pragma once

// The one-layer image a GIMP plug-in sees while it runs on a Patchy layer, and its tile
// server (docs/gimp-plugins.md, "Running"). The layer's pixels live in the
// shared file: `input` is the untouched copy, `current` is what the plug-in reads and
// what merged results go to, and Patchy shows `current` live on its canvas. Writes go
// to the shadow buffer first, as GIMP's protocol has it, and reach `current` only on
// gimp-drawable-merge-shadow, through the selection.

#include "plugins/gimp/gimp_protocol.hpp"
#include "plugins/gimp/gimp_host_link.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace patchy::gimp::host {

inline constexpr std::int32_t kImageId = 1;
inline constexpr std::int32_t kLayerId = 2;
inline constexpr std::int32_t kSelectionId = 3;
inline constexpr std::int32_t kDisplayId = 1;

struct PixelRect {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
  [[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0; }
  [[nodiscard]] PixelRect united(const PixelRect& other) const noexcept;
  [[nodiscard]] PixelRect intersected(const PixelRect& other) const noexcept;
};

class ImageModel {
public:
  ImageModel(const link::RunRequest& request, std::uint8_t* input, std::uint8_t* current, const std::uint8_t* mask);

  // Geometry and format.
  [[nodiscard]] std::int32_t image_width() const noexcept { return image_width_; }
  [[nodiscard]] std::int32_t image_height() const noexcept { return image_height_; }
  [[nodiscard]] std::int32_t layer_x() const noexcept { return layer_x_; }
  [[nodiscard]] std::int32_t layer_y() const noexcept { return layer_y_; }
  [[nodiscard]] std::int32_t width() const noexcept { return width_; }
  [[nodiscard]] std::int32_t height() const noexcept { return height_; }
  [[nodiscard]] std::uint32_t channels() const noexcept { return channels_; }
  [[nodiscard]] std::uint32_t bit_depth() const noexcept { return bit_depth_; }
  [[nodiscard]] bool has_alpha() const noexcept { return channels_ == 4; }
  [[nodiscard]] std::uint32_t bytes_per_pixel() const noexcept { return bpp_; }
  // babl format name of the layer ("R'G'B'A u8", "R'G'B'A u16", "RGBA float").
  [[nodiscard]] std::string format_name() const;
  // GIMP precision enum value of the image.
  [[nodiscard]] std::int32_t precision() const noexcept;
  [[nodiscard]] const std::string& layer_name() const noexcept { return layer_name_; }
  [[nodiscard]] const std::string& image_name() const noexcept { return image_name_; }
  [[nodiscard]] double resolution_x() const noexcept { return resolution_x_; }
  [[nodiscard]] double resolution_y() const noexcept { return resolution_y_; }

  // The selection: bounds in layer pixels (the whole layer without a selection), and
  // whether there is one at all.
  [[nodiscard]] bool has_selection() const noexcept { return mask_ != nullptr; }
  [[nodiscard]] PixelRect selection_bounds() const noexcept { return selection_; }

  [[nodiscard]] bool valid_drawable(std::int32_t id) const noexcept { return id == kLayerId || id == kSelectionId; }
  // Drawable geometry: the layer, or the image-sized selection channel.
  [[nodiscard]] std::int32_t drawable_width(std::int32_t id) const noexcept;
  [[nodiscard]] std::int32_t drawable_height(std::int32_t id) const noexcept;
  [[nodiscard]] std::uint32_t drawable_bpp(std::int32_t id) const noexcept;
  [[nodiscard]] std::string drawable_format(std::int32_t id) const;

  // Tiles (tile_width x tile_height, numbered row by row). False when out of range.
  [[nodiscard]] bool read_tile(std::int32_t drawable, std::uint32_t tile, bool shadow, TileData& out) const;
  [[nodiscard]] bool write_tile(const TileData& tile);

  // GIMP merges the shadow buffer into the drawable through the selection. Returns the
  // area that changed (layer pixels).
  PixelRect merge_shadow();
  // Every area merged so far (for the run's result).
  [[nodiscard]] PixelRect changed_area() const noexcept { return changed_; }
  // An area to show on Patchy's canvas; cleared by take_pending_update.
  void mark_update(const PixelRect& rect);
  [[nodiscard]] std::optional<PixelRect> take_pending_update();

  // A small RGB(A) 8-bit copy of a region scaled to fit max_width x max_height (the
  // thumbnail procedures plug-in previews use).
  struct Thumbnail {
    std::int32_t width{0};
    std::int32_t height{0};
    std::int32_t bpp{0};
    std::vector<std::uint8_t> data;
  };
  [[nodiscard]] Thumbnail thumbnail(std::int32_t drawable, PixelRect source, std::int32_t max_width,
                                    std::int32_t max_height) const;

  // One pixel of the current layer in its own format (gimp-drawable-get-pixel).
  [[nodiscard]] std::vector<std::uint8_t> pixel(std::int32_t x, std::int32_t y) const;

private:
  [[nodiscard]] bool tile_rect(std::int32_t drawable, std::uint32_t tile, PixelRect& rect) const;
  [[nodiscard]] std::uint8_t selection_value(std::int32_t image_x, std::int32_t image_y) const noexcept;

  std::int32_t image_width_;
  std::int32_t image_height_;
  std::int32_t layer_x_;
  std::int32_t layer_y_;
  std::int32_t width_;
  std::int32_t height_;
  std::uint32_t bit_depth_;
  std::uint32_t channels_;
  std::uint32_t bytes_per_channel_;
  std::uint32_t bpp_;
  bool protect_alpha_;
  std::string layer_name_;
  std::string image_name_;
  double resolution_x_;
  double resolution_y_;
  const std::uint8_t* input_;
  std::uint8_t* current_;
  const std::uint8_t* mask_;  // layer-sized coverage, or null
  PixelRect selection_;
  std::uint32_t tile_width_{128};
  std::uint32_t tile_height_{128};
  std::vector<std::uint8_t> shadow_;
  std::vector<std::uint8_t> shadow_written_;  // per tile
  PixelRect changed_{};
  std::optional<PixelRect> pending_update_;
};

}  // namespace patchy::gimp::host
