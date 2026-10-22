#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QScrollBar>
#include <QWindow>


#include "ui/recent_file_list.hpp"
#include "ui/wayland_pointer_cursor.hpp"
#include "ui/wayland_popup_anchor.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/missing_fonts_dialog.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/theme_file.hpp"
#include "ui/theme_palette.hpp"
#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/update_checker.hpp"
#include "ui/update_installer.hpp"
#include "ui/connector_freshness.hpp"
#include "ui/ai_control_paths.hpp"
#include "ui/appimage_environment.hpp"
#include "ui/mcp_attachment.hpp"
#include "ui/qt_paths.hpp"
#include "unicode_path_names.hpp"
#include "synthetic_dng.hpp"
#include "test_harness.hpp"
#include <patchy_version.hpp>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QAbstractItemView>
#include <QComboBox>
#include <QTime>
#include <QTimeZone>
#include <QDialog>
#include <QEnterEvent>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QPixmap>
#include <QLineEdit>
#include <QList>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QSizeGrip>
#include <QMenu>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QIODevice>
#include <QPushButton>
#include <QStringList>
#include <QDateTime>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QAbstractButton>
#include <QUrl>
#include <QVariant>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <array>
#include <cstdint>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <initializer_list>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <map>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "app_shell_test_support.hpp"

namespace {
using namespace patchy::test::ui::app_shell_support;

struct GuiScaleDialogRun {
  bool saw_dialog{false};
  bool dismissed_message{false};
  int entry_percent{0};
};

// An APPDIR that contains this test binary, as the AppImage mount contains patchy.
QByteArray appdir_containing_this_binary();

// One wheel notch down, delivered as the window system delivers it. Qt hands only a
// spontaneous wheel on to the parent widgets (and gives WheelFocus widgets the focus first),
// and sendEvent clears that flag, so this goes through notify() the way QTest's key and
// mouse helpers do.
void send_wheel_notch_down(QWidget& target);

// Opens Preferences, records what the interface-scale spin box shows on entry, optionally
// types `percent` into it, and accepts. A changed scale raises a modal restart/reload notice
// that has to be dismissed before the dialog can finish closing.
GuiScaleDialogRun drive_preferences_gui_scale(patchy::ui::MainWindow& window,
                                              std::optional<int> percent);

// Drives the Preferences color-scheme combo and returns the dialog result, so the
// persist and cancel tests differ only in how they leave the dialog.
int choose_preferences_color_scheme(patchy::ui::MainWindow& window, const QString& token,
                                    bool accept);

QByteArray appdir_containing_this_binary() {
  auto dir = QDir(QCoreApplication::applicationDirPath());
  dir.cdUp();
  return dir.absolutePath().toUtf8();
}

void send_wheel_notch_down(QWidget& target) {
  const QPointF center(target.width() / 2.0, target.height() / 2.0);
  QWheelEvent wheel(center, target.mapToGlobal(center), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                    Qt::NoScrollPhase, false);
  QSpontaneKeyEvent::setSpontaneous(&wheel);
  qApp->notify(&target, &wheel);
}

GuiScaleDialogRun drive_preferences_gui_scale(patchy::ui::MainWindow& window,
                                              std::optional<int> percent) {
  GuiScaleDialogRun run;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* spin = dialog->findChild<QSpinBox*>(QStringLiteral("preferencesGuiScaleSpin"));
    CHECK(spin != nullptr);
    if (spin == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(spin->minimum() == patchy::ui::kMinGuiScalePercent);
    CHECK(spin->maximum() == patchy::ui::kMaxGuiScalePercent);
    CHECK(spin->singleStep() == 1);
    run.entry_percent = spin->value();
    run.saw_dialog = true;
    if (percent.has_value()) {
      // Typed the way a user enters an odd value.
      spin->selectAll();
      QTest::keyClicks(spin, QString::number(*percent));
      spin->interpretText();
      CHECK(spin->value() == *percent);
      // Visual record of the control for review.
      for (auto* parent = spin->parentWidget(); parent != nullptr; parent = parent->parentWidget()) {
        if (auto* scroll = qobject_cast<QScrollArea*>(parent)) {
          scroll->ensureWidgetVisible(spin);
          break;
        }
      }
      QApplication::processEvents();
      ensure_artifact_dir();
      dialog->grab().save(QStringLiteral("test-artifacts/preferences-gui-scale-%1.png").arg(*percent));
      QTimer::singleShot(0, [&] {
        auto* message = qobject_cast<QMessageBox*>(
            find_top_level_dialog(QStringLiteral("preferencesInterfaceScaleMessageBox")));
        CHECK(message != nullptr);
        if (message != nullptr) {
          message->accept();
          run.dismissed_message = true;
        }
      });
    }
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  return run;
}

int choose_preferences_color_scheme(patchy::ui::MainWindow& window, const QString& token,
                                    bool accept) {
  bool saw_dialog = false;
  int result = QDialog::Rejected;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesColorSchemeCombo"));
    CHECK(combo != nullptr);
    if (combo == nullptr) {
      dialog->reject();
      return;
    }
    const auto index = combo->findData(token);
    CHECK(index >= 0);
    combo->setCurrentIndex(index);
    saw_dialog = true;
    result = accept ? QDialog::Accepted : QDialog::Rejected;
    if (accept) {
      dialog->accept();
    } else {
      dialog->reject();
    }
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  return result;
}

// Scoped APPIMAGE/APPDIR for the AppImage tests: what the AppImage runtime sets.
class AppImageEnvironment {
 public:
  AppImageEnvironment(const QByteArray& appimage, const QByteArray& appdir)
      : previous_appimage_(qgetenv("APPIMAGE")), previous_appdir_(qgetenv("APPDIR")),
        had_appimage_(qEnvironmentVariableIsSet("APPIMAGE")), had_appdir_(qEnvironmentVariableIsSet("APPDIR")) {
    qputenv("APPIMAGE", appimage);
    qputenv("APPDIR", appdir);
  }
  ~AppImageEnvironment() {
    restore("APPIMAGE", had_appimage_, previous_appimage_);
    restore("APPDIR", had_appdir_, previous_appdir_);
  }
  AppImageEnvironment(const AppImageEnvironment&) = delete;
  AppImageEnvironment& operator=(const AppImageEnvironment&) = delete;

 private:
  static void restore(const char* name, bool had, const QByteArray& value) {
    if (had) {
      qputenv(name, value);
    } else {
      qunsetenv(name);
    }
  }
  QByteArray previous_appimage_;
  QByteArray previous_appdir_;
  bool had_appimage_;
  bool had_appdir_;
};


// A one-shot HTTP server on the loopback interface that answers every request
// with the given manifest body.
class ManifestServer {
 public:
  explicit ManifestServer(QByteArray body) : body_(std::move(body)) {
    CHECK(server_.listen(QHostAddress::LocalHost));
    QObject::connect(&server_, &QTcpServer::newConnection, &server_, [this] {
      while (auto* socket = server_.nextPendingConnection()) {
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
          if (!socket->readAll().contains("\r\n\r\n")) {
            return;
          }
          ++requests_;
          socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                        QByteArray::number(body_.size()) + "\r\n\r\n" + body_);
          socket->disconnectFromHost();
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      }
    });
  }
  [[nodiscard]] QString manifest_url() const {
    return QStringLiteral("http://127.0.0.1:%1/latest_version.json").arg(server_.serverPort());
  }
  [[nodiscard]] int requests() const { return requests_; }

 private:
  QTcpServer server_;
  QByteArray body_;
  int requests_{0};
};


// Scoped PATCHY_UPDATE_MANIFEST_URL override for the request tests below.
class UpdateManifestUrlOverride {
 public:
  explicit UpdateManifestUrlOverride(const QString& url) : previous_(qgetenv("PATCHY_UPDATE_MANIFEST_URL")) {
    qputenv("PATCHY_UPDATE_MANIFEST_URL", url.toUtf8());
  }
  ~UpdateManifestUrlOverride() {
    if (previous_.isEmpty()) {
      qunsetenv("PATCHY_UPDATE_MANIFEST_URL");
    } else {
      qputenv("PATCHY_UPDATE_MANIFEST_URL", previous_);
    }
  }
  UpdateManifestUrlOverride(const UpdateManifestUrlOverride&) = delete;
  UpdateManifestUrlOverride& operator=(const UpdateManifestUrlOverride&) = delete;

 private:
  QByteArray previous_;
};


// GitHub issue 100: Qt 6.11 on Wayland anchors menu bar menus, submenus, QPushButton menus
// and combo lists with an unscaled rectangle, so at a 90% or 125% interface scale they
// open off their item. The filter overrides the anchor through Qt's window property with
// the rectangle Qt computes, scaled. Offscreen runs the filter on any platform: the
// properties it leaves on each popup window are what the Wayland plug-in would read.
void ui_wayland_popup_anchor_fix_scales_qt_anchor_rects() {
  using patchy::ui::scale_popup_anchor_rect;
  using patchy::ui::wayland_popup_anchor_fix_needed;
  CHECK(wayland_popup_anchor_fix_needed(QStringLiteral("wayland"), QVersionNumber(6, 11, 2), 1.25));
  CHECK(wayland_popup_anchor_fix_needed(QStringLiteral("wayland-egl"), QVersionNumber(6, 11, 0), 0.9));
  CHECK(!wayland_popup_anchor_fix_needed(QStringLiteral("wayland"), QVersionNumber(6, 8, 3), 1.25));
  CHECK(!wayland_popup_anchor_fix_needed(QStringLiteral("wayland"), QVersionNumber(6, 12, 0), 1.25));
  CHECK(!wayland_popup_anchor_fix_needed(QStringLiteral("xcb"), QVersionNumber(6, 11, 2), 1.25));
  CHECK(!wayland_popup_anchor_fix_needed(QStringLiteral("wayland"), QVersionNumber(6, 11, 2), 1.0));
  // The rectangles Qt 6.11 sent unscaled in the probe runs (docs/wayland.md).
  CHECK(scale_popup_anchor_rect(QRect(0, 0, 127, 22), 1.25) == QRect(0, 0, 159, 28));
  CHECK(scale_popup_anchor_rect(QRect(41, 0, 46, 19), 0.9) == QRect(37, 0, 41, 17));

  constexpr double kScale = 1.25;
  const char* const property = "_q_waylandPopupAnchorRect";
  QMainWindow window;
  auto* central = new QWidget(&window);
  auto* combo = new QComboBox(central);
  combo->addItems({QStringLiteral("RGB Color"), QStringLiteral("Grayscale"), QStringLiteral("Bitmap")});
  combo->setGeometry(20, 30, 160, 24);
  auto* button = new QPushButton(QStringLiteral("More"), central);
  button->setGeometry(20, 80, 90, 24);
  auto* button_menu = new QMenu(button);
  button_menu->addAction(QStringLiteral("Delete"));
  button->setMenu(button_menu);
  window.setCentralWidget(central);
  window.menuBar()->addMenu(QStringLiteral("File"))->addAction(QStringLiteral("New"));
  QMenu* image = window.menuBar()->addMenu(QStringLiteral("Image"));
  QMenu* mode = image->addMenu(QStringLiteral("Mode"));
  mode->addAction(QStringLiteral("Grayscale"));
  image->addAction(QStringLiteral("Adjustments"));
  window.resize(640, 480);
  window.show();
  QCoreApplication::processEvents();

  QObject* filter = patchy::ui::create_wayland_popup_anchor_filter(kScale, &window);
  qApp->installEventFilter(filter);
  const auto anchor = [property](QWidget* popup) {
    return popup->windowHandle() != nullptr ? popup->windowHandle()->property(property) : QVariant();
  };

  // Menu bar menu: the item's rectangle in window coordinates.
  QMenuBar* bar = window.menuBar();
  bar->setActiveAction(image->menuAction());
  QCoreApplication::processEvents();
  CHECK(image->isVisible());
  const QRect bar_item = bar->actionGeometry(image->menuAction()).translated(bar->mapTo(&window, QPoint(0, 0)));
  CHECK(anchor(image).toRect() == scale_popup_anchor_rect(bar_item, kScale));

  // Submenu: its item's rectangle in the parent menu.
  image->setActiveAction(mode->menuAction());
  QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
  QCoreApplication::sendEvent(image, &right);
  QCoreApplication::processEvents();
  CHECK(mode->isVisible());
  CHECK(anchor(mode).toRect() == scale_popup_anchor_rect(image->actionGeometry(mode->menuAction()), kScale));
  mode->hide();
  image->hide();
  QCoreApplication::processEvents();

  // The same menu as a context menu: Qt sets no anchor there, so neither does the filter.
  mode->popup(window.mapToGlobal(QPoint(300, 200)));
  QCoreApplication::processEvents();
  CHECK(mode->isVisible());
  CHECK(!anchor(mode).isValid());
  mode->hide();

  // Combo box list: the combo's rectangle.
  combo->showPopup();
  QCoreApplication::processEvents();
  QWidget* list_popup = combo->view()->window();
  CHECK(list_popup != &window && list_popup->isVisible());
  CHECK(anchor(list_popup).toRect() ==
        scale_popup_anchor_rect(QRect(combo->mapTo(&window, QPoint(0, 0)), combo->size()), kScale));
  combo->hidePopup();

  // QPushButton menu (the layers panel's adjustment button): the button's rectangle.
  button->showMenu();
  QCoreApplication::processEvents();
  CHECK(button_menu->isVisible());
  CHECK(anchor(button_menu).toRect() ==
        scale_popup_anchor_rect(QRect(button->mapTo(&window, QPoint(0, 0)), button->size()), kScale));
  button_menu->close();

  qApp->removeEventFilter(filter);
}

// GitHub issue 215: before Qt 6.10 the Wayland plug-in applies a cursor change in any
// window to the pointer, so the canvas refreshing its tool cursor behind a dialog put the
// brush cursor over the dialog. The filter re-applies the cursor of the widget under the
// pointer to the window the pointer is in. Offscreen runs the filter on any platform and
// sends the Enter and Leave events Qt sends when the pointer crosses windows; the cursor
// each window holds is what the Wayland plug-in would show.
void ui_wayland_pointer_cursor_fix_keeps_the_dialog_cursor() {
  using patchy::ui::wayland_pointer_cursor_fix_needed;
  CHECK(wayland_pointer_cursor_fix_needed(QStringLiteral("wayland"), QVersionNumber(6, 8, 3)));
  CHECK(wayland_pointer_cursor_fix_needed(QStringLiteral("wayland-egl"), QVersionNumber(6, 9, 2)));
  CHECK(!wayland_pointer_cursor_fix_needed(QStringLiteral("wayland"), QVersionNumber(6, 10, 0)));
  CHECK(!wayland_pointer_cursor_fix_needed(QStringLiteral("wayland"), QVersionNumber(6, 11, 2)));
  CHECK(!wayland_pointer_cursor_fix_needed(QStringLiteral("xcb"), QVersionNumber(6, 8, 3)));
  CHECK(!wayland_pointer_cursor_fix_needed(QStringLiteral("windows"), QVersionNumber(6, 8, 3)));

  QWidget window;
  auto* canvas = new QWidget(&window);
  canvas->setGeometry(0, 0, 400, 300);
  window.resize(400, 300);
  QDialog dialog(&window);
  auto* field = new QLabel(QStringLiteral("Replace with"), &dialog);
  field->setGeometry(10, 10, 120, 24);
  field->setCursor(Qt::IBeamCursor);  // before the window exists, so nothing applies it yet
  auto* button = new QPushButton(QStringLiteral("Cancel"), &dialog);
  button->setGeometry(10, 50, 90, 24);
  dialog.resize(200, 100);
  window.show();
  dialog.show();
  QCoreApplication::processEvents();
  QWindow* window_handle = window.windowHandle();
  QWindow* dialog_handle = dialog.windowHandle();
  CHECK(window_handle != nullptr && dialog_handle != nullptr && window_handle != dialog_handle);

  QObject* filter = patchy::ui::create_wayland_pointer_cursor_filter(&window);
  qApp->installEventFilter(filter);
  const auto enter = [](std::initializer_list<QWidget*> path) {
    for (QWidget* widget : path) {
      QEnterEvent event(QPointF(5, 5), QPointF(5, 5), QPointF(5, 5));
      QCoreApplication::sendEvent(widget, &event);
    }
  };
  const auto leave = [](std::initializer_list<QWidget*> path) {
    for (QWidget* widget : path) {
      QEvent event(QEvent::Leave);
      QCoreApplication::sendEvent(widget, &event);
    }
  };

  // The pointer rests on the dialog's field; the canvas behind it refreshes its tool cursor.
  enter({&dialog, field});
  dialog_handle->unsetCursor();
  CHECK(dialog_handle->cursor().shape() == Qt::ArrowCursor);
  canvas->setCursor(Qt::CrossCursor);
  CHECK(dialog_handle->cursor().shape() == Qt::IBeamCursor);
  // Over the button, which sets no cursor of its own: the arrow, re-sent to the plug-in.
  leave({field});
  enter({button});
  QPixmap brush_tip(17, 17);
  brush_tip.fill(Qt::transparent);
  canvas->setCursor(QCursor(brush_tip, 8, 8));
  CHECK(dialog_handle->cursor().shape() == Qt::ArrowCursor);
  // A disabled widget shows the arrow even when it sets a cursor.
  leave({button});
  enter({field});
  field->setEnabled(false);
  dialog_handle->setCursor(Qt::IBeamCursor);
  canvas->setCursor(Qt::SizeAllCursor);
  CHECK(dialog_handle->cursor().shape() == Qt::ArrowCursor);
  field->setEnabled(true);

  // Back over the canvas: a change in the canvas's own window is Qt's to apply, while a
  // change inside the dialog no longer reaches the dialog's window.
  leave({field, &dialog});
  enter({&window, canvas});
  dialog_handle->unsetCursor();
  canvas->setCursor(Qt::OpenHandCursor);
  CHECK(dialog_handle->cursor().shape() == Qt::ArrowCursor);
  window_handle->unsetCursor();
  button->setCursor(Qt::WaitCursor);
  CHECK(window_handle->cursor().shape() == Qt::OpenHandCursor);

  // The pointer left the application: nothing to re-apply.
  leave({canvas, &window});
  window_handle->unsetCursor();
  button->setCursor(Qt::PointingHandCursor);
  CHECK(window_handle->cursor().shape() == Qt::ArrowCursor);

  qApp->removeEventFilter(filter);
}

// The issue 215 cases on the real dialogs: Missing Fonts and Preferences open modally over a
// document whose canvas shows the brush cursor, the pointer rests on the dialog, and the
// canvas refreshes its brush cursor behind it. Each dialog window starts from the leaked
// cursor and must end with its own arrow.
void ui_wayland_pointer_cursor_fix_covers_preferences_and_missing_fonts() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(30);
  QApplication::processEvents();
  QObject* filter = patchy::ui::create_wayland_pointer_cursor_filter(&window);
  qApp->installEventFilter(filter);

