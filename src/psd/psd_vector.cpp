// Vector shape/path codec for the PSD reader: vmsk/vsms path records,
// SoCo/GdFl/PtFl fill content, vstk stroke style, vogk live-shape origination,
// and the saved-path image resources. Every encoding here was pinned by
// observation of Photoshop 27.8 (docs/vector-tools.md, July 2026); parse
// failures leave the layer byte-preserved and locked rather than guessing.

#include "psd/psd_document_io.hpp"
#include "psd/psd_io_internal.hpp"

#include "core/vector_raster.hpp"
#include "psd/psd_layer_effects.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace patchy::psd {

namespace {

constexpr std::size_t kPathRecordSize = 26U;

std::int32_t read_i32_at(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
  return static_cast<std::int32_t>((static_cast<std::uint32_t>(bytes[offset]) << 24U) |
                                   (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
                                   (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
                                   static_cast<std::uint32_t>(bytes[offset + 3U]));
}

std::uint16_t read_u16_at(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) |
                                    static_cast<std::uint16_t>(bytes[offset + 1U]));
}

std::uint32_t read_u32_at(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
         static_cast<std::uint32_t>(bytes[offset + 3U]);
}

// Walks the 26-byte path records: selector 6 (fill rule) and 8 (initial fill)
// header records, then per subpath a length record (selector 0 closed /
// 3 open: knot count, combine op, constant 1, subpath/shape-group index)
// followed by its knot records (1/2 closed linked/corner, 4/5 open). Knot
// records hold (in, anchor, out) pairs, each (y, x) as i32 8.24 fixed-point
// fractions of the canvas extent. Selector 7 (clipboard) is skipped; any
// other selector fails the parse.
std::optional<VectorPath> parse_records(std::span<const std::uint8_t> payload, std::size_t offset,
                                        std::int32_t canvas_width, std::int32_t canvas_height) {
  VectorPath path;
  PathSubpath* current = nullptr;
  std::size_t knots_expected = 0;
  while (offset + kPathRecordSize <= payload.size()) {
    const auto record = payload.subspan(offset, kPathRecordSize);
    offset += kPathRecordSize;
    const auto selector = read_u16_at(record, 0);
    switch (selector) {
      case 6:
        path.fill_rule_value = read_u16_at(record, 2);
        break;
      case 8:
        path.initial_fill_value = read_u16_at(record, 2);
        break;
      case 0:
      case 3: {
        if (current != nullptr && current->anchors.size() != knots_expected) {
          return std::nullopt;
        }
        const auto knot_count = read_u16_at(record, 2);
        const auto operation = read_u16_at(record, 4);
        if (operation > 3U) {
          return std::nullopt;
        }
        PathSubpath subpath;
        subpath.closed = selector == 0;
        subpath.op = static_cast<PathCombineOp>(operation);
        subpath.shape_group = static_cast<std::int32_t>(read_u32_at(record, 12));
        subpath.anchors.reserve(knot_count);
        path.subpaths.push_back(std::move(subpath));
        current = &path.subpaths.back();
        knots_expected = knot_count;
        break;
      }
      case 1:
      case 2:
      case 4:
      case 5: {
        if (current == nullptr || current->anchors.size() >= knots_expected) {
          return std::nullopt;
        }
        PathAnchor anchor;
        anchor.in_y = path_coordinate_from_fixed(read_i32_at(record, 2), canvas_height);
        anchor.in_x = path_coordinate_from_fixed(read_i32_at(record, 6), canvas_width);
        anchor.anchor_y = path_coordinate_from_fixed(read_i32_at(record, 10), canvas_height);
        anchor.anchor_x = path_coordinate_from_fixed(read_i32_at(record, 14), canvas_width);
        anchor.out_y = path_coordinate_from_fixed(read_i32_at(record, 18), canvas_height);
        anchor.out_x = path_coordinate_from_fixed(read_i32_at(record, 22), canvas_width);
        anchor.smooth = selector == 1 || selector == 4;
        current->anchors.push_back(anchor);
        break;
      }
      case 7:
        break;  // clipboard record: irrelevant to geometry
      default:
        return std::nullopt;
    }
  }
  if (current != nullptr && current->anchors.size() != knots_expected) {
    return std::nullopt;
  }
  return path;
}

// Vector descriptor blocks begin with either a bare u32 descriptorVersion 16
// (SoCo/GdFl/PtFl/vstk) or a block version followed by descriptorVersion 16
// (vogk: 1, 16). Returns the parsed root descriptor.
std::optional<DescriptorObject> read_block_descriptor(std::span<const std::uint8_t> payload) {
  if (payload.size() < 8U) {
    return std::nullopt;
  }
  BigEndianReader reader(payload);
  const auto first = reader.read_u32();
  if (first != 16U) {
    const auto second = reader.read_u32();
    if (second != 16U) {
      return std::nullopt;
    }
  }
  try {
    return read_descriptor(reader);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<VectorFill> parse_fill_content(VectorFillKind kind, const DescriptorObject& object,
                                             const CmykColorConverter& cmyk) {
  VectorFill fill;
  fill.kind = kind;
  switch (kind) {
    case VectorFillKind::Solid: {
      if (descriptor_object(object, "Clr ") == nullptr) {
        return std::nullopt;
      }
      fill.color = descriptor_rgb_color(object, "Clr ", cmyk, RgbColor{0, 0, 0});
      return fill;
    }
    case VectorFillKind::Gradient: {
      fill.gradient = parse_layer_style_gradient(object, cmyk);
      if (fill.gradient.form == GradientDefinitionForm::Solid && fill.gradient.color_stops.empty()) {
        return std::nullopt;
      }
      fill.gradient_noise_pre_seed =
          static_cast<std::int32_t>(std::lround(descriptor_number(object, "noisePreSeed", 0.0)));
      return fill;
    }
    case VectorFillKind::Pattern: {
      const auto* pattern = descriptor_object(object, "Ptrn");
      if (pattern == nullptr) {
        return std::nullopt;
      }
      if (const auto* name = descriptor_value(*pattern, "Nm  ");
          name != nullptr && name->type == DescriptorValue::Type::String) {
        fill.pattern_name = name->string_value;
      }
      if (const auto* id = descriptor_value(*pattern, "Idnt");
          id != nullptr && id->type == DescriptorValue::Type::String) {
        fill.pattern_id = id->string_value;
      }
      fill.pattern_scale = descriptor_number(object, "Scl ", 100.0) / 100.0;
      fill.pattern_angle_degrees = descriptor_number(object, "Angl", 0.0);
      fill.pattern_linked = descriptor_bool(object, "Algn", true);
      if (const auto* phase = descriptor_object(object, "phase"); phase != nullptr) {
        fill.pattern_phase_x = descriptor_number(*phase, "Hrzn", 0.0);
        fill.pattern_phase_y = descriptor_number(*phase, "Vrtc", 0.0);
      }
      return fill;
    }
    case VectorFillKind::None:
      break;
  }
  return std::nullopt;
}

std::optional<VectorFill> parse_content_object(const DescriptorObject& content,
                                               const CmykColorConverter& cmyk) {
  if (content.class_id == "solidColorLayer") {
    return parse_fill_content(VectorFillKind::Solid, content, cmyk);
  }
  if (content.class_id == "gradientLayer") {
    return parse_fill_content(VectorFillKind::Gradient, content, cmyk);
  }
  if (content.class_id == "patternLayer") {
    return parse_fill_content(VectorFillKind::Pattern, content, cmyk);
  }
  return std::nullopt;
}

double unit_length_to_pixels(const DescriptorValue& value, double resolution) noexcept {
  if (value.type == DescriptorValue::Type::UnitFloat && value.unit == "#Pnt") {
    return value.double_value * resolution / 72.0;
  }
  if (value.type == DescriptorValue::Type::UnitFloat || value.type == DescriptorValue::Type::Double) {
    return value.double_value;
  }
  if (value.type == DescriptorValue::Type::Integer) {
    return value.integer_value;
  }
  return 0.0;
}

}  // namespace

bool is_vector_content_block_key(std::string_view key) noexcept {
  return key == "vmsk" || key == "vsms" || key == "vscg" || key == "vstk" || key == "vogk" ||
         key == "SoCo" || key == "GdFl" || key == "PtFl";
}

std::optional<ParsedVectorMaskBlock> parse_vector_mask_block(std::span<const std::uint8_t> payload,
                                                             std::int32_t canvas_width,
                                                             std::int32_t canvas_height) {
  if (payload.size() < 8U + kPathRecordSize) {
    return std::nullopt;
  }
  const auto flags = read_u32_at(payload, 4);
  auto path = parse_records(payload, 8U, canvas_width, canvas_height);
  if (!path.has_value()) {
    return std::nullopt;
  }
  ParsedVectorMaskBlock parsed;
  parsed.path = std::move(*path);
  parsed.inverted = (flags & 0x01U) != 0;
  parsed.unlinked = (flags & 0x02U) != 0;
  parsed.disabled = (flags & 0x04U) != 0;
  return parsed;
}

std::optional<VectorPath> parse_path_resource_records(std::span<const std::uint8_t> payload,
                                                      std::int32_t canvas_width,
                                                      std::int32_t canvas_height) {
  if (payload.size() < kPathRecordSize) {
    return std::nullopt;
  }
  return parse_records(payload, 0U, canvas_width, canvas_height);
}

std::optional<VectorFill> parse_vector_fill_block(std::string_view key,
                                                  std::span<const std::uint8_t> payload,
                                                  const CmykColorConverter& cmyk) {
  const auto descriptor = read_block_descriptor(payload);
  if (!descriptor.has_value()) {
    return std::nullopt;
  }
  if (key == "SoCo") {
    return parse_fill_content(VectorFillKind::Solid, *descriptor, cmyk);
  }
  if (key == "GdFl") {
    return parse_fill_content(VectorFillKind::Gradient, *descriptor, cmyk);
  }
  if (key == "PtFl") {
    return parse_fill_content(VectorFillKind::Pattern, *descriptor, cmyk);
  }
  return std::nullopt;
}

std::optional<VectorStroke> parse_vector_stroke_block(std::span<const std::uint8_t> payload,
                                                      const CmykColorConverter& cmyk) {
  const auto descriptor = read_block_descriptor(payload);
  if (!descriptor.has_value() || descriptor->class_id != "strokeStyle") {
    return std::nullopt;
  }
  VectorStroke stroke;
  stroke.resolution = descriptor_number(*descriptor, "strokeStyleResolution", 72.0);
  stroke.enabled = descriptor_bool(*descriptor, "strokeEnabled", false);
  stroke.fill_enabled = descriptor_bool(*descriptor, "fillEnabled", true);
  if (const auto* width = descriptor_value(*descriptor, "strokeStyleLineWidth"); width != nullptr) {
    stroke.width = unit_length_to_pixels(*width, stroke.resolution);
  }
  stroke.miter_limit = descriptor_number(*descriptor, "strokeStyleMiterLimit", 100.0);
  const auto cap = descriptor_enum(*descriptor, "strokeStyleLineCapType", "strokeStyleButtCap");
  stroke.cap = cap == "strokeStyleRoundCap"    ? VectorStrokeCap::Round
               : cap == "strokeStyleSquareCap" ? VectorStrokeCap::Square
                                               : VectorStrokeCap::Butt;
  const auto join = descriptor_enum(*descriptor, "strokeStyleLineJoinType", "strokeStyleMiterJoin");
  stroke.join = join == "strokeStyleRoundJoin"   ? VectorStrokeJoin::Round
                : join == "strokeStyleBevelJoin" ? VectorStrokeJoin::Bevel
                                                 : VectorStrokeJoin::Miter;
  const auto alignment = descriptor_enum(*descriptor, "strokeStyleLineAlignment", "strokeStyleAlignCenter");
  stroke.alignment = alignment == "strokeStyleAlignInside"    ? VectorStrokeAlignment::Inside
                     : alignment == "strokeStyleAlignOutside" ? VectorStrokeAlignment::Outside
                                                              : VectorStrokeAlignment::Center;
  stroke.scale_lock = descriptor_bool(*descriptor, "strokeStyleScaleLock", false);
  stroke.stroke_adjust = descriptor_bool(*descriptor, "strokeStyleStrokeAdjust", false);
  if (const auto* dashes = descriptor_value(*descriptor, "strokeStyleLineDashSet");
      dashes != nullptr && dashes->type == DescriptorValue::Type::List) {
    for (const auto& entry : dashes->list_value) {
      if (entry.type == DescriptorValue::Type::UnitFloat || entry.type == DescriptorValue::Type::Double) {
        stroke.dashes.push_back(entry.double_value);
      } else if (entry.type == DescriptorValue::Type::Integer) {
        stroke.dashes.push_back(entry.integer_value);
      }
    }
  }
  if (const auto* offset = descriptor_value(*descriptor, "strokeStyleLineDashOffset"); offset != nullptr) {
    const auto offset_pixels = unit_length_to_pixels(*offset, stroke.resolution);
    stroke.dash_offset = stroke.width > 0.0 ? offset_pixels / stroke.width : 0.0;
  }
  stroke.blend_mode = blend_mode_from_descriptor_enum(
      descriptor_enum(*descriptor, "strokeStyleBlendMode", "Nrml"), std::array<char, 4>{'n', 'o', 'r', 'm'});
  stroke.opacity = percent_to_unit(descriptor_number(*descriptor, "strokeStyleOpacity", 100.0));
  if (const auto* content = descriptor_object(*descriptor, "strokeStyleContent"); content != nullptr) {
    if (auto paint = parse_content_object(*content, cmyk); paint.has_value()) {
      stroke.content = std::move(*paint);
    } else {
      return std::nullopt;
    }
  }
  return stroke;
}

std::optional<std::vector<LiveShapeParams>> parse_vector_origination_block(
    std::span<const std::uint8_t> payload) {
  const auto descriptor = read_block_descriptor(payload);
  if (!descriptor.has_value()) {
    return std::nullopt;
  }
  const auto* list = descriptor_value(*descriptor, "keyDescriptorList");
  if (list == nullptr || list->type != DescriptorValue::Type::List) {
    return std::nullopt;
  }
  std::vector<LiveShapeParams> origination;
  for (const auto& item : list->list_value) {
    if (item.type != DescriptorValue::Type::Object || item.object_value == nullptr) {
      continue;
    }
    const auto& entry = *item.object_value;
    LiveShapeParams params;
    const auto type = static_cast<int>(descriptor_number(entry, "keyOriginType", -1.0));
    params.kind = type == 1   ? LiveShapeKind::Rectangle
                  : type == 2 ? LiveShapeKind::RoundedRectangle
                  : type == 4 ? LiveShapeKind::Line
                  : type == 5 ? LiveShapeKind::Ellipse
                              : LiveShapeKind::Custom;
    params.resolution = descriptor_number(entry, "keyOriginResolution", 72.0);
    params.index = static_cast<std::int32_t>(descriptor_number(entry, "keyOriginIndex", 0.0));
    if (const auto* bbox = descriptor_object(entry, "keyOriginShapeBBox"); bbox != nullptr) {
      params.top = descriptor_number(*bbox, "Top ", 0.0);
      params.left = descriptor_number(*bbox, "Left", 0.0);
      params.bottom = descriptor_number(*bbox, "Btom", 0.0);
      params.right = descriptor_number(*bbox, "Rght", 0.0);
    }
    if (const auto* radii = descriptor_object(entry, "keyOriginRRectRadii"); radii != nullptr) {
      params.corner_radii = {descriptor_number(*radii, "topLeft", 0.0),
                             descriptor_number(*radii, "topRight", 0.0),
                             descriptor_number(*radii, "bottomRight", 0.0),
                             descriptor_number(*radii, "bottomLeft", 0.0)};
    }
    if (const auto* corners = descriptor_object(entry, "keyOriginBoxCorners"); corners != nullptr) {
      const auto corner_point = [&corners](const char* key, double& x, double& y) {
        if (const auto* point = descriptor_object(*corners, key); point != nullptr) {
          x = descriptor_number(*point, "Hrzn", 0.0);
          y = descriptor_number(*point, "Vrtc", 0.0);
        }
      };
      corner_point("rectangleCornerA", params.box_corners[0], params.box_corners[1]);
      corner_point("rectangleCornerB", params.box_corners[2], params.box_corners[3]);
      corner_point("rectangleCornerC", params.box_corners[4], params.box_corners[5]);
      corner_point("rectangleCornerD", params.box_corners[6], params.box_corners[7]);
    }
    if (const auto* transform = descriptor_object(entry, "Trnf"); transform != nullptr) {
      params.transform = {descriptor_number(*transform, "xx", 1.0), descriptor_number(*transform, "xy", 0.0),
                          descriptor_number(*transform, "yx", 0.0), descriptor_number(*transform, "yy", 1.0),
                          descriptor_number(*transform, "tx", 0.0), descriptor_number(*transform, "ty", 0.0)};
    }
    if (params.kind == LiveShapeKind::Line) {
      if (const auto* start = descriptor_object(entry, "keyOriginLineStart"); start != nullptr) {
        params.line_start_x = descriptor_number(*start, "Hrzn", 0.0);
        params.line_start_y = descriptor_number(*start, "Vrtc", 0.0);
      }
      if (const auto* end = descriptor_object(entry, "keyOriginLineEnd"); end != nullptr) {
        params.line_end_x = descriptor_number(*end, "Hrzn", 0.0);
        params.line_end_y = descriptor_number(*end, "Vrtc", 0.0);
      }
      params.line_weight = descriptor_number(entry, "keyOriginLineWeight", 1.0);
      params.arrow_start = descriptor_bool(entry, "keyOriginLineArrowSt", false);
      params.arrow_end = descriptor_bool(entry, "keyOriginLineArrowEnd", false);
      params.arrow_width = descriptor_number(entry, "keyOriginLineArrWdth", 0.0);
      params.arrow_length = descriptor_number(entry, "keyOriginLineArrLngth", 0.0);
      params.arrow_concavity =
          static_cast<std::int32_t>(descriptor_number(entry, "keyOriginLineArrConc", 0.0));
      params.arrow_width_unit_pixels = descriptor_bool(entry, "keyOriginLineWidthArrowUnitPixels", true);
      params.arrow_length_unit_pixels = descriptor_bool(entry, "keyOriginLineLengthArrowUnitPixels", true);
    }
    if (params.kind == LiveShapeKind::Custom) {
      // Unmodeled origination: keep the entry's exact descriptor bytes for
      // verbatim regeneration while the group is untouched.
      BigEndianWriter writer;
      write_descriptor(writer, entry);
      params.raw_descriptor = std::move(writer.bytes());
    }
    origination.push_back(std::move(params));
  }
  return origination;
}

// ---------------------------------------------------------------------------
// Write side. Payloads regenerate patch-in-place from preserved originals
// where possible (the descriptor codec reproduces untouched keys byte-exactly)
// and use the PS 27.8-captured canonical shapes from scratch. Photoshop pads
// these payloads to 4 bytes; the builders do the same.
// ---------------------------------------------------------------------------

namespace {

void pad_payload_to_4(std::vector<std::uint8_t>& payload) {
  while (payload.size() % 4U != 0U) {
    payload.push_back(0);
  }
}

void write_i32_at(std::vector<std::uint8_t>& bytes, std::size_t offset, std::int32_t value) {
  const auto raw = static_cast<std::uint32_t>(value);
  bytes[offset] = static_cast<std::uint8_t>((raw >> 24U) & 0xffU);
  bytes[offset + 1U] = static_cast<std::uint8_t>((raw >> 16U) & 0xffU);
  bytes[offset + 2U] = static_cast<std::uint8_t>((raw >> 8U) & 0xffU);
  bytes[offset + 3U] = static_cast<std::uint8_t>(raw & 0xffU);
}

void write_u16_at(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
  bytes[offset + 1U] = static_cast<std::uint8_t>(value & 0xffU);
}

// Appends the 26-byte record stream (selector 6, selector 8, then subpaths).
void append_path_records(std::vector<std::uint8_t>& out, const VectorPath& path,
                         std::int32_t canvas_width, std::int32_t canvas_height) {
  const auto append_record = [&out]() -> std::size_t {
    const auto offset = out.size();
    out.resize(offset + kPathRecordSize, 0);
    return offset;
  };
  auto fill_rule = append_record();
  write_u16_at(out, fill_rule, 6);
  write_u16_at(out, fill_rule + 2, path.fill_rule_value);
  auto initial_fill = append_record();
  write_u16_at(out, initial_fill, 8);
  write_u16_at(out, initial_fill + 2, path.initial_fill_value);
  for (const auto& subpath : path.subpaths) {
    const auto length_record = append_record();
    write_u16_at(out, length_record, subpath.closed ? 0 : 3);
    write_u16_at(out, length_record + 2, static_cast<std::uint16_t>(subpath.anchors.size()));
    write_u16_at(out, length_record + 4, static_cast<std::uint16_t>(subpath.op));
    write_u16_at(out, length_record + 6, 1);
    write_i32_at(out, length_record + 12, subpath.shape_group);
    for (const auto& anchor : subpath.anchors) {
      const auto knot = append_record();
      const std::uint16_t selector =
          subpath.closed ? (anchor.smooth ? 1 : 2) : (anchor.smooth ? 4 : 5);
      write_u16_at(out, knot, selector);
      write_i32_at(out, knot + 2, path_coordinate_to_fixed(anchor.in_y, canvas_height));
      write_i32_at(out, knot + 6, path_coordinate_to_fixed(anchor.in_x, canvas_width));
      write_i32_at(out, knot + 10, path_coordinate_to_fixed(anchor.anchor_y, canvas_height));
      write_i32_at(out, knot + 14, path_coordinate_to_fixed(anchor.anchor_x, canvas_width));
      write_i32_at(out, knot + 18, path_coordinate_to_fixed(anchor.out_y, canvas_height));
      write_i32_at(out, knot + 22, path_coordinate_to_fixed(anchor.out_x, canvas_width));
    }
  }
}

// --- descriptor construction helpers ---

DescriptorValue make_double_value(double value) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::Double;
  result.double_value = value;
  return result;
}

DescriptorValue make_unit_value(std::string unit, double value) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::UnitFloat;
  result.unit = std::move(unit);
  result.double_value = value;
  return result;
}

DescriptorValue make_bool_value(bool value) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::Bool;
  result.bool_value = value;
  return result;
}

