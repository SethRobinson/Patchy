// First-class automation services shared by JavaScript and the local MCP connector.
#include "ui/script_engine.hpp"
#include "ui/script_api.hpp"
#include "ui/main_window.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/qt_geometry.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "formats/document_flatten.hpp"
#include <QJSEngine>
#include <QApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QTextEdit>
#include <QGuiApplication>
#include <QJSValueIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QScopeGuard>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace patchy::ui {
namespace {
QJsonObject rect_json(const QRect& r) {
  return {{"x", r.x()}, {"y", r.y()}, {"width", r.width()}, {"height", r.height()}};
}

void keys(const QJSValue& object, const QStringList& allowed) {
  if (!object.isObject() || object.isArray()) { throw std::invalid_argument("object"); }
  QJSValueIterator it(object);
  while (it.hasNext()) {
    it.next();
    if (!allowed.contains(it.name())) { throw std::invalid_argument(it.name().toStdString()); }
  }
}

double number(const QJSValue& obj, const QString& key, double fallback, double min, double max) {
  const auto value = obj.property(key);
  if (value.isUndefined()) { return fallback; }
  const double n = value.toNumber();
  if (!value.isNumber() || !std::isfinite(n) || n < min || n > max) {
    throw std::invalid_argument(key.toStdString());
  }
  return n;
}

int integer(const QJSValue& obj, const QString& key, int fallback, int min, int max) {
  const double n = number(obj, key, fallback, min, max);
  if (n != std::floor(n)) { throw std::invalid_argument(key.toStdString()); }
  return static_cast<int>(n);
}

QJsonArray layer_state(const std::vector<Layer>& layers) {
  QJsonArray result;
  for (const auto& layer : layers) {
    result.append(QJsonObject{{"id", QString::number(layer.id())},
                            {"renderRevision", QString::number(layer.render_revision())},
                            {"name", QString::fromStdString(layer.name())},
                            {"bounds", rect_json(to_qrect(layer.bounds()))},
                            {"visible", layer.visible()}, {"locked", layer.lock_flags() != kLayerLockNone},
                            {"opacity", layer.opacity() * 100.0},
                            {"blendMode", script_blend_mode_id(layer.blend_mode())},
                            {"isGroup", layer.kind() == LayerKind::Group}, {"isText", layer_is_text(layer)},
                            {"children", layer_state(layer.children())}});
  }
  return result;
}
}  // namespace

void ScriptEngineHost::interrupt_from_any_thread() {
  const std::lock_guard lock(interrupt_mutex_);
  external_interrupt_ = true;
  if (engine_) { engine_->setInterrupted(true); }
}

void ScriptEngineHost::clear_external_interrupt() {
  const std::lock_guard lock(interrupt_mutex_);
  external_interrupt_ = false;
}

void ScriptEngineHost::scriptSetResult(const QString& json) {
  pump_progress_indicator();
  if (json.size() > 4 * 1024 * 1024) {
    throw_js_error(tr("The script result is too large. Save large data to a file."));
    return;
  }
  const auto document = QJsonDocument::fromJson(json.toUtf8());
  if (!document.isArray() || document.array().size() != 1) {
    throw_js_error(tr("The script result must be JSON serializable."));
    return;
  }
  last_result_ = document.array().at(0);
}

bool ScriptEngineHost::session_modified(std::int64_t id) const {
  const auto* session = window_.session_with_id(id);
  return session && window_.session_is_modified(*session);
}

bool ScriptEngineHost::session_can_undo(std::int64_t id, bool redo) const {
  const auto* session = window_.session_with_id(id);
  return session && !(redo ? session->redo_stack : session->undo_stack).empty();
}

bool ScriptEngineHost::restore_session_history(std::int64_t id, bool redo) {
  // History operations are standalone: mixing a restore with new mutations in one
  // run would invalidate the one-snapshot bookkeeping.
  if (run_ && !run_->snapshotted_sessions.empty()) {
    throw_js_error(tr("Undo and redo must run before any edits in a script."));
    return false;
  }
  if (!session_can_undo(id, redo)) { return false; }
  activate_session(id);
  if (redo) { window_.redo(); } else { window_.undo(); }
  return true;
}