  int brush_size = 30;
  int dialogs_checked = 0;
  const auto check_dialog = [&](const QString& object_name, const QString& child_name) {
    auto* dialog = find_top_level_dialog(object_name);
    CHECK(dialog != nullptr && dialog->isModal());
    if (dialog == nullptr) {
      return;
    }
    auto* child = dialog->findChild<QWidget*>(child_name);
    CHECK(child != nullptr && child->isVisible());
    QWindow* handle = dialog->windowHandle();
    CHECK(handle != nullptr && handle != window.windowHandle());
    if (child != nullptr && handle != nullptr) {
      for (QWidget* widget : {static_cast<QWidget*>(dialog), child}) {
        QEnterEvent enter(QPointF(4, 4), QPointF(4, 4), QPointF(4, 4));
        QCoreApplication::sendEvent(widget, &enter);
      }
      handle->setCursor(Qt::CrossCursor);  // what the plug-in showed after the last leak
      brush_size += 7;
      canvas->set_brush_size(brush_size);
      CHECK(handle->cursor().shape() == Qt::ArrowCursor);
      ++dialogs_checked;
    }
    dialog->reject();
  };

  QTimer::singleShot(0, [&] {
    check_dialog(QStringLiteral("missingFontsDialog"), QStringLiteral("missingFontsIntroLabel"));
  });
  const std::vector<patchy::ui::MissingFontEntry> fonts{
      {QStringLiteral("Anydore"), false, 1, QStringLiteral("Noto Sans")}};
  static_cast<void>(patchy::ui::run_missing_fonts_dialog(&window, fonts, true));
  QApplication::processEvents();

  QTimer::singleShot(0, [&] {
    check_dialog(QStringLiteral("patchyPreferencesDialog"), QStringLiteral("preferencesTabWidget"));
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(dialogs_checked == 2);
  qApp->removeEventFilter(filter);
}

void ui_recent_history_checks_in_background_and_skips_network_paths() {
  ensure_artifact_dir();
  const auto live_file = QFileInfo(QStringLiteral("test-artifacts/recent-bg-live.png")).absoluteFilePath();
  const auto missing_file = QFileInfo(QStringLiteral("test-artifacts/recent-bg-missing.png")).absoluteFilePath();
  const auto live_folder = QFileInfo(QStringLiteral("test-artifacts/recent-bg-dir")).absoluteFilePath();
  const auto missing_folder = QFileInfo(QStringLiteral("test-artifacts/recent-bg-dir-missing")).absoluteFilePath();
  const auto network_file = QStringLiteral("//192.0.2.1/share/recent-bg.psd");
  const auto network_folder = QStringLiteral("//192.0.2.1/share");
  {
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(QColor(60, 120, 180));
    CHECK(image.save(live_file));
  }
  QFile::remove(missing_file);
  CHECK(QDir().mkpath(live_folder));
  remove_test_scratch_dir(missing_folder);

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  SettingsValueRestorer recent_folders_restorer(QStringLiteral("recentFolders"));
  const QStringList stored_files{network_file, missing_file, live_file};
  const QStringList stored_folders{network_folder, missing_folder, live_folder};
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), stored_files);
    settings.setValue(QStringLiteral("recentFolders"), stored_folders);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  auto* files_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  auto* folders_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentFolderMenu"));
  CHECK(files_menu != nullptr);
  CHECK(folders_menu != nullptr);
  auto* file_menu = qobject_cast<QMenu*>(files_menu->parent());
  CHECK(file_menu != nullptr);
  const auto listed = [](QMenu* menu) { return recent_menu_paths(*menu); };

  CHECK(process_events_until([&] {
    return listed(files_menu) == QStringList({network_file, live_file}) &&
           listed(folders_menu) == QStringList({network_folder, live_folder});
  }));

  // Opening the File menu rereads the lists without touching the disk.
  QElapsedTimer timer;
  timer.start();
  emit file_menu->aboutToShow();
  CHECK(timer.elapsed() < 1000);
  CHECK(listed(files_menu) == QStringList({network_file, live_file}));
  CHECK(listed(folders_menu) == QStringList({network_folder, live_folder}));

  // Hidden entries stay stored, so an unplugged drive's entries come back.
  auto settings = patchy::ui::app_settings();
  settings.sync();
  CHECK(settings.value(QStringLiteral("recentFiles")).toStringList() == stored_files);
  CHECK(settings.value(QStringLiteral("recentFolders")).toStringList() == stored_folders);
}

void ui_open_dialog_hides_name_filter_details() {
  patchy::ui::MainWindow window;
  show_window(window);

  // The Open dialog lists one row per format so every supported filetype is readable;
  // the all-formats row alone summarizes (its ~50 patterns overflow the native
  // dropdown). The dialog runs with HideNameFilterDetails and each row embeds its
  // visible patterns in the display name ("TIFF Image (*.tif *.tiff)"): a "*." token
  // must survive stripping or the Windows 11 dialog re-appends the full pattern spec.
  bool saw_open_dialog = false;
  QStringList filters;
  QStringList displayed;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("openFileDialog")));
    CHECK(dialog != nullptr);
    CHECK(dialog->testOption(QFileDialog::HideNameFilterDetails));
    filters = dialog->nameFilters();
    CHECK(!filters.isEmpty());
    CHECK(filters.first().contains(QStringLiteral("*.psd")));
    CHECK(filters.first().contains(QStringLiteral("*.tif")));
    CHECK(filters.first().contains(QStringLiteral("*.jxl")));
    auto* type_combo = dialog->findChild<QComboBox*>(QStringLiteral("fileTypeCombo"));
    CHECK(type_combo != nullptr);
    for (int i = 0; i < type_combo->count(); ++i) {
      displayed.push_back(type_combo->itemText(i));
    }
    CHECK(displayed.size() > 10);
    CHECK(displayed.first().contains(QStringLiteral("*.psd")));
    CHECK(!displayed.first().contains(QStringLiteral("*.tif")));
    CHECK(displayed.contains(QStringLiteral("Photoshop Document (*.psd *.psb)")));
    CHECK(displayed.contains(QStringLiteral("TIFF Image (*.tif *.tiff)")));
    CHECK(displayed.contains(QStringLiteral("JPEG XL Image (*.jxl)")));
    CHECK(displayed.contains(QStringLiteral("Affinity Document (*.af *.afphoto *.afdesign *.afpub)")));
    CHECK(displayed.last() == QStringLiteral("All Files (*.*)"));
    saw_open_dialog = true;
    dialog->reject();
  });
  require_action(window, "fileOpenAction")->trigger();
  CHECK(saw_open_dialog);
  // Checked outside the dialog callback: a failing CHECK throws, and a throw inside the
  // event loop crashes the suite instead of reporting.
  CHECK(displayed.contains(QStringLiteral("Portable Anymap Image (*.pbm *.pgm *.ppm *.pnm)")));
  // A row with many extensions shows a short hint, never the whole list: the Camera Raw
  // row's 27 patterns made GNOME's type dropdown as wide as the screen and hid its
  // scrollbar at the far edge, so the rows below the tenth looked absent (GitHub issue
  // 270). The machine-read spec behind the hint still carries every extension.
  int raw_rows = 0;
  for (int i = 0; i < displayed.size(); ++i) {
    CHECK(displayed[i].size() <= 72);
    if (!displayed[i].startsWith(QStringLiteral("Camera Raw Image"))) {
      continue;
    }
    ++raw_rows;
    CHECK(displayed[i] == QStringLiteral("Camera Raw Image (*.dng *.cr2 *.cr3 *.crw *.nef *.nrw and more)"));
    CHECK(i < filters.size());
    CHECK(filters[i].startsWith(displayed[i]));
    CHECK(filters[i].contains(QStringLiteral("*.x3f")));
    CHECK(filters[i].endsWith(QStringLiteral("*.x3f)")));
  }
  CHECK(raw_rows == 1);

  // Save As filters are one short entry per format; their extension details stay visible.
  bool saw_save_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    CHECK(!dialog->testOption(QFileDialog::HideNameFilterDetails));
    saw_save_dialog = true;
    dialog->reject();
  });
  require_action(window, "fileSaveAsAction")->trigger();
  CHECK(saw_save_dialog);
}

// A cancelled File > Open still remembers the folder it was showing, so the next Open
// starts there (GitHub issue 183).
void ui_open_dialog_cancel_remembers_the_folder() {
  ensure_artifact_dir();
  SettingsValueRestorer last_open_directory_restorer(QStringLiteral("lastOpenDirectory"));
  const auto start = QFileInfo(QStringLiteral("test-artifacts")).absoluteFilePath();
  const auto browsed = start + QStringLiteral("/open-cancel-folder");
  CHECK(QDir().mkpath(browsed));
  patchy::ui::app_settings().setValue(QStringLiteral("lastOpenDirectory"), start);

  patchy::ui::MainWindow window;
  show_window(window);
  const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);
  bool answered = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("openFileDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    dialog->setDirectory(browsed);
    QApplication::processEvents();
    answered = true;
    dialog->reject();
  });
  require_action(window, "fileOpenAction")->trigger();
  QApplication::processEvents();
  CHECK(answered);
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before);
  auto settings = patchy::ui::app_settings();
  settings.sync();
  CHECK(QFileInfo(settings.value(QStringLiteral("lastOpenDirectory")).toString()).absoluteFilePath() ==
        QFileInfo(browsed).absoluteFilePath());
}

