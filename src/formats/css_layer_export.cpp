#include "formats/css_layer_export.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/vector_shape.hpp"
#include "formats/svg_io_internal.hpp"
#include "formats/vector_export_plan.hpp"
#include "psd/psd_text_runs.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace patchy::css {
namespace {

// A rectangle in document pixels. Shapes keep their fractional edges.
struct Box {
  double left{0.0};
  double top{0.0};
  double width{0.0};
  double height{0.0};

  [[nodiscard]] bool empty() const noexcept { return !(width > 0.0 && height > 0.0); }
};

Box box_from_rect(Rect rect) {
  return Box{static_cast<double>(rect.x), static_cast<double>(rect.y), static_cast<double>(rect.width),
             static_cast<double>(rect.height)};
}

Box unite_boxes(const Box& a, const Box& b) {
  if (a.empty()) {
    return b;
  }
  if (b.empty()) {
    return a;
  }
  const double left = std::min(a.left, b.left);
  const double top = std::min(a.top, b.top);
  const double right = std::max(a.left + a.width, b.left + b.width);
  const double bottom = std::max(a.top + a.height, b.top + b.height);
  return Box{left, top, right - left, bottom - top};
}

Box outset_box(const Box& box, double amount) {
  return Box{box.left - amount, box.top - amount, box.width + amount * 2.0, box.height + amount * 2.0};
}

// A number with at most `decimals` decimals and no trailing zeros, built from integers so
// no locale or printf rounding mode can change it.
std::string number(double value, int decimals = 2) {
  if (!std::isfinite(value)) {
    value = 0.0;
  }
  std::int64_t scale = 1;
  for (int i = 0; i < decimals; ++i) {
    scale *= 10;
  }
  const auto scaled = std::llround(value * static_cast<double>(scale));
  const auto magnitude = static_cast<std::uint64_t>(scaled < 0 ? -scaled : scaled);
  std::string text = scaled < 0 ? "-" : "";
  text += std::to_string(magnitude / static_cast<std::uint64_t>(scale));
  auto fraction = decimals > 0 ? std::to_string(magnitude % static_cast<std::uint64_t>(scale)) : std::string();
  if (fraction.size() < static_cast<std::size_t>(std::max(0, decimals))) {
    fraction.insert(0, static_cast<std::size_t>(decimals) - fraction.size(), '0');
  }
  while (!fraction.empty() && fraction.back() == '0') {
    fraction.pop_back();
  }
  if (!fraction.empty()) {
    text += "." + fraction;
  }
  return text;
}

std::string px(double value) {
  return number(value) + "px";
}

std::string color(RgbColor rgb, double alpha) {
  const auto alpha_hundredths = std::llround(std::clamp(alpha, 0.0, 1.0) * 100.0);
  if (alpha_hundredths >= 100) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string text = "#";
    for (const auto channel : {rgb.red, rgb.green, rgb.blue}) {
      text.push_back(kHex[channel >> 4U]);
      text.push_back(kHex[channel & 0x0FU]);
    }
    return text;
  }
  return "rgba(" + std::to_string(rgb.red) + ", " + std::to_string(rgb.green) + ", " + std::to_string(rgb.blue) +
         ", " + number(static_cast<double>(alpha_hundredths) / 100.0) + ")";
}

using Declarations = std::vector<std::pair<std::string, std::string>>;

std::optional<std::string_view> metadata_value(const Layer& layer, const char* key) {
  const auto found = layer.metadata().find(key);
  if (found == layer.metadata().end()) {
    return std::nullopt;
  }
  return std::string_view(found->second);
}

bool style_renders(const Layer& layer) {
  return layer.layer_style().effects_visible && !layer.layer_style().empty();
}

// --- shapes ---

// The one live rectangle, rounded rectangle or ellipse the whole outline is, when the
// outline still fills that box (a rotated one does not, and has no CSS box).
const LiveShapeParams* covering_live_box(const VectorShapeContent& shape) {
  if (shape.origination.size() != 1 || shape.path.empty() || shape.path_inverted || shape.path_disabled) {
    return nullptr;
  }
  const auto& live = shape.origination.front();
  if (live.kind != LiveShapeKind::Rectangle && live.kind != LiveShapeKind::RoundedRectangle &&
      live.kind != LiveShapeKind::Ellipse) {
    return nullptr;
  }
  if (!std::all_of(shape.path.subpaths.begin(), shape.path.subpaths.end(),
                   [&live](const PathSubpath& subpath) { return subpath.shape_group == live.index; })) {
    return nullptr;
  }
  const auto bounds = shape.path.bounds();
  constexpr double kTolerance = 0.51;
  if (!bounds.has_value() || !(live.right > live.left) || !(live.bottom > live.top) ||
      std::abs(bounds->left - live.left) > kTolerance || std::abs(bounds->top - live.top) > kTolerance ||
      std::abs(bounds->right - live.right) > kTolerance || std::abs(bounds->bottom - live.bottom) > kTolerance) {
    return nullptr;
  }
  return &live;
}

