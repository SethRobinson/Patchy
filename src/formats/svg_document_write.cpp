#include "formats/svg_document_io.hpp"

#include "core/blend_math.hpp"
#include "core/rect_utils.hpp"
#include "core/vector_shape.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_file_io.hpp"
#include "formats/miniz/miniz.h"
#include "formats/svg_io_internal.hpp"
#include "formats/vector_export_plan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// SVG export. Vector shape layers stay vectors (live rects/ellipses/lines as
// native elements, everything else as evenodd paths); layers SVG cannot
// composite correctly - adjustment layers, clipping runs, blend modes with no
// CSS equivalent - merge with everything below them into one flattened
// base64-PNG <image> chunk, so the exported file always LOOKS like the
// document even when structure is lost (each loss gets a notice).
//
// Ordering: Patchy's layers()[0] is the bottom layer and SVG paints first
// element first, so both walks are forward with no reversal.
//
// Determinism: numbers go through detail::format_number (classic-locale
// %.15g, correctly rounded on every mainstream toolchain), container walks
// are index-ordered, and generated ids are sequential, so two writes of one
// document are byte-identical.
namespace patchy::svg {
namespace {

// Opacities arrive as floats; a millionth of precision keeps the file free
// of float-to-double conversion noise ("0.850000023841858").
std::string format_opacity(double value) {
  return detail::format_number(std::round(value * 1e6) / 1e6);
}

std::string base64(std::span<const std::uint8_t> bytes) {
  static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve((bytes.size() + 2U) / 3U * 4U);
  for (std::size_t i = 0; i < bytes.size(); i += 3) {
    const std::uint32_t value = (static_cast<std::uint32_t>(bytes[i]) << 16) |
                                (i + 1 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 1]) << 8 : 0U) |
                                (i + 2 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 2]) : 0U);
    result.push_back(kAlphabet[(value >> 18) & 63U]);
    result.push_back(kAlphabet[(value >> 12) & 63U]);
    result.push_back(i + 1 < bytes.size() ? kAlphabet[(value >> 6) & 63U] : '=');
    result.push_back(i + 2 < bytes.size() ? kAlphabet[value & 63U] : '=');
  }
  return result;
}

std::vector<std::uint8_t> png_bytes(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format() != PixelFormat::rgba8()) {
    throw std::runtime_error("SVG export can only embed RGBA images");
  }
  std::size_t size = 0;
  void* encoded =
      tdefl_write_image_to_png_file_in_memory(pixels.data().data(), pixels.width(), pixels.height(), 4, &size);
  if (encoded == nullptr) {
    throw std::runtime_error("Could not encode an embedded PNG for SVG export");
  }
  std::vector<std::uint8_t> result(static_cast<std::uint8_t*>(encoded), static_cast<std::uint8_t*>(encoded) + size);
  mz_free(encoded);
  return result;
}

std::string color_hex(RgbColor color) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string result = "#......";
  result[1] = kHex[color.red >> 4];
  result[2] = kHex[color.red & 15];
  result[3] = kHex[color.green >> 4];
  result[4] = kHex[color.green & 15];
  result[5] = kHex[color.blue >> 4];
  result[6] = kHex[color.blue & 15];
  return result;
}

bool straight_segment(const PathAnchor& from, const PathAnchor& to) noexcept {
  constexpr double kEpsilon = 1e-10;
  return std::abs(from.out_x - from.anchor_x) < kEpsilon && std::abs(from.out_y - from.anchor_y) < kEpsilon &&
         std::abs(to.in_x - to.anchor_x) < kEpsilon && std::abs(to.in_y - to.anchor_y) < kEpsilon;
}

