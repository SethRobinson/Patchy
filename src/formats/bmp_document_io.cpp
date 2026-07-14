#include "formats/bmp_document_io.hpp"

#include "core/blend_math.hpp"
#include "core/layer_render_utils.hpp"
#include "core/palette.hpp"
#include "formats/binary_le.hpp"
#include "formats/palette_io.hpp"
#include "render/layer_compositor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace patchy::bmp {

namespace {

constexpr std::uint32_t kFileHeaderSize = 14;
constexpr std::uint32_t kInfoHeaderSize = 40;
constexpr std::uint32_t kBitmapV4HeaderSize = 108;
constexpr std::uint32_t kBiRgb = 0;
constexpr std::uint32_t kBiBitfields = 3;
constexpr std::uint32_t kLcsSrgb = 0x73524742;

struct BmpHeader {
  std::uint32_t file_size{0};
  std::uint32_t pixel_offset{0};
  std::uint32_t dib_header_size{0};
  std::int32_t width{0};
  std::int32_t height{0};
  bool top_down{false};
  std::uint16_t bit_count{0};
  std::uint32_t compression{0};
  std::uint32_t image_size{0};
  std::int32_t x_pixels_per_meter{0};
  std::int32_t y_pixels_per_meter{0};
  std::uint32_t colors_used{0};
  std::uint32_t red_mask{0};
  std::uint32_t green_mask{0};
  std::uint32_t blue_mask{0};
  std::uint32_t alpha_mask{0};
  std::size_t color_table_offset{0};
};

// The little-endian reader/writer live in formats/binary_le.hpp (shared with ICO/TGA/PCX/
// Aseprite). BMP keeps its historical underrun message via this constructor helper.
[[nodiscard]] LittleEndianReader bmp_reader(std::span<const std::uint8_t> bytes) {
  return LittleEndianReader(bytes, "BMP data ended unexpectedly");
}

[[nodiscard]] std::uint32_t color_key(RgbColor color) noexcept {
  return palette_color_key(color);
}

[[nodiscard]] std::uint32_t checked_u32(std::uint64_t value, const char* message) {
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error(message);
  }
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::size_t checked_size(std::uint64_t value, const char* message) {
  if (value > std::numeric_limits<std::size_t>::max()) {
    throw std::runtime_error(message);
  }
  return static_cast<std::size_t>(value);
}

[[nodiscard]] std::uint32_t palette_capacity_for_bits(std::uint16_t bits) {
  switch (bits) {
    case 2:
      return 4;
    case 4:
      return 16;
    case 8:
      return 256;
    default:
      throw std::runtime_error("Unsupported indexed BMP bit depth");
  }
}

[[nodiscard]] std::uint16_t bit_count_for_encoding(BmpEncoding encoding) {
  switch (encoding) {
    case BmpEncoding::Rgba32:
      return 32;
    case BmpEncoding::Rgb24:
      return 24;
    case BmpEncoding::Indexed8:
      return 8;
    case BmpEncoding::Indexed4:
      return 4;
    case BmpEncoding::Indexed2:
      return 2;
  }
  throw std::runtime_error("Unsupported BMP encoding");
}

[[nodiscard]] std::size_t row_stride_bytes(std::int32_t width, std::uint16_t bit_count) {
  if (width <= 0) {
    throw std::runtime_error("BMP width must be positive");
  }
  const auto bits = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(bit_count);
  return checked_size((((bits + 31U) & ~31ULL) >> 3U), "BMP row is too large");
}

[[nodiscard]] std::uint32_t dots_per_meter_from_ppi(double ppi) noexcept {
  if (!std::isfinite(ppi) || ppi <= 0.0) {
    ppi = 300.0;
  }
  return static_cast<std::uint32_t>(std::clamp(static_cast<int>(std::lround(ppi / 0.0254)), 1, 1000000));
}

[[nodiscard]] double ppi_from_dots_per_meter(std::int32_t dots_per_meter) noexcept {
  // Zero pels-per-meter means the file records no density; Photoshop opens such
  // BMPs at 72 PPI and Patchy's untagged-import policy follows it.
  return dots_per_meter > 0 ? static_cast<double>(dots_per_meter) * 0.0254 : 72.0;
}

[[nodiscard]] BmpHeader read_header(std::span<const std::uint8_t> bytes) {
  auto reader = bmp_reader(bytes);
  if (reader.remaining() < kFileHeaderSize + kInfoHeaderSize) {
    throw std::runtime_error("BMP file is too short");
  }
  if (reader.read_u8() != 'B' || reader.read_u8() != 'M') {
    throw std::runtime_error("File is not a BMP image");
  }

  BmpHeader header;
  header.file_size = reader.read_u32();
  reader.skip(4);
  header.pixel_offset = reader.read_u32();
  const auto dib_start = reader.position();
  header.dib_header_size = reader.read_u32();
  if (header.dib_header_size < kInfoHeaderSize) {
    throw std::runtime_error("Unsupported BMP DIB header");
  }
  if (header.dib_header_size - 4U > reader.remaining()) {
    throw std::runtime_error("BMP DIB header is truncated");
  }

  header.width = reader.read_i32();
  auto raw_height = reader.read_i32();
  const auto planes = reader.read_u16();
  header.bit_count = reader.read_u16();
  header.compression = reader.read_u32();
  header.image_size = reader.read_u32();
  header.x_pixels_per_meter = reader.read_i32();
  header.y_pixels_per_meter = reader.read_i32();
  header.colors_used = reader.read_u32();
  (void)reader.read_u32();

  if (planes != 1) {
    throw std::runtime_error("BMP plane count must be 1");
  }
  if (header.width <= 0 || raw_height == 0 || raw_height == std::numeric_limits<std::int32_t>::min()) {
    throw std::runtime_error("BMP dimensions are invalid");
  }
  header.top_down = raw_height < 0;
  header.height = header.top_down ? -raw_height : raw_height;
  header.color_table_offset = dib_start + header.dib_header_size;

  if (header.dib_header_size >= 56U) {
    header.red_mask = reader.read_u32();
    header.green_mask = reader.read_u32();
    header.blue_mask = reader.read_u32();
    header.alpha_mask = reader.read_u32();
  }

  if (header.file_size != 0 && header.file_size > bytes.size()) {
    throw std::runtime_error("BMP file is truncated");
  }
  if (header.pixel_offset > bytes.size() || header.pixel_offset < kFileHeaderSize + header.dib_header_size) {
    throw std::runtime_error("BMP pixel offset is invalid");
  }
  return header;
}

[[nodiscard]] std::vector<RgbColor> read_palette(std::span<const std::uint8_t> bytes, const BmpHeader& header) {
  const auto capacity = palette_capacity_for_bits(header.bit_count);
  const auto palette_entries = header.colors_used == 0 ? capacity : header.colors_used;
  if (palette_entries == 0 || palette_entries > capacity) {
    throw std::runtime_error("BMP palette size is invalid");
  }
  const auto palette_bytes = static_cast<std::uint64_t>(palette_entries) * 4ULL;
  if (header.color_table_offset > header.pixel_offset ||
      palette_bytes > header.pixel_offset - header.color_table_offset) {
    throw std::runtime_error("BMP palette is truncated");
  }

  std::vector<RgbColor> palette;
  palette.reserve(static_cast<std::size_t>(palette_entries));
  for (std::uint32_t index = 0; index < palette_entries; ++index) {
    const auto offset = header.color_table_offset + static_cast<std::size_t>(index) * 4U;
    palette.push_back(RgbColor{bytes[offset + 2U], bytes[offset + 1U], bytes[offset]});
  }
  return palette;
}

[[nodiscard]] std::vector<RgbColor> read_palette_file(const std::filesystem::path& path) {
  if (path.empty()) {
    throw std::runtime_error("A palette file is required for indexed BMP palette export");
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open BMP palette file");
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (bytes.empty()) {
    throw std::runtime_error("Palette file is empty");
  }

  if (DocumentIo::can_read(bytes)) {
    const auto header = read_header(bytes);
    if (header.bit_count != 2 && header.bit_count != 4 && header.bit_count != 8) {
      throw std::runtime_error("BMP palette file must be an indexed BMP");
    }
    return read_palette(bytes, header);
  }

  // All non-BMP palette formats (JASC/RIFF .pal, .gpl, .hex, .act, .aco, .ase)
  // live in the shared reader.
  return palette_io::read_palette_bytes(bytes).colors;
}

[[nodiscard]] std::uint16_t unpack_index(const std::uint8_t* row, std::int32_t x, std::uint16_t bit_count) {
  switch (bit_count) {
    case 2: {
      const auto byte = row[static_cast<std::size_t>(x) / 4U];
      const auto shift = 6U - static_cast<unsigned>((x % 4) * 2);
      return static_cast<std::uint16_t>((byte >> shift) & 0x03U);
    }
    case 4: {
      const auto byte = row[static_cast<std::size_t>(x) / 2U];
      return static_cast<std::uint16_t>((x % 2) == 0 ? ((byte >> 4U) & 0x0fU) : (byte & 0x0fU));
    }
    case 8:
      return row[x];
    default:
      throw std::runtime_error("Unsupported indexed BMP bit depth");
  }
}

[[nodiscard]] Document read_indexed(std::span<const std::uint8_t> bytes, const BmpHeader& header) {
  if (header.compression != kBiRgb) {
    throw std::runtime_error("Compressed indexed BMP files are not supported");
  }

  const auto palette = read_palette(bytes, header);
  const auto row_stride = row_stride_bytes(header.width, header.bit_count);
  const auto pixel_bytes = checked_size(static_cast<std::uint64_t>(row_stride) *
                                            static_cast<std::uint64_t>(header.height),
                                        "BMP pixel data is too large");
  if (pixel_bytes > bytes.size() - header.pixel_offset) {
    throw std::runtime_error("BMP pixel data is truncated");
  }

  PixelBuffer pixels(header.width, header.height, PixelFormat::rgb8());
  for (std::int32_t file_y = 0; file_y < header.height; ++file_y) {
    const auto document_y = header.top_down ? file_y : (header.height - 1 - file_y);
    const auto* row = bytes.data() + header.pixel_offset + static_cast<std::size_t>(file_y) * row_stride;
    for (std::int32_t x = 0; x < header.width; ++x) {
      const auto palette_index = unpack_index(row, x, header.bit_count);
      if (palette_index >= palette.size()) {
        throw std::runtime_error("BMP pixel references a missing palette color");
      }
      const auto color = palette[palette_index];
      auto* pixel = pixels.pixel(x, document_y);
      pixel[0] = color.red;
      pixel[1] = color.green;
      pixel[2] = color.blue;
    }
  }

  Document document(header.width, header.height, PixelFormat::rgb8());
  document.print_settings().horizontal_ppi = ppi_from_dots_per_meter(header.x_pixels_per_meter);
  document.print_settings().vertical_ppi = ppi_from_dots_per_meter(header.y_pixels_per_meter);
  document.indexed_palette() = DocumentIndexedPalette{palette, header.bit_count};
  document.add_pixel_layer("Imported BMP", std::move(pixels));
  return document;
}

[[nodiscard]] Document read_rgb24(std::span<const std::uint8_t> bytes, const BmpHeader& header) {
  if (header.compression != kBiRgb) {
    throw std::runtime_error("Compressed 24-bit BMP files are not supported");
  }
  const auto row_stride = row_stride_bytes(header.width, header.bit_count);
  const auto pixel_bytes = checked_size(static_cast<std::uint64_t>(row_stride) *
                                            static_cast<std::uint64_t>(header.height),
                                        "BMP pixel data is too large");
  if (pixel_bytes > bytes.size() - header.pixel_offset) {
    throw std::runtime_error("BMP pixel data is truncated");
  }

  PixelBuffer pixels(header.width, header.height, PixelFormat::rgb8());
  for (std::int32_t file_y = 0; file_y < header.height; ++file_y) {
    const auto document_y = header.top_down ? file_y : (header.height - 1 - file_y);
    const auto* row = bytes.data() + header.pixel_offset + static_cast<std::size_t>(file_y) * row_stride;
    for (std::int32_t x = 0; x < header.width; ++x) {
      const auto* source = row + static_cast<std::size_t>(x) * 3U;
      auto* pixel = pixels.pixel(x, document_y);
      pixel[0] = source[2];
      pixel[1] = source[1];
      pixel[2] = source[0];
    }
  }

  Document document(header.width, header.height, PixelFormat::rgb8());
  document.print_settings().horizontal_ppi = ppi_from_dots_per_meter(header.x_pixels_per_meter);
  document.print_settings().vertical_ppi = ppi_from_dots_per_meter(header.y_pixels_per_meter);
  document.add_pixel_layer("Imported BMP", std::move(pixels));
  return document;
}

[[nodiscard]] bool patchy_alpha_masks(const BmpHeader& header) noexcept {
  return header.red_mask == 0x00ff0000U && header.green_mask == 0x0000ff00U &&
         header.blue_mask == 0x000000ffU && header.alpha_mask == 0xff000000U;
}

[[nodiscard]] Document read_rgb32(std::span<const std::uint8_t> bytes, const BmpHeader& header) {
  // Validate that the channel layout is one we can decode. Both BI_RGB (where the
  // fourth byte is conventionally padding) and BITFIELDS with the standard ARGB masks
  // store BGRA bytes, so they share the same byte extraction below.
  if (header.compression != kBiRgb &&
      !(header.compression == kBiBitfields && patchy_alpha_masks(header))) {
    throw std::runtime_error("Unsupported 32-bit BMP channel masks");
  }

  const auto row_stride = row_stride_bytes(header.width, header.bit_count);
  const auto pixel_bytes = checked_size(static_cast<std::uint64_t>(row_stride) *
                                            static_cast<std::uint64_t>(header.height),
                                        "BMP pixel data is too large");
  if (pixel_bytes > bytes.size() - header.pixel_offset) {
    throw std::runtime_error("BMP pixel data is truncated");
  }

  // Always keep the fourth byte as alpha. Photoshop treats the alpha of a 32-bit BI_RGB
  // BMP as a saved channel, and the shared load step decides whether the alpha is
  // meaningful (and should become an editable mask) or is uniform padding to discard.
  PixelBuffer pixels(header.width, header.height, PixelFormat::rgba8());
  for (std::int32_t file_y = 0; file_y < header.height; ++file_y) {
    const auto document_y = header.top_down ? file_y : (header.height - 1 - file_y);
    const auto* row = bytes.data() + header.pixel_offset + static_cast<std::size_t>(file_y) * row_stride;
    for (std::int32_t x = 0; x < header.width; ++x) {
      const auto* source = row + static_cast<std::size_t>(x) * 4U;
      auto* pixel = pixels.pixel(x, document_y);
      pixel[0] = source[2];
      pixel[1] = source[1];
      pixel[2] = source[0];
      pixel[3] = source[3];
    }
  }

  Document document(header.width, header.height, PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = ppi_from_dots_per_meter(header.x_pixels_per_meter);
  document.print_settings().vertical_ppi = ppi_from_dots_per_meter(header.y_pixels_per_meter);
  document.add_pixel_layer("Imported BMP", std::move(pixels));
  return document;
}

class Rgb8RenderTarget {
public:
  explicit Rgb8RenderTarget(PixelBuffer& destination, float initial_alpha)
      : destination_(destination),
        alpha_(static_cast<std::size_t>(destination.width()) * static_cast<std::size_t>(destination.height()),
               clamp_unit(initial_alpha)) {
    if (destination_.format() != PixelFormat::rgb8()) {
      throw std::invalid_argument("BMP RGB render target requires RGB8 pixels");
    }
  }

  void composite_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha, BlendMode mode) {
    alpha = clamp_unit(alpha);
    if (alpha <= 0.0F || x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }

    auto* dst = destination_.pixel(x, y);
    auto& destination_alpha =
        alpha_[static_cast<std::size_t>(y) * static_cast<std::size_t>(destination_.width()) +
               static_cast<std::size_t>(x)];
    const std::array<std::uint8_t, 3> source_rgb{color.red, color.green, color.blue};
    const std::array<std::uint8_t, 3> destination_rgb{dst[0], dst[1], dst[2]};
    const auto blended = composite_blended_rgb(source_rgb, destination_rgb, mode, alpha, destination_alpha);
    dst[0] = blended[0];
    dst[1] = blended[1];
    dst[2] = blended[2];
    destination_alpha = alpha + destination_alpha * (1.0F - alpha);
  }

  [[nodiscard]] render_detail::CompositeSample sample_color(std::int32_t x, std::int32_t y) const noexcept {
    if (x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return {};
    }
    const auto index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(destination_.width()) + static_cast<std::size_t>(x);
    const auto* pixel = destination_.pixel(x, y);
    return render_detail::CompositeSample{RgbColor{pixel[0], pixel[1], pixel[2]}, alpha_[index]};
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentSettings& settings, float amount) {
    amount = clamp_unit(amount);
    if (amount <= 0.0F || x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }
    const auto index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(destination_.width()) + static_cast<std::size_t>(x);
    if (alpha_[index] <= 0.0F) {
      return;
    }
    auto* dst = destination_.pixel(x, y);
    const auto adjusted = apply_adjustment_to_color(RgbColor{dst[0], dst[1], dst[2]}, settings);
    dst[0] = clamp_byte(static_cast<float>(adjusted.red) * amount + static_cast<float>(dst[0]) * (1.0F - amount));
    dst[1] = clamp_byte(static_cast<float>(adjusted.green) * amount + static_cast<float>(dst[1]) * (1.0F - amount));
    dst[2] = clamp_byte(static_cast<float>(adjusted.blue) * amount + static_cast<float>(dst[2]) * (1.0F - amount));
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentLut& lut, float amount) {
    amount = clamp_unit(amount);
    if (amount <= 0.0F || x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }
    const auto index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(destination_.width()) + static_cast<std::size_t>(x);
    if (alpha_[index] <= 0.0F) {
      return;
    }
    auto* dst = destination_.pixel(x, y);
    dst[0] = clamp_byte(static_cast<float>(lut.red[dst[0]]) * amount + static_cast<float>(dst[0]) * (1.0F - amount));
    dst[1] =
        clamp_byte(static_cast<float>(lut.green[dst[1]]) * amount + static_cast<float>(dst[1]) * (1.0F - amount));
    dst[2] = clamp_byte(static_cast<float>(lut.blue[dst[2]]) * amount + static_cast<float>(dst[2]) * (1.0F - amount));
  }

private:
  PixelBuffer& destination_;
  std::vector<float> alpha_;
};

class Rgba8RenderTarget {
public:
  explicit Rgba8RenderTarget(PixelBuffer& destination) : destination_(destination) {
    if (destination_.format() != PixelFormat::rgba8()) {
      throw std::invalid_argument("BMP RGBA render target requires RGBA8 pixels");
    }
  }

  void composite_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha, BlendMode mode) {
    alpha = clamp_unit(alpha);
    if (alpha <= 0.0F || x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }

    auto* dst = destination_.pixel(x, y);
    const auto destination_alpha = static_cast<float>(dst[3]) / 255.0F;
    const std::array<std::uint8_t, 3> source_rgb{color.red, color.green, color.blue};
    const std::array<std::uint8_t, 3> destination_rgb{dst[0], dst[1], dst[2]};
    const auto blended = composite_blended_rgb(source_rgb, destination_rgb, mode, alpha, destination_alpha);
    dst[0] = blended[0];
    dst[1] = blended[1];
    dst[2] = blended[2];
    dst[3] = clamp_byte((alpha + destination_alpha * (1.0F - alpha)) * 255.0F);
  }

  [[nodiscard]] render_detail::CompositeSample sample_color(std::int32_t x, std::int32_t y) const noexcept {
    if (x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return {};
    }
    const auto* pixel = destination_.pixel(x, y);
    return render_detail::CompositeSample{RgbColor{pixel[0], pixel[1], pixel[2]},
                                          static_cast<float>(pixel[3]) / 255.0F};
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentSettings& settings, float amount) {
    amount = clamp_unit(amount);
    if (amount <= 0.0F || x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }
    auto* dst = destination_.pixel(x, y);
    if (dst[3] == 0) {
      return;
    }
    const auto adjusted = apply_adjustment_to_color(RgbColor{dst[0], dst[1], dst[2]}, settings);
    dst[0] = clamp_byte(static_cast<float>(adjusted.red) * amount + static_cast<float>(dst[0]) * (1.0F - amount));
    dst[1] = clamp_byte(static_cast<float>(adjusted.green) * amount + static_cast<float>(dst[1]) * (1.0F - amount));
    dst[2] = clamp_byte(static_cast<float>(adjusted.blue) * amount + static_cast<float>(dst[2]) * (1.0F - amount));
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentLut& lut, float amount) {
    amount = clamp_unit(amount);
    if (amount <= 0.0F || x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }
    auto* dst = destination_.pixel(x, y);
    if (dst[3] == 0) {
      return;
    }
    dst[0] = clamp_byte(static_cast<float>(lut.red[dst[0]]) * amount + static_cast<float>(dst[0]) * (1.0F - amount));
    dst[1] =
        clamp_byte(static_cast<float>(lut.green[dst[1]]) * amount + static_cast<float>(dst[1]) * (1.0F - amount));
    dst[2] = clamp_byte(static_cast<float>(lut.blue[dst[2]]) * amount + static_cast<float>(dst[2]) * (1.0F - amount));
  }

private:
  PixelBuffer& destination_;
};

[[nodiscard]] PixelBuffer render_rgb8_on_white(const Document& document) {
  if (render_detail::layers_have_rendered_underlying_blend_if(document.layers())) {
    // Keep the logical backdrop transparent while Blend If is evaluated, then
    // apply BMP's white matte once at the end. Treating the matte as an opaque
    // underlying layer would make an Underlying Layer range hide pixels that
    // Photoshop leaves visible over transparency.
    PixelBuffer rgba(document.width(), document.height(), PixelFormat::rgba8());
    rgba.clear(0);
    Rgba8RenderTarget rgba_target(rgba);
    const auto canvas = Rect::from_size(document.width(), document.height());
    render_detail::composite_layers(rgba_target, document.layers(), canvas, nullptr, true, nullptr,
                                    &document.metadata().patterns);

    PixelBuffer output(document.width(), document.height(), PixelFormat::rgb8());
    for (std::int32_t y = 0; y < document.height(); ++y) {
      for (std::int32_t x = 0; x < document.width(); ++x) {
        const auto* source = rgba.pixel(x, y);
        auto* destination = output.pixel(x, y);
        const auto alpha = static_cast<int>(source[3]);
        for (int channel = 0; channel < 3; ++channel) {
          destination[channel] = static_cast<std::uint8_t>(
              (static_cast<int>(source[channel]) * alpha + 255 * (255 - alpha) + 127) / 255);
        }
      }
    }
    return output;
  }

  PixelBuffer output(document.width(), document.height(), PixelFormat::rgb8());
  output.clear(255);
  Rgb8RenderTarget target(output, 1.0F);
  const auto canvas = Rect::from_size(document.width(), document.height());
  render_detail::composite_layers(target, document.layers(), canvas, nullptr, true, nullptr,
                                  &document.metadata().patterns);
  return output;
}

[[nodiscard]] PixelBuffer render_rgba8(const Document& document) {
  // A single masked layer is written non-destructively: the original colors stay intact
  // and the mask becomes the alpha channel, so reopening preserves both. Compositing here
  // would instead erase the colors wherever the mask is transparent.
  if (!render_detail::layers_have_rendered_blend_if(document.layers())) {
    if (auto masked = document_alpha_rgba8(document); masked.has_value()) {
      return std::move(*masked);
    }
  }
  PixelBuffer output(document.width(), document.height(), PixelFormat::rgba8());
  output.clear(0);
  Rgba8RenderTarget target(output);
  const auto canvas = Rect::from_size(document.width(), document.height());
  render_detail::composite_layers(target, document.layers(), canvas, nullptr, true, nullptr,
                                  &document.metadata().patterns);
  return output;
}

void write_file_header(LittleEndianWriter& writer, std::uint32_t file_size, std::uint32_t pixel_offset) {
  writer.write_u8('B');
  writer.write_u8('M');
  writer.write_u32(file_size);
  writer.write_u16(0);
  writer.write_u16(0);
  writer.write_u32(pixel_offset);
}

void require_non_empty_document(const Document& document) {
  if (document.width() <= 0 || document.height() <= 0) {
    throw std::runtime_error("Cannot write an empty BMP image");
  }
}

[[nodiscard]] std::vector<std::uint8_t> write_rgba32(const Document& document) {
  require_non_empty_document(document);
  const auto pixels = render_rgba8(document);
  const auto pixel_bytes = checked_u32(static_cast<std::uint64_t>(pixels.width()) *
                                           static_cast<std::uint64_t>(pixels.height()) * 4ULL,
                                       "BMP image is too large to write");
  const auto pixel_offset = kFileHeaderSize + kBitmapV4HeaderSize;
  const auto file_size = checked_u32(static_cast<std::uint64_t>(pixel_offset) + pixel_bytes,
                                     "BMP file is too large to write");

  LittleEndianWriter writer;
  write_file_header(writer, file_size, pixel_offset);
  writer.write_u32(kBitmapV4HeaderSize);
  writer.write_i32(pixels.width());
  writer.write_i32(pixels.height());
  writer.write_u16(1);
  writer.write_u16(32);
  writer.write_u32(kBiBitfields);
  writer.write_u32(pixel_bytes);
  writer.write_i32(static_cast<std::int32_t>(dots_per_meter_from_ppi(document.print_settings().horizontal_ppi)));
  writer.write_i32(static_cast<std::int32_t>(dots_per_meter_from_ppi(document.print_settings().vertical_ppi)));
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(0x00ff0000);
  writer.write_u32(0x0000ff00);
  writer.write_u32(0x000000ff);
  writer.write_u32(0xff000000);
  writer.write_u32(kLcsSrgb);
  for (int index = 0; index < 9; ++index) {
    writer.write_i32(0);
  }
  writer.write_i32(0);
  writer.write_i32(0);
  writer.write_i32(0);

  for (std::int32_t y = pixels.height() - 1; y >= 0; --y) {
    const auto row = pixels.row(y);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* source = row.data() + static_cast<std::size_t>(x) * 4U;
      writer.write_u8(source[2]);
      writer.write_u8(source[1]);
      writer.write_u8(source[0]);
      writer.write_u8(source[3]);
    }
  }
  return std::move(writer.bytes());
}