// What CSS can say about a shape or fill layer: its border box, where the background is
// positioned (the padding box), and the paint it may name.
struct ShapeBox {
  Box border_box;
  Box padding_box;
  const LiveShapeParams* live{nullptr};  // the live box the outline is, if any
  bool whole_canvas{false};              // a fill layer
  bool single_paint{false};              // one fill and stroke (not a merged multi-paint layer)
  double border_width{0.0};              // > 0 when the stroke becomes a CSS border
  double outset{0.0};                    // how far the border box lies outside the outline
};

bool stroke_is_css_border(const VectorShapeContent& shape) {
  const auto& stroke = shape.stroke;
  return shape.parts.empty() && stroke.enabled && stroke.width > 0.0 && stroke.opacity > 0.0 &&
         stroke.content.kind == VectorFillKind::Solid;
}

Box pixel_box(const Layer& layer) {
  const auto bounds = layer_visible_alpha_bounds(layer, layer.bounds());
  return bounds.has_value() ? box_from_rect(*bounds) : Box{};
}

ShapeBox shape_box(const Document& document, const Layer& layer, const VectorShapeContent& shape) {
  ShapeBox result;
  result.single_paint = shape.parts.empty();
  if ((shape.path.empty() || shape.path_disabled) && !shape.path_inverted) {
    // A fill layer covers the canvas; its stroke draws nothing.
    result.whole_canvas = true;
    result.border_box = Box{0.0, 0.0, static_cast<double>(document.width()), static_cast<double>(document.height())};
    result.padding_box = result.border_box;
    return result;
  }
  const bool border = stroke_is_css_border(shape);
  if (const auto* live = covering_live_box(shape); live != nullptr) {
    result.live = live;
    const Box outline{live->left, live->top, live->right - live->left, live->bottom - live->top};
    if (border) {
      result.outset = shape.stroke.alignment == VectorStrokeAlignment::Inside
                          ? 0.0
                          : shape.stroke.alignment == VectorStrokeAlignment::Center ? shape.stroke.width / 2.0
                                                                                   : shape.stroke.width;
    }
    result.border_box = outset_box(outline, result.outset);
  } else {
    // Any other outline: the box of what it draws (stroke included), else of its path.
    result.border_box = pixel_box(layer);
    if (result.border_box.empty()) {
      if (const auto bounds = shape.path.bounds(); bounds.has_value()) {
        result.border_box = Box{bounds->left, bounds->top, bounds->right - bounds->left, bounds->bottom - bounds->top};
      }
    }
  }
  result.padding_box = result.border_box;
  if (border && !result.border_box.empty()) {
    result.border_width =
        std::min(shape.stroke.width, std::min(result.border_box.width, result.border_box.height) / 2.0);
    result.padding_box = outset_box(result.border_box, -result.border_width);
  }
  return result;
}

// The gradient's own stops, joined by straight ramps as CSS draws them (the Classic
// easing and moved midpoints are left out rather than resampled into dozens of stops).
std::vector<vector_export::GradientExportStop> plain_gradient_stops(const LayerStyleGradient& gradient) {
  auto plain = gradient;
  plain.smoothness = 0;
  plain.interpolation = GradientInterpolationMethod::Classic;
  for (auto& stop : plain.color_stops) {
    stop.midpoint = 0.5F;
  }
  for (auto& stop : plain.alpha_stops) {
    stop.midpoint = 0.5F;
  }
  return vector_export::gradient_export_stops(plain);
}

std::string gradient_stop_list(const std::vector<vector_export::GradientExportStop>& stops,
                               const std::vector<double>& percents, double alpha) {
  std::string text;
  for (std::size_t i = 0; i < stops.size(); ++i) {
    if (i != 0) {
      text += ", ";
    }
    text += color(stops[i].color, static_cast<double>(stops[i].opacity) * alpha) + " " + number(percents[i]) + "%";
  }
  return text;
}

