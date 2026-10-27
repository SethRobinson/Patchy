#pragma once

#include <QJSValue>
#include <QObject>
#include <QString>

namespace patchy::ui {

class ScriptEngineHost;

// patchy.perspective: Filter > Perspective Planes from scripts (docs/perspective-planes.md).
// Every edit is clipped to the one plane it is given, as in the workspace.
class ScriptPerspectiveObject : public QObject {
  Q_OBJECT

public:
  explicit ScriptPerspectiveObject(ScriptEngineHost& host);

  // "" for usable corners, else "tooSmall", "crossing" or "concave".
  Q_INVOKABLE QString checkPlane(const QJSValue& corners);
  // The document's planes (what the workspace shows): [{corners, grid}].
  Q_INVOKABLE QJSValue getPlanes(const QJSValue& document);
  Q_INVOKABLE void setPlanes(const QJSValue& document, const QJSValue& planes);
  // Each writes one edit on `layer` (the active layer of the active document) and returns
  // the layer written: a new layer above it, or the layer itself with target "layer".
  Q_INVOKABLE QJSValue paste(const QJSValue& layer, const QJSValue& options);
  // Place Layer: the layer's own content onto the plane ("fill" by default), replacing the
  // layer (target "newLayer" moves it to a new layer above instead).
  Q_INVOKABLE QJSValue placeLayer(const QJSValue& layer, const QJSValue& options);
  Q_INVOKABLE QJSValue clone(const QJSValue& layer, const QJSValue& options);
  Q_INVOKABLE QJSValue paint(const QJSValue& layer, const QJSValue& options);

private:
  QJSValue stroke(const QJSValue& layer, const QJSValue& options, bool clone);
  ScriptEngineHost& host_;
};

}  // namespace patchy::ui
