// Adjustment-layer codecs for the PSD reader/writer: the Photoshop levl /
// curv / hue2 payloads (hue2 patches in place, curv preserves imported bytes
// exactly), the Patchy CRV2 curves extension, and the private plAD adjustment
// block. Split out of psd_document_io.cpp as a pure move.

#include "psd/psd_document_io.hpp"
#include "psd/psd_io_internal.hpp"

#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_resource.hpp"
#include "core/smart_object.hpp"
#include "core/style_contour.hpp"
#include "core/text_warp.hpp"
#include "formats/acv_curves_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_patterns.hpp"
#include "psd/psd_smart_objects.hpp"
#include "render/compositor.hpp"
#include "support/string_utils.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#endif

namespace patchy::psd {

namespace {

std::uint8_t adjustment_kind_value(AdjustmentKind kind) {
  switch (kind) {
    case AdjustmentKind::Levels:
      return 0U;
    case AdjustmentKind::Curves:
      return 1U;
    case AdjustmentKind::HueSaturation:
      return 2U;
    case AdjustmentKind::ColorBalance:
      return 3U;
    case AdjustmentKind::Invert:
    case AdjustmentKind::Posterize:
    case AdjustmentKind::Threshold:
    case AdjustmentKind::BrightnessContrast:
      // Unreachable: patchy_adjustment_payload never writes plAD for kinds
      // newer than v4 (old parsers would misread the byte as Levels).
      return 0U;
  }
  return 0U;
}

AdjustmentKind adjustment_kind_from_value(std::uint8_t value) {
  switch (value) {
    case 1U:
      return AdjustmentKind::Curves;
    case 2U:
      return AdjustmentKind::HueSaturation;
    case 3U:
      return AdjustmentKind::ColorBalance;
    default:
      return AdjustmentKind::Levels;
  }
}

std::uint8_t curves_channel_value(CurvesChannel channel) {
  switch (channel) {
    case CurvesChannel::Rgb:
      return 0U;
    case CurvesChannel::Red:
      return 1U;
    case CurvesChannel::Green:
      return 2U;
    case CurvesChannel::Blue:
      return 3U;
  }
  return 0U;
}

std::optional<CurvesChannel> curves_channel_from_value(std::uint8_t value) {
  switch (value) {
    case 0U:
      return CurvesChannel::Rgb;
    case 1U:
      return CurvesChannel::Red;
    case 2U:
      return CurvesChannel::Green;
    case 3U:
      return CurvesChannel::Blue;
    default:
      return std::nullopt;
  }
}

std::uint8_t levels_channel_value(LevelsChannel channel) {
  switch (channel) {
    case LevelsChannel::Red:
      return 1U;
    case LevelsChannel::Green:
      return 2U;
    case LevelsChannel::Blue:
      return 3U;
    case LevelsChannel::Rgb:
      return 0U;
  }
  return 0U;
}

LevelsChannel levels_channel_from_value(int value) {
  switch (value) {
    case 1:
      return LevelsChannel::Red;
    case 2:
      return LevelsChannel::Green;
    case 3:
      return LevelsChannel::Blue;
    default:
      return LevelsChannel::Rgb;
  }
}

// clamp_levels_record / levels_master_record / set_levels_master_record come
// from core/adjustment_layer.hpp (single source of truth for the clamp ranges).

LevelsRecord levels_record_for_photoshop_index(LevelsAdjustment settings, int index) {
  switch (index) {
    case 0:
      return levels_master_record(settings);
    case 1:
      return clamp_levels_record(settings.red);
    case 2:
      return clamp_levels_record(settings.green);
    case 3:
      return clamp_levels_record(settings.blue);
    default:
      return {};
  }
}

void set_levels_record_for_photoshop_index(LevelsAdjustment& settings, int index, LevelsRecord record) {
  record = clamp_levels_record(record);
  switch (index) {
    case 0:
      set_levels_master_record(settings, record);
      return;
    case 1:
      settings.red = record;
      return;
    case 2:
      settings.green = record;
      return;
    case 3:
      settings.blue = record;
      return;
    default:
      return;
  }
}

void write_i16(BigEndianWriter& writer, int value) {
  writer.write_u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(value)));
}

