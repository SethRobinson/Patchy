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
      // Index 4 included: Patchy writes RGB files, and Photoshop 2026 silently turns a
      // Levels layer into a plain empty layer (mask gone too) when an RGB document's
      // fifth record is not the identity. The black ink's record stays in the model
      // (LevelsAdjustment::black_ink) and is never written.
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
    case 4:
      // A CMYK document's black ink. RGB documents leave this record at its identity.
      settings.black_ink = record;
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
      if (index < 5) {
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
    // (Not when the original carries a black-ink curve: that payload came from a CMYK
    // document, and Patchy writes RGB, where a fifth channel does not belong. See the
    // Levels note in levels_record_for_photoshop_index.)
    if (const auto parsed = parse_photoshop_curves_adjustment(original->payload);
        parsed.has_value() && parsed->curves == curves &&
        curve_points_are_exact_identity(normalized_curve_control_points(curves.black_ink))) {
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

// 'blnc': shadows, midtones and highlights triples of big-endian int16
// (cyan/red, magenta/green, yellow/blue), then the Preserve Luminosity byte and
// one pad byte (20 bytes). A short payload of PS's older midtones-only shape
// leaves the missing tones at zero.
std::optional<AdjustmentSettings> parse_photoshop_color_balance_adjustment(
    std::span<const std::uint8_t> payload) {
  if (payload.size() < 12) {
    return std::nullopt;
  }
  BigEndianReader reader(payload);
  const auto read_tone = [&reader] {
    ColorBalanceTone tone;
    for (auto* field : {&tone.cyan_red, &tone.magenta_green, &tone.yellow_blue}) {
      *field = std::clamp(static_cast<int>(static_cast<std::int16_t>(reader.read_u16())), -100, 100);
    }
    return tone;
  };
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::ColorBalance;
  settings.color_balance.shadows = read_tone();
  settings.color_balance.set_tone(ColorBalanceToneRange::Midtones, read_tone());
  if (payload.size() >= 18) {
    settings.color_balance.highlights = read_tone();
  }
  settings.color_balance.preserve_luminosity = payload.size() >= 19 && payload[18] != 0;
  return settings;
}

// Rewrites the modeled bytes (all three tones and Preserve Luminosity) over the
// original payload, so any trailing bytes past them survive unchanged.
std::vector<std::uint8_t> photoshop_color_balance_payload(const ColorBalanceAdjustment& settings,
                                                          const UnknownPsdBlock* original) {
  std::vector<std::uint8_t> payload;
  if (original != nullptr && original->payload.size() >= 20) {
    payload = original->payload;
  } else {
    payload.assign(20, 0);
  }
  const auto write_tone_at = [&payload](std::size_t offset, const ColorBalanceTone& tone) {
    for (const auto value : {tone.cyan_red, tone.magenta_green, tone.yellow_blue}) {
      const auto encoded = static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp(value, -100, 100)));
      payload[offset] = static_cast<std::uint8_t>(encoded >> 8U);
      payload[offset + 1] = static_cast<std::uint8_t>(encoded & 0xFFU);
      offset += 2;
    }
  };
  write_tone_at(0, settings.shadows);
  write_tone_at(6, settings.tone(ColorBalanceToneRange::Midtones));
  write_tone_at(12, settings.highlights);
  payload[18] = settings.preserve_luminosity ? 1U : 0U;
  return payload;
}

// 'mixr' (RGB documents): u16 version (1), u16 monochrome, then four rows of five int16
// (red, green, blue, an unused ink slot, constant). Rows 0..2 are the red, green and blue
// outputs; with monochrome on, Photoshop keeps the gray mix in row 0 (layout pinned by the
// #250 probe; docs/adjustment-kinds.md, "Channel Mixer").
constexpr std::size_t kChannelMixerRowBytes = 10;
constexpr std::size_t kChannelMixerPayloadBytes = 4 + 4 * kChannelMixerRowBytes;

