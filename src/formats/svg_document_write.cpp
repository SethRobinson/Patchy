#include "formats/svg_document_io.hpp"

#include "core/blend_math.hpp"
#include "core/rect_utils.hpp"
#include "core/vector_shape.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_file_io.hpp"
#include "formats/miniz/miniz.h"
#include "formats/svg_io_internal.hpp"

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

std::string xml_escape(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (const char c : text) {
    switch (c) {
      case '&':
        result += "&amp;";
        break;
      case '<':
        result += "&lt;";
        break;
      case '>':
        result += "&gt;";
        break;
      case '"':
        result += "&quot;";
        break;
      default:
        result.push_back(c);
        break;
    }
  }
  return result;
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

// Groups in first-appearance order (matches sequential-combine rasterization).
std::vector<VectorPath> split_shape_groups(const VectorPath& path) {
  std::vector<VectorPath> groups;
  std::map<std::int32_t, std::size_t> index_of;
  for (const auto& subpath : path.subpaths) {
    const auto found = index_of.find(subpath.shape_group);
    if (found == index_of.end()) {
      index_of.emplace(subpath.shape_group, groups.size());
      groups.emplace_back();
      groups.back().subpaths.push_back(subpath);
    } else {
      groups[found->second].subpaths.push_back(subpath);
    }
  }
  return groups;
}

PathCombineOp group_op(const VectorPath& group) {
  return group.subpaths.empty() ? PathCombineOp::Add : group.subpaths.front().op;
}

bool bounds_disjoint(const VectorPath& a, const VectorPath& b) {
  const auto bounds_a = a.bounds();
  const auto bounds_b = b.bounds();
  if (!bounds_a.has_value() || !bounds_b.has_value()) {
    return true;
  }
  return bounds_a->right <= bounds_b->left || bounds_b->right <= bounds_a->left ||
         bounds_a->bottom <= bounds_b->top || bounds_b->bottom <= bounds_a->top;
}

bool point_inside_group(const VectorPath& group, double x, double y) {
  const auto bounds = group.bounds();
  return bounds.has_value() && x >= bounds->left && x <= bounds->right && y >= bounds->top && y <= bounds->bottom;
}

// How a shape layer's combine structure maps onto SVG.
enum class CombineExport {
  SinglePath,     // one evenodd <path> is exact
  SeparatePaths,  // one <path> per Add group inside a <g> (union of opaque paint)
  Unsupported     // rasterize
};

CombineExport classify_combine(const VectorPath& path) {
  const auto groups = split_shape_groups(path);
  if (groups.size() <= 1) {
    return CombineExport::SinglePath;  // even-odd within one group is our exact rule
  }
  // Adds followed by Subtracts, holes inside their outlines, disjoint
  // outlines: exactly representable as one even-odd path (the letter-O and
  // decomposed-nonzero shapes).
  std::size_t first_subtract = groups.size();
  bool ordered = true;
  for (std::size_t i = 0; i < groups.size(); ++i) {
    const auto op = group_op(groups[i]);
    if (op == PathCombineOp::Intersect || op == PathCombineOp::Xor) {
      return CombineExport::Unsupported;
    }
    if (op == PathCombineOp::Subtract) {
      first_subtract = std::min(first_subtract, i);
    } else if (i > first_subtract) {
      ordered = false;  // Add after a Subtract: sequential semantics we cannot fold
    }
  }
  const auto adds_disjoint = [&groups, first_subtract] {
    for (std::size_t i = 0; i < first_subtract; ++i) {
      for (std::size_t j = i + 1; j < first_subtract; ++j) {
        if (!bounds_disjoint(groups[i], groups[j])) {
          return false;
        }
      }
    }
    return true;
  };
  if (ordered && first_subtract < groups.size()) {
    if (!adds_disjoint()) {
      return CombineExport::Unsupported;
    }
    for (std::size_t i = first_subtract; i < groups.size(); ++i) {
      if (groups[i].subpaths.empty() || groups[i].subpaths.front().anchors.empty()) {
        continue;
      }
      const auto& probe = groups[i].subpaths.front().anchors.front();
      bool inside_an_add = false;
      for (std::size_t j = 0; j < first_subtract && !inside_an_add; ++j) {
        inside_an_add = point_inside_group(groups[j], probe.anchor_x, probe.anchor_y);
      }
      if (!inside_an_add) {
        return CombineExport::Unsupported;
      }
    }
    return CombineExport::SinglePath;
  }
  // All Add: disjoint outlines fold into one even-odd path; overlapping
  // unions need separate sibling paths (exact only for opaque paint - the
  // caller checks).
  if (first_subtract == groups.size()) {
    return adds_disjoint() ? CombineExport::SinglePath : CombineExport::SeparatePaths;
  }
  return CombineExport::Unsupported;
}

bool paint_is_opaque(const VectorFill& fill, const PatternStore& patterns) {
  switch (fill.kind) {
    case VectorFillKind::None:
      return true;  // nothing painted, nothing to double-cover
    case VectorFillKind::Solid:
      return true;  // solid alpha rides the layer opacity, uniform over the union
    case VectorFillKind::Gradient:
      return std::all_of(fill.gradient.alpha_stops.begin(), fill.gradient.alpha_stops.end(),
                         [](const GradientAlphaStop& stop) { return stop.opacity >= 0.9999F; });
    case VectorFillKind::Pattern: {
      const auto* resource = patterns.find(fill.pattern_id);
      if (resource == nullptr || resource->tile.empty()) {
        return false;
      }
      const auto data = resource->tile.data();
      for (std::size_t i = 3; i < data.size(); i += 4) {
        if (data[i] != 255) {
          return false;
        }
      }
      return true;
    }
  }
  return false;
}

bool gradient_type_supported(const VectorFill& fill) {
  if (fill.kind != VectorFillKind::Gradient) {
    return true;
  }
  const auto type = fill.gradient.type;
  return type == LayerStyleGradientType::Linear || type == LayerStyleGradientType::Radial ||
         type == LayerStyleGradientType::Reflected;
}

// Tight alpha bounding box; nullopt for fully transparent pixels.
std::optional<Rect> opaque_bounds(const PixelBuffer& pixels) {
  std::int32_t left = pixels.width();
  std::int32_t top = pixels.height();
  std::int32_t right = -1;
  std::int32_t bottom = -1;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    const auto row = pixels.row(y);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (row[static_cast<std::size_t>(x) * 4U + 3U] != 0) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    }
  }
  if (right < left) {
    return std::nullopt;
  }
  return Rect{left, top, right - left + 1, bottom - top + 1};
}

