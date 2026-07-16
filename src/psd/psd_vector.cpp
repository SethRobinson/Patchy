// Vector shape/path codec for the PSD reader: vmsk/vsms path records,
// SoCo/GdFl/PtFl fill content, vstk stroke style, vogk live-shape origination,
// and the saved-path image resources. Every encoding here was pinned by
// observation of Photoshop 27.8 (docs/vector-tools.md, July 2026); parse
// failures leave the layer byte-preserved and locked rather than guessing.

#include "psd/psd_document_io.hpp"
#include "psd/psd_io_internal.hpp"

#include "core/vector_raster.hpp"

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
