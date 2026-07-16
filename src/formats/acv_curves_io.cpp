#include "formats/acv_curves_io.hpp"

#include "psd/psd_binary.hpp"

#include <array>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace patchy::acv {

namespace {

constexpr std::uint16_t kBitmapVersion = 1;
constexpr std::uint16_t kCountedVersion = 4;
constexpr std::uint16_t kPhotoshopRgbCurveCount = 5;
constexpr std::uint16_t kMaxCurveCount = 19;
constexpr std::uint16_t kMinPointCount = 2;
constexpr std::uint16_t kMaxPointCount = 19;
constexpr std::size_t kMaxFileBytes = 4096;
constexpr CurveControlPoint kIdentityPoints[]{{0, 0}, {255, 255}};

// Big-endian record reader: psd::BigEndianReader does the numeric reads (ACV
// records are the same big-endian shapes as native PSD `curv` records), plus
// the ACV-specific tag and trailing-zero checks.
class Reader : public psd::BigEndianReader {
public:
  explicit Reader(std::span<const std::uint8_t> bytes) : psd::BigEndianReader(bytes), bytes_(bytes) {}

  [[nodiscard]] bool remaining_bytes_are_zero() const noexcept {
    for (std::size_t index = position(); index < bytes_.size(); ++index) {
      if (bytes_[index] != 0) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool read_tag(std::string_view expected) {
    if (expected.size() > remaining()) {
      return false;
    }
    for (const auto character : expected) {
      if (read_u8() != static_cast<std::uint8_t>(character)) {
        return false;
      }
    }
    return true;
  }

private:
  std::span<const std::uint8_t> bytes_;
};

void write_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
  bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

[[nodiscard]] CurveControlPoints read_points(Reader& reader) {
  const auto count = reader.read_u16();
  if (count < kMinPointCount || count > kMaxPointCount) {
    throw std::runtime_error("Curves preset has an invalid point count");
  }

  CurveControlPoints points;
  points.reserve(count);
  int previous_input = -1;
  for (std::uint16_t point_index = 0; point_index < count; ++point_index) {
    const auto output = reader.read_u16();
    const auto input = reader.read_u16();
    if (input > 255U || output > 255U) {
      throw std::runtime_error("Curves preset point values must be between 0 and 255");
    }
    if (static_cast<int>(input) <= previous_input) {
      throw std::runtime_error("Curves preset inputs must be strictly increasing");
    }
    previous_input = input;
    points.push_back({static_cast<int>(input), static_cast<int>(output)});
  }
  return points;
}

void validate_points(const CurveControlPoints& points) {
  if (points.size() < kMinPointCount || points.size() > kMaxPointCount) {
    throw std::runtime_error("Curves preset has an invalid point count");
  }
  int previous_input = -1;
  for (const auto& point : points) {
    if (point.input < 0 || point.input > 255 || point.output < 0 || point.output > 255) {
      throw std::runtime_error("Curves preset point values must be between 0 and 255");
    }
    if (point.input <= previous_input) {
      throw std::runtime_error("Curves preset inputs must be strictly increasing");
    }
    previous_input = point.input;
  }
}

void write_points(std::vector<std::uint8_t>& bytes, const CurveControlPoints& points) {
  validate_points(points);
  write_u16(bytes, static_cast<std::uint16_t>(points.size()));
  for (const auto& point : points) {
    // Photoshop stores the vertical output coordinate before horizontal input.
    write_u16(bytes, static_cast<std::uint16_t>(point.output));
    write_u16(bytes, static_cast<std::uint16_t>(point.input));
  }
}

void set_rgb_curve(CurvesAdjustment& result, std::uint16_t channel_index, CurveControlPoints points) {
  switch (channel_index) {
    case 0:
      result.rgb = std::move(points);
      break;
    case 1:
      result.red = std::move(points);
      break;
    case 2:
      result.green = std::move(points);
      break;
    case 3:
      result.blue = std::move(points);
      break;
    default:
      break;
  }
}

[[nodiscard]] CurvesAdjustment read_counted(Reader& reader) {
  const auto curve_count = reader.read_u16();
  if (curve_count == 0 || curve_count > kMaxCurveCount) {
    throw std::runtime_error("Curves preset has an invalid curve count");
  }

  CurvesAdjustment result;
  for (std::uint16_t channel_index = 0; channel_index < curve_count; ++channel_index) {
    set_rgb_curve(result, channel_index, read_points(reader));
  }
  return result;
}

[[nodiscard]] CurvesAdjustment read_bitmap_shape(Reader& reader, bool photoshop_u32_bitmap) {
  const auto bitmap = photoshop_u32_bitmap ? reader.read_u32() : reader.read_u16();
  const auto bitmap_bits = static_cast<std::uint16_t>(photoshop_u32_bitmap ? kMaxCurveCount : 16U);
  if ((bitmap & ~((std::uint32_t{1} << kMaxCurveCount) - 1U)) != 0) {
    throw std::runtime_error("Curves preset has an invalid curve bitmap");
  }

  CurvesAdjustment result;
  for (std::uint16_t channel_index = 0; channel_index < bitmap_bits; ++channel_index) {
    if ((bitmap & (std::uint32_t{1} << channel_index)) == 0) {
      continue;
    }
    auto points = read_points(reader);
    set_rgb_curve(result, channel_index, std::move(points));
  }

  if (reader.remaining() == 0) {
    if (bitmap == 0) {
      throw std::runtime_error("Curves preset has an empty curve bitmap");
    }
    return result;
  }

  // Photoshop CS and later may append indexed version-4 records to a version-1
  // bitmap file. This shape is also used by native PSD `curv` blocks.
  if (!reader.read_tag("Crv ")) {
    throw std::runtime_error("Curves preset has invalid extra curve data");
  }
  if (reader.read_u16() != kCountedVersion) {
    throw std::runtime_error("Curves preset has an unsupported extra curve version");
  }
  const auto extra_count = reader.read_u32();
  if (extra_count > kMaxCurveCount) {
    throw std::runtime_error("Curves preset has an invalid extra curve count");
  }
  std::array<bool, kMaxCurveCount> have_extra_channel{};
  for (std::uint32_t item = 0; item < extra_count; ++item) {
    const auto channel_index = reader.read_u16();
    if (channel_index >= kMaxCurveCount || have_extra_channel[channel_index]) {
      throw std::runtime_error("Curves preset has a duplicate or invalid channel index");
    }
    auto points = read_points(reader);
    // The indexed v4 extension repeats and supersedes bitmap-channel records
    // in current Photoshop files.
    set_rgb_curve(result, channel_index, std::move(points));
    have_extra_channel[channel_index] = true;
  }
  return result;
}

[[nodiscard]] CurvesAdjustment read_bitmap(Reader& reader) {
  // Photoshop 2026 writes a 32-bit bitmap even though Adobe's ACV table calls
  // this field 2 bytes. The two shapes are structurally ambiguous when the
  // high word is nonzero, so parse both to completion and prefer Photoshop's
  // current shape when both byte streams happen to be valid.
  struct Candidate {
    CurvesAdjustment curves;
    Reader reader;
  };
  const auto try_shape = [&reader](bool photoshop_u32_bitmap) -> std::optional<Candidate> {
    auto candidate_reader = reader;
    try {
      auto curves = read_bitmap_shape(candidate_reader, photoshop_u32_bitmap);
      if (candidate_reader.remaining() > 3 || !candidate_reader.remaining_bytes_are_zero()) {
        return std::nullopt;
      }
      return Candidate{std::move(curves), std::move(candidate_reader)};
    } catch (const std::runtime_error&) {
      return std::nullopt;
    }
  };

  auto photoshop = try_shape(true);
  auto legacy = try_shape(false);
  auto* selected = photoshop.has_value() ? &photoshop : &legacy;
  if (!selected->has_value()) {
    throw std::runtime_error("Curves preset has malformed version-1 data");
  }
  reader = std::move((*selected)->reader);
  return std::move((*selected)->curves);
}

}  // namespace

CurvesAdjustment read(std::span<const std::uint8_t> bytes) {
  if (bytes.size() > kMaxFileBytes) {
    throw std::runtime_error("Curves preset is too large");
  }
  Reader reader(bytes);
  const auto version = reader.read_u16();
  CurvesAdjustment result;
  if (version == kCountedVersion) {
    result = read_counted(reader);
  } else if (version == kBitmapVersion) {
    result = read_bitmap(reader);
  } else {
    throw std::runtime_error("Unsupported Curves preset version");
  }
  // Native PSD `curv` bodies use the same records and pad the body to a
  // four-byte boundary. ACV files normally need no padding, but accepting at
  // most three zero bytes lets this parser be shared without accepting opaque
  // trailing content.
  if (reader.remaining() > 3 || !reader.remaining_bytes_are_zero()) {
    throw std::runtime_error("Curves preset has trailing data");
  }
  return result;
}

CurvesAdjustment read_file(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open Curves preset");
  }
  file.seekg(0, std::ios::end);
  const auto end = file.tellg();
  if (end < std::streampos{}) {
    throw std::runtime_error("Could not read Curves preset");
  }
  const auto length = static_cast<std::streamoff>(end);
  if (length > static_cast<std::streamoff>(kMaxFileBytes)) {
    throw std::runtime_error("Curves preset is too large");
  }
  file.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
  if (!bytes.empty()) {
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
      throw std::runtime_error("Could not read Curves preset");
    }
  }
  return read(bytes);
}

std::vector<std::uint8_t> write(const CurvesAdjustment& curves) {
  validate_points(curves.rgb);
  validate_points(curves.red);
  validate_points(curves.green);
  validate_points(curves.blue);

  std::vector<std::uint8_t> bytes;
  bytes.reserve(4U + static_cast<std::size_t>(kPhotoshopRgbCurveCount) * 78U);
  write_u16(bytes, kCountedVersion);
  write_u16(bytes, kPhotoshopRgbCurveCount);
  write_points(bytes, curves.rgb);
  write_points(bytes, curves.red);
  write_points(bytes, curves.green);
  write_points(bytes, curves.blue);
  write_points(bytes, CurveControlPoints(std::begin(kIdentityPoints), std::end(kIdentityPoints)));
  return bytes;
}

void write_file(const std::filesystem::path& path, const CurvesAdjustment& curves) {
  const auto bytes = write(curves);
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open Curves preset for writing");
  }
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file) {
    throw std::runtime_error("Could not write Curves preset");
  }
}

}  // namespace patchy::acv