int read_i16(BigEndianReader& reader) {
  return static_cast<int>(static_cast<std::int16_t>(reader.read_u16()));
}

void write_levels_record_i32(BigEndianWriter& writer, LevelsRecord record) {
  record = clamp_levels_record(record);
  write_i32(writer, record.black_input);
  write_i32(writer, record.white_input);
  write_i32(writer, record.gamma_percent);
  write_i32(writer, record.black_output);
  write_i32(writer, record.white_output);
}

LevelsRecord read_levels_record_i32(BigEndianReader& reader) {
  return clamp_levels_record(
      LevelsRecord{read_i32(reader), read_i32(reader), read_i32(reader), read_i32(reader), read_i32(reader)});
}

void write_photoshop_levels_record(BigEndianWriter& writer, LevelsRecord record) {
  record = clamp_levels_record(record);
  writer.write_u16(static_cast<std::uint16_t>(record.black_input));
  writer.write_u16(static_cast<std::uint16_t>(record.white_input));
  writer.write_u16(static_cast<std::uint16_t>(record.black_output));
  writer.write_u16(static_cast<std::uint16_t>(record.white_output));
  writer.write_u16(static_cast<std::uint16_t>(record.gamma_percent));
}

LevelsRecord read_photoshop_levels_record(BigEndianReader& reader) {
  const auto black_input = static_cast<int>(reader.read_u16());
  const auto white_input = static_cast<int>(reader.read_u16());
  const auto black_output = static_cast<int>(reader.read_u16());
  const auto white_output = static_cast<int>(reader.read_u16());
  const auto gamma_percent = static_cast<int>(reader.read_u16());
  return clamp_levels_record(LevelsRecord{black_input, white_input, gamma_percent, black_output, white_output});
}

// Photoshop's hue2 hue fields store -180..180; the model keeps 0..360 (UI convention).
int hue2_file_hue_to_model(int hue) {
  return ((hue % 360) + 360) % 360;
}

int hue2_model_hue_to_file(int hue) {
  const auto normalized = ((hue % 360) + 360) % 360;
  return normalized > 180 ? normalized - 360 : normalized;
}

// The six per-hextant band records plus the undocumented 36-byte trailer exactly as
// Photoshop 2026 writes them for a fresh Hue/Saturation layer (COM byte capture, July
// 2026; identical for colorize on/off). Bands are preserved but not rendered.
constexpr std::array<std::uint8_t, 120> kPhotoshopHueSaturationDefaultTail = {
    0x01, 0x3B, 0x01, 0x59, 0x00, 0x0F, 0x00, 0x2D, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x0F, 0x00, 0x2D, 0x00, 0x4B, 0x00, 0x69, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x4B, 0x00, 0x69, 0x00, 0x87, 0x00, 0xA5,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x87, 0x00, 0xA5, 0x00, 0xC3,
    0x00, 0xE1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC3, 0x00, 0xE1,
    0x00, 0xFF, 0x01, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x01, 0x1D, 0x01, 0x3B, 0x01, 0x59, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x64, 0x00, 0x32, 0x00, 0x3C, 0x00, 0x64, 0x00, 0x32,
    0x00, 0x78, 0x00, 0x64, 0x00, 0x32, 0x00, 0xB4, 0x00, 0x64, 0x00, 0x32,
    0x00, 0xF0, 0x00, 0x64, 0x00, 0x32, 0x01, 0x2C, 0x00, 0x64, 0x00, 0x32,
};