// linear-gradient() or radial-gradient() for a gradient fill, positioned in `padding_box`;
// empty for the kinds CSS has no plain form for (noise, Angle, Reflected, Diamond).
std::string gradient_css(const Document& document, const VectorShapeContent& shape, const Box& padding_box,
                         double alpha) {
  const auto& gradient = shape.fill.gradient;
  if (gradient.form != GradientDefinitionForm::Solid || padding_box.empty() ||
      (gradient.type != LayerStyleGradientType::Linear && gradient.type != LayerStyleGradientType::Radial)) {
    return {};
  }
  const auto stops = plain_gradient_stops(gradient);
  if (stops.empty()) {
    return {};
  }
  const auto geometry = vector_export::gradient_export_geometry(
      gradient, shape.path_disabled ? VectorPath{} : shape.path, document.width(), document.height());
  std::vector<double> percents;
  percents.reserve(stops.size());
  if (gradient.type == LayerStyleGradientType::Radial) {
    for (const auto& stop : stops) {
      percents.push_back(stop.offset * 100.0);
    }
    return "radial-gradient(circle " + px(geometry.radius) + " at " + px(geometry.center_x - padding_box.left) +
           " " + px(geometry.center_y - padding_box.top) + ", " + gradient_stop_list(stops, percents, alpha) + ")";
  }
  const double dx = geometry.x2 - geometry.x1;
  const double dy = geometry.y2 - geometry.y1;
  const double length = std::hypot(dx, dy);
  if (length < 1e-9) {
    return {};
  }
  const double ux = dx / length;
  const double uy = dy / length;
  // CSS measures the angle clockwise from "up", and its gradient line runs through the
  // box's center from corner to corner; Patchy's ramp is the chord through the center.
  // The stops are placed on the CSS line where Patchy's ramp puts them.
  const double css_length = std::abs(padding_box.width * ux) + std::abs(padding_box.height * uy);
  if (css_length < 1e-9) {
    return {};
  }
  const double center_x = padding_box.left + padding_box.width / 2.0;
  const double center_y = padding_box.top + padding_box.height / 2.0;
  for (const auto& stop : stops) {
    const double x = geometry.x1 + dx * stop.offset;
    const double y = geometry.y1 + dy * stop.offset;
    percents.push_back((0.5 + ((x - center_x) * ux + (y - center_y) * uy) / css_length) * 100.0);
  }
  double degrees = std::atan2(ux, -uy) * 180.0 / std::numbers::pi;
  if (degrees < 0.0) {
    degrees += 360.0;
  }
  if (std::llround(degrees * 100.0) >= 36000) {
    degrees = 0.0;
  }
  return "linear-gradient(" + number(degrees) + "deg, " + gradient_stop_list(stops, percents, alpha) + ")";
}

void append_shape_paint(Declarations& declarations, const Document& document, const Layer& layer,
                        const VectorShapeContent& shape, const ShapeBox& box) {
  if (!box.single_paint) {
    return;
  }
  const double fill_alpha = static_cast<double>(layer.fill_opacity());
  const bool border = box.border_width > 0.0;
  if (border) {
    declarations.emplace_back("box-sizing", "border-box");
  }
  if (!(shape.stroke.enabled && !shape.stroke.fill_enabled)) {
    if (shape.fill.kind == VectorFillKind::Solid) {
      declarations.emplace_back("background-color", color(shape.fill.color, fill_alpha));
    } else if (shape.fill.kind == VectorFillKind::Gradient) {
      if (auto gradient = gradient_css(document, shape, box.padding_box, fill_alpha); !gradient.empty()) {
        declarations.emplace_back("background-image", std::move(gradient));
      }
    }
  }
  if (border) {
    const auto& stroke = shape.stroke;
    const auto preset = stroke_dash_preset_for(stroke.dashes, stroke.cap);
    const char* style = stroke.dashes.empty() ? "solid"
                                              : preset.has_value() && stroke_dash_preset_is_dots(*preset) ? "dotted"
                                                                                                          : "dashed";
    declarations.emplace_back("border", px(box.border_width) + " " + style + " " +
                                            color(stroke.content.color, stroke.opacity * fill_alpha));
  }
  if (box.live == nullptr) {
    return;
  }
  if (box.live->kind == LiveShapeKind::Ellipse) {
    declarations.emplace_back("border-radius", "50%");
  } else if (box.live->kind == LiveShapeKind::RoundedRectangle) {
    // Model order TL, TR, BR, BL is CSS's order. A CSS radius is the border's outer
    // edge, so a stroke outside the outline widens it.
    std::array<double, 4> radii{};
    bool any = false;
    bool uniform = true;
    for (std::size_t i = 0; i < radii.size(); ++i) {
      const double radius = std::max(0.0, box.live->corner_radii[i]);
      radii[i] = radius > 0.0001 ? radius + box.outset : 0.0;
      any = any || radii[i] > 0.0;
      uniform = uniform && std::llround(radii[i] * 100.0) == std::llround(radii[0] * 100.0);
    }
    if (any) {
      declarations.emplace_back("border-radius", uniform ? px(radii[0])
                                                         : px(radii[0]) + " " + px(radii[1]) + " " + px(radii[2]) +
                                                               " " + px(radii[3]));
    }
  }
}

