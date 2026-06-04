#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/filter_workflows.hpp"
#include "formats/bmp_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/print_dialog.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/app_settings.hpp"
#include "ui/update_checker.hpp"
#include "filters/builtin_filters.hpp"
#include "psd/psd_document_io.hpp"
#include "test_harness.hpp"

#include <QAbstractItemModel>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDockWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QKeyEvent>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMetaObject>
#include <QMouseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QIODevice>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStringList>
#include <QScrollBar>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVariant>
#include <QWheelEvent>
#include <QWindow>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

QFont visual_test_font(int point_size = 9) {
  const QStringList font_files = {
      QStringLiteral("C:/Windows/Fonts/arial.ttf"),
      QStringLiteral("C:/Windows/Fonts/arialbd.ttf"),
      QStringLiteral("C:/Windows/Fonts/ariali.ttf"),
      QStringLiteral("C:/Windows/Fonts/arialbi.ttf"),
      QStringLiteral("C:/Windows/Fonts/segoeui.ttf"),
      QStringLiteral("C:/Windows/Fonts/segoeuib.ttf"),
      QStringLiteral("C:/Windows/Fonts/segoeuii.ttf"),
      QStringLiteral("C:/Windows/Fonts/segoeuiz.ttf"),
      QStringLiteral("C:/Windows/Fonts/calibri.ttf"),
      QStringLiteral("C:/Windows/Fonts/calibrib.ttf"),
      QStringLiteral("C:/Windows/Fonts/calibrii.ttf"),
      QStringLiteral("C:/Windows/Fonts/calibriz.ttf"),
  };
  QString preferred_family;
  for (const auto& path : font_files) {
    if (!QFileInfo::exists(path)) {
      continue;
    }
    const auto font_id = QFontDatabase::addApplicationFont(path);
    const auto families = QFontDatabase::applicationFontFamilies(font_id);
    if (families.contains(QStringLiteral("Arial"))) {
      preferred_family = QStringLiteral("Arial");
    } else if (preferred_family.isEmpty() && !families.isEmpty()) {
      preferred_family = families.front();
    }
  }
  if (!preferred_family.isEmpty()) {
    QFont font(preferred_family);
    font.setPointSize(point_size);
    return font;
  }

  auto font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
  font.setPointSize(point_size);
  return font;
}

using patchy::test::TestCase;

class PaintCounterFilter final : public QObject {
public:
  int paint_events{0};

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::Paint) {
      ++paint_events;
    }
    return QObject::eventFilter(watched, event);
  }
};

patchy::PixelBuffer solid_pixels(std::int32_t width, std::int32_t height, patchy::PixelFormat format,
                                    QColor color) {
  patchy::PixelBuffer pixels(width, height, format);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(color.red());
      px[1] = static_cast<std::uint8_t>(color.green());
      px[2] = static_cast<std::uint8_t>(color.blue());
      if (format.channels >= 4) {
        px[3] = static_cast<std::uint8_t>(color.alpha());
      }
    }
  }
  return pixels;
}

void fill_pixel_rect(patchy::PixelBuffer& pixels, QRect rect, QColor color) {
  rect = rect.intersected(QRect(0, 0, pixels.width(), pixels.height()));
  if (rect.isEmpty()) {
    return;
  }

  for (int y = rect.top(); y <= rect.bottom(); ++y) {
    for (int x = rect.left(); x <= rect.right(); ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(color.red());
      px[1] = static_cast<std::uint8_t>(color.green());
      px[2] = static_cast<std::uint8_t>(color.blue());
      if (pixels.format().channels >= 4) {
        px[3] = static_cast<std::uint8_t>(color.alpha());
      }
    }
  }
}

void ensure_artifact_dir() {
  std::filesystem::create_directories("test-artifacts");
}

double text_points_for_pixels(int pixels, double ppi = 300.0) noexcept {
  return static_cast<double>(pixels) * 72.0 / ppi;
}

class SettingsValueRestorer {
public:
  explicit SettingsValueRestorer(QString key)
      : settings_(patchy::ui::app_settings()),
        key_(std::move(key)),
        had_value_(settings_.contains(key_)),
        value_(settings_.value(key_)) {}

  ~SettingsValueRestorer() {
    if (had_value_) {
      settings_.setValue(key_, value_);
    } else {
      settings_.remove(key_);
    }
    settings_.sync();
  }

private:
  QSettings settings_;
  QString key_;
  bool had_value_{false};
  QVariant value_;
};

class EnvironmentVariableRestorer {
public:
  explicit EnvironmentVariableRestorer(const char* name)
      : name_(name),
        had_value_(qEnvironmentVariableIsSet(name)),
        value_(qgetenv(name)) {}

  ~EnvironmentVariableRestorer() {
    if (had_value_) {
      qputenv(name_.constData(), value_);
    } else {
      qunsetenv(name_.constData());
    }
  }

private:
  QByteArray name_;
  bool had_value_{false};
  QByteArray value_;
};

class PaintRegionRecorder final : public QObject {
public:
  explicit PaintRegionRecorder(QObject* parent = nullptr)
      : QObject(parent) {}

  void reset() {
    region_ = QRegion();
  }

  void set_recording(bool recording) noexcept {
    recording_ = recording;
  }

  [[nodiscard]] QRegion region() const {
    return region_;
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (recording_ && event->type() == QEvent::Paint) {
      if (auto* paint_event = static_cast<QPaintEvent*>(event); paint_event != nullptr) {
        region_ += paint_event->region();
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QRegion region_;
  bool recording_{true};
};

void save_widget_artifact(const std::string& name, QWidget& widget) {
  ensure_artifact_dir();
  const auto path = QString::fromStdString((std::filesystem::path("test-artifacts") / (name + ".png")).string());
  const auto pixmap = widget.grab();
  CHECK(!pixmap.isNull());
  CHECK(pixmap.save(path));
}

void send_mouse(QWidget& widget, QEvent::Type type, QPoint position, Qt::MouseButton button, Qt::MouseButtons buttons,
                Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QMouseEvent event(type, position, widget.mapToGlobal(position), button, buttons, modifiers);
  QApplication::sendEvent(&widget, &event);
  QApplication::processEvents();
}

void drag(QWidget& widget, QPoint from, QPoint to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  send_mouse(widget, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, modifiers);
  send_mouse(widget, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, modifiers);
  send_mouse(widget, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, modifiers);
}

void send_double_click(QWidget& widget, QPoint position, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QMouseEvent event(QEvent::MouseButtonDblClick, position, widget.mapToGlobal(position), Qt::LeftButton,
                    Qt::LeftButton, modifiers);
  QApplication::sendEvent(&widget, &event);
  QApplication::processEvents();
}

void send_key(QWidget& widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent press(QEvent::KeyPress, key, modifiers);
  QApplication::sendEvent(&widget, &press);
  QKeyEvent release(QEvent::KeyRelease, key, modifiers);
  QApplication::sendEvent(&widget, &release);
  QApplication::processEvents();
}

void send_key_press(QWidget& widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent event(QEvent::KeyPress, key, modifiers);
  QApplication::sendEvent(&widget, &event);
  QApplication::processEvents();
}

void send_key_release(QWidget& widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent event(QEvent::KeyRelease, key, modifiers);
  QApplication::sendEvent(&widget, &event);
  QApplication::processEvents();
}

void send_wheel(QWidget& widget, QPoint position, int delta, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QWheelEvent event(QPointF(position), QPointF(widget.mapToGlobal(position)), QPoint(), QPoint(0, delta),
                    Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
  QApplication::sendEvent(&widget, &event);
  QApplication::processEvents();
}

void send_layer_drag_enter(QListWidget& list, QPoint position, const std::vector<patchy::LayerId>& ids) {
  QMimeData mime_data;
  mime_data.setData(QString::fromLatin1(patchy::ui::kLayerDragMimeType), patchy::ui::layer_ids_to_mime_data(ids));
  QDragEnterEvent event(position, Qt::MoveAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(list.viewport(), &event);
  QApplication::processEvents();
}

void send_layer_drag_move(QListWidget& list, QPoint position, const std::vector<patchy::LayerId>& ids) {
  QMimeData mime_data;
  mime_data.setData(QString::fromLatin1(patchy::ui::kLayerDragMimeType), patchy::ui::layer_ids_to_mime_data(ids));
  QDragMoveEvent event(position, Qt::MoveAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(list.viewport(), &event);
  QApplication::processEvents();
}

void send_layer_drag_leave(QListWidget& list) {
  QDragLeaveEvent event;
  QApplication::sendEvent(list.viewport(), &event);
  QApplication::processEvents();
}

void send_layer_drop(QListWidget& list, QPoint position, const std::vector<patchy::LayerId>& ids) {
  QMimeData mime_data;
  mime_data.setData(QString::fromLatin1(patchy::ui::kLayerDragMimeType), patchy::ui::layer_ids_to_mime_data(ids));
  QDragEnterEvent enter(position, Qt::MoveAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(list.viewport(), &enter);
  QDragMoveEvent move(position, Qt::MoveAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(list.viewport(), &move);
  QDropEvent event(QPointF(position), Qt::MoveAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(list.viewport(), &event);
  QApplication::processEvents();
  QApplication::processEvents();
}

void send_layer_button_drop(QWidget& button, const std::vector<patchy::LayerId>& ids) {
  QMimeData mime_data;
  mime_data.setData(QString::fromLatin1(patchy::ui::kLayerDragMimeType), patchy::ui::layer_ids_to_mime_data(ids));
  const auto position = button.rect().center();
  QDragEnterEvent enter(position, Qt::MoveAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&button, &enter);
  QDragMoveEvent move(position, Qt::MoveAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&button, &move);
  QDropEvent drop(QPointF(position), Qt::MoveAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&button, &drop);
  QApplication::processEvents();
  QApplication::processEvents();
}

QAction* require_action(QWidget& root, const char* object_name) {
  auto* action = root.findChild<QAction*>(QString::fromLatin1(object_name));
  CHECK(action != nullptr);
  return action;
}

QAction* find_action_by_text(QWidget& root, const QString& text) {
  const auto actions = root.findChildren<QAction*>();
  for (auto* action : actions) {
    if (action->menu() != nullptr) {
      continue;
    }
    if (action->text().remove('&') == text) {
      return action;
    }
  }
  return nullptr;
}

QAction* require_action_by_text(QWidget& root, const QString& text) {
  auto* action = find_action_by_text(root, text);
  CHECK(action != nullptr);
  return action;
}

QImage image_from_pixels_for_visuals(const patchy::PixelBuffer& pixels) {
  QImage image(pixels.width(), pixels.height(),
               pixels.format().channels >= 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
  image.fill(pixels.format().channels >= 4 ? Qt::transparent : Qt::black);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      image.setPixelColor(x, y, QColor(px[0], px[1], px[2], pixels.format().channels >= 4 ? px[3] : 255));
    }
  }
  return image;
}

QImage flattened_on_white(const patchy::PixelBuffer& pixels) {
  const auto source = image_from_pixels_for_visuals(pixels);
  QImage flattened(source.size(), QImage::Format_RGB32);
  flattened.fill(Qt::white);
  QPainter painter(&flattened);
  painter.drawImage(QPoint(0, 0), source);
  painter.end();
  return flattened;
}

patchy::PixelBuffer make_filter_stroke_source() {
  QImage image(220, 160, QImage::Format_ARGB32);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  QPen pen(QColor(220, 28, 24), 12, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen(pen);
  painter.drawLine(QPoint(26, 132), QPoint(78, 28));
  painter.drawLine(QPoint(70, 88), QPoint(156, 45));
  painter.drawLine(QPoint(86, 118), QPoint(196, 122));
  painter.drawArc(QRect(62, 28, 78, 82), 30 * 16, 290 * 16);
  painter.drawArc(QRect(94, 72, 78, 52), 190 * 16, 250 * 16);
  painter.end();
  return patchy::ui::pixels_from_image_rgba(image);
}

bool spatial_filter_spreads_clean_red_alpha(const patchy::PixelBuffer& before, const patchy::PixelBuffer& after) {
  int spread_pixels = 0;
  bool clean_red = false;
  for (std::int32_t y = 0; y < after.height(); ++y) {
    for (std::int32_t x = 0; x < after.width(); ++x) {
      const auto* src = before.pixel(x, y);
      const auto* dst = after.pixel(x, y);
      if (src[3] == 0 && dst[3] > 8) {
        ++spread_pixels;
        clean_red = clean_red || (dst[0] > 170 && dst[1] < 90 && dst[2] < 90);
      }
    }
  }
  return spread_pixels > 0 && clean_red;
}

QAction* find_menu_action_by_text(QMenu& menu, const QString& text) {
  for (auto* action : menu.actions()) {
    auto action_text = action->text();
    action_text.remove('&');
    if (action_text == text) {
      return action;
    }
  }
  return nullptr;
}

struct LayerStyleContextMenuState {
  bool saw_copy{false};
  bool saw_paste{false};
  bool saw_delete{false};
  bool copy_enabled{false};
  bool paste_enabled{false};
  bool delete_enabled{false};
};

LayerStyleContextMenuState layer_style_context_menu_state(QListWidget& layer_list, QListWidgetItem& item) {
  LayerStyleContextMenuState state;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("layerContextMenu")) {
        continue;
      }
      if (auto* action = find_menu_action_by_text(*menu, QStringLiteral("Copy Layer Style")); action != nullptr) {
        state.saw_copy = true;
        state.copy_enabled = action->isEnabled();
      }
      if (auto* action = find_menu_action_by_text(*menu, QStringLiteral("Paste Layer Style")); action != nullptr) {
        state.saw_paste = true;
        state.paste_enabled = action->isEnabled();
      }
      if (auto* action = find_menu_action_by_text(*menu, QStringLiteral("Delete Layer Style")); action != nullptr) {
        state.saw_delete = true;
        state.delete_enabled = action->isEnabled();
      }
      menu->close();
      return;
    }
    CHECK(false);
  });

  const auto context_point = layer_list.visualItemRect(&item).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  layer_list.viewport()->mapToGlobal(context_point));
  QApplication::sendEvent(layer_list.viewport(), &context_event);
  QApplication::processEvents();
  return state;
}

patchy::ui::CanvasWidget* require_canvas(patchy::ui::MainWindow& window) {
  auto* canvas = dynamic_cast<patchy::ui::CanvasWidget*>(window.centralWidget());
  if (canvas == nullptr) {
    auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
    if (tabs != nullptr) {
      canvas = dynamic_cast<patchy::ui::CanvasWidget*>(tabs->currentWidget());
    }
  }
  CHECK(canvas != nullptr);
  return canvas;
}

void show_window(patchy::ui::MainWindow& window) {
  window.resize(1180, 780);
  window.show();
  QApplication::processEvents();
}

void process_events_for(int milliseconds) {
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec(QEventLoop::AllEvents);
  QApplication::processEvents();
}

QDialog* find_top_level_dialog(const QString& object_name) {
  for (auto* widget : QApplication::topLevelWidgets()) {
    auto* dialog = qobject_cast<QDialog*>(widget);
    if (dialog != nullptr && dialog->objectName() == object_name) {
      return dialog;
    }
  }
  return nullptr;
}

void accept_missing_psd_text_font_warning_if_present() {
  QTimer::singleShot(0, [] {
    auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("missingPsdTextFontMessageBox")));
    if (dialog == nullptr) {
      return;
    }
    for (auto* button : dialog->findChildren<QPushButton*>()) {
      if (dialog->buttonRole(button) == QMessageBox::AcceptRole) {
        button->click();
        return;
      }
    }
    CHECK(false);
  });
}

void accept_compatibility_report_when_present(const std::shared_ptr<bool>& done, int attempts = 2400) {
  QTimer::singleShot(50, [done, attempts] {
    if (done == nullptr || *done) {
      return;
    }
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyCompatibilityReportDialog")));
    if (dialog != nullptr) {
      dialog->accept();
      *done = true;
      return;
    }
    if (attempts > 0) {
      accept_compatibility_report_when_present(done, attempts - 1);
    }
  });
}

void verify_open_progress_dialog(const QString& expected_file_name, bool& saw_dialog) {
  auto* dialog = qobject_cast<QProgressDialog*>(find_top_level_dialog(QStringLiteral("openProgressDialog")));
  CHECK(dialog != nullptr);
  CHECK(dialog->isVisible());
  const auto title = dialog->windowTitle();
  CHECK(title.startsWith(QStringLiteral("Opening ")));
  CHECK(title != QStringLiteral("Opening File"));
  CHECK(!title.contains(QStringLiteral("Patchy")));
  const auto title_file_name = title.mid(QStringLiteral("Opening ").size());
  if (title_file_name.contains(QChar(0x2026))) {
    CHECK(title_file_name.size() < expected_file_name.size());
    CHECK(title_file_name.endsWith(QFileInfo(expected_file_name).suffix()));
  } else {
    CHECK(title_file_name == expected_file_name);
  }
  CHECK(dialog->windowModality() == Qt::WindowModal);
  CHECK(dialog->minimum() == 0);
  CHECK(dialog->maximum() == 0);
  CHECK(dialog->labelText().contains(expected_file_name));
  CHECK(dialog->findChildren<QPushButton*>().isEmpty());
  saw_dialog = true;
}

void cleanup_after_visual_test() {
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (qobject_cast<QDialog*>(widget) != nullptr || qobject_cast<QMenu*>(widget) != nullptr) {
      widget->close();
      widget->deleteLater();
    }
  }
  QApplication::processEvents();
  QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QApplication::processEvents();
  patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false);
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("preferences/language"));
  settings.sync();
}

bool color_close(QColor actual, QColor expected, int tolerance) {
  return std::abs(actual.red() - expected.red()) <= tolerance &&
         std::abs(actual.green() - expected.green()) <= tolerance &&
         std::abs(actual.blue() - expected.blue()) <= tolerance;
}

bool images_equal_rgba(const QImage& left, const QImage& right) {
  if (left.size() != right.size()) {
    return false;
  }
  const auto left_rgba = left.convertToFormat(QImage::Format_RGBA8888);
  const auto right_rgba = right.convertToFormat(QImage::Format_RGBA8888);
  const auto row_bytes = static_cast<std::size_t>(left_rgba.width()) * 4U;
  for (int y = 0; y < left_rgba.height(); ++y) {
    if (std::memcmp(left_rgba.constScanLine(y), right_rgba.constScanLine(y), row_bytes) != 0) {
      return false;
    }
  }
  return true;
}

std::optional<QRect> image_mismatch_bounds_rgba(const QImage& left, const QImage& right) {
  if (left.size() != right.size()) {
    return QRect(QPoint(0, 0), left.size().expandedTo(right.size()));
  }
  const auto left_rgba = left.convertToFormat(QImage::Format_RGBA8888);
  const auto right_rgba = right.convertToFormat(QImage::Format_RGBA8888);
  int min_x = left_rgba.width();
  int min_y = left_rgba.height();
  int max_x = -1;
  int max_y = -1;
  for (int y = 0; y < left_rgba.height(); ++y) {
    const auto* left_row = left_rgba.constScanLine(y);
    const auto* right_row = right_rgba.constScanLine(y);
    for (int x = 0; x < left_rgba.width(); ++x) {
      if (std::memcmp(left_row + static_cast<std::size_t>(x) * 4U,
                      right_row + static_cast<std::size_t>(x) * 4U, 4U) == 0) {
        continue;
      }
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
    }
  }
  if (max_x < min_x || max_y < min_y) {
    return std::nullopt;
  }
  return QRect(QPoint(min_x, min_y), QPoint(max_x, max_y));
}

QImage render_widget_image(QWidget& widget, const QRegion& region = QRegion()) {
  QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  if (region.isEmpty()) {
    widget.render(&painter);
  } else {
    widget.render(&painter, QPoint(), region);
  }
  return image;
}

QImage grab_widget_window_image(QWidget& widget) {
  auto* screen = widget.windowHandle() != nullptr ? widget.windowHandle()->screen() : QApplication::primaryScreen();
  CHECK(screen != nullptr);
  const auto pixmap = screen->grabWindow(widget.winId());
  CHECK(!pixmap.isNull());
  return pixmap.toImage();
}

int count_pixels_close(const QImage& image, QRect region, QColor expected, int tolerance) {
  region = region.intersected(image.rect());
  int count = 0;
  for (int y = region.top(); y <= region.bottom(); ++y) {
    for (int x = region.left(); x <= region.right(); ++x) {
      if (color_close(image.pixelColor(x, y), expected, tolerance)) {
        ++count;
      }
    }
  }
  return count;
}

QColor canvas_pixel(patchy::ui::CanvasWidget& canvas, QPoint document_point) {
  const auto widget_point = canvas.widget_position_for_document_point(document_point);
  return canvas.grab().toImage().pixelColor(widget_point);
}

int count_blended_document_pixels(patchy::ui::CanvasWidget& canvas, QRect document_rect, QColor foreground,
                                  QColor background, int tolerance) {
  const auto image = canvas.grab().toImage();
  int count = 0;
  for (int y = document_rect.top(); y <= document_rect.bottom(); ++y) {
    for (int x = document_rect.left(); x <= document_rect.right(); ++x) {
      const auto widget_point = canvas.widget_position_for_document_point(QPoint(x, y));
      if (!image.rect().contains(widget_point)) {
        continue;
      }
      const auto color = image.pixelColor(widget_point);
      if (!color_close(color, foreground, tolerance) && !color_close(color, background, tolerance)) {
        ++count;
      }
    }
  }
  return count;
}

QColor canvas_pixel_center(patchy::ui::CanvasWidget& canvas, QPoint document_point) {
  const auto top_left = canvas.widget_position_for_document_point(document_point);
  const auto bottom_right = canvas.widget_position_for_document_point(document_point + QPoint(1, 1));
  const QPoint center((top_left.x() + bottom_right.x()) / 2, (top_left.y() + bottom_right.y()) / 2);
  return canvas.grab().toImage().pixelColor(center);
}

std::optional<QRect> dark_document_bounds(patchy::ui::CanvasWidget& canvas, QRect document_rect) {
  const auto image = canvas.grab().toImage();
  int min_x = document_rect.right() + 1;
  int min_y = document_rect.bottom() + 1;
  int max_x = document_rect.left() - 1;
  int max_y = document_rect.top() - 1;
  for (int y = document_rect.top(); y <= document_rect.bottom(); ++y) {
    for (int x = document_rect.left(); x <= document_rect.right(); ++x) {
      const auto widget_point = canvas.widget_position_for_document_point(QPoint(x, y));
      if (!image.rect().contains(widget_point)) {
        continue;
      }
      const auto color = image.pixelColor(widget_point);
      if (color.red() < 80 && color.green() < 80 && color.blue() < 80) {
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
      }
    }
  }
  if (max_x < min_x || max_y < min_y) {
    return std::nullopt;
  }
  return QRect(QPoint(min_x, min_y), QPoint(max_x, max_y));
}

void accept_layer_style_dialog(bool stroke_enabled, bool gradient_enabled, bool shadow_enabled);

QAction* require_legacy_plugin_action(QWidget& root, const QString& text) {
  for (auto* action : root.findChildren<QAction*>(QStringLiteral("legacyPluginAction"))) {
    if (action->text().contains(text, Qt::CaseInsensitive)) {
      return action;
    }
  }
  CHECK(false);
  return nullptr;
}

QListWidgetItem* find_layer_item(QListWidget& list, const QString& text) {
  for (int row = 0; row < list.count(); ++row) {
    if (list.item(row)->text() == text) {
      return list.item(row);
    }
  }
  return nullptr;
}

QListWidgetItem* require_layer_item(QListWidget& list, const QString& text) {
  if (auto* item = find_layer_item(list, text); item != nullptr) {
    return item;
  }
  CHECK(false);
  return nullptr;
}

bool top_level_widget_exists(const QString& object_name) {
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == object_name) {
      return true;
    }
  }
  return false;
}

void ui_main_window_renders_color_swatches() {
  patchy::ui::MainWindow window;
  show_window(window);

  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  auto* background = window.findChild<QPushButton*>(QStringLiteral("backgroundColorButton"));
  CHECK(foreground != nullptr);
  CHECK(background != nullptr);
  CHECK(foreground->text() == QStringLiteral("FG"));
  CHECK(background->text() == QStringLiteral("BG"));
  CHECK(!foreground->text().contains('#'));
  CHECK(!background->text().contains('#'));
  CHECK(window.findChildren<QPushButton*>(QStringLiteral("swatchButton")).size() >= 16);
  const QStringList expected_menus = {QStringLiteral("File"),   QStringLiteral("Edit"),   QStringLiteral("Image"),
                                      QStringLiteral("Layer"),  QStringLiteral("Type"),   QStringLiteral("Select"),
                                      QStringLiteral("Filter"), QStringLiteral("Plugins"), QStringLiteral("View"),
                                      QStringLiteral("Window"), QStringLiteral("Help")};
  QStringList actual_menus;
  for (auto* action : window.menuBar()->actions()) {
    actual_menus << action->text().remove('&');
  }
  CHECK(actual_menus == expected_menus);
  CHECK(window.menuBar()->height() >= 30);
  CHECK(window.windowFlags().testFlag(Qt::FramelessWindowHint));
  auto* app_badge = window.menuBar()->findChild<QLabel*>(QStringLiteral("patchyBadge"));
  CHECK(app_badge != nullptr);
  CHECK(app_badge->pixmap(Qt::ReturnByValue).isNull() == false);
  auto* window_close = window.findChild<QToolButton*>(QStringLiteral("windowCloseButton"));
  CHECK(window_close != nullptr);
  CHECK(window_close->mapTo(&window, QPoint(window_close->width(), 0)).x() >= window.width() - 1);
  CHECK(window.findChild<QAction*>(QStringLiteral("workspaceHomeAction")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("helpHomepageAction")) == nullptr);
  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  auto* filter_menu = window.findChild<QMenu*>(QStringLiteral("filterMenu"));
  CHECK(filter_menu != nullptr);
  QStringList filter_action_texts;
  const std::function<void(QMenu*)> collect_filter_actions = [&](QMenu* menu) {
    for (auto* action : menu->actions()) {
      if (action->isSeparator()) {
        continue;
      }
      if (auto* submenu = action->menu(); submenu != nullptr) {
        collect_filter_actions(submenu);
        continue;
      }
      auto text = action->text();
      text.remove('&');
      filter_action_texts << text;
    }
  };
  collect_filter_actions(filter_menu);
  CHECK(filter_action_texts.contains(QStringLiteral("Soft Glow")));
  CHECK(filter_action_texts.contains(QStringLiteral("Punchy Color")));
  CHECK(filter_action_texts.contains(QStringLiteral("Noir")));
  CHECK(filter_action_texts.contains(QStringLiteral("Cinematic Matte")));
  CHECK(filter_action_texts.contains(QStringLiteral("Vintage Fade")));
  CHECK(filter_action_texts.contains(QStringLiteral("Twirl")));
  CHECK(filter_action_texts.contains(QStringLiteral("Clouds")));
  CHECK(filter_action_texts.contains(QStringLiteral("Pixel Mosaic")));
  CHECK(filter_action_texts.contains(QStringLiteral("Unsharp Mask")));
  CHECK(filter_action_texts.contains(QStringLiteral("Motion Blur")));
  CHECK(filter_action_texts.contains(QStringLiteral("Color Halftone")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Brightness +24")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Contrast +25%")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Brightness")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Contrast")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Auto Contrast")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Desaturate")));
  for (auto* action : filter_menu->actions()) {
    CHECK(!action->isIconVisibleInMenu());
  }
  auto* adjustments_menu = window.findChild<QMenu*>(QStringLiteral("imageAdjustmentsMenu"));
  CHECK(adjustments_menu != nullptr);
  QStringList adjustment_action_texts;
  for (auto* action : adjustments_menu->actions()) {
    CHECK(!action->isIconVisibleInMenu());
    if (!action->isSeparator()) {
      auto text = action->text();
      text.remove('&');
      adjustment_action_texts << text;
    }
  }
  CHECK(adjustment_action_texts.contains(QStringLiteral("Brightness/Contrast...")));
  CHECK(!adjustment_action_texts.contains(QStringLiteral("Brightness...")));
  CHECK(!adjustment_action_texts.contains(QStringLiteral("Contrast...")));
  CHECK(window.findChild<QAction*>(QStringLiteral("imageAdjustBrightnessAction")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("imageAdjustContrastAction")) == nullptr);
  auto* new_adjustments_menu = window.findChild<QMenu*>(QStringLiteral("layerNewAdjustmentMenu"));
  CHECK(new_adjustments_menu != nullptr);
  CHECK(require_action(window, "layerNewLevelsAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewCurvesAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewHueSaturationAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewColorBalanceAdjustmentAction") != nullptr);
  CHECK(window.findChild<QToolButton*>(QStringLiteral("layerNewAdjustmentButton")) != nullptr);
  CHECK(window.findChild<QSpinBox*>(QStringLiteral("selectionFeatherSpin")) != nullptr);
  for (auto* button : window.findChildren<QPushButton*>()) {
    CHECK(button->text() != QStringLiteral("Select and Mask..."));
  }
  auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  CHECK(options_bar != nullptr);
  CHECK(options_bar->height() >= 36);
  auto* tool_palette = window.findChild<QToolBar*>(QStringLiteral("toolPalette"));
  CHECK(tool_palette != nullptr);
  CHECK(tool_palette->width() <= 45);
  auto* marquee_button = window.findChild<QToolButton*>(QStringLiteral("marqueeToolButton"));
  CHECK(marquee_button != nullptr);
  CHECK(marquee_button->menu() != nullptr);
  CHECK(marquee_button->menu()->actions().size() == 2);
  CHECK(marquee_button->defaultAction() == require_action_by_text(window, QStringLiteral("Marquee")));
  auto* shape_button = window.findChild<QToolButton*>(QStringLiteral("shapeToolButton"));
  CHECK(shape_button != nullptr);
  CHECK(shape_button->menu() != nullptr);
  CHECK(shape_button->menu()->actions().size() == 3);
  CHECK(shape_button->defaultAction() == require_action_by_text(window, QStringLiteral("Rect")));

  save_widget_artifact("ui_main_window", window);
}

void ui_window_force_refresh_action_rebuilds_cache() {
  patchy::Document document(180, 130, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(180, 130, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Blue Block",
                      solid_pixels(44, 28, patchy::PixelFormat::rgba8(), QColor(20, 90, 235)));
  layer.set_bounds(patchy::Rect{42, 38, 44, 28});
  const auto expected = patchy::ui::qimage_from_document(document, true);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Force Refresh"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  auto* action = require_action(window, "windowForceRefreshAction");
  CHECK(action->isEnabled());
  CHECK(action->shortcuts().contains(QKeySequence(Qt::Key_F5)));

  const auto before = canvas->render_cache_diagnostics();
  action->trigger();
  QApplication::processEvents();
  const auto after = canvas->render_cache_diagnostics();
  CHECK(after.full_refreshes == before.full_refreshes + 1);
  CHECK(after.forced_refreshes == before.forced_refreshes + 1);

  CHECK(color_close(canvas_pixel(*canvas, QPoint(48, 44)), expected.pixelColor(48, 44), 0));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(16, 16)), expected.pixelColor(16, 16), 0));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Forced refresh"));
  save_widget_artifact("ui_window_force_refresh", window);
}

void ui_top_menu_items_highlight_on_hover() {
  patchy::ui::MainWindow window;
  show_window(window);

  auto* file_menu = window.menuBar()->actions().front()->menu();
  CHECK(file_menu != nullptr);
  auto* open_action = require_action(window, "fileOpenAction");
  CHECK(file_menu->actions().contains(open_action));

  file_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();
  const auto open_rect = file_menu->actionGeometry(open_action);
  CHECK(open_rect.isValid());
  const QPoint sample_point(open_rect.left() + 8, open_rect.center().y());
  const auto idle_color = file_menu->grab().toImage().pixelColor(sample_point);

  send_mouse(*file_menu, QEvent::MouseMove, open_rect.center(), Qt::NoButton, Qt::NoButton);
  if (file_menu->activeAction() != open_action) {
    file_menu->setActiveAction(open_action);
    QApplication::processEvents();
  }

  const auto hover_color = file_menu->grab().toImage().pixelColor(sample_point);
  CHECK(color_close(idle_color, QColor(58, 58, 58), 6));
  CHECK(color_close(hover_color, QColor(78, 111, 149), 6));
  CHECK(!color_close(idle_color, hover_color, 10));
  file_menu->close();
}

void ui_save_as_dialog_lists_recent_files() {
  ensure_artifact_dir();
  const auto first_path =
      QFileInfo(QStringLiteral("test-artifacts/recent-save-target.psd")).absoluteFilePath();
  const auto second_path =
      QFileInfo(QStringLiteral("test-artifacts/recent-save-backup.png")).absoluteFilePath();
  {
    QFile first(first_path);
    CHECK(first.open(QIODevice::WriteOnly));
    CHECK(first.write("patchy psd placeholder") > 0);
    QFile second(second_path);
    CHECK(second.open(QIODevice::WriteOnly));
    CHECK(second.write("patchy png placeholder") > 0);
  }

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), QStringList{first_path, second_path});
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  CHECK(recent_menu->actions().size() >= 2);
  CHECK(recent_menu->actions()[0]->data().toString() == first_path);
  CHECK(recent_menu->actions()[0]->text().remove('&') == QStringLiteral("1 %1").arg(QDir::toNativeSeparators(first_path)));
  CHECK(recent_menu->actions()[1]->data().toString() == second_path);
  CHECK(recent_menu->actions()[1]->text().remove('&') == QStringLiteral("2 %1").arg(QDir::toNativeSeparators(second_path)));

  auto* save_as_action = require_action(window, "fileSaveAsAction");
  CHECK(save_as_action->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("saveAsRecentFileNameCombo"));
    CHECK(combo != nullptr);
    CHECK(combo->isEditable());
    CHECK(combo->count() == 2);
    CHECK(combo->itemText(0) == first_path);
    CHECK(combo->itemData(0).toString() == first_path);
    CHECK(combo->itemData(0, Qt::ToolTipRole).toString() == first_path);
    CHECK(combo->itemText(1) == second_path);
    CHECK(combo->itemData(1).toString() == second_path);
    combo->setCurrentIndex(1);
    QApplication::processEvents();
    const auto selected_files = dialog->selectedFiles();
    CHECK(!selected_files.isEmpty());
    CHECK(QFileInfo(selected_files.first()).absoluteFilePath() == second_path);
    saw_dialog = true;
    dialog->reject();
  });
  save_as_action->trigger();
  CHECK(saw_dialog);
}

void ui_open_recent_keeps_fifty_files() {
  ensure_artifact_dir();
  QStringList recent_files;
  for (int i = 0; i < 55; ++i) {
    const auto path =
        QFileInfo(QStringLiteral("test-artifacts/recent-file-%1.psd").arg(i, 2, 10, QLatin1Char('0')))
            .absoluteFilePath();
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write("patchy recent placeholder") > 0);
    recent_files << path;
  }

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), recent_files);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  QList<QAction*> file_actions;
  for (auto* action : recent_menu->actions()) {
    if (action != nullptr && !action->isSeparator() && !action->data().toString().isEmpty()) {
      file_actions << action;
    }
  }
  CHECK(file_actions.size() == 50);
  for (int i = 0; i < file_actions.size(); ++i) {
    CHECK(file_actions[i]->data().toString() == recent_files[i]);
  }
  CHECK(file_actions.front()->text().remove('&') ==
        QStringLiteral("1 %1").arg(QDir::toNativeSeparators(recent_files.front())));
  CHECK(file_actions.back()->text().remove('&') ==
        QStringLiteral("50 %1").arg(QDir::toNativeSeparators(recent_files[49])));
  CHECK(recent_menu->actions().contains(require_action(window, "fileClearRecentAction")));

  recent_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();
  if (auto* screen = QApplication::primaryScreen(); screen != nullptr) {
    CHECK(recent_menu->height() <= screen->availableGeometry().height());
  }
  recent_menu->close();
}

void ui_recent_file_context_menu_copies_path() {
  ensure_artifact_dir();
  const auto first_path = QFileInfo(QStringLiteral("test-artifacts/recent-copy-target.psd")).absoluteFilePath();
  {
    QFile first(first_path);
    CHECK(first.open(QIODevice::WriteOnly));
    CHECK(first.write("patchy psd placeholder") > 0);
  }

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), QStringList{first_path});
    settings.sync();
  }

  QApplication::clipboard()->clear();

  patchy::ui::MainWindow window;
  show_window(window);

  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  CHECK(recent_menu->contextMenuPolicy() == Qt::CustomContextMenu);
  CHECK(!recent_menu->actions().isEmpty());
  auto* recent_action = recent_menu->actions().front();
  CHECK(recent_action->data().toString() == first_path);

  recent_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();

  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("recentFileContextMenu")) {
        continue;
      }
      auto* copy_action = find_menu_action_by_text(*menu, QStringLiteral("Copy File Path"));
      CHECK(copy_action != nullptr);
      CHECK(copy_action->objectName() == QStringLiteral("recentFileCopyPathAction"));
      copy_action->trigger();
      menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });

  const auto context_point = recent_menu->actionGeometry(recent_action).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  recent_menu->mapToGlobal(context_point));
  QApplication::sendEvent(recent_menu, &context_event);
  QApplication::processEvents();

  CHECK(saw_context_menu);
  CHECK(QApplication::clipboard()->text() == QDir::toNativeSeparators(first_path));
  recent_menu->close();
}

void ui_save_as_remembers_last_save_directory_between_windows() {
  ensure_artifact_dir();
  const auto remembered_dir = QFileInfo(QStringLiteral("test-artifacts/remembered-save-dir")).absoluteFilePath();
  CHECK(QDir().mkpath(remembered_dir));
  const auto saved_path = QDir(remembered_dir).filePath(QStringLiteral("remembered-save.psd"));
  QFile::remove(saved_path);

  SettingsValueRestorer last_save_directory_restorer(QStringLiteral("lastSaveDirectory"));
  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));

  {
    patchy::ui::MainWindow window;
    show_window(window);
    bool saved = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
      CHECK(dialog != nullptr);
      dialog->setDirectory(remembered_dir);
      dialog->selectFile(saved_path);
      saved = true;
      static_cast<QDialog*>(dialog)->accept();
    });
    require_action(window, "fileSaveAsAction")->trigger();
    CHECK(saved);
    CHECK(QFileInfo::exists(saved_path));
  }

  {
    auto settings = patchy::ui::app_settings();
    CHECK(QFileInfo(settings.value(QStringLiteral("lastSaveDirectory")).toString()).absoluteFilePath() ==
          QFileInfo(remembered_dir).absoluteFilePath());
  }

  patchy::ui::MainWindow next_window;
  show_window(next_window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    CHECK(QFileInfo(dialog->directory().absolutePath()).absoluteFilePath() ==
          QFileInfo(remembered_dir).absoluteFilePath());
    const auto selected_files = dialog->selectedFiles();
    CHECK(!selected_files.isEmpty());
    CHECK(QFileInfo(selected_files.first()).absolutePath() == QFileInfo(remembered_dir).absoluteFilePath());
    saw_dialog = true;
    dialog->reject();
  });
  require_action(next_window, "fileSaveAsAction")->trigger();
  CHECK(saw_dialog);
}

QStringList top_level_menu_texts(QMenuBar& menu_bar) {
  QStringList texts;
  for (auto* action : menu_bar.actions()) {
    texts << action->text().remove('&');
  }
  return texts;
}

void choose_preferences_language(patchy::ui::MainWindow& window, const QString& language_code) {
  auto* preferences = require_action(window, "filePreferencesAction");
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesLanguageCombo"));
    CHECK(combo != nullptr);
    const auto index = combo->findData(language_code);
    CHECK(index >= 0);
    combo->setCurrentIndex(index);
    saw_dialog = true;
    dialog->accept();
  });
  preferences->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
}

void update_manifest_parser_handles_supported_cases() {
  const QByteArray newer_manifest = R"({
    "platforms": {
      "windows": {
        "version": "0.2",
        "download_url": "https://rtsoft.com/patchy/PatchyWindowsInstaller.exe"
      },
      "macos": {
        "version": "0.3.0",
        "download_url": "https://rtsoft.com/patchy/PatchyMacOS.dmg"
      }
    }
  })";
  const auto update = patchy::ui::parse_update_manifest(newer_manifest, QStringLiteral("windows"),
                                                        QStringLiteral("0.1.0"));
  CHECK(update.has_value());
  CHECK(update->platform == QStringLiteral("windows"));
  CHECK(update->version == QStringLiteral("0.2"));
  CHECK(update->download_url == QUrl(QStringLiteral("https://rtsoft.com/patchy/PatchyWindowsInstaller.exe")));
  const auto update_result =
      patchy::ui::inspect_update_manifest(newer_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"));
  CHECK(update_result.status == patchy::ui::UpdateCheckStatus::UpdateAvailable);
  CHECK(update_result.update.has_value());
  CHECK(update_result.latest_version == QStringLiteral("0.2"));
  CHECK(!patchy::ui::update_version_is_newer(QStringLiteral("0.2.0"), QStringLiteral("0.2")));
  CHECK(!patchy::ui::update_version_is_newer(QStringLiteral("0.2"), QStringLiteral("0.2.0")));
  CHECK(patchy::ui::update_version_is_newer(QStringLiteral("0.10"), QStringLiteral("0.2")));
  CHECK(!patchy::ui::update_version_is_newer(QStringLiteral("0.1.0"), QStringLiteral("0.1.0")));
  CHECK(!patchy::ui::update_version_is_newer(QStringLiteral("0.0.9"), QStringLiteral("0.1.0")));

  const QByteArray equal_manifest = R"({
    "platforms": {
      "windows": {
        "version": "0.1.0",
        "download_url": "https://rtsoft.com/patchy/PatchyWindowsInstaller.exe"
      }
    }
  })";
  CHECK(!patchy::ui::parse_update_manifest(equal_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"))
             .has_value());
  const auto equal_result =
      patchy::ui::inspect_update_manifest(equal_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"));
  CHECK(equal_result.status == patchy::ui::UpdateCheckStatus::NoUpdateAvailable);
  CHECK(equal_result.latest_version == QStringLiteral("0.1.0"));
  const QByteArray lower_manifest = R"({
    "platforms": {
      "windows": {
        "version": "0.0.9",
        "download_url": "https://rtsoft.com/patchy/PatchyWindowsInstaller.exe"
      }
    }
  })";
  CHECK(!patchy::ui::parse_update_manifest(lower_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"))
             .has_value());
  CHECK(!patchy::ui::parse_update_manifest(newer_manifest, QStringLiteral("linux"), QStringLiteral("0.1.0"))
             .has_value());
  const auto missing_platform_result =
      patchy::ui::inspect_update_manifest(newer_manifest, QStringLiteral("linux"), QStringLiteral("0.1.0"));
  CHECK(missing_platform_result.status == patchy::ui::UpdateCheckStatus::MissingPlatform);
  const auto invalid_manifest_result =
      patchy::ui::inspect_update_manifest(QByteArray("{"), QStringLiteral("windows"), QStringLiteral("0.1.0"));
  CHECK(invalid_manifest_result.status == patchy::ui::UpdateCheckStatus::InvalidManifest);
  CHECK(!patchy::ui::parse_update_manifest(QByteArray("{"), QStringLiteral("windows"), QStringLiteral("0.1.0"))
             .has_value());

  const QByteArray empty_url_manifest = R"({
    "platforms": {
      "windows": {
        "version": "0.2.0",
        "download_url": ""
      }
    }
  })";
  CHECK(!patchy::ui::parse_update_manifest(empty_url_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"))
             .has_value());
  const auto empty_url_result =
      patchy::ui::inspect_update_manifest(empty_url_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"));
  CHECK(empty_url_result.status == patchy::ui::UpdateCheckStatus::InvalidDownloadUrl);

  const QByteArray relative_url_manifest = R"({
    "platforms": {
      "windows": {
        "version": "0.2.0",
        "download_url": "PatchyWindowsInstaller.exe"
      }
    }
  })";
  CHECK(!patchy::ui::parse_update_manifest(relative_url_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"))
             .has_value());
}

void ui_update_available_dialog_warns_to_close_patchy_before_installing() {
  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("updateAvailableMessageBox")));
    CHECK(dialog != nullptr);
    CHECK(dialog->text().contains(
        QStringLiteral("Save your work and close Patchy before running the installer.")));
    saw_dialog = true;
    dialog->reject();
  });

  window.show_update_available({QStringLiteral("windows"), QStringLiteral("9.9"),
                                QUrl(QStringLiteral("https://rtsoft.com/files/PatchyWindowsInstaller.exe"))});
  CHECK(saw_dialog);
}

void ui_update_preference_persists_startup_check_setting() {
  SettingsValueRestorer restore_update_check(QStringLiteral("updates/checkOnStartup"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("updates/checkOnStartup"), false);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("updates/checkOnStartup"), true);
    settings.sync();
  }

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesCheckForUpdatesCheck"));
    CHECK(check != nullptr);
    CHECK(check->isChecked());
    check->setChecked(false);
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);

  auto settings = patchy::ui::app_settings();
  CHECK(!settings.value(QStringLiteral("updates/checkOnStartup"), true).toBool());
}

void ui_update_preference_defaults_startup_check_setting_to_enabled() {
  SettingsValueRestorer restore_update_check(QStringLiteral("updates/checkOnStartup"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("updates/checkOnStartup"));
    settings.sync();
  }

  auto settings = patchy::ui::app_settings();
  CHECK(!settings.contains(QStringLiteral("updates/checkOnStartup")));
  CHECK(settings.value(QStringLiteral("updates/checkOnStartup"), true).toBool());
}

void ui_language_switch_updates_existing_window() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  const auto initial_tab_count = tabs->count();

  choose_preferences_language(window, QStringLiteral("ja"));

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("ja"));
  const auto japanese_menus = top_level_menu_texts(*window.menuBar());
  CHECK(japanese_menus.contains(QStringLiteral("ファイル(F)")));
  CHECK(!japanese_menus.contains(QStringLiteral("環境設定(P)")));
  CHECK(tabs->count() == initial_tab_count);
  CHECK(require_action(window, "preferencesLanguageJapaneseAction")->isChecked());

  choose_preferences_language(window, QStringLiteral("en"));

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("en"));
  const auto english_menus = top_level_menu_texts(*window.menuBar());
  CHECK(english_menus.contains(QStringLiteral("File")));
  CHECK(!english_menus.contains(QStringLiteral("Preferences")));
  CHECK(require_action(window, "fileSaveAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_S));
  CHECK(tabs->count() == initial_tab_count);
  CHECK(require_action(window, "preferencesLanguageEnglishAction")->isChecked());
}

void ui_language_preference_applies_at_startup() {
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/language"), QStringLiteral("ja"));
    settings.sync();
  }
  patchy::ui::LocalizationManager::instance().load_saved_language();

  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("ja"));
  const auto menus = top_level_menu_texts(*window.menuBar());
  CHECK(menus.contains(QStringLiteral("ファイル(F)")));
  CHECK(!menus.contains(QStringLiteral("環境設定(P)")));
  CHECK(require_action(window, "preferencesLanguageJapaneseAction")->isChecked());
}

void ui_language_invalid_preference_falls_back_to_english() {
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/language"), QStringLiteral("zz"));
    settings.sync();
  }
  patchy::ui::LocalizationManager::instance().load_saved_language();

  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("en"));
  const auto menus = top_level_menu_texts(*window.menuBar());
  CHECK(menus.contains(QStringLiteral("File")));
  CHECK(!menus.contains(QStringLiteral("Preferences")));
  CHECK(require_action(window, "preferencesLanguageEnglishAction")->isChecked());
}

void ui_language_catalog_covers_dialog_status_and_properties() {
  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("ja"), false));
  QApplication::processEvents();

  const auto canvas_status = QCoreApplication::translate(
      "patchy::ui::CanvasWidget", "Select a normal pixel layer before painting on text");
  CHECK(canvas_status == QStringLiteral("テキスト上に描画する前に通常のピクセルレイヤーを選択してください"));

  const auto save_title = QCoreApplication::translate("patchy::ui::MainWindow", "Save changes?");
  CHECK(save_title == QStringLiteral("変更を保存しますか?"));
  const auto save_prompt =
      QCoreApplication::translate("patchy::ui::MainWindow", "Save changes to %1 before closing?");
  CHECK(save_prompt == QStringLiteral("閉じる前に %1 への変更を保存しますか?"));

  const auto no_layer = QCoreApplication::translate("patchy::ui::MainWindow", "Layer: No active layer");
  CHECK(no_layer == QStringLiteral("レイヤー: アクティブレイヤーなし"));
  const auto document_info = QCoreApplication::translate(
      "patchy::ui::MainWindow", "Document: %1 x %2 px | %3 ppi | %4 | %5 layers | Zoom %6% | %7");
  CHECK(document_info.startsWith(QStringLiteral("ドキュメント:")));
  const auto bmp_depth = QCoreApplication::translate("QObject", "Color depth");
  CHECK(bmp_depth == QStringLiteral("色深度"));
  const auto bmp_quantize = QCoreApplication::translate("QObject", "Reduce colors automatically");
  CHECK(bmp_quantize == QStringLiteral("色数を自動的に減らす"));
  const auto bmp_palette_file = QCoreApplication::translate("QObject", "Use palette file");
  CHECK(bmp_palette_file == QStringLiteral("パレットファイルを使用"));
  const auto settings_file = QCoreApplication::translate("QObject", "Settings file:");
  CHECK(settings_file == QStringLiteral("設定ファイル:"));
  const auto open_settings_folder = QCoreApplication::translate("QObject", "Open Settings Folder");
  CHECK(open_settings_folder == QStringLiteral("設定フォルダーを開く"));
  const auto settings_folder_failed = QCoreApplication::translate("QObject", "Could not open settings folder.");
  CHECK(settings_folder_failed == QStringLiteral("設定フォルダーを開けませんでした。"));
  const auto checking_updates = QCoreApplication::translate("QObject", "Checking for updates...");
  CHECK(checking_updates == QStringLiteral("更新を確認しています..."));
  const auto up_to_date = QCoreApplication::translate("QObject", "Patchy is up to date (%1).");
  CHECK(up_to_date == QStringLiteral("Patchy は最新です (%1)。"));
  const auto update_failed =
      QCoreApplication::translate("QObject", "Update check failed: invalid update manifest.");
  CHECK(update_failed == QStringLiteral("更新確認に失敗しました: 更新マニフェストが無効です。"));

  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false));
  QApplication::processEvents();
}

void ui_about_dialog_shows_labeled_external_links() {
  bool inspected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchySplashScreen"));
    CHECK(dialog != nullptr);

    const auto link_labels = dialog->findChildren<QLabel*>(QStringLiteral("splashHome"));
    CHECK(link_labels.size() == 2);
    QString combined_text;
    for (const auto* label : link_labels) {
      CHECK(label->textFormat() == Qt::RichText);
      CHECK(label->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse));
      CHECK(label->openExternalLinks());
      combined_text += label->text();
      combined_text += QLatin1Char('\n');
    }

    CHECK(combined_text.contains(QStringLiteral("GitHub: ")));
    CHECK(combined_text.contains(QStringLiteral("href=\"https://github.com/SethRobinson/Patchy\"")));
    CHECK(combined_text.contains(QStringLiteral(">SethRobinson/Patchy</a>")));
    CHECK(combined_text.contains(QStringLiteral("Seth's site: ")));
    CHECK(combined_text.contains(QStringLiteral("href=\"https://rtsoft.com\"")));
    CHECK(combined_text.contains(QStringLiteral(">rtsoft.com</a>")));

    auto* settings_caption = dialog->findChild<QLabel*>(QStringLiteral("splashSettingsCaption"));
    CHECK(settings_caption != nullptr);
    CHECK(settings_caption->text() == QStringLiteral("Settings file:"));
    auto* settings_path = dialog->findChild<QLabel*>(QStringLiteral("splashSettingsPath"));
    CHECK(settings_path != nullptr);
    CHECK(settings_path->text() == QDir::toNativeSeparators(patchy::ui::app_settings().fileName()));
    CHECK(settings_path->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
    CHECK(settings_path->wordWrap());
    auto* open_settings_folder = dialog->findChild<QPushButton*>(QStringLiteral("splashOpenSettingsFolderButton"));
    CHECK(open_settings_folder != nullptr);
    CHECK(open_settings_folder->text() == QStringLiteral("Open Settings Folder"));

    save_widget_artifact("ui_about_dialog_links", *dialog);
    inspected = true;
    dialog->accept();
  });

  patchy::ui::show_about_splash();
  CHECK(inspected);
}

void ui_frameless_window_edges_resize() {
  patchy::ui::MainWindow window;
  show_window(window);
  window.resize(980, 720);
  QApplication::processEvents();

#ifdef Q_OS_WIN
  if (QGuiApplication::platformName() == QStringLiteral("windows")) {
    const auto style = GetWindowLongPtrW(reinterpret_cast<HWND>(window.winId()), GWL_STYLE);
    CHECK((style & WS_THICKFRAME) != 0);
    CHECK((style & WS_CAPTION) == 0);
  }
#endif

  const auto start = window.geometry();
  const QPoint right_edge(window.width() - 2, window.height() / 2);
  send_mouse(window, QEvent::MouseButtonPress, right_edge, Qt::LeftButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseMove, right_edge + QPoint(90, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseButtonRelease, right_edge + QPoint(90, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.width() >= start.width() + 70);
  CHECK(window.height() == start.height());

  const auto widened = window.geometry();
  const QPoint bottom_right(window.width() - 2, window.height() - 2);
  send_mouse(window, QEvent::MouseButtonPress, bottom_right, Qt::LeftButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseMove, bottom_right + QPoint(45, 55), Qt::NoButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseButtonRelease, bottom_right + QPoint(45, 55), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.width() >= widened.width() + 30);
  CHECK(window.height() >= widened.height() + 40);

  const auto expanded = window.geometry();
  const QPoint left_edge(2, window.height() / 2);
  send_mouse(window, QEvent::MouseButtonPress, left_edge, Qt::LeftButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseMove, left_edge - QPoint(60, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseButtonRelease, left_edge - QPoint(60, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.x() <= expanded.x() - 45);
  CHECK(window.width() >= expanded.width() + 45);
}

void ui_right_edge_scrollbars_remain_draggable() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  for (int index = 0; index < 48; ++index) {
    document.add_pixel_layer("Scrollable Layer " + std::to_string(index + 1),
                             solid_pixels(64, 64, patchy::PixelFormat::rgba8(),
                                          QColor(40 + index * 3 % 180, 80, 220, 255)));
  }

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Scrollbar Edge"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  layer_list->setFixedHeight(180);
  QApplication::processEvents();

  auto* scroll = layer_list->verticalScrollBar();
  CHECK(scroll != nullptr);
  CHECK(scroll->isVisible());
  CHECK(scroll->maximum() > 0);
  scroll->setValue(scroll->maximum() / 3);
  QApplication::processEvents();

  QStyleOptionSlider option;
  option.initFrom(scroll);
  option.orientation = scroll->orientation();
  option.minimum = scroll->minimum();
  option.maximum = scroll->maximum();
  option.singleStep = scroll->singleStep();
  option.pageStep = scroll->pageStep();
  option.sliderPosition = scroll->sliderPosition();
  option.sliderValue = scroll->value();
  option.upsideDown = scroll->invertedAppearance();
  const auto handle = scroll->style()->subControlRect(QStyle::CC_ScrollBar, &option,
                                                      QStyle::SC_ScrollBarSlider, scroll);
  CHECK(handle.isValid());
  const auto start_x = std::clamp(scroll->width() - 2, handle.left(), handle.right());
  const QPoint start(start_x, handle.center().y());
  CHECK(handle.contains(start));
  CHECK(scroll->mapTo(&window, start).x() >= window.width() - 10);

  const auto geometry_before = window.geometry();
  const auto scroll_before = scroll->value();
  const auto end = start + QPoint(0, 55);
  send_mouse(*scroll, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*scroll, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  send_mouse(*scroll, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(window.geometry() == geometry_before);
  CHECK(scroll->value() > scroll_before);
}

void ui_svg_icon_resources_are_registered() {
  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/new.svg")));
  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/mask.svg")));
  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/selection-add.svg")));

  const QIcon icon(QStringLiteral(":/patchy/icons/new.svg"));
  CHECK(!icon.isNull());
  CHECK(!icon.pixmap(QSize(32, 32)).isNull());

  CHECK(!require_action(window, "layerNewAction")->icon().isNull());
  CHECK(!require_action(window, "layerAddMaskFromSelectionAction")->icon().isNull());
}

void ui_filter_menu_groups_builtin_filters() {
  patchy::ui::MainWindow window;
  show_window(window);

  auto* filter_menu = window.findChild<QMenu*>(QStringLiteral("filterMenu"));
  auto* photo_looks = window.findChild<QMenu*>(QStringLiteral("filterPhotoLooksMenu"));
  auto* blur = window.findChild<QMenu*>(QStringLiteral("filterBlurMenu"));
  auto* sharpen = window.findChild<QMenu*>(QStringLiteral("filterSharpenMenu"));
  auto* distort = window.findChild<QMenu*>(QStringLiteral("filterDistortMenu"));
  auto* noise = window.findChild<QMenu*>(QStringLiteral("filterNoiseMenu"));
  auto* pixelate = window.findChild<QMenu*>(QStringLiteral("filterPixelateMenu"));
  auto* stylize = window.findChild<QMenu*>(QStringLiteral("filterStylizeMenu"));
  auto* render = window.findChild<QMenu*>(QStringLiteral("filterRenderMenu"));
  CHECK(filter_menu != nullptr);
  CHECK(photo_looks != nullptr);
  CHECK(blur != nullptr);
  CHECK(sharpen != nullptr);
  CHECK(distort != nullptr);
  CHECK(noise != nullptr);
  CHECK(pixelate != nullptr);
  CHECK(stylize != nullptr);
  CHECK(render != nullptr);

  CHECK(filter_menu->actions().contains(photo_looks->menuAction()));
  CHECK(photo_looks->actions().contains(require_action(window, "filterAction_patchy_filters_soft_glow")));
  CHECK(photo_looks->actions().contains(require_action(window, "filterAction_patchy_filters_vignette")));
  CHECK(blur->actions().contains(require_action(window, "filterAction_patchy_filters_motion_blur")));
  CHECK(blur->actions().contains(require_action(window, "filterAction_patchy_filters_radial_blur")));
  CHECK(sharpen->actions().contains(require_action(window, "filterAction_patchy_filters_unsharp_mask")));
  CHECK(distort->actions().contains(require_action(window, "filterAction_patchy_filters_wave")));
  CHECK(distort->actions().contains(require_action(window, "filterAction_patchy_filters_pinch_bloat")));
  CHECK(noise->actions().contains(require_action(window, "filterAction_patchy_filters_film_grain")));
  CHECK(pixelate->actions().contains(require_action(window, "filterAction_patchy_filters_color_halftone")));
  CHECK(stylize->actions().contains(require_action(window, "filterAction_patchy_filters_glowing_edges")));
  CHECK(render->actions().contains(require_action(window, "filterAction_patchy_filters_clouds")));
  CHECK(window.findChild<QAction*>(QStringLiteral("filterAction_patchy_filters_threshold")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("filterAction_patchy_filters_posterize")) == nullptr);
}

void ui_filter_progress_callback_can_cancel_heavy_filter() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto pixels =
      solid_pixels(96, 96, patchy::PixelFormat::rgb8(), QColor(120, 70, 210));
  bool saw_progress = false;
  patchy::ui::FilterProgress progress{[&](int completed, int total, const QString& detail) {
    saw_progress = true;
    CHECK(total == 96);
    CHECK(!detail.isEmpty());
    return completed < 4;
  }};

  bool cancelled = false;
  try {
    (void)patchy::ui::build_filter_preview_pixels(
        pixels, QRegion(), patchy::Rect{0, 0, 96, 96}, QStringLiteral("patchy.filters.gaussian_blur"),
        registry, patchy::ui::FilterPreviewSettings{true, {12}}, Qt::black, Qt::white, &progress);
  } catch (const patchy::ui::FilterCancelled&) {
    cancelled = true;
  }

  CHECK(saw_progress);
  CHECK(cancelled);
}

void ui_filter_progress_callback_can_cancel_simple_filter() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto pixels = solid_pixels(32, 32, patchy::PixelFormat::rgb8(), QColor(120, 70, 210));
  bool saw_progress = false;
  bool saw_filtering_detail = false;
  patchy::ui::FilterProgress progress{[&](int completed, int total, const QString& detail) {
    saw_progress = true;
    saw_filtering_detail = saw_filtering_detail || detail == QStringLiteral("Filtering pixels");
    CHECK(total > 0);
    return completed < 2;
  }};

  bool cancelled = false;
  try {
    (void)patchy::ui::build_filter_preview_pixels(
        pixels, QRegion(), patchy::Rect{0, 0, 32, 32}, QStringLiteral("patchy.filters.sepia"), registry,
        patchy::ui::FilterPreviewSettings{true, {100}}, Qt::black, Qt::white, &progress);
  } catch (const patchy::ui::FilterCancelled&) {
    cancelled = true;
  }

  CHECK(saw_progress);
  CHECK(saw_filtering_detail);
  CHECK(cancelled);
}

void ui_all_builtin_filters_report_progress() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);

  for (const auto& filter : registry.filters()) {
    const auto identifier = QString::fromStdString(filter.identifier);
    const auto spec = patchy::ui::filter_dialog_spec_for(filter);
    std::vector<int> values;
    values.reserve(spec.controls.size());
    for (const auto& control : spec.controls) {
      values.push_back(control.value);
    }

    const auto pixels = solid_pixels(24, 24, patchy::PixelFormat::rgb8(), QColor(120, 70, 210));
    bool saw_progress = false;
    patchy::ui::FilterProgress progress{[&](int completed, int total, const QString& detail) {
      saw_progress = true;
      CHECK(total > 0);
      CHECK(completed >= 0);
      CHECK(!detail.isEmpty());
      return false;
    }};

    bool cancelled = false;
    try {
      (void)patchy::ui::build_filter_preview_pixels(
          pixels, QRegion(), patchy::Rect{0, 0, 24, 24}, identifier, registry,
          patchy::ui::FilterPreviewSettings{true, values}, Qt::black, Qt::white, &progress);
    } catch (const patchy::ui::FilterCancelled&) {
      cancelled = true;
    }

    CHECK(saw_progress);
    CHECK(cancelled);
  }
}

void ui_adjustment_pixel_progress_callback_can_cancel() {
  auto pixels = solid_pixels(32, 32, patchy::PixelFormat::rgb8(), QColor(120, 70, 210));
  bool saw_progress = false;
  patchy::ui::FilterProgress progress{[&](int completed, int total, const QString& detail) {
    saw_progress = true;
    CHECK(total > 0);
    CHECK(completed >= 0);
    CHECK(detail == QStringLiteral("Filtering pixels"));
    return false;
  }};

  bool cancelled = false;
  try {
    patchy::ui::apply_hue_saturation_to_pixels(pixels, patchy::Rect{0, 0, 32, 32}, QRegion(),
                                               patchy::ui::HueSaturationSettings{120, 0, 0}, &progress);
  } catch (const patchy::ui::FilterCancelled&) {
    cancelled = true;
  }

  CHECK(saw_progress);
  CHECK(cancelled);
}

void ui_filter_settings_dialog_shows_before_initial_preview() {
  bool preview_ran = false;
  bool preview_saw_visible_dialog = false;
  const patchy::ui::FilterDialogSpec spec{QStringLiteral("patchy.filters.sepia"), QStringLiteral("Deferred Preview"),
                                          {{QStringLiteral("Amount"), QStringLiteral("filterAmount"), 0, 100, 100,
                                            QStringLiteral("%")}}};

  const auto settings = patchy::ui::request_filter_settings(nullptr, spec, [&](patchy::ui::FilterPreviewSettings) {
    preview_ran = true;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyFilterDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      preview_saw_visible_dialog = dialog->isVisible();
      dialog->accept();
      return;
    }
  });

  CHECK(preview_ran);
  CHECK(preview_saw_visible_dialog);
  CHECK(settings.has_value());
}

void ui_filter_settings_dialog_coalesces_rapid_slider_preview_callbacks() {
  int preview_calls = 0;
  int latest_preview_value = -1;
  const patchy::ui::FilterDialogSpec spec{QStringLiteral("patchy.filters.sepia"), QStringLiteral("Coalesced Preview"),
                                          {{QStringLiteral("Amount"), QStringLiteral("filterAmount"), 0, 100, 0,
                                            QStringLiteral("%")}}};

  QTimer::singleShot(0, [] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* amount = dialog->findChild<QSpinBox*>(QStringLiteral("filterAmountSpin"));
    CHECK(amount != nullptr);
    for (int value = 1; value <= 24; ++value) {
      amount->setValue(value);
    }
    QTimer::singleShot(120, dialog, [dialog] { dialog->accept(); });
  });

  const auto settings = patchy::ui::request_filter_settings(
      nullptr, spec, [&](patchy::ui::FilterPreviewSettings preview) {
        ++preview_calls;
        if (!preview.values.empty()) {
          latest_preview_value = preview.values.front();
        }
      });

  CHECK(settings.has_value());
  CHECK(!settings->empty());
  CHECK(settings->front() == 24);
  CHECK(latest_preview_value == 24);
  CHECK(preview_calls > 0);
  CHECK(preview_calls < 8);
}

void ui_filter_settings_dialog_delivers_latest_after_slow_preview_callback() {
  int preview_calls = 0;
  bool queued_changes = false;
  std::vector<int> preview_values;
  const patchy::ui::FilterDialogSpec spec{QStringLiteral("patchy.filters.sepia"), QStringLiteral("Latest Preview"),
                                          {{QStringLiteral("Amount"), QStringLiteral("filterAmount"), 0, 100, 0,
                                            QStringLiteral("%")}}};

  const auto settings = patchy::ui::request_filter_settings(
      nullptr, spec, [&](patchy::ui::FilterPreviewSettings preview) {
        ++preview_calls;
        if (!preview.values.empty()) {
          preview_values.push_back(preview.values.front());
        }
        if (queued_changes) {
          return;
        }
        queued_changes = true;
        QTimer::singleShot(0, [] {
          auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
          CHECK(dialog != nullptr);
          auto* amount = dialog->findChild<QSpinBox*>(QStringLiteral("filterAmountSpin"));
          CHECK(amount != nullptr);
          for (int value = 1; value <= 18; ++value) {
            amount->setValue(value);
          }
          QTimer::singleShot(120, dialog, [dialog] { dialog->accept(); });
        });
        QElapsedTimer slow_preview;
        slow_preview.start();
        while (slow_preview.elapsed() < 45) {
        }
      });

  CHECK(settings.has_value());
  CHECK(!settings->empty());
  CHECK(settings->front() == 18);
  CHECK(queued_changes);
  CHECK(preview_calls >= 2);
  CHECK(preview_calls < 8);
  CHECK(!preview_values.empty());
  CHECK(preview_values.back() == 18);
}

void ui_brightness_contrast_filter_applies_settings() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto* filter = registry.find("patchy.filters.brightness_contrast");
  CHECK(filter != nullptr);
  const auto spec = patchy::ui::filter_dialog_spec_for(*filter);
  CHECK(spec.display_name == QStringLiteral("Brightness/Contrast"));
  CHECK(spec.controls.size() == 2);
  CHECK(spec.controls[0].object_name == QStringLiteral("filterBrightness"));
  CHECK(spec.controls[0].value == 0);
  CHECK(spec.controls[1].object_name == QStringLiteral("filterContrast"));
  CHECK(spec.controls[1].value == 0);

  const auto source = solid_pixels(1, 1, patchy::PixelFormat::rgb8(), QColor(120, 70, 210));
  const auto apply = [&](std::vector<int> values) {
    return patchy::ui::build_filter_preview_pixels(
        source, QRegion(), patchy::Rect{0, 0, 1, 1}, QStringLiteral("patchy.filters.brightness_contrast"), registry,
        patchy::ui::FilterPreviewSettings{true, std::move(values)}, Qt::black, Qt::white);
  };
  const auto check_pixel = [](const patchy::PixelBuffer& pixels, int red, int green, int blue) {
    const auto* px = pixels.pixel(0, 0);
    CHECK(px[0] == red);
    CHECK(px[1] == green);
    CHECK(px[2] == blue);
  };

  check_pixel(apply({0, 0}), 120, 70, 210);
  check_pixel(apply({24, 0}), 144, 94, 234);
  check_pixel(apply({0, 25}), 118, 56, 231);
  check_pixel(apply({10, 50}), 126, 51, 255);
}

void ui_filter_preview_restores_unselected_region_runs() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto pixels = solid_pixels(4, 3, patchy::PixelFormat::rgb8(), QColor(20, 30, 40));
  QRegion selection(QRect(11, 21, 2, 1));
  selection = selection.united(QRegion(QRect(13, 22, 1, 1)));

  const auto result = patchy::ui::build_filter_preview_pixels(
      pixels, selection, patchy::Rect{10, 20, 4, 3}, QStringLiteral("patchy.filters.brightness_contrast"), registry,
      patchy::ui::FilterPreviewSettings{true, {10, 0}}, Qt::black, Qt::white);

  for (std::int32_t y = 0; y < result.height(); ++y) {
    for (std::int32_t x = 0; x < result.width(); ++x) {
      const auto selected = selection.contains(QPoint(10 + x, 20 + y));
      const auto* px = result.pixel(x, y);
      CHECK(px[0] == static_cast<std::uint8_t>(selected ? 30 : 20));
      CHECK(px[1] == static_cast<std::uint8_t>(selected ? 40 : 30));
      CHECK(px[2] == static_cast<std::uint8_t>(selected ? 50 : 40));
    }
  }
}

void ui_all_builtin_filters_render_stroke_contact_sheet() {
  ensure_artifact_dir();
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto source = make_filter_stroke_source();
  const auto bounds = patchy::Rect{0, 0, source.width(), source.height()};

  std::vector<std::pair<QString, QImage>> cells;
  cells.push_back({QStringLiteral("Original"), flattened_on_white(source)});
  for (const auto& filter : registry.filters()) {
    const auto spec = patchy::ui::filter_dialog_spec_for(filter);
    std::vector<int> values;
    values.reserve(spec.controls.size());
    for (const auto& control : spec.controls) {
      values.push_back(control.value);
    }
    const auto result = patchy::ui::build_filter_preview_pixels(
        source, QRegion(), bounds, spec.identifier, registry, patchy::ui::FilterPreviewSettings{true, values},
        QColor(220, 28, 24), QColor(255, 255, 255));
    if (spec.identifier == QStringLiteral("patchy.filters.box_blur") ||
        spec.identifier == QStringLiteral("patchy.filters.gaussian_blur") ||
        spec.identifier == QStringLiteral("patchy.filters.motion_blur") ||
        spec.identifier == QStringLiteral("patchy.filters.radial_blur") ||
        spec.identifier == QStringLiteral("patchy.filters.pixelate")) {
      CHECK(spatial_filter_spreads_clean_red_alpha(source, result));
    }
    if (spec.identifier == QStringLiteral("patchy.filters.clouds")) {
      CHECK(result.pixel(0, 0)[3] == 255);
      CHECK(result.pixel(result.width() - 1, result.height() - 1)[3] == 255);
    }
    cells.push_back({spec.display_name, flattened_on_white(result)});
  }

  constexpr int kColumns = 5;
  constexpr int kCellWidth = 250;
  constexpr int kCellHeight = 220;
  constexpr int kPadding = 10;
  const auto rows = static_cast<int>((cells.size() + kColumns - 1) / kColumns);
  QImage sheet(kColumns * kCellWidth, rows * kCellHeight, QImage::Format_RGB32);
  sheet.fill(QColor(30, 32, 36));
  QPainter painter(&sheet);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.setFont(visual_test_font());
  painter.setPen(QColor(225, 230, 238));
  for (std::size_t index = 0; index < cells.size(); ++index) {
    const auto column = static_cast<int>(index % kColumns);
    const auto row = static_cast<int>(index / kColumns);
    const QRect cell(column * kCellWidth, row * kCellHeight, kCellWidth, kCellHeight);
    painter.fillRect(cell.adjusted(4, 4, -4, -4), QColor(42, 45, 51));
    painter.drawText(cell.adjusted(kPadding, 8, -kPadding, -kPadding), Qt::AlignTop | Qt::AlignLeft,
                     cells[index].first);
    const QRect image_rect = cell.adjusted(kPadding, 34, -kPadding, -kPadding);
    const auto scaled = cells[index].second.scaled(image_rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint image_pos(image_rect.x() + (image_rect.width() - scaled.width()) / 2,
                           image_rect.y() + (image_rect.height() - scaled.height()) / 2);
    painter.drawImage(image_pos, scaled);
  }
  painter.end();
  CHECK(sheet.save(QStringLiteral("test-artifacts/ui_all_builtin_filters_stroke_contact_sheet.png")));
}

void ui_color_picker_changes_foreground_color() {
  ensure_artifact_dir();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(foreground != nullptr);

  foreground->click();
  QApplication::processEvents();
  QDialog* dialog = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("patchyColorDialog")) {
      dialog = qobject_cast<QDialog*>(widget);
      break;
    }
  }
  CHECK(dialog != nullptr);
  CHECK(dialog->isVisible());
  CHECK(!dialog->isModal());
  CHECK(dialog->windowModality() == Qt::NonModal);
  CHECK(dialog->windowFlags().testFlag(Qt::FramelessWindowHint));
  CHECK(dialog->findChild<QWidget*>(QStringLiteral("dialogChromeTitleBar")) != nullptr);
  CHECK(dialog->findChild<QToolButton*>(QStringLiteral("dialogChromeCloseButton")) != nullptr);

  auto* picker = dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
  CHECK(picker != nullptr);
  auto* color_plane = picker->findChild<QWidget*>(QStringLiteral("patchyColorPlane"));
  CHECK(color_plane != nullptr);
  const auto color_plane_image = color_plane->grab().toImage();
  CHECK(color_close(color_plane_image.pixelColor(1, 1), QColor(255, 255, 255), 4));
  CHECK(color_close(color_plane_image.pixelColor(color_plane_image.width() - 2, 1), QColor(255, 0, 0), 4));
  CHECK(color_close(color_plane_image.pixelColor(color_plane_image.width() / 2, color_plane_image.height() - 2),
                    QColor(0, 0, 0), 4));
  send_mouse(*color_plane, QEvent::MouseButtonPress, QPoint(0, 0), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*color_plane, QEvent::MouseButtonRelease, QPoint(0, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(255, 255, 255));
  auto custom_swatches = picker->findChildren<QPushButton*>(QStringLiteral("patchyCustomColorSwatch"));
  auto* update_custom = picker->findChild<QPushButton*>(QStringLiteral("patchyUpdateCustomColorButton"));
  CHECK(custom_swatches.size() == 16);
  CHECK(update_custom != nullptr);
  CHECK(picker->findChild<QPushButton*>(QStringLiteral("patchyDeleteCustomColorButton")) == nullptr);
  CHECK(!update_custom->isEnabled());
  custom_swatches.front()->click();
  QApplication::processEvents();
  CHECK(update_custom->isEnabled());
  picker->setCurrentColor(QColor(10, 20, 30));
  update_custom->click();
  QApplication::processEvents();
  picker->setCurrentColor(QColor(200, 210, 220));
  custom_swatches.front()->click();
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(10, 20, 30));
  picker->setCurrentColor(QColor(30, 40, 50));
  update_custom->click();
  picker->setCurrentColor(QColor(90, 80, 70));
  custom_swatches.front()->click();
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(30, 40, 50));
  picker->setCurrentColor(QColor(80, 120, 200));
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(80, 120, 200));
  dialog->grab().save(QStringLiteral("test-artifacts/ui_color_picker_gradient.png"));
  picker->setCurrentColor(QColor(12, 180, 240));
  QApplication::processEvents();
  dialog->grab().save(QStringLiteral("test-artifacts/ui_color_picker.png"));
  CHECK(canvas->primary_color() == QColor(12, 180, 240));
  CHECK(std::filesystem::exists(std::filesystem::path("test-artifacts") / "ui_color_picker.png"));
  save_widget_artifact("ui_color_picker_result", window);
  dialog->close();
  QApplication::processEvents();
}

void ui_dialog_position_memory_restores_last_position() {
  const auto settings_group = QStringLiteral("dialogPositions/patchyDialogPositionMemoryTest");
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(settings_group);
    settings.sync();
  }

  const auto screen_rect = QApplication::primaryScreen() != nullptr
                               ? QApplication::primaryScreen()->availableGeometry()
                               : QRect(0, 0, 640, 480);
  const auto target_position = screen_rect.topLeft() + QPoint(37, 43);
  QPoint saved_position;

  {
    QDialog dialog;
    dialog.setObjectName(QStringLiteral("patchyDialogPositionMemoryTest"));
    dialog.resize(140, 90);
    patchy::ui::remember_dialog_position(dialog);
    dialog.show();
    QApplication::processEvents();
    dialog.move(target_position);
    QApplication::processEvents();
    saved_position = dialog.pos();
    dialog.close();
    QApplication::processEvents();
  }

  {
    QDialog dialog;
    dialog.setObjectName(QStringLiteral("patchyDialogPositionMemoryTest"));
    dialog.resize(140, 90);
    patchy::ui::remember_dialog_position(dialog);
    dialog.show();
    QApplication::processEvents();
    CHECK((dialog.pos() - saved_position).manhattanLength() <= 2);
    dialog.close();
    QApplication::processEvents();
  }

  auto settings = patchy::ui::app_settings();
  settings.remove(settings_group);
  settings.sync();
}

void ui_dialog_position_memory_centers_unmoved_dialogs_on_parent() {
  const auto settings_group = QStringLiteral("dialogPositions/patchyDialogCenterTest");
  const auto screen_rect = QApplication::primaryScreen() != nullptr
                               ? QApplication::primaryScreen()->availableGeometry()
                               : QRect(0, 0, 640, 480);
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(settings_group);
    settings.setValue(settings_group + QStringLiteral("/pos"), screen_rect.topLeft() + QPoint(4, 5));
    settings.sync();
  }

  QWidget parent;
  parent.resize(420, 260);
  parent.move(screen_rect.topLeft() + QPoint(80, 70));
  parent.show();
  QApplication::processEvents();

  QDialog dialog(&parent);
  dialog.setObjectName(QStringLiteral("patchyDialogCenterTest"));
  dialog.resize(140, 90);
  patchy::ui::remember_dialog_position(dialog);
  dialog.show();
  QApplication::processEvents();

  const auto expected_position =
      parent.frameGeometry().center() - QPoint(dialog.size().width() / 2, dialog.size().height() / 2);
  CHECK((dialog.pos() - expected_position).manhattanLength() <= 10);
  dialog.close();
  QApplication::processEvents();

  auto settings = patchy::ui::app_settings();
  CHECK(!settings.value(settings_group + QStringLiteral("/pos")).isValid());
  settings.remove(settings_group);
  settings.sync();
}

void ui_dirty_state_marks_tabs_and_undo_restores_saved_revision() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  CHECK(!tabs->tabText(tabs->currentIndex()).endsWith(QStringLiteral("*")));

  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(tabs->tabText(tabs->currentIndex()).endsWith(QStringLiteral("*")));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(!tabs->tabText(tabs->currentIndex()).endsWith(QStringLiteral("*")));
}

void ui_compatibility_report_flags_psd_text_placeholders() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer("PSD Text", solid_pixels(120, 90, patchy::PixelFormat::rgba8(),
                                                                  QColor(0, 0, 0, 0)));
  layer.metadata()[patchy::kLayerMetadataText] = "Title";
  layer.metadata()[patchy::kLayerMetadataTextFont] = "PSD Text";
  layer.metadata()[patchy::kLayerMetadataTextSourceBlock] = "TySh";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "placeholder";
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"TySh", {1, 2, 3}});
  auto warnings = patchy::ui::compatibility_warnings_for_document(document);
  CHECK(!warnings.isEmpty());
  CHECK(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("placeholder")));
  CHECK(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("TySh")));
  CHECK(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("unknown PSD layer block")));
}

void ui_compatibility_report_ignores_patchy_written_psd_blocks() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
  auto& text_layer = document.add_pixel_layer(
      "Text: Hey man", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.distance = 4.0F;
  shadow.size = 6.0F;
  text_layer.layer_style().drop_shadows.push_back(shadow);

  const auto round_tripped = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto warnings = patchy::ui::compatibility_warnings_for_document(round_tripped);
  CHECK(warnings.isEmpty());
}

void ui_compatibility_report_treats_levels_as_native_psd_adjustment() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Levels;
  settings.levels.red.black_output = 128;
  patchy::Layer levels(document.allocate_layer_id(), "Levels", patchy::LayerKind::Adjustment);
  levels.set_bounds(patchy::Rect::from_size(document.width(), document.height()));
  patchy::configure_adjustment_layer(levels, settings);
  document.add_layer(std::move(levels));

  const auto warnings = patchy::ui::compatibility_warnings_for_document(document);
  CHECK(warnings.isEmpty());
}

void ui_compatibility_report_flags_cmyk_rgb_conversion() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgb8());
  document.metadata().values["psd.color_mode"] = "CMYK";
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgb8(), QColor(Qt::white)));

  const auto warnings = patchy::ui::compatibility_warnings_for_document(document);
  CHECK(!warnings.isEmpty());
  const auto text = warnings.join(QLatin1Char('\n'));
  CHECK(text.contains(QStringLiteral("CMYK")));
  CHECK(text.contains(QStringLiteral("converted")));
  CHECK(text.contains(QStringLiteral("RGB/RGBA")));
}

void ui_alt_left_click_samples_foreground_color() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(12, 180, 240));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  const QPoint sample_document_point(80, 80);
  const auto sampled_before_click = canvas_pixel(*canvas, sample_document_point);
  CHECK(color_close(sampled_before_click, QColor(12, 180, 240), 4));

  canvas->set_primary_color(QColor(230, 20, 20));
  const auto sample_widget_point = canvas->widget_position_for_document_point(sample_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, sample_widget_point, Qt::LeftButton, Qt::LeftButton,
             Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, sample_widget_point, Qt::LeftButton, Qt::NoButton,
             Qt::AltModifier);
  QApplication::processEvents();

  CHECK(color_close(canvas->primary_color(), QColor(12, 180, 240), 4));
  CHECK(color_close(canvas_pixel(*canvas, sample_document_point), sampled_before_click, 0));
}

void ui_photoshop_shortcuts_are_registered() {
  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(require_action_by_text(window, QStringLiteral("New"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_N));
  CHECK(require_action_by_text(window, QStringLiteral("Open..."))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_O));
  CHECK(require_action_by_text(window, QStringLiteral("Save"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_S));
  CHECK(require_action_by_text(window, QStringLiteral("Save As..."))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
  CHECK(require_action_by_text(window, QStringLiteral("Close"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_W));
  CHECK(require_action_by_text(window, QStringLiteral("Close All"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
  CHECK(require_action(window, "filePrintAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_P));
  CHECK(require_action(window, "filePageSetupAction") != nullptr);
  CHECK(require_action_by_text(window, QStringLiteral("Undo"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Z));
  CHECK(require_action_by_text(window, QStringLiteral("Redo"))->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
  CHECK(require_action_by_text(window, QStringLiteral("Cut"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_X));
  CHECK(require_action_by_text(window, QStringLiteral("Copy"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_C));
  CHECK(require_action_by_text(window, QStringLiteral("Paste"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_V));
  CHECK(require_action_by_text(window, QStringLiteral("Free Transform..."))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::Key_T));
  CHECK(require_action_by_text(window, QStringLiteral("Select All"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_A));
  CHECK(require_action_by_text(window, QStringLiteral("Clear Selection"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_D));
  CHECK(require_action_by_text(window, QStringLiteral("Reselect"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
  CHECK(require_action_by_text(window, QStringLiteral("Inverse"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
  CHECK(require_action(window, "selectExpandAction") != nullptr);
  CHECK(require_action(window, "selectContractAction") != nullptr);
  CHECK(require_action(window, "selectBorderAction") != nullptr);
  CHECK(require_action(window, "selectLayerTransparencyAction") != nullptr);
  CHECK(require_action(window, "selectGrowAction") != nullptr);
  CHECK(require_action(window, "selectSimilarAction") != nullptr);
  CHECK(require_action(window, "layerEditAdjustmentAction") != nullptr);
  CHECK(require_action_by_text(window, QStringLiteral("New Layer"))->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
  CHECK(require_action_by_text(window, QStringLiteral("Layer Via Copy"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_J));
  CHECK(require_action_by_text(window, QStringLiteral("Layer Via Cut"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_J));
  CHECK(require_action_by_text(window, QStringLiteral("Duplicate Layer"))->shortcut().isEmpty());
  CHECK(require_action(window, "layerFillForegroundAction")->shortcut() == QKeySequence(Qt::ALT | Qt::Key_Backspace));
  CHECK(require_action(window, "layerFillBackgroundAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Backspace));
  CHECK(require_action_by_text(window, QStringLiteral("Merge Down"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_E));
  CHECK(require_action_by_text(window, QStringLiteral("Merge Visible to New Layer"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
  CHECK(require_action(window, "imageSizeAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I));
  CHECK(require_action_by_text(window, QStringLiteral("Canvas Size..."))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
  CHECK(require_action(window, "imageAdjustInvertAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_I));
  CHECK(require_action(window, "imageAdjustLevelsAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_L));
  CHECK(require_action(window, "imageAdjustCurvesAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_M));
  CHECK(require_action(window, "imageAdjustHueSaturationAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_U));
  CHECK(require_action(window, "imageAdjustColorBalanceAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_B));
  CHECK(require_action(window, "imageAdjustDesaturateAction")->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
  CHECK(require_action(window, "imageAdjustAutoContrastAction")->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_L));
  CHECK(require_action(window, "imageAdjustBrightnessContrastAction")->shortcut().isEmpty());
  CHECK(window.findChild<QAction*>(QStringLiteral("imageAdjustBrightnessAction")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("imageAdjustContrastAction")) == nullptr);
  CHECK(require_action(window, "viewToggleSelectionEdgesAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_H));
  CHECK(require_action(window, "viewToggleRulersAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_R));
  CHECK(require_action(window, "viewToggleGridAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Apostrophe));
  CHECK(require_action(window, "viewToggleGuidesAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
  CHECK(require_action(window, "viewToggleSnapAction")->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Semicolon));
  CHECK(require_action(window, "viewLockGuidesAction")->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Semicolon));
  CHECK(require_action(window, "viewSnapToGuidesAction")->isCheckable());
  CHECK(require_action(window, "viewSnapToGridAction")->isCheckable());
  CHECK(require_action(window, "viewSnapToDocumentAction")->isCheckable());
  CHECK(require_action(window, "viewSnapToLayersAction")->isCheckable());
  CHECK(require_action(window, "viewSnapToSelectionAction")->isCheckable());
  CHECK(require_action(window, "viewNewGuideAction") != nullptr);
  CHECK(require_action(window, "viewNewGuideLayoutAction") != nullptr);
  CHECK(require_action_by_text(window, QStringLiteral("Default Colors"))->shortcut() == QKeySequence(Qt::Key_D));
  CHECK(require_action_by_text(window, QStringLiteral("Swap Colors"))->shortcut() == QKeySequence(Qt::Key_X));
  CHECK(require_action_by_text(window, QStringLiteral("Move"))->shortcut() == QKeySequence(Qt::Key_V));
  CHECK(require_action_by_text(window, QStringLiteral("Marquee"))->shortcut() == QKeySequence(Qt::Key_M));
  CHECK(require_action_by_text(window, QStringLiteral("Elliptical Marquee"))->shortcut() ==
        QKeySequence(Qt::SHIFT | Qt::Key_M));
  CHECK(require_action_by_text(window, QStringLiteral("Lasso"))->shortcut() == QKeySequence(Qt::Key_L));
  CHECK(require_action_by_text(window, QStringLiteral("Magic Wand"))->shortcut() == QKeySequence(Qt::Key_W));
  CHECK(require_action_by_text(window, QStringLiteral("Brush"))->shortcut() == QKeySequence(Qt::Key_B));
  CHECK(require_action_by_text(window, QStringLiteral("Clone"))->shortcut() == QKeySequence(Qt::Key_S));
  CHECK(require_action_by_text(window, QStringLiteral("Smudge"))->shortcut() == QKeySequence(Qt::Key_R));
  CHECK(require_action_by_text(window, QStringLiteral("Eraser"))->shortcut() == QKeySequence(Qt::Key_E));
  CHECK(require_action_by_text(window, QStringLiteral("Gradient"))->shortcut() == QKeySequence(Qt::Key_G));
  CHECK(require_action_by_text(window, QStringLiteral("Fill"))->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_G));
  CHECK(require_action_by_text(window, QStringLiteral("Rect"))->shortcut() == QKeySequence(Qt::Key_U));
  CHECK(require_action_by_text(window, QStringLiteral("Line"))->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
  CHECK(require_action_by_text(window, QStringLiteral("Ellipse"))->shortcut() ==
        QKeySequence(Qt::SHIFT | Qt::Key_U));
  CHECK(require_action_by_text(window, QStringLiteral("Pick"))->shortcut() == QKeySequence(Qt::Key_I));
  CHECK(require_action_by_text(window, QStringLiteral("Type"))->shortcut() == QKeySequence(Qt::Key_T));
  CHECK(require_action_by_text(window, QStringLiteral("Hand"))->shortcut() == QKeySequence(Qt::Key_H));
  CHECK(require_action_by_text(window, QStringLiteral("Zoom"))->shortcut() == QKeySequence(Qt::Key_Z));
  CHECK(require_action(window, "brushSmallerAction")->shortcut() == QKeySequence(Qt::Key_BracketLeft));
  CHECK(require_action(window, "brushLargerAction")->shortcut() == QKeySequence(Qt::Key_BracketRight));
  CHECK(require_action(window, "brushMuchSmallerAction")->shortcut() ==
        QKeySequence(Qt::SHIFT | Qt::Key_BracketLeft));
  CHECK(require_action(window, "brushMuchLargerAction")->shortcut() ==
        QKeySequence(Qt::SHIFT | Qt::Key_BracketRight));
  const auto tooltip_matches_shortcut = [](QAction* action) {
    const auto shortcut = action->shortcut().toString(QKeySequence::NativeText);
    CHECK(!shortcut.isEmpty());
    CHECK(action->toolTip().contains(action->text().remove('&')));
    CHECK(action->toolTip().contains(shortcut));
  };
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Move")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Brush")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Smudge")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Clone")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Type")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Cut")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Default Colors")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Swap Colors")));
  tooltip_matches_shortcut(require_action(window, "brushLargerAction"));

  auto* canvas = require_canvas(window);
  auto* blend_combo = window.findChild<QComboBox*>(QStringLiteral("layerBlendModeCombo"));
  CHECK(blend_combo != nullptr);
  CHECK(blend_combo->findText(QStringLiteral("Difference")) >= 0);
  CHECK(blend_combo->findText(QStringLiteral("Color Dodge")) >= 0);
  CHECK(blend_combo->findText(QStringLiteral("Pin Light")) >= 0);
  CHECK(blend_combo->findText(QStringLiteral("Luminosity")) >= 0);
  CHECK(window.findChild<QCheckBox*>(QStringLiteral("layerLockTransparencyCheck")) != nullptr);
  CHECK(require_action(window, "selectionNewModeAction")->isCheckable());
  CHECK(require_action(window, "selectionAddModeAction")->isCheckable());
  CHECK(require_action(window, "selectionSubtractModeAction")->isCheckable());
  CHECK(require_action(window, "selectionIntersectModeAction")->isCheckable());
  auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* brush_size_slider = window.findChild<QSlider*>(QStringLiteral("brushSizeSlider"));
  auto* brush_opacity = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
  auto* brush_opacity_slider = window.findChild<QSlider*>(QStringLiteral("brushOpacitySlider"));
  auto* brush_softness = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
  auto* brush_softness_slider = window.findChild<QSlider*>(QStringLiteral("brushSoftnessSlider"));
  auto* brush_preset = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(brush_size != nullptr);
  CHECK(brush_size_slider != nullptr);
  CHECK(brush_opacity != nullptr);
  CHECK(brush_opacity_slider != nullptr);
  CHECK(brush_softness != nullptr);
  CHECK(brush_softness_slider != nullptr);
  CHECK(brush_preset != nullptr);
  CHECK(brush_size->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(brush_opacity->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(brush_softness->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(brush_preset->currentData().toString() == QStringLiteral("ink"));
  CHECK(brush_size->value() == 12);
  CHECK(brush_opacity->value() == 92);
  CHECK(brush_softness->value() == 20);
  CHECK(brush_softness_slider->value() == 20);
  CHECK(canvas->brush_size() == 12);
  CHECK(canvas->brush_opacity() == 92);
  CHECK(canvas->brush_softness() == 20);
  const auto airbrush_index = brush_preset->findData(QStringLiteral("airbrush"));
  CHECK(airbrush_index >= 0);
  brush_preset->setCurrentIndex(airbrush_index);
  CHECK(brush_size->value() == 56);
  CHECK(brush_opacity->value() == 12);
  CHECK(brush_softness->value() == 100);
  CHECK(!canvas->brush_build_up());
  const auto soft_round_index = brush_preset->findData(QStringLiteral("soft_round"));
  CHECK(soft_round_index >= 0);
  brush_preset->setCurrentIndex(soft_round_index);
  CHECK(!canvas->brush_build_up());
  brush_size->setValue(20);
  CHECK(brush_size_slider->value() == 20);
  require_action(window, "brushLargerAction")->trigger();
  CHECK(brush_size->value() == 21);
  CHECK(brush_size_slider->value() == 21);
  CHECK(canvas->brush_size() == 21);
  require_action(window, "brushSmallerAction")->trigger();
  CHECK(brush_size->value() == 20);
  require_action(window, "brushMuchLargerAction")->trigger();
  CHECK(brush_size->value() == 30);
  CHECK(canvas->brush_size() == 30);
  require_action(window, "brushMuchSmallerAction")->trigger();
  CHECK(brush_size->value() == 20);
  brush_opacity_slider->setValue(45);
  CHECK(brush_opacity->value() == 45);
  CHECK(canvas->brush_opacity() == 45);
  brush_softness_slider->setValue(65);
  CHECK(brush_softness->value() == 65);
  CHECK(canvas->brush_softness() == 65);
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("tools"));
  settings.sync();
}

void ui_startup_defaults_to_ink_brush() {
  SettingsValueRestorer saved_brush_preset(QStringLiteral("tools/brushPreset"));
  SettingsValueRestorer saved_brush_size(QStringLiteral("tools/brushSize"));
  SettingsValueRestorer saved_brush_opacity(QStringLiteral("tools/brushOpacity"));
  SettingsValueRestorer saved_brush_softness(QStringLiteral("tools/brushSoftness"));
  SettingsValueRestorer saved_brush_build_up(QStringLiteral("tools/brushBuildUp"));
  SettingsValueRestorer saved_gradient_method(QStringLiteral("tools/gradientMethod"));
  SettingsValueRestorer saved_gradient_reverse(QStringLiteral("tools/gradientReverse"));
  SettingsValueRestorer saved_gradient_opacity(QStringLiteral("tools/gradientOpacity"));
  SettingsValueRestorer saved_gradient_use_custom(QStringLiteral("tools/gradientUseCustomStops"));
  SettingsValueRestorer saved_gradient_stops(QStringLiteral("tools/gradientStops"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("tools/brushPreset"), QStringLiteral("airbrush"));
    settings.setValue(QStringLiteral("tools/brushSize"), 56);
    settings.setValue(QStringLiteral("tools/brushOpacity"), 12);
    settings.setValue(QStringLiteral("tools/brushSoftness"), 100);
    settings.setValue(QStringLiteral("tools/brushBuildUp"), true);
    settings.setValue(QStringLiteral("tools/gradientMethod"), static_cast<int>(patchy::GradientMethod::Radial));
    settings.setValue(QStringLiteral("tools/gradientReverse"), true);
    settings.setValue(QStringLiteral("tools/gradientOpacity"), 66);
    settings.setValue(QStringLiteral("tools/gradientUseCustomStops"), true);
    settings.setValue(QStringLiteral("tools/gradientStops"),
                      QStringLiteral("[{\"location\":0,\"r\":10,\"g\":20,\"b\":30,\"a\":255},"
                                     "{\"location\":1,\"r\":40,\"g\":50,\"b\":60,\"a\":128}]"));
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* brush_preset = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* brush_opacity = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
  auto* brush_softness = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
  auto* gradient_method = window.findChild<QComboBox*>(QStringLiteral("gradientMethodCombo"));
  auto* gradient_opacity = window.findChild<QSpinBox*>(QStringLiteral("gradientOpacitySpin"));
  auto* gradient_reverse = window.findChild<QCheckBox*>(QStringLiteral("gradientReverseCheck"));
  CHECK(brush_preset != nullptr);
  CHECK(brush_size != nullptr);
  CHECK(brush_opacity != nullptr);
  CHECK(brush_softness != nullptr);
  CHECK(gradient_method != nullptr);
  CHECK(gradient_opacity != nullptr);
  CHECK(gradient_reverse != nullptr);
  CHECK(brush_preset->currentData().toString() == QStringLiteral("ink"));
  CHECK(brush_size->value() == 12);
  CHECK(brush_opacity->value() == 92);
  CHECK(brush_softness->value() == 20);
  CHECK(canvas->brush_size() == 12);
  CHECK(canvas->brush_opacity() == 92);
  CHECK(canvas->brush_softness() == 20);
  CHECK(!canvas->brush_build_up());
  CHECK(canvas->gradient_method() == patchy::GradientMethod::Radial);
  CHECK(canvas->gradient_reverse());
  CHECK(canvas->gradient_opacity() == 66);
  CHECK(canvas->gradient_stops().has_value());
  CHECK(canvas->gradient_stops()->size() == 2);
  CHECK(gradient_method->currentData().toInt() == static_cast<int>(patchy::GradientMethod::Radial));
  CHECK(gradient_opacity->value() == 66);
  CHECK(gradient_reverse->isChecked());
}

void ui_canvas_wheel_matches_photoshop_navigation() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->setFocus();

  const auto initial_zoom = canvas->zoom();
  const auto initial_origin = canvas->widget_position_for_document_point(QPoint(0, 0));
  send_wheel(*canvas, QPoint(300, 240), 120);
  const auto horizontal_pan_origin = canvas->widget_position_for_document_point(QPoint(0, 0));
  CHECK(canvas->zoom() == initial_zoom);
  CHECK(horizontal_pan_origin.x() != initial_origin.x());
  CHECK(horizontal_pan_origin.y() == initial_origin.y());

  send_wheel(*canvas, QPoint(300, 240), 120, Qt::ControlModifier);
  const auto vertical_pan_origin = canvas->widget_position_for_document_point(QPoint(0, 0));
  CHECK(canvas->zoom() == initial_zoom);
  CHECK(vertical_pan_origin.y() != horizontal_pan_origin.y());

  send_wheel(*canvas, QPoint(300, 240), 120, Qt::AltModifier);
  CHECK(canvas->zoom() > initial_zoom);
  save_widget_artifact("ui_canvas_wheel_navigation", *canvas);
}

void ui_canvas_pan_keeps_document_partly_visible() {
  patchy::Document document(100, 80, patchy::PixelFormat::rgba8());
  patchy::ui::CanvasWidget canvas;
  canvas.resize(500, 400);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Pan);
  canvas.show();
  QApplication::processEvents();

  const auto minimum_visible = [](int viewport_span, int document_span) {
    return std::max(1, static_cast<int>(std::ceil(static_cast<double>(std::min(viewport_span, document_span)) * 0.10)));
  };
  const auto visible_document_rect = [&] {
    const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
    return QRect(top_left, QSize(document.width(), document.height())).intersected(canvas.rect());
  };
  const auto check_minimum_visible = [&] {
    const auto visible = visible_document_rect();
    CHECK(visible.width() >= minimum_visible(canvas.width(), document.width()));
    CHECK(visible.height() >= minimum_visible(canvas.height(), document.height()));
  };

  send_mouse(canvas, QEvent::MouseButtonPress, QPoint(250, 200), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, QPoint(5000, 4000), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, QPoint(5000, 4000), Qt::LeftButton, Qt::NoButton);
  check_minimum_visible();

  send_mouse(canvas, QEvent::MouseButtonPress, QPoint(250, 200), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, QPoint(-5000, -4000), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, QPoint(-5000, -4000), Qt::LeftButton, Qt::NoButton);
  check_minimum_visible();
}

void ui_canvas_fractional_zoom_paints_to_document_edge() {
  patchy::Document document(1024, 768, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(1024, 768, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 210;
      px[1] = 80;
      px[2] = 40;
      px[3] = 255;
    }
  }
  document.add_pixel_layer("Opaque", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(915, 706);
  canvas.set_document(&document);
  canvas.fit_to_view();
  canvas.show();
  QApplication::processEvents();

  const auto preview = canvas.grab().toImage();
  const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(document.width(), document.height()));
  const QPoint right_edge_sample(bottom_right.x() - 1, (top_left.y() + bottom_right.y()) / 2);
  CHECK(preview.rect().contains(right_edge_sample));
  CHECK(!color_close(preview.pixelColor(right_edge_sample), QColor(36, 38, 41), 4));
}

void ui_canvas_fractional_zoom_keeps_zoomed_in_pixels_sharp() {
  patchy::Document document(2, 6, patchy::PixelFormat::rgb8());
  auto pixels = solid_pixels(2, 6, patchy::PixelFormat::rgb8(), Qt::white);
  fill_pixel_rect(pixels, QRect(0, 0, 1, 6), Qt::black);
  document.add_pixel_layer("Split", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(160, 120);
  canvas.set_document(&document);
  canvas.set_zoom(5.5);
  canvas.show();
  QApplication::processEvents();

  const auto preview = canvas.grab().toImage();
  const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(document.width(), document.height()));
  const auto sample_y = (top_left.y() + bottom_right.y()) / 2;
  int black_columns = 0;
  int white_columns = 0;
  int interpolated_columns = 0;
  for (int x = top_left.x() + 1; x < bottom_right.x() - 1; ++x) {
    if (!preview.rect().contains(QPoint(x, sample_y))) {
      continue;
    }
    const auto color = preview.pixelColor(x, sample_y);
    if (color_close(color, QColor(Qt::black), 8)) {
      ++black_columns;
    } else if (color_close(color, QColor(Qt::white), 8)) {
      ++white_columns;
    } else {
      ++interpolated_columns;
    }
  }
  CHECK(black_columns > 0);
  CHECK(white_columns > 0);
  CHECK(interpolated_columns == 0);
}

void ui_canvas_deep_zoom_without_grid_keeps_pixels_sharp() {
  patchy::Document document(2, 6, patchy::PixelFormat::rgb8());
  auto pixels = solid_pixels(2, 6, patchy::PixelFormat::rgb8(), Qt::white);
  fill_pixel_rect(pixels, QRect(0, 0, 1, 6), Qt::black);
  document.add_pixel_layer("Split", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(180, 140);
  canvas.set_document(&document);
  canvas.set_zoom(12.25);
  canvas.show();
  QApplication::processEvents();

  const auto preview = canvas.grab().toImage();
  const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(document.width(), document.height()));
  const auto sample_y = (top_left.y() + bottom_right.y()) / 2;
  int black_columns = 0;
  int white_columns = 0;
  int interpolated_columns = 0;
  for (int x = top_left.x() + 1; x < bottom_right.x() - 1; ++x) {
    if (!preview.rect().contains(QPoint(x, sample_y))) {
      continue;
    }
    const auto color = preview.pixelColor(x, sample_y);
    if (color_close(color, QColor(Qt::black), 8)) {
      ++black_columns;
    } else if (color_close(color, QColor(Qt::white), 8)) {
      ++white_columns;
    } else {
      ++interpolated_columns;
    }
  }
  CHECK(black_columns > 0);
  CHECK(white_columns > 0);
  CHECK(interpolated_columns == 0);
}

void ui_zoomed_out_canvas_uses_downsampled_display_mip() {
  patchy::Document document(256, 256, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(256, 256, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto value = ((x + y) % 2 == 0) ? 0 : 255;
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(value);
      px[1] = static_cast<std::uint8_t>(value);
      px[2] = static_cast<std::uint8_t>(value);
    }
  }
  document.add_pixel_layer("Checker", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(180, 180);
  canvas.set_document(&document);
  canvas.set_zoom(0.25);
  canvas.show();
  QApplication::processEvents();

  const auto preview = canvas.grab().toImage();
  const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(document.width(), document.height()));
  const QRect target_rect(top_left, QSize(bottom_right.x() - top_left.x(), bottom_right.y() - top_left.y()));
  const auto sample_rect = target_rect.adjusted(4, 4, -4, -4).intersected(preview.rect());
  CHECK(!sample_rect.isEmpty());

  int midtone_samples = 0;
  int source_tone_samples = 0;
  for (int y = sample_rect.top(); y <= sample_rect.bottom(); y += 3) {
    for (int x = sample_rect.left(); x <= sample_rect.right(); x += 3) {
      const auto color = preview.pixelColor(x, y);
      const auto value = (color.red() + color.green() + color.blue()) / 3;
      if (value >= 96 && value <= 160) {
        ++midtone_samples;
      }
      if (value <= 24 || value >= 231) {
        ++source_tone_samples;
      }
    }
  }

  CHECK(midtone_samples > 0);
  CHECK(midtone_samples > source_tone_samples * 4);
}

void ui_shape_flyout_and_zoom_tool_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* marquee_button = window.findChild<QToolButton*>(QStringLiteral("marqueeToolButton"));
  auto* shape_button = window.findChild<QToolButton*>(QStringLiteral("shapeToolButton"));
  auto* zoom_button = window.findChild<QToolButton*>(QStringLiteral("zoomToolButton"));
  CHECK(marquee_button != nullptr);
  CHECK(marquee_button->menu() != nullptr);
  CHECK(shape_button != nullptr);
  CHECK(shape_button->menu() != nullptr);
  CHECK(zoom_button != nullptr);

  require_action_by_text(window, QStringLiteral("Elliptical Marquee"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::EllipticalMarquee);
  CHECK(marquee_button->defaultAction() == require_action_by_text(window, QStringLiteral("Elliptical Marquee")));

  require_action_by_text(window, QStringLiteral("Ellipse"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Ellipse);
  CHECK(shape_button->defaultAction() == require_action_by_text(window, QStringLiteral("Ellipse")));

  require_action_by_text(window, QStringLiteral("Zoom"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Zoom);
  canvas->set_zoom(0.25);
  const auto before_zoom = canvas->zoom();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(420, 320)));
  CHECK(canvas->zoom() > before_zoom);

  canvas->set_zoom(2.0);
  send_double_click(*zoom_button, zoom_button->rect().center());
  CHECK(std::abs(canvas->zoom() - 1.0) < 0.001);
  save_widget_artifact("ui_shape_flyout_zoom_tool", window);
}

void ui_filled_shape_preview_clears_after_commit() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(96);
  canvas->set_fill_shapes(true);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(170, 170)),
       canvas->widget_position_for_document_point(QPoint(370, 310)));
  QApplication::processEvents();

  const auto outside_final_shape = canvas_pixel(*canvas, QPoint(170, 125));
  CHECK(color_close(outside_final_shape, Qt::white, 10));
  save_widget_artifact("ui_filled_shape_preview_cleanup", *canvas);

  const auto immediate = canvas->grab().toImage();
  canvas->document_changed();
  QApplication::processEvents();
  const auto repainted = canvas->grab().toImage();
  CHECK(immediate.size() == repainted.size());
  CHECK(immediate.pixelColor(canvas->widget_position_for_document_point(QPoint(170, 125))) ==
        repainted.pixelColor(canvas->widget_position_for_document_point(QPoint(170, 125))));
}

void ui_options_bar_tracks_active_tool() {
  SettingsValueRestorer saved_gradient_method(QStringLiteral("tools/gradientMethod"));
  SettingsValueRestorer saved_gradient_reverse(QStringLiteral("tools/gradientReverse"));
  SettingsValueRestorer saved_gradient_opacity(QStringLiteral("tools/gradientOpacity"));
  SettingsValueRestorer saved_gradient_use_custom(QStringLiteral("tools/gradientUseCustomStops"));
  SettingsValueRestorer saved_gradient_stops(QStringLiteral("tools/gradientStops"));
  SettingsValueRestorer saved_text_smoothing(QStringLiteral("tools/textSmoothing"));
  SettingsValueRestorer saved_show_transform_controls(QStringLiteral("tools/showTransformControls"));
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("tools/showTransformControls"));
  settings.sync();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* move_auto_select = window.findChild<QCheckBox*>(QStringLiteral("moveAutoSelectCheck"));
  auto* move_show_transform_controls = window.findChild<QCheckBox*>(QStringLiteral("moveShowTransformControlsCheck"));
  auto* text_font = window.findChild<QFontComboBox*>(QStringLiteral("textFontCombo"));
  auto* text_size = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  auto* text_bold = window.findChild<QPushButton*>(QStringLiteral("textBoldButton"));
  auto* text_italic = window.findChild<QPushButton*>(QStringLiteral("textItalicButton"));
  auto* text_smoothing = window.findChild<QComboBox*>(QStringLiteral("textSmoothingCombo"));
  auto* text_color = window.findChild<QPushButton*>(QStringLiteral("textColorButton"));
  auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* brush_size_slider = window.findChild<QSlider*>(QStringLiteral("brushSizeSlider"));
  auto* brush_opacity = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
  auto* brush_opacity_slider = window.findChild<QSlider*>(QStringLiteral("brushOpacitySlider"));
  auto* brush_softness = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
  auto* brush_softness_slider = window.findChild<QSlider*>(QStringLiteral("brushSoftnessSlider"));
  auto* gradient_method = window.findChild<QComboBox*>(QStringLiteral("gradientMethodCombo"));
  auto* gradient_opacity = window.findChild<QSpinBox*>(QStringLiteral("gradientOpacitySpin"));
  auto* gradient_opacity_slider = window.findChild<QSlider*>(QStringLiteral("gradientOpacitySlider"));
  auto* gradient_reverse = window.findChild<QCheckBox*>(QStringLiteral("gradientReverseCheck"));
  auto* gradient_preview = window.findChild<QPushButton*>(QStringLiteral("gradientPreviewButton"));
  auto* gradient_edit_stops = window.findChild<QPushButton*>(QStringLiteral("gradientEditStopsButton"));
  auto* clone_aligned = window.findChild<QCheckBox*>(QStringLiteral("cloneAlignedCheck"));
  auto* wand_tolerance = window.findChild<QSpinBox*>(QStringLiteral("wandToleranceSpin"));
  auto* wand_contiguous = window.findChild<QCheckBox*>(QStringLiteral("wandContiguousCheck"));
  auto* wand_sample_all_layers = window.findChild<QCheckBox*>(QStringLiteral("wandSampleAllLayersCheck"));
  auto* feather_group = window.findChild<QWidget*>(QStringLiteral("selectionFeatherGroup"));
  auto* anti_alias = window.findChild<QCheckBox*>(QStringLiteral("selectionAntiAliasCheck"));
  CHECK(move_auto_select != nullptr);
  CHECK(move_show_transform_controls != nullptr);
  CHECK(text_font != nullptr);
  CHECK(text_size != nullptr);
  CHECK(text_size->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(text_size->minimum() <= 0.01);
  CHECK(text_bold != nullptr);
  CHECK(text_italic != nullptr);
  CHECK(text_smoothing != nullptr);
  CHECK(text_color != nullptr);
  CHECK(brush_size != nullptr);
  CHECK(brush_size_slider != nullptr);
  CHECK(brush_opacity != nullptr);
  CHECK(brush_opacity_slider != nullptr);
  CHECK(brush_softness != nullptr);
  CHECK(brush_softness_slider != nullptr);
  CHECK(gradient_method != nullptr);
  CHECK(gradient_opacity != nullptr);
  CHECK(gradient_opacity_slider != nullptr);
  CHECK(gradient_reverse != nullptr);
  CHECK(gradient_preview != nullptr);
  CHECK(gradient_edit_stops != nullptr);
  CHECK(clone_aligned != nullptr);
  CHECK(wand_tolerance != nullptr);
  CHECK(wand_contiguous != nullptr);
  CHECK(wand_sample_all_layers != nullptr);
  CHECK(feather_group != nullptr);
  CHECK(anti_alias != nullptr);
  CHECK(anti_alias->isChecked());
  CHECK(wand_contiguous->isChecked());
  CHECK(!wand_sample_all_layers->isChecked());

  CHECK(brush_size->isVisible());
  CHECK(brush_size_slider->isVisible());
  CHECK(brush_opacity->isVisible());
  CHECK(brush_opacity_slider->isVisible());
  CHECK(brush_softness->isVisible());
  CHECK(brush_softness_slider->isVisible());
  CHECK(!clone_aligned->isVisible());
  CHECK(!gradient_method->isVisible());
  CHECK(!gradient_opacity->isVisible());
  CHECK(!gradient_reverse->isVisible());
  CHECK(!gradient_edit_stops->isVisible());
  CHECK(!move_auto_select->isVisible());
  CHECK(!move_show_transform_controls->isVisible());
  CHECK(!wand_contiguous->isVisible());
  CHECK(!wand_sample_all_layers->isVisible());
  CHECK(!text_font->isVisible());
  CHECK(!text_color->isVisible());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(move_auto_select->isVisible());
  CHECK(move_show_transform_controls->isVisible());
  CHECK(move_show_transform_controls->isChecked());
  CHECK(!brush_size->isVisible());
  CHECK(!brush_size_slider->isVisible());
  CHECK(!brush_opacity->isVisible());
  CHECK(!brush_opacity_slider->isVisible());
  CHECK(!brush_softness->isVisible());
  CHECK(!brush_softness_slider->isVisible());
  CHECK(!clone_aligned->isVisible());
  CHECK(!text_font->isVisible());
  move_auto_select->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->auto_select_layer());
  move_auto_select->setChecked(true);
  QApplication::processEvents();
  CHECK(move_auto_select->isChecked());
  CHECK(canvas->auto_select_layer());
  move_show_transform_controls->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->show_transform_controls());
  move_show_transform_controls->setChecked(true);
  QApplication::processEvents();
  CHECK(move_show_transform_controls->isChecked());
  CHECK(canvas->show_transform_controls());
  save_widget_artifact("ui_tool_options_move", window);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  CHECK(text_font->isVisible());
  CHECK(text_size->isVisible());
  CHECK(text_bold->isVisible());
  CHECK(text_italic->isVisible());
  CHECK(text_color->isVisible());
  CHECK(!move_auto_select->isVisible());
  CHECK(!brush_size->isVisible());
  CHECK(!brush_opacity->isVisible());
  CHECK(!brush_softness->isVisible());
  CHECK(!clone_aligned->isVisible());
  text_size->setValue(0.01);
  CHECK(std::abs(text_size->value() - 0.01) < 0.001);
  text_size->setValue(text_points_for_pixels(36));
  text_bold->setChecked(true);
  save_widget_artifact("ui_tool_options_text", window);

  require_action_by_text(window, QStringLiteral("Magic Wand"))->trigger();
  QApplication::processEvents();
  CHECK(wand_tolerance->isVisible());
  CHECK(wand_contiguous->isVisible());
  CHECK(wand_sample_all_layers->isVisible());
  CHECK(feather_group->isVisible());
  CHECK(anti_alias->isVisible());
  CHECK(!text_font->isVisible());
  CHECK(!text_color->isVisible());
  wand_contiguous->setChecked(false);
  wand_sample_all_layers->setChecked(true);
  QApplication::processEvents();
  CHECK(!canvas->wand_contiguous());
  CHECK(canvas->wand_sample_all_layers());
  wand_contiguous->setChecked(true);
  wand_sample_all_layers->setChecked(false);
  QApplication::processEvents();
  CHECK(canvas->wand_contiguous());
  CHECK(!canvas->wand_sample_all_layers());

  require_action_by_text(window, QStringLiteral("Clone"))->trigger();
  QApplication::processEvents();
  CHECK(brush_size->isVisible());
  CHECK(brush_size_slider->isVisible());
  CHECK(brush_opacity->isVisible());
  CHECK(brush_opacity_slider->isVisible());
  CHECK(brush_softness->isVisible());
  CHECK(brush_softness_slider->isVisible());
  CHECK(clone_aligned->isVisible());
  CHECK(clone_aligned->isChecked());
  clone_aligned->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->clone_aligned());
  clone_aligned->setChecked(true);
  QApplication::processEvents();
  CHECK(canvas->clone_aligned());
  CHECK(!wand_tolerance->isVisible());
  CHECK(!wand_contiguous->isVisible());
  CHECK(!wand_sample_all_layers->isVisible());

  require_action_by_text(window, QStringLiteral("Smudge"))->trigger();
  QApplication::processEvents();
  CHECK(brush_size->isVisible());
  CHECK(brush_opacity->isVisible());
  CHECK(brush_softness->isVisible());
  CHECK(!clone_aligned->isVisible());

  require_action_by_text(window, QStringLiteral("Gradient"))->trigger();
  QApplication::processEvents();
  CHECK(gradient_method->isVisible());
  CHECK(gradient_opacity->isVisible());
  CHECK(gradient_opacity_slider->isVisible());
  CHECK(gradient_reverse->isVisible());
  CHECK(gradient_preview->isVisible());
  CHECK(gradient_edit_stops->isVisible());
  CHECK(!brush_size->isVisible());
  CHECK(!brush_opacity->isVisible());
  CHECK(!brush_softness->isVisible());
  const auto radial_index = gradient_method->findText(QStringLiteral("Radial"));
  CHECK(radial_index >= 0);
  gradient_method->setCurrentIndex(radial_index);
  gradient_opacity_slider->setValue(55);
  gradient_reverse->setChecked(true);
  QApplication::processEvents();
  CHECK(canvas->gradient_method() == patchy::GradientMethod::Radial);
  CHECK(canvas->gradient_opacity() == 55);
  CHECK(canvas->gradient_reverse());

  QTimer::singleShot(0, [] {
    auto* dialog = find_top_level_dialog(QStringLiteral("gradientStopsDialog"));
    CHECK(dialog != nullptr);
    auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("gradientStopsTable"));
    auto* add_stop = dialog->findChild<QPushButton*>(QStringLiteral("gradientAddStopButton"));
    CHECK(table != nullptr);
    CHECK(add_stop != nullptr);
    CHECK(table->rowCount() == 2);
    add_stop->click();
    CHECK(table->rowCount() == 3);
    table->item(2, 0)->setText(QStringLiteral("50"));
    table->item(2, 1)->setText(QStringLiteral("#00FF00"));
    table->item(2, 2)->setText(QStringLiteral("25"));
    dialog->accept();
  });
  gradient_edit_stops->click();
  QApplication::processEvents();
  CHECK(canvas->gradient_stops().has_value());
  CHECK(canvas->gradient_stops()->size() == 3);
  CHECK(canvas->gradient_stops()->at(1).color.g == 255);
  CHECK(canvas->gradient_stops()->at(1).color.a >= 63);
  CHECK(canvas->gradient_stops()->at(1).color.a <= 64);
}

void ui_right_docks_collapse_layers_show_metadata_and_info_updates() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* info = window.findChild<QLabel*>(QStringLiteral("canvasInfoLabel"));
  auto* document_info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  auto* active_layer_info = window.findChild<QLabel*>(QStringLiteral("activeLayerInfoLabel"));
  auto* active_layer_geometry = window.findChild<QLabel*>(QStringLiteral("activeLayerGeometryLabel"));
  auto* active_layer_mask = window.findChild<QLabel*>(QStringLiteral("activeLayerMaskLabel"));
  auto* active_layer_adjustment = window.findChild<QLabel*>(QStringLiteral("activeLayerAdjustmentLabel"));
  auto* active_layer_text = window.findChild<QLabel*>(QStringLiteral("activeLayerTextLabel"));
  auto* active_tool_info = window.findChild<QLabel*>(QStringLiteral("activeToolInfoLabel"));
  auto* opacity_spin = window.findChild<QSpinBox*>(QStringLiteral("layerOpacitySpin"));
  CHECK(layer_list != nullptr);
  CHECK(info != nullptr);
  CHECK(document_info != nullptr);
  CHECK(active_layer_info != nullptr);
  CHECK(active_layer_geometry != nullptr);
  CHECK(active_layer_mask != nullptr);
  CHECK(active_layer_adjustment != nullptr);
  CHECK(active_layer_text != nullptr);
  CHECK(active_tool_info != nullptr);
  CHECK(opacity_spin != nullptr);
  CHECK(opacity_spin->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(document_info->text().contains(QStringLiteral("Document")));
  CHECK(document_info->text().contains(QStringLiteral("1024 x 768 px")));
  CHECK(active_layer_info->text().contains(QStringLiteral("Paint Layer")));
  CHECK(active_layer_info->text().contains(QStringLiteral("Pixel Layer")));
  CHECK(active_layer_geometry->text().contains(QStringLiteral("Bounds:")));
  CHECK(!active_layer_mask->isVisible());
  CHECK(!active_layer_adjustment->isVisible());
  CHECK(!active_layer_text->isVisible());
  CHECK(active_tool_info->text().contains(QStringLiteral("Brush")));
  auto* layers_dock = window.findChild<QDockWidget*>(QStringLiteral("layersDock"));
  auto* properties_dock = window.findChild<QDockWidget*>(QStringLiteral("propertiesDock"));
  auto* history_toggle = window.findChild<QToolButton*>(QStringLiteral("historyDockCollapseButton"));
  auto* properties_toggle = window.findChild<QToolButton*>(QStringLiteral("propertiesDockCollapseButton"));
  auto* swatches_toggle = window.findChild<QToolButton*>(QStringLiteral("swatchesDockCollapseButton"));
  auto* info_toggle = window.findChild<QToolButton*>(QStringLiteral("infoDockCollapseButton"));
  CHECK(layers_dock != nullptr);
  CHECK(properties_dock != nullptr);
  CHECK(layers_dock->minimumWidth() >= 280);
  CHECK(layers_dock->minimumHeight() >= 300);
  CHECK(layer_list->minimumHeight() >= 120);
  CHECK(properties_dock->maximumHeight() <= 240);
  CHECK(properties_dock->height() <= 240);
  CHECK(window.minimumSizeHint().height() <= 780);
  CHECK(layer_list->contextMenuPolicy() == Qt::CustomContextMenu);
  const auto layer_action_buttons = window.findChildren<QPushButton*>();
  int visible_layer_action_buttons = 0;
  for (const auto* button : layer_action_buttons) {
    if (button->property("layerActionButton").toBool()) {
      ++visible_layer_action_buttons;
      CHECK(button->minimumWidth() >= 40);
      CHECK(button->minimumHeight() >= 34);
      CHECK(button->iconSize().width() >= 24);
      CHECK(button->iconSize().height() >= 24);
    }
  }
  CHECK(visible_layer_action_buttons == 5);
  CHECK(history_toggle != nullptr);
  CHECK(properties_toggle != nullptr);
  CHECK(swatches_toggle != nullptr);
  CHECK(info_toggle != nullptr);
  CHECK(history_toggle->text() == QStringLiteral(">"));
  CHECK(properties_toggle->text() == QStringLiteral(">"));
  CHECK(swatches_toggle->text() == QStringLiteral(">"));
  CHECK(info_toggle->text() == QStringLiteral(">"));
  CHECK(history_toggle->icon().isNull());
  history_toggle->setChecked(true);
  QApplication::processEvents();
  CHECK(history_toggle->text() == QStringLiteral("v"));
  history_toggle->setChecked(false);
  QApplication::processEvents();
  CHECK(layers_dock->width() >= 260);
  const auto dock_width_before_resize = layers_dock->width();
  auto* dock_resize_handle = window.findChild<QWidget*>(QStringLiteral("rightDockResizeHandle"));
  CHECK(dock_resize_handle != nullptr);
  const auto dock_resize_point = dock_resize_handle->rect().center();
  send_mouse(*dock_resize_handle, QEvent::MouseButtonPress, dock_resize_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*dock_resize_handle, QEvent::MouseMove, dock_resize_point + QPoint(-90, 0), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(*dock_resize_handle, QEvent::MouseButtonRelease, dock_resize_point + QPoint(-90, 0), Qt::LeftButton,
             Qt::NoButton);
  CHECK(layers_dock->width() > dock_width_before_resize + 40);

  auto* row_widget = layer_list->itemWidget(layer_list->item(0));
  CHECK(row_widget != nullptr);
  CHECK(row_widget->findChild<QLabel*>(QStringLiteral("layerRowDetails")) != nullptr);

  const auto point = canvas->widget_position_for_document_point(QPoint(64, 48));
  send_mouse(*canvas, QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton);
  CHECK(info->text().contains(QStringLiteral("X: 64")));
  CHECK(info->text().contains(QStringLiteral("Y: 48")));
  CHECK(info->text().contains(QStringLiteral("RGB:")));

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  const auto marquee_start = canvas->widget_position_for_document_point(QPoint(40, 40));
  const auto marquee_end = canvas->widget_position_for_document_point(QPoint(140, 90));
  send_mouse(*canvas, QEvent::MouseButtonPress, marquee_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, marquee_end, Qt::NoButton, Qt::LeftButton);
  CHECK(info->text().contains(QStringLiteral("Selection:")));
  CHECK(info->text().contains(QStringLiteral(" at 40, 40")));
  send_mouse(*canvas, QEvent::MouseButtonRelease, marquee_end, Qt::LeftButton, Qt::NoButton);
  save_widget_artifact("ui_info_panel_layers_docks", window);
}

void ui_collapsed_right_docks_keep_deep_layer_rows_readable() {
  patchy::Document document(128, 128, patchy::PixelFormat::rgba8());
  patchy::Layer root(document.allocate_layer_id(), "Root Folder", patchy::LayerKind::Group);
  auto* current = &root;
  for (int depth = 1; depth <= 8; ++depth) {
    current->add_child(
        patchy::Layer(document.allocate_layer_id(), "Nested Folder " + std::to_string(depth), patchy::LayerKind::Group));
    current = &current->children().back();
  }
  auto deep_pixels = solid_pixels(128, 128, patchy::PixelFormat::rgba8(), QColor(20, 120, 220, 255));
  patchy::Layer deep_layer(document.allocate_layer_id(), "Deep Paint Layer With Long Name", std::move(deep_pixels));
  const auto deep_layer_id = deep_layer.id();
  current->add_child(std::move(deep_layer));
  for (int index = 1; index <= 24; ++index) {
    current->add_child(patchy::Layer(document.allocate_layer_id(), "Deep Scroll Filler " + std::to_string(index),
                                     solid_pixels(128, 128, patchy::PixelFormat::rgba8(),
                                                  QColor(35, 70 + (index * 7) % 120, 160, 255))));
  }
  document.add_layer(std::move(root));
  document.set_active_layer(deep_layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Deep Layers"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* history_toggle = window.findChild<QToolButton*>(QStringLiteral("historyDockCollapseButton"));
  auto* properties_toggle = window.findChild<QToolButton*>(QStringLiteral("propertiesDockCollapseButton"));
  auto* info_toggle = window.findChild<QToolButton*>(QStringLiteral("infoDockCollapseButton"));
  auto* swatches_toggle = window.findChild<QToolButton*>(QStringLiteral("swatchesDockCollapseButton"));
  CHECK(layer_list != nullptr);
  CHECK(history_toggle != nullptr);
  CHECK(properties_toggle != nullptr);
  CHECK(info_toggle != nullptr);
  CHECK(swatches_toggle != nullptr);
  CHECK(history_toggle->text() == QStringLiteral(">"));
  CHECK(properties_toggle->text() == QStringLiteral(">"));
  CHECK(info_toggle->text() == QStringLiteral(">"));
  CHECK(swatches_toggle->text() == QStringLiteral(">"));

  auto* deep_item = require_layer_item(*layer_list, QStringLiteral("Deep Paint Layer With Long Name"));
  layer_list->scrollToItem(deep_item, QAbstractItemView::PositionAtCenter);
  QApplication::processEvents();

  auto* row_widget = layer_list->itemWidget(deep_item);
  CHECK(row_widget != nullptr);
  auto* visibility = row_widget->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  auto* thumbnail = row_widget->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  auto* horizontal_scroll = layer_list->horizontalScrollBar();
  CHECK(visibility != nullptr);
  CHECK(thumbnail != nullptr);
  CHECK(horizontal_scroll != nullptr);
  CHECK(horizontal_scroll->maximum() > horizontal_scroll->minimum());
  horizontal_scroll->setValue(horizontal_scroll->minimum());
  QApplication::processEvents();
  const auto initial_visibility_left = visibility->mapTo(layer_list->viewport(), QPoint()).x();
  const auto initial_thumbnail_right = thumbnail->mapTo(layer_list->viewport(), QPoint(thumbnail->width(), 0)).x();
  CHECK(initial_visibility_left >= 0);
  CHECK(initial_visibility_left < layer_list->viewport()->width() / 2);
  horizontal_scroll->setValue(std::clamp(initial_thumbnail_right - (layer_list->viewport()->width() - 16),
                                        horizontal_scroll->minimum(), horizontal_scroll->maximum()));
  QApplication::processEvents();
  const auto scrolled_thumbnail_right = thumbnail->mapTo(layer_list->viewport(), QPoint(thumbnail->width(), 0)).x();
  CHECK(scrolled_thumbnail_right <= layer_list->viewport()->width() - 16);

  auto scrollbar_ancestor = [](QWidget* widget) -> QScrollBar* {
    for (auto* current = widget; current != nullptr; current = current->parentWidget()) {
      if (auto* scroll = qobject_cast<QScrollBar*>(current); scroll != nullptr) {
        return scroll;
      }
    }
    return nullptr;
  };
  auto scrollbar_slider_rect = [](QScrollBar* scroll) {
    QStyleOptionSlider option;
    option.initFrom(scroll);
    option.orientation = scroll->orientation();
    option.minimum = scroll->minimum();
    option.maximum = scroll->maximum();
    option.singleStep = scroll->singleStep();
    option.pageStep = scroll->pageStep();
    option.sliderPosition = scroll->sliderPosition();
    option.sliderValue = scroll->value();
    option.upsideDown = scroll->invertedAppearance();
    return scroll->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, scroll);
  };
  auto check_scrollbar_hit_target = [&](QScrollBar* scroll) {
    CHECK(scroll != nullptr);
    CHECK(scroll->maximum() > scroll->minimum());
    scroll->setValue((scroll->minimum() + scroll->maximum()) / 2);
    QApplication::processEvents();
    const auto slider = scrollbar_slider_rect(scroll);
    CHECK(slider.isValid());
    const auto start = scroll->orientation() == Qt::Vertical
                           ? QPoint(std::clamp(scroll->width() - 2, slider.left(), slider.right()),
                                    slider.center().y())
                           : QPoint(slider.center().x(),
                                    std::clamp(scroll->height() - 2, slider.top(), slider.bottom()));
    auto* hit = layer_list->childAt(scroll->mapTo(layer_list, start));
    CHECK(scrollbar_ancestor(hit) == scroll);
  };
  check_scrollbar_hit_target(layer_list->verticalScrollBar());
  check_scrollbar_hit_target(layer_list->horizontalScrollBar());

  auto clear_layer_row_masks = [&] {
    for (int row_index = 0; row_index < layer_list->count(); ++row_index) {
      if (auto* row = layer_list->itemWidget(layer_list->item(row_index)); row != nullptr) {
        row->clearMask();
      }
    }
  };
  auto send_mouse_at_global = [](QWidget& widget, QEvent::Type type, QPoint global_position,
                                 Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, widget.mapFromGlobal(global_position), global_position, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
    QApplication::processEvents();
  };
  auto drag_scrollbar_through_current_hit = [&](QScrollBar* scroll, int pixels) {
    CHECK(scroll != nullptr);
    CHECK(scroll->maximum() > scroll->minimum());
    scroll->setValue((scroll->minimum() + scroll->maximum()) / 2);
    QApplication::processEvents();
    const auto slider = scrollbar_slider_rect(scroll);
    CHECK(slider.isValid());
    const auto start = slider.center();
    const auto start_global = scroll->mapToGlobal(start);
    auto* hit = layer_list->childAt(layer_list->mapFromGlobal(start_global));
    CHECK(hit != nullptr);
    const auto before = scroll->value();
    const auto end_global =
        start_global + (scroll->orientation() == Qt::Vertical ? QPoint(0, pixels) : QPoint(pixels, 0));
    if (hit == scroll) {
      send_mouse(*scroll, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
      send_mouse(*scroll, QEvent::MouseMove, start + (scroll->orientation() == Qt::Vertical ? QPoint(0, pixels)
                                                                                             : QPoint(pixels, 0)),
                 Qt::NoButton, Qt::LeftButton);
      send_mouse(*scroll, QEvent::MouseButtonRelease,
                 start + (scroll->orientation() == Qt::Vertical ? QPoint(0, pixels) : QPoint(pixels, 0)),
                 Qt::LeftButton, Qt::NoButton);
    } else {
      send_mouse_at_global(*hit, QEvent::MouseButtonPress, start_global, Qt::LeftButton, Qt::LeftButton);
      send_mouse_at_global(*hit, QEvent::MouseMove, end_global, Qt::NoButton, Qt::LeftButton);
      send_mouse_at_global(*hit, QEvent::MouseButtonRelease, end_global, Qt::LeftButton, Qt::NoButton);
    }
    return scroll->value() > before;
  };
  QMessageBox warning(QMessageBox::Warning, QStringLiteral("Warning"), QStringLiteral("Warning"), QMessageBox::Ok,
                      &window);
  QTimer::singleShot(0, &warning, [&] { warning.accept(); });
  warning.exec();
  clear_layer_row_masks();
  CHECK(drag_scrollbar_through_current_hit(layer_list->verticalScrollBar(), 48));
  CHECK(drag_scrollbar_through_current_hit(layer_list->horizontalScrollBar(), 48));
  QEvent activate_event(QEvent::WindowActivate);
  QApplication::sendEvent(layer_list, &activate_event);
  QApplication::processEvents();
  check_scrollbar_hit_target(layer_list->verticalScrollBar());
  check_scrollbar_hit_target(layer_list->horizontalScrollBar());
  save_widget_artifact("ui_collapsed_right_docks_deep_layer_rows", window);
}

void ui_layer_context_menu_exposes_blending_options_dialog() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* blending_options = require_action(window, "layerBlendingOptionsAction");
  CHECK(layer_list != nullptr);
  CHECK(blending_options->text().remove('&') == QStringLiteral("Blending Options..."));

  bool saw_context_action = false;
  bool saw_rasterize_action = false;
  bool saw_rasterize_style_action = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("layerContextMenu")) {
        continue;
      }
      for (auto* action : menu->actions()) {
        auto text = action->text();
        text.remove('&');
        if (text == QStringLiteral("Blending Options...")) {
          saw_context_action = true;
        } else if (text == QStringLiteral("Rasterize")) {
          saw_rasterize_action = true;
        } else if (text == QStringLiteral("Rasterize (including layer style)")) {
          saw_rasterize_style_action = true;
        }
      }
      menu->close();
      return;
    }
  });
  const auto context_point = layer_list->visualItemRect(layer_list->item(0)).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  layer_list->viewport()->mapToGlobal(context_point));
  QApplication::sendEvent(layer_list->viewport(), &context_event);
  QApplication::processEvents();
  CHECK(saw_context_action);
  CHECK(saw_rasterize_action);
  CHECK(saw_rasterize_style_action);

  canvas->set_primary_color(QColor(230, 40, 40));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  const auto before = canvas_pixel(*canvas, QPoint(80, 80));
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(24);
  canvas->set_primary_color(QColor(20, 220, 60));

  bool saw_live_style_preview = false;
  bool saw_non_modal_dialog = false;
  bool saw_layer_style_edit_lock = false;
  bool saw_shared_color_picker = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyLayerStyleDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      auto* gradient_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleGradientOverlayCategoryCheck"));
      auto* gradient_angle_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleGradientAngleSlider"));
      auto* gradient_stops = dialog->findChild<QTableWidget*>(QStringLiteral("layerStyleGradientStopsTable"));
      auto* selected_gradient_color =
          dialog->findChild<QPushButton*>(QStringLiteral("layerStyleGradientSelectedColorPreview"));
      auto* pick_gradient_color = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleGradientPickColorButton"));
      auto* stroke_color = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleStrokeColorPreview"));
      auto* stroke_red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeRedSpin"));
      auto* stroke_green = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeGreenSpin"));
      auto* stroke_blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeBlueSpin"));
      auto* outer_glow_color = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleOuterGlowColorPreview"));
      auto* outer_glow_red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowRedSpin"));
      auto* outer_glow_green = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowGreenSpin"));
      auto* outer_glow_blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowBlueSpin"));
      auto* shadow_blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleDropShadowBlendModeCombo"));
      auto* shadow_color = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleDropShadowColorPreview"));
      auto* shadow_red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowRedSpin"));
      auto* shadow_green = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowGreenSpin"));
      auto* shadow_blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowBlueSpin"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePreviewCheck"));
      CHECK(gradient_check != nullptr);
      CHECK(gradient_angle_slider != nullptr);
      CHECK(gradient_stops != nullptr);
      CHECK(selected_gradient_color != nullptr);
      CHECK(pick_gradient_color != nullptr);
      CHECK(stroke_color != nullptr);
      CHECK(stroke_red != nullptr);
      CHECK(stroke_green != nullptr);
      CHECK(stroke_blue != nullptr);
      CHECK(outer_glow_color != nullptr);
      CHECK(outer_glow_red != nullptr);
      CHECK(outer_glow_green != nullptr);
      CHECK(outer_glow_blue != nullptr);
      CHECK(shadow_blend != nullptr);
      CHECK(shadow_color != nullptr);
      CHECK(shadow_red != nullptr);
      CHECK(shadow_green != nullptr);
      CHECK(shadow_blue != nullptr);
      CHECK(preview != nullptr);
      CHECK(preview->isChecked());
      CHECK(shadow_blend->findText(QStringLiteral("Normal")) >= 0);
      CHECK(stroke_red->value() == 255);
      CHECK(stroke_green->value() == 0);
      CHECK(stroke_blue->value() == 0);
      saw_non_modal_dialog = !dialog->isModal() && dialog->windowModality() == Qt::NonModal &&
                             dialog->windowFlags().testFlag(Qt::FramelessWindowHint) &&
                             dialog->findChild<QWidget*>(QStringLiteral("dialogChromeTitleBar")) != nullptr &&
                             dialog->findChild<QToolButton*>(QStringLiteral("dialogChromeCloseButton")) != nullptr;
      CHECK(canvas->edit_locked());
      CHECK(!layer_list->isEnabled());
      CHECK(!require_action(window, "layerNewAction")->isEnabled());
      CHECK(!require_action(window, "layerFillForegroundAction")->isEnabled());
      CHECK(require_action(window, "viewZoomInAction")->isEnabled());
      const auto locked_pixel_before_edit = canvas_pixel(*canvas, QPoint(80, 80));
      drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 80)),
           canvas->widget_position_for_document_point(QPoint(98, 98)));
      const auto locked_pixel_after_edit = canvas_pixel(*canvas, QPoint(80, 80));
      saw_layer_style_edit_lock = color_close(locked_pixel_after_edit, locked_pixel_before_edit, 2);
      gradient_check->setChecked(true);
      gradient_angle_slider->setValue(0);
      QApplication::processEvents();
      saw_live_style_preview = !color_close(canvas_pixel(*canvas, QPoint(80, 80)), before, 20);
      gradient_stops->setCurrentCell(0, 0);
      QApplication::processEvents();
      const auto selected_stop_rect = gradient_stops->visualItemRect(gradient_stops->item(0, 1));
      const auto selected_stop_image = gradient_stops->viewport()->grab(selected_stop_rect).toImage();
      CHECK(color_close(selected_stop_image.pixelColor(selected_stop_image.rect().center()), QColor(255, 255, 255), 20));
      QTimer::singleShot(0, [&saw_shared_color_picker] {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
            continue;
          }
          auto* color_dialog = qobject_cast<QDialog*>(widget);
          CHECK(color_dialog != nullptr);
          auto* picker =
              color_dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
          CHECK(picker != nullptr);
          picker->setCurrentColor(QColor(64, 128, 192));
          saw_shared_color_picker = true;
          color_dialog->accept();
          return;
        }
        CHECK(false);
      });
      pick_gradient_color->click();
      CHECK(gradient_stops->item(0, 1)->text() == QStringLiteral("64"));
      CHECK(gradient_stops->item(0, 2)->text() == QStringLiteral("128"));
      CHECK(gradient_stops->item(0, 3)->text() == QStringLiteral("192"));
      gradient_stops->setCurrentCell(1, 0);
      QApplication::processEvents();
      CHECK(selected_gradient_color->styleSheet().contains(QStringLiteral("rgb(32, 32, 32)")));
      bool saw_gradient_swatch_picker = false;
      QTimer::singleShot(0, [&saw_gradient_swatch_picker] {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
            continue;
          }
          auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
          CHECK(picker != nullptr);
          picker->setCurrentColor(QColor(220, 40, 96));
          saw_gradient_swatch_picker = true;
          qobject_cast<QDialog*>(widget)->accept();
          return;
        }
        CHECK(false);
      });
      selected_gradient_color->click();
      CHECK(saw_gradient_swatch_picker);
      CHECK(gradient_stops->item(1, 1)->text() == QStringLiteral("220"));
      CHECK(gradient_stops->item(1, 2)->text() == QStringLiteral("40"));
      CHECK(gradient_stops->item(1, 3)->text() == QStringLiteral("96"));
      bool saw_stroke_color_picker = false;
      QTimer::singleShot(0, [&saw_stroke_color_picker] {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
            continue;
          }
          auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
          CHECK(picker != nullptr);
          picker->setCurrentColor(QColor(24, 48, 72));
          saw_stroke_color_picker = true;
          qobject_cast<QDialog*>(widget)->accept();
          return;
        }
        CHECK(false);
      });
      stroke_color->click();
      CHECK(saw_stroke_color_picker);
      CHECK(stroke_red->value() == 24);
      CHECK(stroke_green->value() == 48);
      CHECK(stroke_blue->value() == 72);
      bool saw_outer_glow_color_picker = false;
      QTimer::singleShot(0, [&saw_outer_glow_color_picker] {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
            continue;
          }
          auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
          CHECK(picker != nullptr);
          picker->setCurrentColor(QColor(190, 170, 64));
          saw_outer_glow_color_picker = true;
          qobject_cast<QDialog*>(widget)->accept();
          return;
        }
        CHECK(false);
      });
      outer_glow_color->click();
      CHECK(saw_outer_glow_color_picker);
      CHECK(outer_glow_red->value() == 190);
      CHECK(outer_glow_green->value() == 170);
      CHECK(outer_glow_blue->value() == 64);
      bool saw_shadow_color_picker = false;
      QTimer::singleShot(0, [&saw_shadow_color_picker] {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
            continue;
          }
          auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
          CHECK(picker != nullptr);
          picker->setCurrentColor(QColor(250, 250, 250));
          saw_shadow_color_picker = true;
          qobject_cast<QDialog*>(widget)->accept();
          return;
        }
        CHECK(false);
      });
      shadow_color->click();
      CHECK(saw_shadow_color_picker);
      CHECK(shadow_red->value() == 250);
      CHECK(shadow_green->value() == 250);
      CHECK(shadow_blue->value() == 250);
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  blending_options->trigger();
  QApplication::processEvents();
  CHECK(saw_non_modal_dialog);
  CHECK(saw_live_style_preview);
  CHECK(saw_layer_style_edit_lock);
  CHECK(saw_shared_color_picker);
  CHECK(!canvas->edit_locked());
  CHECK(layer_list->isEnabled());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), before, 8));

  accept_layer_style_dialog(false, true, false);
  blending_options->trigger();
  QApplication::processEvents();
  const auto after = canvas_pixel(*canvas, QPoint(80, 80));
  CHECK(!color_close(before, after, 20));

  auto* styled_item = layer_list->item(0);
  CHECK(styled_item != nullptr);
  styled_item->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  styled_item->setCheckState(Qt::Checked);
  QApplication::processEvents();
  const auto after_visibility_toggle = canvas_pixel(*canvas, QPoint(80, 80));
  CHECK(color_close(after, after_visibility_toggle, 8));

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(false);
  QApplication::processEvents();
  const auto before_move_click = canvas->grab().toImage();
  const auto move_point = canvas->widget_position_for_document_point(QPoint(80, 80));
  send_mouse(*canvas, QEvent::MouseButtonPress, move_point, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  const auto during_move_click = canvas->grab().toImage();
  CHECK(color_close(during_move_click.pixelColor(move_point), before_move_click.pixelColor(move_point), 0));
  send_mouse(*canvas, QEvent::MouseButtonRelease, move_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto after_move_click = canvas->grab().toImage();
  CHECK(color_close(after_move_click.pixelColor(move_point), before_move_click.pixelColor(move_point), 0));

  auto* details = layer_list->itemWidget(layer_list->item(0))->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
  CHECK(details != nullptr);
  CHECK(details->text().contains(QStringLiteral("fx")));
  save_widget_artifact("ui_layer_style_result", window);
}

void ui_layer_row_double_click_opens_blending_options_dialog() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* item = layer_list->item(0);
  CHECK(item != nullptr);
  auto* row_widget = layer_list->itemWidget(item);
  CHECK(row_widget != nullptr);
  auto* row_name = row_widget->findChild<QLabel*>(QStringLiteral("layerRowName"));
  CHECK(row_name != nullptr);

  bool saw_blending_options = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog"));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBlendModeCombo"));
    CHECK(categories != nullptr);
    CHECK(blend != nullptr);
    CHECK(categories->currentItem() != nullptr);
    CHECK(categories->currentItem()->text() == QStringLiteral("Blending Options"));
    saw_blending_options = true;
    dialog->reject();
  });

  send_double_click(*row_name, row_name->rect().center());
  QApplication::processEvents();
  CHECK(saw_blending_options);
}

void ui_layer_context_menu_rasterizes_text_and_layer_styles() {
  {
    patchy::Document document(140, 96, patchy::PixelFormat::rgba8());
    patchy::Layer text_layer(document.allocate_layer_id(), "Text: Raster", patchy::LayerKind::Text);
    text_layer.set_bounds(patchy::Rect{24, 24, 96, 36});
    text_layer.metadata()[patchy::kLayerMetadataText] = "Raster";
    text_layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
    text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#000000";
    patchy::LayerDropShadow text_shadow;
    text_shadow.enabled = true;
    text_shadow.opacity = 1.0F;
    text_shadow.distance = 6.0F;
    text_shadow.size = 6.0F;
    text_layer.layer_style().drop_shadows.push_back(text_shadow);
    document.add_layer(std::move(text_layer));

    patchy::ui::MainWindow window;
    window.add_document_session(std::move(document), QStringLiteral("Rasterize Text"));
    show_window(window);
    auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    auto* layer_info = window.findChild<QLabel*>(QStringLiteral("activeLayerInfoLabel"));
    auto* geometry = window.findChild<QLabel*>(QStringLiteral("activeLayerGeometryLabel"));
    auto* text_info = window.findChild<QLabel*>(QStringLiteral("activeLayerTextLabel"));
    CHECK(layer_list != nullptr);
    CHECK(layer_info != nullptr);
    CHECK(geometry != nullptr);
    CHECK(text_info != nullptr);
    auto* thumbnail = layer_list->itemWidget(layer_list->item(0))->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
    CHECK(thumbnail != nullptr);
    CHECK(thumbnail->toolTip() == QStringLiteral("Text layer"));
    const auto thumbnail_image = thumbnail->pixmap(Qt::ReturnByValue).toImage();
    int bright_thumbnail_pixels = 0;
    for (int y = 0; y < thumbnail_image.height(); ++y) {
      for (int x = 0; x < thumbnail_image.width(); ++x) {
        const auto color = thumbnail_image.pixelColor(x, y);
        if (color.red() + color.green() + color.blue() > 600) {
          ++bright_thumbnail_pixels;
        }
      }
    }
    CHECK(bright_thumbnail_pixels > 20);
    CHECK(layer_info->text().contains(QStringLiteral("Text Layer")));
    CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
    CHECK(!text_info->isHidden());

    require_action(window, "layerRasterizeAction")->trigger();
    QApplication::processEvents();

    CHECK(layer_info->text().contains(QStringLiteral("Pixel Layer")));
    CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
    CHECK(text_info->isHidden());
    auto* name = layer_list->itemWidget(layer_list->item(0))->findChild<QLabel*>(QStringLiteral("layerRowName"));
    CHECK(name != nullptr);
    CHECK(name->text() == QStringLiteral("Raster"));
    CHECK(!require_action(window, "layerRasterizeAction")->isEnabled());
    CHECK(require_action(window, "layerRasterizeLayerStyleAction")->isEnabled());
  }

  {
    patchy::Document document(140, 96, patchy::PixelFormat::rgba8());
    auto pixels = solid_pixels(36, 28, patchy::PixelFormat::rgba8(), QColor(40, 90, 220, 255));
    patchy::Layer styled_layer(document.allocate_layer_id(), "Styled Pixel", std::move(pixels));
    styled_layer.set_bounds(patchy::Rect{34, 28, 36, 28});
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.opacity = 1.0F;
    shadow.distance = 8.0F;
    shadow.size = 8.0F;
    shadow.color = patchy::RgbColor{0, 0, 0};
    styled_layer.layer_style().drop_shadows.push_back(shadow);
    document.add_layer(std::move(styled_layer));

    patchy::ui::MainWindow window;
    window.add_document_session(std::move(document), QStringLiteral("Rasterize Style"));
    show_window(window);
    auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    auto* geometry = window.findChild<QLabel*>(QStringLiteral("activeLayerGeometryLabel"));
    CHECK(layer_list != nullptr);
    CHECK(geometry != nullptr);
    CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
    CHECK(!require_action(window, "layerRasterizeAction")->isEnabled());
    CHECK(require_action(window, "layerRasterizeLayerStyleAction")->isEnabled());

    require_action(window, "layerRasterizeLayerStyleAction")->trigger();
    QApplication::processEvents();

    CHECK(geometry->text().contains(QStringLiteral("Effects: none")));
    CHECK(!require_action(window, "layerRasterizeLayerStyleAction")->isEnabled());
  }
}

void ui_layer_context_menu_layer_style_actions_follow_selection_state() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  patchy::Layer styled_layer(document.allocate_layer_id(), "Styled Source",
                             solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255)));
  styled_layer.set_bounds(patchy::Rect{16, 16, 24, 24});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 1.0F;
  shadow.distance = 8.0F;
  shadow.size = 8.0F;
  styled_layer.layer_style().drop_shadows.push_back(shadow);
  document.add_layer(std::move(styled_layer));

  patchy::Layer target_a_layer(document.allocate_layer_id(), "Plain Target A",
                               solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(40, 180, 80, 255)));
  target_a_layer.set_bounds(patchy::Rect{56, 16, 24, 24});
  document.add_layer(std::move(target_a_layer));

  patchy::Layer target_b_layer(document.allocate_layer_id(), "Plain Target B",
                               solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 220, 255)));
  target_b_layer.set_bounds(patchy::Rect{96, 16, 24, 24});
  document.add_layer(std::move(target_b_layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Layer Style Menu State"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  auto* styled_item = require_layer_item(*layer_list, QStringLiteral("Styled Source"));
  auto* target_a_item = require_layer_item(*layer_list, QStringLiteral("Plain Target A"));
  auto* target_b_item = require_layer_item(*layer_list, QStringLiteral("Plain Target B"));

  layer_list->clearSelection();
  layer_list->setCurrentItem(styled_item);
  styled_item->setSelected(true);
  QApplication::processEvents();
  auto state = layer_style_context_menu_state(*layer_list, *styled_item);
  CHECK(state.saw_copy);
  CHECK(state.saw_paste);
  CHECK(state.saw_delete);
  CHECK(state.copy_enabled);
  CHECK(!state.paste_enabled);
  CHECK(state.delete_enabled);

  layer_list->clearSelection();
  target_a_item->setSelected(true);
  target_b_item->setSelected(true);
  QApplication::processEvents();
  state = layer_style_context_menu_state(*layer_list, *target_a_item);
  CHECK(state.saw_copy);
  CHECK(state.saw_paste);
  CHECK(state.saw_delete);
  CHECK(!state.copy_enabled);
  CHECK(!state.paste_enabled);
  CHECK(!state.delete_enabled);

  layer_list->clearSelection();
  styled_item->setSelected(true);
  target_a_item->setSelected(true);
  QApplication::processEvents();
  state = layer_style_context_menu_state(*layer_list, *target_a_item);
  CHECK(!state.copy_enabled);
  CHECK(!state.paste_enabled);
  CHECK(state.delete_enabled);
}

void ui_layer_style_copy_paste_delete_applies_to_selected_layers() {
  patchy::Document document(180, 120, patchy::PixelFormat::rgba8());
  patchy::Layer source_layer(document.allocate_layer_id(), "Style Source",
                             solid_pixels(26, 26, patchy::PixelFormat::rgba8(), QColor(230, 60, 40, 255)));
  source_layer.set_bounds(patchy::Rect{16, 22, 26, 26});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 0.9F;
  shadow.distance = 7.0F;
  shadow.size = 7.0F;
  source_layer.layer_style().drop_shadows.push_back(shadow);
  document.add_layer(std::move(source_layer));

  patchy::Layer target_a_layer(document.allocate_layer_id(), "Target Multiply",
                               solid_pixels(26, 26, patchy::PixelFormat::rgba8(), QColor(40, 180, 80, 255)));
  target_a_layer.set_bounds(patchy::Rect{66, 22, 26, 26});
  target_a_layer.set_blend_mode(patchy::BlendMode::Multiply);
  target_a_layer.set_opacity(0.42F);
  document.add_layer(std::move(target_a_layer));

  patchy::Layer target_b_layer(document.allocate_layer_id(), "Target Screen",
                               solid_pixels(26, 26, patchy::PixelFormat::rgba8(), QColor(40, 80, 220, 255)));
  target_b_layer.set_bounds(patchy::Rect{116, 22, 26, 26});
  target_b_layer.set_blend_mode(patchy::BlendMode::Screen);
  target_b_layer.set_opacity(0.77F);
  document.add_layer(std::move(target_b_layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Layer Style Copy Paste Delete"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* layer_info = window.findChild<QLabel*>(QStringLiteral("activeLayerInfoLabel"));
  auto* geometry = window.findChild<QLabel*>(QStringLiteral("activeLayerGeometryLabel"));
  CHECK(layer_list != nullptr);
  CHECK(layer_info != nullptr);
  CHECK(geometry != nullptr);

  auto select_single_layer = [&](const QString& name) {
    auto* item = require_layer_item(*layer_list, name);
    layer_list->clearSelection();
    layer_list->setCurrentItem(item);
    item->setSelected(true);
    QApplication::processEvents();
    return item;
  };

  select_single_layer(QStringLiteral("Style Source"));
  auto* copy_style = require_action(window, "layerCopyStyleAction");
  auto* paste_style = require_action(window, "layerPasteStyleAction");
  auto* delete_style = require_action(window, "layerDeleteStyleAction");
  CHECK(copy_style->isEnabled());
  CHECK(!paste_style->isEnabled());
  copy_style->trigger();
  QApplication::processEvents();
  CHECK(paste_style->isEnabled());

  auto* target_a_item = require_layer_item(*layer_list, QStringLiteral("Target Multiply"));
  auto* target_b_item = require_layer_item(*layer_list, QStringLiteral("Target Screen"));
  layer_list->clearSelection();
  target_a_item->setSelected(true);
  target_b_item->setSelected(true);
  QApplication::processEvents();
  CHECK(!copy_style->isEnabled());
  CHECK(paste_style->isEnabled());
  paste_style->trigger();
  QApplication::processEvents();

  select_single_layer(QStringLiteral("Target Multiply"));
  CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
  CHECK(layer_info->text().contains(QStringLiteral("Mode: Multiply")));
  CHECK(layer_info->text().contains(QStringLiteral("Opacity: 42%")));

  select_single_layer(QStringLiteral("Target Screen"));
  CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
  CHECK(layer_info->text().contains(QStringLiteral("Mode: Screen")));
  CHECK(layer_info->text().contains(QStringLiteral("Opacity: 77%")));

  target_a_item = require_layer_item(*layer_list, QStringLiteral("Target Multiply"));
  target_b_item = require_layer_item(*layer_list, QStringLiteral("Target Screen"));
  layer_list->clearSelection();
  target_a_item->setSelected(true);
  target_b_item->setSelected(true);
  QApplication::processEvents();
  CHECK(delete_style->isEnabled());
  delete_style->trigger();
  QApplication::processEvents();

  select_single_layer(QStringLiteral("Target Multiply"));
  CHECK(geometry->text().contains(QStringLiteral("Effects: none")));
  CHECK(layer_info->text().contains(QStringLiteral("Mode: Multiply")));
  CHECK(layer_info->text().contains(QStringLiteral("Opacity: 42%")));

  select_single_layer(QStringLiteral("Target Screen"));
  CHECK(geometry->text().contains(QStringLiteral("Effects: none")));
  CHECK(layer_info->text().contains(QStringLiteral("Mode: Screen")));
  CHECK(layer_info->text().contains(QStringLiteral("Opacity: 77%")));

  select_single_layer(QStringLiteral("Style Source"));
  CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
}

void accept_new_document_dialog(int width_value, int height_value) {
  QTimer::singleShot(0, [width_value, height_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyNewDocumentDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* width = dialog->findChild<QSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
      auto* height = dialog->findChild<QSpinBox*>(QStringLiteral("newDocumentHeightSpin"));
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      CHECK(width->buttonSymbols() == QAbstractSpinBox::NoButtons);
      CHECK(height->buttonSymbols() == QAbstractSpinBox::NoButtons);
      width->setValue(width_value);
      height->setValue(height_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_new_document_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void inspect_new_document_dialog_without_clipboard() {
  QTimer::singleShot(0, [] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyNewDocumentDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* preset = dialog->findChild<QComboBox*>(QStringLiteral("newDocumentPresetCombo"));
      auto* width = dialog->findChild<QSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
      auto* height = dialog->findChild<QSpinBox*>(QStringLiteral("newDocumentHeightSpin"));
      CHECK(dialog != nullptr);
      CHECK(preset != nullptr);
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      const QStringList labels = {
          QStringLiteral("Clipboard"), QStringLiteral("1024 x 768"), QStringLiteral("A4 300 ppi"),
          QStringLiteral("A3 300 ppi"), QStringLiteral("1080p"), QStringLiteral("4K")};
      CHECK(preset->count() == static_cast<int>(labels.size()));
      for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
        CHECK(preset->itemText(index) == labels[index]);
      }
      CHECK((preset->model()->flags(preset->model()->index(0, 0)) & Qt::ItemIsEnabled) == 0);
      CHECK(preset->currentText() == QStringLiteral("1024 x 768"));
      CHECK(width->value() == 1024);
      CHECK(height->value() == 768);
      dialog->reject();
      return;
    }
    CHECK(false);
  });
}

void accept_clipboard_new_document_dialog(QSize clipboard_size) {
  QTimer::singleShot(0, [clipboard_size] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyNewDocumentDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* preset = dialog->findChild<QComboBox*>(QStringLiteral("newDocumentPresetCombo"));
      auto* width = dialog->findChild<QSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
      auto* height = dialog->findChild<QSpinBox*>(QStringLiteral("newDocumentHeightSpin"));
      auto* background = dialog->findChild<QComboBox*>(QStringLiteral("newDocumentBackgroundCombo"));
      CHECK(dialog != nullptr);
      CHECK(preset != nullptr);
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      CHECK(background != nullptr);

      const auto clipboard_index = preset->findText(QStringLiteral("Clipboard"));
      CHECK(clipboard_index == 0);
      CHECK((preset->model()->flags(preset->model()->index(clipboard_index, 0)) & Qt::ItemIsEnabled) != 0);

      const std::vector<std::pair<QString, QSize>> expected_sizes = {
          {QStringLiteral("Clipboard"), clipboard_size},
          {QStringLiteral("A4 300 ppi"), QSize(2480, 3508)},
          {QStringLiteral("A3 300 ppi"), QSize(3508, 4961)},
          {QStringLiteral("1080p"), QSize(1920, 1080)},
          {QStringLiteral("4K"), QSize(3840, 2160)},
      };
      for (const auto& [label, size] : expected_sizes) {
        const auto index = preset->findText(label);
        CHECK(index >= 0);
        preset->setCurrentIndex(index);
        QApplication::processEvents();
        CHECK(width->value() == size.width());
        CHECK(height->value() == size.height());
      }

      preset->setCurrentIndex(clipboard_index);
      QApplication::processEvents();
      CHECK(!width->isEnabled());
      CHECK(!height->isEnabled());
      CHECK(!background->isEnabled());
      dialog->accept();
      return;
    }
    CHECK(false);
  });
}

void accept_integer_dialog(const QString& object_name, int value) {
  QTimer::singleShot(0, [object_name, value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != object_name) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      auto* spin = dialog->findChild<QSpinBox*>(QStringLiteral("integerInputSpin"));
      CHECK(spin != nullptr);
      CHECK(spin->minimum() <= value);
      CHECK(spin->maximum() >= value);
      CHECK(spin->buttonSymbols() == QAbstractSpinBox::NoButtons);
      spin->setValue(value);
      dialog->accept();
      return;
    }
  });
}

void accept_canvas_size_dialog(int width_value, int height_value) {
  QTimer::singleShot(0, [width_value, height_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyCanvasSizeDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* width = dialog->findChild<QSpinBox*>(QStringLiteral("canvasSizeWidthSpin"));
      auto* height = dialog->findChild<QSpinBox*>(QStringLiteral("canvasSizeHeightSpin"));
      auto* new_size = dialog->findChild<QLabel*>(QStringLiteral("canvasSizeNewSizeLabel"));
      auto* relative = dialog->findChild<QCheckBox*>(QStringLiteral("canvasSizeRelativeCheck"));
      auto* width_unit = dialog->findChild<QComboBox*>(QStringLiteral("canvasSizeWidthUnitCombo"));
      auto* height_unit = dialog->findChild<QComboBox*>(QStringLiteral("canvasSizeHeightUnitCombo"));
      auto* extension_color = dialog->findChild<QComboBox*>(QStringLiteral("canvasSizeExtensionColorCombo"));
      auto* color_swatch = dialog->findChild<QPushButton*>(QStringLiteral("canvasSizeExtensionColorSwatch"));
      const auto anchors = dialog->findChildren<QToolButton*>(QStringLiteral("canvasSizeAnchorButton"));
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      CHECK(new_size != nullptr);
      CHECK(relative != nullptr);
      CHECK(width_unit != nullptr);
      CHECK(height_unit != nullptr);
      CHECK(extension_color != nullptr);
      CHECK(color_swatch != nullptr);
      CHECK(anchors.size() == 9);
      CHECK(color_swatch->toolTip() == QStringLiteral("Choose canvas extension color"));
      CHECK(width->buttonSymbols() == QAbstractSpinBox::NoButtons);
      CHECK(height->buttonSymbols() == QAbstractSpinBox::NoButtons);
      CHECK(!relative->isChecked());
      CHECK(width_unit->currentText() == QStringLiteral("Pixels"));
      CHECK(height_unit->currentText() == QStringLiteral("Pixels"));
      CHECK(extension_color->currentText() == QStringLiteral("Other..."));
      CHECK(std::any_of(anchors.begin(), anchors.end(), [](QToolButton* button) {
        return button->isChecked() && button->toolTip() == QStringLiteral("Anchor center");
      }));
      width->setValue(width_value);
      height->setValue(height_value);
      CHECK(new_size->text().contains(QStringLiteral("New Size:")));
      widget->grab().save(QStringLiteral("test-artifacts/ui_canvas_size_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_image_size_dialog(int width_value, int height_value) {
  QTimer::singleShot(0, [width_value, height_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyImageSizeDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* width = dialog->findChild<QSpinBox*>(QStringLiteral("imageSizeWidthSpin"));
      auto* height = dialog->findChild<QSpinBox*>(QStringLiteral("imageSizeHeightSpin"));
      auto* dimensions = dialog->findChild<QLabel*>(QStringLiteral("imageSizeDimensionsLabel"));
      auto* preview = dialog->findChild<QLabel*>(QStringLiteral("imageSizePreview"));
      auto* resample = dialog->findChild<QCheckBox*>(QStringLiteral("imageSizeResampleCheck"));
      auto* method = dialog->findChild<QComboBox*>(QStringLiteral("imageSizeResampleCombo"));
      auto* link = dialog->findChild<QToolButton*>(QStringLiteral("imageSizeLinkButton"));
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      CHECK(dimensions != nullptr);
      CHECK(preview != nullptr);
      CHECK(resample != nullptr);
      CHECK(method != nullptr);
      CHECK(link != nullptr);
      CHECK(width->buttonSymbols() == QAbstractSpinBox::NoButtons);
      CHECK(height->buttonSymbols() == QAbstractSpinBox::NoButtons);
      CHECK(dimensions->text().contains(QStringLiteral("px x")));
      CHECK(resample->isChecked());
      CHECK(method->currentText() == QStringLiteral("Bicubic Sharper (reduction)"));
      CHECK(link->isChecked());
      width->setValue(width_value);
      height->setValue(height_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_image_size_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_image_size_resolution_dialog(int resolution_value) {
  QTimer::singleShot(0, [resolution_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyImageSizeDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* resolution = dialog->findChild<QSpinBox*>(QStringLiteral("imageSizeResolutionSpin"));
      auto* resample = dialog->findChild<QCheckBox*>(QStringLiteral("imageSizeResampleCheck"));
      CHECK(resolution != nullptr);
      CHECK(resample != nullptr);
      resample->setChecked(false);
      resolution->setValue(resolution_value);
      dialog->accept();
      return;
    }
  });
}

void accept_layer_style_dialog(bool stroke_enabled, bool gradient_enabled, bool shadow_enabled) {
  QTimer::singleShot(0, [stroke_enabled, gradient_enabled, shadow_enabled] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyLayerStyleDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
      auto* blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBlendModeCombo"));
      auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOpacitySpin"));
      auto* opacity_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleOpacitySlider"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePreviewCheck"));
      auto* options_stack = dialog->findChild<QWidget*>(QStringLiteral("layerStyleOptionsStack"));
      auto* stroke_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleStrokeCategoryCheck"));
      auto* gradient_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleGradientOverlayCategoryCheck"));
      auto* outer_glow_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleOuterGlowCategoryCheck"));
      auto* inner_glow_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleInnerGlowCategoryCheck"));
      auto* shadow_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleDropShadowCategoryCheck"));
      auto* inner_shadow_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleInnerShadowCategoryCheck"));
      auto* bevel_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleBevelEmbossCategoryCheck"));
      auto* stroke_size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeSizeSpin"));
      auto* stroke_size_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleStrokeSizeSlider"));
      auto* gradient_angle = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientAngleSpin"));
      auto* gradient_angle_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleGradientAngleSlider"));
      auto* gradient_stops = dialog->findChild<QTableWidget*>(QStringLiteral("layerStyleGradientStopsTable"));
      auto* add_gradient_stop = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleGradientAddStopButton"));
      auto* outer_glow_size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowSizeSpin"));
      auto* outer_glow_size_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleOuterGlowSizeSlider"));
      auto* inner_glow_size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleInnerGlowSizeSpin"));
      auto* inner_glow_size_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleInnerGlowSizeSlider"));
      auto* add_inner_glow = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddInnerGlowButton"));
      auto* remove_inner_glow = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleRemoveInnerGlowButton"));
      auto* add_inner_glow_instance = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddInnerGlowInstanceButton"));
      auto* add_stroke_instance = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddStrokeInstanceButton"));
      auto* add_color_overlay_instance =
          dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddColorOverlayInstanceButton"));
      auto* add_gradient_instance =
          dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddGradientOverlayInstanceButton"));
      auto* add_outer_glow_instance =
          dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddOuterGlowInstanceButton"));
      auto* add_drop_shadow_instance =
          dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddDropShadowInstanceButton"));
      auto* remove_selected_instance =
          dialog->findChild<QPushButton*>(QStringLiteral("layerStyleRemoveSelectedInstanceButton"));
      auto* shadow_blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleDropShadowBlendModeCombo"));
      auto* shadow_distance = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowDistanceSpin"));
      auto* shadow_distance_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleDropShadowDistanceSlider"));
      auto* inner_shadow_distance = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleInnerShadowDistanceSpin"));
      auto* inner_shadow_distance_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleInnerShadowDistanceSlider"));
      auto* add_inner_shadow = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddInnerShadowButton"));
      auto* remove_inner_shadow = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleRemoveInnerShadowButton"));
      auto* add_inner_shadow_instance =
          dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddInnerShadowInstanceButton"));
      auto* shadow_red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowRedSpin"));
      auto* shadow_red_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleDropShadowRedSlider"));
      auto* shadow_green = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowGreenSpin"));
      auto* shadow_blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowBlueSpin"));
      auto* bevel_size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBevelSizeSpin"));
      auto* bevel_size_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleBevelSizeSlider"));
      auto* stroke_red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeRedSpin"));
      auto* stroke_red_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleStrokeRedSlider"));
      auto* outer_glow_blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowBlueSpin"));
      auto* outer_glow_blue_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleOuterGlowBlueSlider"));
      CHECK(categories != nullptr);
      CHECK(blend != nullptr);
      CHECK(opacity != nullptr);
      CHECK(opacity_slider != nullptr);
      CHECK(preview != nullptr);
      CHECK(options_stack != nullptr);
      CHECK(stroke_check != nullptr);
      CHECK(gradient_check != nullptr);
      CHECK(outer_glow_check != nullptr);
      CHECK(inner_glow_check != nullptr);
      CHECK(shadow_check != nullptr);
      CHECK(inner_shadow_check != nullptr);
      CHECK(bevel_check != nullptr);
      CHECK(stroke_size != nullptr);
      CHECK(stroke_size_slider != nullptr);
      CHECK(gradient_angle != nullptr);
      CHECK(gradient_angle_slider != nullptr);
      CHECK(gradient_stops != nullptr);
      CHECK(add_gradient_stop != nullptr);
      CHECK(outer_glow_size != nullptr);
      CHECK(outer_glow_size_slider != nullptr);
      CHECK(inner_glow_size != nullptr);
      CHECK(inner_glow_size_slider != nullptr);
      CHECK(add_inner_glow != nullptr);
      CHECK(remove_inner_glow != nullptr);
      CHECK(add_inner_glow_instance != nullptr);
      CHECK(add_stroke_instance != nullptr);
      CHECK(add_color_overlay_instance != nullptr);
      CHECK(add_gradient_instance != nullptr);
      CHECK(add_outer_glow_instance != nullptr);
      CHECK(add_drop_shadow_instance != nullptr);
      CHECK(remove_selected_instance != nullptr);
      CHECK(shadow_blend != nullptr);
      CHECK(shadow_distance != nullptr);
      CHECK(shadow_distance_slider != nullptr);
      CHECK(inner_shadow_distance != nullptr);
      CHECK(inner_shadow_distance_slider != nullptr);
      CHECK(add_inner_shadow != nullptr);
      CHECK(remove_inner_shadow != nullptr);
      CHECK(add_inner_shadow_instance != nullptr);
      CHECK(shadow_red != nullptr);
      CHECK(shadow_red_slider != nullptr);
      CHECK(shadow_green != nullptr);
      CHECK(shadow_blue != nullptr);
      CHECK(bevel_size != nullptr);
      CHECK(bevel_size_slider != nullptr);
      CHECK(stroke_red != nullptr);
      CHECK(stroke_red_slider != nullptr);
      CHECK(outer_glow_blue != nullptr);
      CHECK(outer_glow_blue_slider != nullptr);
      const auto check_compact_symbol_button = [](QPushButton* button) {
        CHECK(button != nullptr);
        CHECK(button->property("compactSymbolButton").toBool());
        CHECK(button->width() >= 22);
        CHECK(button->height() >= 22);
        CHECK(button->text().isEmpty());
        CHECK(!button->icon().isNull());
        CHECK(button->iconSize() == QSize(16, 16));
      };
      const auto check_category_symbol_button = [&](QPushButton* button) {
        check_compact_symbol_button(button);
        CHECK(button->parentWidget() != nullptr);
        CHECK(button->parentWidget()->height() >= button->height() + 6);
        CHECK(button->geometry().top() >= 2);
        CHECK(button->geometry().bottom() <= button->parentWidget()->height() - 3);
      };
      for (auto* button : {add_inner_glow, add_inner_shadow, remove_inner_shadow, remove_inner_glow}) {
        check_compact_symbol_button(button);
      }
      for (auto* button : {add_inner_glow_instance,
                           add_stroke_instance,
                           add_color_overlay_instance,
                           add_gradient_instance,
                           add_outer_glow_instance,
                           add_drop_shadow_instance,
                           add_inner_shadow_instance}) {
        check_category_symbol_button(button);
      }
      CHECK(!dialog->isModal());
      CHECK(dialog->windowModality() == Qt::NonModal);
      CHECK(preview->isChecked());
      auto find_item = [categories](const QString& text) {
        const auto items = categories->findItems(text, Qt::MatchExactly);
        CHECK(!items.empty());
        return items.front();
      };
      auto* blending_item = find_item(QStringLiteral("Blending Options"));
      auto* bevel_item = find_item(QStringLiteral("Bevel & Emboss"));
      auto* stroke_item = find_item(QStringLiteral("Stroke"));
      auto* inner_shadow_item = find_item(QStringLiteral("Inner Shadow"));
      auto* inner_glow_item = find_item(QStringLiteral("Inner Glow"));
      auto* gradient_item = find_item(QStringLiteral("Gradient Overlay"));
      auto* outer_glow_item = find_item(QStringLiteral("Outer Glow"));
      auto* shadow_item = find_item(QStringLiteral("Drop Shadow"));
      CHECK((bevel_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((stroke_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((inner_shadow_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((inner_glow_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((gradient_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((outer_glow_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((shadow_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((blending_item->flags() & Qt::ItemIsUserCheckable) == 0);
      CHECK(blend->findText(QStringLiteral("Pin Light")) >= 0);
      opacity_slider->setValue(92);
      CHECK(opacity->value() == 92);
      stroke_item->setCheckState(stroke_enabled ? Qt::Checked : Qt::Unchecked);
      gradient_item->setCheckState(gradient_enabled ? Qt::Checked : Qt::Unchecked);
      outer_glow_item->setCheckState(Qt::Checked);
      inner_glow_item->setCheckState(Qt::Checked);
      shadow_item->setCheckState(shadow_enabled ? Qt::Checked : Qt::Unchecked);
      inner_shadow_item->setCheckState(Qt::Checked);
      bevel_item->setCheckState(Qt::Checked);
      categories->setCurrentItem(stroke_item);
      stroke_size_slider->setValue(6);
      CHECK(stroke_size->value() == 6);
      stroke_red_slider->setValue(32);
      CHECK(stroke_red->value() == 32);
      categories->setCurrentItem(bevel_item);
      bevel_size_slider->setValue(7);
      CHECK(bevel_size->value() == 7);
      categories->setCurrentItem(outer_glow_item);
      outer_glow_size_slider->setValue(8);
      CHECK(outer_glow_size->value() == 8);
      outer_glow_blue_slider->setValue(210);
      CHECK(outer_glow_blue->value() == 210);
      categories->setCurrentItem(inner_glow_item);
      inner_glow_size_slider->setValue(9);
      CHECK(inner_glow_size->value() == 9);
      categories->setCurrentItem(gradient_enabled ? gradient_item : blending_item);
      gradient_angle_slider->setValue(0);
      CHECK(gradient_angle->value() == 0);
      categories->setCurrentItem(shadow_item);
      shadow_blend->setCurrentIndex(std::max(0, shadow_blend->findData(static_cast<int>(patchy::BlendMode::Normal))));
      CHECK(shadow_blend->currentData().toInt() == static_cast<int>(patchy::BlendMode::Normal));
      shadow_red_slider->setValue(245);
      CHECK(shadow_red->value() == 245);
      shadow_green->setValue(246);
      shadow_blue->setValue(247);
      shadow_distance_slider->setValue(10);
      CHECK(shadow_distance->value() == 10);
      categories->setCurrentItem(gradient_enabled ? gradient_item : blending_item);
      const auto original_stop_count = gradient_stops->rowCount();
      CHECK(original_stop_count >= 2);
      gradient_stops->setCurrentCell(0, 0);
      add_gradient_stop->click();
      CHECK(gradient_stops->rowCount() == original_stop_count + 1);
      gradient_stops->item(gradient_stops->rowCount() - 1, 0)->setText(QStringLiteral("50"));
      gradient_stops->item(gradient_stops->rowCount() - 1, 1)->setText(QStringLiteral("255"));
      gradient_stops->item(gradient_stops->rowCount() - 1, 2)->setText(QStringLiteral("160"));
      gradient_stops->item(gradient_stops->rowCount() - 1, 3)->setText(QStringLiteral("0"));
      categories->setCurrentItem(inner_shadow_item);
      inner_shadow_distance_slider->setValue(3);
      CHECK(inner_shadow_distance->value() == 3);
      categories->setCurrentItem(inner_glow_item);
      add_inner_glow_instance->click();
      QApplication::processEvents();
      CHECK(categories->findItems(QStringLiteral("Inner Glow"), Qt::MatchExactly).size() == 2);
      remove_selected_instance->click();
      QApplication::processEvents();
      CHECK(categories->findItems(QStringLiteral("Inner Glow"), Qt::MatchExactly).size() == 1);
      inner_shadow_item = find_item(QStringLiteral("Inner Shadow"));
      categories->setCurrentItem(inner_shadow_item);
      add_inner_shadow_instance = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddInnerShadowInstanceButton"));
      CHECK(add_inner_shadow_instance != nullptr);
      add_inner_shadow_instance->click();
      QApplication::processEvents();
      CHECK(categories->findItems(QStringLiteral("Inner Shadow"), Qt::MatchExactly).size() == 2);
      widget->grab().save(QStringLiteral("test-artifacts/ui_layer_style_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_transform_dialog(int x_value, int y_value, int width_value, int height_value) {
  QTimer::singleShot(0, [x_value, y_value, width_value, height_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyTransformDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* x = dialog->findChild<QSpinBox*>(QStringLiteral("transformXSpin"));
      auto* y = dialog->findChild<QSpinBox*>(QStringLiteral("transformYSpin"));
      auto* width = dialog->findChild<QSpinBox*>(QStringLiteral("transformWidthSpin"));
      auto* height = dialog->findChild<QSpinBox*>(QStringLiteral("transformHeightSpin"));
      CHECK(x != nullptr);
      CHECK(y != nullptr);
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      x->setValue(x_value);
      y->setValue(y_value);
      width->setValue(width_value);
      height->setValue(height_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_transform_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_hue_saturation_dialog(int hue_value, int saturation_value, int lightness_value) {
  QTimer::singleShot(0, [hue_value, saturation_value, lightness_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyHueSaturationDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* hue = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationHueSpin"));
      auto* saturation = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationSaturationSpin"));
      auto* lightness = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationLightnessSpin"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("hueSaturationPreviewCheck"));
      CHECK(hue != nullptr);
      CHECK(saturation != nullptr);
      CHECK(lightness != nullptr);
      CHECK(preview != nullptr);
      CHECK(preview->isChecked());
      hue->setValue(hue_value);
      saturation->setValue(saturation_value);
      lightness->setValue(lightness_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_hue_saturation_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_levels_dialog(int black_value, int white_value, int gamma_value) {
  QTimer::singleShot(0, [black_value, white_value, gamma_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyLevelsDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* black = dialog->findChild<QSpinBox*>(QStringLiteral("levelsBlackInputSpin"));
      auto* white = dialog->findChild<QSpinBox*>(QStringLiteral("levelsWhiteInputSpin"));
      auto* gamma = dialog->findChild<QSpinBox*>(QStringLiteral("levelsGammaSpin"));
      auto* black_output = dialog->findChild<QSpinBox*>(QStringLiteral("levelsBlackOutputSpin"));
      auto* white_output = dialog->findChild<QSpinBox*>(QStringLiteral("levelsWhiteOutputSpin"));
      auto* input_graph = dialog->findChild<QWidget*>(QStringLiteral("levelsInputGraph"));
      auto* output_range = dialog->findChild<QWidget*>(QStringLiteral("levelsOutputRange"));
      auto* channel = dialog->findChild<QComboBox*>(QStringLiteral("levelsChannelCombo"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("levelsPreviewCheck"));
      CHECK(black != nullptr);
      CHECK(white != nullptr);
      CHECK(gamma != nullptr);
      CHECK(black_output != nullptr);
      CHECK(white_output != nullptr);
      CHECK(input_graph != nullptr);
      CHECK(output_range != nullptr);
      CHECK(channel != nullptr);
      CHECK(preview != nullptr);
      CHECK(preview->isChecked());
      CHECK(channel->count() == 4);
      CHECK(channel->itemText(0) == QStringLiteral("RGB"));
      CHECK(channel->itemText(1) == QStringLiteral("Red"));
      CHECK(channel->itemText(2) == QStringLiteral("Green"));
      CHECK(channel->itemText(3) == QStringLiteral("Blue"));
      black->setValue(black_value);
      white->setValue(white_value);
      gamma->setValue(gamma_value);
      CHECK(black_output->value() == 0);
      CHECK(white_output->value() == 255);
      widget->grab().save(QStringLiteral("test-artifacts/ui_levels_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_curves_dialog(int shadow_value, int midtone_value, int highlight_value) {
  QTimer::singleShot(0, [shadow_value, midtone_value, highlight_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyCurvesDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* shadow = dialog->findChild<QSpinBox*>(QStringLiteral("curvesShadowOutputSpin"));
      auto* midtone = dialog->findChild<QSpinBox*>(QStringLiteral("curvesMidtoneOutputSpin"));
      auto* highlight = dialog->findChild<QSpinBox*>(QStringLiteral("curvesHighlightOutputSpin"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("curvesPreviewCheck"));
      CHECK(shadow != nullptr);
      CHECK(midtone != nullptr);
      CHECK(highlight != nullptr);
      CHECK(preview != nullptr);
      CHECK(preview->isChecked());
      shadow->setValue(shadow_value);
      midtone->setValue(midtone_value);
      highlight->setValue(highlight_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_curves_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_color_balance_dialog(int cyan_red_value, int magenta_green_value, int yellow_blue_value) {
  QTimer::singleShot(0, [cyan_red_value, magenta_green_value, yellow_blue_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyColorBalanceDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* cyan_red = dialog->findChild<QSpinBox*>(QStringLiteral("colorBalanceCyanRedSpin"));
      auto* magenta_green = dialog->findChild<QSpinBox*>(QStringLiteral("colorBalanceMagentaGreenSpin"));
      auto* yellow_blue = dialog->findChild<QSpinBox*>(QStringLiteral("colorBalanceYellowBlueSpin"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("colorBalancePreviewCheck"));
      CHECK(cyan_red != nullptr);
      CHECK(magenta_green != nullptr);
      CHECK(yellow_blue != nullptr);
      CHECK(preview != nullptr);
      CHECK(preview->isChecked());
      cyan_red->setValue(cyan_red_value);
      magenta_green->setValue(magenta_green_value);
      yellow_blue->setValue(yellow_blue_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_color_balance_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_filter_dialog(std::vector<std::pair<QString, int>> spin_values = {}) {
  QTimer::singleShot(0, [spin_values = std::move(spin_values)] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyFilterDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      CHECK(dialog->findChild<QCheckBox*>(QStringLiteral("filterPreviewCheck")) != nullptr);
      for (const auto& [object_name, value] : spin_values) {
        auto* spin = dialog->findChild<QSpinBox*>(object_name);
        CHECK(spin != nullptr);
        spin->setValue(value);
      }
      QApplication::processEvents();
      dialog->accept();
      return;
    }
    CHECK(false);
  });
}

void ui_closing_last_document_leaves_empty_workspace() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(tabs != nullptr);
  CHECK(info != nullptr);
  CHECK(layer_list != nullptr);
  CHECK(foreground != nullptr);
  CHECK(tabs->count() == 1);

  CHECK(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0)));
  QApplication::processEvents();

  CHECK(tabs->count() == 0);
  CHECK(info->text() == QStringLiteral("No document"));
  CHECK(layer_list->count() == 0);
  CHECK(!layer_list->isEnabled());
  CHECK(!foreground->isEnabled());
  CHECK(!require_action(window, "fileSaveAction")->isEnabled());
  CHECK(!require_action(window, "layerNewAction")->isEnabled());
  CHECK(!require_action(window, "brushLargerAction")->isEnabled());
  CHECK(require_action(window, "fileNewAction")->isEnabled());
  CHECK(require_action(window, "fileOpenAction")->isEnabled());

  accept_new_document_dialog(320, 180);
  require_action(window, "fileNewAction")->trigger();
  QApplication::processEvents();

  CHECK(tabs->count() == 1);
  CHECK(info->text().contains(QStringLiteral("320 x 180 px")));
  CHECK(layer_list->isEnabled());
  CHECK(foreground->isEnabled());
  CHECK(require_action(window, "fileSaveAction")->isEnabled());
  auto* canvas = require_canvas(window);
  auto* brush_preset = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(brush_preset != nullptr);
  CHECK(brush_preset->currentData().toString() == QStringLiteral("ink"));
  CHECK(canvas->brush_size() == 12);
  CHECK(canvas->brush_opacity() == 92);
  CHECK(canvas->brush_softness() == 20);
}

void ui_document_tab_context_menu_closes_tabs_and_file_menu_closes_all() {
  const auto make_document = [](QColor color) {
    patchy::Document document(32, 24, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_pixels(32, 24, patchy::PixelFormat::rgb8(), color));
    return document;
  };

  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  auto* tab_bar = tabs->findChild<QTabBar*>();
  CHECK(tab_bar != nullptr);
  CHECK(tab_bar->contextMenuPolicy() == Qt::CustomContextMenu);

  window.add_document_session(make_document(QColor(50, 80, 140)), QStringLiteral("Zulu"));
  window.add_document_session(make_document(QColor(140, 80, 50)), QStringLiteral("Alpha"));
  QApplication::processEvents();
  CHECK(tabs->count() == 3);

  CHECK(window.findChild<QAction*>(QStringLiteral("windowArrangeWindowsAction")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("windowCloseAllWindowsAction")) == nullptr);

  const auto target_index = tabs->indexOf(tabs->widget(1));
  CHECK(target_index == 1);
  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("documentTabContextMenu")) {
        continue;
      }
      auto* close_action = find_menu_action_by_text(*menu, QStringLiteral("Close"));
      auto* close_others_action = find_menu_action_by_text(*menu, QStringLiteral("Close Others"));
      auto* close_all_action = find_menu_action_by_text(*menu, QStringLiteral("Close All"));
      CHECK(close_action != nullptr);
      CHECK(close_others_action != nullptr);
      CHECK(close_all_action != nullptr);
      CHECK(close_action->objectName() == QStringLiteral("documentTabCloseAction"));
      CHECK(close_others_action->objectName() == QStringLiteral("documentTabCloseOthersAction"));
      CHECK(close_all_action->objectName() == QStringLiteral("documentTabCloseAllAction"));
      CHECK(close_others_action->isEnabled());
      close_others_action->trigger();
      menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });

  const auto context_point = tab_bar->tabRect(target_index).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point, tab_bar->mapToGlobal(context_point));
  QApplication::sendEvent(tab_bar, &context_event);
  QApplication::processEvents();

  CHECK(saw_context_menu);
  CHECK(tabs->count() == 1);
  CHECK(tabs->tabText(0) == QStringLiteral("Zulu"));
  CHECK(require_action(window, "fileCloseAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_W));
  auto* file_close_all_action = require_action(window, "fileCloseAllAction");
  CHECK(file_close_all_action->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
  file_close_all_action->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 0);
  CHECK(!file_close_all_action->isEnabled());
}

void ui_new_document_and_canvas_size_dialogs_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  CHECK(tabs->count() == 1);

  accept_new_document_dialog(640, 360);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 2);
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(info != nullptr);
  CHECK(info->text().contains(QStringLiteral("640 x 360 px")));
  save_widget_artifact("ui_new_document_result", window);

  tabs->setCurrentIndex(0);
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("1024 x 768 px")));
  tabs->setCurrentIndex(1);
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("640 x 360 px")));
  save_widget_artifact("ui_multiple_documents", window);

  accept_image_size_dialog(800, 450);
  require_action(window, "imageSizeAction")->trigger();
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("800 x 450 px")));
  CHECK(info->text().contains(QStringLiteral("300 ppi")));
  save_widget_artifact("ui_image_size_result", window);

  accept_image_size_resolution_dialog(144);
  require_action(window, "imageSizeAction")->trigger();
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("800 x 450 px")));
  CHECK(info->text().contains(QStringLiteral("144 ppi")));

  accept_canvas_size_dialog(720, 405);
  require_action(window, "imageCanvasSizeAction")->trigger();
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("720 x 405 px")));
  save_widget_artifact("ui_canvas_size_result", window);
}

void ui_new_document_presets_and_clipboard_work() {
  QApplication::clipboard()->clear();

  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(tabs != nullptr);
  CHECK(info != nullptr);
  CHECK(layer_list != nullptr);
  CHECK(tabs->count() == 1);

  inspect_new_document_dialog_without_clipboard();
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 1);

  QImage clipboard_image(123, 45, QImage::Format_RGBA8888);
  clipboard_image.fill(QColor(30, 160, 220, 180));
  QApplication::clipboard()->setImage(clipboard_image);
  QApplication::processEvents();

  accept_clipboard_new_document_dialog(clipboard_image.size());
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();

  CHECK(tabs->count() == 2);
  CHECK(info->text().contains(QStringLiteral("123 x 45 px")));
  CHECK(layer_list->count() == 1);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Clipboard Image")) != nullptr);
  auto* canvas = require_canvas(window);
  const auto pasted_rect = canvas->active_layer_document_rect();
  CHECK(pasted_rect.has_value());
  CHECK(pasted_rect->topLeft() == QPoint(0, 0));
  CHECK(pasted_rect->size() == clipboard_image.size());
  QApplication::clipboard()->clear();
}

void ui_new_document_background_starts_locked() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(tabs != nullptr);
  CHECK(layer_list != nullptr);

  const auto require_locked_background = [layer_list] {
    auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
    auto* background_row = layer_list->itemWidget(background_item);
    CHECK(background_row != nullptr);
    auto* lock = background_row->findChild<QToolButton*>(QStringLiteral("layerLockCheck"));
    CHECK(lock != nullptr);
    CHECK(lock->isChecked());
    CHECK(lock->toolTip().contains(QStringLiteral("unlock")));
    CHECK(require_layer_item(*layer_list, QStringLiteral("Paint Layer"))->isSelected());
  };

  require_locked_background();

  accept_new_document_dialog(360, 240);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 2);
  require_locked_background();
}

void ui_first_tab_still_draws_after_second_tab_created() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);

  accept_new_document_dialog(360, 240);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 2);

  tabs->setCurrentIndex(0);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(30, 190, 255));
  drag(*canvas, QPoint(80, 80), QPoint(200, 120));
  QApplication::processEvents();

  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  CHECK(color_close(QColor(image.pixel(140, 100)), QColor(30, 190, 255), 80));
  save_widget_artifact("ui_first_tab_draw_after_second_tab", *canvas);
}

void ui_tab_switch_layers_follow_the_canvas_after_tab_reorder() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(tabs != nullptr);
  CHECK(layer_list != nullptr);

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  auto* first_canvas = tabs->currentWidget();
  CHECK(!top_level_widget_exists(QStringLiteral("patchyNewLayerDialog")));
  CHECK(layer_list->count() == 3);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Layer 3")) != nullptr);

  accept_new_document_dialog(360, 240);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  auto* second_canvas = tabs->currentWidget();
  CHECK(layer_list->count() == 2);

  auto* tab_bar = tabs->findChild<QTabBar*>();
  CHECK(tab_bar != nullptr);
  tab_bar->moveTab(tabs->indexOf(first_canvas), tabs->count() - 1);
  QApplication::processEvents();

  tabs->setCurrentWidget(first_canvas);
  QApplication::processEvents();
  CHECK(layer_list->count() == 3);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Layer 3")) != nullptr);

  tabs->setCurrentWidget(second_canvas);
  QApplication::processEvents();
  CHECK(layer_list->count() == 2);
  save_widget_artifact("ui_tab_session_layers", window);
}

void ui_new_layer_defaults_and_multiselect_layers_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->selectionMode() == QAbstractItemView::ExtendedSelection);

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  CHECK(!top_level_widget_exists(QStringLiteral("patchyNewLayerDialog")));
  CHECK(layer_list->count() == 3);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Layer 3"));
  save_widget_artifact("ui_new_layer_result", window);

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 4);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Layer 4"));

  layer_list->clearSelection();
  layer_list->item(0)->setSelected(true);
  layer_list->item(1)->setSelected(true);
  CHECK(layer_list->selectedItems().size() == 2);
  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 3);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Layer 3"));
  save_widget_artifact("ui_multiselect_merge_down", window);

  layer_list->clearSelection();
  layer_list->item(0)->setSelected(true);
  layer_list->item(1)->setSelected(true);
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 5);

  layer_list->clearSelection();
  layer_list->item(0)->setSelected(true);
  layer_list->item(1)->setSelected(true);
  require_action(window, "layerDeleteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 3);
  save_widget_artifact("ui_multiselect_duplicate_delete", window);
}

void ui_merge_down_repeatedly_collapses_to_one_layer() {
  patchy::Document document(48, 36, patchy::PixelFormat::rgba8());
  auto bottom_pixels = solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(bottom_pixels, QRect(4, 4, 12, 12), QColor(40, 90, 220, 255));
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Bottom", std::move(bottom_pixels)));

  auto middle_pixels = solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(middle_pixels, QRect(16, 8, 12, 12), QColor(40, 180, 80, 192));
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Middle", std::move(middle_pixels)));

  auto top_pixels = solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(top_pixels, QRect(28, 12, 12, 12), QColor(230, 50, 50, 160));
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Top", std::move(top_pixels)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Merge Down Repeated"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->count() == 3);

  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 2);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Middle"));

  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 1);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Bottom"));
}

void ui_merge_down_preserves_transparent_pixels() {
  patchy::Document document(32, 24, patchy::PixelFormat::rgba8());
  auto lower_pixels = solid_pixels(32, 24, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(lower_pixels, QRect(6, 6, 8, 8), QColor(40, 90, 220, 128));
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Lower", std::move(lower_pixels)));

  auto upper_pixels = solid_pixels(32, 24, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(upper_pixels, QRect(12, 8, 8, 8), QColor(230, 40, 40, 128));
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Upper", std::move(upper_pixels)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Merge Down Alpha"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 1);

  QApplication::clipboard()->clear();
  require_action(window, "editSelectAllAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();
  const auto copied = QApplication::clipboard()->image().convertToFormat(QImage::Format_RGBA8888);
  CHECK(!copied.isNull());
  CHECK(copied.pixelColor(0, 0).alpha() == 0);
  CHECK(copied.pixelColor(8, 8).alpha() > 0);
  CHECK(copied.pixelColor(14, 10).alpha() > 0);
}

void ui_merge_down_rasterizes_text_target() {
  patchy::Document document(160, 96, patchy::PixelFormat::rgba8());
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Lower", patchy::LayerKind::Text);
  text_layer.set_bounds(patchy::Rect{18, 22, 120, 42});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Lower";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "30";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#2040ff";
  document.add_layer(std::move(text_layer));

  auto paint_pixels = solid_pixels(160, 96, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(paint_pixels, QRect(52, 44, 40, 18), QColor(230, 40, 40, 160));
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Paint", std::move(paint_pixels)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Merge Down Text"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 1);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Text: Lower"));

  auto* row = layer_list->itemWidget(layer_list->item(0));
  CHECK(row != nullptr);
  auto* thumbnail = row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(thumbnail != nullptr);
  CHECK(thumbnail->toolTip() == QStringLiteral("Layer thumbnail"));

  auto* layer_info = window.findChild<QLabel*>(QStringLiteral("activeLayerInfoLabel"));
  auto* text_info = window.findChild<QLabel*>(QStringLiteral("activeLayerTextLabel"));
  CHECK(layer_info != nullptr);
  CHECK(text_info != nullptr);
  CHECK(layer_info->text().contains(QStringLiteral("Pixel Layer")));
  CHECK(text_info->isHidden() || text_info->text().isEmpty());
}

void ui_new_layer_button_inserts_above_selected_layer() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* new_button = window.findChild<QPushButton*>(QStringLiteral("layerNewButton"));
  CHECK(layer_list != nullptr);
  CHECK(new_button != nullptr);

  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->setCurrentItem(background_item, QItemSelectionModel::ClearAndSelect);
  new_button->click();
  QApplication::processEvents();

  CHECK(layer_list->count() == 3);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Paint Layer"));
  CHECK(layer_list->item(1)->text() == QStringLiteral("Layer 3"));
  CHECK(layer_list->item(2)->text() == QStringLiteral("Background"));

  auto* paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->setCurrentItem(background_item, QItemSelectionModel::ClearAndSelect);
  paint_item->setSelected(true);
  CHECK(layer_list->selectedItems().size() == 2);
  new_button->click();
  QApplication::processEvents();

  CHECK(layer_list->count() == 4);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Layer 4"));
  CHECK(layer_list->item(1)->text() == QStringLiteral("Paint Layer"));
  CHECK(layer_list->item(2)->text() == QStringLiteral("Layer 3"));
  CHECK(layer_list->item(3)->text() == QStringLiteral("Background"));
}

void ui_document_default_layer_selection_skips_folder() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer folder(document.allocate_layer_id(), "All Layers", patchy::LayerKind::Group);
  const auto folder_id = folder.id();
  patchy::Layer child(document.allocate_layer_id(), "Paint",
                      solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(40, 120, 220)));
  const auto child_id = child.id();
  folder.add_child(std::move(child));
  document.add_layer(std::move(folder));
  CHECK(document.active_layer_id().value() == folder_id);
  const auto default_layer_id = patchy::default_non_group_layer_id(document.layers());
  CHECK(default_layer_id.has_value());
  CHECK(*default_layer_id == child_id);
  document.set_active_layer(*default_layer_id);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Opened With Default Selection"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* active_layer_info = window.findChild<QLabel*>(QStringLiteral("activeLayerInfoLabel"));
  CHECK(layer_list != nullptr);
  CHECK(active_layer_info != nullptr);
  CHECK(layer_list->count() == 3);
  auto* child_item = require_layer_item(*layer_list, QStringLiteral("Paint"));
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("All Layers"));
  CHECK(layer_list->currentItem() == child_item);
  CHECK(layer_list->selectedItems().size() == 1);
  CHECK(child_item->isSelected());
  CHECK(!folder_item->isSelected());
  CHECK(static_cast<patchy::LayerId>(child_item->data(patchy::ui::kLayerIdRole).toULongLong()) == child_id);
  CHECK(active_layer_info->text().contains(QStringLiteral("Paint")));
  CHECK(!active_layer_info->text().contains(QStringLiteral("All Layers")));
}

void ui_document_with_only_folders_opens_with_no_layer_selected() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  patchy::Layer folder(document.allocate_layer_id(), "All Layers", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Nested Folder", patchy::LayerKind::Group));
  document.add_layer(std::move(folder));
  CHECK(!patchy::default_non_group_layer_id(document.layers()).has_value());
  document.clear_active_layer();

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Opened Without Selection"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* active_layer_info = window.findChild<QLabel*>(QStringLiteral("activeLayerInfoLabel"));
  CHECK(layer_list != nullptr);
  CHECK(active_layer_info != nullptr);
  CHECK(layer_list->count() == 2);
  CHECK(layer_list->currentItem() == nullptr);
  CHECK(layer_list->selectedItems().empty());
  CHECK(active_layer_info->text().contains(QStringLiteral("No active layer")));
}

void ui_duplicate_layer_copies_text_and_folder_trees() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Title", patchy::LayerKind::Text);
  text_layer.metadata()[patchy::kLayerMetadataText] = "Title";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "32";
  document.add_layer(std::move(text_layer));

  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Nested Paint",
                                    solid_pixels(16, 16, patchy::PixelFormat::rgba8(), QColor(20, 100, 220))));
  patchy::Layer nested_folder(document.allocate_layer_id(), "Nested Folder", patchy::LayerKind::Group);
  nested_folder.add_child(patchy::Layer(document.allocate_layer_id(), "Deep Paint",
                                           solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(220, 80, 30))));
  folder.add_child(std::move(nested_folder));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Duplicate Trees"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  auto* text_item = require_layer_item(*layer_list, QStringLiteral("Text: Title"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(text_item);
  text_item->setSelected(true);
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();
  CHECK(require_layer_item(*layer_list, QStringLiteral("Text: Title copy")) != nullptr);

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(folder_item);
  folder_item->setSelected(true);
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();

  auto* folder_copy = require_layer_item(*layer_list, QStringLiteral("Folder copy"));
  const auto copy_row = layer_list->row(folder_copy);
  CHECK(copy_row == 0);
  CHECK(folder_copy->data(Qt::UserRole + 2).toBool());
  CHECK(layer_list->item(copy_row + 1)->text() == QStringLiteral("Nested Folder"));
  CHECK(layer_list->item(copy_row + 1)->data(Qt::UserRole + 1).toInt() == 1);
  CHECK(layer_list->item(copy_row + 2)->text() == QStringLiteral("Deep Paint"));
  CHECK(layer_list->item(copy_row + 2)->data(Qt::UserRole + 1).toInt() == 2);
  CHECK(layer_list->item(copy_row + 3)->text() == QStringLiteral("Nested Paint"));
  CHECK(layer_list->item(copy_row + 3)->data(Qt::UserRole + 1).toInt() == 1);
  CHECK(layer_list->count() == 11);
  save_widget_artifact("ui_duplicate_text_folder_tree", window);
}

void ui_copy_paste_layer_panel_copies_layers_and_folder_trees() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Title", patchy::LayerKind::Text);
  text_layer.metadata()[patchy::kLayerMetadataText] = "Title";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "32";
  document.add_layer(std::move(text_layer));

  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Nested Paint",
                                    solid_pixels(16, 16, patchy::PixelFormat::rgba8(), QColor(20, 100, 220))));
  patchy::Layer nested_folder(document.allocate_layer_id(), "Nested Folder", patchy::LayerKind::Group);
  nested_folder.add_child(patchy::Layer(document.allocate_layer_id(), "Deep Paint",
                                           solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(220, 80, 30))));
  folder.add_child(std::move(nested_folder));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Copy Paste Trees"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  require_action(window, "editSelectAllAction")->trigger();
  QApplication::processEvents();

  auto* text_item = require_layer_item(*layer_list, QStringLiteral("Text: Title"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(text_item);
  text_item->setSelected(true);
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->item(0)->text() == QStringLiteral("Text: Title copy"));

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(folder_item);
  folder_item->setSelected(true);
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();

  auto* folder_copy = require_layer_item(*layer_list, QStringLiteral("Folder copy"));
  const auto copy_row = layer_list->row(folder_copy);
  CHECK(copy_row == 0);
  CHECK(layer_list->item(copy_row + 1)->text() == QStringLiteral("Nested Folder"));
  CHECK(layer_list->item(copy_row + 1)->data(Qt::UserRole + 1).toInt() == 1);
  CHECK(layer_list->item(copy_row + 2)->text() == QStringLiteral("Deep Paint"));
  CHECK(layer_list->item(copy_row + 2)->data(Qt::UserRole + 1).toInt() == 2);
  CHECK(layer_list->item(copy_row + 3)->text() == QStringLiteral("Nested Paint"));
  CHECK(layer_list->item(copy_row + 3)->data(Qt::UserRole + 1).toInt() == 1);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background_item);
  background_item->setSelected(true);
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->item(0)->text() == QStringLiteral("Background copy"));
  CHECK(layer_list->count() == 12);
  save_widget_artifact("ui_copy_paste_layer_panel_tree", window);
}

void ui_layer_rows_toggle_visibility_and_drag_reorder() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->dragDropMode() == QAbstractItemView::InternalMove);
  CHECK(!layer_list->dragDropOverwriteMode());
  CHECK(layer_list->item(0)->checkState() == Qt::Checked);

  canvas->set_primary_color(QColor(240, 30, 30));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  canvas->set_primary_color(QColor(20, 100, 255));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->item(0)->text() == QStringLiteral("Layer 3"));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), QColor(20, 100, 255), 40));

  auto* blue_visibility = layer_list->itemWidget(layer_list->item(0))->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(blue_visibility != nullptr);
  CHECK(blue_visibility->text().isEmpty());
  CHECK(!blue_visibility->icon().isNull());
  blue_visibility->click();
  QApplication::processEvents();
  CHECK(layer_list->item(0)->checkState() == Qt::Unchecked);
  CHECK(blue_visibility->text().isEmpty());
  CHECK(!blue_visibility->icon().isNull());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), QColor(240, 30, 30), 40));

  blue_visibility = layer_list->itemWidget(layer_list->item(0))->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(blue_visibility != nullptr);
  blue_visibility->click();
  QApplication::processEvents();
  CHECK(layer_list->item(0)->checkState() == Qt::Checked);
  CHECK(blue_visibility->text().isEmpty());
  CHECK(!blue_visibility->icon().isNull());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), QColor(20, 100, 255), 40));

  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  auto* blue_item = require_layer_item(*layer_list, QStringLiteral("Layer 3"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background_item);
  background_item->setSelected(true);
  QApplication::processEvents();
  auto* blue_name = layer_list->itemWidget(blue_item)->findChild<QLabel*>(QStringLiteral("layerRowName"));
  CHECK(blue_name != nullptr);
  send_mouse(*blue_name, QEvent::MouseButtonPress, blue_name->rect().center(), Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*blue_name, QEvent::MouseButtonRelease, blue_name->rect().center(), Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  CHECK(background_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(!canvas->has_selection());

  send_mouse(*blue_name, QEvent::MouseButtonPress, blue_name->rect().center(), Qt::LeftButton, Qt::LeftButton);
  CHECK(background_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(layer_list->selectedItems().size() == 2);
  send_mouse(*blue_name, QEvent::MouseButtonRelease, blue_name->rect().center(), Qt::LeftButton, Qt::NoButton);
  CHECK(!background_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(layer_list->selectedItems().size() == 1);

  auto* paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  auto* background_name = layer_list->itemWidget(background_item)->findChild<QLabel*>(QStringLiteral("layerRowName"));
  CHECK(background_name != nullptr);
  send_mouse(*background_name, QEvent::MouseButtonPress, background_name->rect().center(), Qt::LeftButton,
             Qt::LeftButton);
  send_mouse(*background_name, QEvent::MouseButtonRelease, background_name->rect().center(), Qt::LeftButton,
             Qt::NoButton);
  CHECK(background_item->isSelected());
  CHECK(layer_list->selectedItems().size() == 1);

  send_mouse(*blue_name, QEvent::MouseButtonPress, blue_name->rect().center(), Qt::LeftButton, Qt::LeftButton,
             Qt::ShiftModifier);
  send_mouse(*blue_name, QEvent::MouseButtonRelease, blue_name->rect().center(), Qt::LeftButton, Qt::NoButton,
             Qt::ShiftModifier);
  CHECK(background_item->isSelected());
  CHECK(paint_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(layer_list->selectedItems().size() == 3);
  CHECK(layer_list->currentItem() == blue_item);

  canvas->clear_selection();
  const auto blue_was_checked = blue_item->checkState();
  blue_visibility = layer_list->itemWidget(blue_item)->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(blue_visibility != nullptr);
  send_mouse(*blue_visibility, QEvent::MouseButtonPress, blue_visibility->rect().center(), Qt::LeftButton,
             Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*blue_visibility, QEvent::MouseButtonRelease, blue_visibility->rect().center(), Qt::LeftButton,
             Qt::NoButton, Qt::ControlModifier);
  CHECK(!canvas->has_selection());
  CHECK(blue_item->checkState() == blue_was_checked);

  auto* blue_thumbnail = layer_list->itemWidget(blue_item)->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(blue_thumbnail != nullptr);
  send_mouse(*blue_thumbnail, QEvent::MouseButtonPress, blue_thumbnail->rect().center(), Qt::LeftButton,
             Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*blue_thumbnail, QEvent::MouseButtonRelease, blue_thumbnail->rect().center(), Qt::LeftButton,
             Qt::NoButton, Qt::ControlModifier);
  CHECK(canvas->has_selection());
  CHECK(blue_item->checkState() == blue_was_checked);

  CHECK(layer_list->model()->moveRow(QModelIndex(), 0, QModelIndex(), layer_list->count()));
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(layer_list->count() == 3);
  CHECK(layer_list->item(layer_list->count() - 1)->text() == QStringLiteral("Layer 3"));
  QStringList names_after_drop;
  for (int row = 0; row < layer_list->count(); ++row) {
    names_after_drop << layer_list->item(row)->text();
  }
  CHECK(names_after_drop.contains(QStringLiteral("Background")));
  CHECK(names_after_drop.contains(QStringLiteral("Paint Layer")));
  CHECK(names_after_drop.contains(QStringLiteral("Layer 3")));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), QColor(240, 30, 30), 40));
  save_widget_artifact("ui_layer_visibility_drag_reorder", window);
}

void ui_layer_folders_create_with_drag_drop_affordances() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(require_action(window, "layerNewFolderAction") != nullptr);
  CHECK(window.findChild<QPushButton*>(QStringLiteral("layerNewFolderButton")) != nullptr);

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  auto* selected_blue_item = require_layer_item(*layer_list, QStringLiteral("Layer 3"));
  auto* selected_paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(selected_blue_item);
  selected_blue_item->setSelected(true);
  selected_paint_item->setSelected(true);
  require_action(window, "layerNewFolderAction")->trigger();
  QApplication::processEvents();

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder 1"));
  auto* blue_item = require_layer_item(*layer_list, QStringLiteral("Layer 3"));
  auto* paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  const auto folder_row = layer_list->row(folder_item);
  CHECK(folder_row == 0);
  CHECK(folder_item->data(Qt::UserRole + 1).toInt() == 0);
  CHECK(folder_item->data(Qt::UserRole + 2).toBool());
  auto* folder_widget = layer_list->itemWidget(folder_item);
  CHECK(folder_widget != nullptr);
  auto* folder_thumbnail = folder_widget->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(folder_thumbnail != nullptr);
  CHECK(folder_thumbnail->toolTip() == QStringLiteral("Folder layer"));
  CHECK(!folder_thumbnail->pixmap(Qt::ReturnByValue).isNull());
  const auto folder_thumbnail_image = folder_thumbnail->pixmap(Qt::ReturnByValue).toImage();
  int bright_folder_outline_pixels = 0;
  int dark_folder_detail_pixels = 0;
  for (int y = 3; y < folder_thumbnail_image.height() - 3; ++y) {
    for (int x = 3; x < folder_thumbnail_image.width() - 3; ++x) {
      const auto color = folder_thumbnail_image.pixelColor(x, y);
      if (color.red() > 185 && color.green() > 135 && color.blue() < 135) {
        ++bright_folder_outline_pixels;
      }
      if (color.red() < 125 && color.green() < 105 && color.blue() < 95) {
        ++dark_folder_detail_pixels;
      }
    }
  }
  CHECK(bright_folder_outline_pixels > 40);
  CHECK(dark_folder_detail_pixels > 90);
  auto* blue_widget = layer_list->itemWidget(blue_item);
  auto* paint_widget = layer_list->itemWidget(paint_item);
  auto* background_widget = layer_list->itemWidget(background_item);
  CHECK(blue_widget != nullptr);
  CHECK(paint_widget != nullptr);
  CHECK(background_widget != nullptr);
  auto* blue_thumbnail = blue_widget->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  auto* paint_thumbnail = paint_widget->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  auto* background_thumbnail = background_widget->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(blue_thumbnail != nullptr);
  CHECK(paint_thumbnail != nullptr);
  CHECK(background_thumbnail != nullptr);
  auto thumbnail_viewport_left = [viewport = layer_list->viewport()](QWidget* widget) {
    return widget->mapTo(viewport, QPoint()).x();
  };
  const auto folder_thumbnail_left = thumbnail_viewport_left(folder_thumbnail);
  CHECK(thumbnail_viewport_left(blue_thumbnail) >= folder_thumbnail_left + 8);
  CHECK(thumbnail_viewport_left(paint_thumbnail) >= folder_thumbnail_left + 8);
  CHECK(thumbnail_viewport_left(background_thumbnail) < thumbnail_viewport_left(blue_thumbnail));
  CHECK(blue_item->data(Qt::UserRole + 1).toInt() == 1);
  CHECK(paint_item->data(Qt::UserRole + 1).toInt() == 1);
  CHECK(background_item->data(Qt::UserRole + 1).toInt() == 0);
  CHECK(layer_list->item(folder_row + 1)->text() == QStringLiteral("Layer 3"));
  CHECK(layer_list->item(folder_row + 2)->text() == QStringLiteral("Paint Layer"));
  CHECK((folder_item->flags() & Qt::ItemIsDropEnabled) != 0);
  CHECK((blue_item->flags() & Qt::ItemIsDragEnabled) != 0);
  CHECK(layer_list->dragDropMode() == QAbstractItemView::InternalMove);
  CHECK(layer_list->defaultDropAction() == Qt::MoveAction);
  save_widget_artifact("ui_layer_folder_drag_drop", window);
}

void ui_layer_panel_mixed_folder_visual_cleanup() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer folder(document.allocate_layer_id(), "Assets", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Pixel Child",
                                 solid_pixels(18, 18, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255))));

  patchy::Layer text(document.allocate_layer_id(), "Text Child", patchy::LayerKind::Text);
  text.metadata()[patchy::kLayerMetadataText] = "Title";
  text.metadata()[patchy::kLayerMetadataTextSize] = "24";
  text.metadata()[patchy::kLayerMetadataTextColor] = "#f2f6fb";
  folder.add_child(std::move(text));

  patchy::Layer adjustment(document.allocate_layer_id(), "Hue/Saturation", patchy::LayerKind::Adjustment);
  patchy::AdjustmentSettings adjustment_settings;
  adjustment_settings.kind = patchy::AdjustmentKind::HueSaturation;
  adjustment_settings.hue_saturation = patchy::HueSaturationAdjustment{18, 22, 0};
  patchy::configure_adjustment_layer(adjustment, adjustment_settings);
  folder.add_child(std::move(adjustment));

  patchy::Layer styled(document.allocate_layer_id(), "Masked Styled",
                       solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(40, 110, 230, 255)));
  const auto styled_id = styled.id();
  patchy::PixelBuffer mask_pixels(20, 20, patchy::PixelFormat::gray8());
  mask_pixels.clear(255);
  styled.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 20, 20}, std::move(mask_pixels), 0, true});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 0.75F;
  shadow.distance = 4.0F;
  shadow.size = 3.0F;
  styled.layer_style().drop_shadows.push_back(shadow);
  folder.add_child(std::move(styled));

  document.add_layer(std::move(folder));
  document.set_active_layer(styled_id);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Layer Panel Visual Cleanup"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Assets"));
  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  auto* styled_item = require_layer_item(*layer_list, QStringLiteral("Masked Styled"));
  CHECK(folder_item->data(patchy::ui::kLayerDepthRole).toInt() == 0);
  CHECK(styled_item->data(patchy::ui::kLayerDepthRole).toInt() == 1);

  auto* folder_row = layer_list->itemWidget(folder_item);
  auto* background_row = layer_list->itemWidget(background_item);
  auto* styled_row = layer_list->itemWidget(styled_item);
  CHECK(folder_row != nullptr);
  CHECK(background_row != nullptr);
  CHECK(styled_row != nullptr);
  auto* folder_name = folder_row->findChild<QLabel*>(QStringLiteral("layerRowName"));
  auto* styled_details = styled_row->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
  CHECK(folder_name != nullptr);
  CHECK(styled_details != nullptr);
  CHECK(folder_name->text() == QStringLiteral("Assets"));
  CHECK(styled_details->text().contains(QStringLiteral("fx")));
  CHECK(styled_details->text().contains(QStringLiteral("mask")));

  auto* folder_visibility = folder_row->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  auto* styled_visibility = styled_row->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  auto* folder_thumbnail = folder_row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  auto* styled_thumbnail = styled_row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  auto* background_thumbnail = background_row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(folder_visibility != nullptr);
  CHECK(styled_visibility != nullptr);
  CHECK(folder_thumbnail != nullptr);
  CHECK(styled_thumbnail != nullptr);
  CHECK(background_thumbnail != nullptr);

  auto widget_left = [viewport = layer_list->viewport()](QWidget* widget) {
    return widget->mapTo(viewport, QPoint()).x();
  };
  CHECK(std::abs(widget_left(folder_visibility) - widget_left(styled_visibility)) <= 1);
  CHECK(widget_left(styled_thumbnail) >= widget_left(folder_thumbnail) + 8);
  CHECK(widget_left(background_thumbnail) < widget_left(styled_thumbnail));
  save_widget_artifact("ui_layer_panel_mixed_folder_visual_cleanup", window);
}

void ui_layer_drag_drops_child_above_parent_folder() {
  patchy::Document document(80, 60, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(245, 245, 245)));
  patchy::Layer folder(document.allocate_layer_id(), "Folder 1", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Paint Layer",
                                 solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(40, 90, 220))));
  patchy::Layer levels(document.allocate_layer_id(), "Levels", patchy::LayerKind::Adjustment);
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Levels;
  settings.levels = patchy::LevelsAdjustment{20, 230, 110};
  patchy::configure_adjustment_layer(levels, settings);
  folder.add_child(std::move(levels));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Child Above Folder"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* levels_item = require_layer_item(*layer_list, QStringLiteral("Levels"));
  CHECK(levels_item->data(patchy::ui::kLayerDepthRole).toInt() == 1);
  const auto levels_id = static_cast<patchy::LayerId>(levels_item->data(patchy::ui::kLayerIdRole).toULongLong());
  const auto levels_rect = layer_list->visualItemRect(levels_item);

  send_layer_drop(*layer_list, QPoint(2, levels_rect.top() + 4), {levels_id});
  process_events_for(1);

  levels_item = require_layer_item(*layer_list, QStringLiteral("Levels"));
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder 1"));
  auto* paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  CHECK(layer_list->row(levels_item) == 0);
  CHECK(layer_list->row(folder_item) == 1);
  CHECK(levels_item->data(patchy::ui::kLayerDepthRole).toInt() == 0);
  CHECK(folder_item->data(patchy::ui::kLayerDepthRole).toInt() == 0);
  CHECK(paint_item->data(patchy::ui::kLayerDepthRole).toInt() == 1);
}

void ui_layer_drag_multiselect_drops_children_above_parent_folder() {
  patchy::Document document(80, 60, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(245, 245, 245)));
  patchy::Layer folder(document.allocate_layer_id(), "Folder 1", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Paint Layer",
                                 solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(40, 90, 220))));
  patchy::Layer levels(document.allocate_layer_id(), "Levels", patchy::LayerKind::Adjustment);
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Levels;
  patchy::configure_adjustment_layer(levels, settings);
  folder.add_child(std::move(levels));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Multi Child Above Folder"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* levels_item = require_layer_item(*layer_list, QStringLiteral("Levels"));
  auto* paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  const auto levels_id = static_cast<patchy::LayerId>(levels_item->data(patchy::ui::kLayerIdRole).toULongLong());
  const auto paint_id = static_cast<patchy::LayerId>(paint_item->data(patchy::ui::kLayerIdRole).toULongLong());
  const auto levels_rect = layer_list->visualItemRect(levels_item);

  send_layer_drop(*layer_list, QPoint(2, levels_rect.top() + 4), {levels_id, paint_id});
  process_events_for(1);

  levels_item = require_layer_item(*layer_list, QStringLiteral("Levels"));
  paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder 1"));
  CHECK(layer_list->row(levels_item) == 0);
  CHECK(layer_list->row(paint_item) == 1);
  CHECK(layer_list->row(folder_item) == 2);
  CHECK(levels_item->data(patchy::ui::kLayerDepthRole).toInt() == 0);
  CHECK(paint_item->data(patchy::ui::kLayerDepthRole).toInt() == 0);
  CHECK(folder_item->data(patchy::ui::kLayerDepthRole).toInt() == 0);
}

void ui_layer_drag_folder_header_crack_drops_inside_folder() {
  patchy::Document document(80, 60, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(245, 245, 245)));
  patchy::Layer folder(document.allocate_layer_id(), "Folder 1", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Paint Layer",
                                 solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(40, 90, 220))));
  document.add_layer(std::move(folder));
  patchy::Layer hue_saturation(document.allocate_layer_id(), "Hue/Saturation", patchy::LayerKind::Adjustment);
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::HueSaturation;
  settings.hue_saturation = patchy::HueSaturationAdjustment{25, 20, 0};
  patchy::configure_adjustment_layer(hue_saturation, settings);
  document.add_layer(std::move(hue_saturation));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Folder Crack Drop"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* hue_item = require_layer_item(*layer_list, QStringLiteral("Hue/Saturation"));
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder 1"));
  const auto hue_id = static_cast<patchy::LayerId>(hue_item->data(patchy::ui::kLayerIdRole).toULongLong());
  const auto folder_rect = layer_list->visualItemRect(folder_item);

  send_layer_drop(*layer_list, QPoint(folder_rect.center().x(), folder_rect.bottom() - 2), {hue_id});
  process_events_for(1);

  folder_item = require_layer_item(*layer_list, QStringLiteral("Folder 1"));
  hue_item = require_layer_item(*layer_list, QStringLiteral("Hue/Saturation"));
  auto* paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  CHECK(layer_list->row(folder_item) == 0);
  CHECK(layer_list->row(hue_item) == 1);
  CHECK(layer_list->row(paint_item) == 2);
  CHECK(hue_item->data(patchy::ui::kLayerDepthRole).toInt() == 1);
  CHECK(paint_item->data(patchy::ui::kLayerDepthRole).toInt() == 1);
}

void ui_layer_drag_shows_insertion_and_folder_drop_previews() {
  patchy::Document document(80, 60, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(245, 245, 245)));
  patchy::Layer folder(document.allocate_layer_id(), "Folder 1", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Paint Layer",
                                 solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(40, 90, 220))));
  patchy::Layer levels(document.allocate_layer_id(), "Levels", patchy::LayerKind::Adjustment);
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Levels;
  patchy::configure_adjustment_layer(levels, settings);
  folder.add_child(std::move(levels));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Layer Drop Preview"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* levels_item = require_layer_item(*layer_list, QStringLiteral("Levels"));
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder 1"));
  const auto levels_id = static_cast<patchy::LayerId>(levels_item->data(patchy::ui::kLayerIdRole).toULongLong());
  const auto blue_pixels = [](const QImage& image) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const auto color = image.pixelColor(x, y);
        if (color.blue() > 190 && color.green() > 115 && color.red() < 95) {
          ++count;
        }
      }
    }
    return count;
  };

  send_layer_drag_move(*layer_list, layer_list->visualItemRect(folder_item).center(), {levels_id});
  process_events_for(1);
  const auto folder_preview = layer_list->viewport()->grab().toImage();
  CHECK(blue_pixels(folder_preview) > 30);

  const auto levels_rect = layer_list->visualItemRect(levels_item);
  send_layer_drag_move(*layer_list, QPoint(2, levels_rect.top() + 4), {levels_id});
  process_events_for(1);
  const auto insertion_preview = layer_list->viewport()->grab().toImage();
  CHECK(blue_pixels(insertion_preview) > 30);
  send_layer_drag_leave(*layer_list);
}

void ui_layer_list_scrolls_with_wheel_and_drag_autoscroll() {
  patchy::Document document(32, 32, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(32, 32, patchy::PixelFormat::rgba8(), QColor(245, 245, 245)));
  for (int index = 0; index < 24; ++index) {
    document.add_layer(patchy::Layer(document.allocate_layer_id(), "Scrollable " + std::to_string(index + 1),
                                     solid_pixels(6, 6, patchy::PixelFormat::rgba8(), QColor(20, 80, 180))));
  }

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Layer Scroll Drag"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* scroll = layer_list->verticalScrollBar();
  CHECK(scroll != nullptr);
  CHECK(scroll->maximum() > 0);
  scroll->setValue(0);
  QApplication::processEvents();

  auto* top_item = layer_list->item(0);
  CHECK(top_item != nullptr);
  auto* top_name = layer_list->itemWidget(top_item)->findChild<QLabel*>(QStringLiteral("layerRowName"));
  CHECK(top_name != nullptr);
  send_wheel(*top_name, top_name->rect().center(), -120);
  CHECK(scroll->value() > 0);

  scroll->setValue(0);
  QApplication::processEvents();
  qApp->installEventFilter(layer_list);
  const auto list_global_position = layer_list->viewport()->mapToGlobal(layer_list->viewport()->rect().center());
  send_wheel(window, window.mapFromGlobal(list_global_position), -120);
  qApp->removeEventFilter(layer_list);
  CHECK(scroll->value() > 0);

  scroll->setValue(0);
  QApplication::processEvents();
  const auto id = static_cast<patchy::LayerId>(top_item->data(patchy::ui::kLayerIdRole).toULongLong());
  send_layer_drag_enter(*layer_list, QPoint(layer_list->viewport()->width() / 2, layer_list->viewport()->height() - 3),
                        {id});
  send_layer_drag_move(*layer_list, QPoint(layer_list->viewport()->width() / 2, layer_list->viewport()->height() - 3),
                       {id});
  process_events_for(320);
  CHECK(scroll->value() > 0);
  const auto scroll_after_auto = scroll->value();
  send_wheel(*top_name, top_name->rect().center(), -120);
  CHECK(scroll->value() > scroll_after_auto);
  send_layer_drag_leave(*layer_list);
}

void ui_layer_new_folder_button_groups_dropped_layers() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* folder_button = window.findChild<QPushButton*>(QStringLiteral("layerNewFolderButton"));
  CHECK(layer_list != nullptr);
  CHECK(folder_button != nullptr);

  require_action(window, "layerNewAction")->trigger();
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  auto* layer3 = require_layer_item(*layer_list, QStringLiteral("Layer 3"));
  auto* layer4 = require_layer_item(*layer_list, QStringLiteral("Layer 4"));
  const std::vector<patchy::LayerId> ids{
      static_cast<patchy::LayerId>(layer4->data(patchy::ui::kLayerIdRole).toULongLong()),
      static_cast<patchy::LayerId>(layer3->data(patchy::ui::kLayerIdRole).toULongLong())};

  send_layer_button_drop(*folder_button, ids);

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder 1"));
  layer4 = require_layer_item(*layer_list, QStringLiteral("Layer 4"));
  layer3 = require_layer_item(*layer_list, QStringLiteral("Layer 3"));
  CHECK(layer_list->row(folder_item) == 0);
  CHECK(layer_list->row(layer4) == 1);
  CHECK(layer_list->row(layer3) == 2);
  CHECK(folder_item->data(patchy::ui::kLayerDepthRole).toInt() == 0);
  CHECK(layer4->data(patchy::ui::kLayerDepthRole).toInt() == 1);
  CHECK(layer3->data(patchy::ui::kLayerDepthRole).toInt() == 1);
}

void ui_layer_action_buttons_accept_multiselect_drops() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* new_button = window.findChild<QPushButton*>(QStringLiteral("layerNewButton"));
  auto* duplicate_button = window.findChild<QPushButton*>(QStringLiteral("layerDuplicateButton"));
  auto* delete_button = window.findChild<QPushButton*>(QStringLiteral("layerDeleteButton"));
  CHECK(layer_list != nullptr);
  CHECK(new_button != nullptr);
  CHECK(duplicate_button != nullptr);
  CHECK(delete_button != nullptr);

  require_action(window, "layerNewAction")->trigger();
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  auto* layer3 = require_layer_item(*layer_list, QStringLiteral("Layer 3"));
  auto* layer4 = require_layer_item(*layer_list, QStringLiteral("Layer 4"));
  const std::vector<patchy::LayerId> ids{
      static_cast<patchy::LayerId>(layer4->data(patchy::ui::kLayerIdRole).toULongLong()),
      static_cast<patchy::LayerId>(layer3->data(patchy::ui::kLayerIdRole).toULongLong())};
  const auto count_before = layer_list->count();

  send_layer_button_drop(*new_button, ids);
  CHECK(layer_list->count() == count_before + 2);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Layer 4 copy")) != nullptr);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Layer 3 copy")) != nullptr);

  send_layer_button_drop(*duplicate_button, ids);
  CHECK(layer_list->count() == count_before + 4);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Layer 4 copy 2")) != nullptr);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Layer 3 copy 2")) != nullptr);

  send_layer_button_drop(*delete_button, ids);
  CHECK(find_layer_item(*layer_list, QStringLiteral("Layer 4")) == nullptr);
  CHECK(find_layer_item(*layer_list, QStringLiteral("Layer 3")) == nullptr);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(require_layer_item(*layer_list, QStringLiteral("Layer 4")) != nullptr);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Layer 3")) != nullptr);
}

void ui_layer_folders_expand_and_contract_children() {
  patchy::Document document(32, 32, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(32, 32, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  patchy::Layer group(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Nested 1",
                                   solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(220, 40, 40))));
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Nested 2",
                                   solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(40, 80, 220))));
  document.add_layer(std::move(group));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Layer Folder Disclosure"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto find_layer_item = [layer_list](const QString& text) -> QListWidgetItem* {
    for (int row = 0; row < layer_list->count(); ++row) {
      if (layer_list->item(row)->text() == text) {
        return layer_list->item(row);
      }
    }
    return nullptr;
  };

  CHECK(layer_list->count() == 4);
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  CHECK(folder_item->data(Qt::UserRole + 3).toBool());
  CHECK(find_layer_item(QStringLiteral("Nested 1")) != nullptr);
  CHECK(find_layer_item(QStringLiteral("Nested 2")) != nullptr);
  auto* folder_widget = layer_list->itemWidget(folder_item);
  CHECK(folder_widget != nullptr);
  auto* disclosure = folder_widget->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
  CHECK(disclosure != nullptr);
  CHECK(disclosure->isChecked());
  CHECK(disclosure->arrowType() == Qt::DownArrow);

  disclosure->click();
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(layer_list->count() == 2);
  CHECK(find_layer_item(QStringLiteral("Nested 1")) == nullptr);
  CHECK(find_layer_item(QStringLiteral("Nested 2")) == nullptr);
  folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  CHECK(!folder_item->data(Qt::UserRole + 3).toBool());
  folder_widget = layer_list->itemWidget(folder_item);
  CHECK(folder_widget != nullptr);
  disclosure = folder_widget->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
  CHECK(disclosure != nullptr);
  CHECK(!disclosure->isChecked());
  CHECK(disclosure->arrowType() == Qt::RightArrow);

  disclosure->click();
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(layer_list->count() == 4);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Nested 1"))->data(Qt::UserRole + 1).toInt() == 1);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Nested 2"))->data(Qt::UserRole + 1).toInt() == 1);
  folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  CHECK(folder_item->data(Qt::UserRole + 3).toBool());
  save_widget_artifact("ui_layer_folder_expand_contract", window);
}

void ui_layer_folders_open_with_saved_expansion_state() {
  patchy::Document document(32, 32, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(32, 32, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));

  patchy::Layer closed_group(document.allocate_layer_id(), "Closed Folder", patchy::LayerKind::Group);
  closed_group.metadata()[patchy::kLayerMetadataGroupExpanded] = "false";
  closed_group.add_child(patchy::Layer(document.allocate_layer_id(), "Closed Child",
                                          solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(220, 40, 40))));
  document.add_layer(std::move(closed_group));

  patchy::Layer open_group(document.allocate_layer_id(), "Open Folder", patchy::LayerKind::Group);
  open_group.metadata()[patchy::kLayerMetadataGroupExpanded] = "true";
  open_group.add_child(patchy::Layer(document.allocate_layer_id(), "Open Child",
                                        solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(40, 80, 220))));
  document.add_layer(std::move(open_group));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Saved Folder State"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto find_layer_item = [layer_list](const QString& text) -> QListWidgetItem* {
    for (int row = 0; row < layer_list->count(); ++row) {
      if (layer_list->item(row)->text() == text) {
        return layer_list->item(row);
      }
    }
    return nullptr;
  };

  auto* open_item = require_layer_item(*layer_list, QStringLiteral("Open Folder"));
  auto* closed_item = require_layer_item(*layer_list, QStringLiteral("Closed Folder"));
  CHECK(open_item->data(Qt::UserRole + 3).toBool());
  CHECK(!closed_item->data(Qt::UserRole + 3).toBool());
  CHECK(find_layer_item(QStringLiteral("Open Child")) != nullptr);
  CHECK(find_layer_item(QStringLiteral("Closed Child")) == nullptr);

  auto* closed_widget = layer_list->itemWidget(closed_item);
  CHECK(closed_widget != nullptr);
  auto* closed_disclosure = closed_widget->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
  CHECK(closed_disclosure != nullptr);
  CHECK(closed_disclosure->arrowType() == Qt::RightArrow);
  save_widget_artifact("ui_layer_folder_saved_state", window);
}

void ui_move_auto_select_reveals_layers_in_collapsed_folders() {
  patchy::Document document(48, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(48, 48, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  patchy::Layer group(document.allocate_layer_id(), "Collapsed Folder", patchy::LayerKind::Group);
  group.metadata()[patchy::kLayerMetadataGroupExpanded] = "false";
  auto child = patchy::Layer(document.allocate_layer_id(), "Hidden Child",
                                solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(40, 80, 220)));
  const auto child_id = child.id();
  child.set_bounds(patchy::Rect{12, 12, 12, 12});
  group.add_child(std::move(child));
  document.add_layer(std::move(group));
  document.set_active_layer(child_id);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Reveal Collapsed Auto Select"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Collapsed Folder"))->data(Qt::UserRole + 3).toBool() == false);
  CHECK(find_layer_item(*layer_list, QStringLiteral("Hidden Child")) == nullptr);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  const auto click = canvas->widget_position_for_document_point(QPoint(16, 16));
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  QApplication::processEvents();

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Collapsed Folder"));
  auto* child_item = require_layer_item(*layer_list, QStringLiteral("Hidden Child"));
  CHECK(folder_item->data(Qt::UserRole + 3).toBool());
  CHECK(child_item->isSelected());
  CHECK(layer_list->currentItem() == child_item);
  CHECK(layer_list->visualItemRect(child_item).intersects(layer_list->viewport()->rect()));
  save_widget_artifact("ui_auto_select_reveals_collapsed_folder", window);
}

void ui_folder_visibility_preserves_layer_panel_scroll() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  patchy::Layer group(document.allocate_layer_id(), "Scrollable Folder", patchy::LayerKind::Group);
  for (int index = 0; index < 42; ++index) {
    auto child = patchy::Layer(
        document.allocate_layer_id(), "Child " + std::to_string(index + 1),
        solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(40 + index * 3 % 180, 80, 220, 255)));
    child.set_bounds(patchy::Rect{index % 16, index % 16, 8, 8});
    group.add_child(std::move(child));
  }
  document.add_layer(std::move(group));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Layer Panel Scroll"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  layer_list->setFixedHeight(180);
  QApplication::processEvents();
  auto* scroll = layer_list->verticalScrollBar();
  CHECK(scroll != nullptr);
  CHECK(scroll->maximum() > 0);
  scroll->setValue(scroll->maximum() / 2);
  QApplication::processEvents();
  const auto scroll_before = scroll->value();
  CHECK(scroll_before > 0);

  QListWidgetItem* folder_item = nullptr;
  for (int row = 0; row < layer_list->count(); ++row) {
    if (layer_list->item(row)->text() == QStringLiteral("Scrollable Folder")) {
      folder_item = layer_list->item(row);
      break;
    }
  }
  CHECK(folder_item != nullptr);
  auto* folder_widget = layer_list->itemWidget(folder_item);
  CHECK(folder_widget != nullptr);
  auto* visibility = folder_widget->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(visibility != nullptr);
  visibility->click();
  QApplication::processEvents();
  CHECK(std::abs(scroll->value() - scroll_before) <= 1);

  folder_item = nullptr;
  for (int row = 0; row < layer_list->count(); ++row) {
    if (layer_list->item(row)->text() == QStringLiteral("Scrollable Folder")) {
      folder_item = layer_list->item(row);
      break;
    }
  }
  CHECK(folder_item != nullptr);
  folder_widget = layer_list->itemWidget(folder_item);
  CHECK(folder_widget != nullptr);
  visibility = folder_widget->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(visibility != nullptr);
  visibility->click();
  QApplication::processEvents();
  CHECK(std::abs(scroll->value() - scroll_before) <= 1);
}

void ui_move_preview_preserves_layer_order() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(230, 20, 30));
  canvas->set_brush_size(30);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(120, 110)),
       canvas->widget_position_for_document_point(QPoint(121, 110)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 110)), QColor(230, 20, 30), 55));

  auto* background = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background);
  background->setSelected(true);
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(false);
  canvas->set_auto_select_layer(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(40, 40));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(70, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 110)), QColor(230, 20, 30), 55));
  save_widget_artifact("ui_move_preview_layer_order", window);
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(70, 0), Qt::LeftButton, Qt::NoButton);
}

void ui_move_tool_moves_selected_layers_together() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(12);
  canvas->set_primary_color(QColor(230, 30, 30));
  auto* paint_layer = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(paint_layer);
  paint_layer->setSelected(true);
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 70)),
       canvas->widget_position_for_document_point(QPoint(71, 70)));
  QApplication::processEvents();

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  canvas->set_primary_color(QColor(20, 90, 240));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(140, 70)),
       canvas->widget_position_for_document_point(QPoint(141, 70)));
  QApplication::processEvents();

  auto* blue_layer = require_layer_item(*layer_list, QStringLiteral("Layer 3"));
  paint_layer = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(blue_layer);
  blue_layer->setSelected(true);
  paint_layer->setSelected(true);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() == 2);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(false);
  canvas->set_auto_select_layer(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(100, 100));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(30, 20), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(30, 20), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(100, 90)), QColor(230, 30, 30), 70));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(170, 90)), QColor(20, 90, 240), 70));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(230, 30, 30), 70));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(140, 70)), QColor(20, 90, 240), 70));
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(blue_layer->isSelected());
  CHECK(paint_layer->isSelected());
  save_widget_artifact("ui_move_selected_layers", window);
}

void ui_move_auto_select_hover_outlines_with_multi_selection() {
  patchy::Document document(140, 100, patchy::PixelFormat::rgba8());

  patchy::Layer red(document.allocate_layer_id(), "Selected Red",
                    solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255)));
  red.set_bounds(patchy::Rect{18, 18, 12, 12});
  document.add_layer(std::move(red));

  patchy::Layer blue(document.allocate_layer_id(), "Selected Blue",
                     solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(40, 90, 220, 255)));
  blue.set_bounds(patchy::Rect{48, 18, 12, 12});
  document.add_layer(std::move(blue));

  patchy::Layer target(document.allocate_layer_id(), "Hover Target",
                       solid_pixels(16, 14, patchy::PixelFormat::rgba8(), QColor(40, 180, 90, 255)));
  target.set_bounds(patchy::Rect{80, 50, 16, 14});
  document.add_layer(std::move(target));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Multi Auto Select Hover"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  auto* blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  auto* target_item = require_layer_item(*layer_list, QStringLiteral("Hover Target"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(blue_item);
  blue_item->setSelected(true);
  red_item->setSelected(true);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(!target_item->isSelected());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(88, 57)), Qt::NoButton,
             Qt::NoButton);
  QApplication::processEvents();

  const auto image = canvas->grab().toImage();
  const QColor outline_color(95, 170, 255);
  const QRect expected_outline(canvas->widget_position_for_document_point(QPoint(80, 50)),
                               canvas->widget_position_for_document_point(QPoint(96, 64)));
  CHECK(count_pixels_close(image, expected_outline.normalized().adjusted(-2, -2, 2, 2), outline_color, 18) > 20);
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(red_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(!target_item->isSelected());
  save_widget_artifact("ui_move_auto_select_multi_hover", window);

  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(24, 24)), Qt::NoButton,
             Qt::NoButton);
  QApplication::processEvents();
  const auto selected_member_hover = canvas->grab().toImage();
  const QRect selected_member_outline(canvas->widget_position_for_document_point(QPoint(18, 18)),
                                      canvas->widget_position_for_document_point(QPoint(30, 30)));
  CHECK(count_pixels_close(selected_member_hover, selected_member_outline.normalized().adjusted(-2, -2, 2, 2),
                           outline_color, 18) > 12);
}

void ui_move_auto_select_drag_replaces_multi_selection() {
  patchy::Document document(140, 100, patchy::PixelFormat::rgba8());

  patchy::Layer red(document.allocate_layer_id(), "Selected Red",
                    solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255)));
  red.set_bounds(patchy::Rect{18, 18, 12, 12});
  document.add_layer(std::move(red));

  patchy::Layer blue(document.allocate_layer_id(), "Selected Blue",
                     solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(40, 90, 220, 255)));
  blue.set_bounds(patchy::Rect{48, 18, 12, 12});
  document.add_layer(std::move(blue));

  patchy::Layer target(document.allocate_layer_id(), "Auto Target",
                       solid_pixels(16, 14, patchy::PixelFormat::rgba8(), QColor(40, 180, 90, 255)));
  target.set_bounds(patchy::Rect{80, 50, 16, 14});
  document.add_layer(std::move(target));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Multi Auto Select Drag"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  auto* blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(blue_item);
  blue_item->setSelected(true);
  red_item->setSelected(true);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() == 2);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  canvas->set_snap_enabled(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(88, 57));
  const auto end = canvas->widget_position_for_document_point(QPoint(108, 67));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 24)), QColor(220, 40, 40), 40));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(54, 24)), QColor(40, 90, 220), 40));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(88, 57)), QColor(40, 180, 90), 40));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(108, 67)), QColor(40, 180, 90), 40));

  red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  auto* target_item = require_layer_item(*layer_list, QStringLiteral("Auto Target"));
  CHECK(layer_list->selectedItems().size() == 1);
  CHECK(!red_item->isSelected());
  CHECK(!blue_item->isSelected());
  CHECK(target_item->isSelected());
  CHECK(layer_list->currentItem() == target_item);
  save_widget_artifact("ui_move_auto_select_multi_drag", window);
}

void ui_move_auto_select_selected_member_drag_keeps_multi_selection() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());

  patchy::Layer red(document.allocate_layer_id(), "Selected Red",
                    solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255)));
  red.set_bounds(patchy::Rect{18, 18, 12, 12});
  document.add_layer(std::move(red));

  patchy::Layer blue(document.allocate_layer_id(), "Selected Blue",
                     solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(40, 90, 220, 255)));
  blue.set_bounds(patchy::Rect{48, 18, 12, 12});
  document.add_layer(std::move(blue));

  patchy::Layer target(document.allocate_layer_id(), "Unselected Target",
                       solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(40, 180, 90, 255)));
  target.set_bounds(patchy::Rect{82, 18, 12, 12});
  document.add_layer(std::move(target));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Multi Auto Select Selected Member"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  auto* blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  auto* target_item = require_layer_item(*layer_list, QStringLiteral("Unselected Target"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(blue_item);
  blue_item->setSelected(true);
  red_item->setSelected(true);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() == 2);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  canvas->set_snap_enabled(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(24, 24));
  const auto end = canvas->widget_position_for_document_point(QPoint(44, 34));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(!color_close(canvas_pixel(*canvas, QPoint(24, 24)), QColor(220, 40, 40), 40));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(54, 24)), QColor(40, 90, 220), 40));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(44, 34)), QColor(220, 40, 40), 40));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(74, 34)), QColor(40, 90, 220), 40));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(88, 24)), QColor(40, 180, 90), 40));

  red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  target_item = require_layer_item(*layer_list, QStringLiteral("Unselected Target"));
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(red_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(!target_item->isSelected());
  save_widget_artifact("ui_move_auto_select_selected_member_drag", window);
}

void ui_move_auto_select_blank_drag_keeps_multi_selection() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());

  patchy::Layer red(document.allocate_layer_id(), "Selected Red",
                    solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255)));
  red.set_bounds(patchy::Rect{18, 18, 12, 12});
  document.add_layer(std::move(red));

  patchy::Layer blue(document.allocate_layer_id(), "Selected Blue",
                     solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(40, 90, 220, 255)));
  blue.set_bounds(patchy::Rect{48, 18, 12, 12});
  document.add_layer(std::move(blue));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Multi Auto Select Blank"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  auto* blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(blue_item);
  blue_item->setSelected(true);
  red_item->setSelected(true);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() == 2);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  canvas->set_snap_enabled(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(100, 70));
  const auto end = canvas->widget_position_for_document_point(QPoint(112, 82));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 24)), QColor(220, 40, 40), 40));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(54, 24)), QColor(40, 90, 220), 40));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(36, 36)), QColor(220, 40, 40), 40));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(66, 36)), QColor(40, 90, 220), 40));

  red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(red_item->isSelected());
  CHECK(blue_item->isSelected());
}

void ui_shift_constrains_move_tool_drag_to_axis() {
  patchy::Document document(120, 100, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 100, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer layer(document.allocate_layer_id(), "Move Target",
                      solid_pixels(10, 10, patchy::PixelFormat::rgba8(), QColor(20, 90, 235)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{20, 20, 10, 10});
  document.add_layer(std::move(layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(480, 360);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();

  auto* moved = document.find_layer(layer_id);
  CHECK(moved != nullptr);

  const auto first_start = canvas.widget_position_for_document_point(QPoint(24, 24));
  const auto first_end = canvas.widget_position_for_document_point(QPoint(59, 42));
  drag(canvas, first_start, first_end, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(moved->bounds().x == 55);
  CHECK(moved->bounds().y == 20);
  CHECK(color_close(canvas_pixel(canvas, QPoint(59, 24)), QColor(20, 90, 235), 35));
  CHECK(color_close(canvas_pixel(canvas, QPoint(59, 42)), QColor(Qt::white), 12));

  const auto second_start = canvas.widget_position_for_document_point(QPoint(59, 24));
  send_mouse(canvas, QEvent::MouseButtonPress, second_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(69, 28)), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(71, 54)), Qt::NoButton,
             Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(canvas, QEvent::MouseButtonRelease, canvas.widget_position_for_document_point(QPoint(71, 54)),
             Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  QApplication::processEvents();

  CHECK(moved->bounds().x == 55);
  CHECK(moved->bounds().y == 50);
  CHECK(color_close(canvas_pixel(canvas, QPoint(59, 54)), QColor(20, 90, 235), 35));
  CHECK(color_close(canvas_pixel(canvas, QPoint(71, 54)), QColor(Qt::white), 12));
}

void ui_move_tool_uses_opaque_bounds_for_transparent_layer() {
  patchy::Document document(180, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(180, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  auto pixels = solid_pixels(180, 120, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(72, 42, 18, 12), QColor(20, 20, 20, 255));
  patchy::Layer small_layer(document.allocate_layer_id(), "Small Opaque", std::move(pixels));
  const auto small_layer_id = small_layer.id();
  small_layer.set_bounds(patchy::Rect{0, 0, 180, 120});
  document.add_layer(std::move(small_layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 360);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({small_layer_id});
  canvas.show();
  QApplication::processEvents();

  const QPoint document_delta(12, 8);
  const auto start = canvas.widget_position_for_document_point(QPoint(20, 20));
  const auto end = canvas.widget_position_for_document_point(QPoint(20, 20) + document_delta);
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();

  const auto image = canvas.grab().toImage();
  const QColor outline_color(95, 170, 255);
  const QRect expected_outline(
      canvas.widget_position_for_document_point(QPoint(72, 42) + document_delta),
      canvas.widget_position_for_document_point(QPoint(72 + 18, 42 + 12) + document_delta));
  CHECK(count_pixels_close(image, expected_outline.normalized().adjusted(-2, -2, 2, 2), outline_color, 18) > 18);

  const QRect full_layer_top_edge(
      canvas.widget_position_for_document_point(QPoint(0, 0) + document_delta),
      canvas.widget_position_for_document_point(QPoint(180, 0) + document_delta));
  CHECK(count_pixels_close(image, full_layer_top_edge.normalized().adjusted(-2, -2, 2, 2), outline_color, 18) < 6);

  save_widget_artifact("ui_move_opaque_bounds", canvas);
  send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

void ui_move_tool_hover_outlines_opaque_bounds() {
  patchy::Document document(180, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(180, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  auto pixels = solid_pixels(180, 120, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(70, 40, 20, 14), QColor(25, 25, 25, 255));
  patchy::Layer hover_layer(document.allocate_layer_id(), "Hover Target", std::move(pixels));
  hover_layer.set_bounds(patchy::Rect{0, 0, 180, 120});
  document.add_layer(std::move(hover_layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 360);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_auto_select_layer(true);
  canvas.set_show_transform_controls(false);
  canvas.show();
  QApplication::processEvents();

  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(75, 45)), Qt::NoButton,
             Qt::NoButton);
  const auto image = canvas.grab().toImage();
  const QColor outline_color(95, 170, 255);
  const QRect expected_outline(canvas.widget_position_for_document_point(QPoint(70, 40)),
                               canvas.widget_position_for_document_point(QPoint(90, 54)));
  CHECK(count_pixels_close(image, expected_outline.normalized().adjusted(-2, -2, 2, 2), outline_color, 18) > 20);

  const QRect full_layer_top_edge(canvas.widget_position_for_document_point(QPoint(0, 0)),
                                  canvas.widget_position_for_document_point(QPoint(180, 0)));
  CHECK(count_pixels_close(image, full_layer_top_edge.normalized().adjusted(-2, -2, 2, 2), outline_color, 18) < 6);
  save_widget_artifact("ui_move_hover_opaque_bounds", canvas);

  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(20, 20)), Qt::NoButton,
             Qt::NoButton);
  const auto cleared = canvas.grab().toImage();
  CHECK(count_pixels_close(cleared, expected_outline.normalized().adjusted(-2, -2, 2, 2), outline_color, 18) < 6);
}

void ui_move_tool_uses_text_rect_for_hit_and_hover() {
  patchy::Document document(220, 140, patchy::PixelFormat::rgba8());
  auto& background =
      document.add_pixel_layer("Background", solid_pixels(220, 140, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  const auto background_id = background.id();

  auto pixels = solid_pixels(120, 48, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 32, 18), QColor(25, 25, 25, 255));
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Wide Label", std::move(pixels));
  text_layer.set_bounds(patchy::Rect{40, 36, 120, 48});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Wide Label";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "32";
  document.add_layer(std::move(text_layer));
  document.set_active_layer(background_id);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Move Text Rect"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  auto* text_item = require_layer_item(*layer_list, QStringLiteral("Text: Wide Label"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background_item);
  background_item->setSelected(true);
  QApplication::processEvents();
  CHECK(background_item->isSelected());
  CHECK(!text_item->isSelected());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  canvas->set_snap_enabled(false);

  const QPoint transparent_text_rect_point(138, 70);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(transparent_text_rect_point),
             Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();

  const auto hover_image = canvas->grab().toImage();
  const QColor outline_color(95, 170, 255);
  const QRect expected_text_outline(canvas->widget_position_for_document_point(QPoint(40, 36)),
                                    canvas->widget_position_for_document_point(QPoint(160, 84)));
  CHECK(count_pixels_close(hover_image, expected_text_outline.normalized().adjusted(-2, -2, 2, 2), outline_color,
                           18) > 30);
  const QRect background_top_edge(canvas->widget_position_for_document_point(QPoint(0, 0)),
                                  canvas->widget_position_for_document_point(QPoint(220, 0)));
  CHECK(count_pixels_close(hover_image, background_top_edge.normalized().adjusted(-2, -2, 2, 2), outline_color,
                           18) < 6);

  const QPoint delta(14, 9);
  drag(*canvas, canvas->widget_position_for_document_point(transparent_text_rect_point),
       canvas->widget_position_for_document_point(transparent_text_rect_point + delta));
  QApplication::processEvents();

  text_item = require_layer_item(*layer_list, QStringLiteral("Text: Wide Label"));
  background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  CHECK(text_item->isSelected());
  CHECK(!background_item->isSelected());
  const auto moved_text = canvas->active_layer_document_rect();
  CHECK(moved_text.has_value());
  CHECK(moved_text->topLeft() == QPoint(40, 36) + delta);
  CHECK(moved_text->size() == QSize(120, 48));
  save_widget_artifact("ui_move_text_rect_hit_hover", window);
}

void ui_move_transform_controls_do_not_block_auto_select_hover() {
  patchy::Document document(180, 120, patchy::PixelFormat::rgba8());
  auto& background =
      document.add_pixel_layer("Background", solid_pixels(180, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  const auto background_id = background.id();

  auto pixels = solid_pixels(180, 120, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(70, 40, 20, 14), QColor(25, 25, 25, 255));
  patchy::Layer hover_layer(document.allocate_layer_id(), "Hover Target", std::move(pixels));
  hover_layer.set_bounds(patchy::Rect{0, 0, 180, 120});
  document.add_layer(std::move(hover_layer));
  document.set_active_layer(background_id);

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 360);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_auto_select_layer(true);
  canvas.set_show_transform_controls(true);
  canvas.show();
  QApplication::processEvents();

  const QColor outline_color(95, 170, 255);
  const QRect expected_outline(canvas.widget_position_for_document_point(QPoint(70, 40)),
                               canvas.widget_position_for_document_point(QPoint(90, 54)));
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(75, 45)), Qt::NoButton,
             Qt::NoButton);
  const auto highlighted = canvas.grab().toImage();
  CHECK(count_pixels_close(highlighted, expected_outline.normalized().adjusted(-2, -2, 2, 2), outline_color, 18) >
        20);

  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(20, 20)), Qt::NoButton,
             Qt::NoButton);
  const auto active_background_hover = canvas.grab().toImage();
  CHECK(count_pixels_close(active_background_hover, expected_outline.normalized().adjusted(-2, -2, 2, 2),
                           outline_color, 18) < 6);

  canvas.set_auto_select_layer(false);
  QApplication::processEvents();
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(75, 45)), Qt::NoButton,
             Qt::NoButton);
  const auto auto_select_disabled = canvas.grab().toImage();
  CHECK(count_pixels_close(auto_select_disabled, expected_outline.normalized().adjusted(-2, -2, 2, 2),
                           outline_color, 18) < 6);
}

void ui_move_tool_moves_selected_folder_tree() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer folder(document.allocate_layer_id(), "Move Folder", patchy::LayerKind::Group);
  auto red = patchy::Layer(document.allocate_layer_id(), "Red Child",
                              solid_pixels(10, 10, patchy::PixelFormat::rgba8(), QColor(230, 30, 30)));
  red.set_bounds(patchy::Rect{20, 20, 10, 10});
  folder.add_child(std::move(red));

  patchy::Layer nested_folder(document.allocate_layer_id(), "Nested Move Folder", patchy::LayerKind::Group);
  auto blue = patchy::Layer(document.allocate_layer_id(), "Blue Grandchild",
                               solid_pixels(10, 10, patchy::PixelFormat::rgba8(), QColor(20, 90, 240)));
  blue.set_bounds(patchy::Rect{50, 20, 10, 10});
  nested_folder.add_child(std::move(blue));
  folder.add_child(std::move(nested_folder));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Move Folder Tree"));
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 24)), QColor(230, 30, 30), 20));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(54, 24)), QColor(20, 90, 240), 20));

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Move Folder"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(folder_item);
  folder_item->setSelected(true);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() == 1);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(false);
  canvas->set_auto_select_layer(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(80, 60));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(18, 12), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(18, 12), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(42, 36)), QColor(230, 30, 30), 35));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(72, 36)), QColor(20, 90, 240), 35));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 24)), QColor(Qt::white), 12));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(54, 24)), QColor(Qt::white), 12));
  CHECK(folder_item->isSelected());
  save_widget_artifact("ui_move_selected_folder_tree", window);
}

void ui_move_preview_clears_transparent_trails_and_keeps_layer_styles() {
  patchy::Document document(180, 120, patchy::PixelFormat::rgba8());

  patchy::PixelBuffer gradient_pixels(16, 16, patchy::PixelFormat::rgba8());
  gradient_pixels.clear(0);
  for (std::int32_t y = 0; y < gradient_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < gradient_pixels.width(); ++x) {
      auto* px = gradient_pixels.pixel(x, y);
      px[0] = 210;
      px[1] = 20;
      px[2] = 20;
      px[3] = 255;
    }
  }
  patchy::Layer gradient_layer(document.allocate_layer_id(), "Gradient Move", std::move(gradient_pixels));
  gradient_layer.set_bounds(patchy::Rect{20, 30, 16, 16});
  patchy::LayerGradientFill gradient_fill;
  gradient_fill.enabled = true;
  gradient_fill.blend_mode = patchy::BlendMode::Normal;
  gradient_fill.opacity = 1.0F;
  gradient_fill.gradient.color_stops.push_back(patchy::GradientColorStop{0.0F, patchy::RgbColor{30, 210, 80}});
  gradient_fill.gradient.color_stops.push_back(patchy::GradientColorStop{1.0F, patchy::RgbColor{30, 210, 80}});
  gradient_layer.layer_style().gradient_fills.push_back(gradient_fill);
  document.add_layer(std::move(gradient_layer));

  patchy::PixelBuffer color_pixels(16, 16, patchy::PixelFormat::rgba8());
  color_pixels.clear(0);
  for (std::int32_t y = 0; y < color_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < color_pixels.width(); ++x) {
      auto* px = color_pixels.pixel(x, y);
      px[0] = 210;
      px[1] = 20;
      px[2] = 20;
      px[3] = 255;
    }
  }
  patchy::Layer color_layer(document.allocate_layer_id(), "Color Move", std::move(color_pixels));
  color_layer.set_bounds(patchy::Rect{20, 60, 16, 16});
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = patchy::BlendMode::Normal;
  overlay.color = patchy::RgbColor{40, 90, 235};
  overlay.opacity = 1.0F;
  color_layer.layer_style().color_overlays.push_back(overlay);
  document.add_layer(std::move(color_layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Move Style Cache"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* gradient_item = require_layer_item(*layer_list, QStringLiteral("Gradient Move"));
  auto* color_item = require_layer_item(*layer_list, QStringLiteral("Color Move"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(color_item);
  color_item->setSelected(true);
  gradient_item->setSelected(true);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(false);
  canvas->set_auto_select_layer(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(24, 34));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(30, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(60, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();

  CHECK(!color_close(canvas_pixel(*canvas, QPoint(54, 34)), QColor(30, 210, 80), 45));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(84, 34)), QColor(30, 210, 80), 45));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(84, 64)), QColor(40, 90, 235), 45));

  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(60, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(!color_close(canvas_pixel(*canvas, QPoint(24, 34)), QColor(210, 20, 20), 35));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(84, 34)), QColor(30, 210, 80), 45));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(84, 64)), QColor(40, 90, 235), 45));
  save_widget_artifact("ui_move_preview_style_cache", window);
}

void ui_move_preview_leaves_no_trail_when_zoomed_out() {
  patchy::Document document(240, 180, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(240, 180, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer layer(document.allocate_layer_id(), "Zoomed Move",
                      solid_pixels(60, 60, patchy::PixelFormat::rgba8(), QColor(255, 40, 40)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{40, 50, 60, 60});
  document.add_layer(std::move(layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 380);
  canvas.set_document(&document);
  // Zoom < 1.0 exercises the smooth-downscaled display path, where the moving
  // layer would otherwise bleed past its bounds in the base image and leave a
  // residual outline at the drag-start position.
  canvas.set_zoom(0.37);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();

  // Drag the layer far enough that its original footprint no longer overlaps
  // its destination. Once the layer has moved away, its original location must
  // show only the (white) background: no residual layer pixels and no faint
  // rectangular seam where the original bounds used to be.
  const QPoint origin(70, 80);
  const QPoint move_delta(90, 60);
  const auto start = canvas.widget_position_for_document_point(origin);
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(origin + move_delta), Qt::NoButton,
             Qt::LeftButton);
  QApplication::processEvents();

  const auto preview = canvas.grab().toImage();
  // grab() honours the device pixel ratio, so convert logical widget points to
  // device pixels before sampling (otherwise HiDPI runs sample the wrong spot).
  const auto dpr = preview.devicePixelRatio();
  const auto device_point = [dpr](QPoint widget_point) {
    return QPoint(static_cast<int>(std::lround(widget_point.x() * dpr)),
                  static_cast<int>(std::lround(widget_point.y() * dpr)));
  };
  int trail_pixels = 0;
  const QRect original_region(40 - 3, 50 - 3, 60 + 6, 60 + 6);
  for (int y = original_region.top(); y <= original_region.bottom(); ++y) {
    for (int x = original_region.left(); x <= original_region.right(); ++x) {
      const auto sample = device_point(canvas.widget_position_for_document_point(QPoint(x, y)));
      if (!preview.rect().contains(sample)) {
        continue;
      }
      if (!color_close(preview.pixelColor(sample), QColor(Qt::white), 24)) {
        ++trail_pixels;
      }
    }
  }
  if (trail_pixels != 0) {
    ensure_artifact_dir();
    CHECK(preview.save(QStringLiteral("test-artifacts/ui_move_preview_zoomed_ghost.png")));
    std::cerr << "ui_move_preview_leaves_no_trail_when_zoomed_out trail_pixels=" << trail_pixels << '\n';
  }
  CHECK(trail_pixels == 0);

  // The moved layer should still be visible at its destination.
  const auto destination_sample = device_point(canvas.widget_position_for_document_point(origin + move_delta));
  CHECK(color_close(preview.pixelColor(destination_sample), QColor(255, 40, 40), 60));

  send_mouse(canvas, QEvent::MouseButtonRelease, canvas.widget_position_for_document_point(origin + move_delta),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

void ui_move_preview_mid_drag_partial_repaint_matches_full_preview() {
  patchy::Document document(220, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(220, 160, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::PixelBuffer pixels(64, 46, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  fill_pixel_rect(pixels, QRect(10, 8, 42, 27), QColor(35, 105, 225, 235));
  fill_pixel_rect(pixels, QRect(20, 17, 18, 10), QColor(240, 80, 45, 230));
  patchy::Layer layer(document.allocate_layer_id(), "Mid Drag Preview", std::move(pixels));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{54, 45, 64, 46});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.distance = 8.0F;
  shadow.size = 5.0F;
  shadow.opacity = 0.5F;
  shadow.color = patchy::RgbColor{0, 0, 0};
  layer.layer_style().drop_shadows.push_back(shadow);
  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.size = 5.0F;
  glow.opacity = 0.35F;
  glow.color = patchy::RgbColor{255, 225, 80};
  layer.layer_style().outer_glows.push_back(glow);
  document.add_layer(std::move(layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(500, 360);
  canvas.set_document(&document);
  canvas.set_zoom(1.5);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();

  PaintRegionRecorder recorder(&canvas);
  canvas.installEventFilter(&recorder);
  auto render_without_recording = [&]() {
    recorder.set_recording(false);
    auto image = render_widget_image(canvas);
    recorder.set_recording(true);
    return image;
  };

  const auto start = canvas.widget_position_for_document_point(QPoint(76, 62));
  const QPoint first_delta(34, 16);
  const QPoint second_delta(62, 31);

  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  recorder.reset();
  send_mouse(canvas, QEvent::MouseMove,
             canvas.widget_position_for_document_point(QPoint(76, 62) + first_delta), Qt::NoButton, Qt::LeftButton);
  auto first_region = recorder.region();
  CHECK(!first_region.isEmpty());

  recorder.reset();
  send_mouse(canvas, QEvent::MouseMove,
             canvas.widget_position_for_document_point(QPoint(76, 62) + second_delta), Qt::NoButton, Qt::LeftButton);
  auto second_region = recorder.region();
  CHECK(!second_region.isEmpty());
  const auto original_probe = canvas.widget_position_for_document_point(QPoint(54, 45));
  CHECK(second_region.contains(original_probe));
  const auto backing = grab_widget_window_image(canvas);
  const auto full_mid_drag = render_without_recording();
  const auto matches_full_mid_drag = images_equal_rgba(backing, full_mid_drag);
  if (!matches_full_mid_drag) {
    ensure_artifact_dir();
    CHECK(backing.save(QStringLiteral("test-artifacts/ui_move_preview_mid_drag_partial_backing.png")));
    CHECK(full_mid_drag.save(QStringLiteral("test-artifacts/ui_move_preview_mid_drag_partial_full.png")));
    if (const auto mismatch = image_mismatch_bounds_rgba(backing, full_mid_drag); mismatch.has_value()) {
      std::cerr << "ui_move_preview_mid_drag_partial_repaint mismatch bounds "
                << mismatch->x() << "," << mismatch->y() << "," << mismatch->width() << ","
                << mismatch->height() << '\n';
    }
  }
  CHECK(matches_full_mid_drag);

  send_mouse(canvas, QEvent::MouseButtonRelease,
             canvas.widget_position_for_document_point(QPoint(76, 62) + second_delta), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  ensure_artifact_dir();
  CHECK(backing.save(QStringLiteral("test-artifacts/ui_move_preview_mid_drag_partial_repaint.png")));
}

void ui_dirty_region_move_preview_matches_force_refresh() {
  patchy::Document document(180, 130, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(180, 130, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::PixelBuffer pixels(44, 34, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  fill_pixel_rect(pixels, QRect(8, 7, 25, 18), QColor(20, 90, 235, 230));
  patchy::Layer layer(document.allocate_layer_id(), "Styled Transparent Move", std::move(pixels));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{42, 38, 44, 34});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.distance = 7.0F;
  shadow.size = 5.0F;
  shadow.opacity = 0.55F;
  shadow.color = patchy::RgbColor{0, 0, 0};
  layer.layer_style().drop_shadows.push_back(shadow);
  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.size = 6.0F;
  glow.opacity = 0.45F;
  glow.color = patchy::RgbColor{255, 220, 80};
  layer.layer_style().outer_glows.push_back(glow);
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.size = 3.0F;
  stroke.opacity = 1.0F;
  stroke.color = patchy::RgbColor{30, 30, 35};
  layer.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 390);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();

  const QPoint delta(24, 10);
  const auto start = canvas.widget_position_for_document_point(QPoint(55, 50));
  const auto end = canvas.widget_position_for_document_point(QPoint(55, 50) + delta);
  const auto before = canvas.render_cache_diagnostics();
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto after_move = canvas.render_cache_diagnostics();
  CHECK(after_move.full_refreshes == before.full_refreshes);
  CHECK(after_move.move_precommit_patches == before.move_precommit_patches + 1);
  CHECK(after_move.move_preview_patch_reuses == before.move_preview_patch_reuses + 1);

  const auto dirty_rendered = canvas.grab().toImage();
  canvas.force_refresh();
  QApplication::processEvents();
  const auto forced = canvas.grab().toImage();
  CHECK(images_equal_rgba(dirty_rendered, forced));
  CHECK(color_close(canvas_pixel(canvas, QPoint(55, 50)), QColor(Qt::white), 12));
  CHECK(!color_close(canvas_pixel(canvas, QPoint(55, 50) + delta), QColor(Qt::white), 20));
  save_widget_artifact("ui_dirty_region_move_preview_force_refresh", canvas);
}

void ui_processing_overlay_animates_for_slow_dirty_render() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer layer(document.allocate_layer_id(), "Nudge Me",
                      solid_pixels(24, 22, patchy::PixelFormat::rgba8(), QColor(230, 40, 35, 255)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{30, 28, 24, 22});
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::CanvasWidget canvas;
  canvas.resize(360, 260);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();
  canvas.force_refresh();
  QApplication::processEvents();

  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  EnvironmentVariableRestorer restore_min_pixels("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS");
  EnvironmentVariableRestorer restore_test_delay("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));
  qputenv("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS", QByteArray("260"));

  const auto before = canvas.render_cache_diagnostics();
  send_key(canvas, Qt::Key_Right);
  const auto after = canvas.render_cache_diagnostics();

  CHECK(after.processing_overlays_shown == before.processing_overlays_shown + 1);
  CHECK(after.processing_overlay_frames > before.processing_overlay_frames);
  CHECK(!canvas.processing_overlay_visible());
  CHECK(color_close(canvas_pixel(canvas, QPoint(31, 39)), QColor(230, 40, 35), 3));
  CHECK(color_close(canvas_pixel(canvas, QPoint(30, 39)), QColor(Qt::white), 3));
}

void ui_processing_overlay_stays_top_aligned_without_dimming_canvas() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(160, 120, patchy::PixelFormat::rgba8(),
                                                      QColor(88, 196, 128)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(420, 320);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.show();
  QApplication::processEvents();
  canvas.force_refresh();
  QApplication::processEvents();

  const auto baseline = canvas.grab().toImage();
  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));

  canvas.begin_processing_operation();
  canvas.tick_processing_operation();
  QApplication::processEvents();

  CHECK(canvas.processing_overlay_visible());
  const auto with_overlay = canvas.grab().toImage();
  const auto mismatch = image_mismatch_bounds_rgba(baseline, with_overlay);
  CHECK(mismatch.has_value());
  CHECK(mismatch->top() <= 24);
  CHECK(mismatch->bottom() < 100);

  const auto lower_document_sample = canvas.widget_position_for_document_point(QPoint(80, 100));
  CHECK(with_overlay.rect().contains(lower_document_sample));
  CHECK(color_close(with_overlay.pixelColor(lower_document_sample),
                    baseline.pixelColor(lower_document_sample), 0));

  canvas.end_processing_operation();
  QApplication::processEvents();
  CHECK(!canvas.processing_overlay_visible());
}

void ui_brush_family_strokes_do_not_trigger_processing_overlay() {
  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));

  const auto exercise_tool = [](patchy::ui::CanvasTool tool) {
    patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
    auto& layer = document.add_pixel_layer("Paint", solid_pixels(120, 90, patchy::PixelFormat::rgba8(),
                                                                 QColor(Qt::white)));
    document.set_active_layer(layer.id());

    patchy::ui::CanvasWidget canvas;
    canvas.resize(360, 260);
    canvas.set_document(&document);
    canvas.set_zoom(2.0);
    canvas.set_tool(tool);
    canvas.set_primary_color(QColor(20, 20, 20));
    canvas.set_brush_size(12);
    canvas.set_brush_opacity(100);
    canvas.set_brush_softness(20);
    canvas.show();
    QApplication::processEvents();
    canvas.force_refresh();
    QApplication::processEvents();

    if (tool == patchy::ui::CanvasTool::Clone) {
      const auto source = canvas.widget_position_for_document_point(QPoint(28, 28));
      send_mouse(canvas, QEvent::MouseButtonPress, source, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
      send_mouse(canvas, QEvent::MouseButtonRelease, source, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
      QApplication::processEvents();
    }

    const auto before = canvas.render_cache_diagnostics();
    const auto start = canvas.widget_position_for_document_point(QPoint(40, 44));
    const auto end = canvas.widget_position_for_document_point(QPoint(76, 44));
    send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    CHECK(!canvas.processing_operation_active());
    CHECK(!canvas.processing_overlay_visible());

    send_mouse(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    CHECK(!canvas.processing_operation_active());
    CHECK(!canvas.processing_overlay_visible());

    send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    const auto after = canvas.render_cache_diagnostics();
    CHECK(after.processing_overlays_shown == before.processing_overlays_shown);
    CHECK(!canvas.processing_overlay_visible());
  };

  exercise_tool(patchy::ui::CanvasTool::Brush);
  exercise_tool(patchy::ui::CanvasTool::Eraser);
  exercise_tool(patchy::ui::CanvasTool::Smudge);
  exercise_tool(patchy::ui::CanvasTool::Clone);
}

void ui_processing_overlay_animates_for_slow_nudge_undo_snapshot() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(96, 72, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Nudge Snapshot",
                      solid_pixels(18, 16, patchy::PixelFormat::rgba8(), QColor(35, 185, 90, 255)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{24, 22, 18, 16});
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Nudge Snapshot Processing"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  canvas->set_show_transform_controls(false);
  canvas->force_refresh();
  QApplication::processEvents();

  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  EnvironmentVariableRestorer restore_undo_delay("PATCHY_UNDO_SNAPSHOT_TEST_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));
  qputenv("PATCHY_UNDO_SNAPSHOT_TEST_DELAY_MS", QByteArray("240"));

  const auto before = canvas->render_cache_diagnostics();
  send_key(*canvas, Qt::Key_Right);
  const auto after = canvas->render_cache_diagnostics();

  CHECK(after.processing_overlays_shown == before.processing_overlays_shown + 1);
  CHECK(after.processing_overlay_frames > before.processing_overlay_frames);
  CHECK(!canvas->processing_overlay_visible());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(25, 30)), QColor(35, 185, 90), 3));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 30)), QColor(Qt::white), 3));
}

void ui_processing_overlay_is_visible_before_slow_move_commit_callback() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Commit Wait",
                      solid_pixels(28, 24, patchy::PixelFormat::rgba8(), QColor(45, 130, 230, 255)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{32, 30, 28, 24});
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::CanvasWidget canvas;
  canvas.resize(360, 260);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();
  canvas.force_refresh();
  QApplication::processEvents();

  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  EnvironmentVariableRestorer restore_min_pixels("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS");
  qputenv("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS", QByteArray("0"));
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));

  bool saw_processing_during_commit_callback = false;
  canvas.set_before_edit_callback([&](QString) {
    saw_processing_during_commit_callback = canvas.processing_overlay_visible();
  });

  const auto before = canvas.render_cache_diagnostics();
  const auto start = canvas.widget_position_for_document_point(QPoint(40, 38));
  const auto end = canvas.widget_position_for_document_point(QPoint(46, 38));
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto after = canvas.render_cache_diagnostics();

  CHECK(saw_processing_during_commit_callback);
  CHECK(after.processing_overlays_shown == before.processing_overlays_shown + 1);
  CHECK(!canvas.processing_overlay_visible());
  CHECK(color_close(canvas_pixel(canvas, QPoint(64, 38)), QColor(45, 130, 230), 3));
  CHECK(color_close(canvas_pixel(canvas, QPoint(34, 38)), QColor(Qt::white), 3));
}

void ui_processing_overlay_ticks_during_filter_apply() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(30, 120, 220));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  canvas->force_refresh();
  QApplication::processEvents();

  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));

  const auto before = canvas->render_cache_diagnostics();
  accept_filter_dialog({{QStringLiteral("filterStrengthSpin"), 150}});
  require_action(window, "filterAction_patchy_filters_edge_detect")->trigger();
  QApplication::processEvents();
  const auto after = canvas->render_cache_diagnostics();

  CHECK(after.processing_overlays_shown > before.processing_overlays_shown);
  CHECK(!canvas->processing_overlay_visible());
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(30, 120, 220), 8));
}

void ui_processing_overlay_ticks_during_fill_tool_loop() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Fill Target",
                      solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  const auto layer_id = layer.id();
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::CanvasWidget canvas;
  canvas.resize(380, 300);
  canvas.set_document(&document);
  canvas.set_zoom(1.5);
  canvas.set_tool(patchy::ui::CanvasTool::Fill);
  canvas.set_primary_color(QColor(210, 45, 80));
  canvas.show();
  QApplication::processEvents();
  canvas.force_refresh();
  QApplication::processEvents();

  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));

  const auto before = canvas.render_cache_diagnostics();
  const auto click = canvas.widget_position_for_document_point(QPoint(40, 40));
  send_mouse(canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto after = canvas.render_cache_diagnostics();

  CHECK(after.processing_overlays_shown > before.processing_overlays_shown);
  CHECK(!canvas.processing_overlay_visible());
  CHECK(color_close(canvas_pixel(canvas, QPoint(40, 40)), QColor(210, 45, 80), 3));
}

void ui_layer_style_cache_invalidates_after_pixel_mutation() {
  patchy::Document document(80, 60, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer layer(document.allocate_layer_id(), "Cached Stroke",
                      solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{20, 15, 20, 20});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = patchy::BlendMode::Normal;
  stroke.color = patchy::RgbColor{40, 180, 80};
  stroke.opacity = 1.0F;
  stroke.size = 2.0F;
  layer.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(layer));

  const auto first = patchy::ui::qimage_from_document(document, true);
  CHECK(color_close(first.pixelColor(30, 25), QColor(220, 40, 40), 2));
  const auto cached = patchy::ui::qimage_from_document(document, true);
  CHECK(color_close(cached.pixelColor(30, 25), QColor(220, 40, 40), 2));

  auto* editable_layer = document.find_layer(layer_id);
  CHECK(editable_layer != nullptr);
  auto* center = editable_layer->pixels().pixel(10, 10);
  center[0] = 35;
  center[1] = 95;
  center[2] = 235;
  center[3] = 255;

  const auto updated = patchy::ui::qimage_from_document(document, true);
  CHECK(color_close(updated.pixelColor(30, 25), QColor(35, 95, 235), 2));
  CHECK(color_close(updated.pixelColor(18, 15), QColor(40, 180, 80), 2));
}

void ui_move_expensive_styled_layer_uses_outline_until_release() {
  patchy::Document document(1500, 1300, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(1500, 1300, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer layer(document.allocate_layer_id(), "Large Styled Move",
                      solid_pixels(1000, 1000, patchy::PixelFormat::rgba8(), QColor(20, 90, 235)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{100, 100, 1000, 1000});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = patchy::BlendMode::Normal;
  stroke.color = patchy::RgbColor{40, 180, 80};
  stroke.opacity = 1.0F;
  stroke.size = 2.0F;
  layer.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(900, 720);
  canvas.set_document(&document);
  canvas.set_zoom(0.5);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();

  const QPoint delta(300, 0);
  const QPoint old_only_point(150, 500);
  const QPoint moved_only_point(1250, 500);
  CHECK(color_close(canvas_pixel(canvas, old_only_point), QColor(20, 90, 235), 45));
  const auto before_release_stats = canvas.render_cache_diagnostics();
  const auto start = canvas.widget_position_for_document_point(QPoint(150, 150));
  const auto end = canvas.widget_position_for_document_point(QPoint(150, 150) + delta);
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(canvas, old_only_point), QColor(20, 90, 235), 45));
  CHECK(color_close(canvas_pixel(canvas, moved_only_point), QColor(Qt::white), 45));

  send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto after_release_stats = canvas.render_cache_diagnostics();
  CHECK(after_release_stats.full_refreshes == before_release_stats.full_refreshes);
  CHECK(after_release_stats.move_precommit_patches == before_release_stats.move_precommit_patches + 1);
  CHECK(color_close(canvas_pixel(canvas, moved_only_point), QColor(20, 90, 235), 45));
  CHECK(color_close(canvas_pixel(canvas, old_only_point), QColor(Qt::white), 45));
  save_widget_artifact("ui_move_expensive_style_outline", canvas);
}

void ui_layer_move_repaints_only_active_document_tab() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);

  accept_new_document_dialog(420, 260);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  accept_new_document_dialog(420, 260);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 3);

  std::vector<patchy::ui::CanvasWidget*> canvases;
  std::vector<std::unique_ptr<PaintCounterFilter>> counters;
  for (int index = 0; index < tabs->count(); ++index) {
    auto* canvas = dynamic_cast<patchy::ui::CanvasWidget*>(tabs->widget(index));
    CHECK(canvas != nullptr);
    canvases.push_back(canvas);
    auto counter = std::make_unique<PaintCounterFilter>();
    canvas->installEventFilter(counter.get());
    counters.push_back(std::move(counter));
  }

  tabs->setCurrentIndex(2);
  QApplication::processEvents();
  auto* active_canvas = canvases[2];
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  active_canvas->set_primary_color(QColor(20, 150, 240));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  for (auto& counter : counters) {
    counter->paint_events = 0;
  }

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  active_canvas->set_show_transform_controls(false);
  active_canvas->set_auto_select_layer(false);
  const auto start = active_canvas->widget_position_for_document_point(QPoint(40, 40));
  send_mouse(*active_canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  for (int step = 1; step <= 8; ++step) {
    send_mouse(*active_canvas, QEvent::MouseMove, start + QPoint(step * 12, step * 3), Qt::NoButton, Qt::LeftButton);
  }
  send_mouse(*active_canvas, QEvent::MouseButtonRelease, start + QPoint(96, 24), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(counters[2]->paint_events > 0);
  CHECK(counters[0]->paint_events == 0);
  CHECK(counters[1]->paint_events == 0);
  save_widget_artifact("ui_move_active_tab_only", window);
}

void ui_arduboy_psd_render_path_if_available() {
  const auto path = std::filesystem::path("D:/projects/C2/MiscPrints/Arduboy.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto image = patchy::ui::qimage_from_document(document, true);
  CHECK(!image.isNull());

  std::size_t non_white_pixels = 0;
  for (int y = 0; y < image.height(); y += 16) {
    for (int x = 0; x < image.width(); x += 16) {
      const auto color = image.pixelColor(x, y);
      if (color.alpha() != 0 && (color.red() < 245 || color.green() < 245 || color.blue() < 245)) {
        ++non_white_pixels;
      }
    }
  }
  CHECK(non_white_pixels > 1000);

  ensure_artifact_dir();
  const auto preview = image.scaled(QSize(360, 480), Qt::KeepAspectRatio, Qt::SmoothTransformation);
  CHECK(preview.save(QStringLiteral("test-artifacts/ui_arduboy_psd_render.png")));
}

void ui_duke_psd_text_edit_stays_responsive_if_available() {
  const auto path = std::filesystem::path("C:/temp/Duke nukem mobile.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  QElapsedTimer timer;
  timer.start();
  auto document = patchy::psd::DocumentIo::read_file(path);
  const auto load_elapsed_ms = timer.elapsed();
  CHECK(load_elapsed_ms < 10000);

  struct TextTarget {
    QRect bounds;
  };
  std::optional<TextTarget> target;
  std::function<void(const std::vector<patchy::Layer>&)> find_target;
  find_target = [&](const std::vector<patchy::Layer>& layers) {
    for (auto it = layers.rbegin(); it != layers.rend() && !target.has_value(); ++it) {
      const auto& layer = *it;
      if (layer.kind() == patchy::LayerKind::Group) {
        find_target(layer.children());
        continue;
      }
      const auto text = layer.metadata().find(patchy::kLayerMetadataText);
      const auto name = QString::fromStdString(layer.name());
      const auto metadata_text = text != layer.metadata().end() ? QString::fromStdString(text->second) : QString();
      if (!name.contains(QStringLiteral("Duke Nukem Mobile"), Qt::CaseInsensitive) &&
          !metadata_text.contains(QStringLiteral("Duke Nukem Mobile"), Qt::CaseInsensitive)) {
        continue;
      }
      const auto bounds = layer.bounds();
      target = TextTarget{QRect(bounds.x, bounds.y, bounds.width, bounds.height)};
    }
  };
  find_target(document.layers());
  CHECK(target.has_value());
  CHECK(!target->bounds.isEmpty());

  timer.restart();
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Duke Nukem Mobile"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->fit_to_view();
  QApplication::processEvents();
  const auto display_elapsed_ms = timer.elapsed();
  CHECK(display_elapsed_ms < 5000);

  const auto hit_document_point = target->bounds.center();
  const auto hit_widget_point = canvas->widget_position_for_document_point(hit_document_point);
  CHECK(canvas->rect().contains(hit_widget_point));

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  timer.restart();
  accept_missing_psd_text_font_warning_if_present();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  process_events_for(320);
  CHECK(!editor->property("patchy.previewPaintsText").toBool());
  CHECK(!editor->property("patchy.textPreviewLayerId").isValid());
  const auto editor_block_tops = [](const QTextEdit& text_editor) {
    std::vector<int> tops;
    const auto* layout = text_editor.document()->documentLayout();
    for (auto block = text_editor.document()->begin(); block.isValid(); block = block.next()) {
      tops.push_back(static_cast<int>(std::round(layout->blockBoundingRect(block).top())));
    }
    return tops;
  };
  const auto block_tops_close = [](const std::vector<int>& expected, const std::vector<int>& actual) {
    if (actual.size() != expected.size()) {
      return false;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
      if (std::abs(actual[index] - expected[index]) > 2) {
        return false;
      }
    }
    return true;
  };
  const auto initial_block_tops = editor_block_tops(*editor);
  CHECK(initial_block_tops.size() >= 5U);
  const auto plain_text = editor->toPlainText();
  const auto selection_end = plain_text.indexOf(QStringLiteral("Ever heard"));
  CHECK(selection_end > 0);
  QTextCursor selection_cursor(editor->document());
  selection_cursor.setPosition(0);
  selection_cursor.setPosition(selection_end, QTextCursor::KeepAnchor);
  editor->setTextCursor(selection_cursor);
  QApplication::processEvents();
  CHECK(editor->textCursor().hasSelection());
  const auto edit_elapsed_ms = timer.elapsed();
  CHECK(edit_elapsed_ms < 3000);
  QTextCursor end_cursor(editor->document());
  end_cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(end_cursor);
  QApplication::processEvents();
  CHECK(!editor->textCursor().hasSelection());

  timer.restart();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  canvas->set_show_transform_controls(false);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(timer.elapsed() < 4000);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  accept_missing_psd_text_font_warning_if_present();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  process_events_for(80);
  const auto reedit_block_tops = editor_block_tops(*editor);
  if (!block_tops_close(initial_block_tops, reedit_block_tops)) {
    send_key(*editor, Qt::Key_Escape);
    QApplication::processEvents();
  }
  CHECK(block_tops_close(initial_block_tops, reedit_block_tops));
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_duke_psd_seth_text_edit_preview_if_available() {
  const auto path = std::filesystem::path("C:/temp/Duke nukem mobile.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  auto document = patchy::psd::DocumentIo::read_file(path);
  struct TextTarget {
    patchy::LayerId id{};
    QRect bounds;
  };
  std::optional<TextTarget> target;
  const std::function<void(const std::vector<patchy::Layer>&)> find_target =
      [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          if (target.has_value()) {
            return;
          }
          if (layer.kind() == patchy::LayerKind::Group) {
            find_target(layer.children());
            continue;
          }
          const auto text = layer.metadata().find(patchy::kLayerMetadataText);
          if (text == layer.metadata().end() ||
              !QString::fromStdString(text->second).contains(QStringLiteral("I did all the programming"),
                                                             Qt::CaseInsensitive)) {
            continue;
          }
          const auto bounds = layer.bounds();
          target = TextTarget{layer.id(), QRect(bounds.x, bounds.y, bounds.width, bounds.height)};
        }
      };
  find_target(document.layers());
  CHECK(target.has_value());
  CHECK(!target->bounds.isEmpty());
  document.set_active_layer(target->id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Duke Nukem Mobile Seth"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->zoom_to_document_rect(target->bounds.adjusted(-280, -220, 280, 220));
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(false);
  canvas->set_show_transform_controls(false);
  const QPoint move_delta(26, -14);
  const auto move_start = canvas->widget_position_for_document_point(target->bounds.center());
  const auto move_end = canvas->widget_position_for_document_point(target->bounds.center() + move_delta);
  drag(*canvas, move_start, move_end);
  target->bounds.translate(move_delta);
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_widget_point = canvas->widget_position_for_document_point(target->bounds.center());
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  process_events_for(420);
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").isValid());
  save_widget_artifact("ui_duke_seth_text_edit_preview", *canvas);

  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_text_reedit_preserves_rich_text_spacing() {
  patchy::Document document(900, 700, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(900, 700, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Text Stability"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(0.75);
  QApplication::processEvents();

  auto* text_size = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  auto* text_bold = window.findChild<QPushButton*>(QStringLiteral("textBoldButton"));
  CHECK(text_size != nullptr);
  CHECK(text_bold != nullptr);
  text_size->setValue(text_points_for_pixels(72));
  text_bold->setChecked(true);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 80)),
       canvas->widget_position_for_document_point(QPoint(820, 620)));
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  const auto title_size = std::max(8, static_cast<int>(std::round(72.0 * canvas->zoom())));
  const auto body_size = std::max(8, static_cast<int>(std::round(36.0 * canvas->zoom())));
  QFont title_font = editor->font();
  title_font.setPixelSize(title_size);
  title_font.setBold(true);
  QFont body_font = editor->font();
  body_font.setPixelSize(body_size);
  body_font.setBold(true);
  QTextCharFormat title_format;
  title_format.setFont(title_font);
  title_format.setForeground(QBrush(QColor(35, 30, 59)));
  QTextCharFormat body_format;
  body_format.setFont(body_font);
  body_format.setForeground(QBrush(QColor(35, 30, 59)));
  QTextCursor rich_cursor(editor->document());
  rich_cursor.select(QTextCursor::Document);
  rich_cursor.removeSelectedText();
  rich_cursor.insertText(QStringLiteral("Duke Nukem Mobile\n\n"), title_format);
  rich_cursor.insertText(QStringLiteral("(for the Tapwave Zodiac released by Machineworks Northwest, 2004)\n\n"),
                         body_format);
  rich_cursor.insertText(
      QStringLiteral("Ever heard of the the Tapwave Zodiac?  It's a failed handheld that was released in 2003.\n\n"),
      body_format);
  rich_cursor.insertText(QStringLiteral(
                             "All the Zodiacs today have gross ass disintegrated left and right shoulder buttons due "
                             "to the poor choice of materials."),
                         body_format);
  editor->setTextCursor(rich_cursor);
  QApplication::processEvents();
  const auto editor_block_tops = [](const QTextEdit& text_editor) {
    std::vector<int> tops;
    const auto* layout = text_editor.document()->documentLayout();
    for (auto block = text_editor.document()->begin(); block.isValid(); block = block.next()) {
      tops.push_back(static_cast<int>(std::round(layout->blockBoundingRect(block).top())));
    }
    return tops;
  };
  const auto block_tops_close = [](const std::vector<int>& expected, const std::vector<int>& actual) {
    if (actual.size() != expected.size()) {
      return false;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
      if (std::abs(actual[index] - expected[index]) > 2) {
        return false;
      }
    }
    return true;
  };
  const auto initial_block_tops = editor_block_tops(*editor);
  CHECK(initial_block_tops.size() >= 6U);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  send_mouse(*canvas, QEvent::MouseButtonDblClick, canvas->widget_position_for_document_point(QPoint(100, 100)),
             Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  process_events_for(80);
  CHECK(!editor->property("patchy.previewPaintsText").toBool());
  CHECK(!editor->property("patchy.textPreviewLayerId").isValid());
  const auto first_reedit_tops = editor_block_tops(*editor);
  if (!block_tops_close(initial_block_tops, first_reedit_tops)) {
    send_key(*editor, Qt::Key_Escape);
    QApplication::processEvents();
  }
  CHECK(block_tops_close(initial_block_tops, first_reedit_tops));

  const auto plain_text = editor->toPlainText();
  const auto selection_end = plain_text.indexOf(QStringLiteral("All the Zodiacs"));
  CHECK(selection_end > 0);
  QTextCursor selection_cursor(editor->document());
  selection_cursor.setPosition(0);
  selection_cursor.setPosition(selection_end, QTextCursor::KeepAnchor);
  editor->setTextCursor(selection_cursor);
  QApplication::processEvents();
  CHECK(editor->textCursor().hasSelection());
  QTextCursor end_cursor(editor->document());
  end_cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(end_cursor);
  QApplication::processEvents();
  CHECK(!editor->textCursor().hasSelection());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  send_mouse(*canvas, QEvent::MouseButtonDblClick, canvas->widget_position_for_document_point(QPoint(100, 100)),
             Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  process_events_for(80);
  CHECK(!editor->property("patchy.previewPaintsText").toBool());
  CHECK(!editor->property("patchy.textPreviewLayerId").isValid());
  const auto second_reedit_tops = editor_block_tops(*editor);
  if (!block_tops_close(initial_block_tops, second_reedit_tops)) {
    send_key(*editor, Qt::Key_Escape);
    QApplication::processEvents();
  }
  CHECK(block_tops_close(initial_block_tops, second_reedit_tops));
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_marquee_selection_modifiers_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);

  drag(*canvas, QPoint(60, 60), QPoint(100, 100));
  auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->contains(QPoint(25, 25)));

  drag(*canvas, QPoint(130, 130), QPoint(170, 170), Qt::ShiftModifier);
  selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->contains(QPoint(25, 25)));
  CHECK(selection->contains(QPoint(125, 125)));
  CHECK(canvas->selected_document_region().contains(QPoint(25, 25)));
  CHECK(canvas->selected_document_region().contains(QPoint(125, 125)));
  CHECK(!canvas->selected_document_region().contains(QPoint(75, 75)));
  const auto added_width = selection->width();

  drag(*canvas, QPoint(120, 50), QPoint(180, 180), Qt::AltModifier);
  selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->width() < added_width);
  save_widget_artifact("ui_selection_modifiers", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
}

void ui_selection_toolbar_modes_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();

  auto* new_mode = require_action(window, "selectionNewModeAction");
  auto* add_mode = require_action(window, "selectionAddModeAction");
  auto* subtract_mode = require_action(window, "selectionSubtractModeAction");
  auto* intersect_mode = require_action(window, "selectionIntersectModeAction");
  CHECK(new_mode->isChecked());

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(70, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(35, 35)));

  add_mode->trigger();
  QApplication::processEvents();
  CHECK(add_mode->isChecked());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(120, 120)),
       canvas->widget_position_for_document_point(QPoint(170, 170)));
  CHECK(canvas->selected_document_region().contains(QPoint(35, 35)));
  CHECK(canvas->selected_document_region().contains(QPoint(145, 145)));
  CHECK(!canvas->selected_document_region().contains(QPoint(95, 95)));

  subtract_mode->trigger();
  QApplication::processEvents();
  CHECK(subtract_mode->isChecked());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(34, 34)),
       canvas->widget_position_for_document_point(QPoint(56, 56)));
  CHECK(!canvas->selected_document_region().contains(QPoint(45, 45)));
  CHECK(canvas->selected_document_region().contains(QPoint(25, 25)));
  CHECK(canvas->selected_document_region().contains(QPoint(145, 145)));

  intersect_mode->trigger();
  QApplication::processEvents();
  CHECK(intersect_mode->isChecked());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(132, 132)),
       canvas->widget_position_for_document_point(QPoint(158, 158)));
  CHECK(!canvas->selected_document_region().contains(QPoint(25, 25)));
  CHECK(canvas->selected_document_region().contains(QPoint(145, 145)));
  CHECK(!canvas->selected_document_region().contains(QPoint(125, 125)));

  new_mode->trigger();
  QApplication::processEvents();
  CHECK(new_mode->isChecked());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(210, 30)),
       canvas->widget_position_for_document_point(QPoint(250, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(225, 45)));
  CHECK(!canvas->selected_document_region().contains(QPoint(145, 145)));
  save_widget_artifact("ui_selection_toolbar_modes", *canvas);
}

void ui_ctrl_a_selects_entire_canvas() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  const auto document_rect = canvas->active_layer_document_rect();
  CHECK(document_rect.has_value());
  CHECK(!canvas->has_selection());

  send_key(*canvas, Qt::Key_A, Qt::ControlModifier);
  QApplication::processEvents();

  const auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->topLeft() == QPoint(0, 0));
  CHECK(selection->size() == document_rect->size());
  CHECK(canvas->selected_document_region().contains(QPoint(0, 0)));
  CHECK(canvas->selected_document_region().contains(document_rect->bottomRight()));

  canvas->contract_selection(10);
  QApplication::processEvents();
  auto contracted_selection = canvas->selected_document_rect();
  CHECK(contracted_selection.has_value());
  CHECK(contracted_selection->topLeft() == QPoint(10, 10));
  CHECK(contracted_selection->bottomRight() == document_rect->bottomRight() - QPoint(10, 10));
  CHECK(!canvas->selected_document_region().contains(QPoint(9, 9)));
  CHECK(canvas->selected_document_region().contains(QPoint(10, 10)));

  require_action(window, "editSelectAllAction")->trigger();
  QApplication::processEvents();
  accept_integer_dialog(QStringLiteral("patchyContractSelectionDialog"), 123);
  require_action(window, "selectContractAction")->trigger();
  QApplication::processEvents();
  contracted_selection = canvas->selected_document_rect();
  CHECK(contracted_selection.has_value());
  CHECK(contracted_selection->topLeft() == QPoint(123, 123));
  CHECK(contracted_selection->bottomRight() == document_rect->bottomRight() - QPoint(123, 123));
  CHECK(!canvas->selected_document_region().contains(QPoint(122, 122)));
  CHECK(canvas->selected_document_region().contains(QPoint(123, 123)));
  save_widget_artifact("ui_ctrl_a_select_all", *canvas);
}

void ui_alt_backspace_fills_selection_with_foreground() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_primary_color(QColor(20, 140, 230));

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 70)),
       canvas->widget_position_for_document_point(QPoint(180, 150)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());

  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 100)), QColor(20, 140, 230), 12));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), Qt::white, 8));
  save_widget_artifact("ui_alt_backspace_fill_selection", *canvas);
}

void ui_feathered_marquee_fill_uses_soft_selection_alpha() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_primary_color(QColor(25, 90, 230));

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  auto* feather = window.findChild<QSpinBox*>(QStringLiteral("selectionFeatherSpin"));
  auto* anti_alias = window.findChild<QCheckBox*>(QStringLiteral("selectionAntiAliasCheck"));
  CHECK(feather != nullptr);
  CHECK(anti_alias != nullptr);
  feather->setValue(20);
  anti_alias->setChecked(true);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Marquee);
  CHECK(canvas->selection_feather_radius() == 20);
  CHECK(canvas->selection_antialias());

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(120, 90)),
       canvas->widget_position_for_document_point(QPoint(260, 210)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  const auto selection_rect = canvas->selected_document_rect();
  CHECK(selection_rect.has_value());
  std::optional<QPoint> solid_point;
  std::optional<QPoint> feather_point;
  for (int y = selection_rect->top(); y <= selection_rect->bottom(); ++y) {
    for (int x = selection_rect->left(); x <= selection_rect->right(); ++x) {
      const auto alpha = canvas->selection_alpha_at(QPoint(x, y));
      if (!solid_point.has_value() && alpha > 240) {
        solid_point = QPoint(x, y);
      }
      if (!feather_point.has_value() && alpha > 40 && alpha < 220) {
        feather_point = QPoint(x, y);
      }
    }
  }
  CHECK(solid_point.has_value());
  CHECK(feather_point.has_value());
  const QPoint hard_corner(120, 90);
  const QPoint hard_top_edge(190, 90);
  const auto corner_alpha = canvas->selection_alpha_at(hard_corner);
  const auto top_edge_alpha = canvas->selection_alpha_at(hard_top_edge);
  const auto center_alpha = canvas->selection_alpha_at(QPoint(190, 150));
  CHECK(corner_alpha > 0);
  CHECK(top_edge_alpha > corner_alpha);
  CHECK(center_alpha > top_edge_alpha);
  CHECK(canvas->selection_alpha_at(QPoint(95, 150)) > 0);
  CHECK(canvas->selection_alpha_at(QPoint(82, 150)) < canvas->selection_alpha_at(QPoint(95, 150)));

  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  canvas->set_selection_edges_visible(false);
  QApplication::processEvents();
  const auto inside = canvas_pixel(*canvas, *solid_point);
  CHECK(inside.blue() > 180);
  CHECK(inside.blue() > inside.green());
  const auto feathered = canvas_pixel(*canvas, *feather_point);
  CHECK(feathered.blue() > feathered.green());
  CHECK(feathered.green() > 110);
  CHECK(feathered.green() < 245);
  const auto corner_fill = canvas_pixel(*canvas, hard_corner);
  const auto top_edge_fill = canvas_pixel(*canvas, hard_top_edge);
  CHECK(corner_fill.green() > top_edge_fill.green());
  CHECK(corner_fill.red() > top_edge_fill.red());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 150)), Qt::white, 8));
  canvas->set_selection_edges_visible(true);
  QApplication::processEvents();
  save_widget_artifact("ui_feathered_marquee_fill", *canvas);
}

void ui_marquee_fixed_size_and_ratio_options_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();

  auto* style = window.findChild<QComboBox*>(QStringLiteral("selectionStyleCombo"));
  auto* width = window.findChild<QSpinBox*>(QStringLiteral("selectionFixedWidthSpin"));
  auto* height = window.findChild<QSpinBox*>(QStringLiteral("selectionFixedHeightSpin"));
  CHECK(style != nullptr);
  CHECK(width != nullptr);
  CHECK(height != nullptr);
  CHECK(style->currentText() == QStringLiteral("Normal"));

  width->setValue(80);
  height->setValue(50);
  style->setCurrentText(QStringLiteral("Fixed Size"));
  QApplication::processEvents();
  CHECK(canvas->marquee_style() == patchy::ui::CanvasWidget::MarqueeStyle::FixedSize);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(30, 30)),
       canvas->widget_position_for_document_point(QPoint(55, 55)));
  auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->topLeft() == QPoint(30, 30));
  CHECK(selection->width() == 80);
  CHECK(selection->height() == 50);
  CHECK(canvas->selected_document_region().contains(QPoint(105, 75)));
  CHECK(!canvas->selected_document_region().contains(QPoint(112, 84)));

  style->setCurrentText(QStringLiteral("Fixed Ratio"));
  width->setValue(2);
  height->setValue(1);
  QApplication::processEvents();
  CHECK(canvas->marquee_style() == patchy::ui::CanvasWidget::MarqueeStyle::FixedRatio);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(180, 40)),
       canvas->widget_position_for_document_point(QPoint(300, 140)));
  selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  const auto ratio = static_cast<double>(selection->width()) / static_cast<double>(selection->height());
  CHECK(ratio > 1.85);
  CHECK(ratio < 2.15);
  CHECK(canvas->selected_document_region().contains(QPoint(230, 64)));
  CHECK(!canvas->selected_document_region().contains(QPoint(230, 130)));
  save_widget_artifact("ui_marquee_fixed_size_ratio", *canvas);
}

void ui_elliptical_marquee_selects_oval_region() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Elliptical Marquee"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::EllipticalMarquee);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 60)),
       canvas->widget_position_for_document_point(QPoint(180, 140)));
  const auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->contains(QPoint(130, 100)));
  CHECK(canvas->selected_document_region().contains(QPoint(130, 100)));
  CHECK(!canvas->selected_document_region().contains(QPoint(82, 62)));
  CHECK(!canvas->selected_document_region().contains(QPoint(178, 62)));
  save_widget_artifact("ui_elliptical_marquee", *canvas);
}

void ui_marquee_space_drag_repositions_active_rect() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  canvas->set_snap_enabled(false);

  const auto start = canvas->widget_position_for_document_point(QPoint(40, 40));
  const auto first_corner = canvas->widget_position_for_document_point(QPoint(100, 80));
  const auto moved_corner = canvas->widget_position_for_document_point(QPoint(130, 110));
  const auto resized_corner = canvas->widget_position_for_document_point(QPoint(150, 140));

  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, first_corner, Qt::NoButton, Qt::LeftButton);
  const auto original = canvas->selected_document_rect();
  CHECK(original.has_value());

  send_key_press(*canvas, Qt::Key_Space);
  send_mouse(*canvas, QEvent::MouseMove, moved_corner, Qt::NoButton, Qt::LeftButton);
  const auto moved = canvas->selected_document_rect();
  CHECK(moved.has_value());
  CHECK(moved->size() == original->size());
  CHECK(moved->topLeft() == original->topLeft() + QPoint(30, 30));
  CHECK(canvas->selected_document_region().contains(QPoint(80, 75)));
  CHECK(!canvas->selected_document_region().contains(QPoint(50, 45)));

  send_key_release(*canvas, Qt::Key_Space);
  send_mouse(*canvas, QEvent::MouseMove, resized_corner, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, resized_corner, Qt::LeftButton, Qt::NoButton);
  const auto resized = canvas->selected_document_rect();
  CHECK(resized.has_value());
  CHECK(resized->topLeft() == moved->topLeft());
  CHECK(resized->width() > moved->width());
  CHECK(resized->height() > moved->height());
  save_widget_artifact("ui_marquee_space_drag_reposition", *canvas);
}

void ui_rulers_grid_guides_render_and_edit() {
  patchy::Document document(96, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(96, 64, patchy::PixelFormat::rgb8(), Qt::white));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(360, 260);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_rulers_visible(true);
  canvas.set_grid_visible(true);
  canvas.set_guides_visible(true);
  canvas.set_grid_subdivisions(4);
  canvas.set_snap_enabled(false);
  CHECK(canvas.guide_color() == QColor(255, 70, 180, 230));
  CHECK(!color_close(canvas.guide_color(), canvas.grid_color(), 48));
  canvas.add_guide(patchy::GuideOrientation::Vertical, 20 * 32);
  canvas.add_guide(patchy::GuideOrientation::Horizontal, 30 * 32);
  canvas.show();
  QApplication::processEvents();
  CHECK(document.guides().size() == 2);
  save_widget_artifact("ui_rulers_grid_guides", canvas);

  const QPoint ruler_start(canvas.widget_position_for_document_point(QPoint(42, 0)).x(), 12);
  const auto new_guide_target = canvas.widget_position_for_document_point(QPoint(42, 18));
  send_mouse(canvas, QEvent::MouseButtonPress, ruler_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, new_guide_target, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, new_guide_target, Qt::LeftButton, Qt::NoButton);
  CHECK(document.guides().size() == 3);
  CHECK(document.guides().back().orientation == patchy::GuideOrientation::Horizontal);
  CHECK(std::abs(document.guides().back().position_32 - 18 * 32) <= 1);

  const QPoint left_ruler_start(12, canvas.widget_position_for_document_point(QPoint(0, 22)).y());
  const auto vertical_guide_target = canvas.widget_position_for_document_point(QPoint(42, 22));
  send_mouse(canvas, QEvent::MouseButtonPress, left_ruler_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, vertical_guide_target, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, vertical_guide_target, Qt::LeftButton, Qt::NoButton);
  CHECK(document.guides().size() == 4);
  CHECK(document.guides().back().orientation == patchy::GuideOrientation::Vertical);
  CHECK(std::abs(document.guides().back().position_32 - 42 * 32) <= 1);
  CHECK(canvas.has_selected_guides());

  canvas.set_tool(patchy::ui::CanvasTool::Marquee);
  drag(canvas, canvas.widget_position_for_document_point(QPoint(55, 8)),
       canvas.widget_position_for_document_point(QPoint(70, 18)));
  CHECK(!canvas.has_selected_guides());
  canvas.clear_selection();

  canvas.set_tool(patchy::ui::CanvasTool::Move);
  const auto move_start = canvas.widget_position_for_document_point(QPoint(20, 50));
  const auto move_end = canvas.widget_position_for_document_point(QPoint(25, 50));
  send_mouse(canvas, QEvent::MouseButtonPress, move_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, move_end, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, move_end, Qt::LeftButton, Qt::NoButton);
  CHECK(std::abs(document.guides()[0].position_32 - 25 * 32) <= 1);

  canvas.set_guides_locked(true);
  canvas.clear_selected_guides();
  CHECK(document.guides().size() == 4);

  canvas.set_guides_locked(false);
  canvas.clear_selected_guides();
  CHECK(document.guides().size() == 3);
  save_widget_artifact("ui_guides_editing", canvas);
}

void ui_deep_zoom_pixel_grid_matches_rendered_pixels() {
  patchy::Document document(24, 12, patchy::PixelFormat::rgb8());
  auto pixels = solid_pixels(24, 12, patchy::PixelFormat::rgb8(), Qt::white);
  fill_pixel_rect(pixels, QRect(3, 3, 1, 1), QColor(230, 20, 45));
  fill_pixel_rect(pixels, QRect(13, 3, 1, 1), QColor(230, 20, 45));
  document.add_pixel_layer("Background", std::move(pixels));
  document.grid_settings().horizontal_cycle_32 = 32;
  document.grid_settings().vertical_cycle_32 = 32;

  patchy::ui::CanvasWidget canvas;
  canvas.resize(900, 360);
  canvas.set_document(&document);
  canvas.set_zoom(32.0);
  canvas.zoom_at_widget_point(QPointF(231.4, 181.7), 1.85);
  CHECK(canvas.zoom() > 32.0);
  CHECK(canvas.zoom() < 64.0);
  canvas.set_grid_visible(true);
  canvas.set_grid_subdivisions(1);
  canvas.set_grid_style(0);
  canvas.set_grid_color(QColor(78, 154, 255, 180));
  canvas.show();
  QApplication::processEvents();

  const auto cell_center = [&canvas](QPoint document_point) {
    const auto a = canvas.widget_position_for_document_point(document_point);
    const auto b = canvas.widget_position_for_document_point(document_point + QPoint(1, 1));
    return QPoint((a.x() + b.x()) / 2, (a.y() + b.y()) / 2);
  };
  const auto image = canvas.grab().toImage();
  CHECK(color_close(image.pixelColor(cell_center(QPoint(3, 3))), QColor(230, 20, 45), 24));
  CHECK(color_close(image.pixelColor(cell_center(QPoint(13, 3))), QColor(230, 20, 45), 24));
  CHECK(color_close(image.pixelColor(cell_center(QPoint(4, 3))), Qt::white, 18));

  auto strongest_grid_column = [&image](int expected_x, int sample_y) {
    int strongest_x = expected_x;
    int strongest_delta = std::numeric_limits<int>::min();
    for (int x = expected_x - 1; x <= expected_x + 1; ++x) {
      if (!image.rect().contains(QPoint(x, sample_y))) {
        continue;
      }
      const auto color = image.pixelColor(x, sample_y);
      const auto delta = color.blue() - ((color.red() + color.green()) / 2);
      if (delta > strongest_delta) {
        strongest_delta = delta;
        strongest_x = x;
      }
    }
    return std::pair<int, int>{strongest_x, strongest_delta};
  };

  const auto assert_red_cell_matches_grid = [&](QPoint document_point) {
    const auto top_left = canvas.widget_position_for_document_point(document_point);
    const auto bottom_right = canvas.widget_position_for_document_point(document_point + QPoint(1, 1));
    const QRect expected_cell(top_left, QSize(bottom_right.x() - top_left.x(), bottom_right.y() - top_left.y()));
    CHECK(expected_cell.width() >= 58);
    CHECK(expected_cell.height() >= 58);

    const auto sample_y = cell_center(QPoint(document_point.x(), 5)).y();
    const auto [left_grid_x, left_grid_delta] = strongest_grid_column(expected_cell.left(), sample_y);
    const auto [right_grid_x, right_grid_delta] = strongest_grid_column(bottom_right.x(), sample_y);
    CHECK(std::abs(left_grid_x - expected_cell.left()) <= 1);
    CHECK(std::abs(right_grid_x - bottom_right.x()) <= 1);
    CHECK(left_grid_delta > 20);
    CHECK(right_grid_delta > 20);

    int min_x = image.width();
    int min_y = image.height();
    int max_x = -1;
    int max_y = -1;
    const auto search_rect = expected_cell.adjusted(-3, -3, 3, 3).intersected(image.rect());
    for (int y = search_rect.top(); y <= search_rect.bottom(); ++y) {
      for (int x = search_rect.left(); x <= search_rect.right(); ++x) {
        const auto color = image.pixelColor(x, y);
        if (color.red() > 170 && color.green() < 80 && color.blue() < 100) {
          min_x = std::min(min_x, x);
          min_y = std::min(min_y, y);
          max_x = std::max(max_x, x);
          max_y = std::max(max_y, y);
        }
      }
    }
    CHECK(min_x >= expected_cell.left());
    CHECK(min_x <= expected_cell.left() + 2);
    CHECK(min_y >= expected_cell.top());
    CHECK(min_y <= expected_cell.top() + 2);
    CHECK(max_x <= expected_cell.right());
    CHECK(max_x >= expected_cell.right() - 2);
    CHECK(max_y <= expected_cell.bottom());
    CHECK(max_y >= expected_cell.bottom() - 2);
  };
  assert_red_cell_matches_grid(QPoint(3, 3));
  assert_red_cell_matches_grid(QPoint(13, 3));
}

void ui_deep_zoom_one_pixel_brush_marks_match_pixel_grid() {
  patchy::Document document(32, 16, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(32, 16, patchy::PixelFormat::rgb8(), Qt::white));
  auto& paint_layer = document.add_pixel_layer("Paint",
                                               solid_pixels(32, 16, patchy::PixelFormat::rgba8(), Qt::transparent));
  document.set_active_layer(paint_layer.id());
  document.grid_settings().horizontal_cycle_32 = 16 * 32;
  document.grid_settings().vertical_cycle_32 = 16 * 32;

  patchy::ui::CanvasWidget canvas;
  canvas.resize(900, 420);
  canvas.set_document(&document);
  canvas.set_zoom(32.0);
  canvas.zoom_at_widget_point(QPointF(286.35, 190.65), 1.25);
  canvas.set_grid_visible(true);
  canvas.set_grid_subdivisions(16);
  canvas.set_grid_style(0);
  canvas.set_grid_color(QColor(78, 154, 255, 180));
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(1);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(75);
  canvas.show();
  QApplication::processEvents();

  const auto cell_center = [&canvas](QPoint document_point) {
    const auto a = canvas.widget_position_for_document_point(document_point);
    const auto b = canvas.widget_position_for_document_point(document_point + QPoint(1, 1));
    return QPoint((a.x() + b.x()) / 2, (a.y() + b.y()) / 2);
  };

  const std::array<QPoint, 3> marks{QPoint(3, 4), QPoint(9, 4), QPoint(17, 4)};
  for (const auto mark : marks) {
    const auto point = cell_center(mark);
    send_mouse(canvas, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
  }
  QApplication::processEvents();

  for (const auto mark : marks) {
    CHECK(paint_layer.pixels().pixel(mark.x(), mark.y())[3] == 255);
    CHECK(paint_layer.pixels().pixel(mark.x() + 1, mark.y())[3] == 0);
  }

  const auto image = canvas.grab().toImage();
  const auto grid_strength = [&image](int x, int y) {
    if (!image.rect().contains(QPoint(x, y))) {
      return std::numeric_limits<int>::min();
    }
    const auto color = image.pixelColor(x, y);
    return color.blue() - ((color.red() + color.green()) / 2);
  };
  const auto strongest_grid_column = [&](int expected_x, int sample_y) {
    int strongest_x = expected_x;
    int strongest_delta = std::numeric_limits<int>::min();
    for (int x = expected_x - 1; x <= expected_x + 1; ++x) {
      const auto delta = grid_strength(x, sample_y);
      if (delta > strongest_delta) {
        strongest_delta = delta;
        strongest_x = x;
      }
    }
    return std::pair<int, int>{strongest_x, strongest_delta};
  };

  const auto sample_y = cell_center(QPoint(6, 8)).y();
  for (const auto mark : marks) {
    const auto top_left = canvas.widget_position_for_document_point(mark);
    const auto bottom_right = canvas.widget_position_for_document_point(mark + QPoint(1, 1));
    const QRect expected_cell(top_left, QSize(bottom_right.x() - top_left.x(), bottom_right.y() - top_left.y()));
    CHECK(expected_cell.width() >= 38);
    CHECK(expected_cell.height() >= 38);

    const auto [left_grid_x, left_grid_delta] = strongest_grid_column(expected_cell.left(), sample_y);
    const auto [right_grid_x, right_grid_delta] = strongest_grid_column(bottom_right.x(), sample_y);
    CHECK(std::abs(left_grid_x - expected_cell.left()) <= 1);
    CHECK(std::abs(right_grid_x - bottom_right.x()) <= 1);
    CHECK(left_grid_delta > 20);
    CHECK(right_grid_delta > 20);

    int min_x = image.width();
    int min_y = image.height();
    int max_x = -1;
    int max_y = -1;
    const auto search_rect = expected_cell.adjusted(-2, -2, 2, 2).intersected(image.rect());
    for (int y = search_rect.top(); y <= search_rect.bottom(); ++y) {
      for (int x = search_rect.left(); x <= search_rect.right(); ++x) {
        const auto color = image.pixelColor(x, y);
        if (color.red() < 40 && color.green() < 40 && color.blue() < 40) {
          min_x = std::min(min_x, x);
          min_y = std::min(min_y, y);
          max_x = std::max(max_x, x);
          max_y = std::max(max_y, y);
        }
      }
    }
    CHECK(min_x >= expected_cell.left());
    CHECK(min_x <= expected_cell.left() + 2);
    CHECK(min_y >= expected_cell.top());
    CHECK(min_y <= expected_cell.top() + 2);
    CHECK(max_x <= expected_cell.right());
    CHECK(max_x >= expected_cell.right() - 2);
    CHECK(max_y <= expected_cell.bottom());
    CHECK(max_y >= expected_cell.bottom() - 2);
  }
}

void ui_deep_zoom_subpixel_subdivisions_do_not_draw_inside_pixels() {
  patchy::Document document(24, 12, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(24, 12, patchy::PixelFormat::rgb8(), Qt::white));
  document.grid_settings().horizontal_cycle_32 = 4 * 32;
  document.grid_settings().vertical_cycle_32 = 4 * 32;

  patchy::ui::CanvasWidget canvas;
  canvas.resize(700, 360);
  canvas.set_document(&document);
  canvas.set_zoom(40.0);
  canvas.set_grid_visible(true);
  canvas.set_grid_subdivisions(16);
  canvas.set_grid_style(0);
  canvas.set_grid_color(QColor(78, 154, 255, 220));
  canvas.show();
  QApplication::processEvents();

  const auto cell_center = [&canvas](QPoint document_point) {
    const auto a = canvas.widget_position_for_document_point(document_point);
    const auto b = canvas.widget_position_for_document_point(document_point + QPoint(1, 1));
    return QPoint((a.x() + b.x()) / 2, (a.y() + b.y()) / 2);
  };
  const auto image = canvas.grab().toImage();
  const auto grid_strength = [&image](int x, int y) {
    if (!image.rect().contains(QPoint(x, y))) {
      return std::numeric_limits<int>::min();
    }
    const auto color = image.pixelColor(x, y);
    return color.blue() - ((color.red() + color.green()) / 2);
  };

  const auto sample_y = cell_center(QPoint(6, 5)).y();
  for (int document_x = 4; document_x <= 8; ++document_x) {
    const auto left = canvas.widget_position_for_document_point(QPoint(document_x, 0)).x();
    const auto right = canvas.widget_position_for_document_point(QPoint(document_x + 1, 0)).x();
    CHECK(grid_strength(left, sample_y) > 20);
    CHECK(grid_strength(right, sample_y) > 20);
    for (int x = left + 3; x <= right - 3; ++x) {
      CHECK(grid_strength(x, sample_y) < 12);
    }
  }
}

void ui_deep_zoom_fractional_subdivision_spacing_stays_on_pixel_edges() {
  patchy::Document document(32, 16, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(32, 16, patchy::PixelFormat::rgb8(), Qt::white));
  document.grid_settings().horizontal_cycle_32 = 18 * 32;
  document.grid_settings().vertical_cycle_32 = 18 * 32;

  patchy::ui::CanvasWidget canvas;
  canvas.resize(900, 420);
  canvas.set_document(&document);
  canvas.set_zoom(40.0);
  canvas.set_grid_visible(true);
  canvas.set_grid_subdivisions(16);
  canvas.set_grid_style(0);
  canvas.set_grid_color(QColor(78, 154, 255, 220));
  canvas.show();
  QApplication::processEvents();

  const auto image = canvas.grab().toImage();
  const auto grid_strength = [&image](int x, int y) {
    if (!image.rect().contains(QPoint(x, y))) {
      return std::numeric_limits<int>::min();
    }
    const auto color = image.pixelColor(x, y);
    return color.blue() - ((color.red() + color.green()) / 2);
  };
  const auto sample_y = (canvas.widget_position_for_document_point(QPoint(0, 4)).y() +
                         canvas.widget_position_for_document_point(QPoint(0, 5)).y()) /
                        2;

  for (int document_x = 1; document_x <= 20; ++document_x) {
    const auto left = canvas.widget_position_for_document_point(QPoint(document_x, 0)).x();
    const auto right = canvas.widget_position_for_document_point(QPoint(document_x + 1, 0)).x();
    for (int x = left + 3; x <= right - 3; ++x) {
      CHECK(grid_strength(x, sample_y) < 12);
    }
  }
}

void ui_deep_zoom_grid_subdivision_counts_change_spacing() {
  patchy::Document document(32, 16, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(32, 16, patchy::PixelFormat::rgb8(), Qt::white));
  document.grid_settings().horizontal_cycle_32 = 16 * 32;
  document.grid_settings().vertical_cycle_32 = 16 * 32;

  patchy::ui::CanvasWidget canvas;
  canvas.resize(900, 420);
  canvas.set_document(&document);
  canvas.set_zoom(40.0);
  canvas.set_grid_visible(true);
  canvas.set_grid_style(0);
  canvas.set_grid_color(QColor(78, 154, 255, 220));
  canvas.show();
  QApplication::processEvents();

  const auto grid_strength = [](const QImage& image, int x, int y) {
    if (!image.rect().contains(QPoint(x, y))) {
      return std::numeric_limits<int>::min();
    }
    const auto color = image.pixelColor(x, y);
    return color.blue() - ((color.red() + color.green()) / 2);
  };
  const auto vertical_line_score = [&](const QImage& image, int document_x) {
    const auto x = canvas.widget_position_for_document_point(QPoint(document_x, 0)).x();
    const auto top = canvas.widget_position_for_document_point(QPoint(0, 2)).y();
    const auto bottom = canvas.widget_position_for_document_point(QPoint(0, 10)).y();
    int score = 0;
    for (int y = top; y <= bottom; y += 2) {
      const auto center = grid_strength(image, x, y);
      const auto side = std::max(grid_strength(image, x - 3, y), grid_strength(image, x + 3, y));
      if (center > 12 && center > side + 8) {
        ++score;
      }
    }
    return score;
  };
  const auto grab_for_subdivisions = [&](int subdivisions) {
    canvas.set_grid_subdivisions(subdivisions);
    QApplication::processEvents();
    return canvas.grab().toImage();
  };

  const auto one = grab_for_subdivisions(1);
  CHECK(vertical_line_score(one, 4) <= 2);
  CHECK(vertical_line_score(one, 8) <= 2);
  CHECK(vertical_line_score(one, 16) > 8);

  const auto two = grab_for_subdivisions(2);
  CHECK(vertical_line_score(two, 4) <= 2);
  CHECK(vertical_line_score(two, 8) > 4);

  const auto four = grab_for_subdivisions(4);
  CHECK(vertical_line_score(four, 4) > 4);

  const auto sixteen = grab_for_subdivisions(16);
  CHECK(vertical_line_score(sixteen, 1) > 8);
}

void ui_snap_marquee_uses_screen_pixel_tolerance_and_target_toggles() {
  patchy::Document document(96, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(96, 64, patchy::PixelFormat::rgb8(), Qt::white));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(420, 300);
  canvas.set_document(&document);
  canvas.set_zoom(1.0);
  canvas.set_tool(patchy::ui::CanvasTool::Marquee);
  canvas.set_guides_visible(false);
  canvas.set_snap_enabled(true);
  canvas.set_snap_to_guides(true);
  canvas.set_snap_to_grid(false);
  canvas.set_snap_to_document(false);
  canvas.set_snap_to_layers(false);
  canvas.set_snap_to_selection(false);
  canvas.add_guide(patchy::GuideOrientation::Vertical, 20 * 32);
  canvas.add_guide(patchy::GuideOrientation::Horizontal, 30 * 32);
  canvas.show();
  QApplication::processEvents();

  drag(canvas, canvas.widget_position_for_document_point(QPoint(13, 10)),
       canvas.widget_position_for_document_point(QPoint(36, 28)));
  auto selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->left() == 20);

  canvas.clear_selection();
  drag(canvas, canvas.widget_position_for_document_point(QPoint(5, 10)),
       canvas.widget_position_for_document_point(QPoint(18, 28)));
  selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->left() == 5);
  CHECK(selection->right() + 1 == 20);

  canvas.clear_selection();
  drag(canvas, canvas.widget_position_for_document_point(QPoint(20, 5)),
       canvas.widget_position_for_document_point(QPoint(36, 18)));
  selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->left() == 20);
  CHECK(document.guides().front().position_32 == 20 * 32);

  canvas.clear_selection();
  const auto space_start = canvas.widget_position_for_document_point(QPoint(5, 45));
  const auto space_initial = canvas.widget_position_for_document_point(QPoint(10, 55));
  const auto space_moved = canvas.widget_position_for_document_point(QPoint(19, 55));
  const auto space_released = canvas.widget_position_for_document_point(QPoint(29, 55));
  send_mouse(canvas, QEvent::MouseButtonPress, space_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, space_initial, Qt::NoButton, Qt::LeftButton);
  send_key_press(canvas, Qt::Key_Space);
  send_mouse(canvas, QEvent::MouseMove, space_moved, Qt::NoButton, Qt::LeftButton);
  selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->left() == 14);
  CHECK(selection->right() + 1 == 20);
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(21, 55)), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(23, 55)), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(25, 55)), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(27, 55)), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, space_released, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, space_released, Qt::LeftButton, Qt::NoButton);
  send_key_release(canvas, Qt::Key_Space);
  selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->width() == 6);
  CHECK(selection->left() > 14);
  CHECK(selection->right() + 1 > 20);

  canvas.clear_selection();
  canvas.set_zoom(4.0);
  drag(canvas, canvas.widget_position_for_document_point(QPoint(17, 10)),
       canvas.widget_position_for_document_point(QPoint(36, 28)));
  selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->left() == 17);

  canvas.clear_selection();
  drag(canvas, canvas.widget_position_for_document_point(QPoint(18, 10)),
       canvas.widget_position_for_document_point(QPoint(36, 28)));
  selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->left() == 20);

  canvas.clear_selection();
  canvas.set_snap_to_guides(false);
  drag(canvas, canvas.widget_position_for_document_point(QPoint(19, 10)),
       canvas.widget_position_for_document_point(QPoint(36, 28)));
  selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->left() == 19);

  canvas.clear_selection();
  canvas.set_snap_to_guides(true);
  canvas.set_zoom(1.0);
  drag(canvas, canvas.widget_position_for_document_point(QPoint(10, 23)),
       canvas.widget_position_for_document_point(QPoint(36, 50)));
  selection = canvas.selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->top() == 30);
  save_widget_artifact("ui_snapped_marquee_guides", canvas);
}

void ui_snap_applies_to_shape_text_and_move_tools() {
  patchy::Document document(96, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(96, 64, patchy::PixelFormat::rgb8(), Qt::white));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(420, 300);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_snap_enabled(true);
  canvas.set_guides_visible(false);
  canvas.set_snap_to_guides(true);
  canvas.set_snap_to_grid(false);
  canvas.set_snap_to_document(false);
  canvas.set_snap_to_layers(false);
  canvas.set_snap_to_selection(false);
  canvas.add_guide(patchy::GuideOrientation::Vertical, 20 * 32);
  canvas.set_tool(patchy::ui::CanvasTool::Rectangle);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(1);
  canvas.show();
  QApplication::processEvents();

  drag(canvas, canvas.widget_position_for_document_point(QPoint(19, 10)),
       canvas.widget_position_for_document_point(QPoint(44, 28)));
  const auto shape_bounds = dark_document_bounds(canvas, QRect(0, 0, 96, 64));
  CHECK(shape_bounds.has_value());
  CHECK(shape_bounds->left() == 20);

  QPoint requested_point;
  QRect requested_box;
  canvas.set_text_requested_callback([&](QPoint point, QRect box) {
    requested_point = point;
    requested_box = box;
  });
  canvas.set_tool(patchy::ui::CanvasTool::Text);
  drag(canvas, canvas.widget_position_for_document_point(QPoint(19, 36)),
       canvas.widget_position_for_document_point(QPoint(48, 56)));
  CHECK(requested_box.isValid());
  CHECK(requested_box.left() == 20);
  CHECK(requested_point.x() == 20);

  patchy::Document move_document(96, 64, patchy::PixelFormat::rgba8());
  auto move_pixels = solid_pixels(8, 8, patchy::PixelFormat::rgba8(), Qt::black);
  patchy::Layer move_layer(move_document.allocate_layer_id(), "Move", std::move(move_pixels));
  const auto move_id = move_layer.id();
  move_layer.set_bounds(patchy::Rect{10, 10, 8, 8});
  move_document.add_layer(std::move(move_layer));

  patchy::ui::CanvasWidget move_canvas;
  move_canvas.resize(420, 300);
  move_canvas.set_document(&move_document);
  move_canvas.set_zoom(2.0);
  move_canvas.set_tool(patchy::ui::CanvasTool::Move);
  move_canvas.set_show_transform_controls(false);
  move_canvas.set_auto_select_layer(false);
  move_canvas.set_selected_layer_ids({move_id});
  move_canvas.set_snap_enabled(true);
  move_canvas.set_snap_to_guides(true);
  move_canvas.set_snap_to_grid(false);
  move_canvas.set_snap_to_document(false);
  move_canvas.set_snap_to_layers(false);
  move_canvas.set_snap_to_selection(false);
  move_canvas.add_guide(patchy::GuideOrientation::Vertical, 25 * 32);
  move_canvas.show();
  QApplication::processEvents();
  drag(move_canvas, move_canvas.widget_position_for_document_point(QPoint(12, 12)),
       move_canvas.widget_position_for_document_point(QPoint(26, 12)));
  const auto* moved = move_document.find_layer(move_id);
  CHECK(moved != nullptr);
  CHECK(moved->bounds().x == 25);
  save_widget_artifact("ui_snap_shape_text_move", canvas);
}

void ui_canvas_aid_preferences_and_guide_dialogs_work() {
  SettingsValueRestorer restore_rulers(QStringLiteral("view/rulersVisible"));
  SettingsValueRestorer restore_grid(QStringLiteral("view/gridVisible"));
  SettingsValueRestorer restore_guides(QStringLiteral("view/guidesVisible"));
  SettingsValueRestorer restore_lock(QStringLiteral("view/guidesLocked"));
  SettingsValueRestorer restore_snap(QStringLiteral("view/snapEnabled"));
  SettingsValueRestorer restore_snap_guides(QStringLiteral("view/snapToGuides"));
  SettingsValueRestorer restore_snap_grid(QStringLiteral("view/snapToGrid"));
  SettingsValueRestorer restore_snap_document(QStringLiteral("view/snapToDocument"));
  SettingsValueRestorer restore_snap_layers(QStringLiteral("view/snapToLayers"));
  SettingsValueRestorer restore_snap_selection(QStringLiteral("view/snapToSelection"));
  SettingsValueRestorer restore_spacing(QStringLiteral("view/gridSpacing32"));
  SettingsValueRestorer restore_subdivisions(QStringLiteral("view/gridSubdivisions"));
  SettingsValueRestorer restore_style(QStringLiteral("view/gridStyle"));
  SettingsValueRestorer restore_grid_color(QStringLiteral("view/gridColor"));
  SettingsValueRestorer restore_guide_color(QStringLiteral("view/guideColor"));
  SettingsValueRestorer restore_guide_color_migration(QStringLiteral("view/guideColorDefaultMigrated"));
  SettingsValueRestorer restore_units(QStringLiteral("view/rulerUnits"));

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  bool saw_preferences = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* tabs = dialog->findChild<QTabWidget*>(QStringLiteral("preferencesTabWidget"));
    CHECK(tabs != nullptr);
    CHECK(tabs->count() == 3);
    CHECK(tabs->tabText(1) == QStringLiteral("Grid and Guides"));
    CHECK(tabs->tabText(2) == QStringLiteral("Snapping"));
    auto* grid_color_button = dialog->findChild<QPushButton*>(QStringLiteral("preferencesGridColorButton"));
    CHECK(grid_color_button != nullptr);
    CHECK(grid_color_button->text().contains(QStringLiteral("#")));
    CHECK(grid_color_button->text().contains(QStringLiteral("%")));
    auto* overlay_preview = dialog->findChild<QLabel*>(QStringLiteral("preferencesGridOverlayPreview"));
    CHECK(overlay_preview != nullptr);
    CHECK(overlay_preview->width() >= 200);
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesShowRulersCheck"))->setChecked(true);
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesShowGridCheck"))->setChecked(true);
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesShowGuidesCheck"))->setChecked(true);
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesLockGuidesCheck"))->setChecked(false);
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesSnapCheck"))->setChecked(false);
    dialog->findChild<QDoubleSpinBox*>(QStringLiteral("preferencesGridSpacingSpin"))->setValue(32.0);
    dialog->findChild<QSpinBox*>(QStringLiteral("preferencesGridSubdivisionsSpin"))->setValue(8);
    dialog->findChild<QComboBox*>(QStringLiteral("preferencesGridStyleCombo"))->setCurrentIndex(1);
    dialog->findChild<QCheckBox*>(QStringLiteral("preferencesSnapGridCheck"))->setChecked(false);
    saw_preferences = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_preferences);
  CHECK(canvas->rulers_visible());
  CHECK(canvas->grid_visible());
  CHECK(canvas->guides_visible());
  CHECK(!canvas->snap_enabled());
  CHECK(canvas->grid_subdivisions() == 8);
  CHECK(canvas->grid_style() == 1);
  CHECK(require_action(window, "viewToggleRulersAction")->isChecked());
  CHECK(require_action(window, "viewToggleGridAction")->isChecked());
  CHECK(!require_action(window, "viewToggleSnapAction")->isChecked());
  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("view/gridSpacing32")).toInt() == 1024);
  CHECK(settings.value(QStringLiteral("view/gridSubdivisions")).toInt() == 8);

  bool saw_new_guide = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("newGuideDialog"));
    CHECK(dialog != nullptr);
    dialog->findChild<QComboBox*>(QStringLiteral("newGuideOrientationCombo"))->setCurrentIndex(0);
    dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newGuidePositionSpin"))->setValue(24.0);
    saw_new_guide = true;
    dialog->accept();
  });
  require_action(window, "viewNewGuideAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_new_guide);
  CHECK(canvas->has_selected_guides());

  bool saw_layout = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("newGuideLayoutDialog"));
    CHECK(dialog != nullptr);
    dialog->findChild<QSpinBox*>(QStringLiteral("newGuideLayoutColumnsSpin"))->setValue(3);
    dialog->findChild<QSpinBox*>(QStringLiteral("newGuideLayoutRowsSpin"))->setValue(2);
    saw_layout = true;
    dialog->accept();
  });
  require_action(window, "viewNewGuideLayoutAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_layout);
  save_widget_artifact("ui_grid_guides_preferences_dialogs", window);
}

void ui_complex_selection_draws_region_outline() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(160, 90)));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 90)),
       canvas->widget_position_for_document_point(QPoint(160, 210)), Qt::ShiftModifier);
  QApplication::processEvents();

  const auto& selection = canvas->selected_document_region();
  CHECK(selection.contains(QPoint(30, 30)));
  CHECK(selection.contains(QPoint(120, 150)));
  CHECK(!selection.contains(QPoint(40, 150)));

  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  int inner_outline_pixels = 0;
  for (int document_y = 96; document_y <= 204; ++document_y) {
    const auto boundary = canvas->widget_position_for_document_point(QPoint(80, document_y));
    bool found_dark_ant_pixel = false;
    for (int dx = -1; dx <= 1; ++dx) {
      const auto sample = image.pixelColor(boundary + QPoint(dx, 0));
      if (sample.red() < 70 && sample.green() < 70 && sample.blue() < 70) {
        found_dark_ant_pixel = true;
      }
    }
    if (found_dark_ant_pixel) {
      ++inner_outline_pixels;
    }
  }
  CHECK(inner_outline_pixels >= 8);
  save_widget_artifact("ui_complex_selection_outline", *canvas);
}

void ui_ctrl_h_hides_selection_edges_without_blue_tint() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(90, 80)),
       canvas->widget_position_for_document_point(QPoint(190, 155)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  CHECK(canvas->selection_edges_visible());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(130, 115)), Qt::white, 2));

  const auto count_black_edge_pixels = [&canvas] {
    const auto image = canvas->grab().toImage();
    const auto left = canvas->widget_position_for_document_point(QPoint(90, 80));
    const auto right = canvas->widget_position_for_document_point(QPoint(190, 80));
    int pixels = 0;
    for (int y = left.y() - 2; y <= left.y() + 2; ++y) {
      for (int x = left.x(); x <= right.x(); ++x) {
        if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
          continue;
        }
        const auto color = image.pixelColor(x, y);
        if (color.red() < 70 && color.green() < 70 && color.blue() < 70) {
          ++pixels;
        }
      }
    }
    return pixels;
  };

  CHECK(count_black_edge_pixels() > 4);
  save_widget_artifact("ui_selection_edges_visible_no_tint", *canvas);

  send_key(*canvas, Qt::Key_H, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(!canvas->selection_edges_visible());
  CHECK(canvas->has_selection());
  CHECK(count_black_edge_pixels() == 0);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(130, 115)), Qt::white, 2));
  save_widget_artifact("ui_selection_edges_hidden", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  CHECK(canvas->selection_edges_visible());

  send_key(*canvas, Qt::Key_H, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(!canvas->selection_edges_visible());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(30, 30)),
       canvas->widget_position_for_document_point(QPoint(80, 70)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  CHECK(canvas->selection_edges_visible());
}

void ui_select_inverse_and_extended_blend_modes_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  const QPoint inside(35, 35);
  const QPoint outside(150, 150);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(70, 70)));
  CHECK(canvas->selected_document_region().contains(inside));
  CHECK(!canvas->selected_document_region().contains(outside));
  require_action(window, "selectInverseAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->selected_document_region().contains(inside));
  CHECK(canvas->selected_document_region().contains(outside));
  save_widget_artifact("ui_select_inverse", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  require_action(window, "selectReselectAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(outside));
  CHECK(!canvas->selected_document_region().contains(inside));
  save_widget_artifact("ui_select_reselect", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  auto* blend_combo = window.findChild<QComboBox*>(QStringLiteral("layerBlendModeCombo"));
  CHECK(blend_combo != nullptr);
  canvas->set_primary_color(QColor(255, 0, 0));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  const auto difference_index = blend_combo->findText(QStringLiteral("Difference"));
  CHECK(difference_index >= 0);
  blend_combo->setCurrentIndex(difference_index);
  QApplication::processEvents();

  const auto sample = canvas_pixel(*canvas, QPoint(30, 30));
  CHECK(sample.red() < 40);
  CHECK(sample.green() > 220);
  CHECK(sample.blue() > 220);
  save_widget_artifact("ui_extended_blend_modes", window);
}

void ui_selection_expand_contract_and_layer_transparency_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(100, 100)));
  canvas->set_primary_color(QColor(40, 180, 255));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  require_action(window, "selectLayerTransparencyAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(70, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(42, 50)));
  CHECK(!canvas->selected_document_region().contains(QPoint(34, 50)));

  canvas->contract_selection(8);
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(70, 70)));
  CHECK(!canvas->selected_document_region().contains(QPoint(42, 50)));

  canvas->expand_selection(6);
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(44, 50)));
  CHECK(!canvas->selected_document_region().contains(QPoint(30, 50)));

  require_action(window, "selectInverseAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->selected_document_region().contains(QPoint(70, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(30, 50)));
  save_widget_artifact("ui_selection_expand_contract_transparency", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(160, 40)),
       canvas->widget_position_for_document_point(QPoint(230, 110)));
  canvas->border_selection(7);
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(162, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(226, 70)));
  CHECK(!canvas->selected_document_region().contains(QPoint(195, 75)));
  CHECK(!canvas->selected_document_region().contains(QPoint(148, 70)));
  save_widget_artifact("ui_selection_border", *canvas);
}

void ui_ctrl_click_layer_loads_layer_transparency() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(55, 45)),
       canvas->widget_position_for_document_point(QPoint(120, 95)));
  canvas->set_primary_color(QColor(20, 130, 230));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());

  QListWidgetItem* paint_layer_item = nullptr;
  for (int row = 0; row < layer_list->count(); ++row) {
    if (layer_list->item(row)->text() == QStringLiteral("Paint Layer")) {
      paint_layer_item = layer_list->item(row);
      break;
    }
  }
  CHECK(paint_layer_item != nullptr);
  auto* paint_layer_row = layer_list->itemWidget(paint_layer_item);
  CHECK(paint_layer_row != nullptr);
  auto* visibility = paint_layer_row->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(visibility != nullptr);
  const auto check_state_before = paint_layer_item->checkState();
  send_mouse(*visibility, QEvent::MouseButtonPress, visibility->rect().center(), Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*visibility, QEvent::MouseButtonRelease, visibility->rect().center(), Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  QApplication::processEvents();

  CHECK(!canvas->has_selection());
  CHECK(paint_layer_item->checkState() == check_state_before);

  auto* thumbnail = paint_layer_row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(thumbnail != nullptr);
  send_mouse(*thumbnail, QEvent::MouseButtonPress, thumbnail->rect().center(), Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*thumbnail, QEvent::MouseButtonRelease, thumbnail->rect().center(), Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  QApplication::processEvents();

  CHECK(canvas->selected_document_region().contains(QPoint(70, 60)));
  CHECK(!canvas->selected_document_region().contains(QPoint(30, 60)));
  CHECK(paint_layer_item->checkState() == check_state_before);
  save_widget_artifact("ui_ctrl_click_layer_transparency", window);
}

void ui_select_grow_and_similar_use_magic_wand_tolerance() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  canvas->set_snap_enabled(false);
  canvas->set_wand_tolerance(8);

  canvas->set_primary_color(QColor(220, 20, 40));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(70, 70)));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(140, 20)),
       canvas->widget_position_for_document_point(QPoint(190, 70)));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  canvas->set_primary_color(QColor(30, 80, 230));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 140)),
       canvas->widget_position_for_document_point(QPoint(70, 190)));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(34, 34)),
       canvas->widget_position_for_document_point(QPoint(40, 40)));
  CHECK(canvas->selected_document_region().contains(QPoint(36, 36)));
  CHECK(!canvas->selected_document_region().contains(QPoint(66, 66)));

  require_action(window, "selectGrowAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(66, 66)));
  CHECK(!canvas->selected_document_region().contains(QPoint(150, 40)));
  CHECK(!canvas->selected_document_region().contains(QPoint(40, 150)));
  save_widget_artifact("ui_select_grow", *canvas);

  require_action(window, "selectSimilarAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(66, 66)));
  CHECK(canvas->selected_document_region().contains(QPoint(150, 40)));
  CHECK(!canvas->selected_document_region().contains(QPoint(40, 150)));
  CHECK(!canvas->selected_document_region().contains(QPoint(240, 40)));
  save_widget_artifact("ui_select_similar", *canvas);
}

void ui_complex_selection_stroke_uses_region_outline() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(24, 24)),
       canvas->widget_position_for_document_point(QPoint(72, 72)));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(132, 132)),
       canvas->widget_position_for_document_point(QPoint(180, 180)), Qt::ShiftModifier);
  CHECK(canvas->selected_document_region().contains(QPoint(40, 40)));
  CHECK(canvas->selected_document_region().contains(QPoint(150, 150)));
  CHECK(!canvas->selected_document_region().contains(QPoint(98, 98)));

  canvas->set_primary_color(QColor(20, 230, 90));
  canvas->set_brush_size(7);
  require_action(window, "editStrokeSelectionAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 45)), QColor(20, 230, 90), 55));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(132, 150)), QColor(20, 230, 90), 55));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(98, 98)), QColor(255, 255, 255), 8));
  save_widget_artifact("ui_complex_stroke_selection", *canvas);
}

void ui_layer_lock_transparency_and_keyboard_nudge_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* lock = window.findChild<QCheckBox*>(QStringLiteral("layerLockTransparencyCheck"));
  CHECK(lock != nullptr);

  canvas->set_primary_color(QColor(220, 20, 40));
  lock->setChecked(true);
  QApplication::processEvents();
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(255, 255, 255), 8));

  lock->setChecked(false);
  QApplication::processEvents();
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 20, 40), 8));

  canvas->set_primary_color(QColor(20, 90, 220));
  lock->setChecked(true);
  QApplication::processEvents();
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(20, 90, 220), 8));

  require_action(window, "layerClearAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(20, 90, 220), 8));
  save_widget_artifact("ui_layer_lock_transparency", window);

  canvas->setFocus();
  send_key(*canvas, Qt::Key_Right, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(5, 30)), QColor(255, 255, 255), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(20, 30)), QColor(20, 90, 220), 8));
  save_widget_artifact("ui_keyboard_nudge_layer", window);
}

void ui_layer_full_lock_row_control_blocks_edits_and_move() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  canvas->set_primary_color(QColor(220, 30, 40));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 30, 40), 8));

  auto* paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  auto* paint_row = layer_list->itemWidget(paint_item);
  CHECK(paint_row != nullptr);
  auto* lock = paint_row->findChild<QToolButton*>(QStringLiteral("layerLockCheck"));
  CHECK(lock != nullptr);
  CHECK(lock->text().isEmpty());
  CHECK(!lock->icon().isNull());
  lock->click();
  QApplication::processEvents();

  paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  paint_row = layer_list->itemWidget(paint_item);
  CHECK(paint_row != nullptr);
  lock = paint_row->findChild<QToolButton*>(QStringLiteral("layerLockCheck"));
  CHECK(lock != nullptr);
  CHECK(lock->isChecked());

  canvas->set_primary_color(QColor(20, 90, 220));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 30, 40), 8));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Unlock it before editing")));

  const auto before = canvas->active_layer_document_rect();
  CHECK(before.has_value());
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(false);
  canvas->set_show_transform_controls(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(30, 30));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(40, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(40, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->active_layer_document_rect() == before);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  save_widget_artifact("ui_layer_full_lock_controls", window);
}

void ui_folder_lock_inherits_to_child_layers() {
  patchy::Document document(80, 80, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(80, 80, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  auto child_pixels = solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(230, 40, 40, 255));
  patchy::Layer child(document.allocate_layer_id(), "Child", std::move(child_pixels));
  const auto child_id = child.id();
  child.set_bounds(patchy::Rect{10, 10, 20, 20});
  folder.add_child(std::move(child));
  document.add_layer(std::move(folder));
  document.set_active_layer(child_id);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Folder Lock"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  auto* folder_row = layer_list->itemWidget(folder_item);
  CHECK(folder_row != nullptr);
  auto* folder_lock = folder_row->findChild<QToolButton*>(QStringLiteral("layerLockCheck"));
  CHECK(folder_lock != nullptr);
  folder_lock->click();
  QApplication::processEvents();

  auto* child_item = require_layer_item(*layer_list, QStringLiteral("Child"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(child_item);
  child_item->setSelected(true);
  QApplication::processEvents();
  auto* child_row = layer_list->itemWidget(child_item);
  CHECK(child_row != nullptr);
  auto* child_lock = child_row->findChild<QToolButton*>(QStringLiteral("layerLockCheck"));
  CHECK(child_lock != nullptr);
  CHECK(child_lock->isChecked());
  CHECK(!child_lock->isEnabled());
  CHECK(child_lock->toolTip().contains(QStringLiteral("folder")));

  canvas->set_primary_color(QColor(20, 80, 220));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(15, 15)), QColor(230, 40, 40), 8));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Unlock it before editing")));
  save_widget_artifact("ui_folder_lock_inheritance", window);
}

void ui_move_auto_select_ignores_locked_layers() {
  patchy::Document document(80, 80, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(80, 80, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto bottom_pixels = solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(30, 90, 220, 255));
  patchy::Layer bottom(document.allocate_layer_id(), "Unlocked", std::move(bottom_pixels));
  bottom.set_bounds(patchy::Rect{16, 16, 24, 24});
  document.add_layer(std::move(bottom));
  auto top_pixels = solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(230, 40, 40, 255));
  patchy::Layer top(document.allocate_layer_id(), "Locked", std::move(top_pixels));
  top.set_bounds(patchy::Rect{16, 16, 24, 24});
  patchy::set_layer_locked(top, true);
  document.add_layer(std::move(top));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Locked Auto Select"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  const auto click = canvas->widget_position_for_document_point(QPoint(20, 20));
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* unlocked_item = require_layer_item(*layer_list, QStringLiteral("Unlocked"));
  CHECK(unlocked_item->isSelected());
  CHECK(layer_list->currentItem() == unlocked_item);
  save_widget_artifact("ui_move_auto_select_locked_layer", window);
}

void ui_lasso_selection_draws_freeform_region() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Lasso);

  const auto a = canvas->widget_position_for_document_point(QPoint(40, 40));
  const auto b = canvas->widget_position_for_document_point(QPoint(115, 42));
  const auto c = canvas->widget_position_for_document_point(QPoint(96, 105));
  const auto d = canvas->widget_position_for_document_point(QPoint(48, 112));
  send_mouse(*canvas, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, c, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, d, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(canvas->selected_document_region().contains(QPoint(70, 70)));
  CHECK(!canvas->selected_document_region().contains(QPoint(25, 25)));
  save_widget_artifact("ui_lasso_selection", *canvas);
}

void ui_copy_paste_and_transform_pasted_layer_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  canvas->set_primary_color(QColor(255, 80, 20));
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, QPoint(60, 60), QPoint(180, 140));
  const auto copied_selection_rect = canvas->selected_document_rect();
  CHECK(copied_selection_rect.has_value());
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  const auto layers_before = layer_list->count();
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);
  const auto pasted_rect = canvas->active_layer_document_rect();
  CHECK(pasted_rect.has_value());
  CHECK(pasted_rect->topLeft() == copied_selection_rect->topLeft());
  CHECK(pasted_rect->size() == copied_selection_rect->size());
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(false);
  drag(*canvas, QPoint(120, 100), QPoint(150, 130));
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);

  auto before_transform = canvas->active_layer_document_rect();
  CHECK(before_transform.has_value());
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto bottom_right =
      canvas->widget_position_for_document_point(QPoint(before_transform->x() + before_transform->width(),
                                                       before_transform->y() + before_transform->height()));
  drag(*canvas, bottom_right, bottom_right + QPoint(180, 40), Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  auto after_transform = canvas->active_layer_document_rect();
  CHECK(after_transform.has_value());
  CHECK(after_transform->width() > before_transform->width() + 50);
  const auto original_ratio = static_cast<double>(before_transform->width()) / before_transform->height();
  const auto transformed_ratio = static_cast<double>(after_transform->width()) / after_transform->height();
  CHECK(std::abs(original_ratio - transformed_ratio) < 0.2);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto top_center = canvas->widget_position_for_document_point(
      QPoint(after_transform->x() + after_transform->width() / 2, after_transform->y()));
  drag(*canvas, top_center + QPoint(0, -32), top_center + QPoint(80, 20));
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  auto after_rotate = canvas->active_layer_document_rect();
  CHECK(after_rotate.has_value());
  CHECK(after_rotate->width() >= after_transform->width());
  save_widget_artifact("ui_copy_paste_transform", window);
}

void ui_external_clipboard_image_paste_creates_centered_layer() {
  QApplication::clipboard()->clear();

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  QImage image(18, 12, QImage::Format_RGBA8888);
  image.fill(QColor(20, 180, 80, 255));
  QApplication::clipboard()->setImage(image);
  QApplication::processEvents();

  const auto layers_before = layer_list->count();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();

  CHECK(layer_list->count() == layers_before + 1);
  const auto pasted_rect = canvas->active_layer_document_rect();
  CHECK(pasted_rect.has_value());
  CHECK(pasted_rect->topLeft() == QPoint((1024 - image.width()) / 2, (768 - image.height()) / 2));
  CHECK(pasted_rect->size() == image.size());
  CHECK(color_close(canvas_pixel(*canvas, pasted_rect->center()), QColor(20, 180, 80), 35));
  QApplication::clipboard()->clear();
}

void ui_external_clipboard_image_paste_overrides_internal_payload() {
  QApplication::clipboard()->clear();

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();

  QImage image(20, 14, QImage::Format_RGBA8888);
  image.fill(QColor(220, 40, 140, 255));
  QApplication::clipboard()->setImage(image);
  QApplication::processEvents();

  const auto layers_before = layer_list->count();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();

  CHECK(layer_list->count() == layers_before + 1);
  const auto pasted_rect = canvas->active_layer_document_rect();
  CHECK(pasted_rect.has_value());
  CHECK(pasted_rect->topLeft() == QPoint((1024 - image.width()) / 2, (768 - image.height()) / 2));
  CHECK(pasted_rect->size() == image.size());
  CHECK(color_close(canvas_pixel(*canvas, pasted_rect->center()), QColor(220, 40, 140), 35));
  QApplication::clipboard()->clear();
}

void ui_free_transform_uses_opaque_pixel_bounds() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 70)),
       canvas->widget_position_for_document_point(QPoint(140, 115)));
  const auto filled_rect = canvas->selected_document_rect();
  CHECK(filled_rect.has_value());
  canvas->set_primary_color(QColor(230, 60, 35));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  const auto full_layer_rect = canvas->active_layer_document_rect();
  CHECK(full_layer_rect.has_value());
  CHECK(full_layer_rect->width() > 900);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  const auto handle = canvas->widget_position_for_document_point(filled_rect->bottomRight() + QPoint(1, 1));
  const auto expanded = canvas->widget_position_for_document_point(filled_rect->bottomRight() + QPoint(75, 55));
  drag(*canvas, handle, expanded, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  const auto transformed_rect = canvas->active_layer_document_rect();
  CHECK(transformed_rect.has_value());
  CHECK(transformed_rect->width() > filled_rect->width() + 20);
  CHECK(transformed_rect->height() > filled_rect->height() + 15);
  CHECK(transformed_rect->width() < 180);
  CHECK(transformed_rect->height() < 140);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  drag(*canvas, canvas->widget_position_for_document_point(transformed_rect->center()),
       canvas->widget_position_for_document_point(transformed_rect->center() + QPoint(40, 24)));
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == transformed_rect);
  save_widget_artifact("ui_transform_opaque_bounds", window);
}

void ui_transform_numeric_controls_apply_values() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(90, 80)),
       canvas->widget_position_for_document_point(QPoint(150, 125)));
  const auto filled_rect = canvas->selected_document_rect();
  CHECK(filled_rect.has_value());
  canvas->set_primary_color(QColor(40, 130, 230));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();

  auto* x = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformXSpin"));
  auto* y = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformYSpin"));
  auto* scale_x = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleXSpin"));
  auto* scale_y = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleYSpin"));
  auto* rotation = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformRotationSpin"));
  auto* interpolation = window.findChild<QComboBox*>(QStringLiteral("freeTransformInterpolationCombo"));
  auto* apply = window.findChild<QPushButton*>(QStringLiteral("freeTransformApplyButton"));
  CHECK(x != nullptr);
  CHECK(y != nullptr);
  CHECK(scale_x != nullptr);
  CHECK(scale_y != nullptr);
  CHECK(rotation != nullptr);
  CHECK(interpolation != nullptr);
  CHECK(apply != nullptr);
  CHECK(x->isVisible());
  CHECK(interpolation->currentText() == QStringLiteral("Bicubic"));

  x->setValue(x->value() + 32.0);
  y->setValue(y->value() + 18.0);
  scale_x->setValue(150.0);
  scale_y->setValue(125.0);
  rotation->setValue(15.0);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  apply->click();
  QApplication::processEvents();

  const auto transformed_rect = canvas->active_layer_document_rect();
  CHECK(transformed_rect.has_value());
  CHECK(transformed_rect->width() > filled_rect->width());
  CHECK(transformed_rect->height() > filled_rect->height());
  CHECK(transformed_rect->center().x() > filled_rect->center().x() + 15);
  CHECK(!canvas->free_transform_active());
}

void ui_transform_numeric_controls_accept_negative_scale() {
  patchy::Document document(260, 180, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(260, 180, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(80, 40, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 80, 20), QColor(220, 40, 40, 255));
  fill_pixel_rect(pixels, QRect(0, 20, 80, 20), QColor(40, 80, 220, 255));
  patchy::Layer layer(document.allocate_layer_id(), "Flip Scale", std::move(pixels));
  layer.set_bounds(patchy::Rect{80, 70, 80, 40});
  document.add_layer(std::move(layer));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Negative Transform Scale"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();

  auto* scale_x = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleXSpin"));
  auto* scale_y = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleYSpin"));
  auto* link = window.findChild<QPushButton*>(QStringLiteral("freeTransformLinkScaleButton"));
  auto* apply = window.findChild<QPushButton*>(QStringLiteral("freeTransformApplyButton"));
  CHECK(scale_x != nullptr);
  CHECK(scale_y != nullptr);
  CHECK(link != nullptr);
  CHECK(apply != nullptr);
  CHECK(scale_x->minimum() <= -100.0);
  CHECK(scale_y->minimum() <= -100.0);
  link->setChecked(false);

  scale_y->setValue(-100.0);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto state = canvas->transform_controls_state();
  CHECK(state.has_value());
  CHECK(std::abs(state->scale_y_percent + 100.0) < 0.001);

  apply->click();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 76)), QColor(40, 80, 220), 50));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 104)), QColor(220, 40, 40), 50));
  save_widget_artifact("ui_transform_negative_scale", window);
}

void ui_transform_numeric_preview_renders_layer_styles() {
  patchy::Document document(360, 260, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(360, 260, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(70, 46, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(54, 8, 14, 30), QColor(35, 85, 210, 255));
  patchy::Layer styled_layer(document.allocate_layer_id(), "Styled Transform", std::move(pixels));
  styled_layer.set_bounds(patchy::Rect{120, 90, 70, 46});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = patchy::BlendMode::Normal;
  stroke.color = patchy::RgbColor{230, 35, 45};
  stroke.opacity = 1.0F;
  stroke.size = 8.0F;
  stroke.position = patchy::LayerStrokePosition::Outside;
  styled_layer.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(styled_layer));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Styled Transform Preview"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();

  auto* scale_x = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleXSpin"));
  auto* rotation = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformRotationSpin"));
  auto* cancel = window.findChild<QPushButton*>(QStringLiteral("freeTransformCancelButton"));
  CHECK(scale_x != nullptr);
  CHECK(rotation != nullptr);
  CHECK(cancel != nullptr);

  scale_x->setValue(240.0);
  rotation->setValue(0.0);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  const auto preview = canvas->grab().toImage();
  const auto count_red_style_pixels = [&](QRect document_rect) {
    int count = 0;
    for (int document_y = document_rect.top(); document_y <= document_rect.bottom(); ++document_y) {
      for (int document_x = document_rect.left(); document_x <= document_rect.right(); ++document_x) {
        const auto widget_point = canvas->widget_position_for_document_point(QPoint(document_x, document_y));
        if (!preview.rect().contains(widget_point)) {
          continue;
        }
        const auto color = preview.pixelColor(widget_point);
        if (color.red() > 190 && color.green() < 90 && color.blue() < 100) {
          ++count;
        }
      }
    }
    return count;
  };
  CHECK(count_red_style_pixels(QRect(154, 88, 14, 54)) > 40);
  CHECK(count_red_style_pixels(QRect(214, 86, 24, 36)) < 10);
  cancel->click();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

void ui_move_show_transform_controls_click_shows_passive_transform() {
  SettingsValueRestorer saved_show_transform_controls(QStringLiteral("tools/showTransformControls"));
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("tools/showTransformControls"));
  settings.sync();

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* show_controls = window.findChild<QCheckBox*>(QStringLiteral("moveShowTransformControlsCheck"));
  CHECK(show_controls != nullptr);
  CHECK(show_controls->text() == QStringLiteral("Show Transform Controls"));
  CHECK(show_controls->isChecked());
  CHECK(canvas->show_transform_controls());

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 70)),
       canvas->widget_position_for_document_point(QPoint(140, 115)));
  const auto filled_rect = canvas->selected_document_rect();
  CHECK(filled_rect.has_value());
  canvas->set_primary_color(QColor(60, 130, 230));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  const auto before = canvas->active_layer_document_rect();
  CHECK(before.has_value());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  const auto click = canvas->widget_position_for_document_point(QPoint(100, 90));
  const auto passive_bottom_right = canvas->widget_position_for_document_point(
      QPoint(filled_rect->x() + filled_rect->width(), filled_rect->y() + filled_rect->height()));
  send_mouse(*canvas, QEvent::MouseMove, passive_bottom_right, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);
  send_mouse(*canvas, QEvent::MouseMove, click, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);

  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == before);
  send_mouse(*canvas, QEvent::MouseMove, passive_bottom_right, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);
  send_mouse(*canvas, QEvent::MouseMove, click, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
  save_widget_artifact("ui_move_show_transform_controls", window);

  const auto jitter = click + QPoint(2, -3);
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, jitter, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, jitter, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == before);

  const auto outside = canvas->widget_position_for_document_point(QPoint(420, 340));
  send_mouse(*canvas, QEvent::MouseButtonPress, outside, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, outside, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == before);

  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == before);

  canvas->set_auto_select_layer(false);
  const auto auto_select_off_before = canvas->active_layer_document_rect();
  CHECK(auto_select_off_before.has_value());
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == auto_select_off_before);

  send_mouse(*canvas, QEvent::MouseButtonPress, outside, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, outside, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == auto_select_off_before);
  send_mouse(*canvas, QEvent::MouseMove, passive_bottom_right, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);

  const auto auto_select_off_jitter = click + QPoint(-2, 3);
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, auto_select_off_jitter, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, auto_select_off_jitter, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == auto_select_off_before);

  const auto bottom_right = canvas->widget_position_for_document_point(
      QPoint(filled_rect->x() + filled_rect->width(), filled_rect->y() + filled_rect->height()));
  send_mouse(*canvas, QEvent::MouseButtonPress, bottom_right, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_mouse(*canvas, QEvent::MouseMove, bottom_right + QPoint(70, 45), Qt::NoButton, Qt::LeftButton,
             Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, bottom_right + QPoint(70, 45), Qt::LeftButton, Qt::NoButton,
             Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  const auto auto_select_off_after = canvas->active_layer_document_rect();
  CHECK(auto_select_off_after.has_value());
  CHECK(auto_select_off_after->width() > filled_rect->width() + 20);
  CHECK(auto_select_off_after->height() > filled_rect->height() + 10);

  const auto transformed_bottom_right = canvas->widget_position_for_document_point(
      QPoint(auto_select_off_after->x() + auto_select_off_after->width(),
             auto_select_off_after->y() + auto_select_off_after->height()));
  const auto transformed_rotate_handle = canvas->widget_position_for_document_point(
                                             QPoint(auto_select_off_after->x() + auto_select_off_after->width() / 2,
                                                    auto_select_off_after->y())) +
                                         QPoint(0, -32);
  const auto transformed_center = canvas->widget_position_for_document_point(auto_select_off_after->center());
  send_mouse(*canvas, QEvent::MouseMove, transformed_bottom_right, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);
  send_mouse(*canvas, QEvent::MouseButtonPress, transformed_center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, transformed_center + QPoint(50, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  const auto moving_preview = canvas->grab().toImage();
  CHECK(color_close(moving_preview.pixelColor(transformed_rotate_handle), Qt::white, 18));
  send_mouse(*canvas, QEvent::MouseButtonRelease, transformed_center + QPoint(50, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

void ui_transform_controls_finish_on_tool_layer_and_duplicate_changes() {
  QApplication::clipboard()->clear();

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  QImage image(48, 32, QImage::Format_RGBA8888);
  image.fill(QColor(40, 180, 120, 255));
  QApplication::clipboard()->setImage(image);
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();

  const auto before_tool_switch = canvas->active_layer_document_rect();
  CHECK(before_tool_switch.has_value());
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == before_tool_switch);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  drag(*canvas, canvas->widget_position_for_document_point(before_tool_switch->center()),
       canvas->widget_position_for_document_point(before_tool_switch->center() + QPoint(34, 0)));
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  const auto after_tool_switch = canvas->active_layer_document_rect();
  CHECK(after_tool_switch.has_value());
  CHECK(after_tool_switch->x() >= before_tool_switch->x() + 28);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  const auto before_duplicate = canvas->active_layer_document_rect();
  CHECK(before_duplicate.has_value());
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  drag(*canvas, canvas->widget_position_for_document_point(before_duplicate->center()),
       canvas->widget_position_for_document_point(before_duplicate->center() + QPoint(24, 14)));
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto layers_before_duplicate = layer_list->count();
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(layer_list->count() == layers_before_duplicate + 1);
  CHECK(layer_list->selectedItems().size() == 1);
  const auto duplicated = canvas->active_layer_document_rect();
  CHECK(duplicated.has_value());
  CHECK(duplicated->x() >= before_duplicate->x() + 18);
  CHECK(duplicated->y() >= before_duplicate->y() + 8);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(layer_list->count() >= 2);
  layer_list->setCurrentItem(layer_list->item(1), QItemSelectionModel::ClearAndSelect);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(layer_list->selectedItems().size() == 1);
  QApplication::clipboard()->clear();
}

void ui_layer_via_copy_and_cut_match_photoshop_shortcuts() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(245, 30, 30));
  canvas->set_brush_size(24);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 80)),
       canvas->widget_position_for_document_point(QPoint(180, 80)));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(110, 80)), QColor(245, 30, 30), 45));

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(52, 58)),
       canvas->widget_position_for_document_point(QPoint(150, 112)));

  const auto initial_layers = layer_list->count();
  require_action(window, "layerViaCopyAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == initial_layers + 1);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Layer Via Copy"));
  layer_list->item(0)->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(110, 80)), QColor(245, 30, 30), 45));

  layer_list->clearSelection();
  auto* paint_layer = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  layer_list->setCurrentItem(paint_layer);
  paint_layer->setSelected(true);
  QApplication::processEvents();
  require_action(window, "layerViaCutAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->item(0)->text() == QStringLiteral("Layer Via Cut"));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(110, 80)), QColor(245, 30, 30), 45));

  layer_list->item(0)->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  const auto revealed = canvas_pixel(*canvas, QPoint(110, 80));
  CHECK(!color_close(revealed, QColor(245, 30, 30), 45));
  CHECK(revealed.red() > 210 && revealed.green() > 210 && revealed.blue() > 210);
  save_widget_artifact("ui_layer_via_copy_cut", window);
}

void ui_layer_mask_from_selection_hides_pixels_and_shows_thumbnail() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(96, 72, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  document.add_pixel_layer("Red Fill", solid_pixels(96, 72, patchy::PixelFormat::rgb8(), QColor(220, 30, 30)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Layer Mask"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);

  const auto start = canvas->widget_position_for_document_point(QPoint(20, 20));
  const auto end = canvas->widget_position_for_document_point(QPoint(70, 54));
  drag(*canvas, start, end);
  require_action(window, "layerAddMaskFromSelectionAction")->trigger();
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 30, 30), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(8, 8)), QColor(255, 255, 255), 8));

  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  auto* item = require_layer_item(*layers, QStringLiteral("Red Fill"));
  auto* row = layers->itemWidget(item);
  CHECK(row != nullptr);
  CHECK(row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail")) != nullptr);
  auto* mask_thumbnail = row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail"));
  CHECK(mask_thumbnail != nullptr);
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  send_mouse(*mask_thumbnail, QEvent::MouseButtonPress, mask_thumbnail->rect().center(), Qt::LeftButton,
             Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*mask_thumbnail, QEvent::MouseButtonRelease, mask_thumbnail->rect().center(), Qt::LeftButton,
             Qt::NoButton, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(30, 30)));
  CHECK(!canvas->selected_document_region().contains(QPoint(8, 8)));
  CHECK(!canvas->selected_document_region().contains(QPoint(80, 30)));
  auto* link = row->findChild<QToolButton*>(QStringLiteral("layerMaskLinkButton"));
  CHECK(link != nullptr);
  CHECK(link->isChecked());
  link->click();
  QApplication::processEvents();

  canvas->set_tool(patchy::ui::CanvasTool::Move);
  canvas->set_show_transform_controls(false);
  const auto move_start = canvas->widget_position_for_document_point(QPoint(30, 30));
  const auto move_end = canvas->widget_position_for_document_point(QPoint(50, 30));
  drag(*canvas, move_start, move_end);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 30, 30), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 30)), QColor(255, 255, 255), 8));

  require_action(window, "layerDeleteMaskAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 30)), QColor(220, 30, 30), 8));
  item = require_layer_item(*layers, QStringLiteral("Red Fill"));
  row = layers->itemWidget(item);
  CHECK(row != nullptr);
  CHECK(row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail")) == nullptr);
  save_widget_artifact("ui_layer_mask_from_selection", window);
}

void ui_layer_mask_target_paints_inverts_disables_and_applies() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  document.add_pixel_layer("Red Fill", solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(220, 30, 30)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Mask Target"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  canvas->set_snap_enabled(false);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(24, 24)),
       canvas->widget_position_for_document_point(QPoint(44, 44)));
  require_action(window, "layerAddMaskFromSelectionAction")->trigger();
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(32, 32)), QColor(220, 30, 30), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(8, 8)), QColor(255, 255, 255), 8));

  auto* item = require_layer_item(*layers, QStringLiteral("Red Fill"));
  auto* row = layers->itemWidget(item);
  CHECK(row != nullptr);
  auto* mask_thumbnail = row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail"));
  CHECK(mask_thumbnail != nullptr);
  send_mouse(*mask_thumbnail, QEvent::MouseButtonPress, mask_thumbnail->rect().center(), Qt::LeftButton,
             Qt::LeftButton);
  send_mouse(*mask_thumbnail, QEvent::MouseButtonRelease, mask_thumbnail->rect().center(), Qt::LeftButton,
             Qt::NoButton);
  QApplication::processEvents();

  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);
  item = require_layer_item(*layers, QStringLiteral("Red Fill"));
  row = layers->itemWidget(item);
  CHECK(row != nullptr);
  mask_thumbnail = row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail"));
  CHECK(mask_thumbnail != nullptr);
  CHECK(mask_thumbnail->property("layerTargetActive").toBool());
  auto* mask_label = window.findChild<QLabel*>(QStringLiteral("activeLayerMaskLabel"));
  CHECK(mask_label != nullptr);
  CHECK(mask_label->text().contains(QStringLiteral("Target: Mask")));

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::white);
  canvas->set_brush_size(18);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(8, 8)),
       canvas->widget_position_for_document_point(QPoint(10, 8)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(8, 8)), QColor(220, 30, 30), 18));

  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(56, 56)), QColor(220, 30, 30), 8));

  require_action(window, "layerInvertMaskAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(32, 32)), QColor(255, 255, 255), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(56, 56)), QColor(255, 255, 255), 8));

  auto* disable_mask = require_action(window, "layerDisableMaskAction");
  disable_mask->trigger();
  QApplication::processEvents();
  CHECK(disable_mask->isChecked());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(32, 32)), QColor(220, 30, 30), 8));

  disable_mask->trigger();
  QApplication::processEvents();
  CHECK(!disable_mask->isChecked());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(32, 32)), QColor(255, 255, 255), 8));

  require_action(window, "layerApplyMaskAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(32, 32)), QColor(255, 255, 255), 8));
  item = require_layer_item(*layers, QStringLiteral("Red Fill"));
  row = layers->itemWidget(item);
  CHECK(row != nullptr);
  CHECK(row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail")) == nullptr);
  CHECK(mask_label->text().isEmpty());
  CHECK(!mask_label->isVisible());
  save_widget_artifact("ui_layer_mask_target_editing", window);
}

void ui_layer_thumbnail_updates_after_brush_edit() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  document.add_pixel_layer("Paint", solid_pixels(64, 64, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Thumbnail Refresh"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);

  auto thumbnail_center = [&]() {
    auto* item = require_layer_item(*layers, QStringLiteral("Paint"));
    auto* row = layers->itemWidget(item);
    CHECK(row != nullptr);
    auto* thumbnail = row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
    CHECK(thumbnail != nullptr);
    const auto image = thumbnail->pixmap(Qt::ReturnByValue).toImage();
    CHECK(!image.isNull());
    return image.pixelColor(image.width() / 2, image.height() / 2);
  };

  const auto before = thumbnail_center();
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(240, 30, 20));
  canvas->set_brush_size(28);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(32, 32)),
       canvas->widget_position_for_document_point(QPoint(34, 32)));
  QApplication::processEvents();

  const auto after = thumbnail_center();
  CHECK(after.red() > before.red() + 80);
  CHECK(after.green() < 90);
  CHECK(after.blue() < 90);
  save_widget_artifact("ui_layer_thumbnail_refresh", window);
}

void ui_layer_thumbnail_defers_brush_refresh_until_stroke_end() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  document.add_pixel_layer("Paint", solid_pixels(64, 64, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Deferred Thumbnail Refresh"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);

  auto thumbnail_center = [&]() {
    auto* item = require_layer_item(*layers, QStringLiteral("Paint"));
    auto* row = layers->itemWidget(item);
    CHECK(row != nullptr);
    auto* thumbnail = row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
    CHECK(thumbnail != nullptr);
    const auto image = thumbnail->pixmap(Qt::ReturnByValue).toImage();
    CHECK(!image.isNull());
    return image.pixelColor(image.width() / 2, image.height() / 2);
  };

  const auto before = thumbnail_center();
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(240, 30, 20));
  canvas->set_brush_size(28);
  const auto start = canvas->widget_position_for_document_point(QPoint(32, 32));
  const auto end = canvas->widget_position_for_document_point(QPoint(34, 32));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);

  const auto mid_stroke = thumbnail_center();
  CHECK(mid_stroke == before);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(32, 32)), QColor(240, 30, 20), 55));

  send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto after = thumbnail_center();
  CHECK(after.red() > before.red() + 80);
  CHECK(after.green() < 90);
  CHECK(after.blue() < 90);
}

void ui_cut_selection_clears_source_and_keeps_clipboard() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  const QColor paint_color(255, 80, 20);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(paint_color);
  canvas->set_brush_size(22);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(92, 92)),
       canvas->widget_position_for_document_point(QPoint(130, 110)));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(100, 100)), paint_color, 55));

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 70)),
       canvas->widget_position_for_document_point(QPoint(150, 130)));
  const auto cut_selection_rect = canvas->selected_document_rect();
  CHECK(cut_selection_rect.has_value());
  const auto layers_before = layer_list->count();
  require_action(window, "editCutAction")->trigger();
  QApplication::processEvents();
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(100, 100)), paint_color, 55));
  const auto cut_clipboard_image = QApplication::clipboard()->image();
  CHECK(!cut_clipboard_image.isNull());
  QApplication::clipboard()->setImage(cut_clipboard_image);
  QApplication::processEvents();

  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);
  const auto pasted_rect = canvas->active_layer_document_rect();
  CHECK(pasted_rect.has_value());
  CHECK(pasted_rect->topLeft() == cut_selection_rect->topLeft());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(100, 100)), paint_color, 65));
  save_widget_artifact("ui_cut_selection", window);
}

void ui_brush_on_pasted_layer_expands_layer_bounds() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(255, 80, 20));
  canvas->set_brush_size(18);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(60, 60)),
       canvas->widget_position_for_document_point(QPoint(70, 60)));
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(45, 45)),
       canvas->widget_position_for_document_point(QPoint(85, 85)));
  const auto layers_before = layer_list->count();
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);

  require_action(window, "editDeselectAction")->trigger();
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(20, 80, 240));
  canvas->set_brush_size(24);
  canvas->set_brush_opacity(100);
  const QPoint outside_original_paste(260, 190);
  drag(*canvas, canvas->widget_position_for_document_point(outside_original_paste),
       canvas->widget_position_for_document_point(outside_original_paste + QPoint(1, 1)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, outside_original_paste), QColor(20, 80, 240), 45));
  save_widget_artifact("ui_brush_expands_pasted_layer", window);
}

void ui_brush_opacity_caps_per_stroke() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto scrub_stroke = [canvas](QPoint document_point) {
    const auto center = canvas->widget_position_for_document_point(document_point);
    send_mouse(*canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    for (const auto offset : {QPoint(0, 0), QPoint(18, 0), QPoint(-18, 0), QPoint(18, 0), QPoint(-18, 0)}) {
      send_mouse(*canvas, QEvent::MouseMove, center + offset, Qt::NoButton, Qt::LeftButton);
    }
    send_mouse(*canvas, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(32);
  canvas->set_brush_opacity(20);
  canvas->set_brush_build_up(false);
  scrub_stroke(QPoint(175, 120));
  const auto first_stroke = canvas_pixel(*canvas, QPoint(175, 120));
  CHECK(first_stroke.red() >= 190);
  CHECK(first_stroke.red() <= 220);
  CHECK(std::abs(first_stroke.red() - first_stroke.green()) <= 3);
  CHECK(std::abs(first_stroke.green() - first_stroke.blue()) <= 3);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 120)),
       canvas->widget_position_for_document_point(QPoint(280, 120)));
  QApplication::processEvents();
  const auto second_stroke = canvas_pixel(*canvas, QPoint(175, 120));
  CHECK(second_stroke.red() < first_stroke.red() - 20);
  CHECK(second_stroke.red() >= 145);
  CHECK(second_stroke.red() <= 180);

  canvas->set_brush_opacity(100);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(175, 180)),
       canvas->widget_position_for_document_point(QPoint(176, 180)));
  QApplication::processEvents();
  CHECK(canvas_pixel(*canvas, QPoint(175, 180)).red() < 20);

  require_action_by_text(window, QStringLiteral("Eraser"))->trigger();
  canvas->set_brush_opacity(20);
  canvas->set_brush_build_up(false);
  scrub_stroke(QPoint(175, 180));
  const auto first_erase = canvas_pixel(*canvas, QPoint(175, 180));
  CHECK(first_erase.red() >= 40);
  CHECK(first_erase.red() <= 70);

  scrub_stroke(QPoint(175, 180));
  const auto second_erase = canvas_pixel(*canvas, QPoint(175, 180));
  CHECK(second_erase.red() > first_erase.red() + 20);
  CHECK(second_erase.red() >= 80);
  CHECK(second_erase.red() <= 105);
  save_widget_artifact("ui_brush_opacity_per_stroke", window);
}

void ui_layer_mask_brush_opacity_caps_per_stroke() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  auto& red = document.add_pixel_layer("Red Fill",
                                       solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(220, 30, 30)));
  patchy::PixelBuffer mask_pixels(64, 64, patchy::PixelFormat::gray8());
  mask_pixels.clear(0);
  red.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 64, 64}, std::move(mask_pixels), 0, false});

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Mask Brush Opacity"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_layer_edit_target(patchy::ui::CanvasWidget::LayerEditTarget::Mask);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::white);
  canvas->set_brush_size(24);
  canvas->set_brush_opacity(20);
  canvas->set_brush_softness(0);
  canvas->set_brush_build_up(false);

  auto scrub_stroke = [canvas](QPoint document_point) {
    const auto center = canvas->widget_position_for_document_point(document_point);
    send_mouse(*canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    for (const auto offset : {QPoint(0, 0), QPoint(14, 0), QPoint(-14, 0), QPoint(14, 0), QPoint(-14, 0)}) {
      send_mouse(*canvas, QEvent::MouseMove, center + offset, Qt::NoButton, Qt::LeftButton);
    }
    send_mouse(*canvas, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  scrub_stroke(QPoint(32, 32));
  const auto first_stroke = canvas_pixel(*canvas, QPoint(32, 32));
  CHECK(first_stroke.red() >= 245);
  CHECK(first_stroke.green() >= 200);
  CHECK(first_stroke.green() <= 222);
  CHECK(std::abs(first_stroke.green() - first_stroke.blue()) <= 4);

  scrub_stroke(QPoint(32, 32));
  const auto second_stroke = canvas_pixel(*canvas, QPoint(32, 32));
  CHECK(second_stroke.green() < first_stroke.green() - 20);
  CHECK(second_stroke.green() >= 165);
  CHECK(second_stroke.green() <= 190);
  save_widget_artifact("ui_layer_mask_brush_opacity_per_stroke", window);
}

void ui_shift_constrains_brush_and_eraser_strokes_to_axis() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(5);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 80)),
       canvas->widget_position_for_document_point(QPoint(150, 110)), Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 80)), Qt::black, 12));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 99)), Qt::white, 12));

  canvas->set_brush_size(45);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(60, 180)),
       canvas->widget_position_for_document_point(QPoint(170, 180)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 199)), Qt::black, 12));

  require_action_by_text(window, QStringLiteral("Eraser"))->trigger();
  canvas->set_brush_size(5);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 180)),
       canvas->widget_position_for_document_point(QPoint(150, 210)), Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 180)), Qt::white, 12));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 199)), Qt::black, 12));
}

void ui_shift_constrains_clone_stamp_strokes_to_axis() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(230, 40, 30));
  canvas->set_brush_size(45);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 90)),
       canvas->widget_position_for_document_point(QPoint(130, 90)));
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Clone"))->trigger();
  canvas->set_brush_size(5);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  const auto source = canvas->widget_position_for_document_point(QPoint(70, 90));
  send_mouse(*canvas, QEvent::MouseButtonPress, source, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, source, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(170, 100)),
       canvas->widget_position_for_document_point(QPoint(230, 130)), Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(210, 100)), QColor(230, 40, 30), 45));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(210, 120)), Qt::white, 12));
}

void ui_one_pixel_brush_drag_paints_fractional_smoothed_line() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_zoom(4.0);
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(1);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  QApplication::processEvents();

  const auto from = canvas->widget_position_for_document_point(QPoint(50, 120)) + QPoint(0, 1);
  const auto to = canvas->widget_position_for_document_point(QPoint(240, 120)) + QPoint(0, 1);
  send_mouse(*canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
  for (int step = 1; step <= 12; ++step) {
    const auto x = from.x() + ((to.x() - from.x()) * step) / 12;
    send_mouse(*canvas, QEvent::MouseMove, QPoint(x, from.y()), Qt::NoButton, Qt::LeftButton);
  }
  send_mouse(*canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  save_widget_artifact("ui_one_pixel_brush_fractional_line", *canvas);

  for (const auto x : {80, 100, 120}) {
    const auto top_left = canvas->widget_position_for_document_point(QPoint(x - 1, 118));
    const auto bottom_right = canvas->widget_position_for_document_point(QPoint(x + 2, 123));
    const auto search_rect = QRect(top_left, bottom_right).normalized();
    CHECK(count_pixels_close(canvas->grab().toImage(), search_rect, Qt::black, 70) > 0);
  }
}

void ui_one_pixel_brush_and_eraser_same_cell_drag_touches_one_pixel() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(64, 64, patchy::PixelFormat::rgb8(), Qt::white));
  auto& paint_layer = document.add_pixel_layer("Paint",
                                               solid_pixels(64, 64, patchy::PixelFormat::rgba8(), Qt::transparent));
  document.set_active_layer(paint_layer.id());

  patchy::ui::CanvasWidget canvas;
  canvas.resize(240, 240);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_zoom(32.0);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(1);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(100);
  canvas.show();
  QApplication::processEvents();

  const QPoint target(34, 28);
  const auto painted_alpha = [&paint_layer](QPoint point) {
    const auto bounds = paint_layer.bounds();
    CHECK(bounds.contains(point.x(), point.y()));
    return paint_layer.pixels().pixel(point.x() - bounds.x, point.y() - bounds.y)[3];
  };
  const auto target_origin = canvas.widget_position_for_document_point(target);
  send_mouse(canvas, QEvent::MouseButtonPress, target_origin + QPoint(5, 16), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, target_origin + QPoint(24, 16), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(painted_alpha(target) == 255);
  CHECK(painted_alpha(target + QPoint(1, 0)) == 0);
  CHECK(painted_alpha(target + QPoint(0, 1)) == 0);

  const QPoint neighbor = target + QPoint(1, 0);
  const auto neighbor_center = canvas.widget_position_for_document_point(neighbor) + QPoint(16, 16);
  send_mouse(canvas, QEvent::MouseButtonPress, neighbor_center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, neighbor_center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(painted_alpha(neighbor) == 255);

  canvas.set_tool(patchy::ui::CanvasTool::Eraser);
  canvas.set_brush_size(1);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(100);
  send_mouse(canvas, QEvent::MouseButtonPress, target_origin + QPoint(6, 10), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, target_origin + QPoint(25, 10), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(painted_alpha(target) == 0);
  CHECK(painted_alpha(neighbor) == 255);
}

void ui_max_zoom_brush_skips_noop_stroke_repaints() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_zoom(32.0);
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(2);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  canvas->set_brush_build_up(false);
  QApplication::processEvents();

  const auto start = canvas->widget_position_for_document_point(QPoint(10, 10));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();

  PaintCounterFilter counter;
  canvas->installEventFilter(&counter);
  QApplication::processEvents();
  counter.paint_events = 0;
  for (int offset = 1; offset <= 10; ++offset) {
    send_mouse(*canvas, QEvent::MouseMove, start + QPoint(offset, 0), Qt::NoButton, Qt::LeftButton);
  }
  CHECK(counter.paint_events == 0);
  canvas->removeEventFilter(&counter);
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(10, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

void ui_deep_zoom_brush_repaint_stays_responsive() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(64, 64, patchy::PixelFormat::rgb8(), Qt::white));
  auto& paint_layer = document.add_pixel_layer("Paint",
                                               solid_pixels(64, 64, patchy::PixelFormat::rgba8(), Qt::transparent));
  document.set_active_layer(paint_layer.id());
  document.grid_settings().horizontal_cycle_32 = 32;
  document.grid_settings().vertical_cycle_32 = 32;

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 380);
  canvas.set_document(&document);
  canvas.set_zoom(128.0);
  canvas.set_grid_visible(true);
  canvas.set_grid_subdivisions(1);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(4);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(0);
  canvas.show();
  QApplication::processEvents();

  const auto cell_center = [&canvas](QPoint document_point) {
    const auto a = canvas.widget_position_for_document_point(document_point);
    const auto b = canvas.widget_position_for_document_point(document_point + QPoint(1, 1));
    return QPoint((a.x() + b.x()) / 2, (a.y() + b.y()) / 2);
  };
  const auto start = cell_center(QPoint(1, 1));
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();

  PaintCounterFilter counter;
  canvas.installEventFilter(&counter);
  QElapsedTimer timer;
  timer.start();
  constexpr int kSteps = 36;
  for (int step = 1; step <= kSteps; ++step) {
    send_mouse(canvas, QEvent::MouseMove, start + QPoint(step * 8, (step % 3) - 1), Qt::NoButton, Qt::LeftButton);
  }
  const auto elapsed_ms = timer.elapsed();
  canvas.removeEventFilter(&counter);
  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(kSteps * 8, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(counter.paint_events <= kSteps + 4);
  CHECK(elapsed_ms < 2500);
  CHECK(paint_layer.pixels().pixel(1, 1)[3] == 255);
}

void ui_airbrush_preset_does_not_stack_within_one_stroke() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* brush_preset = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(brush_preset != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::black);
  const auto airbrush_index = brush_preset->findData(QStringLiteral("airbrush"));
  CHECK(airbrush_index >= 0);
  brush_preset->setCurrentIndex(airbrush_index);
  QApplication::processEvents();
  CHECK(!canvas->brush_build_up());
  CHECK(canvas->brush_opacity() == 12);

  const auto center = canvas->widget_position_for_document_point(QPoint(150, 120));
  send_mouse(*canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  for (int i = 0; i < 6; ++i) {
    send_mouse(*canvas, QEvent::MouseMove, center + QPoint(i % 2, 0), Qt::NoButton, Qt::LeftButton);
  }
  send_mouse(*canvas, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto painted_once = canvas_pixel(*canvas, QPoint(150, 120));
  CHECK(painted_once.red() >= 215);
  CHECK(painted_once.red() <= 232);
  CHECK(std::abs(painted_once.red() - painted_once.green()) <= 4);
  CHECK(std::abs(painted_once.green() - painted_once.blue()) <= 4);

  send_mouse(*canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto second_stroke = canvas_pixel(*canvas, QPoint(150, 120));
  CHECK(second_stroke.red() < painted_once.red() - 15);
  CHECK(second_stroke.red() >= 190);
  CHECK(second_stroke.red() <= 210);
  save_widget_artifact("ui_airbrush_no_same_stroke_stack", window);
}

void ui_airbrush_fast_strokes_ignore_mouse_event_density() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* brush_preset = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(brush_preset != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::black);
  const auto airbrush_index = brush_preset->findData(QStringLiteral("airbrush"));
  CHECK(airbrush_index >= 0);
  brush_preset->setCurrentIndex(airbrush_index);
  QApplication::processEvents();

  auto send_stroke = [canvas](const std::vector<QPoint>& points) {
    CHECK(points.size() >= 2U);
    send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(points.front()),
               Qt::LeftButton, Qt::LeftButton);
    for (std::size_t index = 1; index < points.size(); ++index) {
      send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(points[index]),
                 Qt::NoButton, Qt::LeftButton);
    }
    send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(points.back()),
               Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  send_stroke({QPoint(70, 112), QPoint(280, 112)});
  send_stroke({QPoint(70, 188), QPoint(76, 188), QPoint(83, 188), QPoint(91, 188), QPoint(102, 188),
               QPoint(113, 188), QPoint(127, 188), QPoint(139, 188), QPoint(154, 188), QPoint(166, 188),
               QPoint(181, 188), QPoint(193, 188), QPoint(207, 188), QPoint(219, 188), QPoint(234, 188),
               QPoint(247, 188), QPoint(261, 188), QPoint(273, 188), QPoint(280, 188)});

  for (const auto x : {110, 145, 180, 215, 250}) {
    const auto sparse = canvas_pixel(*canvas, QPoint(x, 112));
    const auto dense = canvas_pixel(*canvas, QPoint(x, 188));
    CHECK(sparse.red() < 245);
    CHECK(dense.red() < 245);
    CHECK(color_close(sparse, dense, 26));
  }
  save_widget_artifact("ui_airbrush_event_density", window);
}

void ui_airbrush_jittered_stroke_uses_smoothed_path() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* brush_preset = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(brush_preset != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_zoom(1.0);
  canvas->set_primary_color(Qt::black);
  const auto airbrush_index = brush_preset->findData(QStringLiteral("airbrush"));
  CHECK(airbrush_index >= 0);
  brush_preset->setCurrentIndex(airbrush_index);
  canvas->set_brush_size(18);
  canvas->set_brush_opacity(64);
  canvas->set_brush_softness(100);
  QApplication::processEvents();

  const std::vector<QPoint> points = {
      QPoint(70, 150),  QPoint(100, 159), QPoint(130, 141), QPoint(160, 159),
      QPoint(190, 141), QPoint(220, 159), QPoint(250, 141), QPoint(280, 150),
  };
  send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(points.front()),
             Qt::LeftButton, Qt::LeftButton);
  for (std::size_t index = 1; index < points.size(); ++index) {
    send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(points[index]), Qt::NoButton,
               Qt::LeftButton);
  }
  send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(points.back()),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto image = canvas->grab().toImage();
  auto darkness_center_y = [canvas, &image](int x) {
    double weighted_y = 0.0;
    double total_darkness = 0.0;
    for (int y = 128; y <= 172; ++y) {
      const auto widget_point = canvas->widget_position_for_document_point(QPoint(x, y));
      const auto color = image.pixelColor(widget_point);
      const auto darkness = static_cast<double>(255 - color.red());
      if (darkness <= 4.0) {
        continue;
      }
      weighted_y += static_cast<double>(y) * darkness;
      total_darkness += darkness;
    }
    CHECK(total_darkness > 20.0);
    return weighted_y / total_darkness;
  };

  std::vector<double> centerline;
  for (const auto x : {100, 130, 160, 190, 220, 250}) {
    centerline.push_back(darkness_center_y(x));
  }
  const auto [min_center, max_center] = std::minmax_element(centerline.begin(), centerline.end());
  CHECK(min_center != centerline.end());
  CHECK((*max_center - *min_center) <= 12.0);
  save_widget_artifact("ui_airbrush_smoothed_jitter", window);
}

void ui_clone_tool_samples_source_and_paints_offset() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(230, 40, 30));
  canvas->set_brush_size(22);
  canvas->set_brush_opacity(100);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 72)),
       canvas->widget_position_for_document_point(QPoint(72, 72)));
  canvas->set_primary_color(QColor(30, 80, 230));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(110, 72)),
       canvas->widget_position_for_document_point(QPoint(112, 72)));
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Clone"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Clone);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  canvas->set_brush_softness(100);
  auto* clone_aligned = window.findChild<QCheckBox*>(QStringLiteral("cloneAlignedCheck"));
  CHECK(clone_aligned != nullptr);
  CHECK(clone_aligned->isChecked());
  CHECK(canvas->clone_aligned());

  const auto source = canvas->widget_position_for_document_point(QPoint(70, 72));
  send_mouse(*canvas, QEvent::MouseButtonPress, source, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, source, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);

  const auto target = canvas->widget_position_for_document_point(QPoint(170, 102));
  drag(*canvas, target, target + QPoint(2, 0));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(170, 102)), QColor(230, 40, 30), 45));
  const auto feathered_edge = canvas_pixel(*canvas, QPoint(178, 102));
  CHECK(feathered_edge.red() > feathered_edge.green());
  CHECK(feathered_edge.green() > 80);
  CHECK(feathered_edge.green() < 245);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(210, 102)),
       canvas->widget_position_for_document_point(QPoint(212, 102)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(210, 102)), QColor(30, 80, 230), 45));

  clone_aligned->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->clone_aligned());
  send_mouse(*canvas, QEvent::MouseButtonPress, source, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, source, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(250, 102)),
       canvas->widget_position_for_document_point(QPoint(252, 102)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(250, 102)), QColor(230, 40, 30), 45));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(290, 102)),
       canvas->widget_position_for_document_point(QPoint(292, 102)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(290, 102)), QColor(230, 40, 30), 45));

  auto* history = window.findChild<QListWidget*>(QStringLiteral("historyList"));
  CHECK(history != nullptr);
  CHECK(history->item(0) != nullptr);
  CHECK(history->item(0)->text().contains(QStringLiteral("Clone")));
  save_widget_artifact("ui_clone_tool_stamp", window);
}

void ui_clone_tool_feathered_rgba_edges_keep_source_color() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(64, 64, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  for (std::int32_t y = 34; y <= 50; ++y) {
    for (std::int32_t x = 8; x <= 56; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 255;
      px[1] = 220;
      px[2] = 0;
      px[3] = 255;
    }
  }
  auto& layer = document.add_pixel_layer("Paint", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(160, 160);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Clone);
  canvas.set_brush_size(24);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(100);
  canvas.show();
  QApplication::processEvents();

  const auto source = canvas.widget_position_for_document_point(QPoint(32, 42));
  send_mouse(canvas, QEvent::MouseButtonPress, source, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(canvas, QEvent::MouseButtonRelease, source, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);

  const auto target = canvas.widget_position_for_document_point(QPoint(32, 16));
  send_mouse(canvas, QEvent::MouseButtonPress, target, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, target, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto* center = layer.pixels().pixel(32, 16);
  CHECK(center[0] == 255);
  CHECK(center[1] == 220);
  CHECK(center[2] == 0);
  CHECK(center[3] == 255);

  const auto* feathered = layer.pixels().pixel(40, 16);
  CHECK(feathered[3] > 20);
  CHECK(feathered[3] < 240);
  CHECK(feathered[0] >= 245);
  CHECK(feathered[1] >= 210);
  CHECK(feathered[2] <= 5);
}

void ui_smudge_tool_drags_painted_pixels() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(230, 50, 30));
  canvas->set_brush_size(28);
  canvas->set_brush_opacity(100);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 120)),
       canvas->widget_position_for_document_point(QPoint(102, 120)));
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Smudge"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Smudge);
  canvas->set_brush_size(28);
  canvas->set_brush_opacity(70);
  canvas->set_brush_softness(100);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 120)),
       canvas->widget_position_for_document_point(QPoint(170, 120)));
  QApplication::processEvents();

  const auto smeared = canvas_pixel(*canvas, QPoint(165, 120));
  CHECK(smeared.red() > 175);
  CHECK(smeared.green() < 170);
  CHECK(smeared.blue() < 160);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(230, 120)), Qt::white, 10));
  auto* history = window.findChild<QListWidget*>(QStringLiteral("historyList"));
  CHECK(history != nullptr);
  CHECK(history->item(0) != nullptr);
  CHECK(history->item(0)->text().contains(QStringLiteral("Smudge")));
  save_widget_artifact("ui_smudge_tool", window);
}

void ui_copy_ignores_hidden_active_layer() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  QApplication::clipboard()->clear();

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(0, 0, 0));
  drag(*canvas, QPoint(80, 80), QPoint(150, 110));
  require_action(window, "editSelectAllAction")->trigger();
  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();

  require_layer_item(*layer_list, QStringLiteral("Paint Layer"))->setCheckState(Qt::Unchecked);
  QApplication::processEvents();

  const auto layers_before = layer_list->count();
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();

  CHECK(layer_list->count() == layers_before);
  save_widget_artifact("ui_hidden_layer_copy_ignored", window);
}

void ui_copy_selected_layers_copies_composited_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(22);
  canvas->set_primary_color(QColor(230, 20, 30));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 42)),
       canvas->widget_position_for_document_point(QPoint(42, 42)));

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  canvas->set_primary_color(QColor(20, 70, 240));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(82, 42)),
       canvas->widget_position_for_document_point(QPoint(84, 42)));

  layer_list->clearSelection();
  require_layer_item(*layer_list, QStringLiteral("Layer 3"))->setSelected(true);
  require_layer_item(*layer_list, QStringLiteral("Paint Layer"))->setSelected(true);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(120, 80)));

  const auto layers_before = layer_list->count();
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 42)), QColor(230, 20, 30), 45));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(82, 42)), QColor(20, 70, 240), 45));
  save_widget_artifact("ui_copy_selected_layers", window);
}

void ui_eraser_on_background_reveals_transparency_and_size_cursor() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(38);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);

  require_action_by_text(window, QStringLiteral("Eraser"))->trigger();
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  auto* background = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background);
  background->setSelected(true);
  QApplication::processEvents();

  auto* background_row = layer_list->itemWidget(background);
  CHECK(background_row != nullptr);
  auto* lock = background_row->findChild<QToolButton*>(QStringLiteral("layerLockCheck"));
  CHECK(lock != nullptr);
  CHECK(lock->isChecked());
  lock->click();
  QApplication::processEvents();
  background = require_layer_item(*layer_list, QStringLiteral("Background"));
  background_row = layer_list->itemWidget(background);
  CHECK(background_row != nullptr);
  lock = background_row->findChild<QToolButton*>(QStringLiteral("layerLockCheck"));
  CHECK(lock != nullptr);
  CHECK(!lock->isChecked());

  const auto erase_point = canvas->widget_position_for_document_point(QPoint(90, 90));
  drag(*canvas, erase_point, erase_point + QPoint(1, 1));
  QApplication::processEvents();
  const auto erased = canvas_pixel(*canvas, QPoint(90, 90));
  CHECK(erased.red() >= 170);
  CHECK(erased.red() <= 245);
  CHECK(std::abs(erased.red() - erased.green()) <= 4);
  CHECK(std::abs(erased.green() - erased.blue()) <= 4);
  save_widget_artifact("ui_background_eraser_transparency", window);
}

void ui_magic_wand_cursor_marks_click_hotspot() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::MagicWand);
  canvas->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::MagicWand);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  CHECK(canvas->cursor().hotSpot() == QPoint(6, 6));

  send_key_press(*canvas, Qt::Key_Space);
  CHECK(canvas->cursor().shape() == Qt::OpenHandCursor);
  send_key_release(*canvas, Qt::Key_Space);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  CHECK(canvas->cursor().hotSpot() == QPoint(6, 6));
}

void ui_move_tool_after_text_edit_keeps_spacebar_pan_active() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto text_position = canvas->widget_position_for_document_point(QPoint(120, 120));
  send_mouse(*canvas, QEvent::MouseButtonPress, text_position, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_position, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("Pan Focus"));
  editor->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(QApplication::focusWidget() == canvas);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);

  auto* focused = QApplication::focusWidget();
  CHECK(focused == canvas);
  send_key_press(*focused, Qt::Key_Space);
  CHECK(canvas->cursor().shape() == Qt::OpenHandCursor);
  send_key_release(*focused, Qt::Key_Space);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
}

void ui_text_tool_creates_visible_text_layer() {
  SettingsValueRestorer saved_text_smoothing(QStringLiteral("tools/textSmoothing"));
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("tools/textSmoothing"));
  settings.sync();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  canvas->set_primary_color(QColor(40, 220, 120));
  const QPoint text_document_point(100, 105);
  const auto text_widget_point = canvas->widget_position_for_document_point(text_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->pos() == text_widget_point);
  CHECK(editor->frameShape() == QFrame::NoFrame);
  CHECK(editor->font().pixelSize() > 0);
  CHECK(editor->document()->documentMargin() == 0.0);
  CHECK(editor->property("patchy.documentTextSize").toInt() == 48);
  CHECK(editor->document()->textWidth() == editor->width());
  CHECK(!editor->styleSheet().contains(QStringLiteral("font-size:")));
  auto* text_size = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  auto* text_bold = window.findChild<QPushButton*>(QStringLiteral("textBoldButton"));
  auto* text_italic = window.findChild<QPushButton*>(QStringLiteral("textItalicButton"));
  auto* text_smoothing = window.findChild<QComboBox*>(QStringLiteral("textSmoothingCombo"));
  auto* text_color = window.findChild<QPushButton*>(QStringLiteral("textColorButton"));
  CHECK(text_size != nullptr);
  CHECK(text_bold != nullptr);
  CHECK(text_italic != nullptr);
  CHECK(text_smoothing != nullptr);
  CHECK(text_color != nullptr);
  CHECK(text_smoothing->currentData().toInt() == 3);
  CHECK(editor->property("patchy.documentTextAntiAlias").toInt() == 3);
  CHECK(editor->property("patchy.documentTextColor").value<QColor>() == QColor(40, 220, 120));
  text_color->click();
  QApplication::processEvents();
  bool changed_text_color = false;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
      continue;
    }
    auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
    CHECK(picker != nullptr);
    picker->setCurrentColor(QColor(20, 70, 240));
    QApplication::processEvents();
    widget->close();
    changed_text_color = true;
    break;
  }
  CHECK(changed_text_color);
  CHECK(editor->property("patchy.documentTextColor").value<QColor>() == QColor(20, 70, 240));
  text_size->setFocus();
  text_size->setValue(text_points_for_pixels(64));
  text_bold->setChecked(true);
  text_italic->setChecked(true);
  text_smoothing->setCurrentIndex(text_smoothing->findData(0));
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == editor);
  CHECK(editor->property("patchy.documentTextSize").toInt() == 64);
  CHECK(editor->property("patchy.documentTextAntiAlias").toInt() == 0);
  CHECK(editor->font().bold());
  CHECK(editor->font().italic());
  CHECK(!editor->styleSheet().contains(QStringLiteral("font-size:")));
  const auto expected_editor_text_size =
      std::max(8, static_cast<int>(std::round(editor->property("patchy.documentTextSize").toInt() * canvas->zoom())));
  editor->selectAll();
  send_key(*editor, Qt::Key_Backspace);
  QApplication::processEvents();
  CHECK(editor->toPlainText().isEmpty());
  const auto empty_format = editor->currentCharFormat();
  CHECK(empty_format.foreground().color() == QColor(20, 70, 240));
  CHECK(empty_format.font().pixelSize() == expected_editor_text_size);
  CHECK(empty_format.font().bold());
  CHECK(empty_format.font().italic());
  editor->insertPlainText(QStringLiteral("Patchy Type"));
  QApplication::processEvents();
  const auto first_fragment = editor->document()->begin().begin().fragment();
  CHECK(first_fragment.isValid());
  const auto inserted_format = first_fragment.charFormat();
  CHECK(inserted_format.foreground().color() == QColor(20, 70, 240));
  CHECK(inserted_format.font().pixelSize() == expected_editor_text_size);
  CHECK(inserted_format.font().bold());
  CHECK(inserted_format.font().italic());
  save_widget_artifact("ui_inline_text_editor", *canvas);
  const auto layer_count_before_commit = layer_list->count();
  editor->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  canvas->set_show_transform_controls(false);

  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->item(0)->text().startsWith(QStringLiteral("Text: Patchy Type")));
  CHECK(layer_list->count() == layer_count_before_commit + 1);
  QApplication::processEvents();
  const auto committed_text_image = canvas->grab().toImage();
  bool found_text_pixel = false;
  for (int y = 0; y < 120 && !found_text_pixel; y += 2) {
    for (int x = 0; x < 420 && !found_text_pixel; x += 2) {
      const auto widget_point = canvas->widget_position_for_document_point(text_document_point + QPoint(x, y));
      if (!committed_text_image.rect().contains(widget_point)) {
        continue;
      }
      found_text_pixel =
          !color_close(committed_text_image.pixelColor(widget_point), QColor(255, 255, 255), 15);
    }
  }
  CHECK(found_text_pixel);
  CHECK(count_blended_document_pixels(*canvas, QRect(text_document_point, QSize(360, 90)),
                                      QColor(20, 70, 240), QColor(Qt::white), 2) == 0);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  auto* background = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background);
  background->setSelected(true);
  QApplication::processEvents();
  const auto reedit_widget_point = text_widget_point + QPoint(18, 18);
  send_mouse(*canvas, QEvent::MouseButtonDblClick, reedit_widget_point, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  auto* reedit = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(reedit != nullptr);
  process_events_for(80);
  CHECK(!reedit->property("patchy.previewPaintsText").toBool());
  CHECK(!reedit->property("patchy.textPreviewLayerId").isValid());
  CHECK(reedit->toPlainText() == QStringLiteral("Patchy Type"));
  CHECK(!reedit->textCursor().hasSelection());
  CHECK(reedit->textCursor().position() <= 2);
  CHECK(reedit->pos() == text_widget_point);
  reedit->setPlainText(QStringLiteral("Canceled Type"));
  send_key(*reedit, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->count() == layer_count_before_commit + 1);
  CHECK(layer_list->item(0)->text().startsWith(QStringLiteral("Text: Patchy Type")));

  send_mouse(*canvas, QEvent::MouseButtonDblClick, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  reedit = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(reedit != nullptr);
  CHECK(reedit->toPlainText() == QStringLiteral("Patchy Type"));
  text_size->setValue(text_points_for_pixels(72));
  text_bold->setChecked(false);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == reedit);
  CHECK(reedit->property("patchy.documentTextSize").toInt() == 72);
  CHECK(!reedit->font().bold());
  CHECK(reedit->font().italic());
  reedit->setPlainText(QStringLiteral("Continue"));
  QApplication::processEvents();
  CHECK(reedit->lineWrapMode() == QTextEdit::NoWrap);
  CHECK(reedit->document()->idealWidth() <= static_cast<qreal>(reedit->width() + 1));
  CHECK(reedit->document()->blockCount() == 1);
  reedit->setPlainText(QStringLiteral("Updated Type"));
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->count() == layer_count_before_commit + 1);
  CHECK(layer_list->item(0)->text().startsWith(QStringLiteral("Text: Updated Type")));

  const auto before_brush = canvas->grab().toImage();
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(34);
  canvas->set_primary_color(QColor(230, 20, 30));
  drag(*canvas, text_widget_point, text_widget_point + QPoint(1, 1));
  QApplication::processEvents();
  const auto after_brush = canvas->grab().toImage();
  CHECK(color_close(after_brush.pixelColor(text_widget_point), before_brush.pixelColor(text_widget_point), 2));
  save_widget_artifact("ui_text_tool_layer", window);
}

void ui_text_tool_outside_click_commits_without_new_text_editor() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  auto* type_action = require_action_by_text(window, QStringLiteral("Type"));
  type_action->trigger();
  const QPoint text_document_point(90, 90);
  const auto text_widget_point = canvas->widget_position_for_document_point(text_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("Outside Commit"));
  QApplication::processEvents();
  const auto layer_count_before_commit = layer_list->count();

  const auto outside_widget_point = canvas->widget_position_for_document_point(QPoint(310, 220));
  send_mouse(*canvas, QEvent::MouseButtonPress, outside_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, outside_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(type_action->isChecked());
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->count() == layer_count_before_commit + 1);
  CHECK(layer_list->item(0)->text().startsWith(QStringLiteral("Text: Outside Commit")));
  save_widget_artifact("ui_text_outside_click_commit", window);
}

void ui_text_edit_hides_editor_glyphs_and_shows_selection_over_style_preview() {
  patchy::Document document(420, 240, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(420, 240, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(160, 60, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 120, 42), QColor(20, 20, 20, 255));

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Styled", std::move(pixels));
  text_layer.set_bounds(patchy::Rect{90, 80, 160, 60});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Styled";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "36";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 1.0F;
  shadow.distance = 6.0F;
  shadow.size = 8.0F;
  text_layer.layer_style().drop_shadows.push_back(shadow);
  document.add_layer(std::move(text_layer));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Styled Text Preview"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(0.5);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(100, 92));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  process_events_for(80);
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").isValid());
  QTextCursor caret_cursor(editor->document());
  caret_cursor.setPosition(3);
  editor->setTextCursor(caret_cursor);
  QApplication::processEvents();
  const auto preview_caret = editor->property("patchy.previewCaretRect").toRect();
  CHECK(!preview_caret.isEmpty());
  CHECK(preview_caret.width() >= 3);
  CHECK(editor->viewport()->rect().contains(preview_caret.center()));
  const auto native_caret = editor->cursorRect();
  CHECK(std::abs(preview_caret.center().x() - native_caret.center().x()) <= 2);
  CHECK(std::abs(preview_caret.center().y() - native_caret.center().y()) <= 2);
  const auto preview_id = editor->property("patchy.textPreviewLayerId").toULongLong();
  const auto unselected_image = canvas->grab().toImage();

  QTextCursor selection_cursor(editor->document());
  selection_cursor.setPosition(0);
  selection_cursor.setPosition(4, QTextCursor::KeepAnchor);
  editor->setTextCursor(selection_cursor);
  QApplication::processEvents();
  const auto preview_selection_rects = editor->property("patchy.previewSelectionRects").toList();
  CHECK(!preview_selection_rects.empty());
  for (const auto& rect_value : preview_selection_rects) {
    CHECK(editor->viewport()->rect().intersects(rect_value.toRect()));
  }
  const auto selected_image = canvas->grab().toImage();
  int changed_pixels = 0;
  const QRect selection_sample_rect(editor->geometry().topLeft(),
                                    QSize(std::max(1, editor->width() / 2), std::max(1, editor->height() / 2)));
  for (int y = selection_sample_rect.top(); y <= selection_sample_rect.bottom(); y += 2) {
    for (int x = selection_sample_rect.left(); x <= selection_sample_rect.right(); x += 2) {
      if (!selected_image.rect().contains(QPoint(x, y)) || !unselected_image.rect().contains(QPoint(x, y))) {
        continue;
      }
      const auto before = unselected_image.pixelColor(x, y);
      const auto after = selected_image.pixelColor(x, y);
      const auto delta = std::abs(before.red() - after.red()) + std::abs(before.green() - after.green()) +
                         std::abs(before.blue() - after.blue());
      if (delta > 12) {
        ++changed_pixels;
      }
    }
  }
  CHECK(changed_pixels > 20);

  QTextCursor cursor(editor->document());
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  editor->insertPlainText(QStringLiteral("!"));
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").toULongLong() == preview_id);
  CHECK(editor->property("patchy.textPreviewPending").toBool());

  process_events_for(80);
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").isValid());
  CHECK(editor->property("patchy.textPreviewLayerId").toULongLong() == preview_id);
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_expensive_text_style_preview_debounces_to_plain_live_text() {
  patchy::Document document(420, 240, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(420, 240, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(170, 68, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 126, 44), QColor(32, 32, 32, 255));

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Styled", std::move(pixels));
  text_layer.set_bounds(patchy::Rect{90, 80, 170, 68});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Styled";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "36";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 0, 0};
  glow.opacity = 0.8F;
  glow.size = 64.0F;
  text_layer.layer_style().outer_glows.push_back(glow);
  document.add_layer(std::move(text_layer));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Expensive Styled Text Preview"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(100, 92));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->property("patchy.expensiveTextStylePreview").toBool());
  CHECK(!editor->property("patchy.previewPaintsText").toBool());

  QTextCursor cursor(editor->document());
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  editor->setCursorWidth(0);
  editor->insertPlainText(QStringLiteral(" live"));
  QApplication::processEvents();
  CHECK(!editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.textPreviewPending").toBool());
  const auto live_editor_image = editor->viewport()->grab().toImage();
  CHECK(count_pixels_close(live_editor_image, live_editor_image.rect(), QColor(32, 32, 32), 48) > 20);

  process_events_for(260);
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").isValid());

  editor->insertPlainText(QStringLiteral(" now"));
  QApplication::processEvents();
  CHECK(!editor->property("patchy.previewPaintsText").toBool());
  CHECK(!editor->property("patchy.textPreviewLayerId").isValid());
  process_events_for(260);
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").isValid());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  const auto committed_image = canvas->grab().toImage();
  bool saw_red_glow = false;
  for (int document_y = 36; document_y < 182 && !saw_red_glow; document_y += 2) {
    for (int document_x = 42; document_x < 382 && !saw_red_glow; document_x += 2) {
      const auto widget_point = canvas->widget_position_for_document_point(QPoint(document_x, document_y));
      if (!committed_image.rect().contains(widget_point)) {
        continue;
      }
      const auto color = committed_image.pixelColor(widget_point);
      saw_red_glow = color.red() > 245 && color.red() > color.green() + 3 && color.red() > color.blue() + 3 &&
                     color.green() < 252 && color.blue() < 252;
    }
  }
  CHECK(saw_red_glow);
  save_widget_artifact("ui_expensive_text_style_preview", *canvas);
}

void ui_text_editor_paste_uses_current_format_for_rich_emoji_clipboard() {
  patchy::Document document(420, 240, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(420, 240, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Rich Emoji Clipboard"));
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  canvas->set_primary_color(QColor(12, 34, 56));
  const auto text_widget_point = canvas->widget_position_for_document_point(QPoint(80, 90));
  send_mouse(*canvas, QEvent::MouseButtonPress, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  auto* text_size = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  CHECK(editor != nullptr);
  CHECK(text_size != nullptr);
  text_size->setValue(text_points_for_pixels(52));
  QApplication::processEvents();

  editor->selectAll();
  editor->insertPlainText(QStringLiteral("Hello"));
  QTextCursor cursor = editor->textCursor();
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  editor->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();

  const auto pasted_text = QString::fromUtf8(QByteArray::fromHex("616e696e677320f09f9281f09f918cf09f8e8df09f988d"));
  const auto paste_start = editor->toPlainText().size();
  auto* mime_data = new QMimeData();
  mime_data->setText(pasted_text);
  mime_data->setHtml(QStringLiteral("<a href=\"https://example.invalid\" "
                                    "style=\"font-size: 9px; color: #0000ee; text-decoration: underline;\">") +
                     pasted_text.toHtmlEscaped() + QStringLiteral("</a>"));
  QApplication::clipboard()->setMimeData(mime_data);

  send_key(*editor, Qt::Key_V, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(editor->toPlainText() == QStringLiteral("Hello") + pasted_text);

  const auto expected_text_size =
      std::max(8, static_cast<int>(std::round(editor->property("patchy.documentTextSize").toInt() * canvas->zoom())));
  const auto expected_color = editor->property("patchy.documentTextColor").value<QColor>();
  bool checked_pasted_fragment = false;
  for (auto block = editor->document()->begin(); block.isValid(); block = block.next()) {
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.position() + fragment.length() <= paste_start) {
        continue;
      }
      checked_pasted_fragment = true;
      const auto format = fragment.charFormat();
      CHECK(format.font().pixelSize() == expected_text_size);
      CHECK(format.foreground().color() == expected_color);
      CHECK(!format.fontUnderline());
      CHECK(!format.isAnchor());
    }
  }
  CHECK(checked_pasted_fragment);
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  QApplication::clipboard()->clear();
}

void ui_text_tool_drag_creates_resizable_wrapped_text_box() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto layer_count_before_cancel_drag = layer_list->count();
  const auto cancel_start = canvas->widget_position_for_document_point(QPoint(40, 42));
  const auto cancel_end = canvas->widget_position_for_document_point(QPoint(130, 92));
  send_mouse(*canvas, QEvent::MouseButtonPress, cancel_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, cancel_end, Qt::NoButton, Qt::LeftButton);
  send_key(*canvas, Qt::Key_Escape);
  send_mouse(*canvas, QEvent::MouseButtonRelease, cancel_end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->count() == layer_count_before_cancel_drag);

  const QPoint box_top_left(92, 96);
  const QPoint box_bottom_right(232, 158);
  const auto start = canvas->widget_position_for_document_point(box_top_left);
  const auto end = canvas->widget_position_for_document_point(box_bottom_right);
  drag(*canvas, start, end);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->property("patchy.documentTextFlow").toString() == QStringLiteral("box"));
  CHECK(editor->property("patchy.documentTextWidth").toInt() >= 130);
  CHECK(editor->property("patchy.documentTextHeight").toInt() >= 50);
  CHECK(editor->lineWrapMode() == QTextEdit::WidgetWidth);
  CHECK(editor->document()->defaultTextOption().wrapMode() == QTextOption::WordWrap);
  CHECK(canvas->findChild<QWidget*>(QStringLiteral("textBoxResizeHandleBottomRight")) != nullptr);

  editor->setPlainText(QStringLiteral("Type"));
  editor->moveCursor(QTextCursor::End);
  send_key(*editor, Qt::Key_Return);
  send_key(*editor, Qt::Key_Return);
  QApplication::processEvents();
  const auto blank_caret_height = editor->cursorRect().height();
  const auto expected_blank_text_size =
      std::max(8, static_cast<int>(std::round(editor->property("patchy.documentTextSize").toInt() * canvas->zoom())));
  const auto blank_format = editor->currentCharFormat();
  CHECK(blank_format.font().pixelSize() == expected_blank_text_size);
  CHECK(blank_caret_height >= QFontMetrics(blank_format.font()).height() - 4);
  editor->insertPlainText(QStringLiteral("hello"));
  QApplication::processEvents();
  const auto paragraph_image = editor->viewport()->grab().toImage();
  const auto line_spacing = editor->fontMetrics().lineSpacing();
  const QRect lower_paragraph_rect(0, std::max(0, line_spacing * 2 - 6), paragraph_image.width(),
                                   std::min(paragraph_image.height(), line_spacing + 18));
  CHECK(count_pixels_close(paragraph_image, lower_paragraph_rect, QColor(Qt::black), 80) > 8);

  const auto editor_width_before_zoom = editor->width();
  canvas->set_zoom(canvas->zoom() * 1.5);
  QApplication::processEvents();
  CHECK(editor->pos() == canvas->widget_position_for_document_point(box_top_left));
  CHECK(editor->width() > editor_width_before_zoom);

  auto* center = window.findChild<QPushButton*>(QStringLiteral("textAlignCenterButton"));
  CHECK(center != nullptr);
  center->click();
  QApplication::processEvents();
  CHECK((editor->alignment() & Qt::AlignHCenter) != 0);

  editor->setPlainText(QStringLiteral("Wrapped paragraph text should occupy multiple visual lines inside the fixed box."));
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == editor);
  CHECK(editor->document()->size().height() > static_cast<qreal>(editor->fontMetrics().lineSpacing()) * 1.5);
  CHECK(editor->verticalScrollBar()->value() == 0);
  editor->moveCursor(QTextCursor::End);
  editor->insertPlainText(QStringLiteral("\nMore clipped text should not scroll the edit view."));
  QApplication::processEvents();
  CHECK(editor->verticalScrollBar()->value() == 0);

  save_widget_artifact("ui_text_box_editor", *canvas);
  const auto layer_count_before_commit = layer_list->count();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->count() == layer_count_before_commit + 1);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto reedit_point = canvas->widget_position_for_document_point(box_top_left + QPoint(8, 8));
  send_mouse(*canvas, QEvent::MouseButtonPress, reedit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, reedit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->property("patchy.documentTextFlow").toString() == QStringLiteral("box"));
  CHECK(editor->lineWrapMode() == QTextEdit::WidgetWidth);
  auto* bottom_right = canvas->findChild<QWidget*>(QStringLiteral("textBoxResizeHandleBottomRight"));
  CHECK(bottom_right != nullptr);
  const auto width_before = editor->property("patchy.documentTextWidth").toInt();
  const auto handle_center = bottom_right->geometry().center();
  editor->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == editor);
  drag(*canvas, handle_center, handle_center + QPoint(44, 24));
  QApplication::processEvents();
  CHECK(editor->property("patchy.documentTextWidth").toInt() > width_before);
  CHECK(canvas->findChild<QWidget*>(QStringLiteral("textBoxResizeHandleBottomRight")) != nullptr);

  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
}

void ui_imported_psd_text_uses_photoshop_frame_after_commit() {
  patchy::Document document(420, 240, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(420, 240, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(180, 60, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 140, 42), QColor(20, 20, 20, 255));

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Imported", std::move(pixels));
  text_layer.set_bounds(patchy::Rect{110, 90, 180, 60});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Imported";
  text_layer.metadata()[patchy::kLayerMetadataTextHtml] =
      "<span style=\"font-family:'Arial'; font-size:32px; color:#202020;\">Imported</span>";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "32";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  text_layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "0";
  text_layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = "v1\n0\t8\tcenter";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "580";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "80";
  text_layer.metadata()[patchy::kLayerMetadataTextSourceBlock] = "TySh";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextTransform] = "1 0 0 1 0 0";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoxBounds] = "-80 70 500 150";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoundingBox] = "110 90 290 132";
  document.add_layer(std::move(text_layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Imported PSD Text"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(115, 95));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  const QPoint expected_editor_origin(-80, 70);
  CHECK(editor->property("patchy.documentTextX").toInt() == expected_editor_origin.x());
  CHECK(editor->property("patchy.documentTextY").toInt() == expected_editor_origin.y());
  CHECK(editor->property("patchy.documentTextWidth").toInt() == 580);
  CHECK(editor->property("patchy.documentTextHeight").toInt() == 80);
  CHECK(editor->property("patchy.documentTextAntiAlias").toInt() == 0);
  CHECK((editor->alignment() & Qt::AlignHCenter) != 0);

  QTextCursor imported_cursor(editor->document());
  imported_cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(imported_cursor);
  editor->insertPlainText(QStringLiteral("!"));
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  canvas->set_show_transform_controls(false);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(count_blended_document_pixels(*canvas, QRect(1, expected_editor_origin.y(), 418, 80),
                                      QColor(32, 32, 32), QColor(Qt::white), 2) == 0);
  auto* text_item = require_layer_item(*layer_list, QStringLiteral("Text: Imported!"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(text_item);
  text_item->setSelected(true);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->property("patchy.documentTextX").toInt() == expected_editor_origin.x());
  CHECK(editor->property("patchy.documentTextY").toInt() == expected_editor_origin.y());
  CHECK(editor->property("patchy.documentTextWidth").toInt() == 580);
  CHECK((editor->alignment() & Qt::AlignHCenter) != 0);
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_imported_psd_point_text_reedit_uses_auto_width() {
  patchy::Document document(420, 240, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(420, 240, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(72, 28, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 58, 24), QColor(20, 20, 20, 255));

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Imported Point", std::move(pixels));
  text_layer.set_bounds(patchy::Rect{116, 86, 72, 28});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Point label";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "32";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  text_layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "4";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "72";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "28";
  text_layer.metadata()[patchy::kLayerMetadataTextSourceBlock] = "TySh";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextTransform] = "1 0 0 1 116 112";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBounds] = "0 -32 140 8";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoundingBox] = "0 -24 72 0";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoxBounds] = "0 0 72 28";
  document.add_layer(std::move(text_layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Imported PSD Point Text"));
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(126, 96));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  auto* text_smoothing = window.findChild<QComboBox*>(QStringLiteral("textSmoothingCombo"));
  CHECK(text_smoothing != nullptr);
  CHECK(text_smoothing->currentData().toInt() == 4);
  CHECK(text_smoothing->currentText() == QStringLiteral("Sharp"));
  CHECK(editor->property("patchy.documentTextAntiAlias").toInt() == 4);
  CHECK(editor->property("patchy.documentTextFlow").toString() == QStringLiteral("point"));
  CHECK(editor->property("patchy.documentTextX").toInt() <= 116);
  CHECK(editor->property("patchy.documentTextX").toInt() > 96);
  CHECK(editor->property("patchy.documentTextY").toInt() < 86);
  CHECK(editor->property("patchy.documentTextY").toInt() > 56);
  CHECK(editor->lineWrapMode() == QTextEdit::NoWrap);
  CHECK(editor->property("patchy.documentTextWidth").toInt() >= 160);
  CHECK(editor->width() >= static_cast<int>(std::round(160.0 * canvas->zoom())));
  CHECK(editor->document()->blockCount() == 1);
  editor->setPlainText(QStringLiteral("Point label extended"));
  QApplication::processEvents();
  CHECK(editor->property("patchy.documentTextWidth").toInt() >= 160);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  canvas->set_show_transform_controls(false);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  auto* text_item = require_layer_item(*layer_list, QStringLiteral("Text: Point label extended"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(text_item);
  text_item->setSelected(true);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->property("patchy.documentTextFlow").toString() == QStringLiteral("point"));
  CHECK(editor->property("patchy.documentTextX").toInt() <= 116);
  CHECK(editor->property("patchy.documentTextX").toInt() > 96);
  CHECK(editor->property("patchy.documentTextY").toInt() < 86);
  CHECK(editor->property("patchy.documentTextY").toInt() > 56);
  CHECK(editor->lineWrapMode() == QTextEdit::NoWrap);
  CHECK(editor->property("patchy.documentTextWidth").toInt() >= 160);
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_imported_psd_point_text_baseline_origin_converts_in_place() {
  patchy::Document document(900, 620, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(900, 620, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(235, 47, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 235, 47), QColor(20, 20, 20, 255));

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Continue", std::move(pixels));
  text_layer.set_bounds(patchy::Rect{536, 479, 235, 47});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Continue";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "72";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  text_layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "4";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "235";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "47";
  text_layer.metadata()[patchy::kLayerMetadataTextSourceBlock] = "TySh";
  text_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "psd_raster_preview";
  text_layer.metadata()[patchy::kLayerMetadataTextTransform] = "1 0 0 1 536 525";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextTransform] = "1 0 0 1 536 525";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBounds] = "0 -60.264404296875 276.5489501953125 18";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoundingBox] =
      "3.0625 -50.000030517578125 273.4150695800781 1.0000152587890625";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoxBounds] = "0 0 276.5489501953125 78.264404296875";
  document.add_layer(std::move(text_layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Imported PSD Continue Text"));
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  canvas->set_show_transform_controls(false);
  QApplication::processEvents();
  const auto original_bounds = dark_document_bounds(*canvas, QRect(520, 460, 320, 100));
  CHECK(original_bounds.has_value());
  CHECK(original_bounds->left() == 536);
  CHECK(original_bounds->top() == 479);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(546, 489));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->property("patchy.documentTextFlow").toString() == QStringLiteral("point"));
  CHECK(editor->property("patchy.documentTextX").toInt() <= original_bounds->left());
  CHECK(editor->property("patchy.documentTextX").toInt() > original_bounds->left() - 24);
  CHECK(editor->property("patchy.documentTextY").toInt() < original_bounds->top());
  CHECK(editor->property("patchy.documentTextY").toInt() < 474);
  CHECK(editor->property("patchy.documentTextY").toInt() > original_bounds->top() - 32);

  QTextCursor cursor(editor->document());
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  editor->insertPlainText(QStringLiteral("!"));
  process_events_for(80);
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.documentTextY").toInt() < original_bounds->top());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  canvas->set_show_transform_controls(false);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  auto* text_item = require_layer_item(*layer_list, QStringLiteral("Text: Continue!"));
  layer_list->setCurrentItem(text_item);
  text_item->setSelected(true);
  QApplication::processEvents();

  const auto converted_bounds = dark_document_bounds(*canvas, QRect(520, 460, 320, 100));
  CHECK(converted_bounds.has_value());
  CHECK(std::abs(converted_bounds->left() - original_bounds->left()) <= 2);
  CHECK(std::abs(converted_bounds->top() - original_bounds->top()) <= 2);

  const QPoint move_delta(44, 32);
  const auto move_start_doc = QPoint(converted_bounds->left() + 20, converted_bounds->top() + 20);
  const auto move_end_doc = move_start_doc + move_delta;
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(false);
  canvas->set_show_transform_controls(false);
  send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(move_start_doc),
             Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(move_end_doc), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(move_end_doc),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto moved_bounds = dark_document_bounds(*canvas, QRect(540, 480, 360, 140));
  CHECK(moved_bounds.has_value());
  CHECK(moved_bounds->left() > converted_bounds->left() + 24);
  CHECK(moved_bounds->top() > converted_bounds->top() + 12);
  const auto moved_layer_rect = canvas->active_layer_document_rect();
  CHECK(moved_layer_rect.has_value());
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();
  const auto bounds_state = canvas->transform_controls_state();
  CHECK(bounds_state.has_value());
  CHECK(bounds_state->reference_position.x() > original_bounds->left() + 8);
  CHECK(bounds_state->reference_position.y() > original_bounds->top() + 8);
  CHECK(canvas->active_layer_document_rect() == moved_layer_rect);
  layer_list->setCurrentItem(text_item);
  text_item->setSelected(true);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto moved_hit_point =
      canvas->widget_position_for_document_point(QPoint(moved_bounds->left() + 10, moved_bounds->top() + 10));
  send_mouse(*canvas, QEvent::MouseButtonPress, moved_hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, moved_hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.documentTextX").toInt() > original_bounds->left() + 8);
  CHECK(editor->property("patchy.documentTextX").toInt() <= moved_bounds->left());
  CHECK(editor->property("patchy.documentTextX").toInt() > moved_bounds->left() - 32);
  CHECK(editor->property("patchy.documentTextY").toInt() > original_bounds->top() + 8);
  CHECK(editor->property("patchy.documentTextY").toInt() <= moved_bounds->top());
  CHECK(editor->property("patchy.documentTextY").toInt() > moved_bounds->top() - 40);
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_imported_psd_mirrored_point_text_uses_local_bounds() {
  patchy::Document document(2200, 320, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(2200, 320, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(1951, 167, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 1951, 167), QColor(20, 20, 20, 255));

  patchy::Layer text_layer(document.allocate_layer_id(), "C2KYOTO SIMULATOR", std::move(pixels));
  text_layer.set_bounds(patchy::Rect{130, 59, 1951, 167});
  text_layer.metadata()[patchy::kLayerMetadataText] = "C2KYOTO SIMULATOR";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "178";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  text_layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "4";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "1951";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "167";
  text_layer.metadata()[patchy::kLayerMetadataTextSourceBlock] = "TySh";
  text_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "psd_raster_preview";
  text_layer.metadata()[patchy::kLayerMetadataTextTransform] =
      "-1.25787768018 0 0.154447958630465 -1.26247300797873 2093.50075867214 62.2146424698701";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextTransform] =
      "-1.25787768018 0 0.154447958630465 -1.26247300797873 2093.50075867214 62.2146424698701";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBounds] =
      "0 -152.1534881591797 1556.326904296875 40.30317306518555";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoundingBox] = "4 -130 1553.7626953125 2";
  document.add_layer(std::move(text_layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Imported PSD Mirrored Point Text"));
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  canvas->set_zoom(0.25);
  canvas->set_show_transform_controls(false);
  QApplication::processEvents();
  const auto original_bounds = dark_document_bounds(*canvas, QRect(100, 20, 2050, 240));
  CHECK(original_bounds.has_value());
  CHECK(std::abs(original_bounds->top() - 59) <= 4);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(160, 80));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  process_events_for(80);

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->property("patchy.documentTextFlow").toString() == QStringLiteral("point"));
  CHECK(editor->property("patchy.transformedPreviewOverlayActive").toBool());
  auto* overlay = canvas->findChild<QWidget*>(QStringLiteral("transformedTextEditOverlay"));
  CHECK(overlay != nullptr);
  const auto editor_polygon_points = overlay->property("patchy.transformedTextEditorPolygon").toList();
  CHECK(editor_polygon_points.size() == 4);
  const auto document_origin = canvas->widget_position_for_document_point(QPoint(0, 0));
  auto polygon_top = std::numeric_limits<double>::max();
  for (const auto& value : editor_polygon_points) {
    const auto point = value.toPointF();
    polygon_top = std::min(polygon_top, (point.y() - static_cast<double>(document_origin.y())) / canvas->zoom());
  }
  CHECK(polygon_top > static_cast<double>(original_bounds->top()) - 70.0);
  CHECK(polygon_top < static_cast<double>(original_bounds->bottom()) + 20.0);

  QTextCursor cursor(editor->document());
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  editor->insertPlainText(QStringLiteral("!"));
  cursor = editor->textCursor();
  cursor.deletePreviousChar();
  editor->setTextCursor(cursor);
  process_events_for(80);
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  auto* text_item = require_layer_item(*layer_list, QStringLiteral("Text: C2KYOTO SIMULATOR"));
  layer_list->setCurrentItem(text_item);
  text_item->setSelected(true);
  QApplication::processEvents();

  const auto converted_bounds = dark_document_bounds(*canvas, QRect(80, 0, 2100, 300));
  CHECK(converted_bounds.has_value());
  CHECK(converted_bounds->top() > original_bounds->top() - 45);
  CHECK(converted_bounds->top() < original_bounds->top() + 90);
  CHECK(converted_bounds->bottom() > original_bounds->top() + 40);
  CHECK(converted_bounds->bottom() < original_bounds->bottom() + 90);
  save_widget_artifact("ui_imported_psd_mirrored_point_text", window);
}

void ui_imported_psd_raster_preview_warns_before_missing_font_substitution() {
  patchy::Document document(320, 180, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(320, 180, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(118, 36, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 96, 30), QColor(20, 20, 20, 255));

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Missing Font", std::move(pixels));
  text_layer.set_bounds(patchy::Rect{86, 62, 118, 36});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Missing";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "PatchyDefinitelyMissingFont123456";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  text_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "psd_raster_preview";
  document.add_layer(std::move(text_layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Missing Font PSD Text"));
  auto* canvas = require_canvas(window);
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(92, 68));

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  bool saw_cancel_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("missingPsdTextFontMessageBox")));
    CHECK(dialog != nullptr);
    CHECK(dialog->text().contains(QStringLiteral("PatchyDefinitelyMissingFont123456")));
    CHECK(dialog->text().contains(QStringLiteral("substitute")));
    saw_cancel_dialog = true;
    dialog->button(QMessageBox::Cancel)->click();
  });
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(saw_cancel_dialog);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  bool saw_continue_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("missingPsdTextFontMessageBox")));
    CHECK(dialog != nullptr);
    QPushButton* continue_button = nullptr;
    for (auto* button : dialog->findChildren<QPushButton*>()) {
      auto text = button->text();
      text.remove(QLatin1Char('&'));
      if (text == QStringLiteral("Continue")) {
        continue_button = button;
        break;
      }
    }
    CHECK(continue_button != nullptr);
    saw_continue_dialog = true;
    continue_button->click();
  });
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(saw_continue_dialog);
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->toPlainText() == QStringLiteral("Missing"));
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_transformed_text_reedit_preserves_transform() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const QPoint box_top_left(120, 120);
  const QPoint box_bottom_right(300, 180);
  drag(*canvas, canvas->widget_position_for_document_point(box_top_left),
       canvas->widget_position_for_document_point(box_bottom_right));
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("Rotate me"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  const auto before_transform = canvas->active_layer_document_rect();
  CHECK(before_transform.has_value());
  canvas->set_show_transform_controls(false);
  QApplication::processEvents();
  const auto before_visible_text = dark_document_bounds(*canvas, before_transform->adjusted(-8, -8, 8, 8));
  CHECK(before_visible_text.has_value());
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();
  const auto move_text_handle =
      canvas->widget_position_for_document_point(QPoint(before_transform->right() + 1, before_transform->bottom() + 1));
  send_mouse(*canvas, QEvent::MouseMove, move_text_handle, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto top_center = canvas->widget_position_for_document_point(
      QPoint(before_visible_text->center().x(), before_visible_text->top()));
  drag(*canvas, top_center + QPoint(0, -32), top_center + QPoint(70, 26));
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  auto transformed = canvas->active_layer_document_rect();
  CHECK(transformed.has_value());
  CHECK(transformed->height() > before_visible_text->height() + 35);
  const auto original_text_area = static_cast<qint64>(before_visible_text->width()) * before_visible_text->height();
  for (const auto rotation : {12.0, -12.0, 8.0}) {
    require_action(window, "editFreeTransformAction")->trigger();
    QApplication::processEvents();
    CHECK(canvas->free_transform_active());
    const auto state = canvas->transform_controls_state();
    CHECK(state.has_value());
    CHECK(canvas->set_transform_controls_state(state->reference_position, 100.0, 100.0, rotation));
    QApplication::processEvents();
    send_key(*canvas, Qt::Key_Return);
    QApplication::processEvents();
    CHECK(!canvas->free_transform_active());
  }
  transformed = canvas->active_layer_document_rect();
  CHECK(transformed.has_value());
  const auto repeated_transform_area = static_cast<qint64>(transformed->width()) * transformed->height();
  CHECK(repeated_transform_area < original_text_area * 8);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto edit_point = canvas->widget_position_for_document_point(transformed->center());
  send_mouse(*canvas, QEvent::MouseButtonPress, edit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, edit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  process_events_for(80);
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.transformedPreviewOverlayActive").toBool());
  auto* overlay = canvas->findChild<QWidget*>(QStringLiteral("transformedTextEditOverlay"),
                                              Qt::FindDirectChildrenOnly);
  CHECK(overlay != nullptr);
  CHECK(overlay->isVisible());
  CHECK(!overlay->geometry().isEmpty());
  CHECK(overlay->geometry() != editor->geometry());
  const auto overlay_controls_image = canvas->grab().toImage();
  CHECK(count_pixels_close(overlay_controls_image, overlay->geometry().adjusted(-2, -2, 2, 2),
                           QColor(245, 248, 252), 18) > 8);
  const auto drag_transformed_resize_handle = [&](int handle_index, QPoint delta) {
    const auto resize_handle_centers = overlay->property("patchy.transformedTextResizeHandleCenters").toList();
    CHECK(resize_handle_centers.size() == 4);
    const auto resize_handle_point = resize_handle_centers[handle_index].toPointF().toPoint();
    const auto text_width_before_resize = editor->property("patchy.documentTextWidth").toInt();
    const auto text_height_before_resize = editor->property("patchy.documentTextHeight").toInt();
    send_mouse(*canvas, QEvent::MouseButtonPress, resize_handle_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseMove, resize_handle_point + delta, Qt::NoButton, Qt::LeftButton);
    const auto live_resize_handle_centers = overlay->property("patchy.transformedTextResizeHandleCenters").toList();
    CHECK(live_resize_handle_centers.size() == 4);
    CHECK((live_resize_handle_centers[handle_index].toPointF() - resize_handle_centers[handle_index].toPointF())
              .manhattanLength() > 4.0);
    send_mouse(*canvas, QEvent::MouseButtonRelease, resize_handle_point + delta, Qt::LeftButton, Qt::NoButton);
    process_events_for(80);
    editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    CHECK(editor != nullptr);
    CHECK(editor->property("patchy.transformedPreviewOverlayActive").toBool());
    overlay = canvas->findChild<QWidget*>(QStringLiteral("transformedTextEditOverlay"),
                                          Qt::FindDirectChildrenOnly);
    CHECK(overlay != nullptr);
    CHECK(overlay->isVisible());
    const auto text_width_after_resize = editor->property("patchy.documentTextWidth").toInt();
    const auto text_height_after_resize = editor->property("patchy.documentTextHeight").toInt();
    CHECK(text_width_after_resize != text_width_before_resize ||
          text_height_after_resize != text_height_before_resize);
  };
  drag_transformed_resize_handle(0, QPoint(-36, -24));
  drag_transformed_resize_handle(2, QPoint(-32, 28));
  drag_transformed_resize_handle(3, QPoint(48, 32));

  QTextCursor selection_cursor(editor->document());
  selection_cursor.setPosition(0);
  editor->setTextCursor(selection_cursor);
  QApplication::processEvents();
  const auto unselected_image = canvas->grab().toImage();
  selection_cursor.setPosition(0);
  selection_cursor.setPosition(6, QTextCursor::KeepAnchor);
  editor->setTextCursor(selection_cursor);
  QApplication::processEvents();
  const auto selected_image = canvas->grab().toImage();
  int changed_pixels_outside_editor = 0;
  const auto editor_hit_rect = editor->geometry().adjusted(-2, -2, 2, 2);
  const auto overlay_sample_rect = overlay->geometry().intersected(selected_image.rect());
  for (int y = overlay_sample_rect.top(); y <= overlay_sample_rect.bottom(); y += 2) {
    for (int x = overlay_sample_rect.left(); x <= overlay_sample_rect.right(); x += 2) {
      if (editor_hit_rect.contains(QPoint(x, y)) || !unselected_image.rect().contains(QPoint(x, y))) {
        continue;
      }
      const auto before = unselected_image.pixelColor(x, y);
      const auto after = selected_image.pixelColor(x, y);
      const auto delta = std::abs(before.red() - after.red()) + std::abs(before.green() - after.green()) +
                         std::abs(before.blue() - after.blue());
      if (delta > 12) {
        ++changed_pixels_outside_editor;
      }
    }
  }
  CHECK(changed_pixels_outside_editor > 8);

  const auto editor_polygon_points = overlay->property("patchy.transformedTextEditorPolygon").toList();
  CHECK(editor_polygon_points.size() == 4);
  const auto top_left = editor_polygon_points[0].toPointF();
  const auto top_right = editor_polygon_points[1].toPointF();
  const auto bottom_right = editor_polygon_points[2].toPointF();
  const auto bottom_left = editor_polygon_points[3].toPointF();
  const auto polygon_center = (top_left + top_right + bottom_right + bottom_left) / 4.0;
  const auto left_mid = (top_left + bottom_left) / 2.0;
  const auto right_mid = (top_right + bottom_right) / 2.0;
  const auto overlay_drag_start = (polygon_center * 0.65 + left_mid * 0.35).toPoint();
  const auto overlay_drag_end = (polygon_center * 0.65 + right_mid * 0.35).toPoint();
  send_mouse(*canvas, QEvent::MouseButtonPress, overlay_drag_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, overlay_drag_end, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, overlay_drag_end, Qt::LeftButton, Qt::NoButton);
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->textCursor().hasSelection());

  editor->setPlainText(QStringLiteral("Rotate me again"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  const auto after_reedit = canvas->active_layer_document_rect();
  CHECK(after_reedit.has_value());
  CHECK(after_reedit->height() > before_visible_text->height() + 35);
  CHECK(after_reedit->height() >= transformed->height() - 16);
  save_widget_artifact("ui_transformed_text_reedit", window);
}

void ui_transformed_expensive_text_preview_stays_transformed_while_typing() {
  patchy::Document document(460, 280, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(460, 280, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(220, 150, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(20, 30, 142, 42), QColor(32, 32, 32, 255));

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Rotated Glow", std::move(pixels));
  text_layer.set_bounds(patchy::Rect{132, 80, 220, 150});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Rotated";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "36";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "180";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "64";
  text_layer.metadata()[patchy::kLayerMetadataTextTransform] = "0.8660254 0.5 -0.5 0.8660254 176 82";
  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = patchy::BlendMode::Normal;
  glow.color = patchy::RgbColor{255, 0, 0};
  glow.opacity = 0.8F;
  glow.size = 64.0F;
  text_layer.layer_style().outer_glows.push_back(glow);
  document.add_layer(std::move(text_layer));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Transformed Expensive Text Preview"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(220, 130));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->property("patchy.expensiveTextStylePreview").toBool());
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.transformedPreviewOverlayActive").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").isValid());
  const auto preview_id = editor->property("patchy.textPreviewLayerId").toULongLong();

  QTextCursor cursor(editor->document());
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  editor->insertPlainText(QStringLiteral("!"));
  QApplication::processEvents();

  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.transformedPreviewOverlayActive").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").toULongLong() == preview_id);
  CHECK(editor->property("patchy.textPreviewPending").toBool());

  process_events_for(80);
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.transformedPreviewOverlayActive").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").isValid());

  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_text_edit_ctrl_t_commits_editor_before_free_transform() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  auto* type_action = require_action_by_text(window, QStringLiteral("Type"));
  auto* move_action = require_action_by_text(window, QStringLiteral("Move"));

  type_action->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(88, 92)),
       canvas->widget_position_for_document_point(QPoint(260, 150)));
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("Transform me"));
  QApplication::processEvents();

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();

  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(canvas->free_transform_active());
  auto* text_resize_handle =
      canvas->findChild<QWidget*>(QStringLiteral("textBoxResizeHandleTopLeft"), Qt::FindDirectChildrenOnly);
  CHECK(text_resize_handle == nullptr || !text_resize_handle->isVisible());
  const auto transform_rect = canvas->active_layer_document_rect();
  CHECK(transform_rect.has_value());
  send_double_click(*canvas, canvas->widget_position_for_document_point(transform_rect->center()));
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(!canvas->free_transform_active());
  CHECK(type_action->isChecked());
  CHECK(!move_action->isChecked());
  CHECK(!canvas->transform_controls_state().has_value());
  text_resize_handle =
      canvas->findChild<QWidget*>(QStringLiteral("textBoxResizeHandleTopLeft"), Qt::FindDirectChildrenOnly);
  CHECK(text_resize_handle != nullptr);
  CHECK(text_resize_handle->isVisible());
  CHECK(text_resize_handle->size() == QSize(9, 9));
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  move_action->trigger();
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();
  CHECK(canvas->transform_controls_state().has_value());
  const auto passive_transform_rect = canvas->active_layer_document_rect();
  CHECK(passive_transform_rect.has_value());
  send_double_click(*canvas, canvas->widget_position_for_document_point(passive_transform_rect->center()));
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(type_action->isChecked());
  CHECK(!move_action->isChecked());
  CHECK(!canvas->free_transform_active());
  CHECK(!canvas->transform_controls_state().has_value());
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_text_free_transform_clicking_current_move_tool_applies() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  auto* type_action = require_action_by_text(window, QStringLiteral("Type"));
  auto* move_action = require_action_by_text(window, QStringLiteral("Move"));

  type_action->trigger();
  const auto text_point = canvas->widget_position_for_document_point(QPoint(92, 104));
  send_mouse(*canvas, QEvent::MouseButtonPress, text_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("Rotate me"));
  QApplication::processEvents();

  move_action->trigger();
  QApplication::processEvents();
  CHECK(move_action->isChecked());
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  const auto before = canvas->active_layer_document_rect();
  CHECK(before.has_value());
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  auto* rotation = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformRotationSpin"));
  CHECK(rotation != nullptr);
  rotation->setValue(30.0);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  move_action->trigger();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  const auto after = canvas->active_layer_document_rect();
  CHECK(after.has_value());
  CHECK(after->width() != before->width() || after->height() != before->height());
}

void ui_text_box_commit_renders_paragraph_alignment() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  canvas->set_primary_color(QColor(Qt::black));
  const QPoint box_top_left(40, 90);
  const QPoint box_bottom_right(340, 150);
  drag(*canvas, canvas->widget_position_for_document_point(box_top_left),
       canvas->widget_position_for_document_point(box_bottom_right));
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  auto* center = window.findChild<QPushButton*>(QStringLiteral("textAlignCenterButton"));
  CHECK(center != nullptr);
  center->click();
  QApplication::processEvents();
  editor->setPlainText(QStringLiteral("Hi"));
  center->click();
  QApplication::processEvents();
  QTextCursor cursor(editor->document());
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  editor->setCursorWidth(0);
  QApplication::processEvents();
  save_widget_artifact("ui_text_alignment_center_editing", *canvas);
  const auto text_band = QRect(box_top_left + QPoint(0, 14),
                               QSize(box_bottom_right.x() - box_top_left.x(), 34));
  const auto active_bounds = dark_document_bounds(*canvas, text_band);
  CHECK(active_bounds.has_value());
  CHECK(active_bounds->left() > box_top_left.x() + 90);
  CHECK(active_bounds->right() < box_bottom_right.x() - 90);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  canvas->set_show_transform_controls(false);
  QApplication::processEvents();
  save_widget_artifact("ui_text_alignment_center_committed", *canvas);

  const auto committed_bounds = dark_document_bounds(*canvas, text_band);
  CHECK(committed_bounds.has_value());
  CHECK(committed_bounds->left() > box_top_left.x() + 90);
  CHECK(committed_bounds->right() < box_bottom_right.x() - 90);
  CHECK(std::abs(committed_bounds->left() - active_bounds->left()) <= 4);
  CHECK(std::abs(committed_bounds->right() - active_bounds->right()) <= 4);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  auto* right = window.findChild<QPushButton*>(QStringLiteral("textAlignRightButton"));
  CHECK(right != nullptr);
  const QPoint right_box_top_left(40, 180);
  const QPoint right_box_bottom_right(340, 240);
  drag(*canvas, canvas->widget_position_for_document_point(right_box_top_left),
       canvas->widget_position_for_document_point(right_box_bottom_right));
  QApplication::processEvents();

  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("Hi"));
  right->click();
  cursor = QTextCursor(editor->document());
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  editor->setCursorWidth(0);
  QApplication::processEvents();
  save_widget_artifact("ui_text_alignment_right_editing", *canvas);
  const auto right_text_band = QRect(right_box_top_left + QPoint(0, 14),
                                     QSize(right_box_bottom_right.x() - right_box_top_left.x(), 34));
  const auto active_right_bounds = dark_document_bounds(*canvas, right_text_band);
  CHECK(active_right_bounds.has_value());
  CHECK(active_right_bounds->left() > right_box_bottom_right.x() - 90);
  CHECK(active_right_bounds->right() <= right_box_bottom_right.x());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  save_widget_artifact("ui_text_alignment_right_committed", *canvas);

  const auto committed_right_bounds = dark_document_bounds(*canvas, right_text_band);
  CHECK(committed_right_bounds.has_value());
  CHECK(committed_right_bounds->left() > right_box_bottom_right.x() - 90);
  CHECK(committed_right_bounds->right() <= right_box_bottom_right.x());
  CHECK(std::abs(committed_right_bounds->left() - active_right_bounds->left()) <= 4);
  CHECK(std::abs(committed_right_bounds->right() - active_right_bounds->right()) <= 4);
}

void ui_text_tool_commits_rich_text_spans() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const QPoint text_document_point(96, 96);
  const auto text_widget_point = canvas->widget_position_for_document_point(text_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->styleSheet().contains(QStringLiteral("selection-color: rgb(")));
  CHECK(!editor->styleSheet().contains(QStringLiteral("font-size:")));
  editor->setHtml(QStringLiteral(
      "<html><body><p style='margin:0px;'>"
      "<span style='font-family:Arial; font-size:56px; color:#e02020;'>Red </span>"
      "<span style='font-family:Times New Roman; font-size:56px; color:#2050f0; font-weight:700; font-style:italic;'>Blue</span>"
      "</p></body></html>"));
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  const auto image = canvas->grab().toImage();
  bool saw_red = false;
  bool saw_blue = false;
  for (int y = 0; y < 110 && (!saw_red || !saw_blue); y += 2) {
    for (int x = 0; x < 360 && (!saw_red || !saw_blue); x += 2) {
      const auto widget_point = canvas->widget_position_for_document_point(text_document_point + QPoint(x, y));
      if (!image.rect().contains(widget_point)) {
        continue;
      }
      const auto color = image.pixelColor(widget_point);
      saw_red = saw_red || (color.red() > 150 && color.green() < 100 && color.blue() < 100);
      saw_blue = saw_blue || (color.blue() > 150 && color.red() < 100 && color.green() < 130);
    }
  }
  CHECK(saw_red);
  CHECK(saw_blue);
  save_widget_artifact("ui_text_tool_rich_text_spans", window);
}

void ui_text_options_follow_active_rich_text_span() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto text_widget_point = canvas->widget_position_for_document_point(QPoint(96, 96));
  send_mouse(*canvas, QEvent::MouseButtonPress, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  auto* text_size = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  auto* text_bold = window.findChild<QPushButton*>(QStringLiteral("textBoldButton"));
  auto* text_italic = window.findChild<QPushButton*>(QStringLiteral("textItalicButton"));
  auto* text_color = window.findChild<QPushButton*>(QStringLiteral("textColorButton"));
  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(editor != nullptr);
  CHECK(text_size != nullptr);
  CHECK(text_bold != nullptr);
  CHECK(text_italic != nullptr);
  CHECK(text_color != nullptr);
  CHECK(foreground != nullptr);

  editor->setHtml(QStringLiteral(
      "<html><body><p style='margin:0px;'>"
      "<span style='font-family:Arial; font-size:24px; color:#e02020;'>Red </span>"
      "<span style='font-family:Times New Roman; font-size:72px; color:#2050f0; font-weight:700; font-style:italic;'>Blue</span>"
      "</p></body></html>"));
  QApplication::processEvents();

  QTextCursor cursor(editor->document());
  cursor.setPosition(1);
  editor->setTextCursor(cursor);
  QApplication::processEvents();
  CHECK(std::abs(text_size->value() - text_points_for_pixels(24)) < 0.01);
  CHECK(!text_bold->isChecked());
  CHECK(!text_italic->isChecked());

  cursor.select(QTextCursor::Document);
  editor->setTextCursor(cursor);
  QApplication::processEvents();
  CHECK(!text_bold->isChecked());
  text_bold->click();
  QApplication::processEvents();

  QTextCursor red_after_bold(editor->document());
  red_after_bold.setPosition(1);
  red_after_bold.setPosition(2, QTextCursor::KeepAnchor);
  const auto red_bold_format = red_after_bold.charFormat();
  CHECK(red_bold_format.font().pixelSize() == 24);
  CHECK(red_bold_format.font().family().contains(QStringLiteral("Arial"), Qt::CaseInsensitive));
  CHECK(red_bold_format.font().bold());
  CHECK(red_bold_format.foreground().color() == QColor(224, 32, 32));

  QTextCursor blue_after_bold(editor->document());
  const auto initial_blue_start = editor->toPlainText().indexOf(QStringLiteral("Blue"));
  blue_after_bold.setPosition(initial_blue_start);
  blue_after_bold.setPosition(initial_blue_start + 1, QTextCursor::KeepAnchor);
  const auto blue_bold_format = blue_after_bold.charFormat();
  CHECK(blue_bold_format.font().pixelSize() == 72);
  CHECK(blue_bold_format.font().family().contains(QStringLiteral("Times"), Qt::CaseInsensitive));
  CHECK(blue_bold_format.font().bold());
  CHECK(blue_bold_format.font().italic());
  CHECK(blue_bold_format.foreground().color() == QColor(32, 80, 240));

  cursor.setPosition(editor->toPlainText().indexOf(QStringLiteral("Blue")));
  cursor.setPosition(cursor.position() + 4, QTextCursor::KeepAnchor);
  editor->setTextCursor(cursor);
  QApplication::processEvents();
  CHECK(std::abs(text_size->value() - text_points_for_pixels(72)) < 0.01);
  CHECK(text_bold->isChecked());
  CHECK(text_italic->isChecked());
  CHECK(editor->property("patchy.documentTextColor").value<QColor>() == QColor(32, 80, 240));

  text_color->click();
  QApplication::processEvents();
  bool changed_text_color = false;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
      continue;
    }
    auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
    CHECK(picker != nullptr);
    picker->setCurrentColor(QColor(20, 180, 90));
    QApplication::processEvents();
    widget->close();
    changed_text_color = true;
    break;
  }
  CHECK(changed_text_color);
  CHECK(editor->textCursor().hasSelection());
  CHECK(editor->styleSheet().contains(QStringLiteral("selection-color: rgb(20, 180, 90)")));

  QTextCursor blue_probe(editor->document());
  const auto blue_start = editor->toPlainText().indexOf(QStringLiteral("Blue"));
  blue_probe.setPosition(blue_start);
  blue_probe.setPosition(blue_start + 1, QTextCursor::KeepAnchor);
  const auto blue_format = blue_probe.charFormat();
  CHECK(blue_format.font().pixelSize() == 72);
  CHECK(blue_format.font().bold());
  CHECK(blue_format.font().italic());
  CHECK(blue_format.foreground().color() == QColor(20, 180, 90));

  QTextCursor red_probe(editor->document());
  red_probe.setPosition(1);
  red_probe.setPosition(2, QTextCursor::KeepAnchor);
  const auto red_format = red_probe.charFormat();
  CHECK(red_format.font().pixelSize() == 24);
  CHECK(red_format.foreground().color() == QColor(224, 32, 32));

  cursor.setPosition(blue_start);
  cursor.setPosition(blue_start + 4, QTextCursor::KeepAnchor);
  editor->setTextCursor(cursor);
  QApplication::processEvents();
  foreground->click();
  QApplication::processEvents();
  bool changed_foreground_color = false;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
      continue;
    }
    auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
    CHECK(picker != nullptr);
    picker->setCurrentColor(QColor(140, 70, 220));
    QApplication::processEvents();
    widget->close();
    changed_foreground_color = true;
    break;
  }
  CHECK(changed_foreground_color);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == editor);
  CHECK(editor->textCursor().hasSelection());
  CHECK(editor->property("patchy.documentTextColor").value<QColor>() == QColor(140, 70, 220));
  CHECK(canvas->primary_color() == QColor(140, 70, 220));

  QTextCursor foreground_blue_probe(editor->document());
  foreground_blue_probe.setPosition(blue_start);
  foreground_blue_probe.setPosition(blue_start + 1, QTextCursor::KeepAnchor);
  CHECK(foreground_blue_probe.charFormat().foreground().color() == QColor(140, 70, 220));

  const auto red_start = editor->toPlainText().indexOf(QStringLiteral("Red"));
  cursor.setPosition(red_start);
  cursor.setPosition(red_start + 3, QTextCursor::KeepAnchor);
  editor->setTextCursor(cursor);
  QApplication::processEvents();
  const auto swatches = window.findChildren<QPushButton*>(QStringLiteral("swatchButton"));
  CHECK(swatches.size() >= 7);
  swatches.at(6)->click();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == editor);
  CHECK(editor->textCursor().hasSelection());
  CHECK(editor->property("patchy.documentTextColor").value<QColor>() == QColor(0, 150, 220));
  CHECK(canvas->primary_color() == QColor(0, 150, 220));

  QTextCursor swatch_red_probe(editor->document());
  swatch_red_probe.setPosition(red_start);
  swatch_red_probe.setPosition(red_start + 1, QTextCursor::KeepAnchor);
  CHECK(swatch_red_probe.charFormat().foreground().color() == QColor(0, 150, 220));

  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
}

void ui_qimage_import_export_preserves_alpha_and_formats() {
  ensure_artifact_dir();
  QImage source(3, 2, QImage::Format_RGBA8888);
  source.fill(Qt::transparent);
  source.setPixelColor(0, 0, QColor(255, 0, 0, 128));
  source.setPixelColor(1, 0, QColor(0, 255, 0, 255));
  source.setPixelColor(2, 0, QColor(0, 0, 255, 32));
  source.setDotsPerMeterX(11811);
  source.setDotsPerMeterY(5906);

  const auto document = patchy::ui::document_from_qimage(source, "Alpha Import");
  CHECK(std::abs(document.print_settings().horizontal_ppi - 300.0) < 0.02);
  CHECK(std::abs(document.print_settings().vertical_ppi - 150.0) < 0.02);
  const auto exported = patchy::ui::qimage_from_document(document, true);
  CHECK(exported.hasAlphaChannel());
  CHECK(std::abs(exported.dotsPerMeterX() - 11811) <= 1);
  CHECK(std::abs(exported.dotsPerMeterY() - 5906) <= 1);
  CHECK(exported.pixelColor(0, 0).alpha() == 128);
  CHECK(exported.pixelColor(1, 0).green() == 255);

  CHECK(exported.save(QStringLiteral("test-artifacts/format_alpha.png")));
  CHECK(patchy::ui::qimage_from_document(document, false).save(QStringLiteral("test-artifacts/format_flat.jpg")));
  CHECK(patchy::ui::qimage_from_document(document, false).save(QStringLiteral("test-artifacts/format_flat.bmp")));
  CHECK(QImage(QStringLiteral("test-artifacts/format_alpha.png")).pixelColor(0, 0).alpha() == 128);
}

void ui_qimage_import_export_writes_tiff_and_webp() {
  ensure_artifact_dir();
  const auto has_format = [](const QList<QByteArray>& formats, const char* expected) {
    return std::any_of(formats.begin(), formats.end(), [expected](QByteArray format) {
      return format.toLower() == QByteArray(expected);
    });
  };

  CHECK(has_format(QImageReader::supportedImageFormats(), "tiff") ||
        has_format(QImageReader::supportedImageFormats(), "tif"));
  CHECK(has_format(QImageWriter::supportedImageFormats(), "tiff") ||
        has_format(QImageWriter::supportedImageFormats(), "tif"));
  CHECK(has_format(QImageReader::supportedImageFormats(), "webp"));
  CHECK(has_format(QImageWriter::supportedImageFormats(), "webp"));

  QImage source(6, 4, QImage::Format_RGBA8888);
  source.fill(QColor(12, 34, 56, 90));
  source.setPixelColor(1, 1, QColor(220, 40, 90, 180));
  source.setPixelColor(3, 2, QColor(40, 210, 110, 255));
  source.setDotsPerMeterX(11811);
  source.setDotsPerMeterY(11811);

  const auto document = patchy::ui::document_from_qimage(source, "Codec Alpha");
  patchy::ui::ImageSaveOptions options;
  const QStringList extensions{QStringLiteral("tif"), QStringLiteral("webp")};
  for (const auto& extension : extensions) {
    const auto path = QStringLiteral("test-artifacts/format_alpha.%1").arg(extension);
    patchy::ui::write_flat_image_file(document, path, extension, options);
    const QImage written(path);
    CHECK(!written.isNull());
    CHECK(written.size() == source.size());
    CHECK(written.hasAlphaChannel());
    CHECK(written.pixelColor(0, 0).alpha() < 255);
  }
}

void ui_image_save_options_write_bmp_alpha_and_jpeg_quality() {
  ensure_artifact_dir();

  QImage source(4, 3, QImage::Format_RGBA8888);
  source.fill(Qt::transparent);
  source.setPixelColor(0, 0, QColor(255, 0, 0, 64));
  source.setPixelColor(1, 0, QColor(0, 255, 0, 128));
  source.setPixelColor(2, 1, QColor(0, 0, 255, 255));
  source.setDotsPerMeterX(11811);
  source.setDotsPerMeterY(5906);

  const auto alpha_document = patchy::ui::document_from_qimage(source, "BMP Alpha");
  patchy::ui::ImageSaveOptions options;
  options.bmp_encoding = patchy::bmp::BmpEncoding::Rgba32;
  patchy::ui::write_flat_image_file(alpha_document, QStringLiteral("test-artifacts/format_alpha.bmp"),
                                    QStringLiteral("bmp"), options);
  const QImage alpha_bmp(QStringLiteral("test-artifacts/format_alpha.bmp"));
  CHECK(!alpha_bmp.isNull());
  CHECK(alpha_bmp.hasAlphaChannel());
  CHECK(alpha_bmp.pixelColor(0, 0).alpha() == 64);
  CHECK(alpha_bmp.pixelColor(1, 0).alpha() == 128);
  CHECK(std::abs(alpha_bmp.dotsPerMeterX() - 11811) <= 1);
  CHECK(std::abs(alpha_bmp.dotsPerMeterY() - 5906) <= 1);

  options.bmp_encoding = patchy::bmp::BmpEncoding::Rgb24;
  patchy::ui::write_flat_image_file(alpha_document, QStringLiteral("test-artifacts/format_flat_no_alpha.bmp"),
                                    QStringLiteral("bmp"), options);
  const QImage flat_bmp(QStringLiteral("test-artifacts/format_flat_no_alpha.bmp"));
  CHECK(!flat_bmp.isNull());
  CHECK(flat_bmp.pixelColor(0, 0).alpha() == 255);

  QImage indexed_source(4, 1, QImage::Format_RGB888);
  indexed_source.setPixelColor(0, 0, QColor(0, 0, 0));
  indexed_source.setPixelColor(1, 0, QColor(255, 0, 0));
  indexed_source.setPixelColor(2, 0, QColor(0, 255, 0));
  indexed_source.setPixelColor(3, 0, QColor(0, 0, 255));
  const auto indexed_document = patchy::ui::document_from_qimage(indexed_source, "Indexed BMP");
  options.bmp_encoding = patchy::bmp::BmpEncoding::Indexed4;
  options.bmp_palette_mode = patchy::bmp::BmpPaletteMode::Exact;
  patchy::ui::write_flat_image_file(indexed_document, QStringLiteral("test-artifacts/format_indexed4.bmp"),
                                    QStringLiteral("bmp"), options);
  const auto indexed_read = patchy::bmp::DocumentIo::read_file("test-artifacts/format_indexed4.bmp");
  CHECK(indexed_read.indexed_palette().has_value());
  CHECK(indexed_read.indexed_palette()->source_bit_depth == 4);
  CHECK(indexed_read.layers().front().pixels().pixel(3, 0)[2] == 255);

  QImage quantized_source(18, 18, QImage::Format_RGB888);
  for (int y = 0; y < quantized_source.height(); ++y) {
    for (int x = 0; x < quantized_source.width(); ++x) {
      quantized_source.setPixelColor(x, y, QColor((x * 31 + y * 7) % 256, (x * 11 + y * 19) % 256,
                                                  (x * 5 + y * 29) % 256));
    }
  }
  const auto quantized_document = patchy::ui::document_from_qimage(quantized_source, "Quantized BMP");
  options.bmp_encoding = patchy::bmp::BmpEncoding::Indexed8;
  options.bmp_palette_mode = patchy::bmp::BmpPaletteMode::Quantize;
  patchy::ui::write_flat_image_file(quantized_document, QStringLiteral("test-artifacts/format_indexed8_quantized.bmp"),
                                    QStringLiteral("bmp"), options);
  const auto quantized_read = patchy::bmp::DocumentIo::read_file("test-artifacts/format_indexed8_quantized.bmp");
  CHECK(quantized_read.indexed_palette().has_value());
  CHECK(quantized_read.indexed_palette()->colors.size() <= 256);

  const QString palette_path = QStringLiteral("test-artifacts/save_palette.pal");
  {
    QFile palette_file(palette_path);
    CHECK(palette_file.open(QIODevice::WriteOnly | QIODevice::Text));
    CHECK(palette_file.write("JASC-PAL\n0100\n4\n0 0 0\n255 0 0\n0 255 0\n0 0 255\n") > 0);
  }
  QImage palette_mapped_source(2, 1, QImage::Format_RGB888);
  palette_mapped_source.setPixelColor(0, 0, QColor(245, 12, 12));
  palette_mapped_source.setPixelColor(1, 0, QColor(8, 10, 240));
  const auto palette_mapped_document = patchy::ui::document_from_qimage(palette_mapped_source, "Palette BMP");
  options.bmp_encoding = patchy::bmp::BmpEncoding::Indexed4;
  options.bmp_palette_mode = patchy::bmp::BmpPaletteMode::PaletteFile;
  options.bmp_palette_path = palette_path;
  patchy::ui::write_flat_image_file(palette_mapped_document, QStringLiteral("test-artifacts/format_indexed4_palette.bmp"),
                                    QStringLiteral("bmp"), options);
  const auto palette_mapped_read = patchy::bmp::DocumentIo::read_file("test-artifacts/format_indexed4_palette.bmp");
  CHECK(palette_mapped_read.indexed_palette().has_value());
  CHECK(palette_mapped_read.indexed_palette()->colors.size() == 4);
  CHECK(palette_mapped_read.layers().front().pixels().pixel(0, 0)[0] == 255);
  CHECK(palette_mapped_read.layers().front().pixels().pixel(1, 0)[2] == 255);

  patchy::Document jpeg_document(128, 128, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer jpeg_pixels(128, 128, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < jpeg_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < jpeg_pixels.width(); ++x) {
      auto* px = jpeg_pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>((x * 29 + y * 11 + (x * y) % 251) % 256);
      px[1] = static_cast<std::uint8_t>((x * 7 + y * 37 + (x + y) * 13) % 256);
      px[2] = static_cast<std::uint8_t>((x * 41 + y * 5 + (x * 3) % 197) % 256);
    }
  }
  jpeg_document.add_pixel_layer("JPEG Pattern", std::move(jpeg_pixels));

  options.jpeg_quality = 5;
  patchy::ui::write_flat_image_file(jpeg_document, QStringLiteral("test-artifacts/quality_low.jpg"),
                                    QStringLiteral("jpg"), options);
  options.jpeg_quality = 100;
  patchy::ui::write_flat_image_file(jpeg_document, QStringLiteral("test-artifacts/quality_high.jpg"),
                                    QStringLiteral("jpg"), options);
  const auto low_size = QFileInfo(QStringLiteral("test-artifacts/quality_low.jpg")).size();
  const auto high_size = QFileInfo(QStringLiteral("test-artifacts/quality_high.jpg")).size();
  CHECK(low_size > 0);
  CHECK(high_size > low_size + 1000);
}

void ui_image_save_options_defaults_and_dialogs() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();

  auto defaults = patchy::ui::load_image_save_option_defaults();
  CHECK(defaults.jpeg_quality == 95);
  CHECK(defaults.bmp_encoding == patchy::bmp::BmpEncoding::Rgba32);
  CHECK(defaults.bmp_palette_mode == patchy::bmp::BmpPaletteMode::Exact);
  CHECK(defaults.bmp_palette_path.isEmpty());
  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral("jpg")));
  CHECK(patchy::ui::image_save_options_apply_to_extension(QStringLiteral(".bmp")));
  CHECK(!patchy::ui::image_save_options_apply_to_extension(QStringLiteral("png")));

  settings.setValue(QStringLiteral("saveOptions/bmpPreserveAlpha"), false);
  settings.sync();
  const auto migrated = patchy::ui::load_image_save_option_defaults();
  CHECK(migrated.bmp_encoding == patchy::bmp::BmpEncoding::Rgb24);

  defaults.jpeg_quality = 37;
  defaults.bmp_encoding = patchy::bmp::BmpEncoding::Indexed4;
  defaults.bmp_palette_mode = patchy::bmp::BmpPaletteMode::PaletteFile;
  defaults.bmp_palette_path = QStringLiteral("test-artifacts/save_palette.pal");
  patchy::ui::save_image_save_option_defaults(defaults);
  const auto loaded = patchy::ui::load_image_save_option_defaults();
  CHECK(loaded.jpeg_quality == 37);
  CHECK(loaded.bmp_encoding == patchy::bmp::BmpEncoding::Indexed4);
  CHECK(loaded.bmp_palette_mode == patchy::bmp::BmpPaletteMode::PaletteFile);
  CHECK(loaded.bmp_palette_path == defaults.bmp_palette_path);

  bool saw_jpeg_dialog = false;
  QTimer::singleShot(0, [&saw_jpeg_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("jpegSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    auto* quality = dialog->findChild<QSpinBox*>(QStringLiteral("jpegQualitySpin"));
    CHECK(quality != nullptr);
    CHECK(quality->value() == 37);
    auto* quality_slider = dialog->findChild<QSlider*>(QStringLiteral("jpegQualitySlider"));
    CHECK(quality_slider != nullptr);
    CHECK(quality_slider->value() == 37);
    quality_slider->setValue(64);
    CHECK(quality->value() == 64);
    quality->setValue(82);
    CHECK(quality_slider->value() == 82);
    saw_jpeg_dialog = true;
    dialog->accept();
  });
  auto jpeg_options = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("jpeg"), loaded);
  CHECK(saw_jpeg_dialog);
  CHECK(jpeg_options.has_value());
  CHECK(jpeg_options->jpeg_quality == 82);
  CHECK(jpeg_options->bmp_encoding == patchy::bmp::BmpEncoding::Indexed4);

  bool saw_bmp_dialog = false;
  QTimer::singleShot(0, [&saw_bmp_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("bmpSaveOptionsDialog"));
    CHECK(dialog != nullptr);
    auto* indexed4 = dialog->findChild<QRadioButton*>(QStringLiteral("bmpEncodingIndexed4Radio"));
    CHECK(indexed4 != nullptr);
    CHECK(indexed4->isChecked());
    auto* palette_file = dialog->findChild<QRadioButton*>(QStringLiteral("bmpPaletteFileRadio"));
    CHECK(palette_file != nullptr);
    CHECK(palette_file->isChecked());
    auto* palette_path = dialog->findChild<QLineEdit*>(QStringLiteral("bmpPalettePathEdit"));
    CHECK(palette_path != nullptr);
    CHECK(palette_path->text() == QStringLiteral("test-artifacts/save_palette.pal"));
    auto* indexed2 = dialog->findChild<QRadioButton*>(QStringLiteral("bmpEncodingIndexed2Radio"));
    CHECK(indexed2 != nullptr);
    indexed2->click();
    auto* exact = dialog->findChild<QRadioButton*>(QStringLiteral("bmpPaletteExactRadio"));
    CHECK(exact != nullptr);
    CHECK(exact->isChecked());
    CHECK(!palette_file->isEnabled());
    CHECK(!palette_path->isEnabled());
    auto* browse = dialog->findChild<QPushButton*>(QStringLiteral("bmpPaletteBrowseButton"));
    CHECK(browse != nullptr);
    CHECK(browse->isEnabled());
    auto* indexed8 = dialog->findChild<QRadioButton*>(QStringLiteral("bmpEncodingIndexed8Radio"));
    CHECK(indexed8 != nullptr);
    indexed8->click();
    CHECK(palette_file->isEnabled());
    palette_file->click();
    CHECK(palette_path->isEnabled());
    palette_path->setText(QStringLiteral("test-artifacts/other_palette.pal"));
    saw_bmp_dialog = true;
    dialog->accept();
  });
  auto bmp_options = patchy::ui::prompt_image_save_options(nullptr, QStringLiteral(".bmp"), *jpeg_options);
  CHECK(saw_bmp_dialog);
  CHECK(bmp_options.has_value());
  CHECK(bmp_options->bmp_encoding == patchy::bmp::BmpEncoding::Indexed8);
  CHECK(bmp_options->bmp_palette_mode == patchy::bmp::BmpPaletteMode::PaletteFile);
  CHECK(bmp_options->bmp_palette_path == QStringLiteral("test-artifacts/other_palette.pal"));

  settings.remove(QStringLiteral("saveOptions"));
  settings.sync();
}

void ui_qimage_multiply_uses_empty_backdrop_as_transparent() {
  patchy::Document transparent_document(1, 1, patchy::PixelFormat::rgba8());
  auto& transparent_multiply = transparent_document.add_pixel_layer(
      "Multiply", solid_pixels(1, 1, patchy::PixelFormat::rgba8(), QColor(200, 100, 50, 128)));
  transparent_multiply.set_blend_mode(patchy::BlendMode::Multiply);

  const auto transparent = patchy::ui::qimage_from_document(transparent_document, true);
  const auto transparent_color = transparent.pixelColor(0, 0);
  CHECK(transparent_color.red() == 200);
  CHECK(transparent_color.green() == 100);
  CHECK(transparent_color.blue() == 50);
  CHECK(transparent_color.alpha() == 128);

  patchy::Document opaque_document(1, 1, patchy::PixelFormat::rgb8());
  opaque_document.add_pixel_layer("Base", solid_pixels(1, 1, patchy::PixelFormat::rgb8(), QColor(100, 160, 240)));
  auto& opaque_multiply = opaque_document.add_pixel_layer(
      "Multiply", solid_pixels(1, 1, patchy::PixelFormat::rgba8(), QColor(200, 100, 50, 255)));
  opaque_multiply.set_blend_mode(patchy::BlendMode::Multiply);

  const auto opaque = patchy::ui::qimage_from_document(opaque_document, true);
  const auto opaque_color = opaque.pixelColor(0, 0);
  CHECK(opaque_color.red() == 78);
  CHECK(opaque_color.green() == 62);
  CHECK(opaque_color.blue() == 47);
  CHECK(opaque_color.alpha() == 255);
}

void ui_print_layout_and_pdf_output_work() {
  ensure_artifact_dir();
  patchy::Document document(300, 150, patchy::PixelFormat::rgb8());
  document.print_settings().horizontal_ppi = 300.0;
  document.print_settings().vertical_ppi = 150.0;
  document.add_pixel_layer("Print", solid_pixels(300, 150, patchy::PixelFormat::rgb8(), QColor(200, 20, 30)));

  auto page_layout = patchy::ui::default_print_page_layout();
  auto settings = patchy::ui::default_print_settings(document, QRect(0, 0, 150, 75));
  settings.scale_mode = patchy::ui::PrintScaleMode::ActualSize;
  auto placement = patchy::ui::calculate_print_placement(document, settings, page_layout);
  CHECK(placement.source_rect == QRect(0, 0, 300, 150));
  CHECK(std::abs(placement.print_size_inches.width() - 1.0) < 0.01);
  CHECK(std::abs(placement.print_size_inches.height() - 0.5) < 0.01);

  settings.scale_mode = patchy::ui::PrintScaleMode::CustomScale;
  settings.scale_percent = 50.0;
  placement = patchy::ui::calculate_print_placement(document, settings, page_layout);
  CHECK(std::abs(placement.print_size_inches.width() - 0.5) < 0.01);

  settings.area_mode = patchy::ui::PrintAreaMode::Selection;
  settings.scale_mode = patchy::ui::PrintScaleMode::ActualSize;
  placement = patchy::ui::calculate_print_placement(document, settings, page_layout);
  CHECK(placement.source_rect == QRect(0, 0, 150, 75));
  CHECK(std::abs(placement.print_size_inches.width() - 0.5) < 0.01);
  CHECK(std::abs(placement.print_size_inches.height() - 0.25) < 0.01);

  settings.crop_marks = true;
  QImage page(page_layout.fullRect(QPageLayout::Point).toAlignedRect().size(), QImage::Format_RGB32);
  page.fill(Qt::black);
  QPainter painter(&page);
  patchy::ui::render_print_page(painter, document, settings, page_layout);
  painter.end();
  const auto sample = placement.target_rect_points.center().toPoint();
  CHECK(color_close(page.pixelColor(sample), QColor(200, 20, 30), 3));
  CHECK(page.save(QStringLiteral("test-artifacts/ui_print_preview_page.png")));

  const auto pdf_path = QStringLiteral("test-artifacts/ui_print_output.pdf");
  QFile::remove(pdf_path);
  CHECK(patchy::ui::write_print_pdf(pdf_path, document, settings, page_layout));
  CHECK(QFileInfo(pdf_path).isFile());
  CHECK(QFileInfo(pdf_path).size() > 1000);
}

void ui_print_dialog_exposes_printer_and_visible_checkboxes() {
  patchy::ui::MainWindow window;
  show_window(window);

  QTimer::singleShot(0, [&window] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyPrintDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* printer = dialog->findChild<QComboBox*>(QStringLiteral("printPrinterCombo"));
      auto* print_button = dialog->findChild<QPushButton*>(QStringLiteral("printDialogPrintButton"));
      auto* scale_to_fit = dialog->findChild<QCheckBox*>(QStringLiteral("printScaleToFitCheck"));
      auto* scale = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("printScalePercentSpin"));
      auto* resolution = dialog->findChild<QSpinBox*>(QStringLiteral("printResolutionSpin"));
      auto* units = dialog->findChild<QComboBox*>(QStringLiteral("printUnitsCombo"));
      auto* scale_size = dialog->findChild<QLabel*>(QStringLiteral("printScaleSizeLabel"));
      auto* image_size = dialog->findChild<QLabel*>(QStringLiteral("printImageSizeLabel"));
      auto* center = dialog->findChild<QCheckBox*>(QStringLiteral("printCenterCheck"));
      auto* crop_marks = dialog->findChild<QCheckBox*>(QStringLiteral("printCropMarksCheck"));
      CHECK(printer != nullptr);
      CHECK(print_button != nullptr);
      CHECK(scale_to_fit != nullptr);
      CHECK(scale != nullptr);
      CHECK(resolution != nullptr);
      CHECK(units != nullptr);
      CHECK(scale_size != nullptr);
      CHECK(image_size != nullptr);
      CHECK(center != nullptr);
      CHECK(crop_marks != nullptr);
      CHECK(printer->count() >= 1);
      CHECK(!printer->currentText().isEmpty());
      CHECK(print_button->isEnabled() == printer->isEnabled());
      CHECK(scale_to_fit->isChecked());
      CHECK(!scale->isEnabled());
      CHECK(resolution->value() == 300);
      CHECK(units->currentData().toString() == QStringLiteral("in"));
      CHECK(scale_size->text().contains(QStringLiteral("in")));
      CHECK(image_size->text().contains(QStringLiteral("in")));
      scale_to_fit->setChecked(false);
      QApplication::processEvents();
      CHECK(scale->isEnabled());
      CHECK(std::abs(scale->value() - 100.0) < 0.01);
      CHECK(window.styleSheet().contains(QStringLiteral("QCheckBox::indicator:checked")));
      CHECK(window.styleSheet().contains(QStringLiteral("checkmark.svg")));
      CHECK(window.styleSheet().contains(QStringLiteral("border-color: #9ccfff")));
      dialog->reject();
      return;
    }
    CHECK(false);
  });

  require_action(window, "filePrintAction")->trigger();
  QApplication::processEvents();
}

void ui_dragged_image_file_opens_document_tab() {
  ensure_artifact_dir();
  const auto image_path = std::filesystem::absolute(std::filesystem::path("test-artifacts") / "drag-open.png");
  const auto image_path_qt = QString::fromStdString(image_path.string());

  QImage source(6, 4, QImage::Format_RGB32);
  source.fill(QColor(20, 40, 60));
  source.setPixelColor(2, 1, QColor(30, 200, 240));
  CHECK(source.save(image_path_qt));

  patchy::ui::MainWindow window;
  show_window(window);

  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  CHECK(tabs->count() == 1);
  auto* canvas = require_canvas(window);

  QMimeData mime_data;
  mime_data.setUrls(QList<QUrl>{QUrl::fromLocalFile(image_path_qt)});
  const auto drop_position = canvas->rect().center();

  QDragEnterEvent drag_enter(drop_position, Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &drag_enter);
  QApplication::processEvents();
  CHECK(drag_enter.isAccepted());

  QDragMoveEvent drag_move(drop_position, Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &drag_move);
  QApplication::processEvents();
  CHECK(drag_move.isAccepted());

  bool saw_open_progress = false;
  QTimer::singleShot(0, [&] { verify_open_progress_dialog(QStringLiteral("drag-open.png"), saw_open_progress); });

  QDropEvent drop(QPointF(drop_position), Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &drop);
  QApplication::processEvents();

  CHECK(drop.isAccepted());
  CHECK(saw_open_progress);
  CHECK(tabs->count() == 2);
  CHECK(tabs->tabText(tabs->currentIndex()) == QStringLiteral("drag-open.png"));
  CHECK(window.windowTitle() == QStringLiteral("drag-open.png"));
  canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* active_layer_info = window.findChild<QLabel*>(QStringLiteral("activeLayerInfoLabel"));
  CHECK(layer_list != nullptr);
  CHECK(active_layer_info != nullptr);
  CHECK(layer_list->count() == 1);
  CHECK(layer_list->currentItem() != nullptr);
  CHECK(layer_list->currentItem()->text() == QStringLiteral("drag-open"));
  CHECK(layer_list->selectedItems().size() == 1);
  CHECK(layer_list->currentItem()->isSelected());
  CHECK(active_layer_info->text().contains(QStringLiteral("drag-open")));
  CHECK(color_close(canvas_pixel_center(*canvas, QPoint(2, 1)), QColor(30, 200, 240), 8));
}

void ui_reported_psd_open_shows_progress_dialog_if_available() {
  const auto psd_path =
      QStringLiteral("D:/projects/cc65/c2game/screenshots_and_labels/"
                     "C2Kyoto Nintendo NES Cartridge Label Template (Front).psd");
  if (!QFileInfo::exists(psd_path)) {
    return;
  }

  patchy::ui::MainWindow window;
  show_window(window);

  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  const auto original_tab_count = tabs->count();
  auto* canvas = require_canvas(window);

  QMimeData mime_data;
  mime_data.setUrls(QList<QUrl>{QUrl::fromLocalFile(psd_path)});
  const auto drop_position = canvas->rect().center();

  QDragEnterEvent drag_enter(drop_position, Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &drag_enter);
  QApplication::processEvents();
  CHECK(drag_enter.isAccepted());

  QDragMoveEvent drag_move(drop_position, Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &drag_move);
  QApplication::processEvents();
  CHECK(drag_move.isAccepted());

  bool saw_open_progress = false;
  const auto expected_file_name = QFileInfo(psd_path).fileName();
  QTimer::singleShot(0, [&] { verify_open_progress_dialog(expected_file_name, saw_open_progress); });
  const auto compatibility_report_done = std::make_shared<bool>(false);
  accept_compatibility_report_when_present(compatibility_report_done);

  QDropEvent drop(QPointF(drop_position), Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &drop);
  QApplication::processEvents();
  *compatibility_report_done = true;

  CHECK(drop.isAccepted());
  CHECK(saw_open_progress);
  CHECK(tabs->count() == original_tab_count + 1);
  CHECK(tabs->tabText(tabs->currentIndex()) == expected_file_name);
}

void ui_qimage_render_respects_hidden_layer_groups() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer background(1, 1, patchy::PixelFormat::rgb8());
  auto* background_px = background.pixel(0, 0);
  background_px[0] = 255;
  background_px[1] = 255;
  background_px[2] = 255;
  document.add_pixel_layer("Background", std::move(background));

  patchy::PixelBuffer child_pixels(1, 1, patchy::PixelFormat::rgba8());
  auto* child_px = child_pixels.pixel(0, 0);
  child_px[0] = 220;
  child_px[1] = 20;
  child_px[2] = 30;
  child_px[3] = 255;
  patchy::Layer group(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Child", std::move(child_pixels)));
  document.add_layer(std::move(group));

  auto shown = patchy::ui::qimage_from_document(document, false);
  CHECK(shown.pixelColor(0, 0).red() == 220);

  document.layers()[1].set_visible(false);
  CHECK(document.layers()[1].children().front().visible());
  auto hidden = patchy::ui::qimage_from_document(document, false);
  CHECK(hidden.pixelColor(0, 0).red() == 255);
  CHECK(hidden.pixelColor(0, 0).green() == 255);
  CHECK(hidden.pixelColor(0, 0).blue() == 255);
}

void ui_qimage_region_render_matches_full_layer_styles() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer background(64, 48, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < background.height(); ++y) {
    for (std::int32_t x = 0; x < background.width(); ++x) {
      auto* px = background.pixel(x, y);
      px[0] = 52;
      px[1] = 58;
      px[2] = 66;
      px[3] = 255;
    }
  }
  document.add_pixel_layer("Background", std::move(background));

  patchy::PixelBuffer badge(24, 16, patchy::PixelFormat::rgba8());
  badge.clear(0);
  for (std::int32_t y = 2; y < 14; ++y) {
    for (std::int32_t x = 3; x < 21; ++x) {
      auto* px = badge.pixel(x, y);
      px[0] = 230;
      px[1] = 150;
      px[2] = 35;
      px[3] = 220;
    }
  }

  auto layer = patchy::Layer(document.allocate_layer_id(), "Styled Badge", std::move(badge));
  const auto styled_layer_id = layer.id();
  layer.set_bounds(patchy::Rect{18, 14, 24, 16});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.distance = 4.0F;
  shadow.size = 5.0F;
  shadow.opacity = 0.6F;
  layer.layer_style().drop_shadows.push_back(shadow);
  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.size = 6.0F;
  glow.opacity = 0.45F;
  glow.color = patchy::RgbColor{255, 230, 120};
  layer.layer_style().outer_glows.push_back(glow);
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.size = 3.0F;
  stroke.color = patchy::RgbColor{15, 25, 35};
  layer.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(layer));

  const QRect region(10, 8, 45, 34);
  const auto full = patchy::ui::qimage_from_document(document, true).copy(region);
  const auto partial = patchy::ui::qimage_from_document_rect(document, region, true);
  CHECK(partial.size() == full.size());
  for (int y = 0; y < partial.height(); ++y) {
    for (int x = 0; x < partial.width(); ++x) {
      CHECK(color_close(partial.pixelColor(x, y), full.pixelColor(x, y), 0));
    }
  }

  const QRegion disjoint_region(QRect(10, 8, 14, 12));
  auto multi_region = disjoint_region.united(QRect(44, 29, 11, 10));
  const auto full_original = patchy::ui::qimage_from_document(document, true);
  const auto patches = patchy::ui::qimage_patches_from_document_region(document, multi_region, true);
  CHECK(patches.size() == 2U);
  for (const auto& patch : patches) {
    CHECK(patch.image.size() == patch.document_rect.size());
    const auto expected_patch = full_original.copy(patch.document_rect);
    CHECK(images_equal_rgba(patch.image, expected_patch));
  }

  const auto moved_bounds = patchy::Rect{24, 17, 24, 16};
  const QRect moved_region(10, 8, 50, 36);
  const auto moved_override =
      patchy::ui::qimage_from_document_rect_with_layer_bounds(document, moved_region, true, styled_layer_id,
                                                                 moved_bounds);
  auto* moved_layer = document.find_layer(styled_layer_id);
  CHECK(moved_layer != nullptr);
  moved_layer->set_bounds(moved_bounds);
  const auto moved_actual = patchy::ui::qimage_from_document_rect(document, moved_region, true);
  CHECK(moved_override.size() == moved_actual.size());
  for (int y = 0; y < moved_override.height(); ++y) {
    for (int x = 0; x < moved_override.width(); ++x) {
      CHECK(color_close(moved_override.pixelColor(x, y), moved_actual.pixelColor(x, y), 0));
    }
  }

  QRegion moved_dirty(QRect(16, 12, 18, 18));
  moved_dirty += QRect(43, 24, 14, 12);
  const std::vector<std::pair<patchy::LayerId, patchy::Rect>> moved_overrides{{styled_layer_id, moved_bounds}};
  const auto moved_patches =
      patchy::ui::qimage_patches_from_document_region_with_layer_bounds(document, moved_dirty, true, moved_overrides);
  CHECK(moved_patches.size() >= 2U);
  const auto moved_full = patchy::ui::qimage_from_document(document, true);
  for (const auto& patch : moved_patches) {
    CHECK(images_equal_rgba(patch.image, moved_full.copy(patch.document_rect)));
  }
}

void ui_image_adjustments_menu_applies_active_layer_filters() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(layer_list != nullptr);
  CHECK(tabs != nullptr);

  canvas->set_primary_color(QColor(10, 120, 240));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(10, 120, 240), 6));
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(24);
  canvas->set_primary_color(QColor(240, 20, 20));

  bool saw_live_filter_preview = false;
  bool canvas_zoomed_with_dialog_open = false;
  bool canvas_panned_with_dialog_open = false;
  bool saw_filter_edit_lock = false;
  const auto zoom_before_dialog = canvas->zoom();
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyFilterDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      CHECK(!dialog->isModal());
      CHECK(dialog->windowModality() == Qt::NonModal);
      CHECK(window.isEnabled());
      CHECK(canvas->edit_locked());
      CHECK(!layer_list->isEnabled());
      CHECK(!tabs->tabBar()->isEnabled());
      CHECK(!require_action(window, "layerNewAction")->isEnabled());
      CHECK(!require_action(window, "layerFillForegroundAction")->isEnabled());
      CHECK(require_action(window, "viewZoomInAction")->isEnabled());
      auto* amount = dialog->findChild<QSpinBox*>(QStringLiteral("filterAmountSpin"));
      CHECK(amount != nullptr);
      CHECK(amount->value() == 100);
      process_events_for(120);
      saw_live_filter_preview = color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(245, 135, 15), 8);
      const auto preview_pixel_before_edit = canvas_pixel(*canvas, QPoint(40, 40));
      drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
           canvas->widget_position_for_document_point(QPoint(58, 58)));
      const auto preview_pixel_after_edit = canvas_pixel(*canvas, QPoint(40, 40));
      saw_filter_edit_lock = color_close(preview_pixel_after_edit, preview_pixel_before_edit, 2);
      send_wheel(*canvas, QPoint(300, 240), 120, Qt::AltModifier);
      canvas_zoomed_with_dialog_open = canvas->zoom() > zoom_before_dialog;
      const auto origin_before_pan = canvas->widget_position_for_document_point(QPoint(0, 0));
      send_mouse(*canvas, QEvent::MouseButtonPress, QPoint(300, 240), Qt::MiddleButton, Qt::MiddleButton);
      send_mouse(*canvas, QEvent::MouseMove, QPoint(318, 252), Qt::NoButton, Qt::MiddleButton);
      send_mouse(*canvas, QEvent::MouseButtonRelease, QPoint(318, 252), Qt::MiddleButton, Qt::NoButton);
      canvas_panned_with_dialog_open = canvas->widget_position_for_document_point(QPoint(0, 0)) != origin_before_pan;
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  require_action(window, "imageAdjustInvertAction")->trigger();
  CHECK(saw_live_filter_preview);
  CHECK(canvas_zoomed_with_dialog_open);
  CHECK(canvas_panned_with_dialog_open);
  CHECK(saw_filter_edit_lock);
  CHECK(!canvas->edit_locked());
  CHECK(layer_list->isEnabled());
  CHECK(tabs->tabBar()->isEnabled());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(10, 120, 240), 6));

  accept_filter_dialog();
  require_action(window, "imageAdjustInvertAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(245, 135, 15), 8));

  accept_filter_dialog();
  require_action(window, "imageAdjustDesaturateAction")->trigger();
  QApplication::processEvents();
  const auto gray = canvas_pixel(*canvas, QPoint(40, 40));
  CHECK(std::abs(gray.red() - gray.green()) <= 2);
  CHECK(std::abs(gray.green() - gray.blue()) <= 2);
  save_widget_artifact("ui_image_adjustments_invert_desaturate", *canvas);

  canvas->set_primary_color(QColor(120, 70, 210));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  accept_filter_dialog({{QStringLiteral("filterBrightnessSpin"), 10},
                        {QStringLiteral("filterContrastSpin"), 50}});
  require_action(window, "imageAdjustBrightnessContrastAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(126, 51, 255), 6));

  canvas->set_primary_color(QColor(50, 50, 50));
  canvas->set_secondary_color(QColor(180, 180, 180));
  require_action_by_text(window, QStringLiteral("Gradient"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 90)),
       canvas->widget_position_for_document_point(QPoint(260, 90)));
  QApplication::processEvents();
  CHECK(canvas_pixel(*canvas, QPoint(20, 90)).red() > 40);
  CHECK(canvas_pixel(*canvas, QPoint(260, 90)).red() < 190);

  accept_filter_dialog();
  require_action(window, "imageAdjustAutoContrastAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas_pixel(*canvas, QPoint(20, 90)).red() < 12);
  CHECK(canvas_pixel(*canvas, QPoint(260, 90)).red() > 242);
  save_widget_artifact("ui_image_adjustments_auto_contrast", *canvas);

  auto* edge_detect = require_action(window, "filterAction_patchy_filters_edge_detect");
  CHECK(edge_detect->toolTip().contains(QStringLiteral("Edge Detect")));
  accept_filter_dialog({{QStringLiteral("filterStrengthSpin"), 150}});
  edge_detect->trigger();
  QApplication::processEvents();
  CHECK(canvas_pixel(*canvas, QPoint(140, 90)).red() < 80);
  auto* history = window.findChild<QListWidget*>(QStringLiteral("historyList"));
  CHECK(history != nullptr);
  CHECK(history->item(0) != nullptr);
  CHECK(history->item(0)->text().contains(QStringLiteral("Edge Detect")));
  save_widget_artifact("ui_filter_edge_detect", *canvas);

  auto* emboss = require_action(window, "filterAction_patchy_filters_emboss");
  accept_filter_dialog({{QStringLiteral("filterAngleSpin"), 90},
                        {QStringLiteral("filterHeightSpin"), 4},
                        {QStringLiteral("filterDepthSpin"), 140}});
  emboss->trigger();
  QApplication::processEvents();
  CHECK(window.findChild<QListWidget*>(QStringLiteral("historyList"))->item(0)->text().contains(QStringLiteral("Emboss")));

  canvas->set_primary_color(QColor(255, 0, 0));
  canvas->set_secondary_color(QColor(0, 0, 255));
  auto* clouds = require_action(window, "filterAction_patchy_filters_clouds");
  accept_filter_dialog({{QStringLiteral("filterScaleSpin"), 48},
                        {QStringLiteral("filterDetailSpin"), 4},
                        {QStringLiteral("filterContrastSpin"), 60},
                        {QStringLiteral("filterSeedSpin"), 3}});
  clouds->trigger();
  QApplication::processEvents();
  const auto cloud_pixel = canvas_pixel(*canvas, QPoint(40, 40));
  CHECK(cloud_pixel.green() < 8);
  CHECK(cloud_pixel.red() > 0);
  CHECK(cloud_pixel.blue() > 0);
  CHECK(std::abs(cloud_pixel.red() - cloud_pixel.blue()) > 4);
  save_widget_artifact("ui_filter_clouds_foreground_background", *canvas);
}

void ui_image_adjustments_respect_active_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(20, 90, 220));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(100, 100)));
  QApplication::processEvents();
  accept_filter_dialog();
  require_action(window, "imageAdjustInvertAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(235, 165, 35), 10));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(160, 70)), QColor(20, 90, 220), 10));
  save_widget_artifact("ui_image_adjustment_selection_scope", *canvas);
}

void ui_direct_pixel_previews_preserve_floating_layer_bounds() {
  patchy::Document document(180, 140, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(180, 140, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer floating_layer(document.allocate_layer_id(), "Floating Console",
                               solid_pixels(44, 32, patchy::PixelFormat::rgba8(), QColor(96, 96, 96, 255)));
  const auto floating_id = floating_layer.id();
  const QRect floating_rect(70, 45, 44, 32);
  floating_layer.set_bounds(
      patchy::Rect{floating_rect.x(), floating_rect.y(), floating_rect.width(), floating_rect.height()});
  document.add_layer(std::move(floating_layer));
  document.set_active_layer(floating_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Floating Levels"));
  show_window(window);
  auto* canvas = require_canvas(window);
  const auto original_pixel = canvas_pixel(*canvas, QPoint(72, 47));
  CHECK(color_close(original_pixel, QColor(96, 96, 96), 5));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(4, 4)), QColor(Qt::white), 5));
  CHECK(canvas->active_layer_document_rect().has_value());
  CHECK(canvas->active_layer_document_rect()->topLeft() == floating_rect.topLeft());

  bool saw_levels_preview_with_bounds = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLevelsDialog"));
    CHECK(dialog != nullptr);
    auto* black = dialog->findChild<QSpinBox*>(QStringLiteral("levelsBlackInputSpin"));
    auto* white = dialog->findChild<QSpinBox*>(QStringLiteral("levelsWhiteInputSpin"));
    CHECK(black != nullptr);
    CHECK(white != nullptr);
    black->setValue(40);
    white->setValue(140);
    process_events_for(160);
    const auto preview_rect = canvas->active_layer_document_rect();
    CHECK(preview_rect.has_value());
    saw_levels_preview_with_bounds = preview_rect->topLeft() == floating_rect.topLeft() &&
                                     color_close(canvas_pixel(*canvas, QPoint(4, 4)), QColor(Qt::white), 5);
    dialog->reject();
  });
  require_action(window, "imageAdjustLevelsAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_levels_preview_with_bounds);
  CHECK(canvas->active_layer_document_rect().has_value());
  CHECK(canvas->active_layer_document_rect()->topLeft() == floating_rect.topLeft());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(4, 4)), QColor(Qt::white), 5));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(72, 47)), original_pixel, 6));

  accept_levels_dialog(40, 140, 100);
  require_action(window, "imageAdjustLevelsAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->active_layer_document_rect().has_value());
  CHECK(canvas->active_layer_document_rect()->topLeft() == floating_rect.topLeft());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(4, 4)), QColor(Qt::white), 5));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(72, 47)), original_pixel, 8));

  accept_filter_dialog();
  require_action(window, "imageAdjustInvertAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->active_layer_document_rect().has_value());
  CHECK(canvas->active_layer_document_rect()->topLeft() == floating_rect.topLeft());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(4, 4)), QColor(Qt::white), 5));
}

void ui_levels_dialog_adjusts_selected_color_channel_on_transparent_layer() {
  patchy::Document document(140, 100, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(140, 100, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto strokes = solid_pixels(140, 100, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(strokes, QRect(24, 24, 24, 16), QColor(0, 255, 0, 255));
  fill_pixel_rect(strokes, QRect(64, 24, 24, 16), QColor(0, 0, 0, 255));
  auto& paint = document.add_pixel_layer("Paint", std::move(strokes));
  document.set_active_layer(paint.id());

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Levels Channels"));
  show_window(window);
  auto* canvas = require_canvas(window);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(0, 255, 0), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 30)), QColor(0, 0, 0), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(110, 70)), QColor(Qt::white), 5));

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  const auto layer_count_before = layer_list->count();
  bool saw_channel_preview = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLevelsDialog"));
    CHECK(dialog != nullptr);
    auto* channel = dialog->findChild<QComboBox*>(QStringLiteral("levelsChannelCombo"));
    auto* black_output = dialog->findChild<QSpinBox*>(QStringLiteral("levelsBlackOutputSpin"));
    CHECK(channel != nullptr);
    CHECK(black_output != nullptr);
    CHECK(channel->count() == 4);
    CHECK(channel->itemText(0) == QStringLiteral("RGB"));
    CHECK(channel->itemText(1) == QStringLiteral("Red"));
    CHECK(channel->itemText(2) == QStringLiteral("Green"));
    CHECK(channel->itemText(3) == QStringLiteral("Blue"));
    channel->setCurrentIndex(1);
    black_output->setValue(255);
    process_events_for(180);
    saw_channel_preview = color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(255, 255, 0), 8) &&
                          color_close(canvas_pixel(*canvas, QPoint(70, 30)), QColor(255, 0, 0), 8) &&
                          color_close(canvas_pixel(*canvas, QPoint(110, 70)), QColor(Qt::white), 5);
    save_widget_artifact("ui_levels_red_channel_transparent_layer", *canvas);
    dialog->accept();
  });
  require_action(window, "imageAdjustLevelsAction")->trigger();
  QApplication::processEvents();

  CHECK(saw_channel_preview);
  CHECK(layer_list->count() == layer_count_before);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(255, 255, 0), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 30)), QColor(255, 0, 0), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(110, 70)), QColor(Qt::white), 5));
}

void ui_levels_dialog_preserves_independent_channel_records() {
  QTimer::singleShot(0, [] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLevelsDialog"));
    CHECK(dialog != nullptr);
    auto* channel = dialog->findChild<QComboBox*>(QStringLiteral("levelsChannelCombo"));
    auto* black_input = dialog->findChild<QSpinBox*>(QStringLiteral("levelsBlackInputSpin"));
    auto* black_output = dialog->findChild<QSpinBox*>(QStringLiteral("levelsBlackOutputSpin"));
    CHECK(channel != nullptr);
    CHECK(black_input != nullptr);
    CHECK(black_output != nullptr);
    CHECK(channel->currentText() == QStringLiteral("RGB"));
    black_input->setValue(24);
    channel->setCurrentIndex(1);
    CHECK(channel->currentText() == QStringLiteral("Red"));
    CHECK(black_input->value() == 0);
    black_output->setValue(255);
    channel->setCurrentIndex(0);
    CHECK(black_input->value() == 24);
    CHECK(black_output->value() == 0);
    channel->setCurrentIndex(1);
    CHECK(black_input->value() == 0);
    CHECK(black_output->value() == 255);
    dialog->accept();
  });

  const auto result = patchy::ui::request_levels_settings(nullptr);
  CHECK(result.has_value());
  CHECK(result->black_input == 24);
  CHECK(result->black_output == 0);
  CHECK(result->red.black_input == 0);
  CHECK(result->red.black_output == 255);
  CHECK(result->green.black_input == 0);
  CHECK(result->green.black_output == 0);
}

void ui_hue_saturation_dialog_adjusts_selected_pixels() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(255, 0, 0));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(255, 0, 0), 8));

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(120, 120)));
  QApplication::processEvents();

  bool saw_hue_preview = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyHueSaturationDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      auto* hue = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationHueSpin"));
      CHECK(hue != nullptr);
      hue->setValue(120);
      process_events_for(120);
      saw_hue_preview = color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 255, 0), 12);
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  require_action(window, "imageAdjustHueSaturationAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_hue_preview);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(255, 0, 0), 12));

  accept_hue_saturation_dialog(120, 0, 0);
  require_action(window, "imageAdjustHueSaturationAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 255, 0), 12));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(180, 70)), QColor(255, 0, 0), 12));
  save_widget_artifact("ui_hue_saturation_selection", *canvas);
}

void ui_hue_saturation_creates_masked_adjustment_layer() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  canvas->set_primary_color(QColor(255, 0, 0));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(120, 120)));
  QApplication::processEvents();

  bool saw_adjustment_layer_preview = false;
  bool saw_adjustment_layer_edit_lock = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyHueSaturationDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      CHECK(!dialog->isModal());
      CHECK(dialog->windowModality() == Qt::NonModal);
      CHECK(canvas->edit_locked());
      CHECK(!layer_list->isEnabled());
      CHECK(!require_action(window, "layerNewAction")->isEnabled());
      auto* hue = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationHueSpin"));
      CHECK(hue != nullptr);
      hue->setValue(120);
      process_events_for(120);
      saw_adjustment_layer_preview = color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 255, 0), 12);
      const auto preview_pixel_before_edit = canvas_pixel(*canvas, QPoint(70, 70));
      drag(*canvas, canvas->widget_position_for_document_point(QPoint(160, 40)),
           canvas->widget_position_for_document_point(QPoint(220, 110)));
      const auto preview_pixel_after_edit = canvas_pixel(*canvas, QPoint(70, 70));
      saw_adjustment_layer_edit_lock = color_close(preview_pixel_after_edit, preview_pixel_before_edit, 2);
      dialog->accept();
      return;
    }
    CHECK(false);
  });
  require_action(window, "layerNewHueSaturationAdjustmentAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_adjustment_layer_preview);
  CHECK(saw_adjustment_layer_edit_lock);
  CHECK(!canvas->edit_locked());
  CHECK(layer_list->isEnabled());

  CHECK(layer_list->item(0) != nullptr);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Hue/Saturation"));
  auto* adjustment_row = layer_list->itemWidget(layer_list->item(0));
  CHECK(adjustment_row != nullptr);
  CHECK(adjustment_row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail")) != nullptr);
  auto* adjustment_thumbnail = adjustment_row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(adjustment_thumbnail != nullptr);
  const auto thumbnail_image = adjustment_thumbnail->pixmap(Qt::ReturnByValue).toImage();
  CHECK(thumbnail_image.pixelColor(2, 2).lightness() < 220);
  auto* details = adjustment_row->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
  CHECK(details != nullptr);
  CHECK(details->text().contains(QStringLiteral("Normal")));
  CHECK(details->text().contains(QStringLiteral("100%")));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 255, 0), 12));

  bool saw_initial_adjustment_settings = false;
  bool saw_adjustment_edit_preview = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyHueSaturationDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      auto* hue = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationHueSpin"));
      CHECK(hue != nullptr);
      saw_initial_adjustment_settings = hue->value() == 120;
      hue->setValue(-120);
      process_events_for(120);
      saw_adjustment_edit_preview = color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 0, 255), 20);
      dialog->accept();
      return;
    }
    CHECK(false);
  });
  require_action(window, "layerEditAdjustmentAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_initial_adjustment_settings);
  CHECK(saw_adjustment_edit_preview);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 0, 255), 20));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(180, 70)), QColor(255, 0, 0), 12));

  layer_list->item(0)->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(255, 0, 0), 12));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(180, 70)), QColor(255, 0, 0), 12));
  save_widget_artifact("ui_hue_saturation_adjustment_layer", window);
}

void ui_adjustment_layer_thumbnails_show_type_symbols() {
  patchy::Document document(120, 96, patchy::PixelFormat::rgba8());
  auto add_adjustment_layer = [&document](const QString& name, const patchy::AdjustmentSettings& settings) {
    patchy::Layer layer(document.allocate_layer_id(), name.toStdString(), patchy::LayerKind::Adjustment);
    patchy::configure_adjustment_layer(layer, settings);
    document.add_layer(std::move(layer));
  };

  patchy::AdjustmentSettings levels;
  levels.kind = patchy::AdjustmentKind::Levels;
  levels.levels = patchy::LevelsAdjustment{18, 232, 85};
  add_adjustment_layer(QStringLiteral("Levels"), levels);

  patchy::AdjustmentSettings curves;
  curves.kind = patchy::AdjustmentKind::Curves;
  curves.curves = patchy::CurvesAdjustment{36, 176, 245};
  add_adjustment_layer(QStringLiteral("Curves"), curves);

  patchy::AdjustmentSettings hue_saturation;
  hue_saturation.kind = patchy::AdjustmentKind::HueSaturation;
  hue_saturation.hue_saturation = patchy::HueSaturationAdjustment{35, 35, 10};
  add_adjustment_layer(QStringLiteral("Hue/Saturation"), hue_saturation);

  patchy::AdjustmentSettings color_balance;
  color_balance.kind = patchy::AdjustmentKind::ColorBalance;
  color_balance.color_balance = patchy::ColorBalanceAdjustment{45, -25, 35};
  add_adjustment_layer(QStringLiteral("Color Balance"), color_balance);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Adjustment Thumbnails"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  auto thumbnail_image = [layer_list](const QString& name) {
    auto* item = require_layer_item(*layer_list, name);
    CHECK(item != nullptr);
    auto* row = layer_list->itemWidget(item);
    CHECK(row != nullptr);
    auto* thumbnail = row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
    CHECK(thumbnail != nullptr);
    CHECK(thumbnail->toolTip() == QStringLiteral("Adjustment Layer"));
    return thumbnail->pixmap(Qt::ReturnByValue).toImage();
  };
  const auto levels_image = thumbnail_image(QStringLiteral("Levels"));
  const auto curves_image = thumbnail_image(QStringLiteral("Curves"));
  const auto hue_saturation_image = thumbnail_image(QStringLiteral("Hue/Saturation"));
  const auto color_balance_image = thumbnail_image(QStringLiteral("Color Balance"));

  auto vivid_pixels = [](const QImage& image) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const auto color = image.pixelColor(x, y);
        const auto maximum = std::max({color.red(), color.green(), color.blue()});
        const auto minimum = std::min({color.red(), color.green(), color.blue()});
        if (maximum > 120 && maximum - minimum > 55) {
          ++count;
        }
      }
    }
    return count;
  };
  auto non_black_pixels = [](const QImage& image) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const auto color = image.pixelColor(x, y);
        if (color.red() + color.green() + color.blue() > 150) {
          ++count;
        }
      }
    }
    return count;
  };
  auto differing_pixels = [](const QImage& left, const QImage& right) {
    int count = 0;
    for (int y = 0; y < std::min(left.height(), right.height()); ++y) {
      for (int x = 0; x < std::min(left.width(), right.width()); ++x) {
        const auto a = left.pixelColor(x, y);
        const auto b = right.pixelColor(x, y);
        if (std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue()) > 80) {
          ++count;
        }
      }
    }
    return count;
  };

  const auto levels_vivid = vivid_pixels(levels_image);
  const auto curves_vivid = vivid_pixels(curves_image);
  const auto hue_saturation_vivid = vivid_pixels(hue_saturation_image);
  const auto color_balance_vivid = vivid_pixels(color_balance_image);
  const auto levels_non_black = non_black_pixels(levels_image);
  const auto curves_non_black = non_black_pixels(curves_image);
  const auto levels_curves_difference = differing_pixels(levels_image, curves_image);
  const auto hue_color_balance_difference = differing_pixels(hue_saturation_image, color_balance_image);
  CHECK(levels_vivid > 80);
  CHECK(curves_vivid > 80);
  CHECK(hue_saturation_vivid > 120);
  CHECK(color_balance_vivid > 80);
  CHECK(levels_non_black > 300);
  CHECK(curves_non_black > 300);
  CHECK(levels_curves_difference > 100);
  CHECK(hue_color_balance_difference > 100);
}

void ui_levels_dialog_remaps_selected_tonal_range() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(50, 50, 50));
  canvas->set_secondary_color(QColor(180, 180, 180));
  canvas->set_brush_opacity(100);
  require_action_by_text(window, QStringLiteral("Gradient"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 90)),
       canvas->widget_position_for_document_point(QPoint(260, 90)));
  QApplication::processEvents();

  const auto outside_before = canvas_pixel(*canvas, QPoint(320, 90));
  CHECK(outside_before.red() >= 170);
  CHECK(outside_before.red() <= 190);

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 50)),
       canvas->widget_position_for_document_point(QPoint(260, 130)));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  const auto layer_count_before = layer_list->count();

  accept_levels_dialog(50, 180, 100);
  require_action(window, "imageAdjustLevelsAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  CHECK(layer_list->count() == layer_count_before);
  CHECK(canvas_pixel(*canvas, QPoint(20, 90)).red() < 12);
  CHECK(canvas_pixel(*canvas, QPoint(260, 90)).red() > 242);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(320, 90)), outside_before, 8));
  save_widget_artifact("ui_levels_selection", *canvas);
}

void ui_curves_dialog_remaps_midtones_in_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(0, 0, 0));
  canvas->set_secondary_color(QColor(255, 255, 255));
  require_action_by_text(window, QStringLiteral("Gradient"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 90)),
       canvas->widget_position_for_document_point(QPoint(260, 90)));
  QApplication::processEvents();

  const auto selected_before = canvas_pixel(*canvas, QPoint(140, 90));
  const auto outside_before = canvas_pixel(*canvas, QPoint(320, 90));
  CHECK(selected_before.red() > 110);
  CHECK(selected_before.red() < 150);
  CHECK(outside_before.red() > 245);

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 50)),
       canvas->widget_position_for_document_point(QPoint(260, 130)));
  QApplication::processEvents();

  accept_curves_dialog(0, 220, 255);
  require_action(window, "imageAdjustCurvesAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  const auto selected_after = canvas_pixel(*canvas, QPoint(140, 90));
  CHECK(selected_after.red() > selected_before.red() + 60);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(320, 90)), outside_before, 8));
  save_widget_artifact("ui_curves_selection", *canvas);
}

void ui_color_balance_dialog_adjusts_selected_pixels() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(128, 128, 128));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(120, 120)));
  QApplication::processEvents();

  accept_color_balance_dialog(50, -40, 30);
  require_action(window, "imageAdjustColorBalanceAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  const auto adjusted = canvas_pixel(*canvas, QPoint(70, 70));
  CHECK(adjusted.red() > 245);
  CHECK(adjusted.green() < 40);
  CHECK(adjusted.blue() > 195);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(180, 70)), QColor(128, 128, 128), 8));
  save_widget_artifact("ui_color_balance_selection", *canvas);
}

void ui_gradient_and_magic_wand_render_visually() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(255, 0, 0));
  canvas->set_secondary_color(QColor(0, 0, 255));
  canvas->set_tool(patchy::ui::CanvasTool::Gradient);
  drag(*canvas, QPoint(60, 140), QPoint(280, 140));
  QApplication::processEvents();

  const auto gradient_image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  CHECK(color_close(QColor(gradient_image.pixel(62, 140)), QColor(255, 0, 0), 35));
  CHECK(color_close(QColor(gradient_image.pixel(278, 140)), QColor(0, 0, 255), 35));
  save_widget_artifact("ui_gradient_tool", *canvas);

  canvas->set_tool(patchy::ui::CanvasTool::MagicWand);
  send_mouse(*canvas, QEvent::MouseButtonPress, QPoint(62, 140), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, QPoint(62, 140), Qt::LeftButton, Qt::NoButton);
  CHECK(canvas->selected_document_rect().has_value());
  save_widget_artifact("ui_magic_wand_selection", *canvas);
}

void ui_radial_gradient_tool_renders_custom_transparency() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Gradient);
  canvas->set_gradient_method(patchy::GradientMethod::Radial);
  canvas->set_gradient_opacity(100);
  canvas->set_gradient_reverse(false);
  canvas->set_gradient_stops(std::vector<patchy::GradientStop>{
      patchy::GradientStop{0.0F, patchy::EditColor{255, 0, 0, 255}},
      patchy::GradientStop{1.0F, patchy::EditColor{0, 0, 255, 0}},
  });
  drag(*canvas, QPoint(160, 140), QPoint(220, 140));
  QApplication::processEvents();

  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  CHECK(color_close(QColor(image.pixel(160, 140)), QColor(255, 0, 0), 35));
  CHECK(color_close(QColor(image.pixel(220, 140)), Qt::white, 35));
  save_widget_artifact("ui_radial_gradient_transparency", *canvas);
}

void ui_magic_wand_contiguous_and_sample_all_layers_options_work() {
  patchy::Document document(320, 220, patchy::PixelFormat::rgba8());
  auto lower_pixels = solid_pixels(320, 220, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(lower_pixels, QRect(210, 42, 38, 38), QColor(220, 30, 55, 255));
  document.add_pixel_layer("Lower Match", std::move(lower_pixels));

  auto active_pixels = solid_pixels(320, 220, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(active_pixels, QRect(30, 42, 38, 38), QColor(220, 30, 55, 255));
  fill_pixel_rect(active_pixels, QRect(110, 42, 38, 38), QColor(220, 30, 55, 255));
  document.add_pixel_layer("Active Matches", std::move(active_pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Magic Wand Options"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagicWand);
  canvas->set_wand_tolerance(0);
  canvas->set_selection_feather_radius(0);

  const auto click_wand = [canvas](QPoint document_point) {
    const auto widget_point = canvas->widget_position_for_document_point(document_point);
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  canvas->set_wand_contiguous(true);
  canvas->set_wand_sample_all_layers(false);
  click_wand(QPoint(40, 52));
  CHECK(canvas->selected_document_region().contains(QPoint(40, 52)));
  CHECK(!canvas->selected_document_region().contains(QPoint(120, 52)));
  CHECK(!canvas->selected_document_region().contains(QPoint(220, 52)));

  canvas->clear_selection();
  canvas->set_wand_contiguous(false);
  canvas->set_wand_sample_all_layers(false);
  click_wand(QPoint(40, 52));
  CHECK(canvas->selected_document_region().contains(QPoint(40, 52)));
  CHECK(canvas->selected_document_region().contains(QPoint(120, 52)));
  CHECK(!canvas->selected_document_region().contains(QPoint(220, 52)));

  canvas->clear_selection();
  canvas->set_wand_contiguous(false);
  canvas->set_wand_sample_all_layers(true);
  click_wand(QPoint(40, 52));
  CHECK(canvas->selected_document_region().contains(QPoint(40, 52)));
  CHECK(canvas->selected_document_region().contains(QPoint(120, 52)));
  CHECK(canvas->selected_document_region().contains(QPoint(220, 52)));
  save_widget_artifact("ui_magic_wand_options", *canvas);
}

void ui_magic_wand_sample_all_layers_clear_transparent_active_layer_is_noop() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("White background",
                           solid_pixels(160, 120, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
  auto active_pixels = solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(active_pixels, QRect(48, 36, 36, 28), QColor(35, 95, 220, 255));
  document.add_pixel_layer("Active art", std::move(active_pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Magic Wand Delete Noop"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* undo_action = require_action_by_text(window, QStringLiteral("Undo"));
  CHECK(!undo_action->isEnabled());

  canvas->set_tool(patchy::ui::CanvasTool::MagicWand);
  canvas->set_wand_tolerance(0);
  canvas->set_wand_contiguous(true);
  canvas->set_wand_sample_all_layers(true);
  const auto click_point = canvas->widget_position_for_document_point(QPoint(8, 8));
  send_mouse(*canvas, QEvent::MouseButtonPress, click_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(canvas->has_selection());
  CHECK(canvas->selected_document_region().contains(QPoint(8, 8)));
  CHECK(!canvas->selected_document_region().contains(QPoint(55, 44)));

  require_action(window, "layerClearAction")->trigger();
  QApplication::processEvents();

  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Nothing to clear")));
  CHECK(!undo_action->isEnabled());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(55, 44)), QColor(35, 95, 220), 8));
}

void ui_magic_wand_complex_selection_is_responsive() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(0, 0, 0));
  canvas->set_brush_size(10);
  for (int y = 80; y <= 240; y += 40) {
    for (int x = 90; x <= 290; x += 40) {
      const auto point = canvas->widget_position_for_document_point(QPoint(x, y));
      drag(*canvas, point, point + QPoint(1, 1));
    }
  }
  QApplication::processEvents();

  canvas->set_tool(patchy::ui::CanvasTool::MagicWand);
  QElapsedTimer timer;
  timer.start();
  const auto click_point = canvas->widget_position_for_document_point(QPoint(25, 25));
  send_mouse(*canvas, QEvent::MouseButtonPress, click_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click_point, Qt::LeftButton, Qt::NoButton);
  const auto elapsed_ms = timer.elapsed();
  CHECK(elapsed_ms < 1500);
  CHECK(canvas->selected_document_region().contains(QPoint(25, 25)));
  CHECK(!canvas->selected_document_region().contains(QPoint(90, 80)));
  save_widget_artifact("ui_magic_wand_complex_selection", *canvas);
}

void ui_bundled_legacy_plugin_action_applies_filter() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(255, 0, 0));
  canvas->set_brush_size(24);
  const QPoint sample_document_point(72, 72);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(60, 72)),
       canvas->widget_position_for_document_point(QPoint(95, 72)));
  QApplication::processEvents();

  const auto sample_widget_point = canvas->widget_position_for_document_point(sample_document_point);
  const auto before = canvas->grab().toImage().pixelColor(sample_widget_point);
  CHECK(before.red() > 180);
  CHECK(before.green() < 100);
  CHECK(before.blue() < 100);

  QAction* greyscale = nullptr;
  for (auto* action : window.findChildren<QAction*>(QStringLiteral("legacyPluginAction"))) {
    if (action->text().contains(QStringLiteral("Greyscale"), Qt::CaseInsensitive)) {
      greyscale = action;
      break;
    }
  }
  CHECK(greyscale != nullptr);
  greyscale->trigger();
  QApplication::processEvents();

  const auto after = canvas->grab().toImage().pixelColor(sample_widget_point);
  CHECK(std::abs(after.red() - after.green()) <= 8);
  CHECK(std::abs(after.green() - after.blue()) <= 8);
  CHECK(after.red() > 45);
  CHECK(after.red() < 120);
  save_widget_artifact("ui_legacy_plugin_greyscale", window);
}

void ui_transparency_checkerboard_and_copy_paste_preserve_alpha() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  canvas->set_primary_color(Qt::white);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_legacy_plugin_action(window, QStringLiteral("White to Transparent"))->trigger();
  QApplication::processEvents();

  require_layer_item(*layer_list, QStringLiteral("Background"))->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  auto transparent_preview = canvas_pixel(*canvas, QPoint(40, 40));
  CHECK(transparent_preview.alpha() == 255);
  CHECK(transparent_preview.red() >= 170);
  CHECK(transparent_preview.red() <= 245);
  CHECK(std::abs(transparent_preview.red() - transparent_preview.green()) <= 4);
  CHECK(std::abs(transparent_preview.green() - transparent_preview.blue()) <= 4);
  save_widget_artifact("ui_transparency_checkerboard", window);

  require_action(window, "editSelectAllAction")->trigger();
  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();

  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  accept_new_document_dialog(220, 180);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();

  canvas = require_canvas(window);
  layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  require_layer_item(*layer_list, QStringLiteral("Background"))->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  const auto pasted_preview = canvas_pixel(*canvas, QPoint(24, 24));
  CHECK(pasted_preview.red() >= 170);
  CHECK(pasted_preview.red() <= 245);
  CHECK(std::abs(pasted_preview.red() - pasted_preview.green()) <= 4);
  CHECK(std::abs(pasted_preview.green() - pasted_preview.blue()) <= 4);
  save_widget_artifact("ui_transparent_copy_paste", window);
}

void ui_crop_rotate_stroke_merge_and_filter_render_visually() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_snap_enabled(false);

  canvas->set_primary_color(QColor(0, 130, 255));
  canvas->set_tool(patchy::ui::CanvasTool::Brush);
  drag(*canvas, QPoint(72, 72), QPoint(190, 140));
  save_widget_artifact("ui_brush_before_crop", *canvas);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, QPoint(60, 60), QPoint(200, 160));
  require_action(window, "imageCropToSelectionAction")->trigger();
  QApplication::processEvents();
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(info != nullptr);
  CHECK(info->text().contains(QStringLiteral("141 x 101 px")));
  save_widget_artifact("ui_crop_to_selection", window);

  require_action(window, "imageRotateClockwiseAction")->trigger();
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("101 x 141 px")));
  save_widget_artifact("ui_rotate_canvas", window);

  canvas->set_primary_color(QColor(255, 50, 50));
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, QPoint(52, 52), QPoint(100, 90));
  require_action(window, "editStrokeSelectionAction")->trigger();
  QApplication::processEvents();
  save_widget_artifact("ui_stroke_selection", *canvas);

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  const auto layers_before = layer_list->count();
  require_action(window, "layerMergeVisibleAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);

  auto* sepia = find_action_by_text(window, QStringLiteral("Vintage Sepia"));
  CHECK(sepia != nullptr);
  accept_filter_dialog();
  sepia->trigger();
  QApplication::processEvents();
  save_widget_artifact("ui_merge_visible_and_filter", window);
}

void visual_contact_sheet_contains_new_feature_artifacts() {
  ensure_artifact_dir();
  const std::vector<std::string> artifacts = {
      "ui_main_window.png",
      "ui_window_force_refresh.png",
      "ui_color_picker.png",
      "ui_color_picker_gradient.png",
      "ui_color_picker_result.png",
      "ui_canvas_wheel_navigation.png",
      "ui_shape_flyout_zoom_tool.png",
      "ui_filled_shape_preview_cleanup.png",
      "ui_tool_options_move.png",
      "ui_tool_options_text.png",
      "ui_info_panel_layers_docks.png",
      "ui_layer_style_dialog.png",
      "ui_layer_style_result.png",
      "ui_new_document_dialog.png",
      "ui_new_document_result.png",
      "ui_image_size_dialog.png",
      "ui_image_size_result.png",
      "ui_canvas_size_dialog.png",
      "ui_canvas_size_result.png",
      "ui_multiple_documents.png",
      "ui_first_tab_draw_after_second_tab.png",
      "ui_tab_session_layers.png",
      "ui_new_layer_result.png",
      "ui_multiselect_merge_down.png",
      "ui_multiselect_duplicate_delete.png",
      "ui_duplicate_text_folder_tree.png",
      "ui_copy_paste_layer_panel_tree.png",
      "ui_layer_visibility_drag_reorder.png",
      "ui_layer_folder_drag_drop.png",
      "ui_layer_panel_mixed_folder_visual_cleanup.png",
      "ui_layer_folder_expand_contract.png",
      "ui_layer_folder_saved_state.png",
      "ui_auto_select_reveals_collapsed_folder.png",
      "ui_move_preview_layer_order.png",
      "ui_move_opaque_bounds.png",
      "ui_move_hover_opaque_bounds.png",
      "ui_move_active_tab_only.png",
      "ui_move_selected_folder_tree.png",
      "ui_move_preview_mid_drag_partial_repaint.png",
      "ui_dirty_region_move_preview_force_refresh.png",
      "ui_selection_modifiers.png",
      "ui_selection_toolbar_modes.png",
      "ui_ctrl_a_select_all.png",
      "ui_alt_backspace_fill_selection.png",
      "ui_feathered_marquee_fill.png",
      "ui_marquee_fixed_size_ratio.png",
      "ui_elliptical_marquee.png",
      "ui_marquee_space_drag_reposition.png",
      "ui_rulers_grid_guides.png",
      "ui_guides_editing.png",
      "ui_snapped_marquee_guides.png",
      "ui_snap_shape_text_move.png",
      "ui_grid_guides_preferences_dialogs.png",
      "ui_complex_selection_outline.png",
      "ui_selection_edges_visible_no_tint.png",
      "ui_selection_edges_hidden.png",
      "ui_select_inverse.png",
      "ui_select_reselect.png",
      "ui_selection_expand_contract_transparency.png",
      "ui_selection_border.png",
      "ui_ctrl_click_layer_transparency.png",
      "ui_select_grow.png",
      "ui_select_similar.png",
      "ui_complex_stroke_selection.png",
      "ui_extended_blend_modes.png",
      "ui_layer_lock_transparency.png",
      "ui_layer_full_lock_controls.png",
      "ui_folder_lock_inheritance.png",
      "ui_move_auto_select_locked_layer.png",
      "ui_keyboard_nudge_layer.png",
      "ui_lasso_selection.png",
      "ui_copy_paste_transform.png",
      "ui_transform_opaque_bounds.png",
      "ui_move_show_transform_controls.png",
      "ui_layer_via_copy_cut.png",
      "ui_layer_mask_from_selection.png",
      "ui_layer_mask_target_editing.png",
      "ui_layer_thumbnail_refresh.png",
      "ui_cut_selection.png",
      "ui_brush_expands_pasted_layer.png",
      "ui_brush_opacity_per_stroke.png",
      "ui_layer_mask_brush_opacity_per_stroke.png",
      "ui_airbrush_no_same_stroke_stack.png",
      "ui_airbrush_event_density.png",
      "ui_airbrush_smoothed_jitter.png",
      "ui_clone_tool_stamp.png",
      "ui_smudge_tool.png",
      "ui_hidden_layer_copy_ignored.png",
      "ui_copy_selected_layers.png",
      "ui_background_eraser_transparency.png",
      "ui_inline_text_editor.png",
      "ui_text_tool_layer.png",
      "ui_transformed_text_reedit.png",
      "format_alpha.png",
      "ui_image_adjustments_invert_desaturate.png",
      "ui_image_adjustments_auto_contrast.png",
      "ui_all_builtin_filters_stroke_contact_sheet.png",
      "ui_image_adjustment_selection_scope.png",
      "ui_hue_saturation_dialog.png",
      "ui_hue_saturation_selection.png",
      "ui_hue_saturation_adjustment_layer.png",
      "ui_levels_dialog.png",
      "ui_levels_selection.png",
      "ui_curves_dialog.png",
      "ui_curves_selection.png",
      "ui_color_balance_dialog.png",
      "ui_color_balance_selection.png",
      "ui_gradient_tool.png",
      "ui_radial_gradient_transparency.png",
      "ui_magic_wand_selection.png",
      "ui_magic_wand_options.png",
      "ui_magic_wand_complex_selection.png",
      "ui_legacy_plugin_greyscale.png",
      "ui_transparency_checkerboard.png",
      "ui_transparent_copy_paste.png",
      "ui_crop_to_selection.png",
      "ui_rotate_canvas.png",
      "ui_stroke_selection.png",
      "ui_merge_visible_and_filter.png",
      "tool_gradient.bmp",
      "tool_soft_brush.bmp",
      "tool_brush_expand_layer.bmp",
      "document_crop.bmp",
      "document_canvas_resize.bmp",
      "document_image_resize.bmp",
      "document_rotate_clockwise.bmp",
      "document_rotate_counterclockwise.bmp",
      "tool_stroke_selection.bmp",
      "tool_lock_transparency.bmp",
      "layer_merge_visible.bmp",
      "filter_brightness_contrast.bmp",
      "filter_grayscale.bmp",
      "filter_desaturate.bmp",
      "filter_auto_contrast.bmp",
      "filter_sepia.bmp",
      "filter_threshold.bmp",
      "filter_posterize.bmp",
      "filter_box_blur.bmp",
      "filter_sharpen.bmp",
      "filter_unsharp_mask.bmp",
      "filter_gaussian_blur.bmp",
      "filter_motion_blur.bmp",
      "filter_radial_blur.bmp",
      "filter_edge_detect.bmp",
      "filter_emboss.bmp",
      "filter_glowing_edges.bmp",
      "filter_twirl.bmp",
      "filter_wave.bmp",
      "filter_pinch_bloat.bmp",
      "filter_clouds.bmp",
      "filter_pixelate.bmp",
      "filter_color_halftone.bmp",
      "filter_film_grain.bmp",
      "filter_vignette.bmp",
      "filter_soft_glow.bmp",
      "filter_punchy_color.bmp",
      "filter_noir.bmp",
      "filter_cinematic_matte.bmp",
      "filter_vintage_fade.bmp",
      "ui_filter_edge_detect.png",
  };

  constexpr int kColumns = 4;
  constexpr int kCellWidth = 280;
  constexpr int kCellHeight = 220;
  constexpr int kPadding = 12;
  const auto rows = static_cast<int>((artifacts.size() + kColumns - 1) / kColumns);
  QImage sheet(kColumns * kCellWidth, rows * kCellHeight, QImage::Format_RGB32);
  sheet.fill(QColor(30, 32, 36));

  QPainter painter(&sheet);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.setFont(visual_test_font());
  painter.setPen(QColor(225, 230, 238));
  for (std::size_t index = 0; index < artifacts.size(); ++index) {
    const auto path = std::filesystem::path("test-artifacts") / artifacts[index];
    CHECK(std::filesystem::exists(path));
    QImage image(QString::fromStdString(path.string()));
    CHECK(!image.isNull());

    const auto column = static_cast<int>(index % kColumns);
    const auto row = static_cast<int>(index / kColumns);
    const QRect cell(column * kCellWidth, row * kCellHeight, kCellWidth, kCellHeight);
    painter.fillRect(cell.adjusted(4, 4, -4, -4), QColor(42, 45, 51));
    painter.drawText(cell.adjusted(kPadding, 8, -kPadding, -kPadding), Qt::AlignTop | Qt::AlignLeft,
                     QString::fromStdString(artifacts[index]));

    const QRect image_rect = cell.adjusted(kPadding, 34, -kPadding, -kPadding);
    const auto scaled = image.scaled(image_rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint image_pos(image_rect.x() + (image_rect.width() - scaled.width()) / 2,
                           image_rect.y() + (image_rect.height() - scaled.height()) / 2);
    painter.drawImage(image_pos, scaled);
  }
  painter.end();

  CHECK(sheet.save(QStringLiteral("test-artifacts/visual_feature_contact_sheet.png")));
}

}  // namespace

int main(int argc, char* argv[]) {
  patchy::test::suppress_crash_dialogs();
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  QApplication app(argc, argv);
  app.setFont(visual_test_font());
  ensure_artifact_dir();
  const auto test_settings_path = QDir::current().filePath(QStringLiteral("test-artifacts/settings"));
  CHECK(QDir().mkpath(test_settings_path));
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, test_settings_path);
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("tools"));
    settings.remove(QStringLiteral("view"));
    settings.remove(QStringLiteral("preferences/language"));
    settings.setValue(QStringLiteral("updates/checkOnStartup"), false);
    settings.sync();
  }
  patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false);

  const std::vector<TestCase> tests = {
      {"ui_main_window_renders_color_swatches", ui_main_window_renders_color_swatches},
      {"ui_window_force_refresh_action_rebuilds_cache", ui_window_force_refresh_action_rebuilds_cache},
      {"ui_top_menu_items_highlight_on_hover", ui_top_menu_items_highlight_on_hover},
      {"ui_save_as_dialog_lists_recent_files", ui_save_as_dialog_lists_recent_files},
      {"ui_open_recent_keeps_fifty_files", ui_open_recent_keeps_fifty_files},
      {"ui_recent_file_context_menu_copies_path", ui_recent_file_context_menu_copies_path},
      {"ui_save_as_remembers_last_save_directory_between_windows",
       ui_save_as_remembers_last_save_directory_between_windows},
      {"update_manifest_parser_handles_supported_cases", update_manifest_parser_handles_supported_cases},
      {"ui_update_available_dialog_warns_to_close_patchy_before_installing",
       ui_update_available_dialog_warns_to_close_patchy_before_installing},
      {"ui_update_preference_defaults_startup_check_setting_to_enabled",
       ui_update_preference_defaults_startup_check_setting_to_enabled},
      {"ui_update_preference_persists_startup_check_setting", ui_update_preference_persists_startup_check_setting},
      {"ui_language_switch_updates_existing_window", ui_language_switch_updates_existing_window},
      {"ui_language_preference_applies_at_startup", ui_language_preference_applies_at_startup},
      {"ui_language_invalid_preference_falls_back_to_english", ui_language_invalid_preference_falls_back_to_english},
      {"ui_language_catalog_covers_dialog_status_and_properties",
       ui_language_catalog_covers_dialog_status_and_properties},
      {"ui_about_dialog_shows_labeled_external_links", ui_about_dialog_shows_labeled_external_links},
      {"ui_frameless_window_edges_resize", ui_frameless_window_edges_resize},
      {"ui_right_edge_scrollbars_remain_draggable", ui_right_edge_scrollbars_remain_draggable},
      {"ui_svg_icon_resources_are_registered", ui_svg_icon_resources_are_registered},
      {"ui_filter_menu_groups_builtin_filters", ui_filter_menu_groups_builtin_filters},
      {"ui_filter_progress_callback_can_cancel_heavy_filter",
       ui_filter_progress_callback_can_cancel_heavy_filter},
      {"ui_filter_progress_callback_can_cancel_simple_filter",
       ui_filter_progress_callback_can_cancel_simple_filter},
      {"ui_all_builtin_filters_report_progress",
       ui_all_builtin_filters_report_progress},
      {"ui_adjustment_pixel_progress_callback_can_cancel",
       ui_adjustment_pixel_progress_callback_can_cancel},
      {"ui_filter_settings_dialog_shows_before_initial_preview",
       ui_filter_settings_dialog_shows_before_initial_preview},
      {"ui_filter_settings_dialog_coalesces_rapid_slider_preview_callbacks",
       ui_filter_settings_dialog_coalesces_rapid_slider_preview_callbacks},
      {"ui_filter_settings_dialog_delivers_latest_after_slow_preview_callback",
       ui_filter_settings_dialog_delivers_latest_after_slow_preview_callback},
      {"ui_brightness_contrast_filter_applies_settings",
       ui_brightness_contrast_filter_applies_settings},
      {"ui_filter_preview_restores_unselected_region_runs",
       ui_filter_preview_restores_unselected_region_runs},
      {"ui_all_builtin_filters_render_stroke_contact_sheet",
       ui_all_builtin_filters_render_stroke_contact_sheet},
      {"ui_color_picker_changes_foreground_color", ui_color_picker_changes_foreground_color},
      {"ui_dialog_position_memory_restores_last_position", ui_dialog_position_memory_restores_last_position},
      {"ui_dialog_position_memory_centers_unmoved_dialogs_on_parent",
       ui_dialog_position_memory_centers_unmoved_dialogs_on_parent},
      {"ui_dirty_state_marks_tabs_and_undo_restores_saved_revision",
       ui_dirty_state_marks_tabs_and_undo_restores_saved_revision},
      {"ui_compatibility_report_flags_psd_text_placeholders",
       ui_compatibility_report_flags_psd_text_placeholders},
      {"ui_compatibility_report_ignores_patchy_written_psd_blocks",
       ui_compatibility_report_ignores_patchy_written_psd_blocks},
      {"ui_compatibility_report_treats_levels_as_native_psd_adjustment",
       ui_compatibility_report_treats_levels_as_native_psd_adjustment},
      {"ui_compatibility_report_flags_cmyk_rgb_conversion",
       ui_compatibility_report_flags_cmyk_rgb_conversion},
      {"ui_alt_left_click_samples_foreground_color", ui_alt_left_click_samples_foreground_color},
      {"ui_photoshop_shortcuts_are_registered", ui_photoshop_shortcuts_are_registered},
      {"ui_startup_defaults_to_ink_brush", ui_startup_defaults_to_ink_brush},
      {"ui_canvas_wheel_matches_photoshop_navigation", ui_canvas_wheel_matches_photoshop_navigation},
      {"ui_canvas_pan_keeps_document_partly_visible", ui_canvas_pan_keeps_document_partly_visible},
      {"ui_canvas_fractional_zoom_paints_to_document_edge", ui_canvas_fractional_zoom_paints_to_document_edge},
      {"ui_canvas_fractional_zoom_keeps_zoomed_in_pixels_sharp",
       ui_canvas_fractional_zoom_keeps_zoomed_in_pixels_sharp},
      {"ui_canvas_deep_zoom_without_grid_keeps_pixels_sharp",
       ui_canvas_deep_zoom_without_grid_keeps_pixels_sharp},
      {"ui_zoomed_out_canvas_uses_downsampled_display_mip",
       ui_zoomed_out_canvas_uses_downsampled_display_mip},
      {"ui_shape_flyout_and_zoom_tool_work", ui_shape_flyout_and_zoom_tool_work},
      {"ui_filled_shape_preview_clears_after_commit", ui_filled_shape_preview_clears_after_commit},
      {"ui_options_bar_tracks_active_tool", ui_options_bar_tracks_active_tool},
      {"ui_right_docks_collapse_layers_show_metadata_and_info_updates",
       ui_right_docks_collapse_layers_show_metadata_and_info_updates},
      {"ui_collapsed_right_docks_keep_deep_layer_rows_readable",
       ui_collapsed_right_docks_keep_deep_layer_rows_readable},
      {"ui_layer_context_menu_exposes_blending_options_dialog",
       ui_layer_context_menu_exposes_blending_options_dialog},
      {"ui_layer_row_double_click_opens_blending_options_dialog",
       ui_layer_row_double_click_opens_blending_options_dialog},
      {"ui_layer_context_menu_rasterizes_text_and_layer_styles",
       ui_layer_context_menu_rasterizes_text_and_layer_styles},
      {"ui_layer_context_menu_layer_style_actions_follow_selection_state",
       ui_layer_context_menu_layer_style_actions_follow_selection_state},
      {"ui_layer_style_copy_paste_delete_applies_to_selected_layers",
       ui_layer_style_copy_paste_delete_applies_to_selected_layers},
      {"ui_closing_last_document_leaves_empty_workspace", ui_closing_last_document_leaves_empty_workspace},
      {"ui_document_tab_context_menu_closes_tabs_and_file_menu_closes_all",
       ui_document_tab_context_menu_closes_tabs_and_file_menu_closes_all},
      {"ui_new_document_and_canvas_size_dialogs_work", ui_new_document_and_canvas_size_dialogs_work},
      {"ui_new_document_presets_and_clipboard_work", ui_new_document_presets_and_clipboard_work},
      {"ui_new_document_background_starts_locked", ui_new_document_background_starts_locked},
      {"ui_first_tab_still_draws_after_second_tab_created", ui_first_tab_still_draws_after_second_tab_created},
      {"ui_tab_switch_layers_follow_the_canvas_after_tab_reorder",
       ui_tab_switch_layers_follow_the_canvas_after_tab_reorder},
      {"ui_new_layer_defaults_and_multiselect_layers_work", ui_new_layer_defaults_and_multiselect_layers_work},
      {"ui_merge_down_repeatedly_collapses_to_one_layer", ui_merge_down_repeatedly_collapses_to_one_layer},
      {"ui_merge_down_preserves_transparent_pixels", ui_merge_down_preserves_transparent_pixels},
      {"ui_merge_down_rasterizes_text_target", ui_merge_down_rasterizes_text_target},
      {"ui_new_layer_button_inserts_above_selected_layer", ui_new_layer_button_inserts_above_selected_layer},
      {"ui_document_default_layer_selection_skips_folder",
       ui_document_default_layer_selection_skips_folder},
      {"ui_document_with_only_folders_opens_with_no_layer_selected",
       ui_document_with_only_folders_opens_with_no_layer_selected},
      {"ui_duplicate_layer_copies_text_and_folder_trees", ui_duplicate_layer_copies_text_and_folder_trees},
      {"ui_copy_paste_layer_panel_copies_layers_and_folder_trees",
       ui_copy_paste_layer_panel_copies_layers_and_folder_trees},
      {"ui_layer_rows_toggle_visibility_and_drag_reorder", ui_layer_rows_toggle_visibility_and_drag_reorder},
      {"ui_layer_folders_create_with_drag_drop_affordances", ui_layer_folders_create_with_drag_drop_affordances},
      {"ui_layer_panel_mixed_folder_visual_cleanup", ui_layer_panel_mixed_folder_visual_cleanup},
      {"ui_layer_drag_drops_child_above_parent_folder", ui_layer_drag_drops_child_above_parent_folder},
      {"ui_layer_drag_multiselect_drops_children_above_parent_folder",
       ui_layer_drag_multiselect_drops_children_above_parent_folder},
      {"ui_layer_drag_folder_header_crack_drops_inside_folder",
       ui_layer_drag_folder_header_crack_drops_inside_folder},
      {"ui_layer_drag_shows_insertion_and_folder_drop_previews",
       ui_layer_drag_shows_insertion_and_folder_drop_previews},
      {"ui_layer_list_scrolls_with_wheel_and_drag_autoscroll",
       ui_layer_list_scrolls_with_wheel_and_drag_autoscroll},
      {"ui_layer_new_folder_button_groups_dropped_layers",
       ui_layer_new_folder_button_groups_dropped_layers},
      {"ui_layer_action_buttons_accept_multiselect_drops", ui_layer_action_buttons_accept_multiselect_drops},
      {"ui_layer_folders_expand_and_contract_children", ui_layer_folders_expand_and_contract_children},
      {"ui_layer_folders_open_with_saved_expansion_state", ui_layer_folders_open_with_saved_expansion_state},
      {"ui_move_auto_select_reveals_layers_in_collapsed_folders",
       ui_move_auto_select_reveals_layers_in_collapsed_folders},
      {"ui_folder_visibility_preserves_layer_panel_scroll", ui_folder_visibility_preserves_layer_panel_scroll},
      {"ui_move_preview_preserves_layer_order", ui_move_preview_preserves_layer_order},
      {"ui_move_tool_moves_selected_layers_together", ui_move_tool_moves_selected_layers_together},
      {"ui_move_auto_select_hover_outlines_with_multi_selection",
       ui_move_auto_select_hover_outlines_with_multi_selection},
      {"ui_move_auto_select_drag_replaces_multi_selection", ui_move_auto_select_drag_replaces_multi_selection},
      {"ui_move_auto_select_selected_member_drag_keeps_multi_selection",
       ui_move_auto_select_selected_member_drag_keeps_multi_selection},
      {"ui_move_auto_select_blank_drag_keeps_multi_selection",
       ui_move_auto_select_blank_drag_keeps_multi_selection},
      {"ui_shift_constrains_move_tool_drag_to_axis", ui_shift_constrains_move_tool_drag_to_axis},
      {"ui_move_tool_uses_opaque_bounds_for_transparent_layer",
       ui_move_tool_uses_opaque_bounds_for_transparent_layer},
      {"ui_move_tool_hover_outlines_opaque_bounds", ui_move_tool_hover_outlines_opaque_bounds},
      {"ui_move_tool_uses_text_rect_for_hit_and_hover",
       ui_move_tool_uses_text_rect_for_hit_and_hover},
      {"ui_move_transform_controls_do_not_block_auto_select_hover",
       ui_move_transform_controls_do_not_block_auto_select_hover},
      {"ui_move_tool_moves_selected_folder_tree", ui_move_tool_moves_selected_folder_tree},
      {"ui_move_preview_clears_transparent_trails_and_keeps_layer_styles",
       ui_move_preview_clears_transparent_trails_and_keeps_layer_styles},
      {"ui_move_preview_leaves_no_trail_when_zoomed_out", ui_move_preview_leaves_no_trail_when_zoomed_out},
      {"ui_move_preview_mid_drag_partial_repaint_matches_full_preview",
       ui_move_preview_mid_drag_partial_repaint_matches_full_preview},
      {"ui_dirty_region_move_preview_matches_force_refresh",
       ui_dirty_region_move_preview_matches_force_refresh},
      {"ui_processing_overlay_animates_for_slow_dirty_render",
       ui_processing_overlay_animates_for_slow_dirty_render},
      {"ui_processing_overlay_stays_top_aligned_without_dimming_canvas",
       ui_processing_overlay_stays_top_aligned_without_dimming_canvas},
      {"ui_brush_family_strokes_do_not_trigger_processing_overlay",
       ui_brush_family_strokes_do_not_trigger_processing_overlay},
      {"ui_processing_overlay_animates_for_slow_nudge_undo_snapshot",
       ui_processing_overlay_animates_for_slow_nudge_undo_snapshot},
      {"ui_processing_overlay_is_visible_before_slow_move_commit_callback",
       ui_processing_overlay_is_visible_before_slow_move_commit_callback},
      {"ui_processing_overlay_ticks_during_filter_apply",
       ui_processing_overlay_ticks_during_filter_apply},
      {"ui_processing_overlay_ticks_during_fill_tool_loop",
       ui_processing_overlay_ticks_during_fill_tool_loop},
      {"ui_layer_style_cache_invalidates_after_pixel_mutation",
       ui_layer_style_cache_invalidates_after_pixel_mutation},
      {"ui_move_expensive_styled_layer_uses_outline_until_release",
       ui_move_expensive_styled_layer_uses_outline_until_release},
      {"ui_layer_move_repaints_only_active_document_tab", ui_layer_move_repaints_only_active_document_tab},
      {"ui_arduboy_psd_render_path_if_available", ui_arduboy_psd_render_path_if_available},
      {"ui_duke_psd_text_edit_stays_responsive_if_available",
       ui_duke_psd_text_edit_stays_responsive_if_available},
      {"ui_duke_psd_seth_text_edit_preview_if_available",
       ui_duke_psd_seth_text_edit_preview_if_available},
      {"ui_text_reedit_preserves_rich_text_spacing",
       ui_text_reedit_preserves_rich_text_spacing},
      {"ui_marquee_selection_modifiers_work", ui_marquee_selection_modifiers_work},
      {"ui_selection_toolbar_modes_work", ui_selection_toolbar_modes_work},
      {"ui_ctrl_a_selects_entire_canvas", ui_ctrl_a_selects_entire_canvas},
      {"ui_alt_backspace_fills_selection_with_foreground", ui_alt_backspace_fills_selection_with_foreground},
      {"ui_feathered_marquee_fill_uses_soft_selection_alpha",
       ui_feathered_marquee_fill_uses_soft_selection_alpha},
      {"ui_marquee_fixed_size_and_ratio_options_work", ui_marquee_fixed_size_and_ratio_options_work},
      {"ui_elliptical_marquee_selects_oval_region", ui_elliptical_marquee_selects_oval_region},
      {"ui_marquee_space_drag_repositions_active_rect", ui_marquee_space_drag_repositions_active_rect},
      {"ui_rulers_grid_guides_render_and_edit", ui_rulers_grid_guides_render_and_edit},
      {"ui_deep_zoom_pixel_grid_matches_rendered_pixels",
       ui_deep_zoom_pixel_grid_matches_rendered_pixels},
      {"ui_deep_zoom_one_pixel_brush_marks_match_pixel_grid",
       ui_deep_zoom_one_pixel_brush_marks_match_pixel_grid},
      {"ui_deep_zoom_subpixel_subdivisions_do_not_draw_inside_pixels",
       ui_deep_zoom_subpixel_subdivisions_do_not_draw_inside_pixels},
      {"ui_deep_zoom_fractional_subdivision_spacing_stays_on_pixel_edges",
       ui_deep_zoom_fractional_subdivision_spacing_stays_on_pixel_edges},
      {"ui_deep_zoom_grid_subdivision_counts_change_spacing",
       ui_deep_zoom_grid_subdivision_counts_change_spacing},
      {"ui_snap_marquee_uses_screen_pixel_tolerance_and_target_toggles",
       ui_snap_marquee_uses_screen_pixel_tolerance_and_target_toggles},
      {"ui_snap_applies_to_shape_text_and_move_tools", ui_snap_applies_to_shape_text_and_move_tools},
      {"ui_canvas_aid_preferences_and_guide_dialogs_work", ui_canvas_aid_preferences_and_guide_dialogs_work},
      {"ui_complex_selection_draws_region_outline", ui_complex_selection_draws_region_outline},
      {"ui_ctrl_h_hides_selection_edges_without_blue_tint", ui_ctrl_h_hides_selection_edges_without_blue_tint},
      {"ui_select_inverse_and_extended_blend_modes_work", ui_select_inverse_and_extended_blend_modes_work},
      {"ui_selection_expand_contract_and_layer_transparency_work",
       ui_selection_expand_contract_and_layer_transparency_work},
      {"ui_ctrl_click_layer_loads_layer_transparency", ui_ctrl_click_layer_loads_layer_transparency},
      {"ui_select_grow_and_similar_use_magic_wand_tolerance",
       ui_select_grow_and_similar_use_magic_wand_tolerance},
      {"ui_complex_selection_stroke_uses_region_outline", ui_complex_selection_stroke_uses_region_outline},
      {"ui_layer_lock_transparency_and_keyboard_nudge_work", ui_layer_lock_transparency_and_keyboard_nudge_work},
      {"ui_layer_full_lock_row_control_blocks_edits_and_move",
       ui_layer_full_lock_row_control_blocks_edits_and_move},
      {"ui_folder_lock_inherits_to_child_layers", ui_folder_lock_inherits_to_child_layers},
      {"ui_move_auto_select_ignores_locked_layers", ui_move_auto_select_ignores_locked_layers},
      {"ui_lasso_selection_draws_freeform_region", ui_lasso_selection_draws_freeform_region},
      {"ui_copy_paste_and_transform_pasted_layer_work", ui_copy_paste_and_transform_pasted_layer_work},
      {"ui_external_clipboard_image_paste_creates_centered_layer",
       ui_external_clipboard_image_paste_creates_centered_layer},
      {"ui_external_clipboard_image_paste_overrides_internal_payload",
       ui_external_clipboard_image_paste_overrides_internal_payload},
      {"ui_free_transform_uses_opaque_pixel_bounds", ui_free_transform_uses_opaque_pixel_bounds},
      {"ui_transform_numeric_controls_apply_values", ui_transform_numeric_controls_apply_values},
      {"ui_transform_numeric_controls_accept_negative_scale",
       ui_transform_numeric_controls_accept_negative_scale},
      {"ui_transform_numeric_preview_renders_layer_styles",
       ui_transform_numeric_preview_renders_layer_styles},
      {"ui_move_show_transform_controls_click_shows_passive_transform",
       ui_move_show_transform_controls_click_shows_passive_transform},
      {"ui_transform_controls_finish_on_tool_layer_and_duplicate_changes",
       ui_transform_controls_finish_on_tool_layer_and_duplicate_changes},
      {"ui_layer_via_copy_and_cut_match_photoshop_shortcuts",
       ui_layer_via_copy_and_cut_match_photoshop_shortcuts},
      {"ui_layer_mask_from_selection_hides_pixels_and_shows_thumbnail",
       ui_layer_mask_from_selection_hides_pixels_and_shows_thumbnail},
      {"ui_layer_mask_target_paints_inverts_disables_and_applies",
       ui_layer_mask_target_paints_inverts_disables_and_applies},
      {"ui_layer_thumbnail_updates_after_brush_edit", ui_layer_thumbnail_updates_after_brush_edit},
      {"ui_layer_thumbnail_defers_brush_refresh_until_stroke_end",
       ui_layer_thumbnail_defers_brush_refresh_until_stroke_end},
      {"ui_cut_selection_clears_source_and_keeps_clipboard", ui_cut_selection_clears_source_and_keeps_clipboard},
      {"ui_brush_on_pasted_layer_expands_layer_bounds", ui_brush_on_pasted_layer_expands_layer_bounds},
      {"ui_brush_opacity_caps_per_stroke", ui_brush_opacity_caps_per_stroke},
      {"ui_layer_mask_brush_opacity_caps_per_stroke",
       ui_layer_mask_brush_opacity_caps_per_stroke},
      {"ui_shift_constrains_brush_and_eraser_strokes_to_axis",
       ui_shift_constrains_brush_and_eraser_strokes_to_axis},
      {"ui_shift_constrains_clone_stamp_strokes_to_axis",
       ui_shift_constrains_clone_stamp_strokes_to_axis},
      {"ui_one_pixel_brush_drag_paints_fractional_smoothed_line",
       ui_one_pixel_brush_drag_paints_fractional_smoothed_line},
      {"ui_one_pixel_brush_and_eraser_same_cell_drag_touches_one_pixel",
       ui_one_pixel_brush_and_eraser_same_cell_drag_touches_one_pixel},
      {"ui_max_zoom_brush_skips_noop_stroke_repaints",
       ui_max_zoom_brush_skips_noop_stroke_repaints},
      {"ui_deep_zoom_brush_repaint_stays_responsive",
       ui_deep_zoom_brush_repaint_stays_responsive},
      {"ui_airbrush_preset_does_not_stack_within_one_stroke",
       ui_airbrush_preset_does_not_stack_within_one_stroke},
      {"ui_airbrush_fast_strokes_ignore_mouse_event_density",
       ui_airbrush_fast_strokes_ignore_mouse_event_density},
      {"ui_airbrush_jittered_stroke_uses_smoothed_path", ui_airbrush_jittered_stroke_uses_smoothed_path},
      {"ui_clone_tool_samples_source_and_paints_offset", ui_clone_tool_samples_source_and_paints_offset},
      {"ui_clone_tool_feathered_rgba_edges_keep_source_color",
       ui_clone_tool_feathered_rgba_edges_keep_source_color},
      {"ui_smudge_tool_drags_painted_pixels", ui_smudge_tool_drags_painted_pixels},
      {"ui_copy_ignores_hidden_active_layer", ui_copy_ignores_hidden_active_layer},
      {"ui_copy_selected_layers_copies_composited_selection", ui_copy_selected_layers_copies_composited_selection},
      {"ui_eraser_on_background_reveals_transparency_and_size_cursor",
       ui_eraser_on_background_reveals_transparency_and_size_cursor},
      {"ui_magic_wand_cursor_marks_click_hotspot", ui_magic_wand_cursor_marks_click_hotspot},
      {"ui_move_tool_after_text_edit_keeps_spacebar_pan_active",
       ui_move_tool_after_text_edit_keeps_spacebar_pan_active},
      {"ui_text_tool_creates_visible_text_layer", ui_text_tool_creates_visible_text_layer},
      {"ui_text_tool_outside_click_commits_without_new_text_editor",
       ui_text_tool_outside_click_commits_without_new_text_editor},
      {"ui_text_edit_hides_editor_glyphs_and_shows_selection_over_style_preview",
       ui_text_edit_hides_editor_glyphs_and_shows_selection_over_style_preview},
      {"ui_expensive_text_style_preview_debounces_to_plain_live_text",
       ui_expensive_text_style_preview_debounces_to_plain_live_text},
      {"ui_text_editor_paste_uses_current_format_for_rich_emoji_clipboard",
       ui_text_editor_paste_uses_current_format_for_rich_emoji_clipboard},
      {"ui_text_tool_drag_creates_resizable_wrapped_text_box",
       ui_text_tool_drag_creates_resizable_wrapped_text_box},
      {"ui_imported_psd_text_uses_photoshop_frame_after_commit",
       ui_imported_psd_text_uses_photoshop_frame_after_commit},
      {"ui_imported_psd_point_text_reedit_uses_auto_width",
       ui_imported_psd_point_text_reedit_uses_auto_width},
      {"ui_imported_psd_point_text_baseline_origin_converts_in_place",
       ui_imported_psd_point_text_baseline_origin_converts_in_place},
      {"ui_imported_psd_mirrored_point_text_uses_local_bounds",
       ui_imported_psd_mirrored_point_text_uses_local_bounds},
      {"ui_imported_psd_raster_preview_warns_before_missing_font_substitution",
       ui_imported_psd_raster_preview_warns_before_missing_font_substitution},
      {"ui_transformed_text_reedit_preserves_transform",
       ui_transformed_text_reedit_preserves_transform},
      {"ui_transformed_expensive_text_preview_stays_transformed_while_typing",
       ui_transformed_expensive_text_preview_stays_transformed_while_typing},
      {"ui_text_edit_ctrl_t_commits_editor_before_free_transform",
       ui_text_edit_ctrl_t_commits_editor_before_free_transform},
      {"ui_text_free_transform_clicking_current_move_tool_applies",
       ui_text_free_transform_clicking_current_move_tool_applies},
      {"ui_text_box_commit_renders_paragraph_alignment", ui_text_box_commit_renders_paragraph_alignment},
      {"ui_text_tool_commits_rich_text_spans", ui_text_tool_commits_rich_text_spans},
      {"ui_text_options_follow_active_rich_text_span",
       ui_text_options_follow_active_rich_text_span},
      {"ui_qimage_import_export_preserves_alpha_and_formats", ui_qimage_import_export_preserves_alpha_and_formats},
      {"ui_qimage_import_export_writes_tiff_and_webp", ui_qimage_import_export_writes_tiff_and_webp},
      {"ui_image_save_options_write_bmp_alpha_and_jpeg_quality",
       ui_image_save_options_write_bmp_alpha_and_jpeg_quality},
      {"ui_image_save_options_defaults_and_dialogs", ui_image_save_options_defaults_and_dialogs},
      {"ui_qimage_multiply_uses_empty_backdrop_as_transparent",
       ui_qimage_multiply_uses_empty_backdrop_as_transparent},
      {"ui_print_layout_and_pdf_output_work", ui_print_layout_and_pdf_output_work},
      {"ui_print_dialog_exposes_printer_and_visible_checkboxes",
       ui_print_dialog_exposes_printer_and_visible_checkboxes},
      {"ui_dragged_image_file_opens_document_tab", ui_dragged_image_file_opens_document_tab},
      {"ui_reported_psd_open_shows_progress_dialog_if_available",
       ui_reported_psd_open_shows_progress_dialog_if_available},
      {"ui_qimage_render_respects_hidden_layer_groups", ui_qimage_render_respects_hidden_layer_groups},
      {"ui_qimage_region_render_matches_full_layer_styles",
       ui_qimage_region_render_matches_full_layer_styles},
      {"ui_image_adjustments_menu_applies_active_layer_filters",
       ui_image_adjustments_menu_applies_active_layer_filters},
      {"ui_image_adjustments_respect_active_selection", ui_image_adjustments_respect_active_selection},
      {"ui_direct_pixel_previews_preserve_floating_layer_bounds",
       ui_direct_pixel_previews_preserve_floating_layer_bounds},
      {"ui_levels_dialog_adjusts_selected_color_channel_on_transparent_layer",
       ui_levels_dialog_adjusts_selected_color_channel_on_transparent_layer},
      {"ui_levels_dialog_preserves_independent_channel_records",
       ui_levels_dialog_preserves_independent_channel_records},
      {"ui_hue_saturation_dialog_adjusts_selected_pixels", ui_hue_saturation_dialog_adjusts_selected_pixels},
      {"ui_hue_saturation_creates_masked_adjustment_layer", ui_hue_saturation_creates_masked_adjustment_layer},
      {"ui_adjustment_layer_thumbnails_show_type_symbols",
       ui_adjustment_layer_thumbnails_show_type_symbols},
      {"ui_levels_dialog_remaps_selected_tonal_range", ui_levels_dialog_remaps_selected_tonal_range},
      {"ui_curves_dialog_remaps_midtones_in_selection", ui_curves_dialog_remaps_midtones_in_selection},
      {"ui_color_balance_dialog_adjusts_selected_pixels", ui_color_balance_dialog_adjusts_selected_pixels},
      {"ui_gradient_and_magic_wand_render_visually", ui_gradient_and_magic_wand_render_visually},
      {"ui_radial_gradient_tool_renders_custom_transparency",
       ui_radial_gradient_tool_renders_custom_transparency},
      {"ui_magic_wand_contiguous_and_sample_all_layers_options_work",
       ui_magic_wand_contiguous_and_sample_all_layers_options_work},
      {"ui_magic_wand_sample_all_layers_clear_transparent_active_layer_is_noop",
       ui_magic_wand_sample_all_layers_clear_transparent_active_layer_is_noop},
      {"ui_magic_wand_complex_selection_is_responsive", ui_magic_wand_complex_selection_is_responsive},
      {"ui_bundled_legacy_plugin_action_applies_filter", ui_bundled_legacy_plugin_action_applies_filter},
      {"ui_transparency_checkerboard_and_copy_paste_preserve_alpha",
       ui_transparency_checkerboard_and_copy_paste_preserve_alpha},
      {"ui_crop_rotate_stroke_merge_and_filter_render_visually", ui_crop_rotate_stroke_merge_and_filter_render_visually},
      {"visual_contact_sheet_contains_new_feature_artifacts", visual_contact_sheet_contains_new_feature_artifacts},
  };

  std::string filter;
  const auto env_filter = qgetenv("PATCHY_UI_TEST_FILTER");
  if (!env_filter.isEmpty()) {
    filter = env_filter.toStdString();
  }
  if (argc > 1) {
    filter = argv[1];
  }

  int failures = 0;
  for (const auto& test : tests) {
    if (!filter.empty() && test.name.find(filter) == std::string::npos) {
      continue;
    }
    cleanup_after_visual_test();
    try {
      test.run();
      std::cout << "[PASS] " << test.name << std::endl;
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "[FAIL] " << test.name << ": " << error.what() << std::endl;
    }
    cleanup_after_visual_test();
  }

  return failures == 0 ? 0 : 1;
}
