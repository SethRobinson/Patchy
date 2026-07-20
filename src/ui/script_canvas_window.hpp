#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QJSValue>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

class QDialog;
class QTimer;

namespace patchy::ui {

class ScriptEngineHost;
class ScriptCanvasWindow;

// The 2D drawing surface of a script canvas window: a QPainter-over-QImage
// wrapper with a deliberately small, color-string based API. C++-owned (child
// of its window object).
class ScriptCanvasGraphics : public QObject {
  Q_OBJECT

public:
  explicit ScriptCanvasGraphics(ScriptCanvasWindow& window);

  Q_INVOKABLE void clear(const QString& color);
  Q_INVOKABLE void fillRect(double x, double y, double width, double height, const QString& color);
  Q_INVOKABLE void strokeRect(double x, double y, double width, double height, const QString& color,
                              double lineWidth = 1.0);
  Q_INVOKABLE void line(double x1, double y1, double x2, double y2, const QString& color,
                        double lineWidth = 1.0);
  Q_INVOKABLE void circle(double centerX, double centerY, double radius, const QString& color,
                          bool filled = true, double lineWidth = 1.0);
  Q_INVOKABLE void text(double x, double y, const QString& value, const QString& color,
                        double sizePt = 14.0);
  // Draws layer pixels (a ScriptLayerObject) or a getPixels()-style
  // {width, height, data} object at the given surface position.
  Q_INVOKABLE void drawImage(const QJSValue& source, double x, double y);

private:
  ScriptCanvasWindow& window_;
};

// An interactive window a script can open (games, demos): a QImage surface in a
// non-modal dialog, an ~60fps onFrame callback, and keyboard/mouse callbacks
// plus an isKeyDown poll for held keys. The QObject exposed to JS deliberately
// is NOT the dialog itself, so scripts only see this surface. The host owns the
// object (scripts may drop their reference mid-game); an open window keeps the
// script run alive, and the run's teardown closes it.
class ScriptCanvasWindow : public QObject {
  Q_OBJECT
  Q_PROPERTY(int width READ surface_width CONSTANT)
  Q_PROPERTY(int height READ surface_height CONSTANT)
  Q_PROPERTY(QJSValue graphics READ graphics CONSTANT)
  Q_PROPERTY(QJSValue onFrame READ on_frame WRITE set_on_frame)
  Q_PROPERTY(QJSValue onKeyDown READ on_key_down WRITE set_on_key_down)
  Q_PROPERTY(QJSValue onKeyUp READ on_key_up WRITE set_on_key_up)
  Q_PROPERTY(QJSValue onMouseDown READ on_mouse_down WRITE set_on_mouse_down)
  Q_PROPERTY(QJSValue onMouseMove READ on_mouse_move WRITE set_on_mouse_move)
  Q_PROPERTY(QJSValue onMouseUp READ on_mouse_up WRITE set_on_mouse_up)

public:
  ScriptCanvasWindow(ScriptEngineHost& host, int width, int height, const QString& title);
  ~ScriptCanvasWindow() override;

  [[nodiscard]] int surface_width() const noexcept;
  [[nodiscard]] int surface_height() const noexcept;
  [[nodiscard]] QJSValue graphics() const;
  [[nodiscard]] QJSValue on_frame() const { return on_frame_; }
  void set_on_frame(const QJSValue& callback);
  [[nodiscard]] QJSValue on_key_down() const { return on_key_down_; }
  void set_on_key_down(const QJSValue& callback) { on_key_down_ = callback; }
  [[nodiscard]] QJSValue on_key_up() const { return on_key_up_; }
  void set_on_key_up(const QJSValue& callback) { on_key_up_ = callback; }
  [[nodiscard]] QJSValue on_mouse_down() const { return on_mouse_down_; }
  void set_on_mouse_down(const QJSValue& callback) { on_mouse_down_ = callback; }
  [[nodiscard]] QJSValue on_mouse_move() const { return on_mouse_move_; }
  void set_on_mouse_move(const QJSValue& callback) { on_mouse_move_ = callback; }
  [[nodiscard]] QJSValue on_mouse_up() const { return on_mouse_up_; }
  void set_on_mouse_up(const QJSValue& callback) { on_mouse_up_ = callback; }

  Q_INVOKABLE void close();
  Q_INVOKABLE void present();
  // Key names follow QKeySequence::toString: "Left", "Space", "A", ...
  Q_INVOKABLE bool isKeyDown(const QString& key) const;

  [[nodiscard]] QImage& surface() noexcept { return surface_; }
  [[nodiscard]] ScriptEngineHost& host() noexcept { return host_; }
  [[nodiscard]] bool is_open() const;
  // Run teardown: drop stored JS callbacks (they must not outlive the engine)
  // and close the dialog without re-entering the host.
  void release_script_state();

  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void fire_frame();
  void handle_closed();
  [[nodiscard]] static QString key_name(int key);

  ScriptEngineHost& host_;
  QPointer<QDialog> dialog_;
  QImage surface_;
  ScriptCanvasGraphics* graphics_{nullptr};
  QTimer* frame_timer_{nullptr};
  QElapsedTimer frame_clock_;
  qint64 last_frame_ms_{0};
  QSet<QString> keys_down_;
  QJSValue on_frame_;
  QJSValue on_key_down_;
  QJSValue on_key_up_;
  QJSValue on_mouse_down_;
  QJSValue on_mouse_move_;
  QJSValue on_mouse_up_;
  bool closing_{false};
};

}  // namespace patchy::ui