void ui_open_dialog_opens_every_selected_file() {
  // File > Open is multi-select: every picked file opens as its own document in
  // dialog order and the last one ends up active, the same loop a multi-file
  // drop runs.
  ensure_artifact_dir();
  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  SettingsValueRestorer recent_folders_restorer(QStringLiteral("recentFolders"));
  SettingsValueRestorer last_open_directory_restorer(QStringLiteral("lastOpenDirectory"));
  const auto dir = QFileInfo(QStringLiteral("test-artifacts")).absoluteFilePath();
  const QStringList names{QStringLiteral("open-multi-a.png"), QStringLiteral("open-multi-b.png")};
  QStringList paths;
  for (const auto& name : names) {
    QImage image(48, 32, QImage::Format_RGB32);
    image.fill(QColor(90, 150, 210));
    paths.push_back(dir + QLatin1Char('/') + name);
    CHECK(image.save(paths.back()));
  }

  patchy::ui::MainWindow window;
  show_window(window);
  const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);
  QStringList picked;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("openFileDialog")));
    CHECK(dialog != nullptr);
    CHECK(dialog->fileMode() == QFileDialog::ExistingFiles);
    dialog->setDirectory(dir);
    // Qt's typed form for several files: "a" "b" (the widget dialog parses the quotes).
    auto* name_edit = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
    CHECK(name_edit != nullptr);
    name_edit->setText(QStringLiteral("\"%1\" \"%2\"").arg(names[0], names[1]));
    QApplication::processEvents();
    picked = dialog->selectedFiles();
    static_cast<QDialog*>(dialog)->accept();
  });
  require_action(window, "fileOpenAction")->trigger();
  QApplication::processEvents();

  CHECK(picked.size() == 2);
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 2);
  if (!picked.isEmpty()) {
    CHECK(QFileInfo(patchy::ui::MainWindowTestAccess::active_session_path(window)).absoluteFilePath() ==
          QFileInfo(picked.last()).absoluteFilePath());
  }
  // Every opened file went through the recent-files bookkeeping, not only the last.
  QStringList recent;
  {
    auto settings = patchy::ui::app_settings();
    settings.sync();
    for (const auto& entry : settings.value(QStringLiteral("recentFiles")).toStringList()) {
      recent.push_back(QFileInfo(entry).absoluteFilePath());
    }
  }
  for (const auto& path : paths) {
    CHECK(recent.contains(QFileInfo(path).absoluteFilePath()));
  }
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
  // A manifest that does carry a linux entry parses for the linux platform id.
  const QByteArray linux_manifest = R"({
    "platforms": {
      "linux": {
        "version": "0.2",
        "download_url": "https://rtsoft.com/patchy/Patchy.flatpak"
      }
    }
  })";
  const auto linux_update =
      patchy::ui::parse_update_manifest(linux_manifest, QStringLiteral("linux"), QStringLiteral("0.1.0"));
  CHECK(linux_update.has_value());
  CHECK(linux_update->platform == QStringLiteral("linux"));
  CHECK(linux_update->download_url == QUrl(QStringLiteral("https://rtsoft.com/patchy/Patchy.flatpak")));
  CHECK(linux_update->appimage_url.isEmpty());
  // The optional appimage_url (a permanent manifest key) is what an AppImage copy downloads;
  // a malformed one is dropped and the update is still offered.
  const auto with_appimage_url = [](const QString& value) {
    return patchy::ui::parse_update_manifest(QStringLiteral(R"({"platforms": {"linux": {"version": "0.2",
        "download_url": "https://rtsoft.com/patchy/Patchy.flatpak", "appimage_url": %1}}})")
                                                 .arg(value)
                                                 .toUtf8(),
                                             QStringLiteral("linux"), QStringLiteral("0.1.0"));
  };
  const auto appimage_update = with_appimage_url(QStringLiteral("\"https://rtsoft.com/patchy/Patchy.AppImage\""));
  CHECK(appimage_update.has_value());
  CHECK(appimage_update->appimage_url == QUrl(QStringLiteral("https://rtsoft.com/patchy/Patchy.AppImage")));
  const auto bad_appimage_update = with_appimage_url(QStringLiteral("\"not a url\""));
  CHECK(bad_appimage_update.has_value() && bad_appimage_update->appimage_url.isEmpty());

  // The AppImage an AppImage copy installs by itself (permanent keys appimage_install_url,
  // _size, _sha256 and appimage_min_version): all three or none, https, and only from this
  // repository's GitHub releases, since no publisher signature backs it.
  const QString appimage_sha(64, QLatin1Char('c'));
  const auto with_appimage_install = [&appimage_sha](const QString& url, const QString& extra = {}) {
    return patchy::ui::parse_update_manifest(
        QStringLiteral(R"({"platforms": {"linux": {"version": "0.2", "download_url": "https://rtsoft.com/p.flatpak",
            "appimage_install_url": "%1", "appimage_install_size": 1000, "appimage_install_sha256": "%2"%3}}})")
            .arg(url, appimage_sha, extra)
            .toUtf8(),
        QStringLiteral("linux"), QStringLiteral("0.1.0"));
  };
  const auto pinned_release =
      QStringLiteral("https://github.com/SethRobinson/Patchy/releases/download/v0.2/PatchyLinux.AppImage");
  const auto installable = with_appimage_install(pinned_release);
  CHECK(installable.has_value() && patchy::ui::update_has_appimage_install(*installable));
  CHECK(installable.has_value() && installable->appimage_install_url == QUrl(pinned_release));
  CHECK(installable.has_value() && installable->appimage_install_size == 1000);
  CHECK(installable.has_value() && installable->appimage_install_sha256 == appimage_sha.toLatin1());
  CHECK(installable.has_value() && !patchy::ui::update_has_installer(*installable));
  const auto elsewhere = with_appimage_install(QStringLiteral("https://example.com/PatchyLinux.AppImage"));
  CHECK(elsewhere.has_value() && !patchy::ui::update_has_appimage_install(*elsewhere));
  const auto other_repo = with_appimage_install(
      QStringLiteral("https://github.com/someone/Patchy/releases/download/v0.2/PatchyLinux.AppImage"));
  CHECK(other_repo.has_value() && !patchy::ui::update_has_appimage_install(*other_repo));
  const auto switched_off = with_appimage_install(pinned_release, QStringLiteral(R"(, "appimage_min_version": "0.2")"));
  CHECK(switched_off.has_value() && !patchy::ui::update_has_appimage_install(*switched_off));
  const auto still_on = with_appimage_install(pinned_release, QStringLiteral(R"(, "appimage_min_version": "0.1")"));
  CHECK(still_on.has_value() && patchy::ui::update_has_appimage_install(*still_on));
  CHECK(linux_update.has_value() && !patchy::ui::update_has_appimage_install(*linux_update));
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

  // The one-click installer fields (docs/auto-update.md) are all or nothing: an entry
  // without them, with one missing, with a non-https URL or with a malformed hash is still
  // an update, offered as a browser download.
  CHECK(!patchy::ui::update_has_installer(*update));
  const auto installer_manifest = [](const QString& extra_fields) {
    return QStringLiteral(R"({"platforms": {"windows": {"version": "0.2",
        "download_url": "https://rtsoft.com/patchy/PatchyWindowsInstaller.exe"%1}}})")
        .arg(extra_fields)
        .toUtf8();
  };
  const auto sha = QStringLiteral("0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef");
  const auto with_installer = patchy::ui::parse_update_manifest(
      installer_manifest(QStringLiteral(R"(, "installer_url": "https://rtsoft.com/v0.2/PatchyWindowsInstaller.exe",
                                           "installer_size": 123456789, "installer_sha256": "%1")")
                             .arg(sha)),
      QStringLiteral("windows"), QStringLiteral("0.1.0"));
  CHECK(with_installer.has_value());
  if (with_installer.has_value()) {
    CHECK(patchy::ui::update_has_installer(*with_installer));
    CHECK(with_installer->installer_url == QUrl(QStringLiteral("https://rtsoft.com/v0.2/PatchyWindowsInstaller.exe")));
    CHECK(with_installer->installer_size == 123456789);
    CHECK(with_installer->installer_sha256 == sha.toLower().toLatin1());
    CHECK(with_installer->download_url == QUrl(QStringLiteral("https://rtsoft.com/patchy/PatchyWindowsInstaller.exe")));
  }
  for (const auto& fields : {
           QStringLiteral(R"(, "installer_url": "https://rtsoft.com/i.exe", "installer_size": 10)"),
           QStringLiteral(R"(, "installer_url": "https://rtsoft.com/i.exe", "installer_sha256": "%1")").arg(sha),
           QStringLiteral(R"(, "installer_size": 10, "installer_sha256": "%1")").arg(sha),
           QStringLiteral(R"(, "installer_url": "http://rtsoft.com/i.exe", "installer_size": 10,
                             "installer_sha256": "%1")").arg(sha),
           QStringLiteral(R"(, "installer_url": "https://rtsoft.com/i.exe", "installer_size": 0,
                             "installer_sha256": "%1")").arg(sha),
           QStringLiteral(R"(, "installer_url": "https://rtsoft.com/i.exe", "installer_size": 10,
                             "installer_sha256": "not-a-hash")"),
       }) {
    const auto partial =
        patchy::ui::parse_update_manifest(installer_manifest(fields), QStringLiteral("windows"), QStringLiteral("0.1.0"));
    CHECK(partial.has_value());
    CHECK(partial.has_value() && !patchy::ui::update_has_installer(*partial));
  }
  // The remote off switch: installer_min_version keeps copies older than it on the browser
  // download (a release whose self-update is broken), while newer ones still install by
  // themselves. A value that does not parse switches self-install off for everyone.
  const auto with_min_version = [&](const QString& min_version, const QString& running) {
    const auto parsed = patchy::ui::parse_update_manifest(
        installer_manifest(QStringLiteral(R"(, "installer_url": "https://rtsoft.com/v9/PatchyWindowsInstaller.exe",
                                             "installer_size": 10, "installer_sha256": "%1",
                                             "installer_min_version": %2)")
                               .arg(sha, min_version))
            .replace("\"version\": \"0.2\"", "\"version\": \"9.0\""),
        QStringLiteral("windows"), running);
    CHECK(parsed.has_value());  // still an update, whatever the switch says
    return parsed.has_value() && patchy::ui::update_has_installer(*parsed);
  };
  CHECK(!with_min_version(QStringLiteral("\"1.11\""), QStringLiteral("1.10")));
  CHECK(!with_min_version(QStringLiteral("\"1.11\""), QStringLiteral("1.9")));
  CHECK(with_min_version(QStringLiteral("\"1.11\""), QStringLiteral("1.11")));
  CHECK(with_min_version(QStringLiteral("\"1.11\""), QStringLiteral("1.12")));
  CHECK(with_min_version(QStringLiteral("\"1.10\""), QStringLiteral("1.10")));
  CHECK(!with_min_version(QStringLiteral("\"never\""), QStringLiteral("1.10")));
  CHECK(!with_min_version(QStringLiteral("\"\""), QStringLiteral("1.10")));
  CHECK(!with_min_version(QStringLiteral("111"), QStringLiteral("1.10")));  // a number, not a version string

  // A manifest chosen through PATCHY_UPDATE_MANIFEST_URL may name an http installer (a
  // local trial of the whole update); the shipped manifest never can (the fourth case above).
  {
    const auto previous = qgetenv("PATCHY_UPDATE_MANIFEST_URL");
    qputenv("PATCHY_UPDATE_MANIFEST_URL", "http://127.0.0.1:1/latest_version.json");
    const auto local = patchy::ui::parse_update_manifest(
        installer_manifest(QStringLiteral(R"(, "installer_url": "http://127.0.0.1:1/i.exe", "installer_size": 10,
                                             "installer_sha256": "%1")")
                               .arg(sha)),
        QStringLiteral("windows"), QStringLiteral("0.1.0"));
    if (previous.isEmpty()) {
      qunsetenv("PATCHY_UPDATE_MANIFEST_URL");
    } else {
      qputenv("PATCHY_UPDATE_MANIFEST_URL", previous);
    }
    CHECK(local.has_value() && patchy::ui::update_has_installer(*local));
  }
}

// main() waits a bounded time for tracked workers at quit and force-exits when
// one is still blocked in the OS: the timed wait must report a running worker,
// then succeed once it finishes, and the count must balance.
void ui_background_worker_wait_is_bounded() {
  std::atomic<bool> release{false};
  std::atomic<bool> finished{false};
  const int before = patchy::ui::tracked_background_worker_count();
  patchy::ui::run_tracked_background_worker([&release, &finished] {
    while (!release.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    finished = true;
  });
  if (patchy::ui::kBackgroundWorkRunsInline) {
    // Ran inline (single-threaded wasm): the worker could not have blocked.
    CHECK(patchy::ui::wait_for_tracked_background_workers(std::chrono::milliseconds(0)));
    return;
  }
  CHECK(patchy::ui::tracked_background_worker_count() == before + 1);
  QElapsedTimer timer;
  timer.start();
  CHECK(!patchy::ui::wait_for_tracked_background_workers(std::chrono::milliseconds(150)));
  CHECK(timer.elapsed() >= 100);
  CHECK(!finished.load());
  release = true;
  CHECK(patchy::ui::wait_for_tracked_background_workers(std::chrono::seconds(10)));
  CHECK(finished.load());
  CHECK(patchy::ui::tracked_background_worker_count() == before);
}

void ui_update_manifest_url_honors_environment_override() {
  {
    const UpdateManifestUrlOverride override(QStringLiteral("http://127.0.0.1:1/custom.json"));
    CHECK(patchy::ui::update_manifest_url() == QUrl(QStringLiteral("http://127.0.0.1:1/custom.json")));
  }
  CHECK(patchy::ui::update_manifest_url().host() == QStringLiteral("raw.githubusercontent.com"));
}

// The request resolves its host on a detached thread and only then talks to the
// server (GitHub issue 48): the whole path, resolver included, must deliver the
// manifest to the owner's thread.
void ui_update_check_fetches_manifest_after_resolving_host() {
  ManifestServer server(R"({"platforms": {"windows": {"version": "9.9", "download_url": "https://rtsoft.com/w.exe"},
                                          "macos": {"version": "9.9", "download_url": "https://rtsoft.com/m.dmg"},
                                          "linux": {"version": "9.9", "download_url": "https://rtsoft.com/l.flatpak"}}})");
  const UpdateManifestUrlOverride override(server.manifest_url());
  QObject owner;
  std::optional<patchy::ui::UpdateCheckResult> result;
  patchy::ui::request_update_check(&owner, QStringLiteral("0.1.0"),
                                   [&result](patchy::ui::UpdateCheckResult value) { result = std::move(value); });
  CHECK(process_events_until([&] { return result.has_value(); }, 15000));
  CHECK(result.has_value());
  if (result.has_value()) {
    CHECK(result->status == patchy::ui::UpdateCheckStatus::UpdateAvailable);
    CHECK(result->latest_version == QStringLiteral("9.9"));
  }
  CHECK(server.requests() == 1);
}

// An unresolvable host fails the check without a request, and an owner destroyed
// while the resolver is still out never hears back (no dangling post at quit).
void ui_update_check_reports_unresolvable_host_and_drops_destroyed_owner() {
  const UpdateManifestUrlOverride override(QStringLiteral("https://patchy-no-such-host.invalid/latest_version.json"));
  {
    QObject owner;
    std::optional<patchy::ui::UpdateCheckResult> result;
    patchy::ui::request_update_check(&owner, QStringLiteral("0.1.0"),
                                     [&result](patchy::ui::UpdateCheckResult value) { result = std::move(value); });
    CHECK(process_events_until([&] { return result.has_value(); }, 15000));
    if (result.has_value()) {
      CHECK(result->status == patchy::ui::UpdateCheckStatus::NetworkError);
      CHECK(result->http_status == 0);
    }
  }
  {
    bool called = false;
    {
      QObject owner;
      patchy::ui::request_update_check(&owner, QStringLiteral("0.1.0"),
                                       [&called](patchy::ui::UpdateCheckResult) { called = true; });
    }
    process_events_for(500);
    CHECK(!called);
  }
}

void ui_update_available_dialog_warns_to_close_patchy_before_installing() {
  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("updateAvailableMessageBox")));
    CHECK(dialog != nullptr);
    // The install advice is per-platform (installer exe / DMG / Flatpak update).
#if defined(Q_OS_MACOS)
    CHECK(dialog->text().contains(QStringLiteral("drag the new Patchy into Applications")));