[[nodiscard]] std::vector<std::uint8_t> write_rgb24(const Document& document) {
  require_non_empty_document(document);
  const auto pixels = render_rgb8_on_white(document);
  const auto row_stride = row_stride_bytes(pixels.width(), 24);
  const auto image_size = checked_u32(static_cast<std::uint64_t>(row_stride) *
                                          static_cast<std::uint64_t>(pixels.height()),
                                      "BMP image is too large to write");
  const auto pixel_offset = kFileHeaderSize + kInfoHeaderSize;
  const auto file_size = checked_u32(static_cast<std::uint64_t>(pixel_offset) + image_size,
                                     "BMP file is too large to write");

  LittleEndianWriter writer;
  write_file_header(writer, file_size, pixel_offset);
  writer.write_u32(kInfoHeaderSize);
  writer.write_i32(pixels.width());
  writer.write_i32(pixels.height());
  writer.write_u16(1);
  writer.write_u16(24);
  writer.write_u32(kBiRgb);
  writer.write_u32(image_size);
  writer.write_i32(static_cast<std::int32_t>(dots_per_meter_from_ppi(document.print_settings().horizontal_ppi)));
  writer.write_i32(static_cast<std::int32_t>(dots_per_meter_from_ppi(document.print_settings().vertical_ppi)));
  writer.write_u32(0);
  writer.write_u32(0);

  std::vector<std::uint8_t> row(row_stride, 0);
  for (std::int32_t y = pixels.height() - 1; y >= 0; --y) {
    std::fill(row.begin(), row.end(), std::uint8_t{0});
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* source = pixels.pixel(x, y);
      auto* destination = row.data() + static_cast<std::size_t>(x) * 3U;
      destination[0] = source[2];
      destination[1] = source[1];
      destination[2] = source[0];
    }
    writer.write_bytes(std::span<const std::uint8_t>(row.data(), row.size()));
  }
  return std::move(writer.bytes());
}

