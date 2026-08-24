// Divide Scanned Photos: the scan-and-divide import, the Image-menu command on
// the current document, the editable region preview, the Straighten / Fix
// Perspective checkbox semantics, and the numbered folder save. All scanner
// paths run offscreen through PATCHY_FAKE_SCANNER_FILE (docs/import.md).

#include "core/document.hpp"
#include "core/pixel_buffer.hpp"
#include "ui/app_settings.hpp"
#include "ui/main_window.hpp"

#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPoint>
#include <QPushButton>
#include <QRadioButton>
#include <QRect>
#include <QScopeGuard>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariant>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

using patchy::test::ui::drag;
using patchy::test::ui::find_top_level_dialog;
using patchy::test::ui::process_events_for;
using patchy::test::ui::save_widget_artifact;
using patchy::test::ui::send_mouse;
using patchy::test::ui::SettingsValueRestorer;
using patchy::test::ui::show_window;

namespace {

// Restores and seeds every dividePhotos/* key so each test starts from the
// defaults regardless of suite order.
struct DividePhotosSettingsGuard {
  SettingsValueRestorer sensitivity{QStringLiteral("dividePhotos/sensitivity")};
  SettingsValueRestorer straighten{QStringLiteral("dividePhotos/straighten")};
  SettingsValueRestorer perspective{QStringLiteral("dividePhotos/fixPerspective")};
  SettingsValueRestorer output{QStringLiteral("dividePhotos/output")};

  DividePhotosSettingsGuard() {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("dividePhotos/sensitivity"), 50);
    settings.setValue(QStringLiteral("dividePhotos/straighten"), true);
    settings.setValue(QStringLiteral("dividePhotos/fixPerspective"), false);
    settings.setValue(QStringLiteral("dividePhotos/output"), 0);
  }
};

// A fake 800x600 platen scan at 300 ppi with three axis-aligned photos, all
// larger than the half-inch physical minimum (150 px at 300 ppi).
QString write_three_photo_scan(const QString& name) {
  std::filesystem::create_directories("test-artifacts");
  const auto path = QFileInfo(QStringLiteral("test-artifacts/") + name).absoluteFilePath();
  QImage scan(800, 600, QImage::Format_RGB888);
  scan.fill(QColor(245, 245, 245));
  QPainter painter(&scan);
  painter.fillRect(QRect(60, 60, 300, 200), QColor(60, 70, 80));
  painter.fillRect(QRect(420, 60, 300, 220), QColor(90, 40, 40));
  painter.fillRect(QRect(60, 320, 340, 240), QColor(30, 90, 60));
  painter.end();
  scan.setDotsPerMeterX(11811);  // 300 ppi
  scan.setDotsPerMeterY(11811);
  CHECK(scan.save(path));
  return path;
}

// A fake scan holding one 300x200 photo rotated 12 degrees.
QString write_rotated_photo_scan(const QString& name) {
  std::filesystem::create_directories("test-artifacts");
  const auto path = QFileInfo(QStringLiteral("test-artifacts/") + name).absoluteFilePath();
  QImage scan(700, 500, QImage::Format_RGB888);
  scan.fill(QColor(245, 245, 245));
  QPainter painter(&scan);
  painter.translate(350, 250);
  painter.rotate(12.0);
  painter.fillRect(QRect(-150, -100, 300, 200), QColor(60, 70, 80));
  painter.end();
  scan.setDotsPerMeterX(11811);
  scan.setDotsPerMeterY(11811);
  CHECK(scan.save(path));
  return path;
}