std::string subpath_data(const PathSubpath& subpath) {
  if (subpath.anchors.empty()) {
    return {};
  }
  std::string result = "M" + detail::format_number(subpath.anchors.front().anchor_x) + " " +
                       detail::format_number(subpath.anchors.front().anchor_y);
  const auto segment = [&result](const PathAnchor& from, const PathAnchor& to) {
    if (straight_segment(from, to)) {
      result += "L" + detail::format_number(to.anchor_x) + " " + detail::format_number(to.anchor_y);
    } else {
      result += "C" + detail::format_number(from.out_x) + " " + detail::format_number(from.out_y) + " " +
                detail::format_number(to.in_x) + " " + detail::format_number(to.in_y) + " " +
                detail::format_number(to.anchor_x) + " " + detail::format_number(to.anchor_y);
    }
  };
  for (std::size_t i = 1; i < subpath.anchors.size(); ++i) {
    segment(subpath.anchors[i - 1], subpath.anchors[i]);
  }
  if (subpath.closed && subpath.anchors.size() > 1) {
    if (!straight_segment(subpath.anchors.back(), subpath.anchors.front())) {
      segment(subpath.anchors.back(), subpath.anchors.front());
    }
    result += "Z";
  }
  return result;
}

std::string path_data(const VectorPath& path) {
  std::string result;
  for (const auto& subpath : path.subpaths) {
    if (!result.empty()) {
      result.push_back(' ');
    }
    result += subpath_data(subpath);
  }
  return result;
}

using vector_export::CombineExport;
using vector_export::classify_combine;
using vector_export::split_shape_groups;
using vector_export::paint_is_opaque;
using vector_export::gradient_type_supported;
using vector_export::opaque_bounds;

struct Writer {
  const Document& document;
  std::vector<std::string>* notices{};
  std::string defs{};
  std::string body{};
  int gradient_index{0};
  int pattern_index{0};
  int clip_index{0};
  int mask_index{0};
  std::set<std::string> used_ids{};

  void notice(std::string value) {
    if (notices != nullptr && std::find(notices->begin(), notices->end(), value) == notices->end()) {
      notices->push_back(std::move(value));
    }
  }

  std::string unique_id(std::string_view name) {
    std::string id;
    id.reserve(name.size());
    for (const char c : name) {
      const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '-' || c == '.';
      id.push_back(safe ? c : '_');
    }
    while (!id.empty() && (id.front() == '-' || id.front() == '.')) {
      id.erase(id.begin());
    }
    if (id.empty() || (id.front() >= '0' && id.front() <= '9')) {
      id.insert(0, "layer-");
    }
    const std::string base = id;
    int suffix = 2;
    while (!used_ids.insert(id).second) {
      id = base + "_" + std::to_string(suffix++);
    }
    return id;
  }

  static void append_indent(std::string& out, int indent) { out.append(static_cast<std::size_t>(indent), ' '); }

  // display/opacity/mix-blend-mode as a style attribute chunk ("" when default).
  std::string layer_style_css(const Layer& layer, bool include_blend = true) const {
    std::string css;
    if (!layer.visible()) {
      css += "display:none;";
    }
    if (std::abs(layer.opacity() - 1.0F) > 0.0001F) {
      css += "opacity:" + format_opacity(layer.opacity()) + ";";
    }
    if (include_blend) {
      if (const auto blend = detail::blend_mode_css(layer.blend_mode());
          !blend.empty() && layer.blend_mode() != BlendMode::Normal) {
        css += "mix-blend-mode:" + std::string(blend) + ";";
      }
    }
    return css;
  }

  // --- paint servers ---