std::optional<AdjustmentSettings> parse_photoshop_channel_mixer_adjustment(std::span<const std::uint8_t> payload) {
  if (payload.size() < 4 + 3 * kChannelMixerRowBytes) {
    return std::nullopt;
  }
  BigEndianReader reader(payload);
  if (reader.read_u16() != 1U) {
    return std::nullopt;
  }
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::ChannelMixer;
  auto& mixer = settings.channel_mixer;
  mixer.monochrome = reader.read_u16() != 0U;
  const auto read_row = [&reader] {
    ChannelMixerRow row;
    row.red = static_cast<std::int16_t>(reader.read_u16());
    row.green = static_cast<std::int16_t>(reader.read_u16());
    row.blue = static_cast<std::int16_t>(reader.read_u16());
    reader.skip(2);
    row.constant = static_cast<std::int16_t>(reader.read_u16());
    return clamp_channel_mixer_row(row);
  };
  const auto first = read_row();
  if (mixer.monochrome) {
    mixer.gray = first;
  } else {
    mixer.red = first;
    mixer.green = read_row();
    mixer.blue = read_row();
  }
  return settings;
}

std::vector<std::uint8_t> photoshop_channel_mixer_payload(const ChannelMixerAdjustment& settings,
                                                          const UnknownPsdBlock* original) {
  std::vector<std::uint8_t> payload;
  if (original != nullptr && original->payload.size() >= kChannelMixerPayloadBytes) {
    payload = original->payload;
  } else {
    payload.assign(kChannelMixerPayloadBytes, 0U);
  }
  const auto put = [&payload](std::size_t offset, int value) {
    const auto encoded = static_cast<std::uint16_t>(static_cast<std::int16_t>(value));
    payload[offset] = static_cast<std::uint8_t>(encoded >> 8U);
    payload[offset + 1] = static_cast<std::uint8_t>(encoded & 0xFFU);
  };
  const auto put_row = [&put](std::size_t row_index, const ChannelMixerRow& row) {
    const auto clamped = clamp_channel_mixer_row(row);
    const auto offset = 4 + row_index * kChannelMixerRowBytes;
    put(offset, clamped.red);
    put(offset + 2, clamped.green);
    put(offset + 4, clamped.blue);
    put(offset + 8, clamped.constant);
  };
  put(0, 1);
  put(2, settings.monochrome ? 1 : 0);
  if (settings.monochrome) {
    put_row(0, settings.gray);
  } else {
    put_row(0, settings.red);
    put_row(1, settings.green);
    put_row(2, settings.blue);
  }
  return payload;
}

// 'phfl' version 2 (20 bytes): u16 version, u16 color space (7 = Lab), four u16 color
// components (L in hundredths 0..10000, then a and b as int16 hundredths, then 0), u32
// density percent, u8 preserve luminosity, 3 pad bytes. Layout pinned by the #250 probe
// (docs/adjustment-kinds.md, "Photo Filter"). Other versions and color spaces stay
// placeholders, written back unchanged.
constexpr std::size_t kPhotoFilterPayloadBytes = 20;
constexpr std::uint16_t kPhotoFilterLabColorSpace = 7;

std::optional<AdjustmentSettings> parse_photoshop_photo_filter_adjustment(std::span<const std::uint8_t> payload) {
  if (payload.size() < 17) {
    return std::nullopt;
  }
  BigEndianReader reader(payload);
  if (reader.read_u16() != 2U || reader.read_u16() != kPhotoFilterLabColorSpace) {
    return std::nullopt;
  }
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::PhotoFilter;
  auto& filter = settings.photo_filter;
  filter.lab_l = reader.read_u16();
  filter.lab_a = static_cast<std::int16_t>(reader.read_u16());
  filter.lab_b = static_cast<std::int16_t>(reader.read_u16());
  reader.skip(2);
  filter.density = static_cast<int>(std::min<std::uint32_t>(reader.read_u32(), 100U));
  filter.preserve_luminosity = reader.read_u8() != 0U;
  filter = clamp_photo_filter(filter);
  return settings;
}