// The quantizer (median cut, color counting, nearest-entry matching) used to live
// here; it moved to core/palette.hpp so palette-mode editing shares it. The calls
// below resolve to the patchy:: versions and behave byte-identically for the RGB8
// buffers this writer feeds them.

struct IndexedPixels {
  std::vector<RgbColor> palette;
  std::vector<std::uint16_t> indices;
};

[[nodiscard]] std::optional<IndexedPixels> try_imported_palette_exact(const Document& document,
                                                                      const PixelBuffer& pixels,
                                                                      std::uint16_t bit_count) {
  const auto& imported = document.indexed_palette();
  if (!imported.has_value() || imported->source_bit_depth != bit_count ||
      imported->colors.size() > palette_capacity_for_bits(bit_count) || imported->colors.empty()) {
    return std::nullopt;
  }

  std::unordered_map<std::uint32_t, std::uint16_t> palette_index;
  palette_index.reserve(imported->colors.size());
  for (std::uint16_t index = 0; index < imported->colors.size(); ++index) {
    const auto [_, inserted] = palette_index.emplace(color_key(imported->colors[index]), index);
    if (!inserted) {
      return std::nullopt;
    }
  }

  IndexedPixels indexed;
  indexed.palette = imported->colors;
  indexed.indices.reserve(static_cast<std::size_t>(pixels.width()) * static_cast<std::size_t>(pixels.height()));
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* pixel = pixels.pixel(x, y);
      const auto found = palette_index.find(color_key(RgbColor{pixel[0], pixel[1], pixel[2]}));
      if (found == palette_index.end()) {
        return std::nullopt;
      }
      indexed.indices.push_back(found->second);
    }
  }
  return indexed;
}