#elif defined(Q_OS_LINUX)
    // Installs update from the Patchy Flatpak repository (GitHub issue 28), so the
    // advice is one flatpak command: no bundle download, and --user like every
    // documented install command (no root, GitHub issue 14).
    CHECK(dialog->text().contains(QStringLiteral("flatpak update --user -y com.rtsoft.patchy")));
    CHECK(!dialog->text().contains(QStringLiteral("curl ")));
    CHECK(dialog->findChild<QAbstractButton*>(QStringLiteral("updateCopyCommandButton")) != nullptr);
#else
    CHECK(dialog->text().contains(
        QStringLiteral("Save your work and close Patchy before running the installer.")));
#endif
    // No pinned installer in this update, and the suite is not an installed copy.
    CHECK(dialog->findChild<QAbstractButton*>(QStringLiteral("updateInstallNowButton")) == nullptr);
    saw_dialog = true;
    dialog->reject();
  });

  // Each platform gets its real download URL (the GitHub latest-release permalink that
  // latest_version.json carries), which is what the Download button opens.
#if defined(Q_OS_LINUX)
  const QUrl download_url(
      QStringLiteral("https://github.com/SethRobinson/Patchy/releases/latest/download/PatchyLinux.flatpak"));
#else
  const QUrl download_url(
      QStringLiteral("https://github.com/SethRobinson/Patchy/releases/latest/download/PatchyWindowsInstaller.exe"));
#endif
  window.show_update_available({QStringLiteral("windows"), QStringLiteral("9.9"), download_url});
  CHECK(saw_dialog);
}

// Only a copy the Windows installer made updates itself: the uninstall entry's
// InstallLocation must name the folder the app runs from.
void update_install_location_matching_ignores_case_and_separators() {
  using patchy::ui::install_location_matches;
  CHECK(install_location_matches(QStringLiteral("C:\\Users\\Me\\AppData\\Local\\Programs\\Patchy"),
                                 QStringLiteral("C:/Users/Me/AppData/Local/Programs/Patchy")));
  CHECK(install_location_matches(QStringLiteral("c:\\apps\\patchy\\"), QStringLiteral("C:/Apps/Patchy")));
  CHECK(!install_location_matches(QStringLiteral("C:\\Apps\\Patchy"), QStringLiteral("C:/Apps/Patchy 2")));
  CHECK(!install_location_matches(QStringLiteral("C:\\Apps"), QStringLiteral("C:/Apps/Patchy")));
  CHECK(!install_location_matches(QString(), QStringLiteral("C:/Apps/Patchy")));
  CHECK(!install_location_matches(QStringLiteral("C:\\Apps\\Patchy"), QString()));
  // The suites never run from an installed copy, so they never start a real update.
  CHECK(!patchy::ui::installed_copy_can_self_update());
  CHECK(patchy::ui::update_download_folder(QStringLiteral("1.10")).endsWith(QStringLiteral("/patchy-update-1.10")));
  CHECK(patchy::ui::update_download_folder(QStringLiteral("..\\1.10/x")).endsWith(QStringLiteral("/patchy-update-..1.10")));
}

// One-click update (docs/auto-update.md), Windows only: an installed copy offered an update
// with a pinned installer gets "Update Now"; without either, the browser download stays.
void ui_update_available_dialog_offers_one_click_update_for_installed_copy() {
#if defined(Q_OS_WIN)
  patchy::ui::MainWindow window;
  show_window(window);

  patchy::ui::UpdateInfo update{QStringLiteral("windows"), QStringLiteral("9.9"),
                                QUrl(QStringLiteral("https://rtsoft.com/patchy/PatchyWindowsInstaller.exe"))};
  update.installer_url = QUrl(QStringLiteral("https://rtsoft.com/patchy/v9.9/PatchyWindowsInstaller.exe"));
  update.installer_size = 1000;
  update.installer_sha256 = QByteArray(64, 'a');

  const auto shown_with_update_now = [&window](const patchy::ui::UpdateInfo& info, QString* text) {
    bool saw_dialog = false;
    bool update_now = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("updateAvailableMessageBox")));
      CHECK(dialog != nullptr);
      auto* button = dialog->findChild<QAbstractButton*>(QStringLiteral("updateInstallNowButton"));
      update_now = button != nullptr;
      if (button != nullptr) {
        CHECK(dialog->defaultButton() == button);
      }
      if (text != nullptr) {
        *text = dialog->text();
      }
      saw_dialog = true;
      dialog->reject();  // never an accept: that would start the download
    });
    window.show_update_available(info);
    CHECK(saw_dialog);
    return update_now;
  };

  QString text;
  patchy::ui::MainWindowTestAccess::set_self_update_override(window, true);
  CHECK(shown_with_update_now(update, &text));
  CHECK(text.contains(QStringLiteral("Patchy can download the update, close, install it, and start again.")));
  CHECK(!text.contains(QStringLiteral("before running the installer")));

  // The portable zip or a development build: the browser download.
  patchy::ui::MainWindowTestAccess::set_self_update_override(window, false);
  CHECK(!shown_with_update_now(update, &text));
  CHECK(text.contains(QStringLiteral("before running the installer")));

  // An installed copy and a manifest without the installer fields: the same.
  patchy::ui::MainWindowTestAccess::set_self_update_override(window, true);
  CHECK(!shown_with_update_now({QStringLiteral("windows"), QStringLiteral("9.9"), update.download_url}, nullptr));
  CHECK(!patchy::ui::MainWindowTestAccess::update_install_pending(window));
#else
  patchy::test::skip("Windows only (one-click update)");
#endif
}

// After a one-click update the installer hands Patchy the AI apps still running the
// previous connector; the note names them and the version now installed.
void ui_update_connector_restart_notice_names_the_apps() {
  patchy::ui::MainWindow window;
  show_window(window);
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog =
        qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("updateConnectorNoticeMessageBox")));
    CHECK(dialog != nullptr);
    if (dialog != nullptr) {
      CHECK(dialog->text().contains(QStringLiteral("Claude, Codex")));
      CHECK(dialog->text().contains(QStringLiteral(PATCHY_VERSION)));
      CHECK(dialog->text().contains(QStringLiteral("Restart them")));
      saw_dialog = true;
      dialog->accept();
    }
  });
  window.show_connector_restart_notice({QStringLiteral("Claude"), QStringLiteral("Codex")});
  CHECK(saw_dialog);
  // Nothing to name: no dialog at all.
  window.show_connector_restart_notice({});
  CHECK(find_top_level_dialog(QStringLiteral("updateConnectorNoticeMessageBox")) == nullptr);
}

// A connector an update left behind notices that another file now sits at its path
// (ui/connector_freshness.hpp); one that is still current adds nothing to its replies.
void connector_update_notice_reports_a_replaced_executable() {
  QTemporaryDir dir;
  CHECK(dir.isValid());
  const auto path = dir.filePath(QStringLiteral("patchy-mcp.exe"));
  const auto write = [&path](const QByteArray& bytes) {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size());
  };
  write("version one");
  const patchy::ui::ReplacedFileWatch watch(path);
  CHECK(!watch.replaced());
  // The update: the running file is renamed aside and a new one takes its name.
  CHECK(QFile::rename(path, path + QStringLiteral(".old-20260101000000")));
  CHECK(watch.replaced());
  write("version two, a different size");
  CHECK(watch.replaced());
  // A path with no file at the start never reports a replacement.
  const patchy::ui::ReplacedFileWatch absent(dir.filePath(QStringLiteral("absent.exe")));
  CHECK(!absent.replaced());

  // This test binary has not been replaced: no notice, and replies stay as they were.
  patchy::ui::watch_running_executable();
  CHECK(patchy::ui::connector_update_notice().isEmpty());
  QJsonObject structured{{QStringLiteral("version"), QStringLiteral("1")}};
  QJsonArray content;
  CHECK(!patchy::ui::add_connector_update_notice(structured, content));
  CHECK(structured.size() == 1 && content.isEmpty());

  // The connector's own update check words its finding for the assistant; nothing is
  // reported until a check has found a newer version.
  CHECK(patchy::ui::connector_update_available().isEmpty());
  const auto available = patchy::ui::connector_update_available_message(QStringLiteral("9.9"));
  CHECK(available.contains(QStringLiteral("Patchy 9.9 is available")));
  // The address is fixed in the code: nothing a manifest says reaches the assistant as text.
  CHECK(available.contains(QStringLiteral("https://github.com/SethRobinson/Patchy/releases/latest")));
  CHECK(available.contains(QStringLiteral("open the Patchy app")));
}

// An AppImage runs from a new /tmp/.mount_* directory each launch (docs/appimage.md), so
// the user-visible path is the .AppImage file, and only when this executable really lives
// in the mount: an AppImage'd program that starts a native Patchy leaves its own APPIMAGE
// and APPDIR behind. The attach socket then stops depending on the mount, or the user's
// Patchy and an assistant's `<file> patchy-mcp --attach` would never meet.
void appimage_environment_names_the_appimage_file() {
  CHECK(patchy::ui::appimage_file_path().isEmpty());
  CHECK(patchy::ui::user_visible_executable_path() == QCoreApplication::applicationFilePath());
  const auto plain_endpoint = patchy::ui::mcp_attachment_endpoint();
  {
    const AppImageEnvironment env("/home/someone/Apps/PatchyLinux.AppImage", appdir_containing_this_binary());
#ifdef Q_OS_LINUX
    CHECK(patchy::ui::appimage_file_path() == QStringLiteral("/home/someone/Apps/PatchyLinux.AppImage"));
    CHECK(patchy::ui::user_visible_executable_path() == QStringLiteral("/home/someone/Apps/PatchyLinux.AppImage"));
    const auto identity = (QDir::homePath() + QStringLiteral("\nappimage")).toUtf8();
    CHECK(patchy::ui::mcp_attachment_endpoint() ==
          QStringLiteral("PatchyMcp-") +
              QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(32)));
    CHECK(patchy::ui::mcp_attachment_endpoint() != plain_endpoint);
    patchy::ui::AiControlPaths paths = patchy::ui::resolve_ai_control_paths();
    CHECK(paths.appimage_path == QStringLiteral("/home/someone/Apps/PatchyLinux.AppImage"));
#else
    // Windows and macOS have no AppImages; a stray APPIMAGE changes nothing.
    CHECK(patchy::ui::appimage_file_path().isEmpty());
    CHECK(patchy::ui::mcp_attachment_endpoint() == plain_endpoint);
#endif
  }
  {
    // Another program's AppImage: this binary is not under its APPDIR.
    QTemporaryDir elsewhere;
    CHECK(elsewhere.isValid());
    const AppImageEnvironment env("/home/someone/Apps/Other.AppImage", elsewhere.path().toUtf8());
    CHECK(patchy::ui::appimage_file_path().isEmpty());
    CHECK(patchy::ui::mcp_attachment_endpoint() == plain_endpoint);
  }
  {
    const AppImageEnvironment env("relative/PatchyLinux.AppImage", appdir_containing_this_binary());
    CHECK(patchy::ui::appimage_file_path().isEmpty());
  }
}

// An AppImage copy is one file the user replaces: its update prompt names that file, opens
// the AppImage download (the manifest's appimage_url, else the GitHub permalink), and
// offers no flatpak command.
void ui_update_available_dialog_offers_appimage_download() {
#if defined(Q_OS_LINUX)
  const AppImageEnvironment env("/home/someone/Apps/PatchyLinux.AppImage", appdir_containing_this_binary());
  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("updateAvailableMessageBox")));
    CHECK(dialog != nullptr);
    CHECK(dialog->text().contains(QStringLiteral("replace this file")));
    CHECK(dialog->text().contains(QStringLiteral("/home/someone/Apps/PatchyLinux.AppImage")));
    CHECK(!dialog->text().contains(QStringLiteral("flatpak")));
    CHECK(dialog->findChild<QAbstractButton*>(QStringLiteral("updateCopyCommandButton")) == nullptr);
    CHECK(dialog->defaultButton() != nullptr &&
          dialog->defaultButton()->objectName() == QStringLiteral("updateDownloadButton"));
    saw_dialog = true;
    dialog->reject();
  });
  window.show_update_available({QStringLiteral("linux"), QStringLiteral("9.9"),
                                QUrl(QStringLiteral(
                                    "https://github.com/SethRobinson/Patchy/releases/latest/download/PatchyLinux.flatpak"))});
  CHECK(saw_dialog);
#else
  patchy::test::skip("Linux only (AppImage update prompt)");
#endif
}

// The AppImage half of a one-click update (docs/auto-update.md, "AppImage"): the download is
// checked once more and renamed over the running file, which only then holds the new bytes;
// a damaged or missing download leaves the running file alone.
void update_appimage_replacement_checks_and_renames() {
  QTemporaryDir dir;
  CHECK(dir.isValid());
  const auto target = dir.filePath(QStringLiteral("PatchyLinux.AppImage"));
  const QByteArray old_bytes(4000, 'o');
  const QByteArray new_bytes(6000, 'n');
  const auto write = [](const QString& path, const QByteArray& bytes) {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size());
  };
  const auto read = [](const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  };
  write(target, old_bytes);
  CHECK(patchy::ui::appimage_can_be_replaced(target));
  CHECK(!patchy::ui::appimage_can_be_replaced(dir.filePath(QStringLiteral("absent.AppImage"))));
  CHECK(!patchy::ui::appimage_can_be_replaced(QString()));

  const auto name = patchy::ui::appimage_update_download_name(target, QStringLiteral("9.9"));
  CHECK(name == QStringLiteral(".PatchyLinux.AppImage.update-9.9"));
  CHECK(patchy::ui::appimage_update_download_name(target, QStringLiteral("../9.9/x")) ==
        QStringLiteral(".PatchyLinux.AppImage.update-..9.9"));
  const auto downloaded = dir.filePath(name);
  const auto sha = QCryptographicHash::hash(new_bytes, QCryptographicHash::Sha256).toHex();

  patchy::ui::UpdateInstallerLaunch launch{downloaded, {}, 0, new_bytes.size(), sha, {}, target};
  CHECK(patchy::ui::replace_appimage(launch).failure == patchy::ui::UpdateLaunchFailure::InstallerMissing);
  write(downloaded, new_bytes);
  auto wrong = launch;
  wrong.expected_sha256 = QByteArray(64, '0');
  CHECK(patchy::ui::replace_appimage(wrong).failure == patchy::ui::UpdateLaunchFailure::ChecksumMismatch);
  CHECK(read(target) == old_bytes);
  CHECK(QFileInfo::exists(downloaded));

  CHECK(patchy::ui::replace_appimage(launch).started());
  CHECK(read(target) == new_bytes);
  CHECK(!QFileInfo::exists(downloaded));
#ifdef Q_OS_LINUX
  CHECK(QFileInfo(target).isExecutable());
