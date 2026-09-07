#include "ui/script_vector.hpp"
#include "ui/script_api.hpp"
#include <algorithm>

namespace patchy::ui {
using namespace script_vector;
namespace {
const DocumentPath& resolve(ScriptEngineHost& host, std::int64_t session, DocumentPathId id) {
  const auto* p = document(host, session).find_path(id);
  if (!p) { invalid("path.id"); }
  return *p;
}
DocumentPath& write_path(ScriptEngineHost& host, std::int64_t session, DocumentPathId id) {
  auto& doc = writable(host, session);
  auto* p = doc.find_path(id);
  if (!p) { invalid("path.id"); }
  return *p;
}
}
namespace script_vector {
QJSValue path_value(ScriptEngineHost& host, std::int64_t session, DocumentPathId id) {
  return host.engine()->newQObject(new ScriptPathObject(host, session, id));
}
}
ScriptPathObject::ScriptPathObject(ScriptEngineHost& host, std::int64_t session, DocumentPathId path)
    : host_(host), session_(session), path_(path) {}
QString ScriptPathObject::id() const {
  return guarded(host_, [&] { return QString::number(resolve(host_, session_, path_).id()); });
}
QString ScriptPathObject::name() const {
  return guarded(host_, [&] { return QString::fromStdString(resolve(host_, session_, path_).name()); });
}
QString ScriptPathObject::kind() const {
  return guarded(host_, [&] { return QString(resolve(host_, session_, path_).kind() == DocumentPathKind::Work ? "work" : "saved"); });
}
void ScriptPathObject::set_name(const QString& name) {
  guarded(host_, [&] {
    const auto& current = resolve(host_, session_, path_);
    if (current.kind() != DocumentPathKind::Saved) { invalid("path.kind"); }
    if (current.name() == name.toStdString()) { return; }
    write_path(host_, session_, path_).set_name(name.toStdString());
    host_.note_vector_changed(session_);
  });
}
QJSValue ScriptPathObject::getPath() const {
  return guarded(host_, [&] { return to_js(host_, path_json(resolve(host_, session_, path_).path())); });
}
void ScriptPathObject::setPath(const QJSValue& data) {
  guarded(host_, [&] {
    auto parsed = parse_path(object(data));
    const auto& old = resolve(host_, session_, path_).path();
    parsed.fill_rule_value = old.fill_rule_value; parsed.initial_fill_value = old.initial_fill_value;
    if (old == parsed) { return; }
    write_path(host_, session_, path_).set_path(std::move(parsed));
    host_.note_vector_changed(session_);
  });
}
void ScriptPathObject::transform(const QJSValue& value) {
  guarded(host_, [&] {
    const auto m = matrix(value);
    auto path = resolve(host_, session_, path_).path();
    validate_transformed_path(path, m);
    transform_vector_path(path, m);
    if (path == resolve(host_, session_, path_).path()) { return; }
    write_path(host_, session_, path_).set_path(std::move(path));
    host_.note_vector_changed(session_);
  });
}
QJSValue ScriptPathObject::duplicate(const QString& name) {
  return guarded(host_, [&] {
    const auto source = resolve(host_, session_, path_);
    auto& doc = writable(host_, session_);
    const auto id = doc.allocate_path_id();
    DocumentPath copy(id, name.isEmpty() ? source.name() : name.toStdString(), DocumentPathKind::Saved, source.path());
    copy.mark_dirty(); doc.add_path(std::move(copy));
    host_.note_vector_changed(session_);
    return path_value(host_, session_, id);
  });
}
void ScriptPathObject::remove() {
  guarded(host_, [&] {
    (void)resolve(host_, session_, path_);
    auto& doc = writable(host_, session_); doc.remove_path(path_);
    host_.note_vector_changed(session_);
  });
}
void ScriptPathObject::moveTo(double requested_index) {
  guarded(host_, [&] {
    const auto index = integer(QJsonObject{{"index", requested_index}}, "index", 0, 0, 100000);
    if (resolve(host_, session_, path_).kind() != DocumentPathKind::Saved) { invalid("path.kind"); }
    auto paths = document(host_, session_).paths();
    std::vector<std::size_t> positions;
    std::vector<DocumentPath> saved;
    for (std::size_t i = 0; i < paths.size(); ++i) {
      if (paths[i].kind() == DocumentPathKind::Saved) { positions.push_back(i); saved.push_back(paths[i]); }
    }
    if (index < 0 || static_cast<std::size_t>(index) >= saved.size()) { invalid("path.index"); }
    const auto it = std::find_if(saved.begin(), saved.end(), [&](const auto& p) { return p.id() == path_; });
    if (std::distance(saved.begin(), it) == index) { return; }
    auto moving = *it; saved.erase(it); saved.insert(saved.begin() + index, std::move(moving));
    for (std::size_t i = 0; i < saved.size(); ++i) { paths[positions[i]] = std::move(saved[i]); }
    writable(host_, session_).paths() = std::move(paths);
    host_.note_vector_changed(session_);
  });
}
void ScriptPathObject::activate() {
  guarded(host_, [&] { (void)resolve(host_, session_, path_); host_.activate_document_path(session_, path_); });
}
void ScriptPathObject::save(const QString& name) {
  guarded(host_, [&] {
    const auto& before = resolve(host_, session_, path_);
    if (before.kind() == DocumentPathKind::Saved) { set_name(name); return; }
    auto paths = document(host_, session_).paths();
    const auto it = std::find_if(paths.begin(), paths.end(), [&](const auto& p) { return p.id() == path_; });
    auto saved = *it; paths.erase(it); saved.set_kind(DocumentPathKind::Saved); saved.set_name(name.toStdString());
    paths.push_back(std::move(saved));
    writable(host_, session_).paths() = std::move(paths);
    host_.note_vector_changed(session_);
  });
}
QJSValue ScriptDocumentObject::paths() const {
  return guarded(host_, [&] {
    const auto& values = document(host_, session_id_).paths();
    auto result = host_.engine()->newArray(static_cast<quint32>(values.size()));
    for (std::size_t i = 0; i < values.size(); ++i) { result.setProperty(static_cast<quint32>(i), path_value(host_, session_id_, values[i].id())); }
    return result;
  });
}
QJSValue ScriptDocumentObject::work_path() const {
  return guarded(host_, [&] {
    for (const auto& p : document(host_, session_id_).paths()) { if (p.kind() == DocumentPathKind::Work) { return path_value(host_, session_id_, p.id()); } }
    return QJSValue(QJSValue::NullValue);
  });
}
QJSValue ScriptDocumentObject::clipping_path() const {
  return guarded(host_, [&] {
    for (const auto& p : document(host_, session_id_).paths()) { if (p.is_clipping_path()) { return path_value(host_, session_id_, p.id()); } }
    return QJSValue(QJSValue::NullValue);
  });
}
void ScriptDocumentObject::set_clipping_path(const QJSValue& value) {
  guarded(host_, [&] {
    std::optional<DocumentPathId> id;
    if (!value.isNull()) {
      const auto* path = qobject_cast<ScriptPathObject*>(value.toQObject());
      if (!path || path->session_id() != session_id_ || resolve(host_, session_id_, path->path_id()).kind() != DocumentPathKind::Saved) { invalid("clippingPath"); }
      id = path->path_id();
    }
    bool changed = false;
    for (const auto& p : document(host_, session_id_).paths()) { changed |= p.is_clipping_path() != (id && p.id() == *id); }
    if (!changed) { return; }
    for (auto& p : writable(host_, session_id_).paths()) { p.set_clipping_path(id && p.id() == *id); }
    host_.note_vector_changed(session_id_);
  });
}
QJSValue ScriptDocumentObject::getPath(const QString& text) const {
  return guarded(host_, [&] {
    bool ok = false; const auto id = text.toULongLong(&ok);
    if (!ok) { invalid("path.id"); }
    (void)resolve(host_, session_id_, id);
    return path_value(host_, session_id_, id);
  });
}
QJSValue ScriptDocumentObject::addPath(const QString& name, const QJSValue& data) {
  return guarded(host_, [&] {
    auto path = parse_path(object(data));
    (void)document(host_, session_id_);
    auto& doc = writable(host_, session_id_);
    const auto id = doc.allocate_path_id();
    DocumentPath created(id, name.toStdString(), DocumentPathKind::Saved, std::move(path));
    created.mark_dirty(); doc.add_path(std::move(created));
    host_.note_vector_changed(session_id_);
    return path_value(host_, session_id_, id);
  });
}
QJSValue ScriptDocumentObject::setWorkPath(const QJSValue& data) {
  return guarded(host_, [&] {
    auto path = parse_path(object(data));
    std::optional<DocumentPathId> id;
    for (const auto& p : document(host_, session_id_).paths()) {
      if (p.kind() == DocumentPathKind::Work) {
        id = p.id();
        path.fill_rule_value = p.path().fill_rule_value; path.initial_fill_value = p.path().initial_fill_value;
        if (p.path() == path) { return path_value(host_, session_id_, *id); }
      }
    }
    auto& doc = writable(host_, session_id_);
    if (id) { doc.find_path(*id)->set_path(std::move(path)); }
    else {
      id = doc.allocate_path_id();
      DocumentPath created(*id, ScriptEngineHost::tr("Work Path").toStdString(), DocumentPathKind::Work, std::move(path));
      created.mark_dirty(); doc.add_path(std::move(created));
    }
    host_.note_vector_changed(session_id_);
    return path_value(host_, session_id_, *id);
  });
}
void ScriptSelectionObject::fromPath(const QJSValue& data, const QJSValue& options) {
  guarded(host_, [&] {
    const auto path = parse_path(object(data), false);
    const auto args = object(options, true); keys(args, {"operation", "feather", "antialias"});
    const auto feather = number(args, "feather", 0, 0, 250);
    const auto aa = boolean(args, "antialias", true);
    const auto op = string(args, "operation", "replace");
    if (!QStringList{"replace", "add", "subtract", "intersect"}.contains(op)) { invalid("operation"); }
    host_.select_vector_path(session_id_, path, feather, aa, op);
  });
}
QJSValue ScriptSelectionObject::toPath(const QJSValue& options) const {
  return guarded(host_, [&] {
    const auto args = object(options, true); keys(args, {"tolerance"});
    return to_js(host_, path_json(host_.selection_vector_path(session_id_, number(args, "tolerance", 2, 0.5, 10))));
  });
}
}  // namespace patchy::ui