[[nodiscard]] IndexedPixels make_exact_indexed(const Document& document, const PixelBuffer& pixels,
                                               std::uint16_t bit_count, bool use_imported_palette) {
  if (use_imported_palette) {
    if (auto imported = try_imported_palette_exact(document, pixels, bit_count)) {
      return std::move(*imported);
    }
  }

  const auto capacity = palette_capacity_for_bits(bit_count);
  IndexedPixels indexed;
  indexed.indices.reserve(static_cast<std::size_t>(pixels.width()) * static_cast<std::size_t>(pixels.height()));
  std::unordered_map<std::uint32_t, std::uint16_t> palette_index;
  palette_index.reserve(capacity);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* pixel = pixels.pixel(x, y);
      const auto color = RgbColor{pixel[0], pixel[1], pixel[2]};
      const auto key = color_key(color);
      auto found = palette_index.find(key);
      if (found == palette_index.end()) {
        if (indexed.palette.size() >= capacity) {
          throw std::runtime_error("Image has too many colors for exact indexed BMP export");
        }
        const auto index = static_cast<std::uint16_t>(indexed.palette.size());
        indexed.palette.push_back(color);
        found = palette_index.emplace(key, index).first;
      }
      indexed.indices.push_back(found->second);
    }
  }
  return indexed;
}

