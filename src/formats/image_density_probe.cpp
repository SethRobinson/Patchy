#include "formats/image_density_probe.hpp"

#include <cmath>
#include <cstring>

namespace patchy::formats {

namespace {

constexpr double kInchesPerMeter = 0.0254;
constexpr double kCentimetersPerInch = 2.54;

[[nodiscard]] bool density_is_usable(double horizontal, double vertical) noexcept {
  return std::isfinite(horizontal) && std::isfinite(vertical) && horizontal > 0.0 && vertical > 0.0;
}

[[nodiscard]] std::uint16_t read_u16_be(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) |
                                    static_cast<std::uint16_t>(bytes[offset + 1U]));
}

[[nodiscard]] std::uint32_t read_u32_be(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
         static_cast<std::uint32_t>(bytes[offset + 3U]);
}

[[nodiscard]] std::uint16_t read_u16_le(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset]) |
                                    (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] std::uint32_t read_u32_le(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

constexpr std::uint8_t kPngSignature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

[[nodiscard]] bool looks_like_png(std::span<const std::uint8_t> bytes) noexcept {
  return bytes.size() >= 8U && std::memcmp(bytes.data(), kPngSignature, 8U) == 0;
}

[[nodiscard]] bool looks_like_jpeg(std::span<const std::uint8_t> bytes) noexcept {
  return bytes.size() >= 3U && bytes[0] == 0xFFU && bytes[1] == 0xD8U && bytes[2] == 0xFFU;
}

[[nodiscard]] std::optional<ImageDensity> probe_png_density(std::span<const std::uint8_t> bytes) noexcept {
  // Chunk walk: u32 length, 4-byte type, data, u32 crc. pHYs must precede IDAT, so the
  // scan can stop at the first image-data chunk.
  std::size_t offset = 8;
  while (offset + 8U <= bytes.size()) {
    const auto length = static_cast<std::size_t>(read_u32_be(bytes, offset));
    if (length > bytes.size() - offset - 8U) {
      return std::nullopt;
    }
    const auto* type = bytes.data() + offset + 4U;
    if (std::memcmp(type, "IDAT", 4U) == 0 || std::memcmp(type, "IEND", 4U) == 0) {
      return std::nullopt;
    }
    if (std::memcmp(type, "pHYs", 4U) == 0) {
      if (length < 9U) {
        return std::nullopt;
      }
      const auto x_per_meter = read_u32_be(bytes, offset + 8U);
      const auto y_per_meter = read_u32_be(bytes, offset + 12U);
      const auto unit = bytes[offset + 16U];
      if (unit != 1U) {
        // Unit 0 records an aspect ratio only; there is no absolute density.
        return std::nullopt;
      }
      ImageDensity density;
      density.horizontal_ppi = static_cast<double>(x_per_meter) * kInchesPerMeter;
      density.vertical_ppi = static_cast<double>(y_per_meter) * kInchesPerMeter;
      if (!density_is_usable(density.horizontal_ppi, density.vertical_ppi)) {
        return std::nullopt;
      }
      return density;
    }
    offset += 8U + length + 4U;
  }
  return std::nullopt;
}

// EXIF rational at a TIFF-relative offset. Returns 0.0 on any structural problem.
[[nodiscard]] double read_exif_rational(std::span<const std::uint8_t> tiff, std::size_t offset,
                                        bool little_endian) noexcept {
  if (offset + 8U > tiff.size()) {
    return 0.0;
  }
  const auto numerator = little_endian ? read_u32_le(tiff, offset) : read_u32_be(tiff, offset);
  const auto denominator =
      little_endian ? read_u32_le(tiff, offset + 4U) : read_u32_be(tiff, offset + 4U);
  if (denominator == 0U) {
    return 0.0;
  }
  return static_cast<double>(numerator) / static_cast<double>(denominator);
}

[[nodiscard]] std::optional<ImageDensity> probe_exif_density(std::span<const std::uint8_t> exif) noexcept {
  // exif spans the APP1 payload after the "Exif\0\0" preamble: a TIFF stream.
  if (exif.size() < 8U) {
    return std::nullopt;
  }
  bool little_endian = false;
  if (exif[0] == 0x49U && exif[1] == 0x49U) {
    little_endian = true;
  } else if (exif[0] == 0x4DU && exif[1] == 0x4DU) {
    little_endian = false;
  } else {
    return std::nullopt;
  }
  const auto magic = little_endian ? read_u16_le(exif, 2U) : read_u16_be(exif, 2U);
  if (magic != 42U) {
    return std::nullopt;
  }
  const auto ifd_offset =
      static_cast<std::size_t>(little_endian ? read_u32_le(exif, 4U) : read_u32_be(exif, 4U));
  if (ifd_offset + 2U > exif.size()) {
    return std::nullopt;
  }
  const auto entry_count = little_endian ? read_u16_le(exif, ifd_offset) : read_u16_be(exif, ifd_offset);
  if (entry_count > 512U) {
    return std::nullopt;
  }

  double x_resolution = 0.0;
  double y_resolution = 0.0;
  std::uint16_t resolution_unit = 2;  // TIFF default: inches
  bool saw_resolution_unit_tag = false;

  for (std::uint16_t entry = 0; entry < entry_count; ++entry) {
    const auto entry_offset = ifd_offset + 2U + static_cast<std::size_t>(entry) * 12U;
    if (entry_offset + 12U > exif.size()) {
      return std::nullopt;
    }
    const auto tag = little_endian ? read_u16_le(exif, entry_offset) : read_u16_be(exif, entry_offset);
    const auto type =
        little_endian ? read_u16_le(exif, entry_offset + 2U) : read_u16_be(exif, entry_offset + 2U);
    if (tag == 282U || tag == 283U) {  // XResolution / YResolution, RATIONAL
      if (type != 5U) {
        continue;
      }
      const auto value_offset = static_cast<std::size_t>(
          little_endian ? read_u32_le(exif, entry_offset + 8U) : read_u32_be(exif, entry_offset + 8U));
      const auto value = read_exif_rational(exif, value_offset, little_endian);
      if (tag == 282U) {
        x_resolution = value;
      } else {
        y_resolution = value;
      }
    } else if (tag == 296U) {  // ResolutionUnit, SHORT: 1 none, 2 inch, 3 cm
      if (type == 3U) {
        resolution_unit =
            little_endian ? read_u16_le(exif, entry_offset + 8U) : read_u16_be(exif, entry_offset + 8U);
        saw_resolution_unit_tag = true;
      }
    }
  }

  if (resolution_unit == 1U && saw_resolution_unit_tag) {
    return std::nullopt;  // explicitly unitless: no absolute density
  }
  if (x_resolution <= 0.0 && y_resolution <= 0.0) {
    return std::nullopt;
  }
  if (x_resolution <= 0.0) {
    x_resolution = y_resolution;
  }
  if (y_resolution <= 0.0) {
    y_resolution = x_resolution;
  }
  const double to_ppi = resolution_unit == 3U ? kCentimetersPerInch : 1.0;
  ImageDensity density;
  density.horizontal_ppi = x_resolution * to_ppi;
  density.vertical_ppi = y_resolution * to_ppi;
  if (!density_is_usable(density.horizontal_ppi, density.vertical_ppi)) {
    return std::nullopt;
  }
  return density;
}

[[nodiscard]] std::optional<ImageDensity> probe_jfif_density(std::span<const std::uint8_t> app0) noexcept {
  // app0 spans the APP0 payload: "JFIF\0", u16 version, u8 units, u16 X density, u16 Y density.
  if (app0.size() < 12U || std::memcmp(app0.data(), "JFIF\0", 5U) != 0) {
    return std::nullopt;
  }
  const auto units = app0[7];
  if (units != 1U && units != 2U) {
    return std::nullopt;  // 0 = pixel aspect ratio only
  }
  const auto x_density = read_u16_be(app0, 8U);
  const auto y_density = read_u16_be(app0, 10U);
  const double to_ppi = units == 2U ? kCentimetersPerInch : 1.0;
  ImageDensity density;
  density.horizontal_ppi = static_cast<double>(x_density) * to_ppi;
  density.vertical_ppi = static_cast<double>(y_density) * to_ppi;
  if (!density_is_usable(density.horizontal_ppi, density.vertical_ppi)) {
    return std::nullopt;
  }
  return density;
}

[[nodiscard]] std::optional<ImageDensity> probe_jpeg_density(std::span<const std::uint8_t> bytes) noexcept {
  std::optional<ImageDensity> jfif_density;
  std::size_t offset = 2;
  while (offset + 4U <= bytes.size()) {
    if (bytes[offset] != 0xFFU) {
      return jfif_density;  // lost marker sync; keep whatever was found
    }
    auto marker = bytes[offset + 1U];
    while (marker == 0xFFU && offset + 2U < bytes.size()) {
      ++offset;
      marker = bytes[offset + 1U];
    }
    if (marker == 0xD8U || (marker >= 0xD0U && marker <= 0xD7U) || marker == 0x01U) {
      offset += 2U;  // standalone markers carry no length
      continue;
    }
    if (marker == 0xDAU || marker == 0xD9U) {
      return jfif_density;  // entropy-coded data / end: densities appear before SOS
    }
    if (offset + 4U > bytes.size()) {
      return jfif_density;
    }
    const auto segment_length = static_cast<std::size_t>(read_u16_be(bytes, offset + 2U));
    if (segment_length < 2U || offset + 2U + segment_length > bytes.size()) {
      return jfif_density;
    }
    const auto payload = bytes.subspan(offset + 4U, segment_length - 2U);
    if (marker == 0xE1U && payload.size() > 6U && std::memcmp(payload.data(), "Exif\0\0", 6U) == 0) {
      // EXIF wins over JFIF when both record a density (the camera-file convention
      // Photoshop follows), so return immediately.
      if (auto exif_density = probe_exif_density(payload.subspan(6U)); exif_density.has_value()) {
        return exif_density;
      }
    } else if (marker == 0xE0U && !jfif_density.has_value()) {
      jfif_density = probe_jfif_density(payload);
    }
    offset += 2U + segment_length;
  }
  return jfif_density;
}

}  // namespace

ImageDensityProbe probe_image_density(std::span<const std::uint8_t> bytes) noexcept {
  ImageDensityProbe probe;
  if (looks_like_png(bytes)) {
    probe.container = ImageDensityContainer::Png;
    probe.density = probe_png_density(bytes);
  } else if (looks_like_jpeg(bytes)) {
    probe.container = ImageDensityContainer::Jpeg;
    probe.density = probe_jpeg_density(bytes);
  }
  return probe;
}

}  // namespace patchy::formats
