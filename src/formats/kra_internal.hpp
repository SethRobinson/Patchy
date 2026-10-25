#pragma once

// Shared pieces of the .kra reader (docs/kra.md): the color space a layer's samples are in,
// its tiled pixel data decoded into one buffer, and the conversions to Patchy's buffers.

#include "core/adjustment_layer.hpp"
#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"
#include "core/vector_shape.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::kra::detail {

enum class ColorModel : std::uint8_t { Rgb, Gray, Cmyk, Lab, Alpha, Unsupported };
enum class SampleType : std::uint8_t { U8, U16, F16, F32 };

// A layer's color space, from its colorspacename ("RGBA", "RGBA16", "RGBAF32", "GRAYAU16",
// "CMYK", "CMYKA16", "LABA", ...).
struct ColorSpace {
  ColorModel model{ColorModel::Unsupported};
  SampleType sample{SampleType::U8};
  std::string name;

  [[nodiscard]] int channels() const noexcept;      // alpha included
  [[nodiscard]] int sample_bytes() const noexcept;  // per channel
  [[nodiscard]] int pixel_bytes() const noexcept { return channels() * sample_bytes(); }
  [[nodiscard]] bool is_float() const noexcept { return sample == SampleType::F16 || sample == SampleType::F32; }
};

[[nodiscard]] ColorSpace parse_color_space(std::string_view name);

// A tiled paint device read into one interleaved buffer in Krita's own channel order (the
// samples as stored, little-endian), covering `bounds` in layer coordinates (tile origins
// plus the node's x/y). Pixels outside every stored tile hold the default pixel.
struct RawImage {
  Rect bounds{};
  int pixel_bytes{0};
  std::vector<std::uint8_t> data;           // bounds.width * bounds.height * pixel_bytes
  std::vector<std::uint8_t> default_pixel;  // pixel_bytes
  bool default_is_transparent{true};        // the default pixel's alpha (or value) is zero
};

struct TileReadLimits {
  std::int64_t max_side{300000};
  std::uint64_t max_pixels{1ULL << 28};
};

// Reads one tile file ("VERSION 2", the TILEWIDTH/TILEHEIGHT/PIXELSIZE/DATA header, then
// "x,y,LZF,size" records). `default_pixel` comes from the ".defaultpixel" entry (zero
// bytes when absent). Nullopt with `reason` set when the data is damaged or past the limits;
// an empty layer (no tiles) gives an image with empty bounds.
[[nodiscard]] std::optional<RawImage> read_tiles(std::span<const std::uint8_t> file, int expected_pixel_bytes,
                                                 std::vector<std::uint8_t> default_pixel, std::int32_t offset_x,
                                                 std::int32_t offset_y, const TileReadLimits& limits,
                                                 std::uint64_t& pixel_budget, std::string& reason);

// A filter or fill layer's settings (".filterconfig": a <params> list of named values).
struct FilterConfig {
  std::string name;  // Krita's filter or generator id ("levels", "color", ...)
  int version{0};
  std::map<std::string, std::string> params;

  [[nodiscard]] const std::string* find(const std::string& key) const;
  [[nodiscard]] double number(const std::string& key, double fallback) const;
  [[nodiscard]] bool flag(const std::string& key, bool fallback) const;
};

[[nodiscard]] FilterConfig parse_filter_config(std::string name, std::span<const std::uint8_t> xml);

// The Patchy adjustment a Krita filter maps to (docs/kra.md "Filters"). Nullopt for filters
// with no adjustment equivalent. `approximate` is set when the mapping is close but not
// Krita's exact math. `blend` receives the blend mode the adjustment layer needs to come
// close (Luminosity for Krita's lightness-only Levels), Normal otherwise.
[[nodiscard]] std::optional<AdjustmentSettings> adjustment_from_filter(const FilterConfig& config, bool& approximate,
                                                                       BlendMode& blend);

// A fill layer's paint for Krita's "color" generator; nullopt for the other generators.
[[nodiscard]] std::optional<VectorFill> fill_from_generator(const FilterConfig& config);

// A vector layer's content.svg as Patchy layers (shape and text layers through the SVG
// importer), in the document's pixel space: Krita writes shape coordinates in points, so
// the root is re-sized to the canvas in pixels. Patterns the shapes use are adopted into
// `target`. Throws when the SVG cannot be read.
[[nodiscard]] std::vector<Layer> read_vector_layer(std::span<const std::uint8_t> svg, Document& target,
                                                   std::vector<std::string>& notices);

// A float from IEEE 754 half precision bits.
[[nodiscard]] float half_to_float(std::uint16_t bits) noexcept;

}  // namespace patchy::kra::detail