[[nodiscard]] IndexedPixels make_quantized_indexed(const Document& document, const PixelBuffer& pixels,
                                                   std::uint16_t bit_count, bool use_imported_palette) {
  try {
    return make_exact_indexed(document, pixels, bit_count, use_imported_palette);
  } catch (const std::runtime_error&) {
  }

  const auto capacity = palette_capacity_for_bits(bit_count);
  const auto color_counts = collect_color_counts(pixels);
  IndexedPixels indexed;
  indexed.palette = median_cut_palette(color_counts, capacity);
  indexed.indices.reserve(static_cast<std::size_t>(pixels.width()) * static_cast<std::size_t>(pixels.height()));
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* pixel = pixels.pixel(x, y);
      indexed.indices.push_back(nearest_palette_index(RgbColor{pixel[0], pixel[1], pixel[2]}, indexed.palette));
    }
  }
  return indexed;
}

[[nodiscard]] IndexedPixels make_palette_file_indexed(const PixelBuffer& pixels, std::uint16_t bit_count,
                                                      const std::filesystem::path& palette_path) {
  IndexedPixels indexed;
  indexed.palette = read_palette_file(palette_path);
  const auto capacity = palette_capacity_for_bits(bit_count);
  if (indexed.palette.empty()) {
    throw std::runtime_error("Palette file does not contain any colors");
  }
  if (indexed.palette.size() > capacity) {
    throw std::runtime_error("Palette file has too many colors for the selected BMP depth");
  }

  indexed.indices.reserve(static_cast<std::size_t>(pixels.width()) * static_cast<std::size_t>(pixels.height()));
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* pixel = pixels.pixel(x, y);
      indexed.indices.push_back(nearest_palette_index(RgbColor{pixel[0], pixel[1], pixel[2]}, indexed.palette));
    }
  }
  return indexed;
}