void ui_divide_photos_menu_actions_registered() {
  patchy::ui::MainWindow window;
  show_window(window);

  auto* scan_action = window.findChild<QAction*>(QStringLiteral("fileImportDivideScannedAction"));
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  // Rides the same native scanner acquisition as import/photocopy.
  CHECK(scan_action != nullptr);
  auto* import_menu = window.findChild<QMenu*>(QStringLiteral("fileImportMenu"));
  CHECK(import_menu != nullptr);
  CHECK(import_menu->actions().contains(scan_action));
  const auto* scan_command =
      window.hotkey_registry().find_command(QStringLiteral("file.import_divide_photos"));
  CHECK(scan_command != nullptr);
  CHECK(scan_command->action == scan_action);
#else
  CHECK(scan_action == nullptr);
  CHECK(window.hotkey_registry().find_command(QStringLiteral("file.import_divide_photos")) == nullptr);
#endif
  // The current-document command exists on every platform.
  auto* image_action = window.findChild<QAction*>(QStringLiteral("imageDivideScannedPhotosAction"));
  CHECK(image_action != nullptr);
  const auto* image_command = window.hotkey_registry().find_command(QStringLiteral("image.divide_photos"));
  CHECK(image_command != nullptr);
  CHECK(image_command->action == image_action);
}

void ui_scan_and_divide_opens_document_per_photo() {
  DividePhotosSettingsGuard settings_guard;
  qputenv("PATCHY_FAKE_SCANNER_FILE",
          write_three_photo_scan(QStringLiteral("ui_fake_divide_scan.png")).toUtf8());
  const auto env_guard = qScopeGuard([] { qunsetenv("PATCHY_FAKE_SCANNER_FILE"); });

  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  const auto tab_count_before = tabs->count();

  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("dividePhotosDialog"));
    CHECK(dialog != nullptr);
    auto* pane = dialog->findChild<QWidget*>(QStringLiteral("dividePhotosPreviewPane"));
    auto* straighten = dialog->findChild<QCheckBox*>(QStringLiteral("dividePhotosStraightenCheck"));
    auto* perspective = dialog->findChild<QCheckBox*>(QStringLiteral("dividePhotosPerspectiveCheck"));
    auto* open_radio = dialog->findChild<QRadioButton*>(QStringLiteral("dividePhotosOpenDocumentsRadio"));
    auto* folder_radio = dialog->findChild<QRadioButton*>(QStringLiteral("dividePhotosSaveFolderRadio"));
    auto* count_label = dialog->findChild<QLabel*>(QStringLiteral("dividePhotosCountLabel"));
    auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("dividePhotosButtonBox"));
    CHECK(pane != nullptr);
    CHECK(straighten != nullptr);
    CHECK(perspective != nullptr);
    CHECK(open_radio != nullptr);
    CHECK(folder_radio != nullptr);
    CHECK(count_label != nullptr);
    CHECK(buttons != nullptr);
    CHECK(pane->property("regionCount").toInt() == 3);
    CHECK(straighten->isChecked());
    CHECK(!perspective->isChecked());
    CHECK(open_radio->isChecked());
    CHECK(count_label->text().contains(QStringLiteral("3")));
    save_widget_artifact("ui_divide_photos_dialog", *dialog);
    saw_dialog = true;
    buttons->button(QDialogButtonBox::Ok)->click();
  });
  patchy::ui::MainWindowTestAccess::import_and_divide_from_scanner(window);
  QApplication::processEvents();
  CHECK(saw_dialog);
  // One untitled, modified document per photo; the scan itself never becomes a session.
  CHECK(tabs->count() == tab_count_before + 3);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 340);
  CHECK(document.height() == 240);
  // 11811 dots per meter is 299.9994 ppi; the photos inherit the scan's density.
  CHECK(std::abs(document.print_settings().horizontal_ppi - 300.0) < 0.01);
  CHECK(std::abs(document.print_settings().vertical_ppi - 300.0) < 0.01);
  CHECK(tabs->tabText(tabs->currentIndex()).contains(QStringLiteral("Photo 3")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window).isEmpty());
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
}