  std::string gradient_paint(const VectorFill& fill, const VectorPath& path) {
    const auto& gradient = fill.gradient;
    const std::string id = "grad" + std::to_string(++gradient_index);
    // Geometry and stops come from the shared export plan (the import mapping
    // inverted: center-chord span, reverse via 1-x, dense resampling for eased
    // or noise ramps).
    const auto geometry = vector_export::gradient_export_geometry(gradient, path, document.width(), document.height());
    if (gradient.type == LayerStyleGradientType::Radial) {
      defs += "<radialGradient id=\"" + id + "\" gradientUnits=\"userSpaceOnUse\" cx=\"" +
              detail::format_number(geometry.center_x) + "\" cy=\"" + detail::format_number(geometry.center_y) +
              "\" r=\"" + detail::format_number(geometry.radius) + "\">";
    } else {
      defs += "<linearGradient id=\"" + id + "\" gradientUnits=\"userSpaceOnUse\" x1=\"" +
              detail::format_number(geometry.x1) + "\" y1=\"" + detail::format_number(geometry.y1) + "\" x2=\"" +
              detail::format_number(geometry.x2) + "\" y2=\"" + detail::format_number(geometry.y2) + "\"";
      if (geometry.reflected) {
        defs += " spreadMethod=\"reflect\"";
      }
      defs += ">";
    }
    for (const auto& stop : vector_export::gradient_export_stops(gradient)) {
      defs += "<stop offset=\"" + detail::format_number(stop.offset) + "\" stop-color=\"" + color_hex(stop.color) + "\"";
      if (stop.opacity < 0.9999F) {
        defs += " stop-opacity=\"" + format_opacity(stop.opacity) + "\"";
      }
      defs += "/>";
    }
    defs += gradient.type == LayerStyleGradientType::Radial ? "</radialGradient>" : "</linearGradient>";
    return "url(#" + id + ")";
  }

  std::string pattern_paint(const VectorFill& fill) {
    const auto* resource = document.metadata().patterns.find(fill.pattern_id);
    if (resource == nullptr || resource->tile.empty() || pattern_tile_is_unrenderable(resource->tile)) {
      notice("A pattern fill's tile was missing and exported as gray");
      return "#808080";
    }
    const std::string id = "pat" + std::to_string(++pattern_index);
    const double scale = std::clamp(fill.pattern_scale, 0.01, 100.0);
    const double cell_width = resource->tile.width() * scale;
    const double cell_height = resource->tile.height() * scale;
    std::string transform;
    if (std::abs(fill.pattern_phase_x) > 1e-9 || std::abs(fill.pattern_phase_y) > 1e-9) {
      transform += "translate(" + detail::format_number(fill.pattern_phase_x) + " " +
                   detail::format_number(fill.pattern_phase_y) + ")";
    }
    if (std::abs(fill.pattern_angle_degrees) > 1e-9) {
      if (!transform.empty()) {
        transform.push_back(' ');
      }
      transform += "rotate(" + detail::format_number(fill.pattern_angle_degrees) + ")";
    }
    if (fill.pattern_linked) {
      // Linked placement anchors at the layer's effects reference point;
      // SVG patterns anchor at the user-space origin.
      notice("A layer-linked pattern fill was exported anchored to the document origin");
    }
    defs += "<pattern id=\"" + id + "\" patternUnits=\"userSpaceOnUse\" width=\"" + detail::format_number(cell_width) +
            "\" height=\"" + detail::format_number(cell_height) + "\"";
    if (!transform.empty()) {
      defs += " patternTransform=\"" + transform + "\"";
    }
    const auto png = base64(png_bytes(resource->tile));
    defs += "><image width=\"" + detail::format_number(cell_width) + "\" height=\"" +
            detail::format_number(cell_height) + "\" href=\"data:image/png;base64," + png +
            "\" xlink:href=\"data:image/png;base64," + png + "\" preserveAspectRatio=\"none\"/></pattern>";
    return "url(#" + id + ")";
  }

  std::string paint(const VectorFill& fill, const VectorPath& path) {
    switch (fill.kind) {
      case VectorFillKind::None:
        return "none";
      case VectorFillKind::Solid:
        return color_hex(fill.color);
      case VectorFillKind::Gradient:
        return gradient_paint(fill, path);
      case VectorFillKind::Pattern:
        return pattern_paint(fill);
    }
    return "none";
  }

  // --- masks and clips ---

  std::string clip_reference(const LayerVectorMask& mask) {
    const std::string id = "clip" + std::to_string(++clip_index);
    defs += "<clipPath id=\"" + id + "\">";
    if (mask.inverted) {
      // Complement: full canvas rect + the path under even-odd.
      defs += "<path fill-rule=\"evenodd\" d=\"M0 0H" + std::to_string(document.width()) + "V" +
              std::to_string(document.height()) + "H0Z " + path_data(mask.path) + "\"/>";
    } else {
      defs += "<path fill-rule=\"evenodd\" d=\"" + path_data(mask.path) + "\"/>";
    }
    defs += "</clipPath>";
    return "clip-path=\"url(#" + id + ")\"";
  }