void pack_index(std::vector<std::uint8_t>& row, std::int32_t x, std::uint16_t bit_count, std::uint16_t index) {
  switch (bit_count) {
    case 2: {
      const auto offset = static_cast<std::size_t>(x) / 4U;
      const auto shift = 6U - static_cast<unsigned>((x % 4) * 2);
      row[offset] = static_cast<std::uint8_t>(row[offset] | static_cast<std::uint8_t>((index & 0x03U) << shift));
      return;
    }
    case 4: {
      const auto offset = static_cast<std::size_t>(x) / 2U;
      if ((x % 2) == 0) {
        row[offset] = static_cast<std::uint8_t>(row[offset] | static_cast<std::uint8_t>((index & 0x0fU) << 4U));
      } else {
        row[offset] = static_cast<std::uint8_t>(row[offset] | static_cast<std::uint8_t>(index & 0x0fU));
      }
      return;
    }
    case 8:
      row[static_cast<std::size_t>(x)] = static_cast<std::uint8_t>(index);
      return;
    default:
      throw std::runtime_error("Unsupported indexed BMP bit depth");
  }
}

[[nodiscard]] std::vector<std::uint8_t> write_indexed(const Document& document, WriteOptions options) {
  require_non_empty_document(document);
  const auto bit_count = bit_count_for_encoding(options.encoding);
  auto pixels = render_rgb8_on_white(document);
  if (const auto& editing = document.palette_editing();
      editing.has_value() && !editing->palette.colors.empty()) {
    // Palette-mode WYSIWYG: live layer styles / text can leave the flatten
    // off-palette, which would make Exact export fail. Snap to the editing
    // palette first, exactly like the canvas display and PNG-8 export do.
    PaletteLut lut;
    lut.build(editing->palette.colors);
    (void)apply_palette_to_pixels(pixels, lut, PaletteDither::None, editing->alpha_threshold);
  }
  IndexedPixels indexed;
  switch (options.palette_mode) {
    case BmpPaletteMode::Exact:
      indexed = make_exact_indexed(document, pixels, bit_count, options.use_imported_palette);
      break;
    case BmpPaletteMode::Quantize:
      indexed = make_quantized_indexed(document, pixels, bit_count, options.use_imported_palette);
      break;
    case BmpPaletteMode::PaletteFile:
      indexed = make_palette_file_indexed(pixels, bit_count, options.palette_path);
      break;
  }
  const auto capacity = palette_capacity_for_bits(bit_count);
  if (indexed.palette.empty() || indexed.palette.size() > capacity) {
    throw std::runtime_error("Indexed BMP palette size is invalid");
  }

  const auto row_stride = row_stride_bytes(pixels.width(), bit_count);
  const auto image_size = checked_u32(static_cast<std::uint64_t>(row_stride) *
                                          static_cast<std::uint64_t>(pixels.height()),
                                      "BMP image is too large to write");
  const auto palette_bytes = checked_u32(static_cast<std::uint64_t>(indexed.palette.size()) * 4ULL,
                                         "BMP palette is too large to write");
  const auto pixel_offset = kFileHeaderSize + kInfoHeaderSize + palette_bytes;
  const auto file_size = checked_u32(static_cast<std::uint64_t>(pixel_offset) + image_size,
                                     "BMP file is too large to write");

  LittleEndianWriter writer;
  write_file_header(writer, file_size, pixel_offset);
  writer.write_u32(kInfoHeaderSize);
  writer.write_i32(pixels.width());
  writer.write_i32(pixels.height());
  writer.write_u16(1);
  writer.write_u16(bit_count);
  writer.write_u32(kBiRgb);
  writer.write_u32(image_size);
  writer.write_i32(static_cast<std::int32_t>(dots_per_meter_from_ppi(document.print_settings().horizontal_ppi)));
  writer.write_i32(static_cast<std::int32_t>(dots_per_meter_from_ppi(document.print_settings().vertical_ppi)));
  writer.write_u32(static_cast<std::uint32_t>(indexed.palette.size()));
  writer.write_u32(0);

  for (const auto color : indexed.palette) {
    writer.write_u8(color.blue);
    writer.write_u8(color.green);
    writer.write_u8(color.red);
    writer.write_u8(0);
  }

  std::vector<std::uint8_t> row(row_stride, 0);
  for (std::int32_t y = pixels.height() - 1; y >= 0; --y) {
    std::fill(row.begin(), row.end(), std::uint8_t{0});
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto index = indexed.indices[static_cast<std::size_t>(y) * static_cast<std::size_t>(pixels.width()) +
                                         static_cast<std::size_t>(x)];
      pack_index(row, x, bit_count, index);
    }
    writer.write_bytes(std::span<const std::uint8_t>(row.data(), row.size()));
  }
  return std::move(writer.bytes());
}

}  // namespace