std::vector<std::uint8_t> photoshop_photo_filter_payload(const PhotoFilterAdjustment& settings,
                                                         const UnknownPsdBlock* original) {
  std::vector<std::uint8_t> payload;
  if (original != nullptr && original->payload.size() >= kPhotoFilterPayloadBytes) {
    payload = original->payload;
  } else {
    payload.assign(kPhotoFilterPayloadBytes, 0U);
  }
  const auto filter = clamp_photo_filter(settings);
  const auto put16 = [&payload](std::size_t offset, int value) {
    const auto encoded = static_cast<std::uint16_t>(static_cast<std::int16_t>(value));
    payload[offset] = static_cast<std::uint8_t>(encoded >> 8U);
    payload[offset + 1] = static_cast<std::uint8_t>(encoded & 0xFFU);
  };
  put16(0, 2);
  put16(2, kPhotoFilterLabColorSpace);
  put16(4, filter.lab_l);
  put16(6, filter.lab_a);
  put16(8, filter.lab_b);
  put16(10, 0);
  put16(12, 0);
  put16(14, filter.density);
  payload[16] = filter.preserve_luminosity ? 1U : 0U;
  return payload;
}

// 'grdm' (docs/adjustment-kinds.md, "Gradient Map"): u16 version (1, or 3 with a 4-byte
// method key after the flags), u8 reverse, u8 dither, Unicode name (u32 count with the
// trailing null), u16 color stop count and per stop u32 location (0..4096), u32 midpoint
// (0..100), u16 color space (0 = RGB), four u16 components, u16 pad; u16 opacity stop count
// and per stop u32 location, u32 midpoint, u16 opacity (0..255); then u16 2, u16 smoothness
// (0..4096) and the noise fields, padded to four bytes. Only RGB solid stops are read.
constexpr std::array<std::pair<GradientInterpolationMethod, std::array<char, 4>>, 3> kGradientMapMethodKeys{{
    {GradientInterpolationMethod::Classic, {'G', 'c', 'l', 's'}},
    {GradientInterpolationMethod::Linear, {'L', 'n', 'r', ' '}},
    {GradientInterpolationMethod::Perceptual, {'P', 'e', 'r', 'c'}},
}};

struct ParsedGradientMap {
  AdjustmentSettings settings;
  std::size_t tail_offset{0};  // where the u16 2 before the smoothness starts
};

