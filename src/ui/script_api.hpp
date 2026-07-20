#pragma once

#include "core/layer.hpp"

#include <QJSValue>
#include <QObject>
#include <QString>

#include <cstdint>

namespace patchy {
class Document;
}

namespace patchy::ui {

class ScriptEngineHost;

// The QObject wrappers the scripting engine exposes to JS (docs/scripting.md).
// Lifetime rules: the singleton objects (app/io/ui) are parented to the host
// (C++-owned); document/layer/selection wrappers are created per access with no
// parent, so the JS garbage collector owns them. Wrappers hold session ids and
// LayerIds only and re-resolve on every call, throwing a JS error when the
// target is gone: the layers vector reallocates and sessions close, so a stored
// pointer is the historical use-after-free pattern. Reads resolve through const
// documents (mutable layer accessors bump revisions on access).

// Stable script-facing blend mode ids ("normal", "multiply", ...). Append-only,
// aligned with the BlendMode enum; these are a persistence-adjacent contract
// (scripts in the wild will hard-code them), so never rename one.
[[nodiscard]] QString script_blend_mode_id(BlendMode mode);
[[nodiscard]] bool script_blend_mode_from_id(const QString& id, BlendMode* mode);

class ScriptLayerObject : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString name READ name WRITE set_name)
  Q_PROPERTY(double opacity READ opacity WRITE set_opacity)
  Q_PROPERTY(bool visible READ visible WRITE set_visible)
  Q_PROPERTY(QString blendMode READ blend_mode WRITE set_blend_mode)
  Q_PROPERTY(bool locked READ locked WRITE set_locked)
  Q_PROPERTY(int x READ x WRITE set_x)
  Q_PROPERTY(int y READ y WRITE set_y)
  Q_PROPERTY(QJSValue bounds READ bounds)
  Q_PROPERTY(bool isGroup READ is_group)
  Q_PROPERTY(bool isText READ is_text)
  Q_PROPERTY(QJSValue children READ children)
  Q_PROPERTY(QString text READ text WRITE set_text)

public:
  ScriptLayerObject(ScriptEngineHost& host, std::int64_t session_id, LayerId layer_id);

  [[nodiscard]] QString name() const;
  void set_name(const QString& name);
  [[nodiscard]] double opacity() const;  // 0..100
  void set_opacity(double opacity);
  [[nodiscard]] bool visible() const;
  void set_visible(bool visible);
  [[nodiscard]] QString blend_mode() const;
  void set_blend_mode(const QString& mode);
  [[nodiscard]] bool locked() const;
  void set_locked(bool locked);
  [[nodiscard]] int x() const;
  void set_x(int x);
  [[nodiscard]] int y() const;
  void set_y(int y);
  [[nodiscard]] QJSValue bounds() const;
  [[nodiscard]] bool is_group() const;
  [[nodiscard]] bool is_text() const;
  [[nodiscard]] QJSValue children() const;
  [[nodiscard]] QString text() const;
  void set_text(const QString& text);

  Q_INVOKABLE void moveTo(int x, int y);
  Q_INVOKABLE QJSValue duplicate();
  Q_INVOKABLE void remove();
  Q_INVOKABLE void fill(const QString& color);
  Q_INVOKABLE void fillRect(int x, int y, int width, int height, const QString& color);
  Q_INVOKABLE void applyFilter(const QString& filterId, const QJSValue& params = QJSValue());
  Q_INVOKABLE QJSValue getPixels();
  Q_INVOKABLE void setPixels(const QJSValue& imageData);

  [[nodiscard]] LayerId layer_id() const noexcept { return layer_id_; }
  [[nodiscard]] std::int64_t session_id() const noexcept { return session_id_; }

private:
  // Const resolution for reads; nullptr (after a thrown JS error) when gone.
  [[nodiscard]] const Layer* read_layer() const;
  // Mutation resolution: undo snapshot bookkeeping + non-const layer, or null.
  [[nodiscard]] Layer* write_layer();

  ScriptEngineHost& host_;
  std::int64_t session_id_{0};
  LayerId layer_id_{0};
};

class ScriptSelectionObject : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool exists READ exists)
  Q_PROPERTY(QJSValue bounds READ bounds)

public:
  ScriptSelectionObject(ScriptEngineHost& host, std::int64_t session_id);

  [[nodiscard]] bool exists() const;
  [[nodiscard]] QJSValue bounds() const;
  Q_INVOKABLE void selectAll();
  Q_INVOKABLE void deselect();
  Q_INVOKABLE void selectRect(int x, int y, int width, int height);
  Q_INVOKABLE void selectEllipse(int x, int y, int width, int height);