// --- text ---

std::string compact_lower(std::string_view text) {
  std::string result;
  for (const char ch : text) {
    if (ch >= 'A' && ch <= 'Z') {
      result.push_back(static_cast<char>(ch - 'A' + 'a'));
    } else if (ch != ' ' && ch != '-' && ch != '_') {
      result.push_back(ch);
    }
  }
  return result;
}

// The CSS weight a face name states ("Demi", "Extra Bold", "Light Italic"); the bold flag
// decides for a name that states none.
int css_font_weight(const psd::PsdTextStyleRun& run) {
  const auto style = compact_lower(run.style);
  const auto has = [&style](std::string_view word) { return style.find(word) != std::string::npos; };
  int weight = run.bold ? 700 : 400;
  if (has("thin") || has("hairline")) {
    weight = 100;
  } else if (has("extralight") || has("ultralight")) {
    weight = 200;
  } else if (has("semilight") || has("demilight")) {
    weight = 350;
  } else if (has("light")) {
    weight = 300;
  } else if (has("medium")) {
    weight = 500;
  } else if (has("semibold") || has("demi")) {
    weight = 600;
  } else if (has("extrabold") || has("ultrabold")) {
    weight = 800;
  } else if (has("black") || has("heavy")) {
    weight = 900;
  } else if (has("bold")) {
    weight = 700;
  }
  return run.faux_bold ? std::max(weight, 700) : weight;
}

std::string css_string(std::string_view text) {
  std::string result = "\"";
  for (const char ch : text) {
    if (ch == '"' || ch == '\\') {
      result.push_back('\\');
    }
    result.push_back(static_cast<unsigned char>(ch) < 0x20U ? ' ' : ch);
  }
  result.push_back('"');
  return result;
}

// How much the layer's transform scales the type's height (Free Transform on a text layer
// keeps the font size and stores the scale).
double text_scale(const Layer& layer) {
  for (const char* key : {kLayerMetadataTextTransform, kLayerMetadataPsdTextTransform}) {
    if (const auto value = metadata_value(layer, key); value.has_value()) {
      if (const auto transform = parse_layer_affine_transform(*value); transform.has_value()) {
        const double scale = std::hypot((*transform)[2], (*transform)[3]);
        return scale > 0.0001 && std::isfinite(scale) ? scale : 1.0;
      }
    }
  }
  return 1.0;
}