DescriptorValue make_long_value(std::int32_t value) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::Integer;
  result.integer_value = value;
  return result;
}

DescriptorValue make_enum_value(std::string type, std::string value) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::Enum;
  result.enum_type = std::move(type);
  result.enum_value = std::move(value);
  return result;
}

DescriptorValue make_text_value(std::string value) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::String;
  result.string_value = std::move(value);
  return result;
}

DescriptorValue make_object_value(DescriptorObject object) {
  DescriptorValue result;
  result.type = DescriptorValue::Type::Object;
  result.object_value = std::make_shared<DescriptorObject>(std::move(object));
  return result;
}

void put_value(DescriptorObject& object, const std::string& key, DescriptorValue value) {
  if (object.values.find(key) == object.values.end()) {
    object.key_order.push_back(DescriptorObject::KeyEntry{key, false});
  }
  object.values[key] = std::move(value);
}

DescriptorObject rgb_color_object(RgbColor color) {
  DescriptorObject object;
  object.class_id = "RGBC";
  put_value(object, "Rd  ", make_double_value(color.red));
  put_value(object, "Grn ", make_double_value(color.green));
  put_value(object, "Bl  ", make_double_value(color.blue));
  return object;
}

const char* gradient_type_enum_value(LayerStyleGradientType type) {
  switch (type) {
    case LayerStyleGradientType::Radial:
      return "Rdl ";
    case LayerStyleGradientType::Angle:
      return "Angl";
    case LayerStyleGradientType::Reflected:
      return "Rflc";
    case LayerStyleGradientType::Diamond:
      return "Dmnd";
    case LayerStyleGradientType::Linear:
      break;
  }
  return "Lnr ";
}