std::optional<ParsedGradientMap> parse_gradient_map_block(std::span<const std::uint8_t> payload) {
  try {
    BigEndianReader reader(payload);
    const auto version = reader.read_u16();
    if (version != 1U && version != 3U) {
      return std::nullopt;
    }
    ParsedGradientMap parsed;
    parsed.settings.kind = AdjustmentKind::GradientMap;
    auto& gradient = parsed.settings.gradient_map.gradient;
    gradient.reverse = reader.read_u8() != 0U;
    gradient.dither = reader.read_u8() != 0U;
    gradient.interpolation = GradientInterpolationMethod::Classic;
    if (version == 3U) {
      const auto key = reader.read_span(4);
      bool known = false;
      for (const auto& [method, text] : kGradientMapMethodKeys) {
        if (std::equal(text.begin(), text.end(), key.begin(), key.end(),
                       [](char a, std::uint8_t b) { return static_cast<std::uint8_t>(a) == b; })) {
          gradient.interpolation = method;
          known = true;
        }
      }
      if (!known) {
        return std::nullopt;
      }
    }
    const auto name_units = reader.read_u32();
    if (name_units > reader.remaining() / 2U) {
      return std::nullopt;
    }
    std::vector<std::uint16_t> name;
    name.reserve(name_units);
    for (std::uint32_t index = 0; index < name_units; ++index) {
      name.push_back(reader.read_u16());
    }
    while (!name.empty() && name.back() == 0U) {
      name.pop_back();
    }
    gradient.name = utf16_units_to_utf8(name);
    const auto color_count = reader.read_u16();
    if (color_count < 2U) {
      return std::nullopt;
    }
    gradient.color_stops.clear();
    for (std::uint16_t index = 0; index < color_count; ++index) {
      GradientColorStop stop;
      stop.location = static_cast<float>(std::min<std::uint32_t>(reader.read_u32(), 4096U)) / 4096.0F;
      stop.midpoint = static_cast<float>(std::min<std::uint32_t>(reader.read_u32(), 100U)) / 100.0F;
      if (reader.read_u16() != 0U) {
        return std::nullopt;  // HSB, CMYK, Lab or gray stops are not read
      }
      const auto component = [&reader] {
        return static_cast<std::uint8_t>((static_cast<std::uint32_t>(reader.read_u16()) + 128U) / 257U);
      };
      stop.color.red = component();
      stop.color.green = component();
      stop.color.blue = component();
      reader.skip(4);
      gradient.color_stops.push_back(stop);
    }
    const auto alpha_count = reader.read_u16();
    gradient.alpha_stops.clear();
    for (std::uint16_t index = 0; index < alpha_count; ++index) {
      GradientAlphaStop stop;
      stop.location = static_cast<float>(std::min<std::uint32_t>(reader.read_u32(), 4096U)) / 4096.0F;
      stop.midpoint = static_cast<float>(std::min<std::uint32_t>(reader.read_u32(), 100U)) / 100.0F;
      stop.opacity = static_cast<float>(std::min<std::uint16_t>(reader.read_u16(), 255U)) / 255.0F;
      gradient.alpha_stops.push_back(stop);
    }
    parsed.tail_offset = reader.position();
    reader.skip(2);
    gradient.smoothness = static_cast<std::uint16_t>(std::min<std::uint16_t>(reader.read_u16(), 4096U));
    return parsed;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<AdjustmentSettings> parse_photoshop_gradient_map_adjustment(std::span<const std::uint8_t> payload) {
  auto parsed = parse_gradient_map_block(payload);
  if (!parsed.has_value()) {
    return std::nullopt;
  }
  return std::move(parsed->settings);
}

std::vector<std::uint8_t> photoshop_gradient_map_payload(const GradientMapAdjustment& settings,
                                                         const UnknownPsdBlock* original) {
  std::optional<ParsedGradientMap> parsed_original;
  if (original != nullptr) {
    parsed_original = parse_gradient_map_block(original->payload);
    if (parsed_original.has_value() && parsed_original->settings.gradient_map == settings) {
      return original->payload;
    }
  }
  const auto& gradient = settings.gradient;
  BigEndianWriter writer;
  writer.write_u16(3);
  writer.write_u8(gradient.reverse ? 1U : 0U);
  writer.write_u8(gradient.dither ? 1U : 0U);
  for (const auto& [method, text] : kGradientMapMethodKeys) {
    if (method == gradient.interpolation) {
      for (const char c : text) {
        writer.write_u8(static_cast<std::uint8_t>(c));
      }
    }
  }
  auto name = utf8_to_utf16(gradient.name);
  name.push_back(0U);
  writer.write_u32(static_cast<std::uint32_t>(name.size()));
  for (const auto unit : name) {
    writer.write_u16(unit);
  }
  const auto location = [](float value) {
    return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 4096.0F));
  };
  const auto midpoint = [](float value) {
    return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 100.0F));
  };
  writer.write_u16(static_cast<std::uint16_t>(gradient.color_stops.size()));
  for (const auto& stop : gradient.color_stops) {
    writer.write_u32(location(stop.location));
    writer.write_u32(midpoint(stop.midpoint));
    writer.write_u16(0);
    writer.write_u16(static_cast<std::uint16_t>(stop.color.red * 257U));
    writer.write_u16(static_cast<std::uint16_t>(stop.color.green * 257U));
    writer.write_u16(static_cast<std::uint16_t>(stop.color.blue * 257U));
    writer.write_u16(0);
    writer.write_u16(0);
  }
  writer.write_u16(static_cast<std::uint16_t>(gradient.alpha_stops.size()));
  for (const auto& stop : gradient.alpha_stops) {
    writer.write_u32(location(stop.location));
    writer.write_u32(midpoint(stop.midpoint));
    writer.write_u16(static_cast<std::uint16_t>(std::lround(std::clamp(stop.opacity, 0.0F, 1.0F) * 255.0F)));
  }
  // The noise fields after the smoothness: the original's when there is one, else the
  // values Photoshop 2026 writes for a solid gradient.
  std::vector<std::uint8_t> tail;
  constexpr std::size_t kNoiseFieldBytes = 36;
  if (parsed_original.has_value() &&
      parsed_original->tail_offset + 4U + kNoiseFieldBytes <= original->payload.size()) {
    const auto start = original->payload.begin() + static_cast<std::ptrdiff_t>(parsed_original->tail_offset + 4U);
    tail.assign(start, start + static_cast<std::ptrdiff_t>(kNoiseFieldBytes));
  } else {
    tail = {0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
            0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x80, 0x00,
            0x80, 0x00, 0x80, 0x00, 0x00, 0x00};
  }
  writer.write_u16(2);
  writer.write_u16(gradient.smoothness);
  writer.write_bytes(tail);
  while (writer.bytes().size() % 4U != 0U) {
    writer.write_u8(0);
  }
  return std::move(writer.bytes());
}