void append_text(Declarations& declarations, const Layer& layer) {
  const auto runs = psd::text_style_runs_from_layer(layer);
  if (runs.empty()) {
    return;
  }
  const auto& run = runs.front();
  const auto paragraphs = psd::text_paragraph_runs_from_layer(layer);
  const auto paragraph = paragraphs.empty() ? psd::PsdTextParagraphRun{} : paragraphs.front();
  const double scale = text_scale(layer);
  const bool photoshop_layout =
      metadata_value(layer, kLayerMetadataTextLayoutMode).value_or(std::string_view{}) == kTextLayoutModePhotoshop;

  declarations.emplace_back("font-family", css_string(run.family));
  declarations.emplace_back("font-size", px(run.size * scale));
  const int weight = css_font_weight(run);
  declarations.emplace_back("font-weight", weight == 400 ? "normal" : weight == 700 ? "bold" : std::to_string(weight));
  const auto style = compact_lower(run.style);
  if (run.italic || run.faux_italic || style.find("italic") != std::string::npos ||
      style.find("oblique") != std::string::npos) {
    declarations.emplace_back("font-style", "italic");
  }
  declarations.emplace_back("color", color(run.color, static_cast<double>(layer.fill_opacity())));
  if (run.leading.has_value() && !run.auto_leading) {
    declarations.emplace_back("line-height", px(*run.leading * scale));
  } else if (photoshop_layout) {
    // Photoshop's auto leading is a fraction of the size, which is what a unitless
    // line-height is.
    declarations.emplace_back("line-height", number(paragraph.auto_leading_fraction));
  } else {
    // Patchy's own text uses the font's natural line spacing.
    declarations.emplace_back("line-height", "normal");
  }
  if (std::abs(run.tracking) >= 0.5) {
    declarations.emplace_back("letter-spacing", number(run.tracking / 1000.0, 3) + "em");
  }
  switch (paragraph.justification) {
    case 1: declarations.emplace_back("text-align", "right"); break;
    case 2: declarations.emplace_back("text-align", "center"); break;
    case 3: declarations.emplace_back("text-align", "justify"); break;
    case 4:
      declarations.emplace_back("text-align", "justify");
      declarations.emplace_back("text-align-last", "right");
      break;
    case 5:
      declarations.emplace_back("text-align", "justify");
      declarations.emplace_back("text-align-last", "center");
      break;
    case 6:
      declarations.emplace_back("text-align", "justify");
      declarations.emplace_back("text-align-last", "justify");
      break;
    default: declarations.emplace_back("text-align", "left"); break;
  }
  if (paragraph.direction == "rtl") {
    declarations.emplace_back("direction", "rtl");
  }
  if (run.font_caps == 2) {
    declarations.emplace_back("text-transform", "uppercase");
  } else if (run.font_caps == 1) {
    declarations.emplace_back("font-variant", "small-caps");
  }
  if (run.underline || run.strikethrough) {
    declarations.emplace_back("text-decoration", run.underline && run.strikethrough ? "underline line-through"
                                                 : run.underline                    ? "underline"
                                                                                    : "line-through");
  }
  if (metadata_value(layer, kLayerMetadataTextOrientation).value_or(std::string_view{}) ==
      kTextOrientationVertical) {
    declarations.emplace_back("writing-mode", "vertical-rl");
  }
  if (metadata_value(layer, kLayerMetadataTextFlow).value_or(std::string_view{}) != "box") {
    // Point text never wraps, and the box is exactly as wide as the ink.
    const auto text = metadata_value(layer, kLayerMetadataText).value_or(std::string_view{});
    declarations.emplace_back("white-space", text.find('\n') != std::string_view::npos ? "pre" : "nowrap");
  }
}

// --- drop shadows ---

enum class ShadowForm {
  Box,    // box-shadow: the layer is its CSS box
  Text,   // text-shadow
  Filter  // filter: drop-shadow(), which follows the layer's transparency
};

void append_drop_shadows(Declarations& declarations, const Layer& layer, ShadowForm form) {
  if (!style_renders(layer)) {
    return;
  }
  std::string value;
  for (const auto& shadow : layer.layer_style().drop_shadows) {
    if (!shadow.enabled || shadow.opacity <= 0.0F) {
      continue;
    }
    // The angle is where the light comes from, counterclockwise from the right; the
    // shadow falls the other way (layer_style_effect_padding uses the same offsets).
    const double radians = (180.0 - static_cast<double>(shadow.angle_degrees)) * std::numbers::pi / 180.0;
    const double dx = std::cos(radians) * static_cast<double>(shadow.distance);
    const double dy = std::sin(radians) * static_cast<double>(shadow.distance);
    const double size = std::max(0.0, static_cast<double>(shadow.size));
    const double spread = size * std::clamp(static_cast<double>(shadow.spread), 0.0, 100.0) / 100.0;
    const auto shadow_color = color(shadow.color, static_cast<double>(shadow.opacity));
    const auto offsets = px(dx) + " " + px(dy) + " ";
    switch (form) {
      case ShadowForm::Box:
        value += std::string(value.empty() ? "" : ", ") + offsets + px(size - spread) +
                 (std::llround(spread * 100.0) != 0 ? " " + px(spread) : std::string()) + " " + shadow_color;
        break;
      case ShadowForm::Text:
        value += std::string(value.empty() ? "" : ", ") + offsets + px(size) + " " + shadow_color;
        break;
      case ShadowForm::Filter:
        // drop-shadow() takes a standard deviation, half of a box-shadow blur radius.
        value += std::string(value.empty() ? "" : " ") + "drop-shadow(" + offsets + px(size / 2.0) + " " +
                 shadow_color + ")";
        break;
    }
  }
  if (!value.empty()) {
    declarations.emplace_back(form == ShadowForm::Box ? "box-shadow" : form == ShadowForm::Text ? "text-shadow"
                                                                                                 : "filter",
                              std::move(value));
  }
}