const char* gradient_interpolation_enum_value(GradientInterpolationMethod method) {
  switch (method) {
    case GradientInterpolationMethod::Perceptual:
      return "perceptual";
    case GradientInterpolationMethod::Linear:
      return "linear";
    case GradientInterpolationMethod::Classic:
      break;
  }
  return "Gcls";
}

DescriptorObject gradient_object(const LayerStyleGradient& gradient) {
  DescriptorObject object;
  object.name = "Gradient";
  object.class_id = "Grdn";
  put_value(object, "Nm  ", make_text_value(gradient.name));
  put_value(object, "GrdF", make_enum_value("GrdF", "CstS"));
  put_value(object, "Intr", make_double_value(gradient.smoothness));
  DescriptorValue colors;
  colors.type = DescriptorValue::Type::List;
  for (const auto& stop : gradient.color_stops) {
    DescriptorObject color_stop;
    color_stop.class_id = "Clrt";
    put_value(color_stop, "Clr ", make_object_value(rgb_color_object(stop.color)));
    const char* stop_type = stop.kind == GradientColorStop::Kind::Foreground   ? "FrgC"
                            : stop.kind == GradientColorStop::Kind::Background ? "BckC"
                                                                               : "UsrS";
    put_value(color_stop, "Type", make_enum_value("Clry", stop_type));
    put_value(color_stop, "Lctn",
              make_long_value(static_cast<std::int32_t>(std::lround(stop.location * 4096.0F))));
    put_value(color_stop, "Mdpn",
              make_long_value(static_cast<std::int32_t>(std::lround(stop.midpoint * 100.0F))));
    colors.list_value.push_back(make_object_value(std::move(color_stop)));
  }
  put_value(object, "Clrs", std::move(colors));
  DescriptorValue transparency;
  transparency.type = DescriptorValue::Type::List;
  for (const auto& stop : gradient.alpha_stops) {
    DescriptorObject alpha_stop;
    alpha_stop.class_id = "TrnS";
    put_value(alpha_stop, "Opct", make_unit_value("#Prc", stop.opacity * 100.0F));
    put_value(alpha_stop, "Lctn",
              make_long_value(static_cast<std::int32_t>(std::lround(stop.location * 4096.0F))));
    put_value(alpha_stop, "Mdpn",
              make_long_value(static_cast<std::int32_t>(std::lround(stop.midpoint * 100.0F))));
    transparency.list_value.push_back(make_object_value(std::move(alpha_stop)));
  }
  put_value(object, "Trns", std::move(transparency));
  return object;
}

