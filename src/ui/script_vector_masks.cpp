#include "ui/script_vector.hpp"
#include "ui/script_api.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/vector_raster.hpp"
#include "core/pixel_tools.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/vector_operations.hpp"
#include <cmath>

namespace patchy::ui {
using namespace script_vector;
namespace {
QJsonObject mask_json(const LayerVectorMask& mask) {
  return {{"path", path_json(mask.path)}, {"enabled", !mask.disabled}, {"inverted", mask.inverted},
    {"linked", !mask.unlinked}, {"density", mask.density * 100.0 / 255.0}, {"feather", mask.feather}};
}
void commit_mask(ScriptEngineHost& host, std::int64_t session, LayerId id, Layer prepared, QRect before, bool structure) {
  const auto after = to_qrect(layer_render_bounds(std::as_const(prepared)));
  auto& doc = writable(host, session);
  (void)layer(host, session, id, true);
  *doc.find_layer(id) = std::move(prepared);
  host.note_vector_changed(session, before.united(after), structure);
}
}
QJSValue ScriptLayerObject::getVectorMask() const {
  return guarded(host_, [&] {
    const auto* mask = layer(host_, session_id_, layer_id_).vector_mask();
    return mask ? to_js(host_, mask_json(*mask)) : QJSValue(QJSValue::NullValue);
  });
}
void ScriptLayerObject::setVectorMask(const QJSValue& options) {
  guarded(host_, [&] {
    const auto args = object(options);
    keys(args, {"path", "enabled", "inverted", "linked", "density", "feather"});
    const auto& old = layer(host_, session_id_, layer_id_, true);
    if (old.vector_shape()) {
      host_.throw_js_error(ScriptEngineHost::tr("Put shape layers in a group and apply the vector mask to that group."));
      return;
    }
    const auto& doc = document(host_, session_id_);
    auto mask = old.vector_mask() ? *old.vector_mask() : LayerVectorMask{};
    if (args.contains("path")) {
      auto path = parse_path(child_object(args, "path"));
      path.fill_rule_value = mask.path.fill_rule_value; path.initial_fill_value = mask.path.initial_fill_value;
      mask.path = std::move(path);
    }
    mask.disabled = !boolean(args, "enabled", !mask.disabled);
    mask.inverted = boolean(args, "inverted", mask.inverted); mask.unlinked = !boolean(args, "linked", !mask.unlinked);
    mask.density = static_cast<std::uint8_t>(std::lround(number(args, "density", mask.density * 100.0 / 255.0, 0, 100) * 255.0 / 100.0));
    mask.feather = number(args, "feather", mask.feather, 0, 1000);
    if (old.vector_mask() && mask_json(*old.vector_mask()) == mask_json(mask)) { return; }
    const auto before = to_qrect(layer_render_bounds(old));
    const bool structure = old.vector_mask() == nullptr;
    auto prepared = old;
    prepared.set_vector_mask(std::move(mask)); mark_layer_vector_block_dirty(prepared);
    update_vector_mask_raster(prepared, Rect::from_size(doc.width(), doc.height()));
    commit_mask(host_, session_id_, layer_id_, std::move(prepared), before, structure);
  });
}
void ScriptLayerObject::removeVectorMask() {
  guarded(host_, [&] {
    const auto& old = layer(host_, session_id_, layer_id_, true);
    if (!old.vector_mask()) { return; }
    auto prepared = old; const auto before = to_qrect(layer_render_bounds(old));
    prepared.clear_vector_mask(); erase_mask_blocks(prepared);
    commit_mask(host_, session_id_, layer_id_, std::move(prepared), before, true);
  });
}
void ScriptLayerObject::transformVectorMask(const QJSValue& value) {
  guarded(host_, [&] {
    const auto m = matrix(value);
    const auto& old = layer(host_, session_id_, layer_id_, true);
    if (!old.vector_mask()) { invalid("layer.vectorMask"); }
    auto mask = *old.vector_mask(); validate_transformed_path(mask.path, m); transform_vector_path(mask.path, m);
    if (mask.path == old.vector_mask()->path) { return; }
    const auto& doc = document(host_, session_id_);
    const auto before = to_qrect(layer_render_bounds(old)); auto prepared = old;
    prepared.set_vector_mask(std::move(mask)); mark_layer_vector_block_dirty(prepared);
    update_vector_mask_raster(prepared, Rect::from_size(doc.width(), doc.height()));
    commit_mask(host_, session_id_, layer_id_, std::move(prepared), before, false);
  });
}
void ScriptLayerObject::rasterizeVectorMask() {
  guarded(host_, [&] {
    const auto& old = layer(host_, session_id_, layer_id_, true);
    if (!old.vector_mask()) { invalid("layer.vectorMask"); }
    auto prepared = old; const auto before = to_qrect(layer_render_bounds(old));
    const auto& doc = document(host_, session_id_);
    bake_vector_mask(prepared, doc.width(), doc.height());
    commit_mask(host_, session_id_, layer_id_, std::move(prepared), before, true);
  });
}
void ScriptLayerObject::strokePath(const QJSValue& data, const QJSValue& options) {
  guarded(host_, [&] {
    const auto path = parse_path(object(data), false);
    auto opts = object(options, true);
    keys(opts, {"tool", "color", "size", "opacity", "flow", "softness", "seed", "sizeJitter", "scatter", "pressure"});
    const auto pressure = number(opts, "pressure", 1, 0, 1); opts.remove("pressure");
    auto batch = host_.engine()->newArray(); quint32 index = 0; std::size_t total = 0;
    for (const auto& line : stroke_polylines_for_path(path)) {
      total += line.size();
      if (total > 100000 || index >= 1000) { invalid("strokePath.points"); }
      QJsonArray points;
      for (const auto& p : line) { points.append(QJsonObject{{"x", p.x()}, {"y", p.y()}, {"pressure", pressure}}); }
      auto entry = opts; entry["points"] = points;
      batch.setProperty(index++, to_js(host_, entry));
    }
    host_.draw_strokes(session_id_, layer_id_, batch);
  });
}
void ScriptLayerObject::fillPath(const QJSValue& data, const QJSValue& options) {
  guarded(host_, [&] {
    const auto path = parse_path(object(data), false);
    const auto args = object(options, true); keys(args, {"paint", "opacity"});
    const auto opacity = number(args, "opacity", 1, 0, 1);
    const auto& source = layer(host_, session_id_, layer_id_, true);
    if (source.kind() != LayerKind::Pixel || layer_is_text(source) || layer_is_smart_object(source) || source.vector_shape() ||
        source.pixels().format().bit_depth != BitDepth::UInt8 || source.pixels().format().channels < 3) { invalid("layer.pixels"); }
    const auto& doc = document(host_, session_id_);
    auto patterns = doc.metadata().patterns;
    VectorShapeContent content; content.path = path;
    content.fill = paint(host_, args.contains("paint") ? args["paint"] : QJsonValue("#000000"), patterns);
    auto raster = rasterize_vector_shape(content, Rect::from_size(doc.width(), doc.height()), &patterns, &source);
    if (raster.pixels.empty() || opacity == 0) { return; }
    for (int y = 0; y < raster.pixels.height(); ++y) {
      for (int x = 0; x < raster.pixels.width(); ++x) {
        auto* pixel = raster.pixels.pixel(x, y);
        pixel[3] = static_cast<std::uint8_t>(std::lround(pixel[3] * opacity));
      }
    }
    raster.pixels = host_.pixels_limited_to_selection(session_id_, raster.pixels, raster.bounds);
    auto staged = doc;
    auto* target = staged.find_layer(layer_id_);
    EditOptions edit;
    PaletteLut lut;
    PaletteSnapContext snap;
    if (doc.palette_editing()) {
      const auto& p = *doc.palette_editing(); lut.build(p.palette.colors); snap.lut = &lut; snap.alpha_threshold = p.alpha_threshold;
      snap.coverage_threshold = static_cast<float>(p.alpha_threshold) / 255.0F; edit.palette_snap = &snap;
    }
    const auto before = to_qrect(layer_render_bounds(source));
    const auto changed = paint_pixel_block(*target, raster.pixels, raster.bounds, edit);
    if (changed.empty()) { return; }
    const auto after = to_qrect(layer_render_bounds(std::as_const(*target)));
    auto& mutable_doc = writable(host_, session_id_);
    (void)layer(host_, session_id_, layer_id_, true);
    *mutable_doc.find_layer(layer_id_) = std::move(*target);
    host_.note_pixels_changed(session_id_, before.united(after));
  });
}
}  // namespace patchy::ui