// --- one layer ---

Box layer_box(const Document& document, const Layer& layer);

Box group_box(const Document& document, const Layer& group) {
  Box united;
  for (const auto& child : group.children()) {
    if (child.visible()) {
      united = unite_boxes(united, layer_box(document, child));
    }
  }
  return united;
}

// The box the layer's class is given: what it draws, in document pixels.
Box layer_box(const Document& document, const Layer& layer) {
  if (layer.kind() == LayerKind::Group) {
    return group_box(document, layer);
  }
  if (layer.kind() == LayerKind::Adjustment) {
    return {};
  }
  if (const auto* shape = layer.vector_shape(); shape != nullptr && layer_is_vector_shape(layer)) {
    return shape_box(document, layer, *shape).border_box;
  }
  return pixel_box(layer);
}

Declarations layer_declarations(const Document& document, const Layer& layer, const Box& box) {
  Declarations declarations;
  declarations.emplace_back("position", "absolute");
  declarations.emplace_back("left", px(box.left));
  declarations.emplace_back("top", px(box.top));
  declarations.emplace_back("width", px(box.width));
  declarations.emplace_back("height", px(box.height));

  const bool group = layer.kind() == LayerKind::Group;
  const auto* shape = !group && layer_is_vector_shape(layer) ? layer.vector_shape() : nullptr;
  const bool text = !group && shape == nullptr && layer_is_text(layer);

  // Fill opacity dims the layer's own paint and leaves its effects alone. Shape and text
  // classes carry it in their colors; a layer with nothing but pixels can only fold it
  // into `opacity`, which is the same thing while no effect renders.
  double opacity = static_cast<double>(layer.opacity());
  if (shape == nullptr && !text && !style_renders(layer)) {
    opacity *= static_cast<double>(layer.fill_opacity());
  }
  if (std::llround(std::clamp(opacity, 0.0, 1.0) * 100.0) < 100) {
    declarations.emplace_back("opacity", number(std::clamp(opacity, 0.0, 1.0)));
  }
  if (const auto blend = svg::detail::blend_mode_css(layer.blend_mode()); !blend.empty() && blend != "normal") {
    declarations.emplace_back("mix-blend-mode", std::string(blend));
  }
  if (group) {
    return declarations;
  }
  if (shape != nullptr) {
    const auto shape_geometry = shape_box(document, layer, *shape);
    append_shape_paint(declarations, document, layer, *shape, shape_geometry);
    append_drop_shadows(declarations, layer,
                        shape_geometry.live != nullptr || shape_geometry.whole_canvas ? ShadowForm::Box
                                                                                      : ShadowForm::Filter);
  } else if (text) {
    append_text(declarations, layer);
    append_drop_shadows(declarations, layer, ShadowForm::Text);
  } else {
    append_drop_shadows(declarations, layer, ShadowForm::Filter);
  }
  return declarations;
}

struct Emitter {
  const Document& document;
  const std::set<LayerId>& wanted;
  LayerCss result;
  std::set<std::string> used_names;
  std::string body;

  std::string unique_class_name(std::string_view layer_name) {
    const auto base = class_name_from_layer_name(layer_name);
    auto candidate = base;
    for (int suffix = 2; used_names.contains(candidate); ++suffix) {
      candidate = base + "-" + std::to_string(suffix);
    }
    used_names.insert(candidate);
    return candidate;
  }

  // Panel order: the last layer of a list is the top row.
  void walk(const std::vector<Layer>& layers, bool ancestors_visible) {
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
      const auto& layer = *it;
      const bool visible = ancestors_visible && layer.visible();
      if (wanted.contains(layer.id())) {
        emit(layer, visible);
      }
      if (layer.kind() == LayerKind::Group) {
        walk(layer.children(), visible);
      }
    }
  }

  void emit(const Layer& layer, bool visible) {
    if (!visible) {
      ++result.hidden_skipped;
      return;
    }
    const auto box = layer_box(document, layer);
    if (box.empty()) {
      ++result.empty_skipped;
      return;
    }
    body += "\n." + unique_class_name(layer.name()) + " {\n";
    for (const auto& [property, value] : layer_declarations(document, layer, box)) {
      body += "  " + property + ": " + value + ";\n";
    }
    body += "}\n";
    ++result.classes;
  }
};