// Builds the content descriptor for a fill (also the vstk strokeStyleContent).
DescriptorObject fill_content_object(const VectorFill& fill) {
  DescriptorObject object;
  switch (fill.kind) {
    case VectorFillKind::Gradient: {
      object.class_id = "gradientLayer";
      put_value(object, "gradientsInterpolationMethod",
                make_enum_value("gradientInterpolationMethodType",
                                gradient_interpolation_enum_value(fill.gradient.interpolation)));
      put_value(object, "Angl", make_unit_value("#Ang", fill.gradient.angle_degrees));
      if (fill.gradient.reverse) {
        put_value(object, "Rvrs", make_bool_value(true));
      }
      if (fill.gradient.dither) {
        put_value(object, "Dthr", make_bool_value(true));
      }
      put_value(object, "Type",
                make_enum_value("GrdT", gradient_type_enum_value(fill.gradient.type)));
      if (!fill.gradient.align_with_layer) {
        put_value(object, "Algn", make_bool_value(false));
      }
      if (std::fabs(fill.gradient.scale - 1.0F) > 1e-4F) {
        put_value(object, "Scl ", make_unit_value("#Prc", fill.gradient.scale * 100.0F));
      }
      if (std::fabs(fill.gradient.offset_x_percent) > 1e-4F ||
          std::fabs(fill.gradient.offset_y_percent) > 1e-4F) {
        DescriptorObject offset;
        offset.class_id = "Pnt ";
        put_value(offset, "Hrzn", make_unit_value("#Prc", fill.gradient.offset_x_percent));
        put_value(offset, "Vrtc", make_unit_value("#Prc", fill.gradient.offset_y_percent));
        put_value(object, "Ofst", make_object_value(std::move(offset)));
      }
      put_value(object, "noisePreSeed", make_long_value(fill.gradient_noise_pre_seed));
      put_value(object, "Grad", make_object_value(gradient_object(fill.gradient)));
      break;
    }
    case VectorFillKind::Pattern: {
      object.class_id = "patternLayer";
      DescriptorObject pattern;
      pattern.class_id = "Ptrn";
      put_value(pattern, "Nm  ", make_text_value(fill.pattern_name));
      put_value(pattern, "Idnt", make_text_value(fill.pattern_id));
      put_value(object, "Ptrn", make_object_value(std::move(pattern)));
      if (std::fabs(fill.pattern_angle_degrees) > 1e-9) {
        put_value(object, "Angl", make_unit_value("#Ang", fill.pattern_angle_degrees));
      }
      if (std::fabs(fill.pattern_scale - 1.0) > 1e-9) {
        put_value(object, "Scl ", make_unit_value("#Prc", fill.pattern_scale * 100.0));
      }
      if (!fill.pattern_linked) {
        put_value(object, "Algn", make_bool_value(false));
      }
      if (std::fabs(fill.pattern_phase_x) > 1e-9 || std::fabs(fill.pattern_phase_y) > 1e-9) {
        DescriptorObject phase;
        phase.class_id = "Pnt ";
        put_value(phase, "Hrzn", make_double_value(fill.pattern_phase_x));
        put_value(phase, "Vrtc", make_double_value(fill.pattern_phase_y));
        put_value(object, "phase", make_object_value(std::move(phase)));
      }
      break;
    }
    case VectorFillKind::Solid:
    case VectorFillKind::None:
      object.class_id = "solidColorLayer";
      put_value(object, "Clr ", make_object_value(rgb_color_object(fill.color)));
      break;
  }
  return object;
}

