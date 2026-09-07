#include "ui/script_vector.hpp"
#include "ui/script_api.hpp"
#include "core/layer_metadata.hpp"
#include "core/vector_live_shapes.hpp"
#include "ui/custom_shape_library.hpp"
#include <QJSValueIterator>
#include <QColor>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>

namespace patchy::ui::script_vector {
void invalid(const QString& field) { throw std::invalid_argument(field.toUtf8().constData()); }
namespace {
QJsonValue json_value(const QJSValue& value, int depth, int& remaining) {
  if (depth > 32 || --remaining < 0) { invalid("data.length"); }
  if (value.isNull()) { return QJsonValue(QJsonValue::Null); }
  if (value.isBool()) { return value.toBool(); }
  if (value.isString()) { return value.toString(); }
  if (value.isNumber()) {
    const double n = value.toNumber();
    if (!std::isfinite(n)) { invalid("number"); }
    return n;
  }
  if (value.isArray()) {
    const auto count = value.property("length").toUInt();
    if (count > 100000) { invalid("array.length"); }
    QJsonArray result;
    for (quint32 i = 0; i < count; ++i) { result.append(json_value(value.property(i), depth + 1, remaining)); }
    return result;
  }
  if (!value.isObject() || value.isCallable() || value.isQObject()) { invalid("object"); }
  QJsonObject result;
  QJSValueIterator it(value);
  while (it.hasNext()) {
    it.next();
    result[it.name()] = json_value(it.value(), depth + 1, remaining);
  }
  return result;
}
QString operation_id(PathCombineOp op) {
  switch (op) {
    case PathCombineOp::Add: return "unite";
    case PathCombineOp::Subtract: return "subtract";
    case PathCombineOp::Intersect: return "intersect";
    case PathCombineOp::Xor: return "exclude";
  }
  return {};
}
}
QJsonObject object(const QJSValue& value, bool optional) {
  if (optional && value.isUndefined()) { return {}; }
  int remaining = 1200000;
  const auto json = json_value(value, 0, remaining);
  if (!json.isObject()) { invalid("object"); }
  return json.toObject();
}
void keys(const QJsonObject& value, const QStringList& allowed) {
  for (auto it = value.begin(); it != value.end(); ++it) {
    if (!allowed.contains(it.key())) { invalid(it.key()); }
  }
}
double number(const QJsonObject& value, const QString& key, double fallback, double minimum, double maximum) {
  if (!value.contains(key)) { return fallback; }
  const auto item = value[key];
  if (!item.isDouble() || !std::isfinite(item.toDouble()) || item.toDouble() < minimum || item.toDouble() > maximum) {
    invalid(key);
  }
  return item.toDouble();
}
double required_number(const QJsonObject& value, const QString& key, double minimum, double maximum) {
  if (!value.contains(key)) { invalid(key); }
  return number(value, key, 0, minimum, maximum);
}
int integer(const QJsonObject& value, const QString& key, int fallback, int minimum, int maximum) {
  const auto n = number(value, key, fallback, minimum, maximum);
  if (std::floor(n) != n) { invalid(key); }
  return static_cast<int>(n);
}
bool boolean(const QJsonObject& value, const QString& key, bool fallback) {
  if (!value.contains(key)) { return fallback; }
  if (!value[key].isBool()) { invalid(key); }
  return value[key].toBool();
}
QString string(const QJsonObject& value, const QString& key, const QString& fallback) {
  if (!value.contains(key)) { return fallback; }
  if (!value[key].isString()) { invalid(key); }
  return value[key].toString();
}
QJsonObject child_object(const QJsonObject& value, const QString& key) {
  if (!value[key].isObject()) { invalid(key); }
  return value[key].toObject();
}
QJSValue to_js(ScriptEngineHost& host, const QJsonValue& value) {
  // QVariantList wrappers stringify as arrays but QJSValue::isArray() is false.
  // Return actual detached JS arrays/objects so snapshots can be edited and
  // submitted back through the same strictly typed path representation.
  if (value.isArray()) {
    const auto values = value.toArray();
    auto result = host.engine()->newArray(static_cast<quint32>(values.size()));
    for (qsizetype i = 0; i < values.size(); ++i) { result.setProperty(static_cast<quint32>(i), to_js(host, values[i])); }
    return result;
  }
  if (value.isObject()) {
    auto result = host.engine()->newObject();
    const auto values = value.toObject();
    for (auto i = values.begin(); i != values.end(); ++i) { result.setProperty(i.key(), to_js(host, i.value())); }
    return result;
  }
  if (value.isBool()) { return QJSValue(value.toBool()); }
  if (value.isDouble()) { return QJSValue(value.toDouble()); }
  if (value.isString()) { return QJSValue(value.toString()); }
  return QJSValue(QJSValue::NullValue);
}
const Document& document(ScriptEngineHost& host, std::int64_t session) {
  const auto* doc = host.session_document_const(session);
  if (!doc) { invalid("document.id"); }
  return *doc;
}
const Layer& layer(ScriptEngineHost& host, std::int64_t session, LayerId id, bool editable) {
  const auto& doc = document(host, session);
  const auto* result = doc.find_layer(id);
  if (!result) { invalid("layer.id"); }
  if (editable && (result->lock_flags() != kLayerLockNone ||
      layer_effectively_locks_image_pixels(doc.layers(), id) || layer_effectively_locks_position(doc.layers(), id) ||
      !vector_lock_reason(*result).empty())) { invalid("layer.locked"); }
  return *result;
}
Document& writable(ScriptEngineHost& host, std::int64_t session) {
  if (host.engine()->isInterrupted() || !host.prepare_mutation(session) || host.engine()->isInterrupted()) { invalid("document.interrupted"); }
  auto* result = host.session_document(session);
  if (!result) { invalid("document.id"); }
  return *result;
}
VectorPath parse_path(const QJsonObject& value, bool allow_empty) {
  keys(value, {"subpaths"});
  if (!value["subpaths"].isArray()) { invalid("subpaths"); }
  const auto parts = value["subpaths"].toArray();
  if (parts.size() > 4096 || (!allow_empty && parts.isEmpty())) { invalid("subpaths.length"); }
  VectorPath result;
  std::map<int, PathCombineOp> groups;
  int count = 0;
  for (const auto& part : parts) {
    if (!part.isObject()) { invalid("subpath"); }
    const auto sub = part.toObject();
    keys(sub, {"anchors", "closed", "operation", "group"});
    PathSubpath path;
    path.closed = boolean(sub, "closed", true);
    path.shape_group = integer(sub, "group", static_cast<int>(result.subpaths.size()), 0, 2147483646);
    const auto op = string(sub, "operation", "unite");
    if (op == "unite") { path.op = PathCombineOp::Add; }
    else if (op == "subtract") { path.op = PathCombineOp::Subtract; }
    else if (op == "intersect") { path.op = PathCombineOp::Intersect; }
    else if (op == "exclude") { path.op = PathCombineOp::Xor; }
    else { invalid("operation"); }
    if (const auto it = groups.find(path.shape_group); it != groups.end() && it->second != path.op) { invalid("group.operation"); }
    groups[path.shape_group] = path.op;
    if (!sub["anchors"].isArray()) { invalid("anchors"); }
    const auto anchors = sub["anchors"].toArray();
    count += static_cast<int>(anchors.size());
    if (anchors.size() < 2 || count > 100000) { invalid("anchors.length"); }
    for (const auto& item : anchors) {
      if (!item.isObject()) { invalid("anchor"); }
      const auto a = item.toObject();
      keys(a, {"x", "y", "inX", "inY", "outX", "outY", "smooth"});
      PathAnchor anchor;
      anchor.anchor_x = required_number(a, "x"); anchor.anchor_y = required_number(a, "y");
      anchor.in_x = number(a, "inX", anchor.anchor_x); anchor.in_y = number(a, "inY", anchor.anchor_y);
      anchor.out_x = number(a, "outX", anchor.anchor_x); anchor.out_y = number(a, "outY", anchor.anchor_y);
      anchor.smooth = boolean(a, "smooth", false);
      path.anchors.push_back(anchor);
    }
    result.subpaths.push_back(std::move(path));
  }
  return result;
}
QJsonObject path_json(const VectorPath& path) {
  QJsonArray subpaths;
  for (const auto& sub : path.subpaths) {
    QJsonArray anchors;
    for (const auto& a : sub.anchors) {
      anchors.append(QJsonObject{{"x", a.anchor_x}, {"y", a.anchor_y}, {"inX", a.in_x}, {"inY", a.in_y},
                                {"outX", a.out_x}, {"outY", a.out_y}, {"smooth", a.smooth}});
    }
    subpaths.append(QJsonObject{{"anchors", anchors}, {"closed", sub.closed},
                                {"operation", operation_id(sub.op)}, {"group", sub.shape_group}});
  }
  return {{"subpaths", subpaths}};
}
std::array<double, 6> matrix(const QJSValue& value) {
  if (!value.isArray() || value.property("length").toInt() != 6) { invalid("matrix"); }
  std::array<double, 6> result{};
  for (quint32 i = 0; i < 6; ++i) {
    const auto n = value.property(i);
    if (!n.isNumber() || !std::isfinite(n.toNumber()) || std::abs(n.toNumber()) > 100000) { invalid("matrix"); }
    result[i] = n.toNumber();
  }
  if (std::abs(result[0] * result[3] - result[1] * result[2]) < 1e-12) { invalid("matrix.determinant"); }
  return result;
}
void validate_transformed_path(const VectorPath& path, const std::array<double, 6>& transform) {
  auto copy = path;
  transform_vector_path(copy, transform);
  (void)parse_path(path_json(copy));
}
VectorShapeContent geometry(ScriptEngineHost& host, const QJsonObject& value, double resolution) {
  const auto type = string(value, "type");
  VectorShapeContent result;
  result.stroke.alignment = VectorStrokeAlignment::Inside;
  if (type == "path") {
    keys(value, {"type", "path"});
    result.path = parse_path(child_object(value, "path"), false);
    return result;
  }
  if (type == "polygon") {
    keys(value, {"type", "cx", "cy", "radius", "sides", "starInset", "angle"});
    const auto cx = required_number(value, "cx"), cy = required_number(value, "cy");
    const auto radius = required_number(value, "radius", 0.5, 30000);
    const auto sides = integer(value, "sides", 5, 3, 100);
    const auto inset = integer(value, "starInset", 0, 0, 99);
    const auto angle = number(value, "angle", -90, -36000, 36000);
    result.path.subpaths.push_back(generate_polygon_subpath(cx, cy, radius, angle * std::numbers::pi / 180.0, sides, inset));
    (void)parse_path(path_json(result.path), false);
    return result;
  }
  LiveShapeParams params;
  params.resolution = resolution;
  if (type == "line") {
    keys(value, {"type", "x1", "y1", "x2", "y2", "weight", "arrowStart", "arrowEnd", "arrowWidth", "arrowLength"});
    params.kind = LiveShapeKind::Line;
    params.line_start_x = required_number(value, "x1"); params.line_start_y = required_number(value, "y1");
    params.line_end_x = required_number(value, "x2"); params.line_end_y = required_number(value, "y2");
    if (std::hypot(params.line_end_x - params.line_start_x, params.line_end_y - params.line_start_y) < 0.001) { invalid("line.length"); }
    params.line_weight = number(value, "weight", 1, 0.001, 30000);
    params.arrow_start = boolean(value, "arrowStart", false); params.arrow_end = boolean(value, "arrowEnd", false);
    params.arrow_width = number(value, "arrowWidth", params.line_weight * 5, 0, 30000);
    params.arrow_length = number(value, "arrowLength", params.line_weight * 10, 0, 30000);
    params.left = std::min(params.line_start_x, params.line_end_x); params.right = std::max(params.line_start_x, params.line_end_x);
    params.top = std::min(params.line_start_y, params.line_end_y); params.bottom = std::max(params.line_start_y, params.line_end_y);
  } else {
    keys(value, {"type", "x", "y", "width", "height", "radius", "radii", "resourceId"});
    params.left = required_number(value, "x"); params.top = required_number(value, "y");
    params.right = params.left + required_number(value, "width", 0.001, 30000);
    params.bottom = params.top + required_number(value, "height", 0.001, 30000);
    if (type == "custom") {
      const auto* entry = host.vector_custom_shape_library().find_entry_by_shape_id(string(value, "resourceId"));
      if (!entry) { invalid("resourceId"); }
      result.path = entry->path;
      transform_vector_path(result.path, {params.right - params.left, 0, 0, params.bottom - params.top, params.left, params.top});
      (void)parse_path(path_json(result.path), false);
      return result;
    }
    if (type == "rectangle") { params.kind = LiveShapeKind::Rectangle; }
    else if (type == "roundedRectangle") { params.kind = LiveShapeKind::RoundedRectangle; }
    else if (type == "ellipse") { params.kind = LiveShapeKind::Ellipse; }
    else { invalid("geometry.type"); }
    if (value.contains("radius") || value.contains("radii")) {
      if (params.kind == LiveShapeKind::Ellipse || (value.contains("radius") && value.contains("radii"))) { invalid("radius"); }
      params.kind = LiveShapeKind::RoundedRectangle;
      params.corner_radii.fill(number(value, "radius", 0, 0, 30000));
      if (value.contains("radii")) {
        const auto a = value["radii"].toArray();
        if (!value["radii"].isArray() || a.size() != 4) { invalid("radii"); }
        for (int i = 0; i < 4; ++i) { params.corner_radii[static_cast<std::size_t>(i)] = required_number({{"r", a[i]}}, "r", 0, 30000); }
      }
    }
  }
  populate_live_shape_box_corners(params);
  result.path.subpaths = generate_live_shape_subpaths(params);
  (void)parse_path(path_json(result.path), false);
  result.origination.push_back(params);
  return result;
}
QJsonArray live_json(const std::vector<LiveShapeParams>& values) {
  QJsonArray result;
  for (const auto& p : values) {
    QString type;
    switch (p.kind) {
      case LiveShapeKind::Rectangle: type = "rectangle"; break;
      case LiveShapeKind::RoundedRectangle: type = "roundedRectangle"; break;
      case LiveShapeKind::Ellipse: type = "ellipse"; break;
      case LiveShapeKind::Line: type = "line"; break;
      default: type = "custom"; break;
    }
    QJsonObject g{{"type", type}};
    if (p.kind == LiveShapeKind::Line) {
      g["x1"] = p.line_start_x; g["y1"] = p.line_start_y; g["x2"] = p.line_end_x; g["y2"] = p.line_end_y;
      g["weight"] = p.line_weight; g["arrowStart"] = p.arrow_start; g["arrowEnd"] = p.arrow_end;
      g["arrowWidth"] = p.arrow_width; g["arrowLength"] = p.arrow_length;
    } else {
      g["x"] = p.left; g["y"] = p.top; g["width"] = p.right - p.left; g["height"] = p.bottom - p.top;
      if (p.kind == LiveShapeKind::RoundedRectangle) { g["radii"] = QJsonArray{p.corner_radii[0], p.corner_radii[1], p.corner_radii[2], p.corner_radii[3]}; }
    }
    result.append(QJsonObject{{"group", p.index}, {"geometry", g}});
  }
  return result;
}
void erase_mask_blocks(Layer& value) {
  auto& blocks = value.unknown_psd_blocks();
  std::erase_if(blocks, [](const UnknownPsdBlock& b) { return b.key == "vmsk" || b.key == "vsms"; });
  mark_layer_vector_block_dirty(value);
}
}  // namespace patchy::ui::script_vector