  std::string mask_reference(const LayerMask& mask) {
    const std::string id = "mask" + std::to_string(++mask_index);
    // Luminance mask: the gray plane becomes an r=g=b image; area outside the
    // mask bounds shows per default_color via a backing rect.
    PixelBuffer rgba(std::max(1, mask.bounds.width), std::max(1, mask.bounds.height), PixelFormat::rgba8());
    for (std::int32_t y = 0; y < rgba.height(); ++y) {
      const auto source = mask.pixels.row(y);
      auto destination = rgba.row(y);
      for (std::int32_t x = 0; x < rgba.width(); ++x) {
        const auto value = source[static_cast<std::size_t>(x)];
        destination[static_cast<std::size_t>(x) * 4U + 0U] = value;
        destination[static_cast<std::size_t>(x) * 4U + 1U] = value;
        destination[static_cast<std::size_t>(x) * 4U + 2U] = value;
        destination[static_cast<std::size_t>(x) * 4U + 3U] = 255;
      }
    }
    defs += "<mask id=\"" + id + "\" maskUnits=\"userSpaceOnUse\" x=\"0\" y=\"0\" width=\"" +
            std::to_string(document.width()) + "\" height=\"" + std::to_string(document.height()) + "\">";
    if (mask.default_color != 0) {
      defs += "<rect x=\"0\" y=\"0\" width=\"" + std::to_string(document.width()) + "\" height=\"" +
              std::to_string(document.height()) + "\" fill=\"" +
              color_hex(RgbColor{mask.default_color, mask.default_color, mask.default_color}) + "\"/>";
    }
    const auto png = base64(png_bytes(rgba));
    defs += "<image x=\"" + std::to_string(mask.bounds.x) + "\" y=\"" + std::to_string(mask.bounds.y) +
            "\" width=\"" + std::to_string(rgba.width()) + "\" height=\"" + std::to_string(rgba.height()) +
            "\" href=\"data:image/png;base64," + png + "\" xlink:href=\"data:image/png;base64," + png + "\"/>";
    defs += "</mask>";
    return "mask=\"url(#" + id + ")\"";
  }

  // --- representability ---

  static bool blend_expressible(BlendMode mode) {
    return mode == BlendMode::Normal || !detail::blend_mode_css(mode).empty();
  }

  bool vector_representable(const Layer& layer) const {
    if (!vector_export::shape_layer_exportable_as_vector(layer, document.metadata().patterns)) {
      return false;
    }
    const auto& shape = *layer.vector_shape();
    if (shape.stroke.enabled && shape.stroke.alignment == VectorStrokeAlignment::Inside &&
        layer.vector_mask() != nullptr) {
      return false;  // the inside-stroke clip and the vector-mask clip cannot share one element
    }
    if (layer.mask().has_value() && layer.mask()->disabled) {
      return false;  // a disabled raster mask must NOT apply; <mask> has no disable
    }
    return true;
  }

  // --- element emission ---

  // The masking attributes shared by every element form. An Inside-aligned
  // stroke claims the element's clip-path slot for its own outline
  // (representability guarantees no vector mask coexists then).
  std::string masking_attributes(const Layer& layer, const VectorShapeContent* shape = nullptr) {
    std::string attributes;
    if (shape != nullptr && shape->stroke.enabled && shape->stroke.alignment == VectorStrokeAlignment::Inside) {
      attributes += inside_stroke_clip(shape->path);
    } else if (const auto* vector_mask = layer.vector_mask()) {
      attributes += " " + clip_reference(*vector_mask);
    }
    if (layer.mask().has_value() && !layer.mask()->disabled && !layer.mask()->pixels.empty()) {
      attributes += " " + mask_reference(*layer.mask());
    }
    return attributes;
  }