// Decodes one UTF-8 sequence at `index`; 0 when the bytes there are not a valid one.
std::size_t utf8_sequence(std::string_view text, std::size_t index, std::uint32_t& code_point) {
  const auto lead = static_cast<unsigned char>(text[index]);
  std::size_t length = 0;
  if (lead >= 0xC2U && lead <= 0xDFU) {
    length = 2;
    code_point = lead & 0x1FU;
  } else if (lead >= 0xE0U && lead <= 0xEFU) {
    length = 3;
    code_point = lead & 0x0FU;
  } else if (lead >= 0xF0U && lead <= 0xF4U) {
    length = 4;
    code_point = lead & 0x07U;
  } else {
    return 0;
  }
  if (index + length > text.size()) {
    return 0;
  }
  for (std::size_t i = 1; i < length; ++i) {
    const auto next = static_cast<unsigned char>(text[index + i]);
    if ((next & 0xC0U) != 0x80U) {
      return 0;
    }
    code_point = (code_point << 6U) | (next & 0x3FU);
  }
  const bool overlong = (length == 3 && code_point < 0x800U) || (length == 4 && code_point < 0x10000U);
  const bool surrogate = code_point >= 0xD800U && code_point <= 0xDFFFU;
  return overlong || surrogate || code_point > 0x10FFFFU ? 0 : length;
}

// Spaces, controls and invisible marks that would make a class name hard to type or see.
bool separator_code_point(std::uint32_t code_point) {
  return code_point <= 0xA0U || code_point == 0x1680U || (code_point >= 0x2000U && code_point <= 0x200FU) ||
         (code_point >= 0x2028U && code_point <= 0x202FU) || (code_point >= 0x205FU && code_point <= 0x206FU) ||
         code_point == 0x3000U || code_point == 0xFEFFU;
}

}  // namespace

std::string class_name_from_layer_name(std::string_view layer_name) {
  std::string name;
  const auto separator = [&name] {
    if (!name.empty() && name.back() != '-') {
      name.push_back('-');
    }
  };
  for (std::size_t index = 0; index < layer_name.size();) {
    const char ch = layer_name[index];
    if (static_cast<unsigned char>(ch) < 0x80U) {
      if (ch >= 'A' && ch <= 'Z') {
        name.push_back(static_cast<char>(ch - 'A' + 'a'));
      } else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_') {
        name.push_back(ch);
      } else {
        separator();
      }
      ++index;
      continue;
    }
    std::uint32_t code_point = 0;
    const auto length = utf8_sequence(layer_name, index, code_point);
    if (length == 0) {
      separator();
      ++index;
    } else if (separator_code_point(code_point)) {
      separator();
      index += length;
    } else {
      name.append(layer_name.substr(index, length));
      index += length;
    }
  }
  while (!name.empty() && name.back() == '-') {
    name.pop_back();
  }
  if (name.empty()) {
    return "layer";
  }
  if (name.front() >= '0' && name.front() <= '9') {
    name.insert(0, "layer-");
  }
  return name;
}

LayerCss layers_to_css(const Document& document, std::span<const LayerId> layer_ids,
                       std::string_view document_name) {
  const std::set<LayerId> wanted(layer_ids.begin(), layer_ids.end());
  Emitter emitter{document, wanted, {}, {}, {}};
  emitter.walk(document.layers(), true);
  auto result = std::move(emitter.result);
  if (result.classes == 0) {
    return result;
  }
  // The header names the document; nothing in it may end the comment early.
  std::string header = "/* Patchy";
  std::string name;
  for (const char ch : document_name) {
    name.push_back(static_cast<unsigned char>(ch) < 0x20U ? ' ' : ch);
  }
  for (auto found = name.find("*/"); found != std::string::npos; found = name.find("*/", found)) {
    name.replace(found, 2, "* /");
  }
  if (!name.empty()) {
    header += ": " + name;
  }
  header += " */\n";
  result.text = header + emitter.body;
  return result;
}

}  // namespace patchy::css
