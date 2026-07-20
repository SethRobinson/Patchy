// Script canvas windows (docs/scripting.md): the interactive surface scripts
// draw games/demos into. The JS-visible QObject owns a non-modal QDialog (via
// run_non_modal_dialog, per the dialog rules) holding a 1:1 QImage blit widget;
// input events and the ~60fps frame tick enter script code exclusively through
// ScriptEngineHost::call_script_callback so every entry shares the watchdog and
// error handling. An open window keeps the script run alive.

#include "ui/script_canvas_window.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/main_window.hpp"
#include "ui/script_api.hpp"
#include "ui/script_engine.hpp"

#include <QDialog>
#include <QElapsedTimer>
#include <QJSEngine>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace patchy::ui {

namespace {

// Plain (non-Q_OBJECT) widget that blits the window's surface image 1:1.
class SurfaceWidget : public QWidget {
public:
  explicit SurfaceWidget(QImage& surface, QWidget* parent) : QWidget(parent), surface_(surface) {
    setFixedSize(surface_.size());
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.drawImage(0, 0, surface_);
  }

private:
  QImage& surface_;
};

}  // namespace

// ---------------------------------------------------------------------------
// ScriptCanvasGraphics

ScriptCanvasGraphics::ScriptCanvasGraphics(ScriptCanvasWindow& window)
    : QObject(&window), window_(window) {}

void ScriptCanvasGraphics::clear(const QString& color) {
  window_.surface().fill(QColor(color));
}

void ScriptCanvasGraphics::fillRect(double x, double y, double width, double height,
                                    const QString& color) {
  QPainter painter(&window_.surface());
  painter.fillRect(QRectF(x, y, width, height), QColor(color));
}

void ScriptCanvasGraphics::strokeRect(double x, double y, double width, double height,
                                      const QString& color, double lineWidth) {
  QPainter painter(&window_.surface());
  painter.setPen(QPen(QColor(color), lineWidth));
  painter.drawRect(QRectF(x, y, width, height));
}

void ScriptCanvasGraphics::line(double x1, double y1, double x2, double y2, const QString& color,
                                double lineWidth) {
  QPainter painter(&window_.surface());
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(color), lineWidth));
  painter.drawLine(QPointF(x1, y1), QPointF(x2, y2));
}

void ScriptCanvasGraphics::circle(double centerX, double centerY, double radius,
                                  const QString& color, bool filled, double lineWidth) {
  QPainter painter(&window_.surface());
  painter.setRenderHint(QPainter::Antialiasing);
  if (filled) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(color));
  } else {
    painter.setPen(QPen(QColor(color), lineWidth));
    painter.setBrush(Qt::NoBrush);
  }
  painter.drawEllipse(QPointF(centerX, centerY), radius, radius);
}

void ScriptCanvasGraphics::text(double x, double y, const QString& value, const QString& color,
                                double sizePt) {
  QPainter painter(&window_.surface());
  painter.setRenderHint(QPainter::TextAntialiasing);
  QFont font = painter.font();
  font.setPointSizeF(std::max(1.0, sizePt));
  painter.setFont(font);
  painter.setPen(QColor(color));
  painter.drawText(QPointF(x, y), value);
}

void ScriptCanvasGraphics::drawImage(const QJSValue& source, double x, double y) {
  QImage image;
  if (auto* layer_wrapper = qobject_cast<ScriptLayerObject*>(source.toQObject())) {
    // Draw a document layer's pixels (const read; reads must not bump revisions).
    // The engine host resolves the layer; a stale wrapper throws there.
    ScriptEngineHost* host = &window_.host();
    const auto* document = host->session_document_const(layer_wrapper->session_id());
    const auto* layer = document != nullptr ? document->find_layer(layer_wrapper->layer_id()) : nullptr;
    if (layer == nullptr) {
      host->throw_js_error(ScriptEngineHost::tr("The layer no longer exists."));
      return;
    }
    const auto& pixels = layer->pixels();
    if (pixels.empty() || pixels.format().channels != 4 ||
        pixels.format().bit_depth != BitDepth::UInt8) {
      return;
    }
    image = QImage(pixels.data().data(), pixels.width(), pixels.height(),
                   static_cast<qsizetype>(pixels.stride_bytes()), QImage::Format_RGBA8888)
                .copy();
  } else if (source.isObject()) {
    const int width = source.property(QStringLiteral("width")).toInt();
    const int height = source.property(QStringLiteral("height")).toInt();
    const auto data = source.property(QStringLiteral("data")).toVariant().toByteArray();
    if (width < 1 || height < 1 ||
        data.size() != static_cast<qsizetype>(width) * height * 4) {
      return;
    }
    image = QImage(reinterpret_cast<const uchar*>(data.constData()), width, height,
                   static_cast<qsizetype>(width) * 4, QImage::Format_RGBA8888)
                .copy();
  }
  if (image.isNull()) {
    return;
  }
  QPainter painter(&window_.surface());
  painter.drawImage(QPointF(x, y), image);
}

// ---------------------------------------------------------------------------
// ScriptCanvasWindow

