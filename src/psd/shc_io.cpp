#include "psd/shc_io.hpp"

#include "core/contour_editing.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_io_internal.hpp"
#include "support/string_utils.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace patchy::psd {

namespace {

constexpr std::size_t kMaxFileBytes = 8U * 1024U * 1024U;
constexpr std::uint32_t kMaxContours = 65536U;
constexpr std::uint32_t kMaxNameUnits = 4096U;
// A contour maps 256 input levels, so no file needs more points than that.
constexpr std::uint16_t kMaxFilePoints = 256U;
constexpr std::uint32_t kRecordVersion = 2U;
constexpr std::uint16_t kCurveVersion = 2U;
constexpr std::size_t kRecordTailBytes = 8U;

std::uint16_t level_u16(float value) {
  if (!std::isfinite(value)) {
    return 0U;
  }
  return static_cast<std::uint16_t>(std::clamp(std::lround(value), 0L, 255L));
}

}  // namespace

std::optional<ShcReadResult> read_shc(std::span<const std::uint8_t> bytes, std::string& error) {
  error.clear();
  if (bytes.size() > kMaxFileBytes) {
    error = PATCHY_TRANSLATE_NOOP("QObject", "The contour file is too large");
    return std::nullopt;
  }
  if (bytes.size() < 10U || bytes[0] != '8' || bytes[1] != 'B' || bytes[2] != 'F' || bytes[3] != 'S') {
    error = PATCHY_TRANSLATE_NOOP("QObject", "Not a Photoshop contour file");
    return std::nullopt;
  }
  BigEndianReader reader(bytes);
  reader.skip(4U);
  if (reader.read_u16() != 1U) {
    error = PATCHY_TRANSLATE_NOOP("QObject", "Unsupported contour file version");
    return std::nullopt;
  }
  const auto count = reader.read_u32();
  ShcReadResult result;
  bool damaged_tail = false;
  for (std::uint32_t index = 0; index < count && index < kMaxContours; ++index) {
    try {
      if (reader.read_u32() != kRecordVersion) {
        damaged_tail = true;
        break;
      }
      const auto name_units = reader.read_u32();
      if (name_units > kMaxNameUnits) {
        damaged_tail = true;
        break;
      }
      std::vector<std::uint16_t> units;
      units.reserve(name_units);
      for (std::uint32_t unit = 0; unit < name_units; ++unit) {
        units.push_back(reader.read_u16());
      }
      if (const auto terminator = std::find(units.begin(), units.end(), std::uint16_t{0}); terminator != units.end()) {
        units.erase(terminator, units.end());
      }
      if (reader.read_u16() != kCurveVersion) {
        damaged_tail = true;
        break;
      }
      const auto point_count = reader.read_u16();
      if (point_count > kMaxFilePoints) {
        damaged_tail = true;
        break;
      }
      StyleContour contour;
      contour.points.reserve(point_count);
      for (std::uint16_t point_index = 0; point_index < point_count; ++point_index) {
        StyleContourPoint point;
        point.y = static_cast<float>(std::min<std::uint16_t>(reader.read_u16(), 255U));
        point.x = static_cast<float>(std::min<std::uint16_t>(reader.read_u16(), 255U));
        contour.points.push_back(point);
      }
      for (auto& point : contour.points) {
        point.corner = reader.read_u8() == 0U;
      }
      reader.skip(kRecordTailBytes);
      if (contour.points.size() < kMinContourPoints) {
        result.warnings.emplace_back(PATCHY_TRANSLATE_NOOP("QObject", "Skipped a contour with fewer than two points"));
        continue;
      }
      contour.name = photoshop_zstring_display_text(utf16_units_to_utf8(units));
      if (contour.name.empty()) {
        contour.name = kCustomContourName;
      }
      // Sorted, clamped and free of equal inputs, the way the renderer reads it.
      auto normalized = editable_style_contour(contour);
      normalized.name = contour.name;
      result.contours.push_back(std::move(normalized));
    } catch (const std::exception&) {
      damaged_tail = true;
      break;
    }
  }
  if (damaged_tail || count > kMaxContours) {
    result.warnings.emplace_back(
        PATCHY_TRANSLATE_NOOP("QObject", "The file is damaged past the last decoded contour"));
  }
  if (result.contours.empty()) {
    error = PATCHY_TRANSLATE_NOOP("QObject", "The file holds no usable contours");
    return std::nullopt;
  }
  return result;
}

std::vector<std::uint8_t> write_shc(std::span<const StyleContour> contours) {
  BigEndianWriter writer;
  for (const auto character : {'8', 'B', 'F', 'S'}) {
    writer.write_u8(static_cast<std::uint8_t>(character));
  }
  writer.write_u16(1U);
  writer.write_u32(static_cast<std::uint32_t>(std::min<std::size_t>(contours.size(), kMaxContours)));
  std::size_t written = 0;
  for (const auto& source : contours) {
    if (written++ >= kMaxContours) {
      break;
    }
    // Round first, so two points a fraction apart cannot land on one input level.
    auto rounded = source;
    for (auto& point : rounded.points) {
      point.x = static_cast<float>(level_u16(point.x));
      point.y = static_cast<float>(level_u16(point.y));
    }
    auto contour = editable_style_contour(rounded);
    if (contour.points.size() > kMaxFilePoints) {
      contour.points.resize(kMaxFilePoints);
    }
    auto units = utf8_to_utf16(source.name.empty() ? std::string_view(kCustomContourName) : std::string_view(source.name));
    if (units.size() > kMaxNameUnits - 1U) {
      units.resize(kMaxNameUnits - 1U);
      // Never end on half of a surrogate pair.
      if (!units.empty() && units.back() >= 0xD800U && units.back() <= 0xDBFFU) {
        units.pop_back();
      }
    }
    writer.write_u32(kRecordVersion);
    writer.write_u32(static_cast<std::uint32_t>(units.size() + 1U));
    for (const auto unit : units) {
      writer.write_u16(unit);
    }
    writer.write_u16(0U);
    writer.write_u16(kCurveVersion);
    writer.write_u16(static_cast<std::uint16_t>(contour.points.size()));
    for (const auto& point : contour.points) {
      writer.write_u16(level_u16(point.y));
      writer.write_u16(level_u16(point.x));
    }
    for (const auto& point : contour.points) {
      writer.write_u8(point.corner ? 0U : 1U);
    }
    for (std::size_t pad = 0; pad < kRecordTailBytes; ++pad) {
      writer.write_u8(0U);
    }
  }
  return writer.bytes();
}

}  // namespace patchy::psd