QJsonObject ScriptEngineHost::automation_state() const {
  QJsonArray documents;
  for (const auto id : session_ids()) {
    const auto* doc = session_document_const(id);
    QJsonObject selection{{"exists", has_selection(id)}};
    if (has_selection(id)) { selection["bounds"] = rect_json(selection_region(id).boundingRect()); }
    documents.append(QJsonObject{{"id", QString::number(id)}, {"name", session_title(id)},
        {"revision", QString::number(window_.session_with_id(id)->revision)},
        {"historyStateId", QString::number(window_.session_with_id(id)->current_state_id)},
        {"path", session_file_path(id)}, {"width", doc->width()}, {"height", doc->height()},
        {"modified", session_modified(id)}, {"canUndo", session_can_undo(id)},
        {"canRedo", session_can_undo(id, true)}, {"layers", layer_state(doc->layers())},
        {"activeLayerId", doc->active_layer_id() ? QJsonValue(QString::number(*doc->active_layer_id())) : QJsonValue(QJsonValue::Null)},
        {"selection", selection}});
  }
  return {{"activeDocumentId", active_session_id() ? QJsonValue(QString::number(active_session_id())) : QJsonValue(QJsonValue::Null)},
          {"documents", documents}};
}

QString ScriptEngineHost::automation_fingerprint() const {
  // Revisions and selection geometry, never a scan of layer pixel buffers.
  // The active document/layer and save state are deliberately part of the token.
  QByteArray bytes = QJsonDocument(automation_state()).toJson(QJsonDocument::Compact);
  QDataStream stream(&bytes, QIODevice::Append);
  stream.setVersion(QDataStream::Qt_6_0);
  for (const auto id : session_ids()) {
    stream << selection_region(id);
    const auto* doc = session_document_const(id);
    stream << quint64(doc->palette_editing() ? doc->palette_editing()->palette_revision : 0);
    for (const auto& channel : doc->channels()) { stream << quint64(channel.id()) << quint64(channel.content_revision()); }
    for (const auto& path : doc->paths()) { stream << quint64(path.id()) << quint64(path.content_revision()); }
    if (const auto* canvas = window_.session_with_id(id)->canvas) {
      stream << qint32(canvas->layer_edit_target()) << canvas->quick_mask_active()
             << quint64(canvas->quick_mask_revision()) << quint64(canvas->smart_filter_mask_revision());
    }
  }
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool ScriptEngineHost::automation_ready() const {
  if (run_active() || window_.preview_dialog_edit_locked() || QApplication::activeModalWidget()) { return false; }
  for (const auto& session : window_.sessions_) {
    const auto* canvas = session->canvas;
    if (canvas && (canvas->pointer_gesture_active() || canvas->free_transform_active() ||
                   canvas->warp_transform_active() || canvas->path_transform_active() || canvas->crop_session_active() ||
                   canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")))) { return false; }
  }
  return true;
}

QImage ScriptEngineHost::render_preview(std::int64_t id, const QJsonObject& options, QJsonObject* metadata) {
  const auto* doc = session_document_const(id);
  if (!doc) { throw std::runtime_error(tr("The document is no longer open.").toStdString()); }
  const QStringList allowed{"rect", "maxWidth", "maxHeight", "nearestNeighbor"};
  for (auto it = options.begin(); it != options.end(); ++it) {
    if (!allowed.contains(it.key())) { throw std::runtime_error(tr("Unknown preview option: %1").arg(it.key()).toStdString()); }
  }
  const auto dimension = [&](const char* key) {
    const auto v = options.value(QLatin1String(key));
    const auto n = v.isUndefined() ? 1024.0 : v.toDouble(-1);
    if (!std::isfinite(n) || n < 1 || n > 4096 || n != std::floor(n)) {
      throw std::runtime_error(tr("Preview dimensions must be integers from 1 to 4096.").toStdString());
    }
    return static_cast<int>(n);
  };
  const int max_width = dimension("maxWidth");
  const int max_height = dimension("maxHeight");
  QRect rect(0, 0, doc->width(), doc->height());
  if (options.contains("rect")) {
    const auto value = options.value("rect");
    const auto r = value.toObject();
    if (!value.isObject() || r.size() != 4) { throw std::runtime_error(tr("Invalid preview rectangle.").toStdString()); }
    for (const auto& key : {"x", "y", "width", "height"}) {
      const auto v = r.value(QLatin1String(key));
      const auto n = v.toDouble(-1);
      if (!v.isDouble() || !std::isfinite(n) || n < 0 || n > 1000000 || n != std::floor(n)) {
        throw std::runtime_error(tr("Invalid preview rectangle.").toStdString());
      }
    }
    const QRect requested(r["x"].toInt(), r["y"].toInt(), r["width"].toInt(), r["height"].toInt());
    rect = rect.intersected(requested);
    if (rect.isEmpty()) { throw std::runtime_error(tr("The preview rectangle is outside the canvas.").toStdString()); }
  }
  if (options.contains("nearestNeighbor") && !options["nearestNeighbor"].isBool()) {
    throw std::runtime_error(tr("nearestNeighbor must be a boolean.").toStdString());
  }
  const bool nearest = options["nearestNeighbor"].toBool(false);
  const auto pixels = flatten_document_rgba8(*doc);
  QImage full(doc->width(), doc->height(), QImage::Format_RGBA8888);
  if (full.isNull()) { throw std::runtime_error(tr("Could not allocate the preview image.").toStdString()); }
  for (int y = 0; y < doc->height(); ++y) {
    std::copy_n(pixels.row(y).begin(), static_cast<std::size_t>(doc->width()) * 4, full.scanLine(y));
  }
  auto image = full.copy(rect);
  // Nearest-neighbor can enlarge sprites. Ordinary previews only shrink.
  if (nearest || image.width() > max_width || image.height() > max_height) {
    image = image.scaled(max_width, max_height, Qt::KeepAspectRatio,
                         nearest ? Qt::FastTransformation : Qt::SmoothTransformation);
  }
  *metadata = {{"documentId", QString::number(id)}, {"rect", rect_json(rect)},
               {"width", image.width()}, {"height", image.height()},
               {"scaleX", static_cast<double>(image.width()) / rect.width()},
               {"scaleY", static_cast<double>(image.height()) / rect.height()},
               {"offscreen", QGuiApplication::platformName() == QStringLiteral("offscreen")}};
  return image;
}

void ScriptEngineHost::draw_strokes(std::int64_t session_id, LayerId layer_id, const QJSValue& input) {
  try {
    if (!input.isArray()) { throw std::invalid_argument("strokes"); }
    const auto length = input.property("length").toUInt();
    if (length == 0 || length > 1000) { throw std::invalid_argument("strokes.length (1..1000)"); }
    std::vector<ScriptStroke> strokes;
    std::size_t total_points = 0;
    for (quint32 i = 0; i < length; ++i) {
      const auto value = input.property(i);
      keys(value, {"points", "tool", "color", "size", "opacity", "flow", "softness", "seed", "sizeJitter", "scatter"});
      ScriptStroke stroke;
      const auto tool = value.property("tool");
      if (!tool.isUndefined() && (!tool.isString() || (tool.toString() != "brush" && tool.toString() != "eraser"))) {
        throw std::invalid_argument("tool");
      }
      stroke.erase = tool.toString() == "eraser";
      const auto color = value.property("color");
      if (!color.isUndefined()) {
        if (!color.isString()) { throw std::invalid_argument("color"); }
        stroke.color = QColor(color.toString());
        if (!stroke.color.isValid()) { throw std::invalid_argument("color"); }
      }
      stroke.size = integer(value, "size", 1, 1, 1024);
      stroke.opacity = integer(value, "opacity", 100, 1, 100);
      stroke.flow = integer(value, "flow", 100, 1, 100);
      stroke.softness = integer(value, "softness", 0, 0, 100);
      const double seed = number(value, "seed", 0, 0, 4294967295.0);
      if (seed != std::floor(seed)) { throw std::invalid_argument("seed"); }
      stroke.dynamics.seed = static_cast<std::uint32_t>(seed);
      stroke.dynamics.size_jitter = number(value, "sizeJitter", 0, 0, 1);
      stroke.dynamics.scatter = number(value, "scatter", 0, 0, 10);
      const auto points = value.property("points");
      if (!points.isArray()) { throw std::invalid_argument("points"); }
      const auto count = points.property("length").toUInt();
      total_points += count;
      if (count == 0 || total_points > 100000) { throw std::invalid_argument("points.length (1..100000 total)"); }
      for (quint32 p = 0; p < count; ++p) {
        const auto point = points.property(p);
        keys(point, {"x", "y", "pressure"});
        if (point.property("x").isUndefined() || point.property("y").isUndefined()) { throw std::invalid_argument("x/y"); }
        ScriptStrokePoint sample;
        sample.position = {number(point, "x", 0, -100000, 100000), number(point, "y", 0, -100000, 100000)};
        if (!point.property("pressure").isUndefined()) { sample.pressure = static_cast<float>(number(point, "pressure", 1, 0, 1)); }
        stroke.points.push_back(sample);
      }
      strokes.push_back(std::move(stroke));
    }
    auto* session = window_.session_with_id(session_id);
    const auto* doc = session_document_const(session_id);
    const auto* layer = doc ? doc->find_layer(layer_id) : nullptr;
    if (!session || !layer || layer->kind() == LayerKind::Group ||
        layer_effectively_locks_image_pixels(doc->layers(), layer_id) || layer_is_text(*layer) ||
        layer_is_smart_object(*layer) || layer_is_vector_shape(*layer) ||
        layer->pixels().format().bit_depth != BitDepth::UInt8 || layer->pixels().format().channels < 3) {
      throw_js_error(tr("Strokes require an unlocked 8-bit pixel layer."));
      return;
    }
    if (!prepare_mutation(session_id)) { return; }
    const auto previous = doc->active_layer_id();
    session->document.set_active_layer(layer_id);
    const auto restore = qScopeGuard([&] {
      if (previous) { session->document.set_active_layer(*previous); }
      else { session->document.clear_active_layer(); }
    });
    for (const auto& stroke : strokes) {
      if (engine_ && engine_->isInterrupted()) { break; }
      const auto dirty = session->canvas->paint_script_stroke(stroke, [this] {
        pump_progress_indicator();
        return engine_ && engine_->isInterrupted();
      });
      note_pixels_changed(session_id, dirty);
    }
  } catch (const std::exception& e) {
    throw_js_error(tr("Invalid stroke argument: %1").arg(QString::fromUtf8(e.what())));
  }
}

QString ScriptLayerObject::id() const { return read_layer() ? QString::number(layer_id_) : QString(); }
void ScriptLayerObject::drawStrokes(const QJSValue& strokes) { host_.draw_strokes(session_id_, layer_id_, strokes); }
QString ScriptDocumentObject::id() const { return read_document() ? QString::number(session_id_) : QString(); }
bool ScriptDocumentObject::modified() const { return read_document() && host_.session_modified(session_id_); }
bool ScriptDocumentObject::can_undo() const { return read_document() && host_.session_can_undo(session_id_); }
bool ScriptDocumentObject::can_redo() const { return read_document() && host_.session_can_undo(session_id_, true); }
bool ScriptDocumentObject::undo() { return read_document() && host_.restore_session_history(session_id_, false); }
bool ScriptDocumentObject::redo() { return read_document() && host_.restore_session_history(session_id_, true); }

QJSValue ScriptDocumentObject::getLayer(const QString& id) {
  bool ok = false;
  const auto value = id.toULongLong(&ok);
  const auto* doc = read_document();
  if (!ok || !doc || !doc->find_layer(value)) {
    host_.throw_js_error(ScriptEngineHost::tr("Unknown layer ID: %1").arg(id));
    return {};
  }
  return make_layer_value(host_, session_id_, value);
}

QJSValue ScriptAppObject::getDocument(const QString& id) {
  bool ok = false;
  const auto value = id.toLongLong(&ok);
  if (!ok || !host_.session_document_const(value)) {
    host_.throw_js_error(ScriptEngineHost::tr("Unknown document ID: %1").arg(id));
    return {};
  }
  return make_document_value(host_, value);
}

QJSValue ScriptDocumentObject::renderPreview(const QString& path, const QJSValue& options) {
  try {
    if (path.isEmpty()) { throw std::runtime_error(ScriptEngineHost::tr("A preview output path is required.").toStdString()); }
    if (!options.isUndefined() && (!options.isObject() || options.isArray())) {
      throw std::runtime_error(ScriptEngineHost::tr("Preview options must be an object.").toStdString());
    }
    const auto params = QJsonObject::fromVariantMap(options.toVariant().toMap());
    QJsonObject metadata;
    const auto image = host_.render_preview(session_id_, params, &metadata);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
      throw std::runtime_error(ScriptEngineHost::tr("Could not save the preview: %1").arg(path).toStdString());
    }
    metadata["path"] = path;
    return host_.engine()->toScriptValue(metadata.toVariantMap());
  } catch (const std::exception& e) {
    host_.throw_js_error(QString::fromUtf8(e.what()));
    return {};
  }
}

}  // namespace patchy::ui