ScriptCanvasWindow::ScriptCanvasWindow(ScriptEngineHost& host, int width, int height,
                                       const QString& title)
    : host_(host), surface_(width, height, QImage::Format_RGBA8888) {
  surface_.fill(Qt::black);
  graphics_ = new ScriptCanvasGraphics(*this);

  auto* dialog = new QDialog(&host_.window());
  dialog_ = dialog;
  dialog->setObjectName(QStringLiteral("scriptCanvasWindowDialog"));
  dialog->setWindowTitle(title);
  dialog->setAttribute(Qt::WA_DeleteOnClose, false);
  auto* layout = new QVBoxLayout(dialog);
  layout->setContentsMargins(0, 0, 0, 0);
  auto* surface_widget = new SurfaceWidget(surface_, dialog);
  surface_widget->setObjectName(QStringLiteral("scriptCanvasSurface"));
  layout->addWidget(surface_widget);
  dialog->installEventFilter(this);
  surface_widget->installEventFilter(this);
  connect(dialog, &QDialog::finished, this, [this](int) { handle_closed(); });

  frame_timer_ = new QTimer(this);
  frame_timer_->setInterval(16);
  connect(frame_timer_, &QTimer::timeout, this, &ScriptCanvasWindow::fire_frame);

  // Deliberately NOT run_non_modal_dialog: that helper parks the caller in a
  // nested event loop until the dialog finishes, and the calling script must
  // keep executing while its window is open. Its macOS above-parent anchoring
  // is still required for any bypassing path, so apply it directly; the
  // auto-reject-with-parent clause is moot (the parent is the main window).
  keep_dialog_above_parent_window(*dialog);
  dialog->setModal(false);
  dialog->setWindowModality(Qt::NonModal);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
  surface_widget->setFocus();
}

ScriptCanvasWindow::~ScriptCanvasWindow() {
  if (dialog_ != nullptr) {
    dialog_->removeEventFilter(this);
    dialog_->deleteLater();
  }
}

int ScriptCanvasWindow::surface_width() const noexcept { return surface_.width(); }

int ScriptCanvasWindow::surface_height() const noexcept { return surface_.height(); }

QJSValue ScriptCanvasWindow::graphics() const {
  auto* engine = const_cast<ScriptEngineHost&>(host_).engine();
  return engine != nullptr ? engine->newQObject(graphics_) : QJSValue();
}

void ScriptCanvasWindow::set_on_frame(const QJSValue& callback) {
  on_frame_ = callback;
  if (on_frame_.isCallable() && is_open()) {
    last_frame_ms_ = 0;
    frame_timer_->start();
  } else {
    frame_timer_->stop();
  }
}

bool ScriptCanvasWindow::is_open() const { return dialog_ != nullptr && dialog_->isVisible(); }

void ScriptCanvasWindow::close() {
  if (dialog_ != nullptr && dialog_->isVisible()) {
    dialog_->close();
  }
}

void ScriptCanvasWindow::present() {
  if (dialog_ != nullptr) {
    if (auto* widget = dialog_->findChild<QWidget*>(QStringLiteral("scriptCanvasSurface"))) {
      widget->update();
    }
  }
}

bool ScriptCanvasWindow::isKeyDown(const QString& key) const {
  return keys_down_.contains(key.toLower());
}

void ScriptCanvasWindow::release_script_state() {
  closing_ = true;
  frame_timer_->stop();
  on_frame_ = QJSValue();
  on_key_down_ = QJSValue();
  on_key_up_ = QJSValue();
  on_mouse_down_ = QJSValue();
  on_mouse_move_ = QJSValue();
  on_mouse_up_ = QJSValue();
  if (dialog_ != nullptr && dialog_->isVisible()) {
    dialog_->close();
  }
}

QString ScriptCanvasWindow::key_name(int key) {
  return QKeySequence(key).toString(QKeySequence::PortableText);
}

void ScriptCanvasWindow::fire_frame() {
  if (closing_ || !is_open() || !on_frame_.isCallable()) {
    frame_timer_->stop();
    return;
  }
  if (!frame_clock_.isValid()) {
    frame_clock_.start();
  }
  const qint64 now = frame_clock_.elapsed();
  const double dt_ms = last_frame_ms_ == 0 ? 16.0 : static_cast<double>(now - last_frame_ms_);
  last_frame_ms_ = now;
  if (host_.call_script_callback(on_frame_, QJSValueList{QJSValue(dt_ms)})) {
    present();
  }
}

void ScriptCanvasWindow::handle_closed() {
  frame_timer_->stop();
  keys_down_.clear();
  if (!closing_) {
    host_.canvas_window_closed(this);
  }
}

bool ScriptCanvasWindow::eventFilter(QObject* watched, QEvent* event) {
  if (closing_) {
    return QObject::eventFilter(watched, event);
  }
  switch (event->type()) {
    case QEvent::KeyPress: {
      auto* key_event = static_cast<QKeyEvent*>(event);
      const auto name = key_name(key_event->key());
      if (!key_event->isAutoRepeat() && !name.isEmpty()) {
        keys_down_.insert(name.toLower());
        if (on_key_down_.isCallable()) {
          host_.call_script_callback(on_key_down_, QJSValueList{QJSValue(name)});
        }
      }
      return true;
    }
    case QEvent::KeyRelease: {
      auto* key_event = static_cast<QKeyEvent*>(event);
      const auto name = key_name(key_event->key());
      if (!key_event->isAutoRepeat() && !name.isEmpty()) {
        keys_down_.remove(name.toLower());
        if (on_key_up_.isCallable()) {
          host_.call_script_callback(on_key_up_, QJSValueList{QJSValue(name)});
        }
      }
      return true;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease: {
      if (qobject_cast<QDialog*>(watched) != nullptr) {
        break;  // surface-relative coordinates only
      }
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      QJSValue callback;
      if (event->type() == QEvent::MouseButtonPress) {
        callback = on_mouse_down_;
      } else if (event->type() == QEvent::MouseMove) {
        callback = on_mouse_move_;
      } else {
        callback = on_mouse_up_;
      }
      if (callback.isCallable()) {
        const auto position = mouse_event->position();
        host_.call_script_callback(
            callback, QJSValueList{QJSValue(position.x()), QJSValue(position.y()),
                                   QJSValue(static_cast<int>(mouse_event->button()))});
      }
      break;
    }
    default:
      break;
  }
  return QObject::eventFilter(watched, event);
}

}  // namespace patchy::ui