std::vector<std::uint8_t> descriptor_block_payload(const DescriptorObject& descriptor,
                                                   std::optional<std::uint32_t> leading_version) {
  BigEndianWriter writer;
  if (leading_version.has_value()) {
    writer.write_u32(*leading_version);
  }
  writer.write_u32(16);
  write_descriptor(writer, descriptor);
  auto payload = std::move(writer.bytes());
  pad_payload_to_4(payload);
  return payload;
}

DescriptorObject unit_rect_object(double top, double left, double bottom, double right) {
  DescriptorObject object;
  object.class_id = "unitRect";
  put_value(object, "unitValueQuadVersion", make_long_value(1));
  put_value(object, "Top ", make_unit_value("#Pxl", top));
  put_value(object, "Left", make_unit_value("#Pxl", left));
  put_value(object, "Btom", make_unit_value("#Pxl", bottom));
  put_value(object, "Rght", make_unit_value("#Pxl", right));
  return object;
}

DescriptorObject point_object(double x, double y) {
  DescriptorObject object;
  object.class_id = "Pnt ";
  put_value(object, "Hrzn", make_double_value(x));
  put_value(object, "Vrtc", make_double_value(y));
  return object;
}

DescriptorObject box_corners_object(const std::array<double, 8>& corners) {
  DescriptorObject object;
  object.class_id = "null";
  put_value(object, "rectangleCornerA", make_object_value(point_object(corners[0], corners[1])));
  put_value(object, "rectangleCornerB", make_object_value(point_object(corners[2], corners[3])));
  put_value(object, "rectangleCornerC", make_object_value(point_object(corners[4], corners[5])));
  put_value(object, "rectangleCornerD", make_object_value(point_object(corners[6], corners[7])));
  return object;
}

