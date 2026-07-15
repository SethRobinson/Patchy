#include "psd/abr_reader.hpp"

#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <stdexcept>
#include <utility>

namespace patchy::psd {

namespace {

constexpr std::int32_t kMaxBrushDimension = 4096;

struct DescBrushInfo {
  std::string name;
  std::optional<double> spacing;  // fraction of diameter
  double base_angle_degrees{0.0};
  double base_roundness{100.0};
  BrushDynamics dynamics{};
  std::optional<int> tool_flow_percent;
  std::optional<bool> tool_airbrush;
};

// One 'brVr' variation object: {'bVTy' control, 'fStp' fade steps, 'jitter' %, 'Mnm ' minimum %}.
struct VariationRead {
  double jitter{0.0};     // 0..1
  double minimum{0.0};    // 0..1
  int control{0};         // raw bVTy value
  int fade_steps{25};
};

VariationRead read_variation(const DescriptorObject& preset, std::string_view key) {
  VariationRead out;
  const auto* object = descriptor_object(preset, key);
  if (object == nullptr) {
    return out;
  }
  out.jitter = std::clamp(descriptor_number(*object, "jitter") / 100.0, 0.0, 10.0);
  out.minimum = std::clamp(descriptor_number(*object, "Mnm ") / 100.0, 0.0, 1.0);
  out.control = static_cast<int>(descriptor_number(*object, "bVTy"));
  out.fade_steps = std::max(1, static_cast<int>(descriptor_number(*object, "fStp", 25.0)));
  return out;
}

// Photoshop's 'bVTy' control values: 0 Off, 1 Fade, 2 Pen Pressure, 3 Pen Tilt, 4 Stylus Wheel,
// 5 Rotation, 6 Initial Direction, 7 Direction. Unknown values degrade to Off (the jitter still
// imports).
[[nodiscard]] BrushDynamicControl control_from_bvty(int value) {
  switch (value) {
    case 1: return BrushDynamicControl::Fade;
    case 2: return BrushDynamicControl::PenPressure;
    case 3: return BrushDynamicControl::PenTilt;
    case 4: return BrushDynamicControl::StylusWheel;
    case 5: return BrushDynamicControl::PenRotation;
    case 6: return BrushDynamicControl::InitialDirection;
    case 7: return BrushDynamicControl::Direction;
    default: return BrushDynamicControl::Off;
  }
}

// bVTy for a non-angle dynamic. A Photoshop "Off" (0) means the preset author chose no control,
// which in Patchy maps to the slot's default: GlobalDefault for size/roundness/opacity so the
// user's global pen preferences keep working on imported jitter-only packs (Photoshop's
// options-bar pressure-override buttons are the analog of those preferences), plain Off for
// scatter/count. Direction/InitialDirection are angle-only and degrade to Off.
[[nodiscard]] BrushDynamicControl non_angle_control_from_bvty(int value,
                                                              BrushDynamicControl zero_default) {
  switch (value) {
    case 1: return BrushDynamicControl::Fade;
    case 2: return BrushDynamicControl::PenPressure;
    case 3: return BrushDynamicControl::PenTilt;
    case 4: return BrushDynamicControl::StylusWheel;
    case 5: return BrushDynamicControl::PenRotation;
    case 6:
    case 7: return BrushDynamicControl::Off;
    default: return zero_default;
  }
}

// Extracts the supported dynamics from a brushPreset descriptor. Keys verified against a
// Photoshop 2026 export (test-fixtures/abr/photoshop-dynamics.abr): the preset-level
// flipX/flipY are the flip jitters (the static tip flips live inside the 'Brsh' object), the
// minimum diameter/roundness are preset-level siblings of the 'brVr' objects, and 'Cnt ' is a
// double. Every dynamic's control imports (size/roundness/opacity map bVTy 0 through
// non_angle_control_from_bvty to GlobalDefault). The later blocks add the compatible static
// Texture, single Dual Brush, Color Dynamics, and Wet Edges subsets.
[[nodiscard]] std::uint32_t stable_string_seed(std::string_view text) noexcept {
  std::uint32_t hash = 2166136261U;
  for (const auto character : text) {
    hash ^= static_cast<std::uint8_t>(character);
    hash *= 16777619U;
  }
  return hash;
}

[[nodiscard]] std::string descriptor_string(const DescriptorObject& object, std::string_view key) {
  const auto* value = descriptor_value(object, key);
  return value != nullptr && value->type == DescriptorValue::Type::String ? value->string_value
                                                                          : std::string{};
}

[[nodiscard]] BrushTextureStyle texture_style_from_name(std::string_view name) noexcept {
  const auto contains = [name](std::string_view needle) {
    return name.find(needle) != std::string_view::npos;
  };
  if (contains("Canvas") || contains("canvas") || contains("Burlap") || contains("Towel") ||
      contains("Paper") || contains("paper")) {
    return BrushTextureStyle::Canvas;
  }
  if (contains("Dot") || contains("dot") || contains("Pebble") || contains("Stone")) {
    return BrushTextureStyle::Speckle;
  }
  return BrushTextureStyle::FineGrain;
}

[[nodiscard]] BrushDynamics parse_brush_dynamics(const DescriptorObject& preset,
                                                  const DescriptorObject& primary_brush) {
  BrushDynamics dynamics;
  if (descriptor_bool(preset, "useTipDynamics")) {
    const auto size = read_variation(preset, "szVr");
    dynamics.size_jitter = std::clamp(size.jitter, 0.0, 1.0);
    dynamics.minimum_diameter =
        std::clamp(descriptor_number(preset, "minimumDiameter", size.minimum * 100.0) / 100.0, 0.0, 1.0);
    dynamics.size_control =
        non_angle_control_from_bvty(size.control, BrushDynamicControl::GlobalDefault);
    dynamics.size_fade_steps = size.fade_steps;
    const auto angle = read_variation(preset, "angleDynamics");
    dynamics.angle_jitter = std::clamp(angle.jitter, 0.0, 1.0);
    dynamics.angle_control = control_from_bvty(angle.control);
    dynamics.angle_fade_steps = angle.fade_steps;
    const auto roundness = read_variation(preset, "roundnessDynamics");
    dynamics.roundness_jitter = std::clamp(roundness.jitter, 0.0, 1.0);
    dynamics.minimum_roundness =
        std::clamp(descriptor_number(preset, "minimumRoundness", 25.0) / 100.0, 0.0, 1.0);
    dynamics.roundness_control =
        non_angle_control_from_bvty(roundness.control, BrushDynamicControl::GlobalDefault);
    dynamics.roundness_fade_steps = roundness.fade_steps;
    dynamics.flip_x_jitter = descriptor_bool(preset, "flipX");
    dynamics.flip_y_jitter = descriptor_bool(preset, "flipY");
  }
  if (descriptor_bool(preset, "useScatter")) {
    const auto scatter = read_variation(preset, "scatterDynamics");
    dynamics.scatter = std::clamp(scatter.jitter, 0.0, 10.0);
    dynamics.scatter_both_axes = descriptor_bool(preset, "bothAxes");
    dynamics.scatter_control =
        non_angle_control_from_bvty(scatter.control, BrushDynamicControl::Off);
    dynamics.scatter_fade_steps = scatter.fade_steps;
    dynamics.count =
        std::clamp(static_cast<int>(std::lround(descriptor_number(preset, "Cnt ", 1.0))), 1, 16);
    const auto count = read_variation(preset, "countDynamics");
    dynamics.count_jitter = std::clamp(count.jitter, 0.0, 1.0);
    dynamics.count_control = non_angle_control_from_bvty(count.control, BrushDynamicControl::Off);
    dynamics.count_fade_steps = count.fade_steps;
  }
  if (descriptor_bool(preset, "usePaintDynamics")) {
    const auto opacity = read_variation(preset, "opVr");
    dynamics.opacity_jitter = std::clamp(opacity.jitter, 0.0, 1.0);
    dynamics.minimum_opacity = std::clamp(opacity.minimum, 0.0, 1.0);
    dynamics.opacity_control =
        non_angle_control_from_bvty(opacity.control, BrushDynamicControl::GlobalDefault);
    dynamics.opacity_fade_steps = opacity.fade_steps;
    const auto flow = read_variation(preset, "prVr");
    dynamics.flow_jitter = std::clamp(flow.jitter, 0.0, 1.0);
    dynamics.minimum_flow = std::clamp(flow.minimum, 0.0, 1.0);
    dynamics.flow_control = non_angle_control_from_bvty(flow.control, BrushDynamicControl::Off);
    dynamics.flow_fade_steps = flow.fade_steps;
  }
  if (descriptor_bool(preset, "useTexture")) {
    dynamics.texture_enabled = true;
    dynamics.texture_scale =
        std::clamp(descriptor_number(preset, "textureScale", 100.0) / 100.0, 0.01, 10.0);
    dynamics.texture_depth =
        std::clamp(descriptor_number(preset, "textureDepth", 50.0) / 100.0, 0.0, 1.0);
    dynamics.texture_invert = descriptor_bool(preset, "InvT") ||
                              descriptor_bool(preset, "invertTexture");
    if (const auto* texture = descriptor_object(preset, "Txtr"); texture != nullptr) {
      auto identity = descriptor_string(*texture, "Idnt");
      const auto name = descriptor_string(*texture, "Nm  ");
      if (identity.empty()) {
        identity = name;
      }
      if (!identity.empty()) {
        dynamics.texture_seed = stable_string_seed(identity);
      }
      dynamics.texture_style = texture_style_from_name(name);
    }
    // Deliberate patent design-around: textureDepthDynamics/minimumDepth are observed for
    // compatibility but never connected to pressure, velocity, direction, or stylus pose.
  }
  if (const auto* dual = descriptor_object(preset, "dualBrush");
      dual != nullptr && descriptor_bool(*dual, "useDualBrush")) {
    dynamics.dual_brush_enabled = true;
    if (const auto* secondary = descriptor_object(*dual, "Brsh"); secondary != nullptr) {
      const auto primary_diameter = std::max(1.0, descriptor_number(primary_brush, "Dmtr", 1.0));
      const auto secondary_diameter = descriptor_number(*secondary, "Dmtr", primary_diameter * 0.5);
      dynamics.dual_brush_size =
          std::clamp(secondary_diameter / primary_diameter, 0.05, 4.0);
      dynamics.dual_brush_hardness =
          std::clamp(descriptor_number(*secondary, "Hrdn", 100.0) / 100.0, 0.0, 1.0);
      dynamics.dual_brush_spacing =
          std::clamp(descriptor_number(*secondary, "Spcn", 100.0) / 100.0, 0.1, 10.0);
    }
  }
  if (descriptor_bool(preset, "useColorDynamics")) {
    dynamics.color_dynamics_enabled = true;
    const auto foreground_background = read_variation(preset, "clVr");
    dynamics.foreground_background_jitter =
        std::clamp(foreground_background.jitter, 0.0, 1.0);
    dynamics.color_control = non_angle_control_from_bvty(foreground_background.control,
                                                         BrushDynamicControl::Off);
    dynamics.color_fade_steps = foreground_background.fade_steps;
    dynamics.hue_jitter =
        std::clamp(descriptor_number(preset, "H   ") / 100.0, 0.0, 1.0);
    dynamics.saturation_jitter =
        std::clamp(descriptor_number(preset, "Strt") / 100.0, 0.0, 1.0);
    dynamics.brightness_jitter =
        std::clamp(descriptor_number(preset, "Brgh") / 100.0, 0.0, 1.0);
    dynamics.purity =
        std::clamp(descriptor_number(preset, "purity") / 100.0, -1.0, 1.0);
    dynamics.color_per_tip = descriptor_bool(preset, "colorDynamicsPerTip", true);
  }
  dynamics.wet_edges = descriptor_bool(preset, "Wtdg") || descriptor_bool(preset, "wetEdges");
  return dynamics;
}

// The 'desc' block is one serialized ActionDescriptor whose "Brsh" list holds every brush preset
// in file order. Sampled presets (class sampledBrush) pair with 'samp' block entries in order.
std::vector<DescBrushInfo> parse_desc_brush_infos(std::span<const std::uint8_t> desc_block,
                                                  std::vector<std::string>& warnings) {
  std::vector<DescBrushInfo> infos;
  BigEndianReader reader(desc_block);
  const auto descriptor_version = reader.read_u32();
  if (descriptor_version != 16U) {
    throw std::runtime_error("Unsupported ABR descriptor version");
  }
  const auto root = read_descriptor(reader);
  const auto* brush_list = descriptor_value(root, "Brsh");
  if (brush_list == nullptr || brush_list->type != DescriptorValue::Type::List) {
    return infos;
  }
  for (const auto& entry : brush_list->list_value) {
    if (entry.type != DescriptorValue::Type::Object || entry.object_value == nullptr) {
      continue;
    }
    const auto& preset = *entry.object_value;
    const auto* brush = descriptor_object(preset, "Brsh");
    if (brush == nullptr || brush->class_id != "sampledBrush") {
      continue;  // computed brushes have no bitmap and no samp entry
    }
    DescBrushInfo info;
    if (const auto* name = descriptor_value(preset, "Nm  ");
        name != nullptr && name->type == DescriptorValue::Type::String) {
      info.name = name->string_value;
    }
    if (const auto* spacing = descriptor_value(*brush, "Spcn");
        spacing != nullptr &&
        (spacing->type == DescriptorValue::Type::UnitFloat || spacing->type == DescriptorValue::Type::Double)) {
      info.spacing = std::clamp(spacing->double_value / 100.0, 0.01, 10.0);
    }
    info.base_angle_degrees = descriptor_number(*brush, "Angl", 0.0);
    info.base_roundness = std::clamp(descriptor_number(*brush, "Rndn", 100.0), 1.0, 100.0);
    info.dynamics = parse_brush_dynamics(preset, *brush);
    // Photoshop 2026 ground-truth capture: Transfer Flow is the 'prVr' variation, the
    // options-bar percentage is toolOptions.flow, and Airbrush is preset 'Rpt ' (the Action
    // Manager names it "repeat"). Only a brush preset that explicitly carries tool options
    // may change Patchy's application-wide Brush settings when selected.
    if (const auto* tool_options = descriptor_object(preset, "toolOptions");
        tool_options != nullptr && descriptor_bool(*tool_options, "brushPreset")) {
      const auto* flow = descriptor_value(*tool_options, "flow");
      if (flow != nullptr &&
          (flow->type == DescriptorValue::Type::Integer ||
           flow->type == DescriptorValue::Type::LargeInteger ||
           flow->type == DescriptorValue::Type::Double ||
           flow->type == DescriptorValue::Type::UnitFloat)) {
        info.tool_flow_percent = std::clamp(
            static_cast<int>(std::lround(descriptor_number(*tool_options, "flow", 100.0))), 1, 100);
      }
      const auto* repeat = descriptor_value(preset, "Rpt ");
      if (repeat != nullptr && repeat->type == DescriptorValue::Type::Bool) {
        info.tool_airbrush = repeat->bool_value;
      }
    }

    if (descriptor_bool(preset, "useTexture")) {
      const auto depth = read_variation(preset, "textureDepthDynamics");
      if (depth.control != 0 || depth.jitter > 0.0) {
        warnings.push_back(
            "Brush \"" + (info.name.empty() ? std::string("(unnamed)") : info.name) +
            "\": input-driven texture depth was imported as a static depth for patent safety");
      }
    }
    infos.push_back(std::move(info));
  }
  return infos;
}

// Reads mask rows (raw or RLE) into an 8-bit mask, converting 16-bit samples down.
std::vector<std::uint8_t> read_mask_rows(BigEndianReader& reader, std::int32_t width, std::int32_t height,
                                         std::int32_t depth, std::uint8_t compression) {
  const auto bytes_per_sample = depth == 16 ? 2 : 1;
  const auto row_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(bytes_per_sample);
  std::vector<std::uint8_t> data;
  data.reserve(row_bytes * static_cast<std::size_t>(height));

  if (compression == 0) {
    data = reader.read_bytes(row_bytes * static_cast<std::size_t>(height));
  } else if (compression == 1) {
    std::vector<std::uint16_t> row_lengths(static_cast<std::size_t>(height));
    for (auto& length : row_lengths) {
      length = reader.read_u16();
    }
    for (const auto length : row_lengths) {
      const auto encoded = reader.read_bytes(length);
      const auto decoded = decode_packbits(encoded, row_bytes);
      data.insert(data.end(), decoded.begin(), decoded.end());
    }
  } else {
    throw std::runtime_error("Unknown ABR brush compression mode");
  }

  if (bytes_per_sample == 1) {
    return data;
  }
  std::vector<std::uint8_t> mask(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (std::size_t index = 0; index < mask.size(); ++index) {
    mask[index] = data[index * 2U];  // big-endian: high byte is a fine 16→8 conversion
  }
  return mask;
}

// Crops a mask to its non-empty bounding box; returns false when the mask is entirely empty.
bool crop_mask_to_content(AbrBrush& brush) {
  std::int32_t min_x = brush.width;
  std::int32_t min_y = brush.height;
  std::int32_t max_x = -1;
  std::int32_t max_y = -1;
  for (std::int32_t y = 0; y < brush.height; ++y) {
    const auto* row = brush.mask.data() + static_cast<std::size_t>(y) * brush.width;
    for (std::int32_t x = 0; x < brush.width; ++x) {
      if (row[x] != 0U) {
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
      }
    }
  }
  if (max_x < min_x || max_y < min_y) {
    return false;
  }
  const auto cropped_width = max_x - min_x + 1;
  const auto cropped_height = max_y - min_y + 1;
  if (cropped_width == brush.width && cropped_height == brush.height) {
    return true;
  }
  std::vector<std::uint8_t> cropped(static_cast<std::size_t>(cropped_width) *
                                    static_cast<std::size_t>(cropped_height));
  for (std::int32_t y = 0; y < cropped_height; ++y) {
    const auto* src = brush.mask.data() + static_cast<std::size_t>(y + min_y) * brush.width + min_x;
    std::copy_n(src, cropped_width, cropped.data() + static_cast<std::size_t>(y) * cropped_width);
  }
  brush.width = cropped_width;
  brush.height = cropped_height;
  brush.mask = std::move(cropped);
  return true;
}

void validate_brush_dimensions(std::int32_t width, std::int32_t height, std::int32_t depth) {
  if (width <= 0 || height <= 0) {
    throw std::runtime_error("brush has empty bounds");
  }
  if (width > kMaxBrushDimension || height > kMaxBrushDimension) {
    throw std::runtime_error("brush is larger than 4096px");
  }
  if (depth != 8 && depth != 16) {
    throw std::runtime_error("brush depth is not 8 or 16 bit");
  }
}

AbrReadResult read_abr_v12(BigEndianReader& reader, std::uint16_t version, std::string& error) {
  AbrReadResult result;
  const auto count = reader.read_u16();
  for (std::uint16_t index = 0; index < count; ++index) {
    const auto type = reader.read_u16();
    const auto size = reader.read_u32();
    if (size > reader.remaining()) {
      throw std::runtime_error("ABR brush entry is truncated");
    }
    const auto entry_bytes = reader.read_bytes(size);
    if (type != 2U) {
      result.warnings.push_back("Skipped computed (non-bitmap) brush " + std::to_string(index + 1));
      continue;
    }
    try {
      BigEndianReader entry(entry_bytes);
      (void)entry.read_u32();                    // misc
      const auto spacing = entry.read_u16();     // percent of diameter
      AbrBrush brush;
      brush.spacing = std::clamp(static_cast<double>(spacing) / 100.0, 0.01, 10.0);
      if (version == 2U) {
        brush.name = read_descriptor_unicode_string(entry);  // same int32-count UTF-16BE layout
      }
      (void)entry.read_u8();                     // antialiasing
      entry.skip(8);                             // short bounds
      const auto top = static_cast<std::int32_t>(entry.read_u32());
      const auto left = static_cast<std::int32_t>(entry.read_u32());
      const auto bottom = static_cast<std::int32_t>(entry.read_u32());
      const auto right = static_cast<std::int32_t>(entry.read_u32());
      const auto depth = static_cast<std::int32_t>(entry.read_u16());
      const auto compression = entry.read_u8();
      brush.width = right - left;
      brush.height = bottom - top;
      validate_brush_dimensions(brush.width, brush.height, depth);
      brush.mask = read_mask_rows(entry, brush.width, brush.height, depth, compression);
      if (!crop_mask_to_content(brush)) {
        result.warnings.push_back("Skipped empty brush " + std::to_string(index + 1));
        continue;
      }
      result.brushes.push_back(std::move(brush));
    } catch (const std::exception& entry_error) {
      result.warnings.push_back("Skipped unreadable brush " + std::to_string(index + 1) + ": " +
                                entry_error.what());
    }
  }
  if (result.brushes.empty()) {
    error = "The file contains no sampled (bitmap) brushes";
  }
  return result;
}

AbrReadResult read_abr_v6(BigEndianReader& reader, std::span<const std::uint8_t> bytes, std::string& error) {
  AbrReadResult result;
  const auto subversion = reader.read_u16();
  if (subversion != 1U && subversion != 2U) {
    throw std::runtime_error("Unsupported ABR subversion " + std::to_string(subversion));
  }

  std::span<const std::uint8_t> samp_block;
  std::span<const std::uint8_t> desc_block;
  while (reader.remaining() >= 12U) {
    const auto signature = key_string(read_signature(reader));
    if (signature != "8BIM") {
      throw std::runtime_error("ABR tagged block has a corrupt signature");
    }
    const auto key = key_string(read_signature(reader));
    const auto length = reader.read_u32();
    if (length > reader.remaining()) {
      throw std::runtime_error("ABR tagged block is truncated");
    }
    const auto block = bytes.subspan(reader.position(), length);
    if (key == "samp") {
      samp_block = block;
    } else if (key == "desc") {
      desc_block = block;
    }
    // Tagged blocks are padded to 4-byte boundaries; the length field excludes the padding.
    auto padded_length = static_cast<std::size_t>(length);
    while (padded_length % 4U != 0U) {
      ++padded_length;
    }
    reader.skip(std::min(padded_length, reader.remaining()));
  }
  if (samp_block.empty()) {
    error = "The file contains no sampled (bitmap) brushes";
    return result;
  }

  std::vector<DescBrushInfo> infos;
  if (!desc_block.empty()) {
    try {
      infos = parse_desc_brush_infos(desc_block, result.warnings);
    } catch (const std::exception& desc_error) {
      result.warnings.push_back(std::string("Brush names unavailable: ") + desc_error.what());
    }
  }

  BigEndianReader samp(samp_block);
  const auto key_skip = subversion == 1U ? 47U : 301U;
  std::size_t sample_index = 0;
  while (samp.remaining() >= 4U) {
    const auto brush_size = samp.read_u32();
    auto padded_size = static_cast<std::size_t>(brush_size);
    while (padded_size % 4U != 0U) {
      ++padded_size;
    }
    if (padded_size > samp.remaining()) {
      // A truncated trailing entry: keep what we already parsed and warn.
      result.warnings.push_back("Ignored a truncated trailing brush entry");
      break;
    }
    const auto entry_bytes = samp.read_bytes(padded_size);
    ++sample_index;
    try {
      BigEndianReader entry(std::span<const std::uint8_t>(entry_bytes.data(), brush_size));
      entry.skip(key_skip);  // UUID string + unknown fixed-layout fields
      const auto top = static_cast<std::int32_t>(entry.read_u32());
      const auto left = static_cast<std::int32_t>(entry.read_u32());
      const auto bottom = static_cast<std::int32_t>(entry.read_u32());
      const auto right = static_cast<std::int32_t>(entry.read_u32());
      const auto depth = static_cast<std::int32_t>(entry.read_u16());
      const auto compression = entry.read_u8();
      AbrBrush brush;
      brush.width = right - left;
      brush.height = bottom - top;
      validate_brush_dimensions(brush.width, brush.height, depth);
      brush.mask = read_mask_rows(entry, brush.width, brush.height, depth, compression);
      if (!crop_mask_to_content(brush)) {
        result.warnings.push_back("Skipped empty brush " + std::to_string(sample_index));
        continue;
      }
      const auto info_index = result.brushes.size();
      if (info_index < infos.size()) {
        const auto& info = infos[info_index];
        brush.name = info.name;
        if (info.spacing.has_value()) {
          brush.spacing = *info.spacing;
        }
        brush.base_angle_degrees = info.base_angle_degrees;
        brush.base_roundness = info.base_roundness;
        brush.dynamics = info.dynamics;
        brush.tool_flow_percent = info.tool_flow_percent;
        brush.tool_airbrush = info.tool_airbrush;
      }
      result.brushes.push_back(std::move(brush));
    } catch (const std::exception& entry_error) {
      result.warnings.push_back("Skipped unreadable brush " + std::to_string(sample_index) + ": " +
                                entry_error.what());
    }
  }
  if (result.brushes.empty()) {
    error = "The file contains no sampled (bitmap) brushes";
  }
  return result;
}

}  // namespace

std::optional<AbrReadResult> read_abr(std::span<const std::uint8_t> bytes, std::string& error) {
  error.clear();
  try {
    BigEndianReader reader(bytes);
    const auto version = reader.read_u16();
    AbrReadResult result;
    if (version == 1U || version == 2U) {
      result = read_abr_v12(reader, version, error);
    } else if (version == 6U || version == 7U || version == 10U) {
      result = read_abr_v6(reader, bytes, error);
    } else {
      error = "Unsupported ABR version " + std::to_string(version);
      return std::nullopt;
    }
    if (!error.empty()) {
      return std::nullopt;
    }
    return result;
  } catch (const std::exception& parse_error) {
    error = parse_error.what();
    return std::nullopt;
  }
}

}  // namespace patchy::psd