  // The clip that makes a double-width stroke render inside-only: the
  // element's own outline as a clipPath.
  std::string inside_stroke_clip(const VectorPath& path) {
    const std::string id = "clip" + std::to_string(++clip_index);
    defs += "<clipPath id=\"" + id + "\"><path fill-rule=\"evenodd\" d=\"" + path_data(path) + "\"/></clipPath>";
    return " clip-path=\"url(#" + id + ")\"";
  }

  std::string stroke_attributes(const VectorStroke& stroke, const VectorPath& path) {
    if (!stroke.enabled) {
      return {};
    }
    // Inside/outside alignments render at double width (clipped/under-filled
    // back to one half); data-patchy-* hints let Patchy re-import the true
    // geometry while other renderers see plain attributes.
    const bool doubled = stroke.alignment != VectorStrokeAlignment::Center;
    std::string attributes = " stroke=\"" + paint(stroke.content, path) + "\" stroke-width=\"" +
                             detail::format_number(stroke.width * (doubled ? 2.0 : 1.0)) + "\"";
    if (doubled) {
      attributes += " data-patchy-stroke-align=\"";
      attributes += stroke.alignment == VectorStrokeAlignment::Inside ? "inside" : "outside";
      attributes += "\" data-patchy-stroke-width=\"" + detail::format_number(stroke.width) + "\"";
      if (stroke.alignment == VectorStrokeAlignment::Outside) {
        attributes += " paint-order=\"stroke\"";  // stroke under the fill = outside half only
      }
    }
    attributes += " stroke-linecap=\"";
    attributes += stroke.cap == VectorStrokeCap::Round ? "round" : stroke.cap == VectorStrokeCap::Square ? "square" : "butt";
    attributes += "\" stroke-linejoin=\"";
    attributes += stroke.join == VectorStrokeJoin::Round ? "round" : stroke.join == VectorStrokeJoin::Bevel ? "bevel" : "miter";
    attributes += "\"";
    if (std::abs(stroke.miter_limit - 4.0) > 0.0001) {
      attributes += " stroke-miterlimit=\"" + detail::format_number(stroke.miter_limit) + "\"";
    }
    if (stroke.opacity < 0.9999) {
      attributes += " stroke-opacity=\"" + format_opacity(stroke.opacity) + "\"";
    }
    if (!stroke.dashes.empty()) {
      attributes += " stroke-dasharray=\"";
      for (std::size_t i = 0; i < stroke.dashes.size(); ++i) {
        if (i != 0) {
          attributes.push_back(' ');
        }
        attributes += detail::format_number(stroke.dashes[i] * stroke.width);  // width multiples -> user units
      }
      attributes += "\"";
      if (std::abs(stroke.dash_offset) > 1e-9) {
        attributes += " stroke-dashoffset=\"" + detail::format_number(stroke.dash_offset * stroke.width) + "\"";
      }
    }
    return attributes;
  }