DescriptorObject transform_object(const std::array<double, 6>& transform) {
  DescriptorObject object;
  object.name = "Transform";
  object.class_id = "Trnf";
  put_value(object, "xx", make_double_value(transform[0]));
  put_value(object, "xy", make_double_value(transform[1]));
  put_value(object, "yx", make_double_value(transform[2]));
  put_value(object, "yy", make_double_value(transform[3]));
  put_value(object, "tx", make_double_value(transform[4]));
  put_value(object, "ty", make_double_value(transform[5]));
  return object;
}

}  // namespace

const char* vector_fill_block_key(VectorFillKind kind) {
  switch (kind) {
    case VectorFillKind::Gradient:
      return "GdFl";
    case VectorFillKind::Pattern:
      return "PtFl";
    case VectorFillKind::Solid:
    case VectorFillKind::None:
      break;
  }
  return "SoCo";
}

std::vector<std::uint8_t> vector_mask_block_payload(const VectorPath& path, bool disabled, bool inverted,
                                                    bool unlinked, std::int32_t canvas_width,
                                                    std::int32_t canvas_height) {
  std::vector<std::uint8_t> payload;
  payload.resize(8, 0);
  write_i32_at(payload, 0, 3);  // version
  std::uint32_t flags = 0;
  if (inverted) {
    flags |= 0x01U;
  }
  if (unlinked) {
    flags |= 0x02U;
  }
  if (disabled) {
    flags |= 0x04U;
  }
  write_i32_at(payload, 4, static_cast<std::int32_t>(flags));
  append_path_records(payload, path, canvas_width, canvas_height);
  pad_payload_to_4(payload);
  return payload;
}

std::vector<std::uint8_t> vector_fill_block_payload(const VectorFill& fill,
                                                    const UnknownPsdBlock* original) {
  // Patch-in-place: parse the original descriptor and overwrite only the
  // modeled keys so unmodeled data and id forms survive byte-exactly.
  if (original != nullptr) {
    // Photoshop stores color doubles the 8-bit model quantizes (213.9995...);
    // when the model still equals the original's parse, keep its exact bytes.
    if (const auto reparsed =
            parse_vector_fill_block(original->key, original->payload, CmykColorConverter{});
        reparsed.has_value() && *reparsed == fill) {
      return original->payload;
    }
    if (auto descriptor = read_block_descriptor(original->payload); descriptor.has_value()) {
      auto content = fill_content_object(fill);
      for (const auto& entry : content.key_order) {
        auto value = content.values.at(entry.key);
        put_value(*descriptor, entry.key, std::move(value));
      }
      return descriptor_block_payload(*descriptor, std::nullopt);
    }
  }
  auto content = fill_content_object(fill);
  content.class_id = "null";  // fill blocks use a null root holding the content keys
  return descriptor_block_payload(content, std::nullopt);
}

std::vector<std::uint8_t> vector_stroke_block_payload(const VectorStroke& stroke,
                                                      const UnknownPsdBlock* original) {
  DescriptorObject descriptor;
  if (original != nullptr) {
    if (const auto reparsed = parse_vector_stroke_block(original->payload, CmykColorConverter{});
        reparsed.has_value() && *reparsed == stroke) {
      return original->payload;
    }
    if (auto parsed = read_block_descriptor(original->payload);
        parsed.has_value() && parsed->class_id == "strokeStyle") {
      descriptor = std::move(*parsed);
    }
  }
  if (descriptor.class_id.empty()) {
    descriptor.class_id = "strokeStyle";
  }
  const char* cap = stroke.cap == VectorStrokeCap::Round    ? "strokeStyleRoundCap"
                    : stroke.cap == VectorStrokeCap::Square ? "strokeStyleSquareCap"
                                                            : "strokeStyleButtCap";
  const char* join = stroke.join == VectorStrokeJoin::Round   ? "strokeStyleRoundJoin"
                     : stroke.join == VectorStrokeJoin::Bevel ? "strokeStyleBevelJoin"
                                                              : "strokeStyleMiterJoin";
  const char* alignment = stroke.alignment == VectorStrokeAlignment::Inside    ? "strokeStyleAlignInside"
                          : stroke.alignment == VectorStrokeAlignment::Outside ? "strokeStyleAlignOutside"
                                                                               : "strokeStyleAlignCenter";
  // The PS 27.8 canonical 16-item order (docs/vector-tools.md).
  put_value(descriptor, "strokeStyleVersion", make_long_value(2));
  put_value(descriptor, "strokeEnabled", make_bool_value(stroke.enabled));
  put_value(descriptor, "fillEnabled", make_bool_value(stroke.fill_enabled));
  put_value(descriptor, "strokeStyleLineWidth", make_unit_value("#Pxl", stroke.width));
  put_value(descriptor, "strokeStyleLineDashOffset",
            make_unit_value("#Pnt", stroke.dash_offset * stroke.width * 72.0 /
                                        (stroke.resolution > 0.0 ? stroke.resolution : 72.0)));
  put_value(descriptor, "strokeStyleMiterLimit", make_double_value(stroke.miter_limit));
  put_value(descriptor, "strokeStyleLineCapType", make_enum_value("strokeStyleLineCapType", cap));
  put_value(descriptor, "strokeStyleLineJoinType", make_enum_value("strokeStyleLineJoinType", join));
  put_value(descriptor, "strokeStyleLineAlignment", make_enum_value("strokeStyleLineAlignment", alignment));
  put_value(descriptor, "strokeStyleScaleLock", make_bool_value(stroke.scale_lock));
  put_value(descriptor, "strokeStyleStrokeAdjust", make_bool_value(stroke.stroke_adjust));
  DescriptorValue dash_set;
  dash_set.type = DescriptorValue::Type::List;
  for (const auto dash : stroke.dashes) {
    dash_set.list_value.push_back(make_unit_value("#Nne", dash));
  }
  put_value(descriptor, "strokeStyleLineDashSet", std::move(dash_set));
  put_value(descriptor, "strokeStyleBlendMode",
            make_enum_value("BlnM", std::string(blend_mode_lfx2_string(stroke.blend_mode))));
  put_value(descriptor, "strokeStyleOpacity", make_unit_value("#Prc", stroke.opacity * 100.0));
  put_value(descriptor, "strokeStyleContent", make_object_value(fill_content_object(stroke.content)));
  put_value(descriptor, "strokeStyleResolution", make_double_value(stroke.resolution));
  return descriptor_block_payload(descriptor, std::nullopt);
}