PixelBuffer crop(const PixelBuffer& pixels, Rect rect) {
  PixelBuffer result(rect.width, rect.height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < rect.height; ++y) {
    const auto source = pixels.row(rect.y + y);
    std::copy_n(source.data() + static_cast<std::size_t>(rect.x) * 4U, static_cast<std::size_t>(rect.width) * 4U,
                result.row(y).data());
  }
  return result;
}

struct Writer {
  const Document& document;
  std::vector<std::string>* notices{};
  std::string defs;
  std::string body;
  int gradient_index{0};
  int pattern_index{0};
  int clip_index{0};
  int mask_index{0};
  std::set<std::string> used_ids;

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
    // Geometry inverts the import mapping: Patchy's calibrated span is the
    // center chord of the FILL's aligned bounds (docs/vector-tools.md "GdFl
    // gradient fill geometry"); an empty path means a full-canvas fill layer.
    const auto path_bounds = path.bounds();
    const double left = path_bounds.has_value() ? path_bounds->left : 0.0;
    const double top = path_bounds.has_value() ? path_bounds->top : 0.0;
    const double width =
        std::max(1.0, path_bounds.has_value() ? path_bounds->right - path_bounds->left : document.width());
    const double height =
        std::max(1.0, path_bounds.has_value() ? path_bounds->bottom - path_bounds->top : document.height());
    const double center_x = left + width * (0.5 + gradient.offset_x_percent / 100.0);
    const double center_y = top + height * (0.5 + gradient.offset_y_percent / 100.0);

