#include "ui/script_vector.hpp"
#include "ui/script_api.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_tools.hpp"
#include "core/vector_raster.hpp"
#include "ui/qt_geometry.hpp"
#include <algorithm>
#include <functional>
#include <set>

namespace patchy::ui {
using namespace script_vector;
namespace {
bool shape_editable(const Document& doc, const Layer& item) {
  return item.vector_shape() && vector_lock_reason(item).empty() && item.lock_flags() == kLayerLockNone &&
    !layer_effectively_locks_image_pixels(doc.layers(), item.id()) && !layer_effectively_locks_position(doc.layers(), item.id());
}
void appearance(ScriptEngineHost& host, const QJsonObject& args, VectorShapeContent& content, PatternStore& patterns) {
  keys(args, {"fill", "stroke"});
  if (args.contains("fill")) { content.fill = paint(host, args["fill"], patterns, content.fill); }
  if (args.contains("stroke")) { content.stroke.enabled = true; content.stroke = stroke(host, child_object(args, "stroke"), patterns, content.stroke); }
}
QJSValue create_shape(ScriptEngineHost& host, std::int64_t session, const QString& name,
                      VectorShapeContent content, PatternStore patterns) {
  const auto& before = document(host, session);
  Layer prepared(0, name.toStdString(), PixelBuffer{});
  prepared.set_vector_shape(std::move(content));
  prepared.metadata()[kLayerMetadataVectorShape] = "1";
  mark_layer_vector_block_dirty(prepared);
  update_vector_shape_raster(prepared, Rect::from_size(before.width(), before.height()), &patterns);
  auto& doc = writable(host, session);
  const auto id = doc.allocate_layer_id();
  doc.metadata().patterns = std::move(patterns);
  doc.add_layer(prepared.clone_with_id(id));
  doc.set_active_layer(id);
  host.note_vector_changed(session, {}, true);
  return make_layer_value(host, session, id);
}
std::vector<LayerId> layer_ids(ScriptEngineHost& host, std::int64_t session, const QJSValue& input) {
  if (!input.isArray() || input.property("length").toUInt() > 10000) { invalid("layers"); }
  std::set<LayerId> requested;
  for (quint32 i = 0; i < input.property("length").toUInt(); ++i) {
    const auto* wrapper = qobject_cast<ScriptLayerObject*>(input.property(i).toQObject());
    if (!wrapper || wrapper->session_id() != session || !requested.insert(wrapper->layer_id()).second) { invalid("layers"); }
    (void)layer(host, session, wrapper->layer_id(), true);
  }
  if (requested.empty()) { invalid("layers.length"); }
  std::vector<LayerId> ordered;
  std::function<void(const std::vector<Layer>&)> collect = [&](const auto& items) {
    for (const auto& item : items) {
      if (requested.contains(item.id())) {
        for (const auto id : requested) { if (id != item.id() && layer_contains_descendant(item, id)) { invalid("layers.ancestor"); } }
        ordered.push_back(item.id());
      } else { collect(item.children()); }
    }
  };
  collect(document(host, session).layers());
  return ordered;
}
}
bool ScriptLayerObject::is_shape() const {
  return guarded(host_, [&] { return layer_has_vector_shape_marker(layer(host_, session_id_, layer_id_)); });
}
QJSValue ScriptLayerObject::getShape() const {
  return guarded(host_, [&] {
    const auto& item = layer(host_, session_id_, layer_id_);
    if (!layer_has_vector_shape_marker(item)) { return QJSValue(QJSValue::NullValue); }
    QJsonObject result{{"editable", shape_editable(document(host_, session_id_), item)},
      {"lockReason", QString::fromStdString(vector_lock_reason(item))}};
    if (const auto* shape = item.vector_shape()) {
      result["path"] = path_json(shape->path); result["liveShapes"] = live_json(shape->origination);
      result["fill"] = paint_json(shape->fill); result["stroke"] = stroke_json(shape->stroke);
      result["pathDisabled"] = shape->path_disabled; result["pathInverted"] = shape->path_inverted;
      result["isFillLayer"] = shape->path.empty();
    }
    return to_js(host_, result);
  });
}
QJSValue ScriptDocumentObject::addShape(const QString& name, const QJSValue& shape, const QJSValue& style) {
  return guarded(host_, [&] {
    const auto& doc = document(host_, session_id_);
    auto content = geometry(host_, object(shape), doc.print_settings().horizontal_ppi);
    auto patterns = doc.metadata().patterns;
    appearance(host_, object(style, true), content, patterns);
    return create_shape(host_, session_id_, name, std::move(content), std::move(patterns));
  });
}
QJSValue ScriptDocumentObject::addFillLayer(const QString& name, const QJSValue& value) {
  return guarded(host_, [&] {
    auto patterns = document(host_, session_id_).metadata().patterns;
    VectorShapeContent content;
    content.stroke.alignment = VectorStrokeAlignment::Inside;
    // Wrap to validate primitive paint shorthand as well as object paint definitions.
    auto wrapper = host_.engine()->newObject(); wrapper.setProperty("paint", value);
    content.fill = paint(host_, object(wrapper)["paint"], patterns);
    return create_shape(host_, session_id_, name, std::move(content), std::move(patterns));
  });
}
void ScriptLayerObject::updateShape(const QJSValue& changes) {
  guarded(host_, [&] {
    const auto args = object(changes);
    keys(args, {"geometry", "group", "path", "fill", "stroke", "pathDisabled", "pathInverted"});
    if (args.contains("geometry") && args.contains("path")) { invalid("geometry/path"); }
    if (args.contains("group") && !args.contains("geometry")) { invalid("group"); }
    const auto& original = layer(host_, session_id_, layer_id_, true);
    if (!original.vector_shape()) { invalid("layer.shape"); }
    auto content = *original.vector_shape();
    const auto& old_doc = document(host_, session_id_);
    auto patterns = old_doc.metadata().patterns;
    const auto before = to_qrect(layer_render_bounds(original));
    if (args.contains("geometry")) {
      auto fresh = geometry(host_, child_object(args, "geometry"), old_doc.print_settings().horizontal_ppi);
      if (args.contains("group")) {
        const auto group = integer(args, "group", 0, 0, 2147483646);
        auto first = std::find_if(content.path.subpaths.begin(), content.path.subpaths.end(), [group](const auto& p) { return p.shape_group == group; });
        if (first == content.path.subpaths.end()) { invalid("group"); }
        const auto position = std::distance(content.path.subpaths.begin(), first);
        const auto op = first->op;
        std::erase_if(content.path.subpaths, [group](const auto& p) { return p.shape_group == group; });
        for (auto& p : fresh.path.subpaths) { p.shape_group = group; p.op = op; }
        content.path.subpaths.insert(content.path.subpaths.begin() + position, fresh.path.subpaths.begin(), fresh.path.subpaths.end());
        drop_live_shape_origination(content, {group});
        for (auto& p : fresh.origination) { p.index = group; content.origination.push_back(p); }
      } else { content.path = std::move(fresh.path); content.origination = std::move(fresh.origination); }
    }
    if (args.contains("path")) {
      auto fresh = parse_path(child_object(args, "path"), false);
      // An unchanged group keeps its live parameters and preserved origination data.
      std::vector<int> changed;
      for (const auto& live : content.origination) {
        std::vector<PathSubpath> old_parts, new_parts;
        for (const auto& p : content.path.subpaths) { if (p.shape_group == live.index) { old_parts.push_back(p); } }
        for (const auto& p : fresh.subpaths) { if (p.shape_group == live.index) { new_parts.push_back(p); } }
        if (old_parts != new_parts) { changed.push_back(live.index); }
      }
      if (!changed.empty()) { drop_live_shape_origination(content, changed); }
      // Preserve imported fill-rule bookkeeping, which the public path doesn't reinterpret.
      fresh.fill_rule_value = content.path.fill_rule_value; fresh.initial_fill_value = content.path.initial_fill_value;
      content.path = std::move(fresh);
    }
    if (args.contains("fill")) { content.fill = paint(host_, args["fill"], patterns, content.fill); }
    if (args.contains("stroke")) { content.stroke = stroke(host_, child_object(args, "stroke"), patterns, content.stroke); }
    content.path_disabled = boolean(args, "pathDisabled", content.path_disabled);
    content.path_inverted = boolean(args, "pathInverted", content.path_inverted);
    const auto& old = *original.vector_shape();
    if (old.path == content.path && old.origination == content.origination && old.fill == content.fill && old.stroke == content.stroke &&
        old.path_disabled == content.path_disabled && old.path_inverted == content.path_inverted) { return; }
    Layer prepared = original;
    prepared.set_vector_shape(std::move(content)); mark_layer_vector_block_dirty(prepared);
    update_vector_shape_raster(prepared, Rect::from_size(old_doc.width(), old_doc.height()), &patterns);
    auto& doc = writable(host_, session_id_);
    (void)layer(host_, session_id_, layer_id_, true);
    *doc.find_layer(layer_id_) = std::move(prepared);
    doc.metadata().patterns = std::move(patterns);
    const auto after = to_qrect(layer_render_bounds(*std::as_const(doc).find_layer(layer_id_)));
    host_.note_vector_changed(session_id_, before.united(after));
  });
}
void ScriptLayerObject::transformShape(const QJSValue& transform, const QJSValue& options) {
  guarded(host_, [&] {
    const auto m = matrix(transform);
    const auto args = object(options, true); keys(args, {"strokeScale"});
    const auto stroke_scale = number(args, "strokeScale", 1, 0.0001, 10000);
    const auto& source = layer(host_, session_id_, layer_id_, true);
    const auto& doc = document(host_, session_id_);
    std::vector<LayerId> ids;
    std::function<void(const Layer&)> validate = [&](const Layer& l) {
      (void)layer(host_, session_id_, l.id(), true);
      if (l.vector_mask()) { validate_transformed_path(l.vector_mask()->path, m); }
      if (l.kind() == LayerKind::Group) {
        if (l.vector_mask()) { ids.push_back(l.id()); }
        for (const auto& child : l.children()) { validate(child); }
      } else {
        if (!l.vector_shape()) { invalid("layer.shape"); }
        validate_transformed_path(l.vector_shape()->path, m);
        if (l.vector_shape()->stroke.width * stroke_scale > 30000) { invalid("strokeScale"); }
        ids.push_back(l.id());
      }
    };
    validate(source);
    if (ids.empty() || (m == std::array<double, 6>{1, 0, 0, 1, 0, 0} && stroke_scale == 1)) { return; }
    auto staged = doc;
    QRect dirty;
    for (const auto id : ids) {
      auto* l = staged.find_layer(id);
      dirty |= to_qrect(layer_render_bounds(std::as_const(*l)));
      transform_layer_vector_data(staged, *l, m, Rect::from_size(doc.width(), doc.height()), stroke_scale);
      dirty |= to_qrect(layer_render_bounds(std::as_const(*l)));
    }
    auto& target = writable(host_, session_id_);
    for (const auto id : ids) { (void)layer(host_, session_id_, id, true); }
    target = std::move(staged);
    host_.note_vector_changed(session_id_, dirty);
  });
}
QJSValue ScriptDocumentObject::listVectorResources() const {
  return guarded(host_, [&] { return to_js(host_, resources(host_, document(host_, session_id_))); });
}
QJSValue ScriptDocumentObject::addGroup(const QString& name) {
  return guarded(host_, [&] {
    (void)document(host_, session_id_);
    auto& doc = writable(host_, session_id_);
    const auto id = doc.allocate_layer_id();
    Layer group(id, name.toStdString(), LayerKind::Group); group.set_blend_mode(BlendMode::PassThrough);
    doc.add_layer(std::move(group)); doc.set_active_layer(id);
    host_.note_vector_changed(session_id_, {}, true);
    return make_layer_value(host_, session_id_, id);
  });
}
QJSValue ScriptDocumentObject::groupLayers(const QJSValue& layers, const QString& name) {
  return guarded(host_, [&] {
    const auto ids = layer_ids(host_, session_id_, layers);
    const auto& original = document(host_, session_id_);
    const auto first = find_layer_location(original.layers(), ids.front());
    for (const auto id : ids) { if (find_layer_location(original.layers(), id)->siblings != first->siblings) { invalid("layers.parent"); } }
    auto staged = original;
    const auto destination = find_layer_location(staged.layers(), ids.front());
    const auto id = staged.allocate_layer_id();
    Layer group(id, name.toStdString(), LayerKind::Group); group.set_blend_mode(BlendMode::PassThrough);
    for (const auto child : ids) { auto moved = take_layer_from_tree(staged.layers(), child); group.add_child(std::move(*moved)); }
    destination->siblings->insert(destination->siblings->begin() + static_cast<std::ptrdiff_t>(destination->index), std::move(group));
    staged.set_active_layer(id);
    auto& doc = writable(host_, session_id_); doc = std::move(staged);
    host_.note_vector_changed(session_id_, {}, true);
    return make_layer_value(host_, session_id_, id);
  });
}
void ScriptDocumentObject::moveLayers(const QJSValue& layers, const QJSValue& destination) {
  guarded(host_, [&] {
    const auto ids = layer_ids(host_, session_id_, layers);
    const auto args = object(destination); keys(args, {"parentId", "index"});
    const auto& original = document(host_, session_id_);
    std::optional<LayerId> parent;
    if (args.contains("parentId") && !args["parentId"].isNull()) {
      bool ok = false; parent = string(args, "parentId").toULongLong(&ok);
      if (!ok || layer(host_, session_id_, *parent, true).kind() != LayerKind::Group) { invalid("parentId"); }
      for (const auto id : ids) {
        if (id == *parent || layer_contains_descendant(*original.find_layer(id), *parent)) { invalid("parentId.cycle"); }
      }
    }
    auto staged = original;
    std::vector<Layer> moved;
    for (const auto id : ids) { auto item = take_layer_from_tree(staged.layers(), id); moved.push_back(std::move(*item)); }
    auto& siblings = parent ? staged.find_layer(*parent)->children() : staged.layers();
    const auto index = integer(args, "index", static_cast<int>(siblings.size()), 0, static_cast<int>(siblings.size()));
    siblings.insert(siblings.begin() + index, std::make_move_iterator(moved.begin()), std::make_move_iterator(moved.end()));
    if (layer_tree_signature(original.layers()) == layer_tree_signature(std::as_const(staged).layers())) { return; }
    auto& doc = writable(host_, session_id_); doc = std::move(staged);
    host_.note_vector_changed(session_id_, {}, true);
  });
}
}  // namespace patchy::ui