void ui_divide_dialog_region_editing_and_sensitivity() {
  DividePhotosSettingsGuard settings_guard;
  qputenv("PATCHY_FAKE_SCANNER_FILE",
          write_three_photo_scan(QStringLiteral("ui_fake_divide_edit.png")).toUtf8());
  const auto env_guard = qScopeGuard([] { qunsetenv("PATCHY_FAKE_SCANNER_FILE"); });

  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  const auto tab_count_before = tabs->count();

  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("dividePhotosDialog"));
    CHECK(dialog != nullptr);
    auto* pane = dialog->findChild<QWidget*>(QStringLiteral("dividePhotosPreviewPane"));
    auto* add_button = dialog->findChild<QPushButton*>(QStringLiteral("dividePhotosAddRegionButton"));
    auto* remove_button = dialog->findChild<QPushButton*>(QStringLiteral("dividePhotosRemoveRegionButton"));
    auto* sensitivity = dialog->findChild<QSpinBox*>(QStringLiteral("dividePhotosSensitivitySpin"));
    auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("dividePhotosButtonBox"));
    CHECK(pane != nullptr);
    CHECK(add_button != nullptr);
    CHECK(remove_button != nullptr);
    CHECK(sensitivity != nullptr);
    CHECK(buttons != nullptr);
    CHECK(pane->property("regionCount").toInt() == 3);
    // Calibrate the pane's source-to-device mapping from the first region
    // (drawn at source rect 60,60 300x200).
    const auto rects = pane->property("regionRectsView").toList();
    CHECK(rects.size() == 3);
    const QRect first = rects[0].toRect();
    CHECK(!first.isEmpty());
    const double scale = first.width() / 300.0;
    const QPointF origin(first.left() - 60.0 * scale, first.top() - 60.0 * scale);
    const auto device = [&](double source_x, double source_y) {
      return QPoint(static_cast<int>(std::lround(origin.x() + source_x * scale)),
                    static_cast<int>(std::lround(origin.y() + source_y * scale)));
    };
    // Click the first photo to select it, then remove it.
    send_mouse(*pane, QEvent::MouseButtonPress, device(210, 160), Qt::LeftButton, Qt::LeftButton,
               Qt::NoModifier);
    send_mouse(*pane, QEvent::MouseButtonRelease, device(210, 160), Qt::LeftButton, Qt::NoButton,
               Qt::NoModifier);
    CHECK(pane->property("selectedRegionIndex").toInt() == 0);
    CHECK(remove_button->isEnabled());
    remove_button->click();
    CHECK(pane->property("regionCount").toInt() == 2);
    // Add Region drops a user-owned centered region.
    add_button->click();
    CHECK(pane->property("regionCount").toInt() == 3);
    CHECK(pane->property("selectedRegionIndex").toInt() == 2);
    // Dragging on empty background lays out another region by hand.
    drag(*pane, device(560, 400), device(700, 520));
    CHECK(pane->property("regionCount").toInt() == 4);
    // Re-detection (sensitivity change) rebuilds auto regions, so the deleted
    // photo returns, while both user-owned regions survive.
    sensitivity->setValue(75);
    process_events_for(500);
    CHECK(pane->property("regionCount").toInt() == 5);
    saw_dialog = true;
    dialog->reject();
  });
  patchy::ui::MainWindowTestAccess::import_and_divide_from_scanner(window);
  QApplication::processEvents();
  CHECK(saw_dialog);
  // Cancel creates nothing.
  CHECK(tabs->count() == tab_count_before);
}