  // One live origination covering every subpath -> a native SVG element.
  // Returns false when the shape needs the generic path form.
  bool emit_live_shape(const Layer& layer, const VectorShapeContent& shape, int indent) {
    if (shape.origination.size() != 1) {
      return false;
    }
    const auto& live = shape.origination.front();
    const bool covers = std::all_of(shape.path.subpaths.begin(), shape.path.subpaths.end(),
                                    [&](const PathSubpath& subpath) { return subpath.shape_group == live.index; });
    if (!covers) {
      return false;
    }
    const auto fill_attribute = [&]() {
      return " fill=\"" + (shape.stroke.enabled && !shape.stroke.fill_enabled ? std::string("none")
                                                                              : paint(shape.fill, shape.path)) +
             "\"";
    };
    std::string element;
    switch (live.kind) {
      case LiveShapeKind::Rectangle:
      case LiveShapeKind::RoundedRectangle: {
        const auto& radii = live.corner_radii;
        const bool uniform = std::abs(radii[0] - radii[1]) < 0.0001 && std::abs(radii[0] - radii[2]) < 0.0001 &&
                             std::abs(radii[0] - radii[3]) < 0.0001;
        if (live.kind == LiveShapeKind::RoundedRectangle && !uniform) {
          return false;  // per-corner radii have no <rect> form
        }
        element = "<rect x=\"" + detail::format_number(live.left) + "\" y=\"" + detail::format_number(live.top) +
                  "\" width=\"" + detail::format_number(live.right - live.left) + "\" height=\"" +
                  detail::format_number(live.bottom - live.top) + "\"";
        if (live.kind == LiveShapeKind::RoundedRectangle && radii[0] > 0.0001) {
          element += " rx=\"" + detail::format_number(radii[0]) + "\"";
        }
        break;
      }
      case LiveShapeKind::Ellipse: {
        const double rx = (live.right - live.left) / 2.0;
        const double ry = (live.bottom - live.top) / 2.0;
        element = "<ellipse cx=\"" + detail::format_number(live.left + rx) + "\" cy=\"" +
                  detail::format_number(live.top + ry) + "\" rx=\"" + detail::format_number(rx) + "\" ry=\"" +
                  detail::format_number(ry) + "\"";
        break;
      }
      case LiveShapeKind::Line: {
        // The live Line is a filled quad; <line> reproduces it exactly as a
        // butt-capped stroke in the fill paint (the import's inverse).
        if (live.arrow_start || live.arrow_end || shape.stroke.enabled) {
          return false;
        }
        element = "<line x1=\"" + detail::format_number(live.line_start_x) + "\" y1=\"" +
                  detail::format_number(live.line_start_y) + "\" x2=\"" + detail::format_number(live.line_end_x) +
                  "\" y2=\"" + detail::format_number(live.line_end_y) + "\" stroke=\"" +
                  paint(shape.fill, shape.path) + "\" stroke-width=\"" + detail::format_number(live.line_weight) +
                  "\" stroke-linecap=\"butt\" fill=\"none\"";
        break;
      }
      default:
        return false;
    }
    append_indent(body, indent);
    body += element + " id=\"" + unique_id(layer.name()) + "\"";
    if (live.kind != LiveShapeKind::Line) {
      body += fill_attribute();
      body += stroke_attributes(shape.stroke, shape.path);
    }
    body += masking_attributes(layer, &shape);
    if (const auto css = layer_style_css(layer); !css.empty()) {
      body += " style=\"" + css + "\"";
    }
    body += "/>\n";
    return true;
  }

  void emit_vector_layer(const Layer& layer, int indent) {
    const auto& shape = *layer.vector_shape();
    if (shape.path.empty()) {
      // Fill layer: the empty path covers the whole canvas.
      append_indent(body, indent);
      body += "<rect x=\"0\" y=\"0\" width=\"" + std::to_string(document.width()) + "\" height=\"" +
              std::to_string(document.height()) + "\" id=\"" + unique_id(layer.name()) + "\" fill=\"" +
              paint(shape.fill, shape.path) + "\"" + stroke_attributes(shape.stroke, shape.path) +
              masking_attributes(layer);
      if (const auto css = layer_style_css(layer); !css.empty()) {
        body += " style=\"" + css + "\"";
      }
      body += "/>\n";
      return;
    }
    if (emit_live_shape(layer, shape, indent)) {
      return;
    }
    const std::string fill_value =
        shape.stroke.enabled && !shape.stroke.fill_enabled ? "none" : paint(shape.fill, shape.path);
    const auto combine = classify_combine(shape.path);
    if (combine == CombineExport::SinglePath) {
      append_indent(body, indent);
      body += "<path d=\"" + path_data(shape.path) + "\" fill-rule=\"evenodd\" id=\"" + unique_id(layer.name()) +
              "\" fill=\"" + fill_value + "\"" + stroke_attributes(shape.stroke, shape.path) +
              masking_attributes(layer, &shape);
      if (const auto css = layer_style_css(layer); !css.empty()) {
        body += " style=\"" + css + "\"";
      }
      body += "/>\n";
      return;
    }
    // Overlapping Add groups: sibling paths under one <g> (this re-imports as
    // a folder of shapes; the union rendering is identical). Paint servers
    // resolve once so a gradient stroke does not mint one def per group.
    const std::string stroke_value = stroke_attributes(shape.stroke, shape.path);
    append_indent(body, indent);
    body += "<g id=\"" + unique_id(layer.name()) + "\"" + masking_attributes(layer);
    if (const auto css = layer_style_css(layer); !css.empty()) {
      body += " style=\"" + css + "\"";
    }
    body += ">\n";
    for (const auto& group : split_shape_groups(shape.path)) {
      append_indent(body, indent + 2);
      body += "<path d=\"" + path_data(group) + "\" fill-rule=\"evenodd\" fill=\"" + fill_value + "\"" + stroke_value +
              "/>\n";
    }
    append_indent(body, indent);
    body += "</g>\n";
  }