private:
  ScriptEngineHost& host_;
  std::int64_t session_id_{0};
};

class ScriptDocumentObject : public QObject {
  Q_OBJECT
  Q_PROPERTY(int width READ width)
  Q_PROPERTY(int height READ height)
  Q_PROPERTY(QString name READ name)
  Q_PROPERTY(QString path READ path)
  Q_PROPERTY(double resolution READ resolution)
  Q_PROPERTY(QJSValue layers READ layers)
  Q_PROPERTY(QJSValue activeLayer READ active_layer WRITE set_active_layer)
  Q_PROPERTY(QJSValue selection READ selection)

public:
  ScriptDocumentObject(ScriptEngineHost& host, std::int64_t session_id);

  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;
  [[nodiscard]] QString name() const;
  [[nodiscard]] QString path() const;
  [[nodiscard]] double resolution() const;
  [[nodiscard]] QJSValue layers() const;
  [[nodiscard]] QJSValue active_layer() const;
  void set_active_layer(const QJSValue& layer);
  [[nodiscard]] QJSValue selection() const;

  Q_INVOKABLE QJSValue addLayer(const QString& name);
  Q_INVOKABLE QJSValue addTextLayer(const QString& text, const QJSValue& options = QJSValue());
  Q_INVOKABLE QJSValue findLayer(const QString& name);
  Q_INVOKABLE void flatten();
  Q_INVOKABLE void resizeImage(int width, int height);
  Q_INVOKABLE void resizeCanvas(int width, int height);
  Q_INVOKABLE void crop(int x, int y, int width, int height);
  Q_INVOKABLE bool saveAs(const QString& path);
  Q_INVOKABLE bool exportAs(const QString& path);
  Q_INVOKABLE void close();
  Q_INVOKABLE void activate();

  [[nodiscard]] std::int64_t session_id() const noexcept { return session_id_; }

private:
  [[nodiscard]] const Document* read_document() const;
  [[nodiscard]] Document* write_document();

  ScriptEngineHost& host_;
  std::int64_t session_id_{0};
};

class ScriptAppObject : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString version READ version CONSTANT)
  Q_PROPERTY(int apiVersion READ api_version CONSTANT)
  Q_PROPERTY(QJSValue documents READ documents)
  Q_PROPERTY(QJSValue activeDocument READ active_document)
  Q_PROPERTY(bool undoEnabled READ undo_enabled WRITE set_undo_enabled)

public:
  explicit ScriptAppObject(ScriptEngineHost& host);

  [[nodiscard]] QString version() const;
  [[nodiscard]] int api_version() const noexcept { return 1; }
  [[nodiscard]] QJSValue documents() const;
  [[nodiscard]] QJSValue active_document() const;
  [[nodiscard]] bool undo_enabled() const;
  void set_undo_enabled(bool enabled);

  Q_INVOKABLE QJSValue open(const QString& path);
  Q_INVOKABLE QJSValue newDocument(int width, int height);
  Q_INVOKABLE void alert(const QString& text);
  Q_INVOKABLE QJSValue prompt(const QString& text, const QString& defaultValue = QString());

private:
  ScriptEngineHost& host_;
};

class ScriptIoObject : public QObject {
  Q_OBJECT

public:
  explicit ScriptIoObject(ScriptEngineHost& host);

  Q_INVOKABLE QString readTextFile(const QString& path);
  Q_INVOKABLE void writeTextFile(const QString& path, const QString& text);

private:
  ScriptEngineHost& host_;
};

class ScriptUiObject : public QObject {
  Q_OBJECT

public:
  explicit ScriptUiObject(ScriptEngineHost& host);

  // Options object: {width, height, title}. Returns a ScriptCanvasWindow.
  Q_INVOKABLE QJSValue createCanvas(const QJSValue& options = QJSValue());

private:
  ScriptEngineHost& host_;
};

// Shared by the wrapper implementations: a JS document/layer wrapper for the
// given identity (JS-GC-owned, parentless).
[[nodiscard]] QJSValue make_document_value(ScriptEngineHost& host, std::int64_t session_id);
[[nodiscard]] QJSValue make_layer_value(ScriptEngineHost& host, std::int64_t session_id, LayerId layer_id);

}  // namespace patchy::ui