void ui_divide_current_document_uses_flattened_composite() {
  DividePhotosSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  // show_window's default document: 1024x768, white, one layer.
  auto& source_document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(source_document.width() == 1024);
  // Two photos on a second layer: the command must see the flattened composite.
  patchy::PixelBuffer overlay(1024, 768, patchy::PixelFormat::rgba8());
  overlay.clear(0);
  const auto fill = [&overlay](QRect rect, std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
      for (int x = rect.left(); x <= rect.right(); ++x) {
        auto* pixel = overlay.pixel(x, y);
        pixel[0] = red;
        pixel[1] = green;
        pixel[2] = blue;
        pixel[3] = 255;
      }
    }
  };
  fill(QRect(60, 60, 220, 160), 60, 70, 80);
  fill(QRect(360, 80, 240, 180), 90, 40, 40);
  source_document.add_pixel_layer("Photos", std::move(overlay));
  const auto source_layer_count = std::as_const(source_document).layers().size();

  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  const auto tab_count_before = tabs->count();
  const auto source_tab = tabs->currentIndex();

  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("dividePhotosDialog"));
    CHECK(dialog != nullptr);
    auto* pane = dialog->findChild<QWidget*>(QStringLiteral("dividePhotosPreviewPane"));
    auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("dividePhotosButtonBox"));
    CHECK(pane != nullptr);
    CHECK(buttons != nullptr);
    CHECK(pane->property("regionCount").toInt() == 2);
    saw_dialog = true;
    buttons->button(QDialogButtonBox::Ok)->click();
  });
  patchy::ui::MainWindowTestAccess::divide_current_document_photos(window);
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(tabs->count() == tab_count_before + 2);
  // Active document is the last photo (reading order: the right-hand one).
  CHECK(patchy::ui::MainWindowTestAccess::document(window).width() == 240);
  CHECK(patchy::ui::MainWindowTestAccess::document(window).height() == 180);
  // The source document is untouched: same canvas, both layers still there.
  tabs->setCurrentIndex(source_tab);
  QApplication::processEvents();
  auto& back = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(back.width() == 1024);
  CHECK(back.height() == 768);
  CHECK(std::as_const(back).layers().size() == source_layer_count);
}

void ui_divide_straighten_and_perspective_toggles() {
  DividePhotosSettingsGuard settings_guard;
  qputenv("PATCHY_FAKE_SCANNER_FILE",
          write_rotated_photo_scan(QStringLiteral("ui_fake_divide_rotated.png")).toUtf8());
  const auto env_guard = qScopeGuard([] { qunsetenv("PATCHY_FAKE_SCANNER_FILE"); });

  patchy::ui::MainWindow window;
  show_window(window);

  // Run 1, Straighten off: the photo is cut along its axis-aligned bounding
  // box, so the result is the rotated rect's larger footprint.
  bool saw_cut_dialog = false;
  QTimer::singleShot(0, [&saw_cut_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("dividePhotosDialog"));
    CHECK(dialog != nullptr);
    auto* pane = dialog->findChild<QWidget*>(QStringLiteral("dividePhotosPreviewPane"));
    auto* straighten = dialog->findChild<QCheckBox*>(QStringLiteral("dividePhotosStraightenCheck"));
    auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("dividePhotosButtonBox"));
    CHECK(pane != nullptr);
    CHECK(straighten != nullptr);
    CHECK(buttons != nullptr);
    CHECK(pane->property("regionCount").toInt() == 1);
    straighten->setChecked(false);
    saw_cut_dialog = true;
    buttons->button(QDialogButtonBox::Ok)->click();
  });
  patchy::ui::MainWindowTestAccess::import_and_divide_from_scanner(window);
  QApplication::processEvents();
  CHECK(saw_cut_dialog);
  {
    // 300 cos 12 + 200 sin 12 by 300 sin 12 + 200 cos 12: about 335 x 258.
    auto& document = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(std::abs(document.width() - 335) <= 8);
    CHECK(std::abs(document.height() - 258) <= 8);
  }

  // Run 2, Straighten on (plus the checkbox implication: Fix Perspective
  // forces and disables Straighten): the photo comes out level at its own size.
  bool saw_straighten_dialog = false;
  QTimer::singleShot(0, [&saw_straighten_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("dividePhotosDialog"));
    CHECK(dialog != nullptr);
    auto* straighten = dialog->findChild<QCheckBox*>(QStringLiteral("dividePhotosStraightenCheck"));
    auto* perspective = dialog->findChild<QCheckBox*>(QStringLiteral("dividePhotosPerspectiveCheck"));
    auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("dividePhotosButtonBox"));
    CHECK(straighten != nullptr);
    CHECK(perspective != nullptr);
    CHECK(buttons != nullptr);
    // Run 1 persisted Straighten off.
    CHECK(!straighten->isChecked());
    perspective->setChecked(true);
    CHECK(straighten->isChecked());
    CHECK(!straighten->isEnabled());
    perspective->setChecked(false);
    CHECK(straighten->isEnabled());
    CHECK(straighten->isChecked());
    saw_straighten_dialog = true;
    buttons->button(QDialogButtonBox::Ok)->click();
  });
  patchy::ui::MainWindowTestAccess::import_and_divide_from_scanner(window);
  QApplication::processEvents();
  CHECK(saw_straighten_dialog);
  {
    auto& document = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(std::abs(document.width() - 300) <= 6);
    CHECK(std::abs(document.height() - 200) <= 6);
  }
}