    if (gradient.type == LayerStyleGradientType::Radial) {
      const double radius = std::max(width, height) / 2.0 * std::max(0.01F, gradient.scale);
      defs += "<radialGradient id=\"" + id + "\" gradientUnits=\"userSpaceOnUse\" cx=\"" +
              detail::format_number(center_x) + "\" cy=\"" + detail::format_number(center_y) + "\" r=\"" +
              detail::format_number(radius) + "\">";
    } else {
      const double radians = static_cast<double>(gradient.angle_degrees) * std::numbers::pi / 180.0;
      const double direction_x = std::cos(radians);
      const double direction_y = -std::sin(radians);  // screen y grows downward
      const double abs_cos = std::abs(direction_x);
      const double abs_sin = std::abs(direction_y);
      double span = std::min(abs_cos > 1e-9 ? width / abs_cos : std::numeric_limits<double>::infinity(),
                             abs_sin > 1e-9 ? height / abs_sin : std::numeric_limits<double>::infinity());
      span = std::max(1.0, span) * std::max(0.01F, gradient.scale);
      if (gradient.type == LayerStyleGradientType::Reflected) {
        span *= 0.5;  // the import doubles it back: Reflected mirrors the ramp
      }
      defs += "<linearGradient id=\"" + id + "\" gradientUnits=\"userSpaceOnUse\" x1=\"" +
              detail::format_number(center_x - direction_x * span * 0.5) + "\" y1=\"" +
              detail::format_number(center_y - direction_y * span * 0.5) + "\" x2=\"" +
              detail::format_number(center_x + direction_x * span * 0.5) + "\" y2=\"" +
              detail::format_number(center_y + direction_y * span * 0.5) + "\"";
      if (gradient.type == LayerStyleGradientType::Reflected) {
        defs += " spreadMethod=\"reflect\"";
      }
      defs += ">";
    }