std::vector<std::uint8_t> vector_origination_block_payload(std::span<const LiveShapeParams> origination,
                                                           const UnknownPsdBlock* original) {
  if (original != nullptr) {
    if (const auto reparsed = parse_vector_origination_block(original->payload);
        reparsed.has_value() && std::equal(reparsed->begin(), reparsed->end(),
                                           origination.begin(), origination.end())) {
      return original->payload;
    }
  }
  DescriptorObject root;
  root.class_id = "null";
  DescriptorValue list;
  list.type = DescriptorValue::Type::List;
  for (const auto& params : origination) {
    if (params.kind == LiveShapeKind::Custom && !params.raw_descriptor.empty()) {
      BigEndianReader reader(params.raw_descriptor);
      try {
        list.list_value.push_back(make_object_value(read_descriptor(reader)));
      } catch (const std::exception&) {
      }
      continue;
    }
    DescriptorObject entry;
    entry.class_id = "null";
    const std::int32_t type = params.kind == LiveShapeKind::Rectangle          ? 1
                              : params.kind == LiveShapeKind::RoundedRectangle ? 2
                              : params.kind == LiveShapeKind::Line             ? 4
                              : params.kind == LiveShapeKind::Ellipse          ? 5
                                                                               : 0;
    put_value(entry, "keyOriginType", make_long_value(type));
    put_value(entry, "keyOriginResolution", make_double_value(params.resolution));
    if (params.kind == LiveShapeKind::RoundedRectangle) {
      DescriptorObject radii;
      radii.class_id = "radii";
      put_value(radii, "unitValueQuadVersion", make_long_value(1));
      // Descriptor order per capture: topRight, topLeft, bottomLeft, bottomRight
      // (model order is TL, TR, BR, BL).
      put_value(radii, "topRight", make_unit_value("#Pxl", params.corner_radii[1]));
      put_value(radii, "topLeft", make_unit_value("#Pxl", params.corner_radii[0]));
      put_value(radii, "bottomLeft", make_unit_value("#Pxl", params.corner_radii[3]));
      put_value(radii, "bottomRight", make_unit_value("#Pxl", params.corner_radii[2]));
      put_value(entry, "keyOriginRRectRadii", make_object_value(std::move(radii)));
    }
    put_value(entry, "keyOriginShapeBBox",
              make_object_value(unit_rect_object(params.top, params.left, params.bottom, params.right)));
    if (params.kind == LiveShapeKind::Line) {
      put_value(entry, "Trnf", make_object_value(transform_object(params.transform)));
      put_value(entry, "keyOriginLineEnd",
                make_object_value(point_object(params.line_end_x, params.line_end_y)));
      put_value(entry, "keyOriginLineStart",
                make_object_value(point_object(params.line_start_x, params.line_start_y)));
      put_value(entry, "keyOriginLineWeight", make_double_value(params.line_weight));
      put_value(entry, "keyOriginLineArrowSt", make_bool_value(params.arrow_start));
      put_value(entry, "keyOriginLineArrowEnd", make_bool_value(params.arrow_end));
      put_value(entry, "keyOriginLineArrWdth", make_double_value(params.arrow_width));
      put_value(entry, "keyOriginLineArrLngth", make_double_value(params.arrow_length));
      put_value(entry, "keyOriginLineArrConc", make_long_value(params.arrow_concavity));
      put_value(entry, "keyOriginLineWidthArrowUnitPixels",
                make_bool_value(params.arrow_width_unit_pixels));
      put_value(entry, "keyOriginLineLengthArrowUnitPixels",
                make_bool_value(params.arrow_length_unit_pixels));
      put_value(entry, "keyOriginBoxCorners", make_object_value(box_corners_object(params.box_corners)));
      put_value(entry, "keyOriginIndex", make_long_value(params.index));
    } else {
      if (params.kind != LiveShapeKind::Ellipse) {
        put_value(entry, "keyOriginBoxCorners", make_object_value(box_corners_object(params.box_corners)));
      }
      put_value(entry, "Trnf", make_object_value(transform_object(params.transform)));
      put_value(entry, "keyOriginIndex", make_long_value(params.index));
    }
    list.list_value.push_back(make_object_value(std::move(entry)));
  }
  put_value(root, "keyDescriptorList", std::move(list));
  return descriptor_block_payload(root, 1U);
}

CoverageBuffer vector_mask_derived_plane(const LayerVectorMask& mask) {
  const auto hull = mask.path.bounds();
  if (!hull.has_value()) {
    return CoverageBuffer{};
  }
  Rect clip{static_cast<std::int32_t>(std::floor(hull->left)) - 1,
            static_cast<std::int32_t>(std::floor(hull->top)) - 1, 0, 0};
  clip.width = static_cast<std::int32_t>(std::ceil(hull->right)) + 2 - clip.x;
  clip.height = static_cast<std::int32_t>(std::ceil(hull->bottom)) + 2 - clip.y;
  auto plain = mask;
  plain.inverted = false;  // the derived plane holds plain coverage
  return rasterize_vector_mask_coverage(plain, clip);
}

std::vector<std::uint8_t> document_path_resource_payload(const DocumentPath& path,
                                                         std::int32_t canvas_width,
                                                         std::int32_t canvas_height) {
  std::vector<std::uint8_t> payload;
  append_path_records(payload, path.path(), canvas_width, canvas_height);
  return payload;
}