#endif

  // A leftover download (an update cancelled at the save prompt) goes once that version runs;
  // nothing else in the folder is touched.
  write(downloaded, new_bytes);
  const auto unrelated = dir.filePath(QStringLiteral(".PatchyLinux.AppImage.update-9.8"));
  write(unrelated, old_bytes);
  patchy::ui::remove_appimage_update_download(target, QStringLiteral("9.9"));
  CHECK(!QFileInfo::exists(downloaded));
  CHECK(QFileInfo::exists(unrelated));
  CHECK(QFileInfo::exists(target));
}

// The whole AppImage one-click path on Linux, from the prompt: Update Now downloads the
// pinned AppImage beside the running file, the save prompt can still keep Patchy open, and a
// settled close hands the replacement over (captured here, then applied for real).
void ui_update_one_click_replaces_appimage_on_close() {
#if defined(Q_OS_LINUX)
  QTemporaryDir apps;
  CHECK(apps.isValid());
  const auto appimage = apps.filePath(QStringLiteral("PatchyLinux.AppImage"));
  {
    QFile running(appimage);
    CHECK(running.open(QIODevice::WriteOnly) && running.write(QByteArray(3000, 'o')) == 3000);
  }
  QByteArray new_bytes;
  for (int i = 0; i < 150000; ++i) {
    new_bytes.append(static_cast<char>(i % 239));
  }
  QTemporaryDir source_dir;
  CHECK(source_dir.isValid());
  const auto source_path = source_dir.filePath(QStringLiteral("PatchyLinux.AppImage"));
  {
    QFile source(source_path);
    CHECK(source.open(QIODevice::WriteOnly) && source.write(new_bytes) == new_bytes.size());
  }
  const AppImageEnvironment env(appimage.toUtf8(), appdir_containing_this_binary());
  CHECK(patchy::ui::appimage_file_path() == appimage);

  patchy::ui::UpdateInfo update{QStringLiteral("linux"), QStringLiteral("9.9"),
                                QUrl(QStringLiteral("https://rtsoft.com/patchy/PatchyLinux.flatpak"))};
  update.appimage_install_url = QUrl::fromLocalFile(source_path);
  update.appimage_install_size = new_bytes.size();
  update.appimage_install_sha256 = QCryptographicHash::hash(new_bytes, QCryptographicHash::Sha256).toHex();
  const auto downloaded = apps.filePath(QStringLiteral(".PatchyLinux.AppImage.update-9.9"));

  patchy::ui::MainWindow window;
  show_window(window);
  std::vector<patchy::ui::UpdateInstallerLaunch> launches;
  patchy::ui::MainWindowTestAccess::set_update_installer_launcher(
      window, [&](const patchy::ui::UpdateInstallerLaunch& launch) {
        launches.push_back(launch);
        return patchy::ui::UpdateLaunchResult{};
      });
  int exit_requests = 0;
  patchy::ui::MainWindowTestAccess::set_application_exit_hook(window, [&] { ++exit_requests; });

  bool prompt_answered = false;
  bool saw_update_now = false;
  auto save_answer = QMessageBox::Cancel;
  QTimer driver;
  QObject::connect(&driver, &QTimer::timeout, &window, [&] {
    if (auto* save = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("saveChangesMessageBox")))) {
      save->button(save_answer)->click();
      return;
    }
    if (prompt_answered) {
      return;
    }
    if (auto* prompt = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("updateAvailableMessageBox")))) {
      prompt_answered = true;
      CHECK(prompt->text().contains(QStringLiteral("replace this AppImage")));
      CHECK(prompt->findChild<QAbstractButton*>(QStringLiteral("updateCopyCommandButton")) == nullptr);
      auto* update_now = prompt->findChild<QAbstractButton*>(QStringLiteral("updateInstallNowButton"));
      saw_update_now = update_now != nullptr;
      if (update_now != nullptr) {
        update_now->click();
      } else {
        prompt->reject();
      }
    }
  });
  const auto run_update = [&] {
    prompt_answered = false;
    driver.start(10);
    window.show_update_available(update);
    process_events_for(200);
    driver.stop();
    CHECK(prompt_answered);
  };

  // Unsaved work and Cancel at the save prompt: Patchy stays open, the download is kept.
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  save_answer = QMessageBox::Cancel;
  run_update();
  CHECK(saw_update_now);
  CHECK(launches.empty());
  CHECK(window.isVisible());
  CHECK(QFileInfo(downloaded).size() == new_bytes.size());
  CHECK(exit_requests == 0);

  // Settled: the replacement is handed over for this file, the window closes, and the
  // application exit is requested once.
  save_answer = QMessageBox::Discard;
  run_update();
  CHECK(launches.size() == 1);
  CHECK(!window.isVisible());
  CHECK(exit_requests == 1);
  if (launches.size() == 1) {
    const auto& launch = launches.front();
    CHECK(launch.appimage_target == appimage);
    CHECK(QFileInfo(launch.installer_path) == QFileInfo(downloaded));
    CHECK(launch.wait_process_id == QCoreApplication::applicationPid());
    CHECK(launch.expected_size == new_bytes.size());
    // The real replacement (launch_update_installer would also start the relaunch waiter).
    CHECK(patchy::ui::replace_appimage(launch).started());
    QFile replaced(appimage);
    CHECK(replaced.open(QIODevice::ReadOnly) && replaced.readAll() == new_bytes);
  }

  // A read-only folder cannot take the rename: the prompt falls back to the download.
  const AppImageEnvironment readonly_env("/usr/bin/PatchyLinux.AppImage", appdir_containing_this_binary());
  patchy::ui::MainWindow second;
  show_window(second);
  bool saw_download_prompt = false;
  QTimer::singleShot(0, [&] {
    auto* prompt = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("updateAvailableMessageBox")));
    CHECK(prompt != nullptr);
    if (prompt != nullptr) {
      saw_download_prompt =
          prompt->findChild<QAbstractButton*>(QStringLiteral("updateInstallNowButton")) == nullptr &&
          prompt->findChild<QAbstractButton*>(QStringLiteral("updateDownloadButton")) != nullptr;
      prompt->reject();
    }
  });
  second.show_update_available(update);
  CHECK(saw_download_prompt);
#else
  patchy::test::skip("Linux only (AppImage one-click update)");
#endif
}

// The installer is checked again at the moment it is started: its pinned checksum, and for a
// signed Patchy the same Authenticode publisher. A file that fails either is never run.
void update_installer_is_checked_again_before_it_runs() {
  QTemporaryDir dir;
  CHECK(dir.isValid());
  const auto path = dir.filePath(QStringLiteral("PatchyWindowsInstaller.exe"));
  const QByteArray bytes(50000, 'i');
  {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size());
  }
  const auto sha = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
  CHECK(patchy::ui::file_matches_checksum(path, bytes.size(), sha));
  CHECK(patchy::ui::file_matches_checksum(path, bytes.size(), sha.toUpper()));
  CHECK(!patchy::ui::file_matches_checksum(path, bytes.size() + 1, sha));
  CHECK(!patchy::ui::file_matches_checksum(path, bytes.size(), QByteArray(64, '0')));
  CHECK(!patchy::ui::file_matches_checksum(path, 0, {}));
  CHECK(!patchy::ui::file_matches_checksum(dir.filePath(QStringLiteral("absent.exe")), bytes.size(), sha));

  using patchy::ui::FileSignature;
  using patchy::ui::installer_signer_acceptable;
  const FileSignature unsigned_file{};
  const FileSignature publisher{true, QStringLiteral("Seth A. Robinson")};
  const FileSignature someone_else{true, QStringLiteral("Someone Else")};
  CHECK(installer_signer_acceptable(publisher, publisher));
  CHECK(!installer_signer_acceptable(publisher, someone_else));
  CHECK(!installer_signer_acceptable(publisher, unsigned_file));
  // A development build has no publisher to compare with; the checksum still applies.
  CHECK(installer_signer_acceptable(unsigned_file, unsigned_file));
  CHECK(installer_signer_acceptable(unsigned_file, someone_else));

  // A swapped or damaged file is refused before anything is started.
  patchy::ui::UpdateInstallerLaunch launch{path, dir.path(), QCoreApplication::applicationPid(), bytes.size(),
                                           QByteArray(64, '0')};
#if defined(Q_OS_WIN)
  CHECK(patchy::ui::launch_update_installer(launch).failure == patchy::ui::UpdateLaunchFailure::ChecksumMismatch);
  launch.installer_path = dir.filePath(QStringLiteral("absent.exe"));
  CHECK(patchy::ui::launch_update_installer(launch).failure == patchy::ui::UpdateLaunchFailure::InstallerMissing);
#else
  CHECK(!patchy::ui::launch_update_installer(launch).started());
#endif
#if defined(Q_OS_WIN)
  // Real signatures: Qt's DLL beside the suite is signed, the suite itself and the stand-in are not.
  const auto qt_signature = patchy::ui::file_signature(
      QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("Qt6Core.dll")));
  CHECK(qt_signature.valid);
  CHECK(qt_signature.signer.contains(QStringLiteral("Qt Company")));
  CHECK(!patchy::ui::file_signature(QCoreApplication::applicationFilePath()).valid);
  CHECK(!patchy::ui::file_signature(path).valid);
#endif
}

// The whole one-click path with a file:// stand-in for the installer and the launch
// captured: a wrong checksum never reaches the installer, a cancelled save prompt or a
// launch that fails leaves Patchy open, and a settled close hands over exactly once.
void ui_update_one_click_downloads_installer_and_hands_over_on_close() {
#if defined(Q_OS_WIN)
  const auto root = patchy::ui::to_qstring(
      std::filesystem::absolute(patchy::test::unicode_artifact_dir(u8"one-click-update")));
  QDir(root).mkpath(QStringLiteral("source"));
  QByteArray installer_bytes;
  for (int i = 0; i < 180000; ++i) {
    installer_bytes.append(static_cast<char>(i % 241));
  }
  const auto source_path = QDir(root).filePath(QStringLiteral("source/PatchyWindowsInstaller.exe"));
  {
    QFile source(source_path);
    CHECK(source.open(QIODevice::WriteOnly) && source.write(installer_bytes) == installer_bytes.size());
  }
  const auto download_folder = QDir(root).filePath(QStringLiteral("download"));
  const auto downloaded_path = QDir(download_folder).filePath(QStringLiteral("PatchyWindowsInstaller.exe"));

  patchy::ui::UpdateInfo update{QStringLiteral("windows"), QStringLiteral("9.9"),
                                QUrl(QStringLiteral("https://rtsoft.com/patchy/PatchyWindowsInstaller.exe"))};
  update.installer_url = QUrl::fromLocalFile(source_path);
  update.installer_size = installer_bytes.size();
  update.installer_sha256 = QCryptographicHash::hash(installer_bytes, QCryptographicHash::Sha256).toHex();

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::set_self_update_override(window, true);
  patchy::ui::MainWindowTestAccess::set_update_download_folder(window, download_folder);
  std::vector<patchy::ui::UpdateInstallerLaunch> launches;
  bool launcher_starts = true;
  patchy::ui::MainWindowTestAccess::set_update_installer_launcher(
      window, [&](const patchy::ui::UpdateInstallerLaunch& launch) {
        launches.push_back(launch);
        return launcher_starts
                   ? patchy::ui::UpdateLaunchResult{}
                   : patchy::ui::UpdateLaunchResult{patchy::ui::UpdateLaunchFailure::NotSigned,
                                                    QStringLiteral("Seth A. Robinson"), {}};
      });
  // A settled close ends the event loop itself (Qt's posted quit can be cancelled by a window
  // shown in the meantime); here the request is counted instead of ending the suite.
  int exit_requests = 0;
  patchy::ui::MainWindowTestAccess::set_application_exit_hook(window, [&] { ++exit_requests; });

  // Answers each dialog of one run as it appears.
  bool prompt_answered = false;
  bool saw_failure = false;
  bool saw_save_prompt = false;
  QString install_failure_text;
  auto save_answer = QMessageBox::Cancel;
  QTimer driver;
  QObject::connect(&driver, &QTimer::timeout, &window, [&] {
    if (auto* failure =
            qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("updateDownloadFailedMessageBox")))) {
      saw_failure = true;
      failure->reject();  // never the Download button: it opens a browser
      return;
    }
    if (auto* refused =
            qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("updateInstallFailedMessageBox")))) {
      install_failure_text = refused->text();
      refused->reject();  // never the Download button: it opens a browser
      return;
    }
    if (auto* save = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("saveChangesMessageBox")))) {
      saw_save_prompt = true;
      save->button(save_answer)->click();
      return;
    }
    if (prompt_answered) {
      return;
    }
    if (auto* prompt = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("updateAvailableMessageBox")))) {
      prompt_answered = true;
      auto* update_now = prompt->findChild<QAbstractButton*>(QStringLiteral("updateInstallNowButton"));
      CHECK(update_now != nullptr);
      if (update_now != nullptr) {
        update_now->click();
      } else {
        prompt->reject();
      }
    }
  });
  const auto run_update = [&](const patchy::ui::UpdateInfo& info) {
    prompt_answered = false;
    saw_failure = false;
    saw_save_prompt = false;
    install_failure_text.clear();
    driver.start(10);
    window.show_update_available(info);
    process_events_for(200);  // a refused installer is reported once the close event is over
    driver.stop();
    CHECK(prompt_answered);
  };

  // A download that does not match the manifest's checksum: reported, nothing kept.
  auto wrong = update;
  wrong.installer_sha256 = QCryptographicHash::hash(QByteArray("something else"), QCryptographicHash::Sha256).toHex();
  run_update(wrong);
  CHECK(saw_failure);
  CHECK(launches.empty());
  CHECK(window.isVisible());
  CHECK(!QFileInfo::exists(downloaded_path));

  // Unsaved work and Cancel at the save prompt: Patchy stays open, nothing is launched.
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  save_answer = QMessageBox::Cancel;
  run_update(update);
  CHECK(!saw_failure);
  CHECK(saw_save_prompt);
  CHECK(launches.empty());
  CHECK(window.isVisible());
  CHECK(!patchy::ui::MainWindowTestAccess::update_install_pending(window));
  CHECK(exit_requests == 0);
  {
    QFile downloaded(downloaded_path);
    CHECK(downloaded.open(QIODevice::ReadOnly) && downloaded.readAll() == installer_bytes);
  }

  // The installer cannot be started: Patchy stays open instead of quitting un-updated.
  save_answer = QMessageBox::Discard;
  launcher_starts = false;
  run_update(update);
  CHECK(saw_save_prompt);
  CHECK(launches.size() == 1);
  CHECK(window.isVisible());
  CHECK(!patchy::ui::MainWindowTestAccess::update_install_pending(window));
  // And says why, not only that it failed.
  CHECK(install_failure_text.contains(QStringLiteral("The update was not installed.")));
  CHECK(install_failure_text.contains(QStringLiteral("is not signed by Seth A. Robinson")));
  CHECK(exit_requests == 0);

  // Settled: the installer gets this copy's folder and process id, the window closes, and
  // the application exit is requested exactly once.
  launcher_starts = true;
  run_update(update);
  CHECK(launches.size() == 2);
  if (launches.size() == 2) {
    CHECK(QFileInfo(launches.back().installer_path) == QFileInfo(downloaded_path));
    CHECK(launches.back().install_dir == QCoreApplication::applicationDirPath());
    CHECK(launches.back().wait_process_id == QCoreApplication::applicationPid());
    CHECK(launches.back().expected_size == installer_bytes.size());
    CHECK(launches.back().expected_sha256 == update.installer_sha256);
  }
  CHECK(!window.isVisible());
  CHECK(exit_requests == 1);