// 'blwh' (docs/adjustment-kinds.md, "Black & White"): u32 descriptor version 16 and a
// descriptor of six weight longs (Rd, Yllw, Grn, Cyn, Bl, Mgnt), the useTint bool, a
// tintColor RGBC object of doubles, bwPresetKind and blackAndWhitePresetFileName.
constexpr std::array<const char*, 6> kBlackWhiteWeightKeys{"Rd  ", "Yllw", "Grn ", "Cyn ", "Bl  ", "Mgnt"};

std::array<int*, 6> black_white_weights(BlackWhiteAdjustment& settings) {
  return {&settings.reds, &settings.yellows, &settings.greens, &settings.cyans, &settings.blues, &settings.magentas};
}

std::optional<DescriptorObject> read_black_white_descriptor(std::span<const std::uint8_t> payload) {
  try {
    BigEndianReader reader(payload);
    if (reader.read_u32() != 16U) {
      return std::nullopt;
    }
    return read_descriptor(reader);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<AdjustmentSettings> parse_photoshop_black_white_adjustment(std::span<const std::uint8_t> payload) {
  const auto descriptor = read_black_white_descriptor(payload);
  if (!descriptor.has_value()) {
    return std::nullopt;
  }
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::BlackWhite;
  auto& bw = settings.black_white;
  const auto weights = black_white_weights(bw);
  for (std::size_t index = 0; index < weights.size(); ++index) {
    const auto* value = descriptor_value(*descriptor, kBlackWhiteWeightKeys[index]);
    if (value == nullptr || value->type != DescriptorValue::Type::Integer) {
      return std::nullopt;
    }
    *weights[index] = value->integer_value;
  }
  bw.tint = descriptor_bool(*descriptor, "useTint");
  if (const auto* tint = descriptor_object(*descriptor, "tintColor"); tint != nullptr) {
    const auto channel = [tint](const char* key) {
      return static_cast<std::uint8_t>(std::clamp(std::lround(descriptor_number(*tint, key, 0.0)), 0L, 255L));
    };
    bw.tint_color = RgbColor{channel("Rd  "), channel("Grn "), channel("Bl  ")};
  }
  bw = clamp_black_white(bw);
  return settings;
}

std::vector<std::uint8_t> photoshop_black_white_payload(const BlackWhiteAdjustment& settings,
                                                        const UnknownPsdBlock* original) {
  // Photoshop 2026's block for a new layer without a tint, the base for fresh layers.
  static const std::vector<std::uint8_t> kFreshBlock{
      0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x6E, 0x75, 0x6C, 0x6C, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x52, 0x64,
      0x20, 0x20, 0x6C, 0x6F, 0x6E, 0x67, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00,
      0x59, 0x6C, 0x6C, 0x77, 0x6C, 0x6F, 0x6E, 0x67, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x00,
      0x00, 0x00, 0x47, 0x72, 0x6E, 0x20, 0x6C, 0x6F, 0x6E, 0x67, 0x00, 0x00, 0x00, 0x28,
      0x00, 0x00, 0x00, 0x00, 0x43, 0x79, 0x6E, 0x20, 0x6C, 0x6F, 0x6E, 0x67, 0x00, 0x00,
      0x00, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x42, 0x6C, 0x20, 0x20, 0x6C, 0x6F, 0x6E, 0x67,
      0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x4D, 0x67, 0x6E, 0x74, 0x6C, 0x6F,
      0x6E, 0x67, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x07, 0x75, 0x73, 0x65, 0x54,
      0x69, 0x6E, 0x74, 0x62, 0x6F, 0x6F, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x09, 0x74, 0x69,
      0x6E, 0x74, 0x43, 0x6F, 0x6C, 0x6F, 0x72, 0x4F, 0x62, 0x6A, 0x63, 0x00, 0x00, 0x00,
      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x52, 0x47, 0x42, 0x43, 0x00, 0x00, 0x00,
      0x03, 0x00, 0x00, 0x00, 0x00, 0x52, 0x64, 0x20, 0x20, 0x64, 0x6F, 0x75, 0x62, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x47, 0x72, 0x6E,
      0x20, 0x64, 0x6F, 0x75, 0x62, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x42, 0x6C, 0x20, 0x20, 0x64, 0x6F, 0x75, 0x62, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x62, 0x77, 0x50, 0x72, 0x65,
      0x73, 0x65, 0x74, 0x4B, 0x69, 0x6E, 0x64, 0x6C, 0x6F, 0x6E, 0x67, 0x00, 0x00, 0x00,
      0x04, 0x00, 0x00, 0x00, 0x1B, 0x62, 0x6C, 0x61, 0x63, 0x6B, 0x41, 0x6E, 0x64, 0x57,
      0x68, 0x69, 0x74, 0x65, 0x50, 0x72, 0x65, 0x73, 0x65, 0x74, 0x46, 0x69, 0x6C, 0x65,
      0x4E, 0x61, 0x6D, 0x65, 0x54, 0x45, 0x58, 0x54, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
      0x00, 0x00};
  if (original != nullptr) {
    if (const auto parsed = parse_photoshop_black_white_adjustment(original->payload);
        parsed.has_value() && parsed->black_white == clamp_black_white(settings)) {
      return original->payload;
    }
  }
  // Every modeled value has a fixed width, so it is patched in place: Patchy's descriptor
  // writer does not reproduce Photoshop's one-character null name, and the original's
  // other keys stay exactly as they were.
  std::vector<std::uint8_t> payload =
      original != nullptr && read_black_white_descriptor(original->payload).has_value() ? original->payload
                                                                                        : kFreshBlock;
  if (!read_black_white_descriptor(payload).has_value() || payload.size() < 16U) {
    payload = kFreshBlock;
  }
  // The value after a charID key ("\0\0\0\0" + key + type) or a stringID key (length + id + type).
  const auto value_offset = [&payload](std::string_view key, std::string_view type, bool string_id,
                                       std::size_t from) -> std::optional<std::size_t> {
    std::vector<std::uint8_t> pattern;
    const auto length = static_cast<std::uint32_t>(string_id ? key.size() : 0U);
    for (int shift = 24; shift >= 0; shift -= 8) {
      pattern.push_back(static_cast<std::uint8_t>((length >> shift) & 0xFFU));
    }
    pattern.insert(pattern.end(), key.begin(), key.end());
    pattern.insert(pattern.end(), type.begin(), type.end());
    const auto found = std::search(payload.begin() + static_cast<std::ptrdiff_t>(from), payload.end(),
                                   pattern.begin(), pattern.end());
    if (found == payload.end()) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(found - payload.begin()) + pattern.size();
  };
  const auto put_u32 = [&payload](std::size_t offset, std::uint32_t value) {
    for (int index = 0; index < 4; ++index) {
      payload[offset + static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(value >> (24 - 8 * index));
    }
  };
  const auto put_f64 = [&payload](std::size_t offset, double value) {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    for (int index = 0; index < 8; ++index) {
      payload[offset + static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(bits >> (56 - 8 * index));
    }
  };
  auto bw = clamp_black_white(settings);
  const auto weights = black_white_weights(bw);
  for (std::size_t index = 0; index < weights.size(); ++index) {
    if (const auto offset = value_offset(kBlackWhiteWeightKeys[index], "long", false, 4); offset.has_value()) {
      put_u32(*offset, static_cast<std::uint32_t>(*weights[index]));
    }
  }
  if (const auto offset = value_offset("useTint", "bool", true, 4); offset.has_value()) {
    payload[*offset] = bw.tint ? 1U : 0U;
  }
  if (const auto color = value_offset("tintColor", "Objc", true, 4); color.has_value()) {
    const std::array<std::pair<const char*, std::uint8_t>, 3> channels{
        {{"Rd  ", bw.tint_color.red}, {"Grn ", bw.tint_color.green}, {"Bl  ", bw.tint_color.blue}}};
    for (const auto& [key, value] : channels) {
      if (const auto offset = value_offset(key, "doub", false, *color); offset.has_value()) {
        put_f64(*offset, static_cast<double>(value));
      }
    }
  }
  return payload;
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
  // 'brit' alone (no CgEd descriptor) is the CS-era legacy record.
  settings.brightness_contrast.use_legacy = true;
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
    if (const auto* legacy = descriptor_value(descriptor, "useLegacy");
        legacy != nullptr && legacy->type == DescriptorValue::Type::Bool) {
      parsed.use_legacy = legacy->bool_value;
    }
    parsed.settings.brightness_contrast.use_legacy = parsed.use_legacy;
    if (parsed.use_legacy) {
      parsed.settings.brightness_contrast.brightness = std::clamp(brightness->integer_value, -100, 100);
      parsed.settings.brightness_contrast.contrast = std::clamp(contrast->integer_value, -100, 100);
    } else {
      parsed.settings.brightness_contrast.brightness =
          std::clamp(brightness->integer_value, -kModernBrightnessRange, kModernBrightnessRange);
      parsed.settings.brightness_contrast.contrast =
          std::clamp(contrast->integer_value, kModernContrastMin, kModernContrastMax);
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

bool brightness_contrast_settings_match(const BrightnessContrastAdjustment& a,
                                        const BrightnessContrastAdjustment& b) {
  return a.brightness == b.brightness && a.contrast == b.contrast && a.use_legacy == b.use_legacy;
}

}  // namespace

std::vector<std::uint8_t> photoshop_brightness_contrast_payload(const BrightnessContrastAdjustment& settings,
                                                                const Layer& layer) {
  const auto clamped = clamp_brightness_contrast(settings);
  const auto original = original_brightness_contrast_state(layer);
  if (original.has_value() && brightness_contrast_settings_match(*original, clamped)) {
    for (const auto& block : layer.unknown_psd_blocks()) {
      if (block.key == "brit") {
        return block.payload;  // unedited: byte-identical round trip
      }
    }
  }
  BigEndianWriter writer;
  if (!clamped.use_legacy) {
    // Photoshop writes an all-zero compatibility 'brit' beside a modern CgEd
    // (byte-verified on PS 2026 files); the descriptor carries the values.
    writer.write_u16(0);
    writer.write_u16(0);
    writer.write_u16(0);
    writer.write_u8(0);
    writer.write_u8(0);
    return writer.bytes();
  }
  writer.write_u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(clamped.brightness)));
  writer.write_u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(clamped.contrast)));
  writer.write_u16(127);  // mean, Photoshop's fixed midpoint
  writer.write_u8(0);     // lab
  writer.write_u8(0);     // pad
  return writer.bytes();
}

std::optional<std::vector<std::uint8_t>> photoshop_brightness_contrast_descriptor_payload(
    const BrightnessContrastAdjustment& settings, const Layer& layer) {
  const auto clamped = clamp_brightness_contrast(settings);
  const UnknownPsdBlock* original_block = nullptr;
  for (const auto& block : layer.unknown_psd_blocks()) {
    if (block.key == "CgEd") {
      original_block = &block;
    }
  }
  const auto original = original_brightness_contrast_state(layer);
  if (original.has_value() && brightness_contrast_settings_match(*original, clamped)) {
    if (original_block != nullptr) {
      return original_block->payload;  // unedited: byte-identical round trip
    }
    // Legacy brit-only file, untouched: keep it descriptor-free.
    return std::nullopt;
  }
  if (clamped.use_legacy && original_block == nullptr) {
    // Edited legacy settings on a file that never had a descriptor stay
    // brit-only, the historical Patchy output Photoshop reads as legacy.
    return std::nullopt;
  }
  // Regenerate Photoshop 2026's native 7-item shape: null descriptor with
  // Vrsn/Brgh/Cntr/means/Lab/useLegacy/Auto ('means' and 'useLegacy' are
  // stringIDs, the rest charIDs). 'means' is inert at render time (dark- and
  // light-context COM probes render identically); preserve an imported value,
  // else write Photoshop's default 127.
  auto means = 127;
  auto lab = false;
  auto auto_flag = false;
  if (original_block != nullptr) {
    try {
      BigEndianReader reader(original_block->payload);
      if (reader.read_u32() == 16) {
        const auto descriptor = read_descriptor(reader);
        if (const auto* value = descriptor_value(descriptor, "means");
            value != nullptr && value->type == DescriptorValue::Type::Integer) {
          means = value->integer_value;
        }
        lab = descriptor_bool(descriptor, "Lab ", false);
        auto_flag = descriptor_bool(descriptor, "Auto", false);
      }
    } catch (const std::exception&) {
    }
  }
  DescriptorObject descriptor;
  descriptor.name = "";
  descriptor.class_id = "null";
  const auto add_integer = [&descriptor](const std::string& key, bool long_form, int value) {
    DescriptorValue entry;
    entry.type = DescriptorValue::Type::Integer;
    entry.integer_value = value;
    descriptor.values.emplace(key, std::move(entry));
    descriptor.key_order.push_back({key, long_form});
  };
  const auto add_bool = [&descriptor](const std::string& key, bool long_form, bool value) {
    DescriptorValue entry;
    entry.type = DescriptorValue::Type::Bool;
    entry.bool_value = value;
    descriptor.values.emplace(key, std::move(entry));
    descriptor.key_order.push_back({key, long_form});
  };
  add_integer("Vrsn", false, 1);
  add_integer("Brgh", false, clamped.brightness);
  add_integer("Cntr", false, clamped.contrast);
  add_integer("means", true, means);
  add_bool("Lab ", false, lab);
  add_bool("useLegacy", true, clamped.use_legacy);
  add_bool("Auto", false, auto_flag);
  BigEndianWriter writer;
  writer.write_u32(16);
  write_descriptor(writer, descriptor);
  return writer.bytes();
}

std::optional<AdjustmentSettings> parse_photoshop_exposure_adjustment(std::span<const std::uint8_t> payload) {
  if (payload.size() < 14) {
    return std::nullopt;
  }
  BigEndianReader reader(payload);
  if (reader.read_u16() != 1) {
    return std::nullopt;
  }
  const auto exposure = std::bit_cast<float>(reader.read_u32());
  const auto offset = std::bit_cast<float>(reader.read_u32());
  const auto gamma = std::bit_cast<float>(reader.read_u32());
  if (!std::isfinite(exposure) || !std::isfinite(offset) || !std::isfinite(gamma)) {
    return std::nullopt;
  }
  // Clamp as doubles first: a wild float must not overflow the integer conversion.
  const auto scaled = [](float value, double scale, int low, int high) {
    return static_cast<int>(
        std::lround(std::clamp(static_cast<double>(value) * scale, static_cast<double>(low), static_cast<double>(high))));
  };
  AdjustmentSettings settings;
  settings.kind = AdjustmentKind::Exposure;
  settings.exposure = ExposureAdjustment{scaled(exposure, 100.0, -kExposureValueRange, kExposureValueRange),
                                         scaled(offset, 10000.0, -kExposureOffsetRange, kExposureOffsetRange),
                                         scaled(gamma, 100.0, kExposureGammaMin, kExposureGammaMax)};
  return settings;
}

std::vector<std::uint8_t> photoshop_exposure_payload(const ExposureAdjustment& settings,
                                                     const UnknownPsdBlock* original) {
  const auto clamped = clamp_exposure(settings);
  if (original != nullptr) {
    // Unedited imported payloads re-emit byte-for-byte, which also keeps Photoshop's
    // exact float32 values instead of the rounded fields.
    const auto parsed = parse_photoshop_exposure_adjustment(original->payload);
    if (parsed.has_value() && parsed->exposure.exposure_hundredths == clamped.exposure_hundredths &&
        parsed->exposure.offset_ten_thousandths == clamped.offset_ten_thousandths &&
        parsed->exposure.gamma_hundredths == clamped.gamma_hundredths) {
      return original->payload;
    }
  }
  BigEndianWriter writer;
  writer.write_u16(1);
  writer.write_u32(std::bit_cast<std::uint32_t>(static_cast<float>(clamped.exposure_hundredths / 100.0)));
  writer.write_u32(std::bit_cast<std::uint32_t>(static_cast<float>(clamped.offset_ten_thousandths / 10000.0)));
  writer.write_u32(std::bit_cast<std::uint32_t>(static_cast<float>(clamped.gamma_hundredths / 100.0)));
  writer.write_u16(0);
  return writer.bytes();
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