  // Flattens `layers` (bottom..top slice of one sibling list) through the real
  // compositor into one cropped <image>. `css` carries display/opacity/blend
  // when the chunk stands in for a single unit.
  void emit_raster_chunk(std::vector<Layer> layers, std::string_view id_name, const std::string& css, int indent) {
    Document scratch(document.width(), document.height(), PixelFormat::rgba8());
    scratch.metadata().patterns = document.metadata().patterns;
    for (auto& layer : layers) {
      scratch.add_layer(std::move(layer));
    }
    const auto pixels = flatten_document_rgba8(scratch);
    const auto bounds = opaque_bounds(pixels);
    if (!bounds.has_value()) {
      return;  // nothing visible, nothing to embed
    }
    const auto png = base64(png_bytes(vector_export::crop_pixels(pixels, *bounds)));
    append_indent(body, indent);
    body += "<image id=\"" + unique_id(id_name) + "\" x=\"" + std::to_string(bounds->x) + "\" y=\"" +
            std::to_string(bounds->y) + "\" width=\"" + std::to_string(bounds->width) + "\" height=\"" +
            std::to_string(bounds->height) + "\" href=\"data:image/png;base64," + png +
            "\" xlink:href=\"data:image/png;base64," + png + "\"";
    if (!css.empty()) {
      body += " style=\"" + css + "\"";
    }
    body += "/>\n";
  }

  // One layer (or clip run) rasterized on its own: bake with Normal/full
  // opacity, then reapply opacity/blend/display as CSS so it still composites
  // correctly against what's below.
  void emit_raster_unit(std::span<const Layer> run, int indent) {
    const Layer& base = run.front();
    std::vector<Layer> copies;
    copies.reserve(run.size());
    for (const auto& member : run) {
      copies.push_back(member);
    }
    copies.front().set_visible(true);
    copies.front().set_opacity(1.0F);
    copies.front().set_blend_mode(BlendMode::Normal);
    if (run.size() > 1) {
      notice("Clipping-mask group '" + base.name() + "' was rasterized for SVG export");
    } else if (base.kind() == LayerKind::Group) {
      notice("Group '" + base.name() + "' was rasterized for SVG export");
    } else if (layer_is_vector_shape(base)) {
      notice("Shape layer '" + base.name() + "' uses features SVG cannot express and was rasterized");
    } else if (base.kind() == LayerKind::Text) {
      notice("Text layer '" + base.name() + "' was rasterized for SVG export");
    }
    emit_raster_chunk(std::move(copies), base.name(), layer_style_css(base), indent);
  }

  // --- group export ---

  static bool group_representable(const Layer& group) { return vector_export::group_exportable(group); }