#else
  patchy::test::skip("Windows only (one-click update)");
#endif
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

// A store build (CMake PATCHY_STORE_BUILD) and PATCHY_NO_UPDATE_CHECK=1 share one switch:
// no startup request leaves the machine and Preferences offers no setting for it.
void ui_update_checks_can_be_switched_off() {
  CHECK(patchy::ui::update_checks_available());
  const EnvironmentVariableRestorer restore_switch("PATCHY_NO_UPDATE_CHECK");
  qputenv("PATCHY_NO_UPDATE_CHECK", "1");
  CHECK(!patchy::ui::update_checks_available());

  SettingsValueRestorer restore_update_check(QStringLiteral("updates/checkOnStartup"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("updates/checkOnStartup"), true);
    settings.sync();
  }
  ManifestServer server(R"({"platforms": {"windows": {"version": "9.9", "download_url": "https://rtsoft.com/w.exe"},
                                          "macos": {"version": "9.9", "download_url": "https://rtsoft.com/m.dmg"},
                                          "linux": {"version": "9.9", "download_url": "https://rtsoft.com/l.flatpak"}}})");
  const UpdateManifestUrlOverride override(server.manifest_url());

  patchy::ui::MainWindow window;
  show_window(window);
  window.begin_startup_update_check();
  process_events_for(500);
  CHECK(server.requests() == 0);
  CHECK(find_top_level_dialog(QStringLiteral("updateAvailableMessageBox")) == nullptr);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    CHECK(dialog->findChild<QCheckBox*>(QStringLiteral("preferencesCheckForUpdatesCheck")) == nullptr);
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);

  // Accepting Preferences without the checkbox leaves the stored choice alone.
  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("updates/checkOnStartup"), false).toBool());
}

// GitHub issue 92: a wheel that passes over a Preferences dropdown or number field scrolls
// the page and leaves the setting alone; only a control with keyboard focus takes the wheel.
void ui_preferences_wheel_over_unfocused_control_scrolls_the_page() {
  patchy::ui::MainWindow window;
  show_window(window);
  run_preferences_dialog(window, /*accept=*/false, [](QDialog& dialog) {
    auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("preferencesTabWidget"));
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("preferencesIndexedOpenCombo"));
    CHECK(tabs != nullptr && combo != nullptr);
    if (tabs == nullptr || combo == nullptr) {
      return;
    }
    dialog.resize(dialog.minimumSize());
    QApplication::processEvents();
    auto* scroll = tabs->currentWidget()->findChild<QScrollArea*>(QStringLiteral("preferencesTabScroll"));
    CHECK(scroll != nullptr);
    if (scroll == nullptr) {
      return;
    }
    auto* bar = scroll->verticalScrollBar();
    CHECK(bar->maximum() > 0);
    bar->setValue(0);

    // A real wheel focuses a WheelFocus widget before delivery, so the policy matters too
    // (macOS combo boxes start at TabFocus, which the guard leaves alone).
    CHECK(combo->focusPolicy() != Qt::WheelFocus && (combo->focusPolicy() & Qt::TabFocus) != 0);
    combo->clearFocus();
    CHECK(combo->count() > 1);
    combo->setCurrentIndex(0);
    send_wheel_notch_down(*combo);
    CHECK(combo->currentIndex() == 0);
    CHECK(bar->value() > 0);

    // With keyboard focus the wheel is the control's again.
    combo->setFocus();
    QApplication::processEvents();
    CHECK(combo->hasFocus());
    // (macOS combo boxes never take the wheel; the style says so.)
    if (combo->style()->styleHint(QStyle::SH_ComboBox_AllowWheelScrolling, nullptr, combo) != 0) {
      const int scrolled = bar->value();
      send_wheel_notch_down(*combo);
      CHECK(combo->currentIndex() == 1);
      CHECK(bar->value() == scrolled);
    }
    combo->clearFocus();

    // The interface-scale spin box (GitHub issue 333) and spin boxes on the other tabs are
    // covered by the same guard.
    auto* scale_spin = dialog.findChild<QSpinBox*>(QStringLiteral("preferencesGuiScaleSpin"));
    CHECK(scale_spin != nullptr);
    if (scale_spin != nullptr) {
      CHECK(scale_spin->focusPolicy() != Qt::WheelFocus && (scale_spin->focusPolicy() & Qt::TabFocus) != 0);
      const int before = scale_spin->value();
      send_wheel_notch_down(*scale_spin);
      CHECK(scale_spin->value() == before);
    }
    auto* spin = dialog.findChild<QAbstractSpinBox*>(QStringLiteral("preferencesGridSpacingSpin"));
    CHECK(spin != nullptr);
    if (spin != nullptr) {
      CHECK(spin->focusPolicy() != Qt::WheelFocus && (spin->focusPolicy() & Qt::TabFocus) != 0);
      const auto before = spin->text();
      send_wheel_notch_down(*spin);
      CHECK(spin->text() == before);
    }
  });
}

// GitHub issue 158: the recent-files filter matches the file name, so "jpg" lists
// the JPEGs even when every file sits under a folder called JPG; ".jpg" / "*.jpg"
// filter by extension and a token with a slash still matches the folder path.
void ui_recent_filter_matches_file_names_and_extensions() {
  using patchy::ui::recent_filter_tokens;
  using patchy::ui::recent_path_matches;
  const QString photo = QStringLiteral("/mnt/jay/JPG/jpg-zip/002/photo.jpg");
  const QString scan = QStringLiteral("/mnt/jay/JPG/jpg-zip/002/scan.PNG");
  const QString beta = QStringLiteral("C:/work/Beta Shots/final.psd");
  CHECK(recent_path_matches(photo, recent_filter_tokens(QStringLiteral("jpg"))));
  CHECK(!recent_path_matches(scan, recent_filter_tokens(QStringLiteral("jpg"))));
  CHECK(recent_path_matches(scan, recent_filter_tokens(QStringLiteral(".png"))));
  CHECK(recent_path_matches(scan, recent_filter_tokens(QStringLiteral("*.png"))));
  CHECK(!recent_path_matches(photo, recent_filter_tokens(QStringLiteral(".png"))));
  CHECK(recent_path_matches(beta, recent_filter_tokens(QStringLiteral("FINAL"))));
  CHECK(!recent_path_matches(beta, recent_filter_tokens(QStringLiteral("beta"))));  // folder, not name
  CHECK(recent_path_matches(beta, recent_filter_tokens(QStringLiteral("shots/"))));  // a slash reaches the folder
  CHECK(recent_path_matches(photo, recent_filter_tokens(QStringLiteral("pho .jpg"))));  // every token must hold
  CHECK(!recent_path_matches(photo, recent_filter_tokens(QStringLiteral("pho .png"))));
  CHECK(recent_path_matches(photo, recent_filter_tokens(QString())));
}

void ui_preferences_dialog_resizes_and_remembers_its_size() {
  SettingsValueRestorer restore_size(QStringLiteral("preferences/dialogSize"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("preferences/dialogSize"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);

  QSize target;
  run_preferences_dialog(window, /*accept=*/false, [&](QDialog& dialog) {
    CHECK(dialog.findChild<QSizeGrip*>(QStringLiteral("preferencesSizeGrip")) != nullptr);
    auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("preferencesTabWidget"));
    CHECK(tabs != nullptr);
    if (tabs == nullptr) {
      return;
    }
    CHECK(dialog.maximumHeight() > dialog.minimumHeight());
    const auto entry_size = dialog.size();
    dialog.resize(dialog.minimumSize());
    QApplication::processEvents();
    const int small_tabs_height = tabs->height();
    // Taller only: the tab row can make the minimum width as wide as the offscreen screen,
    // and a restored size is kept inside the screen.
    target = dialog.minimumSize() + QSize(0, 90);
    CHECK(target != entry_size);
    dialog.resize(target);
    QApplication::processEvents();
    CHECK(dialog.size() == target);
    CHECK(tabs->height() > small_tabs_height);
  });
  CHECK(target.isValid());
  CHECK(patchy::ui::stored_preferences_dialog_size() == target);

  run_preferences_dialog(window, /*accept=*/false, [&](QDialog& dialog) { CHECK(dialog.size() == target); });
}

// GitHub issue 92: the frameless Preferences dialog resizes by its corner grip, its pages
// grow with it, and the size comes back the next time it opens.
// GitHub issue 146: the interval combos take the width of their longest entry, so
// "Every 2 weeks" and "10 minute(s)" never elide in the field or its popup.
void ui_preferences_interval_combos_fit_their_longest_entry() {
  patchy::ui::MainWindow window;
  show_window(window);
  run_preferences_dialog(window, /*accept=*/false, [](QDialog& dialog) {
    int seen = 0;
    for (const auto* name : {"preferencesUpdateCheckIntervalCombo", "preferencesRecoveryIntervalCombo"}) {
      auto* combo = dialog.findChild<QComboBox*>(QString::fromLatin1(name));
      if (combo == nullptr) {
        continue;  // the update combo is absent in builds without update checks
      }
      ++seen;
      CHECK(combo->sizeAdjustPolicy() == QComboBox::AdjustToContents);
      const QFontMetrics metrics(combo->font());
      int longest = 0;
      for (int i = 0; i < combo->count(); ++i) {
        longest = std::max(longest, metrics.horizontalAdvance(combo->itemText(i)));
      }
      CHECK(combo->width() >= longest);
    }
    CHECK(seen >= 1);
  });
}

// GitHub issue 144: Preferences answers Photoshop's Ctrl+K off macOS (Cmd+, there).
void ui_preferences_hotkey_is_ctrl_k_off_macos() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* action = require_hotkey_action(window, QStringLiteral("file.preferences"));
#ifdef Q_OS_MACOS
  CHECK(action->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Comma));
#else
  CHECK(action->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_K));
#endif
}

// GitHub issue 92: the interval gate of the startup update check, without any network.
void ui_update_check_interval_gates_the_startup_check() {
  using patchy::ui::startup_update_check_due;
  constexpr qint64 kDay = 24 * 60 * 60;
  constexpr qint64 kNow = 1'800'000'000;

  constexpr qint64 kHour = 60 * 60;
  // kNow is 08:00 UTC; the gate counts calendar days in the given zone (GitHub issue 189).
  const QTimeZone utc(QTimeZone::UTC);
  CHECK(QDateTime::fromSecsSinceEpoch(kNow, utc).time() == QTime(8, 0));

  // Every launch is the default and always due.
  CHECK(patchy::ui::kDefaultUpdateCheckIntervalDays == 0);
  CHECK(startup_update_check_due(0, kNow - 1, kNow, utc));
  CHECK(startup_update_check_due(0, kNow, kNow, utc));
  for (const int days : {1, 7, 14, 30}) {
    CHECK(!startup_update_check_due(days, kNow - 1, kNow, utc));
    // Earlier on the same day, or that many days back but still not that many dates.
    CHECK(!startup_update_check_due(days, kNow - 8 * kHour, kNow, utc));
    CHECK(!startup_update_check_due(days, kNow - (days - 1) * kDay - 8 * kHour, kNow, utc));
    // The last minute of the day `days` dates back is enough, even short of days * 24 h.
    CHECK(startup_update_check_due(days, kNow - (days - 1) * kDay - 8 * kHour - 60, kNow, utc));
    CHECK(startup_update_check_due(days, kNow - days * kDay, kNow, utc));
    // Never checked, or a record from the future (the clock moved back).
    CHECK(startup_update_check_due(days, 0, kNow, utc));
    CHECK(startup_update_check_due(days, kNow + kDay, kNow, utc));
  }
  // Daily: a check at 23:00 the evening before makes a 07:00 launch due, a check at 01:00
  // the same morning does not. The day boundary is the user's own: 07:00 UTC the same day
  // is 16:00 in UTC+9, the same date as kNow (17:00 there), but 23:00 UTC the day before
  // is 08:00 there, so UTC+9 still calls it today.
  CHECK(startup_update_check_due(1, kNow - 9 * kHour, kNow - kHour, utc));
  CHECK(!startup_update_check_due(1, kNow - 7 * kHour, kNow - kHour, utc));
  const auto tokyo = QTimeZone::fromSecondsAheadOfUtc(9 * 60 * 60);
  CHECK(!startup_update_check_due(1, kNow - kHour, kNow, tokyo));
  CHECK(!startup_update_check_due(1, kNow - 9 * kHour, kNow, tokyo));
  CHECK(startup_update_check_due(1, kNow - 9 * kHour, kNow, utc));

  for (const int days : patchy::ui::kUpdateCheckIntervalDays) {
    CHECK(patchy::ui::normalize_update_check_interval_days(days) == days);
  }
  CHECK(patchy::ui::normalize_update_check_interval_days(1) == 1);
  CHECK(patchy::ui::normalize_update_check_interval_days(5) == 0);
  CHECK(patchy::ui::normalize_update_check_interval_days(-7) == 0);

  SettingsValueRestorer restore_interval(QStringLiteral("updates/checkIntervalDays"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("updates/checkIntervalDays"));
    settings.sync();
  }
  CHECK(patchy::ui::stored_update_check_interval_days() == 0);

  using patchy::ui::update_prompt_wanted;
  CHECK(update_prompt_wanted(QStringLiteral("9.9"), QString()));
  CHECK(update_prompt_wanted(QStringLiteral("9.9"), QStringLiteral("not a version")));
  CHECK(!update_prompt_wanted(QStringLiteral("9.9"), QStringLiteral("9.9")));
  CHECK(!update_prompt_wanted(QStringLiteral("9.8"), QStringLiteral("9.9")));
  CHECK(update_prompt_wanted(QStringLiteral("9.10"), QStringLiteral("9.9")));
}

