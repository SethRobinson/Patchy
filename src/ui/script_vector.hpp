#pragma once

#include "core/document.hpp"
#include "core/document_path.hpp"
#include "core/vector_shape.hpp"
#include "ui/script_engine.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QJSEngine>
#include <QObject>
#include <QStringList>
#include <array>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace patchy::ui {

class ScriptPathObject : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString id READ id)
  Q_PROPERTY(QString name READ name WRITE set_name)
  Q_PROPERTY(QString kind READ kind)
public:
  ScriptPathObject(ScriptEngineHost& host, std::int64_t session, DocumentPathId path);
  QString id() const;
  QString name() const;
  QString kind() const;
  void set_name(const QString& name);
  Q_INVOKABLE QJSValue getPath() const;
  Q_INVOKABLE void setPath(const QJSValue& data);
  Q_INVOKABLE void transform(const QJSValue& matrix);
  Q_INVOKABLE QJSValue duplicate(const QString& name = QString());
  Q_INVOKABLE void remove();
  Q_INVOKABLE void moveTo(double index);
  Q_INVOKABLE void activate();
  Q_INVOKABLE void save(const QString& name);
  std::int64_t session_id() const { return session_; }
  DocumentPathId path_id() const { return path_; }
private:
  ScriptEngineHost& host_;
  std::int64_t session_;
  DocumentPathId path_;
};

namespace script_vector {
// All parsing precedes prepare_mutation. Failures identify a public option or target.
[[noreturn]] void invalid(const QString& field);
template<class F> auto guarded(ScriptEngineHost& host, F&& function) -> std::invoke_result_t<F> {
  using Result = std::invoke_result_t<F>;
  try { return function(); }
  catch (const std::exception& error) {
    host.throw_js_error(ScriptEngineHost::tr("Invalid vector option or target: %1.").arg(QString::fromUtf8(error.what())));
    if constexpr (!std::is_void_v<Result>) { return Result{}; }
  }
}
QJsonObject object(const QJSValue& value, bool optional = false);
void keys(const QJsonObject& value, const QStringList& allowed);
double number(const QJsonObject& value, const QString& key, double fallback,
              double minimum = -100000.0, double maximum = 100000.0);
double required_number(const QJsonObject& value, const QString& key,
                       double minimum = -100000.0, double maximum = 100000.0);
int integer(const QJsonObject& value, const QString& key, int fallback, int minimum, int maximum);
bool boolean(const QJsonObject& value, const QString& key, bool fallback);
QString string(const QJsonObject& value, const QString& key, const QString& fallback = {});
QJsonObject child_object(const QJsonObject& value, const QString& key);
QJSValue to_js(ScriptEngineHost& host, const QJsonValue& value);
const Document& document(ScriptEngineHost& host, std::int64_t session);
const Layer& layer(ScriptEngineHost& host, std::int64_t session, LayerId id, bool editable = false);
Document& writable(ScriptEngineHost& host, std::int64_t session);
VectorPath parse_path(const QJsonObject& value, bool allow_empty = true);
QJsonObject path_json(const VectorPath& path);
std::array<double, 6> matrix(const QJSValue& value);
void validate_transformed_path(const VectorPath& path, const std::array<double, 6>& matrix);
VectorShapeContent geometry(ScriptEngineHost& host, const QJsonObject& value, double resolution);
QJsonArray live_json(const std::vector<LiveShapeParams>& live);
VectorFill paint(ScriptEngineHost& host, const QJsonValue& value, PatternStore& patterns,
                 VectorFill initial = {});
QJsonObject paint_json(const VectorFill& value);
VectorStroke stroke(ScriptEngineHost& host, const QJsonObject& value, PatternStore& patterns,
                    VectorStroke initial);
QJsonObject stroke_json(const VectorStroke& value);
QJsonObject resources(ScriptEngineHost& host, const Document& doc);
QJSValue path_value(ScriptEngineHost& host, std::int64_t session, DocumentPathId id);
void erase_mask_blocks(Layer& layer);
}  // namespace script_vector
}  // namespace patchy::ui
