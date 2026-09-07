#include "ui/script_vector.hpp"
#include "ui/script_api.hpp"
#include "ui/custom_shape_library.hpp"
#include "ui/gradient_library.hpp"
#include "ui/pattern_library.hpp"
#include "ui/shape_appearance_dialog.hpp"
#include <QColor>
#include <algorithm>
#include <cmath>

namespace patchy::ui::script_vector {
namespace {
RgbColor color(const QString& text) {
  const QColor c(text);
  // Solid vector paints use the native RGB model; opacity is a layer/stroke property.
  if (!c.isValid() || c.alpha() != 255) { invalid("color"); }
  return {static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()), static_cast<std::uint8_t>(c.blue())};
}
QString color_json(RgbColor c) { return QColor(c.red, c.green, c.blue).name(); }
int choice(const QJsonObject& obj, const QString& key, const QStringList& values, int fallback) {
  if (!obj.contains(key)) { return fallback; }
  const int i = static_cast<int>(values.indexOf(string(obj, key)));
  if (i < 0) { invalid(key); }
  return i;
}
const QStringList gradient_types{"linear", "radial", "angle", "reflected", "diamond"};
const QStringList interpolations{"classic", "perceptual", "linear"};
QJsonObject gradient_json(const LayerStyleGradient& g) {
  QJsonArray colors, alpha, minimum, maximum;
  for (const auto& s : g.color_stops) { colors.append(QJsonObject{{"position", s.location}, {"color", color_json(s.color)}, {"midpoint", s.midpoint},
      {"kind", s.kind == GradientColorStop::Kind::Foreground ? "foreground" : s.kind == GradientColorStop::Kind::Background ? "background" : "color"}}); }
  for (const auto& s : g.alpha_stops) { alpha.append(QJsonObject{{"position", s.location}, {"opacity", s.opacity}, {"midpoint", s.midpoint}}); }
  for (auto v : g.noise.minimum) { minimum.append(v); }
  for (auto v : g.noise.maximum) { maximum.append(v); }
  return {{"form", g.form == GradientDefinitionForm::Noise ? "noise" : "solid"}, {"name", QString::fromStdString(g.name)},
    {"type", gradient_types.value(static_cast<int>(g.type), "linear")}, {"colorStops", colors}, {"alphaStops", alpha},
    {"smoothness", g.smoothness}, {"angle", g.angle_degrees}, {"scale", g.scale}, {"reverse", g.reverse},
    {"dither", g.dither}, {"interpolation", interpolations.value(static_cast<int>(g.interpolation))},
    {"alignWithLayer", g.align_with_layer}, {"offsetX", g.offset_x_percent}, {"offsetY", g.offset_y_percent},
    {"noise", QJsonObject{{"seed", static_cast<double>(g.noise.seed)}, {"roughness", g.noise.roughness},
      {"transparency", g.noise.add_transparency}, {"restrictColors", g.noise.restrict_colors},
      {"colorModel", QStringList{"rgb", "hsb", "lab"}.value(static_cast<int>(g.noise.color_model))},
      {"minimum", minimum}, {"maximum", maximum}}}};
}
LayerStyleGradient gradient(ScriptEngineHost& host, const QJsonObject& v, LayerStyleGradient g) {
  keys(v, {"presetId", "foreground", "background", "form", "name", "type", "colorStops", "alphaStops", "smoothness",
           "angle", "scale", "reverse", "dither", "interpolation", "alignWithLayer", "offsetX", "offsetY", "noise"});
  if (v.contains("presetId")) {
    if (v.contains("colorStops") || v.contains("alphaStops") || v.contains("noise") || v.contains("form")) { invalid("gradient.presetId"); }
    const auto* preset = host.vector_gradient_library().find_entry(string(v, "presetId"));
    if (!preset) { invalid("gradient.presetId"); }
    static_cast<GradientDefinition&>(g) = preset->definition;
  }
  g.form = static_cast<GradientDefinitionForm>(choice(v, "form", {"solid", "noise"}, static_cast<int>(g.form)));
  g.name = string(v, "name", QString::fromStdString(g.name)).toStdString();
  g.type = static_cast<LayerStyleGradientType>(choice(v, "type", gradient_types, static_cast<int>(g.type)));
  g.smoothness = static_cast<std::uint16_t>(integer(v, "smoothness", g.smoothness, 0, 4096));
  g.angle_degrees = static_cast<float>(number(v, "angle", g.angle_degrees, -36000, 36000));
  g.scale = static_cast<float>(number(v, "scale", g.scale, 0.0001, 10000));
  g.reverse = boolean(v, "reverse", g.reverse); g.dither = boolean(v, "dither", g.dither);
  g.interpolation = static_cast<GradientInterpolationMethod>(choice(v, "interpolation", interpolations, static_cast<int>(g.interpolation)));
  g.align_with_layer = boolean(v, "alignWithLayer", g.align_with_layer);
  g.offset_x_percent = static_cast<float>(number(v, "offsetX", g.offset_x_percent));
  g.offset_y_percent = static_cast<float>(number(v, "offsetY", g.offset_y_percent));
  if (v.contains("colorStops")) {
    if (!v["colorStops"].isArray() || v["colorStops"].toArray().size() > 256) { invalid("colorStops"); }
    g.color_stops.clear();
    for (const auto& stop : v["colorStops"].toArray()) {
      if (!stop.isObject()) { invalid("colorStops"); }
      const auto s = stop.toObject(); keys(s, {"position", "color", "midpoint", "kind"});
      GradientColorStop parsed;
      parsed.location = static_cast<float>(required_number(s, "position", 0, 1));
      parsed.midpoint = static_cast<float>(number(s, "midpoint", 0.5, 0, 1));
      parsed.kind = static_cast<GradientColorStop::Kind>(choice(s, "kind", {"color", "foreground", "background"}, 0));
      parsed.color = color(string(s, "color", "#000000"));
      g.color_stops.push_back(parsed);
    }
    std::stable_sort(g.color_stops.begin(), g.color_stops.end(), [](const auto& a, const auto& b) { return a.location < b.location; });
  }
  if (v.contains("alphaStops")) {
    if (!v["alphaStops"].isArray() || v["alphaStops"].toArray().size() > 256) { invalid("alphaStops"); }
    g.alpha_stops.clear();
    for (const auto& stop : v["alphaStops"].toArray()) {
      if (!stop.isObject()) { invalid("alphaStops"); }
      const auto s = stop.toObject(); keys(s, {"position", "opacity", "midpoint"});
      g.alpha_stops.push_back({static_cast<float>(required_number(s, "position", 0, 1)),
        static_cast<float>(required_number(s, "opacity", 0, 1)), static_cast<float>(number(s, "midpoint", 0.5, 0, 1))});
    }
    std::stable_sort(g.alpha_stops.begin(), g.alpha_stops.end(), [](const auto& a, const auto& b) { return a.location < b.location; });
  }
  if (v.contains("noise")) {
    const auto n = child_object(v, "noise");
    keys(n, {"seed", "roughness", "transparency", "restrictColors", "colorModel", "minimum", "maximum"});
    const auto seed = number(n, "seed", g.noise.seed, 0, 4294967295.0);
    if (std::floor(seed) != seed) { invalid("noise.seed"); }
    g.noise.seed = static_cast<std::uint32_t>(seed);
    g.noise.roughness = static_cast<std::uint16_t>(integer(n, "roughness", g.noise.roughness, 0, 4096));
    g.noise.add_transparency = boolean(n, "transparency", g.noise.add_transparency);
    g.noise.restrict_colors = boolean(n, "restrictColors", g.noise.restrict_colors);
    g.noise.color_model = static_cast<GradientNoiseColorModel>(choice(n, "colorModel", {"rgb", "hsb", "lab"}, static_cast<int>(g.noise.color_model)));
    for (const auto& key : {QStringLiteral("minimum"), QStringLiteral("maximum")}) {
      if (!n.contains(key)) { continue; }
      if (!n[key].isArray() || n[key].toArray().size() != 4) { invalid(key); }
      auto& destination = key == "minimum" ? g.noise.minimum : g.noise.maximum;
      const auto array = n[key].toArray();
      for (int i = 0; i < 4; ++i) { destination[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(integer({{"v", array[i]}}, "v", 0, 0, 100)); }
    }
    for (std::size_t i = 0; i < 4; ++i) { if (g.noise.minimum[i] > g.noise.maximum[i]) { invalid("noise.minimum"); } }
  }
  if (g.form == GradientDefinitionForm::Solid && g.color_stops.empty()) {
    if (v.contains("colorStops")) { invalid("colorStops.length"); }
    g.color_stops = {{0, {0, 0, 0}}, {1, {255, 255, 255}}};
  }
  const auto fg = color(string(v, "foreground", "#000000")), bg = color(string(v, "background", "#ffffff"));
  if (v.contains("presetId") || v.contains("colorStops") || v.contains("foreground") || v.contains("background")) {
    static_cast<GradientDefinition&>(g) = resolve_gradient_definition(g, fg, bg);
  }
  return g;
}
}
VectorFill paint(ScriptEngineHost& host, const QJsonValue& input, PatternStore& patterns, VectorFill result) {
  QJsonObject v;
  if (input.isString()) {
    v = {{"type", input.toString() == "none" ? "none" : "solid"}};
    if (input.toString() != "none") { v["color"] = input.toString(); }
  }
  else if (input.isObject()) { v = input.toObject(); }
  else { invalid("paint"); }
  const auto kind = string(v, "type", paint_json(result)["type"].toString());
  if (kind == "none") { keys(v, {"type"}); result.kind = VectorFillKind::None; }
  else if (kind == "solid") { keys(v, {"type", "color"}); result.kind = VectorFillKind::Solid; result.color = color(string(v, "color", color_json(result.color))); }
  else if (kind == "gradient") {
    keys(v, {"type", "gradient"});
    result.kind = VectorFillKind::Gradient;
    result.gradient = gradient(host, v.contains("gradient") ? child_object(v, "gradient") : QJsonObject{}, result.gradient);
  } else if (kind == "pattern") {
    keys(v, {"type", "source", "resourceId", "scale", "angle", "linked", "offsetX", "offsetY"});
    result.kind = VectorFillKind::Pattern;
    if (v.contains("resourceId") || v.contains("source")) {
      const auto source = string(v, "source", "document"), id = string(v, "resourceId");
      std::optional<PatternResource> resource;
      if (source == "library") { resource = host.vector_pattern_library().resource_for_entry(id); }
      else if (source == "document") {
        if (const auto* found = patterns.find(id.toStdString())) { resource = *found; }
      } else { invalid("pattern.source"); }
      if (!resource || pattern_tile_is_unrenderable(resource->tile)) { invalid("pattern.resourceId"); }
      if (source == "library") {
        if (const auto* old = patterns.find(resource->id); old && !presets::pattern_tiles_equal(old->tile, resource->tile)) {
          const auto same = std::find_if(patterns.patterns.begin(), patterns.patterns.end(), [&](const auto& p) {
            return p.name == resource->name && presets::pattern_tiles_equal(p.tile, resource->tile);
          });
          if (same != patterns.patterns.end()) { resource->id = same->id; }
          else { do { resource->id = generate_pattern_uuid(); } while (patterns.find(resource->id)); }
        }
        resource->provenance = PatternProvenance::Authored;
        patterns.adopt(*resource);
      }
      result.pattern_id = resource->id; result.pattern_name = resource->name;
    }
    if (!patterns.find(result.pattern_id)) { invalid("pattern.resourceId"); }
    result.pattern_scale = number(v, "scale", result.pattern_scale, 0.0001, 10000);
    result.pattern_angle_degrees = number(v, "angle", result.pattern_angle_degrees, -36000, 36000);
    result.pattern_linked = boolean(v, "linked", result.pattern_linked);
    result.pattern_phase_x = number(v, "offsetX", result.pattern_phase_x);
    result.pattern_phase_y = number(v, "offsetY", result.pattern_phase_y);
  } else { invalid("paint.type"); }
  return result;
}
QJsonObject paint_json(const VectorFill& p) {
  switch (p.kind) {
    case VectorFillKind::None: return {{"type", "none"}};
    case VectorFillKind::Solid: return {{"type", "solid"}, {"color", color_json(p.color)}};
    case VectorFillKind::Gradient: return {{"type", "gradient"}, {"gradient", gradient_json(p.gradient)}};
    case VectorFillKind::Pattern: return {{"type", "pattern"}, {"source", "document"}, {"resourceId", QString::fromStdString(p.pattern_id)},
      {"scale", p.pattern_scale}, {"angle", p.pattern_angle_degrees}, {"linked", p.pattern_linked}, {"offsetX", p.pattern_phase_x}, {"offsetY", p.pattern_phase_y}};
  }
  return {};
}
VectorStroke stroke(ScriptEngineHost& host, const QJsonObject& v, PatternStore& patterns, VectorStroke s) {
  keys(v, {"enabled", "fillEnabled", "width", "paint", "alignment", "cap", "join", "dashes", "dashOffset", "miterLimit", "opacity", "blendMode", "scaleLock", "adjust"});
  s.enabled = boolean(v, "enabled", s.enabled); s.fill_enabled = boolean(v, "fillEnabled", s.fill_enabled);
  s.width = number(v, "width", s.width, 0, 30000);
  if (v.contains("paint")) { s.content = paint(host, v["paint"], patterns, s.content); }
  s.alignment = static_cast<VectorStrokeAlignment>(choice(v, "alignment", {"inside", "center", "outside"}, static_cast<int>(s.alignment)));
  s.cap = static_cast<VectorStrokeCap>(choice(v, "cap", {"butt", "round", "square"}, static_cast<int>(s.cap)));
  s.join = static_cast<VectorStrokeJoin>(choice(v, "join", {"miter", "round", "bevel"}, static_cast<int>(s.join)));
  s.dash_offset = number(v, "dashOffset", s.dash_offset);
  s.miter_limit = number(v, "miterLimit", s.miter_limit, 1, 10000);
  s.opacity = number(v, "opacity", s.opacity, 0, 1);
  s.scale_lock = boolean(v, "scaleLock", s.scale_lock); s.stroke_adjust = boolean(v, "adjust", s.stroke_adjust);
  if (v.contains("blendMode") && !script_blend_mode_from_id(string(v, "blendMode"), &s.blend_mode)) { invalid("stroke.blendMode"); }
  if (s.blend_mode == BlendMode::PassThrough) { invalid("stroke.blendMode"); }
  if (v.contains("dashes")) {
    if (!v["dashes"].isArray() || v["dashes"].toArray().size() > 64) { invalid("dashes"); }
    s.dashes.clear();
    for (const auto& d : v["dashes"].toArray()) { s.dashes.push_back(required_number({{"dash", d}}, "dash", 0.0001, 10000)); }
  }
  return s;
}
QJsonObject stroke_json(const VectorStroke& s) {
  QJsonArray dashes;
  for (auto d : s.dashes) { dashes.append(d); }
  return {{"enabled", s.enabled}, {"fillEnabled", s.fill_enabled}, {"width", s.width}, {"paint", paint_json(s.content)},
    {"alignment", QStringList{"inside", "center", "outside"}.value(static_cast<int>(s.alignment))},
    {"cap", QStringList{"butt", "round", "square"}.value(static_cast<int>(s.cap))},
    {"join", QStringList{"miter", "round", "bevel"}.value(static_cast<int>(s.join))},
    {"dashes", dashes}, {"dashOffset", s.dash_offset}, {"miterLimit", s.miter_limit}, {"opacity", s.opacity},
    {"blendMode", script_blend_mode_id(s.blend_mode)}, {"scaleLock", s.scale_lock}, {"adjust", s.stroke_adjust}};
}
QJsonObject resources(ScriptEngineHost& host, const Document& doc) {
  QJsonArray shapes, gradients, patterns;
  for (const auto& s : host.vector_custom_shape_library().entries()) {
    shapes.append(QJsonObject{{"resourceId", s.id}, {"name", s.name}, {"folder", s.folder}});
  }
  for (const auto& g : host.vector_gradient_library().entries()) {
    gradients.append(QJsonObject{{"presetId", g.storage_id}, {"name", g.name}, {"folder", g.folder}});
  }
  for (const auto& p : host.vector_pattern_library().entries()) {
    patterns.append(QJsonObject{{"source", "library"}, {"resourceId", p.storage_id}, {"name", p.name},
      {"folder", p.folder}, {"width", p.size.width()}, {"height", p.size.height()}});
  }
  for (const auto& p : doc.metadata().patterns.patterns) {
    patterns.append(QJsonObject{{"source", "document"}, {"resourceId", QString::fromStdString(p.id)},
      {"name", QString::fromStdString(p.name)}, {"width", p.tile.width()}, {"height", p.tile.height()}});
  }
  return {{"customShapes", shapes}, {"gradients", gradients}, {"patterns", patterns}};
}
}  // namespace patchy::ui::script_vector