  void emit_group(const Layer& group, int indent) {
    if (!group_representable(group)) {
      emit_raster_unit(std::span<const Layer>(&group, 1), indent);
      return;
    }
    append_indent(body, indent);
    body += "<g id=\"" + unique_id(group.name()) + "\"" + masking_attributes(group);
    std::string css = layer_style_css(group);
    if (group.blend_mode() != BlendMode::PassThrough) {
      // A Photoshop group isolates its children's blending; a plain <g> does
      // not, so non-pass-through groups isolate explicitly.
      css += "isolation:isolate;";
    } else if (std::abs(group.opacity() - 1.0F) > 0.0001F) {
      notice("Pass-through group opacity is approximated (SVG group opacity isolates the group)");
    }
    if (!css.empty()) {
      body += " style=\"" + css + "\"";
    }
    body += ">\n";
    emit_siblings(group.children(), indent + 2);
    append_indent(body, indent);
    body += "</g>\n";
  }

  // --- the sibling walk with barrier chunking ---

  using Unit = vector_export::Unit;

  static std::vector<Unit> build_units(const std::vector<Layer>& siblings) {
    return vector_export::build_units(siblings);
  }

  // Adjustment layers and unmapped blend modes force merging everything below
  // into one chunk (the shared plan; SVG's expressible set is the CSS map).
  static bool unit_is_barrier(const std::vector<Layer>& siblings, const Unit& unit) {
    return vector_export::unit_is_barrier(siblings, unit, blend_expressible);
  }

  void emit_siblings(const std::vector<Layer>& siblings, int indent) {
    const auto units = build_units(siblings);
    std::size_t resume_at = 0;
    std::size_t barrier_end = 0;
    std::vector<std::string> merged_names;
    for (std::size_t i = 0; i < units.size(); ++i) {
      if (unit_is_barrier(siblings, units[i])) {
        barrier_end = i + 1;
      }
    }
    if (barrier_end > 0) {
      std::vector<Layer> chunk(siblings.begin(),
                               siblings.begin() + static_cast<std::ptrdiff_t>(units[barrier_end - 1].end));
      for (const auto& layer : chunk) {
        merged_names.push_back(layer.name());
      }
      std::string names;
      for (std::size_t i = 0; i < merged_names.size(); ++i) {
        if (i != 0) {
          names += ", ";
        }
        names += merged_names[i];
      }
      notice("Merged into one flattened image for SVG export (adjustment layers or unsupported blend modes): " +
             names);
      emit_raster_chunk(std::move(chunk), "Merged", std::string(), indent);
      resume_at = barrier_end;
    }
    for (std::size_t i = resume_at; i < units.size(); ++i) {
      const auto& unit = units[i];
      const Layer& base = siblings[unit.begin];
      if (unit.end - unit.begin > 1) {
        emit_raster_unit(std::span<const Layer>(siblings.data() + unit.begin, unit.end - unit.begin), indent);
        continue;
      }
      if (base.kind() == LayerKind::Group) {
        emit_group(base, indent);
        continue;
      }
      if (vector_representable(base)) {
        emit_vector_layer(base, indent);
        continue;
      }
      emit_raster_unit(std::span<const Layer>(&base, 1), indent);
    }
  }

  std::vector<std::uint8_t> run() {
    emit_siblings(document.layers(), 2);
    std::string output;
    output += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    output += "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
              "version=\"1.1\" width=\"" +
              std::to_string(document.width()) + "\" height=\"" + std::to_string(document.height()) +
              "\" viewBox=\"0 0 " + std::to_string(document.width()) + " " + std::to_string(document.height()) +
              "\">\n";
    if (!defs.empty()) {
      output += "  <defs>" + defs + "</defs>\n";
    }
    output += body;
    output += "</svg>\n";
    return {output.begin(), output.end()};
  }
};

}  // namespace

std::vector<std::uint8_t> DocumentIo::write(const Document& document, std::vector<std::string>* notices) {
  if (document.width() <= 0 || document.height() <= 0) {
    throw std::runtime_error("Cannot export an empty document as SVG");
  }
  Writer writer{document, notices};
  return writer.run();
}

void DocumentIo::write_file(const Document& document, const std::filesystem::path& path,
                            std::vector<std::string>* notices) {
  formats::write_file_bytes(path, write(document, notices), "SVG");
}

}  // namespace patchy::svg
