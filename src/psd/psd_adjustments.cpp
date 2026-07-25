// Adjustment-layer codecs for the PSD reader/writer: the Photoshop levl /
// curv / hue2 payloads (hue2 patches in place, curv preserves imported bytes
// exactly), plus read-only legacy support for the private plAD adjustment
// block and its CRV2 curves extension (never written since 2026-07: Photoshop
// reported the unknown key as "unknown data" on every open). Split out of
// psd_document_io.cpp as a pure move.

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
    // Six per-hue-range band records: four i16 range stops in wheel order then
    // an i16 hue/saturation/lightness triple. Files that stop after the header
    // (the legacy 16-byte shape) keep Photoshop's default hextants.
    settings.hue_saturation.bands = default_hue_saturation_bands();
    if (reader.remaining() >= kPhotoshopHueSaturationBandRecordSize * settings.hue_saturation.bands.size()) {
      const auto degrees = [](int value) { return ((value % 360) + 360) % 360; };
      for (auto& band : settings.hue_saturation.bands) {
        band.outer_start = degrees(read_i16(reader));
        band.inner_start = degrees(read_i16(reader));
        band.inner_end = degrees(read_i16(reader));
        band.outer_end = degrees(read_i16(reader));
        band.hue_shift = std::clamp(read_i16(reader), -180, 180);
        band.saturation_delta = std::clamp(read_i16(reader), -100, 100);
        band.lightness_delta = std::clamp(read_i16(reader), -100, 100);
      }
    }
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
  for (const auto& band : settings.bands) {
    write_i16(header, ((band.outer_start % 360) + 360) % 360);
    write_i16(header, ((band.inner_start % 360) + 360) % 360);
    write_i16(header, ((band.inner_end % 360) + 360) % 360);
    write_i16(header, ((band.outer_end % 360) + 360) % 360);
    write_i16(header, std::clamp(band.hue_shift, -180, 180));
    write_i16(header, std::clamp(band.saturation_delta, -100, 100));
    write_i16(header, std::clamp(band.lightness_delta, -100, 100));
  }

  auto bytes = header.bytes();
  if (original != nullptr && original->payload.size() >= kPhotoshopHueSaturationHeaderSize &&
      original->payload[0] == 0x00 && original->payload[1] == kPhotoshopHueSaturationVersion) {
    // Patch-in-place: the header and the six band records come from the model,
    // the undocumented 36-byte trailer stays byte-identical to the imported
    // payload, so an unedited layer still round-trips exactly.
    std::vector<std::uint8_t> patched(original->payload.begin(), original->payload.end());
    const auto copied = std::min(bytes.size(), patched.size());
    std::copy_n(bytes.begin(), copied, patched.begin());
    return patched;
  }
  // A fresh layer already carries its band records from the model, so only the
  // undocumented 36-byte trailer is appended from Photoshop's template.
  bytes.insert(bytes.end(), kPhotoshopHueSaturationDefaultTail.begin() + kPhotoshopHueSaturationBandBlockSize,
               kPhotoshopHueSaturationDefaultTail.end());
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

std::optional<AdjustmentSettings> parse_photoshop_color_balance_adjustment(
    std::span<const std::uint8_t> payload) {
  if (payload.size() < 12) {
    return std::nullopt;
  }
  BigEndianReader reader(payload);
  reader.skip(6);  // shadows: preserved via patch-in-place, not modeled
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::ColorBalance;
  settings.color_balance.cyan_red =
      std::clamp(static_cast<int>(static_cast<std::int16_t>(reader.read_u16())), -100, 100);
  settings.color_balance.magenta_green =
      std::clamp(static_cast<int>(static_cast<std::int16_t>(reader.read_u16())), -100, 100);
  settings.color_balance.yellow_blue =
      std::clamp(static_cast<int>(static_cast<std::int16_t>(reader.read_u16())), -100, 100);
  return settings;
}

std::vector<std::uint8_t> photoshop_color_balance_payload(const ColorBalanceAdjustment& settings,
                                                          const UnknownPsdBlock* original) {
  std::vector<std::uint8_t> payload;
  if (original != nullptr && original->payload.size() >= 12) {
    payload = original->payload;  // keep shadows/highlights/preserve-luminosity bytes
  } else {
    payload.assign(20, 0);  // PS 2026's fresh midtones-only shape
  }
  const auto write_i16_at = [&payload](std::size_t offset, int value) {
    const auto encoded = static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp(value, -100, 100)));
    payload[offset] = static_cast<std::uint8_t>(encoded >> 8U);
    payload[offset + 1] = static_cast<std::uint8_t>(encoded & 0xFFU);
  };
  write_i16_at(6, settings.cyan_red);
  write_i16_at(8, settings.magenta_green);
  write_i16_at(10, settings.yellow_blue);
  return payload;
}

bool photoshop_color_balance_payload_has_unrendered_data(std::span<const std::uint8_t> payload) {
  for (std::size_t index = 0; index < payload.size(); ++index) {
    const auto in_shadows = index < 6;
    const auto in_highlights = index >= 12 && index < 18;
    const auto is_preserve_luminosity = index == 18;
    if ((in_shadows || in_highlights || is_preserve_luminosity) && payload[index] != 0) {
      return true;
    }
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

// Read-only since 2026-07: no adjustment kind writes plAD anymore (Photoshop
// reported the unknown key as "unknown data" on every open). The v4 layout
// stays parseable for legacy imports: 'PLAD' signature, u16 version 4, kind u8
// (0 Levels, 1 Curves, 2 HueSat, 3 ColorBalance; newer kinds were never
// written because old builds read unknown kind bytes as Levels), 4 levels
// records of 5 i32, levels channel i32, 3 legacy curve outputs, 3 hue/sat,
// 3 color balance, optional 4-i32 colorize tail, optional CRV2 curves tail.
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