// The startup check against a loopback manifest: skipped inside the interval, run and
// recorded once it has passed, and always run on "every launch".
void ui_update_startup_check_honors_stored_interval() {
  SettingsValueRestorer restore_update_check(QStringLiteral("updates/checkOnStartup"));
  SettingsValueRestorer restore_interval(QStringLiteral("updates/checkIntervalDays"));
  SettingsValueRestorer restore_last_check(QStringLiteral("updates/lastCheckSecsSinceEpoch"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("updates/checkOnStartup"), true);
    settings.sync();
  }
  // Nothing newer, so no prompt opens.
  ManifestServer server(R"({"platforms": {"windows": {"version": "0.0.1", "download_url": "https://rtsoft.com/w.exe"},
                                          "macos": {"version": "0.0.1", "download_url": "https://rtsoft.com/m.dmg"},
                                          "linux": {"version": "0.0.1", "download_url": "https://rtsoft.com/l.flatpak"}}})");
  const UpdateManifestUrlOverride override(server.manifest_url());
  patchy::ui::MainWindow window;
  show_window(window);

  constexpr qint64 kDay = 24 * 60 * 60;
  const auto now = QDateTime::currentSecsSinceEpoch();

  patchy::ui::set_stored_update_check_interval_days(7);
  patchy::ui::set_stored_update_last_check_secs(now - 3600);
  window.begin_startup_update_check();
  process_events_for(500);
  CHECK(server.requests() == 0);
  CHECK(patchy::ui::stored_update_last_check_secs() == now - 3600);

  patchy::ui::set_stored_update_last_check_secs(now - 8 * kDay);
  window.begin_startup_update_check();
  CHECK(process_events_until([&] { return patchy::ui::stored_update_last_check_secs() >= now; }, 15000));
  CHECK(server.requests() == 1);

  // Daily: a check already made today waits for tomorrow, one two days back runs.
  patchy::ui::set_stored_update_check_interval_days(1);
  patchy::ui::set_stored_update_last_check_secs(QDateTime::currentSecsSinceEpoch());
  const auto checked_today = patchy::ui::stored_update_last_check_secs();
  window.begin_startup_update_check();
  process_events_for(500);
  CHECK(server.requests() == 1);
  patchy::ui::set_stored_update_last_check_secs(checked_today - 2 * kDay);
  window.begin_startup_update_check();
  CHECK(process_events_until([&] { return patchy::ui::stored_update_last_check_secs() >= checked_today; }, 15000));
  CHECK(server.requests() == 2);

  // The record is fresh now, but "every launch" checks regardless.
  patchy::ui::set_stored_update_check_interval_days(0);
  window.begin_startup_update_check();
  CHECK(process_events_until([&] { return server.requests() == 3; }, 15000));
}

// GitHub issue 189: "Daily" sits between "Every launch" and "Weekly", for people who start
// Patchy several times a day. The capture shows the row and the open list.
void ui_update_preference_offers_daily_interval() {
  SettingsValueRestorer restore_update_check(QStringLiteral("updates/checkOnStartup"));
  SettingsValueRestorer restore_interval(QStringLiteral("updates/checkIntervalDays"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("updates/checkOnStartup"), true);
    settings.remove(QStringLiteral("updates/checkIntervalDays"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);

  run_preferences_dialog(window, /*accept=*/true, [](QDialog& dialog) {
    auto* row = dialog.findChild<QWidget*>(QStringLiteral("preferencesUpdateCheckRow"));
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("preferencesUpdateCheckIntervalCombo"));
    CHECK(row != nullptr && combo != nullptr);
    if (row == nullptr || combo == nullptr) {
      return;
    }
    QList<QImage> strips;
    auto* group = row->parentWidget();
    strips << (group != nullptr ? group->grab(QRect(0, std::max(0, row->y() - 30), group->width(), row->height() + 60))
                                : row->grab())
                  .toImage();
    combo->view()->setMinimumWidth(combo->view()->sizeHintForColumn(0) + 24);
    combo->showPopup();
    QApplication::processEvents();
    strips << combo->view()->window()->grab().toImage();
    combo->hidePopup();
    QApplication::processEvents();
    save_stacked_artifact("issue189_update_check_interval", strips);
    const auto daily = combo->findData(1);
    CHECK(daily == 1);
    if (daily >= 0) {
      CHECK(combo->itemText(daily) == QStringLiteral("Daily"));
      combo->setCurrentIndex(daily);
    }
  });
  CHECK(patchy::ui::stored_update_check_interval_days() == 1);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("updates/checkIntervalDays")).toInt() == 1);
}

void ui_update_preference_persists_check_interval() {
  SettingsValueRestorer restore_update_check(QStringLiteral("updates/checkOnStartup"));
  SettingsValueRestorer restore_interval(QStringLiteral("updates/checkIntervalDays"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("updates/checkOnStartup"), true);
    settings.remove(QStringLiteral("updates/checkIntervalDays"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);

  run_preferences_dialog(window, /*accept=*/true, [](QDialog& dialog) {
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("preferencesUpdateCheckIntervalCombo"));
    CHECK(combo != nullptr);
    if (combo == nullptr) {
      return;
    }
    // Every launch stays the default; the stored value is the day count, never an index.
    CHECK(combo->isEnabled());
    CHECK(combo->currentData().toInt() == 0);
    CHECK(combo->currentText() == QStringLiteral("Every launch"));
    QList<int> days;
    for (int index = 0; index < combo->count(); ++index) {
      days.push_back(combo->itemData(index).toInt());
    }
    CHECK(days == QList<int>({0, 1, 7, 14, 30}));
    combo->setCurrentIndex(combo->findData(14));
  });
  CHECK(patchy::ui::stored_update_check_interval_days() == 14);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("updates/checkIntervalDays")).toInt() == 14);

  run_preferences_dialog(window, /*accept=*/false, [](QDialog& dialog) {
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("preferencesUpdateCheckIntervalCombo"));
    auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("preferencesCheckForUpdatesCheck"));
    CHECK(combo != nullptr && check != nullptr);
    if (combo == nullptr || check == nullptr) {
      return;
    }
    CHECK(combo->currentData().toInt() == 14);
    check->setChecked(false);
    CHECK(!combo->isEnabled());
  });
}

// "Don't ask again about version X" on the update prompt silences the startup prompt for
// that version only: the start panel still reports it, and a later release asks again.
void ui_update_prompt_skip_version_silences_until_a_newer_one() {
  SettingsValueRestorer restore_update_check(QStringLiteral("updates/checkOnStartup"));
  SettingsValueRestorer restore_interval(QStringLiteral("updates/checkIntervalDays"));
  SettingsValueRestorer restore_last_check(QStringLiteral("updates/lastCheckSecsSinceEpoch"));
  SettingsValueRestorer restore_skipped(QStringLiteral("updates/skippedVersion"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("updates/checkOnStartup"), true);
    settings.setValue(QStringLiteral("updates/checkIntervalDays"), 0);
    settings.remove(QStringLiteral("updates/skippedVersion"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);

  // Closes every update prompt that opens, ticking the checkbox when asked to.
  int prompts = 0;
  bool tick_skip = true;
  QTimer closer;
  QObject::connect(&closer, &QTimer::timeout, &closer, [&] {
    auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("updateAvailableMessageBox")));
    if (dialog == nullptr) {
      return;
    }
    ++prompts;
    auto* skip = dialog->findChild<QCheckBox*>(QStringLiteral("updateSkipVersionCheck"));
    CHECK(skip != nullptr);
    if (skip != nullptr) {
      CHECK(skip->text() == QStringLiteral("Don't ask again about version %1").arg(prompts == 1 ? QStringLiteral("9.9") : QStringLiteral("9.10")));
      CHECK(!skip->isChecked());
      skip->setChecked(tick_skip);
    }
    dialog->reject();
  });
  closer.start(20);

  window.show_update_available(
      {QStringLiteral("windows"), QStringLiteral("9.9"), QUrl(QStringLiteral("https://rtsoft.com/w.exe"))});
  CHECK(prompts == 1);
  CHECK(patchy::ui::stored_update_skipped_version() == QStringLiteral("9.9"));

  const auto manifest = [](const char* version) {
    return QByteArray(R"({"platforms": {"windows": {"version": "V", "download_url": "https://rtsoft.com/w.exe"},
                                         "macos": {"version": "V", "download_url": "https://rtsoft.com/m.dmg"},
                                         "linux": {"version": "V", "download_url": "https://rtsoft.com/l.flatpak"}}})")
        .replace("V", version);
  };
  {
    ManifestServer server(manifest("9.9"));
    const UpdateManifestUrlOverride override(server.manifest_url());
    window.begin_startup_update_check();
    CHECK(process_events_until([&] { return server.requests() == 1; }, 15000));
    process_events_for(300);
    CHECK(prompts == 1);
    auto* status = window.findChild<QLabel*>(QStringLiteral("startPanelUpdateStatus"));
    CHECK(status != nullptr);
    if (status != nullptr) {
      CHECK(status->text().contains(QStringLiteral("9.9")));
    }
  }
  {
    tick_skip = false;
    ManifestServer server(manifest("9.10"));
    const UpdateManifestUrlOverride override(server.manifest_url());
    window.begin_startup_update_check();
    CHECK(process_events_until([&] { return prompts == 2; }, 15000));
    // Not ticked this time: the stored choice is left alone.
    CHECK(patchy::ui::stored_update_skipped_version() == QStringLiteral("9.9"));
  }
}

void ui_gui_scale_preference_persists_setting() {
  SettingsValueRestorer restore_gui_scale(QStringLiteral("preferences/guiScalePercent"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/guiScalePercent"), 100);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  // GitHub issue 333: any whole percent, not only the old preset steps.
  const auto run = drive_preferences_gui_scale(window, 113);
  CHECK(run.saw_dialog);
  CHECK(run.dismissed_message);
  CHECK(run.entry_percent == 100);
  CHECK(patchy::ui::stored_gui_scale_percent() == 113);
}

// The values below 100% are what let a browser tab match the desktop build's apparent
// size, so keep one of them exercised end to end through the spin box.
void ui_gui_scale_preference_persists_step_below_full_size() {
  SettingsValueRestorer restore_gui_scale(QStringLiteral("preferences/guiScalePercent"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/guiScalePercent"), 100);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  const auto run = drive_preferences_gui_scale(window, 75);
  CHECK(run.saw_dialog);
  CHECK(run.dismissed_message);
  CHECK(patchy::ui::stored_gui_scale_percent() == 75);
}

// A stored value outside the range (a hand-edited ini) must never reach QT_SCALE_FACTOR
// as is: it is clamped to the range, and a non-numeric one falls back to the platform
// default. The spin box shows the value startup applies, and accepting without touching
// it writes nothing and raises no notice.
void ui_gui_scale_preference_falls_back_for_unknown_step() {
  SettingsValueRestorer restore_gui_scale(QStringLiteral("preferences/guiScalePercent"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/guiScalePercent"), QStringLiteral("large"));
    settings.sync();
  }
  CHECK(patchy::ui::stored_gui_scale_percent() == patchy::ui::kDefaultGuiScalePercent);
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/guiScalePercent"), 30);
    settings.sync();
  }
  CHECK(patchy::ui::stored_gui_scale_percent() == patchy::ui::kMinGuiScalePercent);

  patchy::ui::MainWindow window;
  show_window(window);

  const auto run = drive_preferences_gui_scale(window, std::nullopt);
  CHECK(run.saw_dialog);
  CHECK(!run.dismissed_message);
  CHECK(run.entry_percent == patchy::ui::kMinGuiScalePercent);
}

void gui_scale_percent_normalizes_to_offered_steps() {
  // GitHub issue 333: every whole percent in the range is kept, including the old preset
  // steps that existing settings files hold.
  for (const int percent : {50, 67, 75, 90, 100, 113, 125, 150, 175, 199, 200, 257, 300}) {
    CHECK(patchy::ui::normalize_gui_scale_percent(percent) == percent);
  }
  // Out of range clamps to the nearest end; zero or below (a missing or non-numeric
  // value) lands on the platform default.
  CHECK(patchy::ui::normalize_gui_scale_percent(1) == patchy::ui::kMinGuiScalePercent);
  CHECK(patchy::ui::normalize_gui_scale_percent(49) == patchy::ui::kMinGuiScalePercent);
  CHECK(patchy::ui::normalize_gui_scale_percent(301) == patchy::ui::kMaxGuiScalePercent);
  CHECK(patchy::ui::normalize_gui_scale_percent(1000) == patchy::ui::kMaxGuiScalePercent);
  for (const int percent : {-50, 0}) {
    CHECK(patchy::ui::normalize_gui_scale_percent(percent) ==
          patchy::ui::kDefaultGuiScalePercent);
  }
  // The desktop build must start at full size; only the web build shrinks by default.
  CHECK(patchy::ui::kDefaultGuiScalePercent == 100);
}

void ui_color_scheme_preference_persists_setting() {
  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  ColorSchemeRestorer restore_active;

  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(patchy::ui::active_color_scheme() == patchy::ui::ColorScheme::Dark);

  choose_preferences_color_scheme(window, QStringLiteral("light"), /*accept=*/true);

  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("preferences/colorScheme")).toString() ==
        QStringLiteral("light"));
  CHECK(patchy::ui::ThemeManager::instance().preference() == patchy::ui::ColorSchemePreference::Light);
  CHECK(patchy::ui::active_color_scheme() == patchy::ui::ColorScheme::Light);

  // Unlike interface scale, the scheme applies immediately, so no restart notice
  // may appear. A stray modal here would also hang the rest of the suite.
  CHECK(find_top_level_dialog(QStringLiteral("preferencesInterfaceScaleMessageBox")) == nullptr);
}

void ui_color_scheme_cancel_restores_entry_scheme() {
  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  // Seeded rather than asserted absent: accepting Preferences persists the scheme
  // like every other setting on that page, so any earlier test that accepted the
  // dialog has already written this key.
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/colorScheme"), QStringLiteral("dark"));
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  // The combo previews live; closing without accepting has to undo the preview.
  choose_preferences_color_scheme(window, QStringLiteral("light"), /*accept=*/false);
  QApplication::processEvents();

  CHECK(patchy::ui::ThemeManager::instance().preference() == patchy::ui::ColorSchemePreference::Dark);
  CHECK(patchy::ui::active_color_scheme() == patchy::ui::ColorScheme::Dark);
  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("preferences/colorScheme")).toString() == QStringLiteral("dark"));
}

// GitHub issue 133: the Preferences combo previews a built-in scheme without persisting,
// which already clears the active custom theme; the OK that follows must still clear the
// STORED custom theme id, or the next launch re-applies the old theme over the new scheme.
void ui_color_scheme_commit_after_preview_clears_stored_custom_theme() {
  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  SettingsValueRestorer restore_custom(QStringLiteral("preferences/customThemeId"));
  ColorSchemeRestorer restore_active;
  patchy::ui::CustomTheme custom;
  custom.name = QStringLiteral("Darkest-like");
  custom.base = patchy::ui::ColorScheme::Dark;
  custom.palette = patchy::ui::dark_palette();
  auto& manager = patchy::ui::ThemeManager::instance();
  manager.set_custom_theme(QStringLiteral("bundled:darkest.patchytheme"), custom, /*persist=*/true);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("preferences/customThemeId")).toString() ==
        QStringLiteral("bundled:darkest.patchytheme"));
  // The live preview of a built-in scheme (persist off) clears the active custom theme...
  manager.set_preference(patchy::ui::ColorSchemePreference::Light, /*persist=*/false);
  CHECK(!manager.active_custom_theme_id().has_value());
  // ...and the commit (persist on) must clear the stored id although none is active now.
  manager.set_preference(patchy::ui::ColorSchemePreference::Light, /*persist=*/true);
  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("preferences/customThemeId")).toString().isEmpty());
  CHECK(settings.value(QStringLiteral("preferences/colorScheme")).toString() == QStringLiteral("light"));
}