void write_patchy_curves_extension(BigEndianWriter& writer, const CurvesAdjustment& curves) {
  BigEndianWriter extension;
  extension.write_u16(kPatchyCurvesExtensionVersion);
  extension.write_u16(kPatchyCurvesExtensionChannelCount);
  constexpr std::array channels{CurvesChannel::Rgb, CurvesChannel::Red, CurvesChannel::Green, CurvesChannel::Blue};
  for (const auto channel : channels) {
    const auto points = normalized_curve_control_points(curve_points_for_channel(curves, channel));
    extension.write_u8(curves_channel_value(channel));
    extension.write_u8(0U);  // reserved
    extension.write_u16(static_cast<std::uint16_t>(points.size()));
    for (const auto point : points) {
      extension.write_u16(static_cast<std::uint16_t>(point.input));
      extension.write_u16(static_cast<std::uint16_t>(point.output));
    }
  }

  write_signature(writer, kPatchyCurvesExtensionSignature);
  writer.write_u32(static_cast<std::uint32_t>(extension.bytes().size()));
  writer.write_bytes(extension.bytes());
}

std::optional<CurvesAdjustment> parse_patchy_curves_extension(std::span<const std::uint8_t> payload) {
  if (payload.size() > kPatchyCurvesExtensionMaxPayloadSize) {
    return std::nullopt;
  }
  try {
    BigEndianReader reader(payload);
    if (reader.read_u16() != kPatchyCurvesExtensionVersion ||
        reader.read_u16() != kPatchyCurvesExtensionChannelCount) {
      return std::nullopt;
    }

    CurvesAdjustment curves;
    std::array<bool, kPatchyCurvesExtensionChannelCount> seen{};
    for (std::uint16_t index = 0; index < kPatchyCurvesExtensionChannelCount; ++index) {
      const auto channel_value = reader.read_u8();
      const auto channel = curves_channel_from_value(channel_value);
      if (!channel.has_value() || reader.read_u8() != 0U || seen[channel_value]) {
        return std::nullopt;
      }
      seen[channel_value] = true;
      const auto count = reader.read_u16();
      if (count < 2U || count > 19U || reader.remaining() < static_cast<std::size_t>(count) * 4U) {
        return std::nullopt;
      }
      CurveControlPoints points;
      points.reserve(count);
      for (std::uint16_t point_index = 0; point_index < count; ++point_index) {
        points.push_back(CurveControlPoint{static_cast<int>(reader.read_u16()),
                                           static_cast<int>(reader.read_u16())});
      }
      if (normalized_curve_control_points(points) != points) {
        return std::nullopt;
      }
      set_curve_points_for_channel(curves, *channel, std::move(points));
    }
    if (reader.remaining() != 0U || std::any_of(seen.begin(), seen.end(), [](bool value) { return !value; })) {
      return std::nullopt;
    }
    return curves;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool curve_points_are_exact_identity(const CurveControlPoints& points) {
  return points.size() == 2U && points[0] == CurveControlPoint{0, 0} &&
         points[1] == CurveControlPoint{255, 255};
}

bool curves_require_patchy_extension(const CurvesAdjustment& curves) {
  if (!curve_points_are_exact_identity(curves.red) || !curve_points_are_exact_identity(curves.green) ||
      !curve_points_are_exact_identity(curves.blue)) {
    return true;
  }
  if (curve_points_are_exact_identity(curves.rgb)) {
    return false;
  }
  return curves.rgb.size() != 3U || curves.rgb[0].input != 0 || curves.rgb[1].input != 128 ||
         curves.rgb[2].input != 255;
}

}  // namespace

void write_i32(BigEndianWriter& writer, int value) {
  writer.write_u32(static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
}

int read_i32(BigEndianReader& reader) {
  return static_cast<int>(static_cast<std::int32_t>(reader.read_u32()));
}

std::vector<std::uint8_t> photoshop_levels_payload(LevelsAdjustment settings) {
  BigEndianWriter writer;
  writer.write_u16(kPhotoshopLevelsAdjustmentVersion);
  for (int index = 0; index < kPhotoshopLevelsRecordCount; ++index) {
    write_photoshop_levels_record(writer, levels_record_for_photoshop_index(settings, index));
  }
  return writer.bytes();
}

std::optional<AdjustmentSettings> parse_photoshop_levels_adjustment(std::span<const std::uint8_t> payload) {
  try {
    BigEndianReader reader(payload);
    if (reader.read_u16() != kPhotoshopLevelsAdjustmentVersion ||
        reader.remaining() < static_cast<std::size_t>(kPhotoshopLevelsRecordCount) * 10U) {
      return std::nullopt;
    }
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::Levels;
    for (int index = 0; index < kPhotoshopLevelsRecordCount; ++index) {
      const auto record = read_photoshop_levels_record(reader);
      if (index < 4) {
        set_levels_record_for_photoshop_index(settings.levels, index, record);
      }
    }
    return settings;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<AdjustmentSettings> parse_photoshop_hue2_adjustment(std::span<const std::uint8_t> payload) {
  try {
    BigEndianReader reader(payload);
    if (reader.read_u16() != kPhotoshopHueSaturationVersion ||
        reader.remaining() < kPhotoshopHueSaturationHeaderSize - 2U) {
      return std::nullopt;
    }
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::HueSaturation;
    settings.hue_saturation.colorize = reader.read_u8() != 0;
    reader.skip(1);  // padding
    settings.hue_saturation.colorize_hue = hue2_file_hue_to_model(read_i16(reader));
    settings.hue_saturation.colorize_saturation = std::clamp(read_i16(reader), 0, 100);
    settings.hue_saturation.colorize_lightness = std::clamp(read_i16(reader), -100, 100);
    settings.hue_saturation.hue_shift = std::clamp(read_i16(reader), -180, 180);
    settings.hue_saturation.saturation_delta = std::clamp(read_i16(reader), -100, 100);
    settings.hue_saturation.lightness_delta = std::clamp(read_i16(reader), -100, 100);
    return settings;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::vector<std::uint8_t> photoshop_hue2_payload(const HueSaturationAdjustment& settings,
                                                 const UnknownPsdBlock* original) {
  BigEndianWriter header;
  header.write_u16(kPhotoshopHueSaturationVersion);
  header.write_u8(settings.colorize ? 1 : 0);
  header.write_u8(0);  // padding
  write_i16(header, hue2_model_hue_to_file(settings.colorize_hue));
  write_i16(header, std::clamp(settings.colorize_saturation, 0, 100));
  write_i16(header, std::clamp(settings.colorize_lightness, -100, 100));
  write_i16(header, std::clamp(settings.hue_shift, -180, 180));
  write_i16(header, std::clamp(settings.saturation_delta, -100, 100));
  write_i16(header, std::clamp(settings.lightness_delta, -100, 100));

  auto bytes = header.bytes();
  if (original != nullptr && original->payload.size() >= kPhotoshopHueSaturationHeaderSize &&
      original->payload[0] == 0x00 && original->payload[1] == kPhotoshopHueSaturationVersion) {
    // Patch-in-place: everything past the header (band records, trailer) stays
    // byte-identical to the imported payload, so unedited layers round-trip exactly.
    std::vector<std::uint8_t> patched(original->payload.begin(), original->payload.end());
    std::copy(bytes.begin(), bytes.end(), patched.begin());
    return patched;
  }
  bytes.insert(bytes.end(), kPhotoshopHueSaturationDefaultTail.begin(), kPhotoshopHueSaturationDefaultTail.end());
  return bytes;
}

std::optional<AdjustmentSettings> parse_photoshop_curves_adjustment(
    std::span<const std::uint8_t> payload) {
  // Photoshop's curv adjustment block begins with one zero byte, followed by
  // the documented Curves-file body. Photoshop 2026 writes a version-1 bitmap
  // body plus its indexed `Crv ` version-4 extension and pads the payload to a
  // four-byte boundary. The shared ACV reader handles both sections and gives
  // the richer indexed extension authority when it is present.
  if (payload.empty() || payload.front() != 0U) {
    return std::nullopt;
  }
  try {
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::Curves;
    settings.curves = acv::read(payload.subspan(1U));
    return settings;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::vector<std::uint8_t> photoshop_curves_payload(const CurvesAdjustment& curves,
                                                   const UnknownPsdBlock* original) {
  if (original != nullptr) {
    if (const auto parsed = parse_photoshop_curves_adjustment(original->payload);
        parsed.has_value() && parsed->curves == curves) {
      // The imported payload may contain compatibility details Patchy does not
      // model. Keep every byte until the modeled control points actually change.
      return original->payload;
    }
  }

  constexpr std::array channels{CurvesChannel::Rgb, CurvesChannel::Red,
                                CurvesChannel::Green, CurvesChannel::Blue};
  struct ActiveCurve {
    std::uint16_t channel{0};
    CurveControlPoints points;
  };
  std::vector<ActiveCurve> active;
  std::uint32_t bitmap = 0U;
  for (std::size_t index = 0; index < channels.size(); ++index) {
    auto points = normalized_curve_control_points(curve_points_for_channel(curves, channels[index]));
    if (curve_points_are_exact_identity(points)) {
      continue;
    }
    bitmap |= 1U << static_cast<unsigned>(index);
    active.push_back(ActiveCurve{static_cast<std::uint16_t>(index), std::move(points)});
  }

  const auto write_curve = [](BigEndianWriter& writer, const CurveControlPoints& points) {
    writer.write_u16(static_cast<std::uint16_t>(points.size()));
    for (const auto point : points) {
      // Photoshop stores each control point as output first, then input.
      writer.write_u16(static_cast<std::uint16_t>(point.output));
      writer.write_u16(static_cast<std::uint16_t>(point.input));
    }
  };

  BigEndianWriter writer;
  writer.write_u8(0U);  // curv adjustment-block prefix
  writer.write_u16(1U);
  // Photoshop 2026 writes this bitmap as four bytes even though Adobe's table
  // labels the field as two. Real captures use 0x0000000f for RGB+R+G+B.
  writer.write_u32(bitmap);
  for (const auto& curve : active) {
    write_curve(writer, curve.points);
  }

  write_signature(writer, kPhotoshopCurvesExtraMarker);
  writer.write_u16(4U);
  writer.write_u32(static_cast<std::uint32_t>(active.size()));
  for (const auto& curve : active) {
    writer.write_u16(curve.channel);
    write_curve(writer, curve.points);
  }
  while ((writer.bytes().size() % 4U) != 0U) {
    writer.write_u8(0U);
  }
  return writer.bytes();
}

std::optional<AdjustmentSettings> parse_patchy_adjustment(std::span<const std::uint8_t> payload);

bool patchy_plad_supports_kind(AdjustmentKind kind) {
  switch (kind) {
    case AdjustmentKind::Levels:
    case AdjustmentKind::Curves:
    case AdjustmentKind::HueSaturation:
    case AdjustmentKind::ColorBalance:
      return true;
    case AdjustmentKind::Invert:
    case AdjustmentKind::Posterize:
    case AdjustmentKind::Threshold:
    case AdjustmentKind::BrightnessContrast:
      return false;
  }
  return false;
}

std::optional<AdjustmentSettings> parse_photoshop_posterize_adjustment(std::span<const std::uint8_t> payload) {
  if (payload.size() < 2) {
    return std::nullopt;
  }
  BigEndianReader reader(payload);
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::Posterize;
  settings.posterize.levels = std::clamp(static_cast<int>(reader.read_u16()), 2, 255);
  return settings;
}

std::vector<std::uint8_t> photoshop_posterize_payload(const PosterizeAdjustment& settings,
                                                      const UnknownPsdBlock* original) {
  const auto levels = std::clamp(settings.levels, 2, 255);
  if (original != nullptr) {
    // Unedited imported payloads re-emit byte-for-byte (curv-style guard) so
    // any undocumented trailing bytes Photoshop may add survive untouched.
    const auto parsed = parse_photoshop_posterize_adjustment(original->payload);
    if (parsed.has_value() && parsed->posterize.levels == levels) {
      return original->payload;
    }
  }
  BigEndianWriter writer;
  writer.write_u16(static_cast<std::uint16_t>(levels));
  writer.write_u16(0);
  return writer.bytes();
}

std::optional<AdjustmentSettings> parse_photoshop_brightness_contrast_adjustment(
    std::span<const std::uint8_t> payload) {
  if (payload.size() < 4) {
    return std::nullopt;
  }
  BigEndianReader reader(payload);
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::BrightnessContrast;
  settings.brightness_contrast.brightness =
      std::clamp(static_cast<int>(static_cast<std::int16_t>(reader.read_u16())), -100, 100);
  settings.brightness_contrast.contrast =
      std::clamp(static_cast<int>(static_cast<std::int16_t>(reader.read_u16())), -100, 100);
  return settings;
}

std::optional<BrightnessContrastDescriptorParse> parse_photoshop_brightness_contrast_descriptor(
    std::span<const std::uint8_t> payload) {
  if (payload.size() < 4) {
    return std::nullopt;
  }
  try {
    BigEndianReader reader(payload);
    if (reader.read_u32() != 16) {
      return std::nullopt;
    }
    const auto descriptor = read_descriptor(reader);
    const auto* brightness = descriptor_value(descriptor, "Brgh");
    const auto* contrast = descriptor_value(descriptor, "Cntr");
    if (brightness == nullptr || brightness->type != DescriptorValue::Type::Integer ||
        contrast == nullptr || contrast->type != DescriptorValue::Type::Integer) {
      return std::nullopt;
    }
    BrightnessContrastDescriptorParse parsed;
    parsed.settings.kind = AdjustmentKind::BrightnessContrast;
    // Modern-mode values live in wider ranges (-150..150 / -50..100); they
    // clamp into the legacy model, the accepted approximation.
    parsed.settings.brightness_contrast.brightness = std::clamp(brightness->integer_value, -100, 100);
    parsed.settings.brightness_contrast.contrast = std::clamp(contrast->integer_value, -100, 100);
    if (const auto* legacy = descriptor_value(descriptor, "useLegacy");
        legacy != nullptr && legacy->type == DescriptorValue::Type::Bool) {
      parsed.use_legacy = legacy->bool_value;
    }
    return parsed;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

namespace {

// The imported state the current settings are compared against for the
// unedited-round-trip guards: a parseable CgEd wins over brit.
std::optional<BrightnessContrastAdjustment> original_brightness_contrast_state(const Layer& layer) {
  const UnknownPsdBlock* brit = nullptr;
  const UnknownPsdBlock* descriptor = nullptr;
  for (const auto& block : layer.unknown_psd_blocks()) {
    if (block.key == "brit") {
      brit = &block;
    } else if (block.key == "CgEd") {
      descriptor = &block;
    }
  }
  if (descriptor != nullptr) {
    if (const auto parsed = parse_photoshop_brightness_contrast_descriptor(descriptor->payload);
        parsed.has_value()) {
      return parsed->settings.brightness_contrast;
    }
  }
  if (brit != nullptr) {
    if (const auto parsed = parse_photoshop_brightness_contrast_adjustment(brit->payload); parsed.has_value()) {
      return parsed->brightness_contrast;
    }
  }
  return std::nullopt;
}

}  // namespace

std::vector<std::uint8_t> photoshop_brightness_contrast_payload(const BrightnessContrastAdjustment& settings,
                                                                const Layer& layer) {
  const auto brightness = std::clamp(settings.brightness, -100, 100);
  const auto contrast = std::clamp(settings.contrast, -100, 100);
  const auto original = original_brightness_contrast_state(layer);
  if (original.has_value() && original->brightness == brightness && original->contrast == contrast) {
    for (const auto& block : layer.unknown_psd_blocks()) {
      if (block.key == "brit") {
        return block.payload;  // unedited: byte-identical round trip
      }
    }
  }
  BigEndianWriter writer;
  writer.write_u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(brightness)));
  writer.write_u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(contrast)));
  writer.write_u16(127);  // mean, Photoshop's fixed midpoint
  writer.write_u8(0);     // lab
  writer.write_u8(0);     // pad
  return writer.bytes();
}

bool brightness_contrast_descriptor_is_stale(const Layer& layer) {
  const auto settings = adjustment_settings_from_layer(layer);
  if (!settings.has_value() || settings->kind != AdjustmentKind::BrightnessContrast) {
    return false;
  }
  const auto original = original_brightness_contrast_state(layer);
  return !original.has_value() ||
         original->brightness != settings->brightness_contrast.brightness ||
         original->contrast != settings->brightness_contrast.contrast;
}

std::optional<AdjustmentSettings> parse_photoshop_threshold_adjustment(std::span<const std::uint8_t> payload) {
  if (payload.size() < 2) {
    return std::nullopt;
  }
  BigEndianReader reader(payload);
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::Threshold;
  settings.threshold.level = std::clamp(static_cast<int>(reader.read_u16()), 1, 255);
  return settings;
}

std::vector<std::uint8_t> photoshop_threshold_payload(const ThresholdAdjustment& settings,
                                                      const UnknownPsdBlock* original) {
  const auto level = std::clamp(settings.level, 1, 255);
  if (original != nullptr) {
    const auto parsed = parse_photoshop_threshold_adjustment(original->payload);
    if (parsed.has_value() && parsed->threshold.level == level) {
      return original->payload;
    }
  }
  BigEndianWriter writer;
  writer.write_u16(static_cast<std::uint16_t>(level));
  writer.write_u16(0);
  return writer.bytes();
}

std::vector<std::uint8_t> patchy_adjustment_payload(const Layer& layer) {
  const auto settings = adjustment_settings_from_layer(layer);
  if (!settings.has_value() || !patchy_plad_supports_kind(settings->kind)) {
    return {};
  }

  if (settings->kind == AdjustmentKind::Curves) {
    for (const auto& block : layer.unknown_psd_blocks()) {
      if (block.key != "plAD") {
        continue;
      }
      const auto original = parse_patchy_adjustment(block.payload);
      if (original.has_value() && original->kind == AdjustmentKind::Curves &&
          original->curves == settings->curves) {
        // Untouched imported Curves payloads remain byte-identical, including an
        // unknown, future, or malformed CRV2 tail. A real Patchy edit changes the
        // modeled points and falls through to regenerate the known v4 shape.
        return block.payload;
      }
      break;
    }
  }

  BigEndianWriter writer;
  write_signature(writer, kPatchyAdjustmentPayloadSignature);
  writer.write_u16(kPatchyAdjustmentVersion);
  writer.write_u8(adjustment_kind_value(settings->kind));
  for (int index = 0; index < 4; ++index) {
    write_levels_record_i32(writer, levels_record_for_photoshop_index(settings->levels, index));
  }
  write_i32(writer, levels_channel_value(settings->levels.channel));
  const auto composite_curve_lut = build_curve_lut(settings->curves.rgb);
  write_i32(writer, composite_curve_lut[0]);
  write_i32(writer, composite_curve_lut[128]);
  write_i32(writer, composite_curve_lut[255]);
  write_i32(writer, settings->hue_saturation.hue_shift);
  write_i32(writer, settings->hue_saturation.saturation_delta);
  write_i32(writer, settings->hue_saturation.lightness_delta);
  write_i32(writer, settings->color_balance.cyan_red);
  write_i32(writer, settings->color_balance.magenta_green);
  write_i32(writer, settings->color_balance.yellow_blue);
  // Trailing version-4 extension (July 2026): Hue/Saturation colorize. Kept under
  // version 4 because shipped parsers tolerate longer payloads but reject unknown
  // versions - a bump would make every new adjustment file unreadable in old builds.
  write_i32(writer, settings->hue_saturation.colorize ? 1 : 0);
  write_i32(writer, settings->hue_saturation.colorize_hue);
  write_i32(writer, settings->hue_saturation.colorize_saturation);
  write_i32(writer, settings->hue_saturation.colorize_lightness);
  if (settings->kind == AdjustmentKind::Curves && curves_require_patchy_extension(settings->curves)) {
    // Length-delimited extension under the intentionally unchanged plAD v4.
    // Older builds ignore trailing bytes and continue to use the three legacy
    // composite outputs written above.
    write_patchy_curves_extension(writer, settings->curves);
  }
  return writer.bytes();
}

std::optional<AdjustmentSettings> parse_patchy_adjustment(std::span<const std::uint8_t> payload) {
  try {
    BigEndianReader reader(payload);
    if (read_signature(reader) != kPatchyAdjustmentPayloadSignature) {
      return std::nullopt;
    }
    if (reader.read_u16() != kPatchyAdjustmentVersion) {
      return std::nullopt;
    }
    constexpr auto expected_i32_count = 30U;
    if (reader.remaining() < 1U + expected_i32_count * 4U) {
      return std::nullopt;
    }

    AdjustmentSettings settings;
    settings.kind = adjustment_kind_from_value(reader.read_u8());
    for (int index = 0; index < 4; ++index) {
      set_levels_record_for_photoshop_index(settings.levels, index, read_levels_record_i32(reader));
    }
    settings.levels.channel = levels_channel_from_value(read_i32(reader));
    const auto legacy_curve_shadow = read_i32(reader);
    const auto legacy_curve_midtone = read_i32(reader);
    const auto legacy_curve_highlight = read_i32(reader);
    settings.curves =
        curves_adjustment_from_legacy_outputs(legacy_curve_shadow, legacy_curve_midtone, legacy_curve_highlight);
    settings.hue_saturation.hue_shift = read_i32(reader);
    settings.hue_saturation.saturation_delta = read_i32(reader);
    settings.hue_saturation.lightness_delta = read_i32(reader);
    settings.color_balance.cyan_red = read_i32(reader);
    settings.color_balance.magenta_green = read_i32(reader);
    settings.color_balance.yellow_blue = read_i32(reader);
    if (reader.remaining() >= 16U) {
      // Version-4 trailing colorize extension; absent in pre-July-2026 files.
      settings.hue_saturation.colorize = read_i32(reader) != 0;
      settings.hue_saturation.colorize_hue = std::clamp(read_i32(reader), 0, 360) % 360;
      settings.hue_saturation.colorize_saturation = std::clamp(read_i32(reader), 0, 100);
      settings.hue_saturation.colorize_lightness = std::clamp(read_i32(reader), -100, 100);
    }
    if (settings.kind == AdjustmentKind::Curves && reader.remaining() >= 8U &&
        read_signature(reader) == kPatchyCurvesExtensionSignature) {
      const auto extension_length = static_cast<std::size_t>(reader.read_u32());
      if (extension_length <= kPatchyCurvesExtensionMaxPayloadSize && extension_length <= reader.remaining()) {
        const auto extension = reader.read_bytes(extension_length);
        if (const auto rich_curves = parse_patchy_curves_extension(extension); rich_curves.has_value()) {
          settings.curves = *rich_curves;
        }
      }
      // A malformed or unknown rich tail never invalidates the legacy plAD
      // fields above. This is the compatibility escape hatch for old files and
      // future extensions that retain version 4.
    }
    return settings;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

}  // namespace patchy::psd