void ui_divide_save_to_folder_writes_numbered_files() {
  patchy::ui::MainWindow window;
  show_window(window);
  QTemporaryDir temp;
  CHECK(temp.isValid());

  std::vector<patchy::PixelBuffer> photos;
  for (int i = 0; i < 2; ++i) {
    patchy::PixelBuffer photo(20 + i * 10, 10 + i * 5, patchy::PixelFormat::rgba8());
    photo.clear(static_cast<std::uint8_t>(200 + i * 20));
    photos.push_back(std::move(photo));
  }
  patchy::DocumentPrintSettings print_settings;
  print_settings.horizontal_ppi = 300.0;
  print_settings.vertical_ppi = 300.0;

  const auto chosen = temp.filePath(QStringLiteral("pic_001.png"));
  CHECK(patchy::ui::MainWindowTestAccess::save_divided_photos_to_folder(
      window, photos, print_settings, chosen, QStringLiteral("png")));
  CHECK(QFileInfo::exists(temp.filePath(QStringLiteral("pic_001.png"))));
  CHECK(QFileInfo::exists(temp.filePath(QStringLiteral("pic_002.png"))));
  const QImage second(temp.filePath(QStringLiteral("pic_002.png")));
  CHECK(second.width() == 30);
  CHECK(second.height() == 15);

  // A second save into the same folder finds pic_002.png already there and
  // asks once, in aggregate, before overwriting (the image-sequence rule).
  bool prompted = false;
  QTimer::singleShot(0, [&prompted] {
    auto* dialog = find_top_level_dialog(QStringLiteral("dividePhotosOverwriteMessageBox"));
    CHECK(dialog != nullptr);
    auto* box = qobject_cast<QMessageBox*>(dialog);
    CHECK(box != nullptr);
    prompted = true;
    box->button(QMessageBox::Yes)->click();
  });
  CHECK(patchy::ui::MainWindowTestAccess::save_divided_photos_to_folder(
      window, photos, print_settings, chosen, QStringLiteral("png")));
  CHECK(prompted);
}

}  // namespace

std::vector<patchy::test::TestCase> divide_photos_tests() {
  return {
      {"ui_divide_photos_menu_actions_registered", ui_divide_photos_menu_actions_registered},
      {"ui_scan_and_divide_opens_document_per_photo", ui_scan_and_divide_opens_document_per_photo},
      {"ui_divide_dialog_region_editing_and_sensitivity",
       ui_divide_dialog_region_editing_and_sensitivity},
      {"ui_divide_current_document_uses_flattened_composite",
       ui_divide_current_document_uses_flattened_composite},
      {"ui_divide_straighten_and_perspective_toggles", ui_divide_straighten_and_perspective_toggles},
      {"ui_divide_save_to_folder_writes_numbered_files",
       ui_divide_save_to_folder_writes_numbered_files},
  };
}