void upsert_document_path_resources(std::vector<ImageResource>& resources, const Document& document) {
  // Remove path resources whose document path is gone (or whose kind moved).
  const auto document_uses_resource_id = [&document](std::uint16_t id) {
    for (const auto& path : document.paths()) {
      if (path.resource_id().has_value() && *path.resource_id() == id) {
        return true;
      }
    }
    return false;
  };
  std::erase_if(resources, [&](const ImageResource& resource) {
    const bool is_path_resource = (resource.id >= kPsdSavedPathResourceFirst &&
                                   resource.id <= kPsdSavedPathResourceLast) ||
                                  resource.id == kPsdWorkPathResourceId;
    if (!is_path_resource || document_uses_resource_id(resource.id)) {
      return false;
    }
    // Only payloads that parse as path records were importable as document
    // paths, so their absence from the document means the user deleted them.
    // Anything unparseable in the id range stays byte-preserved (never guess).
    return parse_path_resource_records(resource.payload, document.width(), document.height())
        .has_value();
  });

  // Allocate ids for new paths and upsert dirty/new payloads.
  const auto id_taken = [&resources, &document](std::uint16_t id) {
    for (const auto& resource : resources) {
      if (resource.id == id) {
        return true;
      }
    }
    for (const auto& path : document.paths()) {
      if (path.resource_id().has_value() && *path.resource_id() == id) {
        return true;
      }
    }
    return false;
  };
  for (const auto& path : document.paths()) {
    std::uint16_t resource_id = 0;
    if (path.resource_id().has_value()) {
      resource_id = *path.resource_id();
    } else if (path.kind() == DocumentPathKind::Work) {
      resource_id = kPsdWorkPathResourceId;
    } else {
      // Deterministic allocation: the lowest free saved-path id (stable
      // across saves; the document itself stays const during writes).
      for (std::uint16_t candidate = kPsdSavedPathResourceFirst;
           candidate <= kPsdSavedPathResourceLast; ++candidate) {
        if (!id_taken(candidate)) {
          resource_id = candidate;
          break;
        }
      }
      if (resource_id == 0) {
        continue;  // 998 saved paths exhausted; skip rather than corrupt
      }
    }
    const bool needs_payload =
        path.dirty() || path.raw_payload() == nullptr || !path.resource_id().has_value();
    if (needs_payload) {
      auto payload = document_path_resource_payload(path, document.width(), document.height());
      upsert_image_resource(resources, resource_id, std::move(payload));
    }
    if (path.kind() != DocumentPathKind::Work) {
      // Saved-path resource names carry the path name (renames included).
      for (auto& resource : resources) {
        if (resource.id == resource_id) {
          resource.name = path.name();
        }
      }
    }
  }

  // Clipping-path selector (2999): pascal name padded even + 4 zero bytes + 1.
  const DocumentPath* clipping = nullptr;
  for (const auto& path : document.paths()) {
    if (path.is_clipping_path() && path.kind() == DocumentPathKind::Saved) {
      clipping = &path;
      break;
    }
  }
  if (clipping == nullptr) {
    remove_image_resource(resources, kPsdClippingPathNameResourceId);
  } else {
    std::vector<std::uint8_t> payload;
    const auto& name = clipping->name();
    const auto length = std::min<std::size_t>(name.size(), 255U);
    payload.push_back(static_cast<std::uint8_t>(length));
    payload.insert(payload.end(), name.begin(), name.begin() + static_cast<std::ptrdiff_t>(length));
    if (payload.size() % 2U != 0U) {
      payload.push_back(0);
    }
    payload.insert(payload.end(), {0, 0, 0, 0, 1});
    upsert_image_resource(resources, kPsdClippingPathNameResourceId, std::move(payload));
  }
}

void finalize_vector_layers(Document& document) {
  const Rect canvas = Rect::from_size(document.width(), document.height());
  const auto process = [&](auto&& self, std::vector<Layer>& layers) -> void {
    for (auto& layer : layers) {
      if (!layer.children().empty()) {
        self(self, layer.children());
      }
      if (!vector_lock_reason(layer).empty()) {
        continue;
      }
      if (layer.vector_shape() != nullptr) {
        if (!has_visible_alpha(std::as_const(layer).pixels())) {
          update_vector_shape_raster(layer, canvas, &document.metadata().patterns);
        } else {
          layer.metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPhotoshop;
        }
      }
      if (layer.vector_mask() != nullptr && layer.vector_mask()->cache.empty()) {
        update_vector_mask_raster(layer, canvas);
      }
    }
  };
  process(process, document.layers());
}

void parse_document_path_resources(Document& document, std::span<const std::uint8_t> image_resources) {
  BigEndianReader reader(image_resources);
  std::string clipping_path_name;
  struct PendingPath {
    std::uint16_t id{0};
    std::string name;
    std::vector<std::uint8_t> payload;
  };
  std::vector<PendingPath> pending;
  while (reader.remaining() >= 12U) {
    const auto signature = read_signature(reader);
    if (signature != std::array<char, 4>{'8', 'B', 'I', 'M'}) {
      break;
    }
    const auto id = reader.read_u16();
    const auto name = read_pascal_string(reader, 2);
    const auto length = reader.read_u32();
    if (length > reader.remaining()) {
      break;
    }
    auto payload = reader.read_bytes(length);
    if ((length % 2U) != 0U && reader.remaining() > 0U) {
      reader.skip(1);
    }
    if ((id >= kPsdSavedPathResourceFirst && id <= kPsdSavedPathResourceLast) ||
        id == kPsdWorkPathResourceId) {
      pending.push_back(PendingPath{id, name, std::move(payload)});
    } else if (id == kPsdClippingPathNameResourceId && !payload.empty()) {
      const auto name_length = std::min<std::size_t>(payload[0], payload.size() - 1U);
      clipping_path_name.assign(reinterpret_cast<const char*>(payload.data()) + 1, name_length);
    }
  }
  for (auto& entry : pending) {
    auto parsed = parse_path_resource_records(entry.payload, document.width(), document.height());
    if (!parsed.has_value()) {
      continue;  // stays byte-preserved in raw_psd_image_resources
    }
    const bool is_work = entry.id == kPsdWorkPathResourceId;
    DocumentPath path(document.allocate_path_id(), is_work ? std::string("Work Path") : entry.name,
                      is_work ? DocumentPathKind::Work : DocumentPathKind::Saved, std::move(*parsed));
    path.set_resource_source(entry.id,
                             std::make_shared<const std::vector<std::uint8_t>>(std::move(entry.payload)));
    if (!is_work && !clipping_path_name.empty() && entry.name == clipping_path_name) {
      path.set_clipping_path(true);
    }
    // Import plumbing is not a user edit: imported paths start clean so the
    // writer re-emits their original resource bytes (dirty-or-verbatim rule).
    path.reset_dirty();
    document.add_path(std::move(path));
  }
}

}  // namespace patchy::psd