bool DocumentIo::can_read(std::span<const std::uint8_t> bytes) noexcept {
  return bytes.size() >= 2U && bytes[0] == 'B' && bytes[1] == 'M';
}

Document DocumentIo::read(std::span<const std::uint8_t> bytes) {
  const auto header = read_header(bytes);
  switch (header.bit_count) {
    case 2:
    case 4:
    case 8:
      return read_indexed(bytes, header);
    case 24:
      return read_rgb24(bytes, header);
    case 32:
      return read_rgb32(bytes, header);
    default:
      throw std::runtime_error("Unsupported BMP bit depth");
  }
}

Document DocumentIo::read_file(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open BMP file");
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  auto document = read(bytes);
  const auto stem = path.stem().string();
  if (!stem.empty() && !document.layers().empty()) {
    document.layers().front().set_name(stem);
  }
  return document;
}

std::vector<std::uint8_t> DocumentIo::write(const Document& document, WriteOptions options) {
  switch (options.encoding) {
    case BmpEncoding::Rgba32:
      return write_rgba32(document);
    case BmpEncoding::Rgb24:
      return write_rgb24(document);
    case BmpEncoding::Indexed8:
    case BmpEncoding::Indexed4:
    case BmpEncoding::Indexed2:
      return write_indexed(document, options);
  }
  throw std::runtime_error("Unsupported BMP encoding");
}

void DocumentIo::write_file(const Document& document, const std::filesystem::path& path, WriteOptions options) {
  const auto bytes = write(document, options);
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open BMP file for writing");
  }
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file) {
    throw std::runtime_error("Could not write BMP file");
  }
}

}  // namespace patchy::bmp