    // SVG interpolates stops linearly. Non-identity midpoints, the Classic
    // catmull-rom ease, and noise gradients all need resampling into dense
    // linear stops; plain ramps emit their real stops.
    const bool linear_exact =
        gradient.form == GradientDefinitionForm::Solid &&
        (gradient.interpolation == GradientInterpolationMethod::Linear ||
         (gradient.interpolation == GradientInterpolationMethod::Classic && gradient.smoothness == 0)) &&
        std::all_of(gradient.color_stops.begin(), gradient.color_stops.end(),
                    [](const GradientColorStop& stop) { return std::abs(stop.midpoint - 0.5F) < 0.0001F; }) &&
        std::all_of(gradient.alpha_stops.begin(), gradient.alpha_stops.end(),
                    [](const GradientAlphaStop& stop) { return std::abs(stop.midpoint - 0.5F) < 0.0001F; });
    const auto emit_stop = [this](double offset, RgbColor color, float opacity) {
      defs += "<stop offset=\"" + detail::format_number(offset) + "\" stop-color=\"" + color_hex(color) + "\"";
      if (opacity < 0.9999F) {
        defs += " stop-opacity=\"" + format_opacity(opacity) + "\"";
      }
      defs += "/>";
    };
    if (linear_exact) {
      // Merged ascending union of the color and alpha stop locations, each
      // evaluated exactly; reverse maps locations through 1-x.
      std::vector<float> locations;
      for (const auto& stop : gradient.color_stops) {
        locations.push_back(std::clamp(stop.location, 0.0F, 1.0F));
      }
      for (const auto& stop : gradient.alpha_stops) {
        locations.push_back(std::clamp(stop.location, 0.0F, 1.0F));
      }
      std::sort(locations.begin(), locations.end());
      locations.erase(std::unique(locations.begin(), locations.end(),
                                  [](float a, float b) { return std::abs(a - b) < 0.0001F; }),
                      locations.end());
      if (locations.empty()) {
        locations = {0.0F, 1.0F};
      }
      if (gradient.reverse) {
        std::reverse(locations.begin(), locations.end());  // 1-x below keeps offsets ascending
      }
      for (const auto location : locations) {
        const double offset = gradient.reverse ? 1.0 - location : location;
        emit_stop(offset, gradient_color(gradient, location, true), gradient_stop_opacity(gradient, location, true));
      }
    } else {
      constexpr int kSamples = 64;
      for (int i = 0; i <= kSamples; ++i) {
        const float offset = static_cast<float>(i) / kSamples;
        const float sample_at = gradient.reverse ? 1.0F - offset : offset;
        emit_stop(offset, gradient_color(gradient, sample_at, true), gradient_stop_opacity(gradient, sample_at, true));
      }
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
    if (!layer_is_vector_shape(layer) || !vector_lock_reason(layer).empty()) {
      return false;
    }
    if (!layer.layer_style().empty() || std::abs(layer.fill_opacity() - 1.0F) > 0.0001F) {
      return false;
    }
    const auto& shape = *layer.vector_shape();
    if (shape.path_disabled || shape.path_inverted) {
      return false;
    }
    if (!gradient_type_supported(shape.fill) || !gradient_type_supported(shape.stroke.content)) {
      return false;
    }
    if (shape.fill.kind == VectorFillKind::Gradient && shape.fill.gradient.form == GradientDefinitionForm::Noise &&
        shape.fill.gradient.type != LayerStyleGradientType::Linear &&
        shape.fill.gradient.type != LayerStyleGradientType::Radial &&
        shape.fill.gradient.type != LayerStyleGradientType::Reflected) {
      return false;
    }
    if (shape.stroke.enabled && shape.stroke.blend_mode != BlendMode::Normal) {
      return false;
    }
    const auto combine = classify_combine(shape.path);
    if (combine == CombineExport::Unsupported) {
      return false;
    }
    if (combine == CombineExport::SeparatePaths &&
        (!paint_is_opaque(shape.fill, document.metadata().patterns) ||
         (shape.stroke.enabled && (shape.stroke.opacity < 0.9999 ||
                                   shape.stroke.alignment != VectorStrokeAlignment::Center)))) {
      return false;  // double-painted overlaps would differ from the union
    }
    if (shape.stroke.enabled && shape.stroke.alignment == VectorStrokeAlignment::Outside &&
        (shape.fill.kind == VectorFillKind::None || !paint_is_opaque(shape.fill, document.metadata().patterns) ||
         !shape.stroke.fill_enabled)) {
      return false;  // the under-fill trick needs an opaque fill covering the inner half
    }
    if (shape.stroke.enabled && shape.stroke.alignment == VectorStrokeAlignment::Inside &&
        layer.vector_mask() != nullptr) {
      return false;  // the inside-stroke clip and the vector-mask clip cannot share one element
    }
    if (const auto* mask = layer.vector_mask();
        mask != nullptr && (mask->disabled || mask->density != 255 || mask->feather > 0.0001)) {
      return false;
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
    const auto png = base64(png_bytes(crop(pixels, *bounds)));
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

  bool group_representable(const Layer& group) const {
    if (!group.layer_style().empty() || std::abs(group.fill_opacity() - 1.0F) > 0.0001F) {
      return false;
    }
    if (const auto* mask = group.vector_mask();
        mask != nullptr && (mask->disabled || mask->density != 255 || mask->feather > 0.0001)) {
      return false;
    }
    if (group.mask().has_value() && group.mask()->disabled) {
      return false;
    }
    return true;
  }

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

  struct Unit {
    std::size_t begin{0};
    std::size_t end{0};  // [begin, end): a layer, or a clip base + its clipped run
  };

  static std::vector<Unit> build_units(const std::vector<Layer>& siblings) {
    std::vector<Unit> units;
    std::size_t index = 0;
    while (index < siblings.size()) {
      Unit unit{index, index + 1};
      if (!siblings[index].clipped()) {
        while (unit.end < siblings.size() && siblings[unit.end].clipped()) {
          ++unit.end;
        }
      }
      // An orphaned clipped layer at the bottom stays its own unit (the
      // compositor renders it unclipped defensively).
      units.push_back(unit);
      index = unit.end;
    }
    return units;
  }

  // Does compositing this unit need the pixels below it (something SVG cannot
  // express per-element)? Adjustment layers and unmapped blend modes force
  // merging everything below into one chunk.
  bool unit_is_barrier(const std::vector<Layer>& siblings, const Unit& unit) const {
    const Layer& base = siblings[unit.begin];
    if (base.kind() == LayerKind::Adjustment) {
      return true;
    }
    if (base.blend_mode() != BlendMode::PassThrough && !blend_expressible(base.blend_mode())) {
      return true;
    }
    if (base.kind() == LayerKind::Group && base.blend_mode() == BlendMode::PassThrough) {
      // A pass-through group does not isolate: a barrier inside it reaches
      // this sibling level, and rasterizing a non-representable pass-through
      // group standalone would bake its children's blending against nothing.
      if (!group_representable(base)) {
        return true;
      }
      const auto child_units = build_units(base.children());
      for (const auto& child_unit : child_units) {
        if (unit_is_barrier(base.children(), child_unit)) {
          return true;
        }
      }
    }
    return false;
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