void ui_color_scheme_follow_system_tracks_style_hints() {
  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  ColorSchemeRestorer restore_active;

  auto& manager = patchy::ui::ThemeManager::instance();
  manager.set_preference(patchy::ui::ColorSchemePreference::FollowSystem, /*persist=*/false);

  // The offscreen platform reports Qt::ColorScheme::Unknown and never emits
  // colorSchemeChanged, so the system side is driven through the test seam.
  manager.set_system_color_scheme_for_testing(Qt::ColorScheme::Light);
  CHECK(manager.resolved_scheme() == patchy::ui::ColorScheme::Light);
  CHECK(patchy::ui::active_color_scheme() == patchy::ui::ColorScheme::Light);

  manager.set_system_color_scheme_for_testing(Qt::ColorScheme::Dark);
  CHECK(manager.resolved_scheme() == patchy::ui::ColorScheme::Dark);
  CHECK(patchy::ui::active_color_scheme() == patchy::ui::ColorScheme::Dark);

  // Unknown is what every platform without a color-scheme notion reports; it must
  // land on Patchy's historical Dark rather than an invalid state.
  manager.set_system_color_scheme_for_testing(Qt::ColorScheme::Unknown);
  CHECK(manager.resolved_scheme() == patchy::ui::ColorScheme::Dark);

  // An explicit choice ignores the system entirely.
  manager.set_preference(patchy::ui::ColorSchemePreference::Light, /*persist=*/false);
  manager.set_system_color_scheme_for_testing(Qt::ColorScheme::Dark);
  CHECK(manager.resolved_scheme() == patchy::ui::ColorScheme::Light);
  CHECK(patchy::ui::active_color_scheme() == patchy::ui::ColorScheme::Light);
}

// Exports the full palette, tweaks are not needed to prove fidelity: two
// deliberately-changed roles (one carrying alpha, to prove #RRGGBBAA
// round-trips) are enough, but every role is compared so a role the writer or
// the reader table silently skipped would still show up here.
void ui_theme_file_round_trips_full_palette() {
  auto palette = patchy::ui::dark_palette();
  palette.accent = QColor(0x12, 0x34, 0x56);
  palette.window_bg = QColor(0xAA, 0xBB, 0xCC, 0x80);

  const auto json =
      patchy::ui::serialize_theme_to_json(palette, patchy::ui::ColorScheme::Dark, QStringLiteral("Round Trip"));
  const auto result = patchy::ui::load_theme_from_json(json);

  CHECK(result.error.isEmpty());
  CHECK(result.warnings.isEmpty());
  CHECK(result.theme.has_value());
  CHECK(result.theme->name == QStringLiteral("Round Trip"));
  CHECK(result.theme->base == patchy::ui::ColorScheme::Dark);

  for (const auto& [name, member] : patchy::ui::theme_palette_roles()) {
    const auto original = palette.*member;
    const auto loaded = result.theme->palette.*member;
    if (original.rgba() != loaded.rgba()) {
      fprintf(stderr, "  role \"%s\" did not round-trip\n", QString(name).toUtf8().constData());
    }
    CHECK(original.rgba() == loaded.rgba());
  }
}

// A role the file never mentions keeps the declared base scheme's built-in
// value; only roles actually present in "roles" are overridden.
void ui_theme_file_missing_role_falls_back_to_base() {
  QJsonObject roles;
  roles.insert(QStringLiteral("accent"), QStringLiteral("#123456"));
  QJsonObject object;
  object.insert(QStringLiteral("base"), QStringLiteral("dark"));
  object.insert(QStringLiteral("roles"), roles);
  const auto json = QJsonDocument(object).toJson();

  const auto result = patchy::ui::load_theme_from_json(json);
  CHECK(result.error.isEmpty());
  CHECK(result.theme.has_value());
  CHECK(result.theme->palette.accent == QColor(0x12, 0x34, 0x56));
  CHECK(result.theme->palette.window_bg == patchy::ui::dark_palette().window_bg);
}

// A malformed color is a hard load error naming the offending role, not a
// warning: nothing downstream should ever see a half-loaded custom theme.
void ui_theme_file_invalid_hex_is_a_hard_error() {
  QJsonObject roles;
  roles.insert(QStringLiteral("accent"), QStringLiteral("not-a-color"));
  QJsonObject object;
  object.insert(QStringLiteral("base"), QStringLiteral("dark"));
  object.insert(QStringLiteral("roles"), roles);
  const auto json = QJsonDocument(object).toJson();

  const auto result = patchy::ui::load_theme_from_json(json);
  CHECK(!result.theme.has_value());
  CHECK(!result.error.isEmpty());
  CHECK(result.error.contains(QStringLiteral("accent")));
  CHECK(result.warnings.isEmpty());
}

// An unknown role name is forward-compatibility tolerance, not a failure: a
// theme authored against a newer build (with roles this build never heard of)
// still has to load on this one.
void ui_theme_file_unknown_role_is_a_warning_not_an_error() {
  QJsonObject roles;
  roles.insert(QStringLiteral("accent"), QStringLiteral("#123456"));
  roles.insert(QStringLiteral("totally_not_a_role"), QStringLiteral("#ffffff"));
  QJsonObject object;
  object.insert(QStringLiteral("base"), QStringLiteral("dark"));
  object.insert(QStringLiteral("roles"), roles);
  const auto json = QJsonDocument(object).toJson();

  const auto result = patchy::ui::load_theme_from_json(json);
  CHECK(result.error.isEmpty());
  CHECK(result.theme.has_value());
  CHECK(result.theme->palette.accent == QColor(0x12, 0x34, 0x56));
  CHECK(!result.warnings.isEmpty());
  CHECK(result.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("totally_not_a_role")));
}

// "base" is the one field with no fallback: anything other than the two
// tokens ThemeManager already persists is a hard error, never a guess.
void ui_theme_file_invalid_base_is_a_hard_error() {
  QJsonObject object;
  object.insert(QStringLiteral("base"), QStringLiteral("purple"));
  const auto json = QJsonDocument(object).toJson();

  const auto result = patchy::ui::load_theme_from_json(json);
  CHECK(!result.theme.has_value());
  CHECK(!result.error.isEmpty());
}

// set_custom_theme never routes through apply_resolved_scheme(), whose
// equal-scheme guard would otherwise swallow a custom theme that declares the
// same base as the scheme already active. Assert the generation still bumps,
// theme() reflects the new colors, and the photoshop_style() cache (keyed on
// theme_generation(), not on light/dark) picks the change up.
void ui_custom_theme_applies_over_matching_base_and_bumps_generation() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);

  const auto generation_before = patchy::ui::theme_generation();
  const auto style_before = patchy::ui::photoshop_style();

  patchy::ui::CustomTheme custom;
  custom.name = QStringLiteral("Distinctive");
  custom.base = patchy::ui::ColorScheme::Dark;
  custom.palette = patchy::ui::dark_palette();
  custom.palette.window_bg = QColor(1, 2, 3);

  patchy::ui::ThemeManager::instance().set_custom_theme(QStringLiteral("distinctive.patchytheme"), custom,
                                                         /*persist=*/false);

  CHECK(patchy::ui::theme_generation() > generation_before);
  CHECK(patchy::ui::active_color_scheme() == patchy::ui::ColorScheme::Dark);
  CHECK(patchy::ui::theme().window_bg == QColor(1, 2, 3));
  CHECK(patchy::ui::has_active_custom_palette());

  const auto style_after = patchy::ui::photoshop_style();
  CHECK(!style_after.isEmpty());
  CHECK(!style_after.contains(QLatin1Char('@')));
  CHECK(style_after != style_before);
}

// A built-in and a custom theme are mutually exclusive: choosing a built-in
// scheme always wins, even while a custom theme is active.
void ui_custom_theme_cleared_by_switching_to_a_builtin_scheme() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);

  patchy::ui::CustomTheme custom;
  custom.base = patchy::ui::ColorScheme::Dark;
  custom.palette = patchy::ui::dark_palette();
  patchy::ui::ThemeManager::instance().set_custom_theme(QStringLiteral("temp.patchytheme"), custom,
                                                         /*persist=*/false);
  CHECK(patchy::ui::ThemeManager::instance().active_custom_theme_id().has_value());

  patchy::ui::ThemeManager::instance().set_preference(patchy::ui::ColorSchemePreference::Light,
                                                       /*persist=*/false);

  CHECK(!patchy::ui::ThemeManager::instance().active_custom_theme_id().has_value());
  CHECK(!patchy::ui::has_active_custom_palette());
  CHECK(patchy::ui::active_color_scheme() == patchy::ui::ColorScheme::Light);
}

}  // namespace

std::vector<patchy::test::TestCase> app_shell_tests_part2() {
  return {
      {"ui_wayland_popup_anchor_fix_scales_qt_anchor_rects", ui_wayland_popup_anchor_fix_scales_qt_anchor_rects},
      {"ui_wayland_pointer_cursor_fix_keeps_the_dialog_cursor", ui_wayland_pointer_cursor_fix_keeps_the_dialog_cursor},
      {"ui_wayland_pointer_cursor_fix_covers_preferences_and_missing_fonts",
       ui_wayland_pointer_cursor_fix_covers_preferences_and_missing_fonts},
      {"ui_recent_history_checks_in_background_and_skips_network_paths", ui_recent_history_checks_in_background_and_skips_network_paths},
      {"ui_open_dialog_hides_name_filter_details", ui_open_dialog_hides_name_filter_details},
      {"ui_open_dialog_opens_every_selected_file", ui_open_dialog_opens_every_selected_file},
      {"ui_open_dialog_cancel_remembers_the_folder", ui_open_dialog_cancel_remembers_the_folder},
      {"update_manifest_parser_handles_supported_cases", update_manifest_parser_handles_supported_cases},
      {"ui_background_worker_wait_is_bounded", ui_background_worker_wait_is_bounded},
      {"ui_update_manifest_url_honors_environment_override", ui_update_manifest_url_honors_environment_override},
      {"ui_update_check_fetches_manifest_after_resolving_host", ui_update_check_fetches_manifest_after_resolving_host},
      {"ui_update_check_reports_unresolvable_host_and_drops_destroyed_owner", ui_update_check_reports_unresolvable_host_and_drops_destroyed_owner},
      {"ui_update_available_dialog_warns_to_close_patchy_before_installing", ui_update_available_dialog_warns_to_close_patchy_before_installing},
      {"update_install_location_matching_ignores_case_and_separators", update_install_location_matching_ignores_case_and_separators},
      {"ui_update_available_dialog_offers_one_click_update_for_installed_copy", ui_update_available_dialog_offers_one_click_update_for_installed_copy},
      {"ui_update_connector_restart_notice_names_the_apps", ui_update_connector_restart_notice_names_the_apps},
      {"connector_update_notice_reports_a_replaced_executable", connector_update_notice_reports_a_replaced_executable},
      {"appimage_environment_names_the_appimage_file", appimage_environment_names_the_appimage_file},
      {"ui_update_available_dialog_offers_appimage_download", ui_update_available_dialog_offers_appimage_download},
      {"update_appimage_replacement_checks_and_renames", update_appimage_replacement_checks_and_renames},
      {"ui_update_one_click_replaces_appimage_on_close", ui_update_one_click_replaces_appimage_on_close},
      {"update_installer_is_checked_again_before_it_runs", update_installer_is_checked_again_before_it_runs},
      {"ui_update_one_click_downloads_installer_and_hands_over_on_close", ui_update_one_click_downloads_installer_and_hands_over_on_close},
      {"ui_update_preference_defaults_startup_check_setting_to_enabled", ui_update_preference_defaults_startup_check_setting_to_enabled},
      {"ui_update_preference_persists_startup_check_setting", ui_update_preference_persists_startup_check_setting},
      {"ui_update_checks_can_be_switched_off", ui_update_checks_can_be_switched_off},
      {"ui_preferences_wheel_over_unfocused_control_scrolls_the_page", ui_preferences_wheel_over_unfocused_control_scrolls_the_page},
      {"ui_recent_filter_matches_file_names_and_extensions", ui_recent_filter_matches_file_names_and_extensions},
      {"ui_preferences_dialog_resizes_and_remembers_its_size", ui_preferences_dialog_resizes_and_remembers_its_size},
      {"ui_preferences_interval_combos_fit_their_longest_entry", ui_preferences_interval_combos_fit_their_longest_entry},
      {"ui_preferences_hotkey_is_ctrl_k_off_macos", ui_preferences_hotkey_is_ctrl_k_off_macos},
      {"ui_update_check_interval_gates_the_startup_check", ui_update_check_interval_gates_the_startup_check},
      {"ui_update_startup_check_honors_stored_interval", ui_update_startup_check_honors_stored_interval},
      {"ui_update_preference_persists_check_interval", ui_update_preference_persists_check_interval},
      {"ui_update_preference_offers_daily_interval", ui_update_preference_offers_daily_interval},
      {"ui_update_prompt_skip_version_silences_until_a_newer_one", ui_update_prompt_skip_version_silences_until_a_newer_one},
      {"ui_gui_scale_preference_persists_setting", ui_gui_scale_preference_persists_setting},
      {"ui_gui_scale_preference_persists_step_below_full_size", ui_gui_scale_preference_persists_step_below_full_size},
      {"ui_gui_scale_preference_falls_back_for_unknown_step", ui_gui_scale_preference_falls_back_for_unknown_step},
      {"gui_scale_percent_normalizes_to_offered_steps", gui_scale_percent_normalizes_to_offered_steps},
      {"ui_color_scheme_preference_persists_setting", ui_color_scheme_preference_persists_setting},
      {"ui_color_scheme_cancel_restores_entry_scheme", ui_color_scheme_cancel_restores_entry_scheme},
      {"ui_color_scheme_commit_after_preview_clears_stored_custom_theme", ui_color_scheme_commit_after_preview_clears_stored_custom_theme},
      {"ui_color_scheme_follow_system_tracks_style_hints", ui_color_scheme_follow_system_tracks_style_hints},
      {"ui_theme_file_round_trips_full_palette", ui_theme_file_round_trips_full_palette},
      {"ui_theme_file_missing_role_falls_back_to_base", ui_theme_file_missing_role_falls_back_to_base},
      {"ui_theme_file_invalid_hex_is_a_hard_error", ui_theme_file_invalid_hex_is_a_hard_error},
      {"ui_theme_file_unknown_role_is_a_warning_not_an_error", ui_theme_file_unknown_role_is_a_warning_not_an_error},
      {"ui_theme_file_invalid_base_is_a_hard_error", ui_theme_file_invalid_base_is_a_hard_error},
      {"ui_custom_theme_applies_over_matching_base_and_bumps_generation", ui_custom_theme_applies_over_matching_base_and_bumps_generation},
      {"ui_custom_theme_cleared_by_switching_to_a_builtin_scheme", ui_custom_theme_cleared_by_switching_to_a_builtin_scheme},
  };
}
