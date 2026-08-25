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
#include <QByteArray>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
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
#include <QToolButton>
#include <QVariant>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
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
  SettingsValueRestorer up_direction{QStringLiteral("dividePhotos/upDirection")};
  SettingsValueRestorer folder{QStringLiteral("dividePhotos/folder")};
  SettingsValueRestorer prefix{QStringLiteral("dividePhotos/prefix")};
  SettingsValueRestorer format{QStringLiteral("dividePhotos/format")};
  SettingsValueRestorer existing_files{QStringLiteral("dividePhotos/existingFiles")};

  DividePhotosSettingsGuard() {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("dividePhotos/sensitivity"), 50);
    settings.setValue(QStringLiteral("dividePhotos/straighten"), true);
    settings.setValue(QStringLiteral("dividePhotos/fixPerspective"), false);
    settings.setValue(QStringLiteral("dividePhotos/output"), 0);
    settings.setValue(QStringLiteral("dividePhotos/upDirection"), 0);
    settings.setValue(QStringLiteral("dividePhotos/folder"), QString());
    settings.setValue(QStringLiteral("dividePhotos/prefix"), QStringLiteral("photo_"));
    settings.setValue(QStringLiteral("dividePhotos/format"), QStringLiteral("png"));
    settings.setValue(QStringLiteral("dividePhotos/existingFiles"), 0);
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
    auto* both_radio = dialog->findChild<QRadioButton*>(QStringLiteral("dividePhotosSaveAndOpenRadio"));
    auto* up_button = dialog->findChild<QToolButton*>(QStringLiteral("dividePhotosUpDirectionUpButton"));
    auto* left_button = dialog->findChild<QToolButton*>(QStringLiteral("dividePhotosUpDirectionLeftButton"));
    auto* count_label = dialog->findChild<QLabel*>(QStringLiteral("dividePhotosCountLabel"));
    auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("dividePhotosButtonBox"));
    CHECK(pane != nullptr);
    CHECK(straighten != nullptr);
    CHECK(perspective != nullptr);
    CHECK(open_radio != nullptr);
    CHECK(folder_radio != nullptr);
    CHECK(both_radio != nullptr);
    CHECK(up_button != nullptr);
    CHECK(left_button != nullptr);
    CHECK(count_label != nullptr);
    CHECK(buttons != nullptr);
    CHECK(pane->property("regionCount").toInt() == 3);
    CHECK(straighten->isChecked());
    CHECK(!perspective->isChecked());
    CHECK(open_radio->isChecked());
    CHECK(up_button->isChecked());
    CHECK(!left_button->isChecked());
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

std::vector<patchy::PixelBuffer> make_two_test_photos() {
  std::vector<patchy::PixelBuffer> photos;
  for (int i = 0; i < 2; ++i) {
    patchy::PixelBuffer photo(20 + i * 10, 10 + i * 5, patchy::PixelFormat::rgba8());
    photo.clear(static_cast<std::uint8_t>(200 + i * 20));
    photos.push_back(std::move(photo));
  }
  return photos;
}

// Schedules a watcher that fails the test if the overwrite prompt appears (and
// clicks it away so the suite cannot hang). Returns the flag to CHECK after.
std::shared_ptr<bool> arm_unexpected_overwrite_prompt_check() {
  auto prompted = std::make_shared<bool>(false);
  QTimer::singleShot(0, [prompted] {
    auto* dialog = find_top_level_dialog(QStringLiteral("dividePhotosOverwriteMessageBox"));
    if (dialog == nullptr) {
      return;
    }
    *prompted = true;
    auto* box = qobject_cast<QMessageBox*>(dialog);
    CHECK(box != nullptr);
    box->button(QMessageBox::No)->click();
  });
  return prompted;
}

void ui_divide_save_to_folder_writes_numbered_files() {
  DividePhotosSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  QTemporaryDir temp;
  CHECK(temp.isValid());

  const auto photos = make_two_test_photos();
  patchy::DocumentPrintSettings print_settings;
  print_settings.horizontal_ppi = 300.0;
  print_settings.vertical_ppi = 300.0;

  const auto first = patchy::ui::MainWindowTestAccess::save_divided_photos_to_folder(
      window, photos, print_settings, temp.path(), QStringLiteral("pic_"), QStringLiteral("png"),
      patchy::ui::DividePhotosExistingFiles::AddNumbering);
  CHECK(first.has_value());
  CHECK(first->size() == 2);
  CHECK(QFileInfo::exists(temp.filePath(QStringLiteral("pic_001.png"))));
  CHECK(QFileInfo::exists(temp.filePath(QStringLiteral("pic_002.png"))));
  const QImage second_image(temp.filePath(QStringLiteral("pic_002.png")));
  CHECK(second_image.width() == 30);
  CHECK(second_image.height() == 15);

  // Add mode never overwrites and never asks: a second batch continues the
  // numbering after the existing files.
  const auto prompted = arm_unexpected_overwrite_prompt_check();
  const auto second = patchy::ui::MainWindowTestAccess::save_divided_photos_to_folder(
      window, photos, print_settings, temp.path(), QStringLiteral("pic_"), QStringLiteral("png"),
      patchy::ui::DividePhotosExistingFiles::AddNumbering);
  QApplication::processEvents();
  CHECK(!*prompted);
  CHECK(second.has_value());
  CHECK(second->size() == 2);
  CHECK(QFileInfo::exists(temp.filePath(QStringLiteral("pic_003.png"))));
  CHECK(QFileInfo::exists(temp.filePath(QStringLiteral("pic_004.png"))));
}

void ui_divide_add_mode_continues_numbering_past_existing() {
  DividePhotosSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  // Pre-existing unrelated files claim pic_001..pic_003; the batch must land
  // after them without touching them.
  for (int i = 1; i <= 3; ++i) {
    QFile file(temp.filePath(QStringLiteral("pic_00%1.png").arg(i)));
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("placeholder");
  }

  const auto photos = make_two_test_photos();
  patchy::DocumentPrintSettings print_settings;
  const auto prompted = arm_unexpected_overwrite_prompt_check();
  const auto saved = patchy::ui::MainWindowTestAccess::save_divided_photos_to_folder(
      window, photos, print_settings, temp.path(), QStringLiteral("pic_"), QStringLiteral("png"),
      patchy::ui::DividePhotosExistingFiles::AddNumbering);
  QApplication::processEvents();
  CHECK(!*prompted);
  CHECK(saved.has_value());
  CHECK(saved->size() == 2);
  CHECK(saved->at(0).endsWith(QStringLiteral("pic_004.png")));
  CHECK(saved->at(1).endsWith(QStringLiteral("pic_005.png")));
  CHECK(QFileInfo::exists(temp.filePath(QStringLiteral("pic_004.png"))));
  CHECK(QFileInfo::exists(temp.filePath(QStringLiteral("pic_005.png"))));
  // The placeholders were not overwritten.
  QFile placeholder(temp.filePath(QStringLiteral("pic_001.png")));
  CHECK(placeholder.open(QIODevice::ReadOnly));
  CHECK(placeholder.readAll() == QByteArray("placeholder"));
}

void ui_divide_overwrite_mode_prompts_once_naming_first_conflict() {
  DividePhotosSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  {
    QFile file(temp.filePath(QStringLiteral("pic_001.png")));
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("placeholder");
  }

  const auto photos = make_two_test_photos();
  patchy::DocumentPrintSettings print_settings;

  // Declining the single prompt (which names the first colliding file) writes
  // nothing at all.
  bool prompted = false;
  QString prompt_text;
  QTimer::singleShot(0, [&prompted, &prompt_text] {
    auto* dialog = find_top_level_dialog(QStringLiteral("dividePhotosOverwriteMessageBox"));
    CHECK(dialog != nullptr);
    auto* box = qobject_cast<QMessageBox*>(dialog);
    CHECK(box != nullptr);
    prompted = true;
    prompt_text = box->text();
    box->button(QMessageBox::No)->click();
  });
  const auto declined = patchy::ui::MainWindowTestAccess::save_divided_photos_to_folder(
      window, photos, print_settings, temp.path(), QStringLiteral("pic_"), QStringLiteral("png"),
      patchy::ui::DividePhotosExistingFiles::Overwrite);
  CHECK(prompted);
  CHECK(prompt_text.contains(QStringLiteral("pic_001.png")));
  CHECK(!declined.has_value());
  CHECK(!QFileInfo::exists(temp.filePath(QStringLiteral("pic_002.png"))));
  {
    QFile placeholder(temp.filePath(QStringLiteral("pic_001.png")));
    CHECK(placeholder.open(QIODevice::ReadOnly));
    CHECK(placeholder.readAll() == QByteArray("placeholder"));
  }

  // Accepting overwrites the whole batch from 001.
  bool accepted_prompt = false;
  QTimer::singleShot(0, [&accepted_prompt] {
    auto* dialog = find_top_level_dialog(QStringLiteral("dividePhotosOverwriteMessageBox"));
    CHECK(dialog != nullptr);
    auto* box = qobject_cast<QMessageBox*>(dialog);
    CHECK(box != nullptr);
    accepted_prompt = true;
    box->button(QMessageBox::Yes)->click();
  });
  const auto saved = patchy::ui::MainWindowTestAccess::save_divided_photos_to_folder(
      window, photos, print_settings, temp.path(), QStringLiteral("pic_"), QStringLiteral("png"),
      patchy::ui::DividePhotosExistingFiles::Overwrite);
  CHECK(accepted_prompt);
  CHECK(saved.has_value());
  CHECK(saved->size() == 2);
  const QImage first_image(temp.filePath(QStringLiteral("pic_001.png")));
  CHECK(first_image.width() == 20);
  CHECK(first_image.height() == 10);
  CHECK(QFileInfo::exists(temp.filePath(QStringLiteral("pic_002.png"))));

  // No collision, no prompt: a fresh folder saves silently in Overwrite mode.
  QTemporaryDir clean;
  CHECK(clean.isValid());
  const auto quiet = arm_unexpected_overwrite_prompt_check();
  const auto clean_saved = patchy::ui::MainWindowTestAccess::save_divided_photos_to_folder(
      window, photos, print_settings, clean.path(), QStringLiteral("pic_"), QStringLiteral("png"),
      patchy::ui::DividePhotosExistingFiles::Overwrite);
  QApplication::processEvents();
  CHECK(!*quiet);
  CHECK(clean_saved.has_value());
}

// One photo whose content lies sideways: its top edge (a red stripe) runs
// along the LEFT side of the region on the platen. Choosing "top edge points
// left" must rotate the extracted photo one clockwise quarter turn, landing
// the stripe on top.
void ui_divide_up_direction_rotates_photos() {
  DividePhotosSettingsGuard settings_guard;
  std::filesystem::create_directories("test-artifacts");
  const auto scan_path =
      QFileInfo(QStringLiteral("test-artifacts/ui_fake_divide_up_direction.png")).absoluteFilePath();
  {
    QImage scan(800, 600, QImage::Format_RGB888);
    scan.fill(QColor(245, 245, 245));
    QPainter painter(&scan);
    painter.fillRect(QRect(60, 60, 300, 200), QColor(60, 70, 80));
    painter.fillRect(QRect(60, 60, 20, 200), QColor(200, 30, 30));  // the photo's top edge
    painter.end();
    scan.setDotsPerMeterX(11811);
    scan.setDotsPerMeterY(11811);
    CHECK(scan.save(scan_path));
  }
  qputenv("PATCHY_FAKE_SCANNER_FILE", scan_path.toUtf8());
  const auto env_guard = qScopeGuard([] { qunsetenv("PATCHY_FAKE_SCANNER_FILE"); });

  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("dividePhotosDialog"));
    CHECK(dialog != nullptr);
    auto* left_button = dialog->findChild<QToolButton*>(QStringLiteral("dividePhotosUpDirectionLeftButton"));
    auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("dividePhotosButtonBox"));
    CHECK(left_button != nullptr);
    CHECK(buttons != nullptr);
    left_button->click();
    CHECK(left_button->isChecked());
    saw_dialog = true;
    buttons->button(QDialogButtonBox::Ok)->click();
  });
  patchy::ui::MainWindowTestAccess::import_and_divide_from_scanner(window);
  QApplication::processEvents();
  CHECK(saw_dialog);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 200);
  CHECK(document.height() == 300);
  const auto& pixels = std::as_const(document).layers()[0].pixels();
  // Stripe on the top rows, photo body below it.
  const auto* top = pixels.pixel(100, 5);
  CHECK(top[0] > 150 && top[1] < 90);
  const auto* body = pixels.pixel(100, 150);
  CHECK(body[0] < 90);
  // The persisted direction survives for the next batch.
  CHECK(patchy::ui::app_settings().value(QStringLiteral("dividePhotos/upDirection")).toInt() == 3);
}

void ui_divide_save_and_open_opens_unmodified_sessions() {
  DividePhotosSettingsGuard settings_guard;
  qputenv("PATCHY_FAKE_SCANNER_FILE",
          write_three_photo_scan(QStringLiteral("ui_fake_divide_save_open.png")).toUtf8());
  const auto env_guard = qScopeGuard([] { qunsetenv("PATCHY_FAKE_SCANNER_FILE"); });

  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  const auto tab_count_before = tabs->count();
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const QString folder = temp.path();

  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog, &folder] {
    auto* dialog = find_top_level_dialog(QStringLiteral("dividePhotosDialog"));
    CHECK(dialog != nullptr);
    auto* both_radio = dialog->findChild<QRadioButton*>(QStringLiteral("dividePhotosSaveAndOpenRadio"));
    auto* folder_edit = dialog->findChild<QLineEdit*>(QStringLiteral("dividePhotosFolderEdit"));
    auto* prefix_edit = dialog->findChild<QLineEdit*>(QStringLiteral("dividePhotosPrefixEdit"));
    auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("dividePhotosButtonBox"));
    CHECK(both_radio != nullptr);
    CHECK(folder_edit != nullptr);
    CHECK(prefix_edit != nullptr);
    CHECK(buttons != nullptr);
    both_radio->setChecked(true);
    folder_edit->setText(folder);
    prefix_edit->setText(QStringLiteral("both_"));
    saw_dialog = true;
    buttons->button(QDialogButtonBox::Ok)->click();
  });
  patchy::ui::MainWindowTestAccess::import_and_divide_from_scanner(window);
  QApplication::processEvents();
  CHECK(saw_dialog);

  for (int i = 1; i <= 3; ++i) {
    CHECK(QFileInfo::exists(temp.filePath(QStringLiteral("both_00%1.png").arg(i))));
  }
  CHECK(tabs->count() == tab_count_before + 3);
  // Saved-and-opened sessions carry the file's path, are not modified, and so
  // never prompt to save on close.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window)
            .endsWith(QStringLiteral("both_003.png")));
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  CHECK(tabs->tabText(tabs->currentIndex()).contains(QStringLiteral("both_003.png")));
}

void ui_divide_output_controls_follow_output_mode() {
  DividePhotosSettingsGuard settings_guard;
  qputenv("PATCHY_FAKE_SCANNER_FILE",
          write_three_photo_scan(QStringLiteral("ui_fake_divide_controls.png")).toUtf8());
  const auto env_guard = qScopeGuard([] { qunsetenv("PATCHY_FAKE_SCANNER_FILE"); });

  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("dividePhotosDialog"));
    CHECK(dialog != nullptr);
    auto* open_radio = dialog->findChild<QRadioButton*>(QStringLiteral("dividePhotosOpenDocumentsRadio"));
    auto* folder_radio = dialog->findChild<QRadioButton*>(QStringLiteral("dividePhotosSaveFolderRadio"));
    auto* folder_edit = dialog->findChild<QLineEdit*>(QStringLiteral("dividePhotosFolderEdit"));
    auto* browse_button = dialog->findChild<QPushButton*>(QStringLiteral("dividePhotosFolderBrowseButton"));
    auto* prefix_edit = dialog->findChild<QLineEdit*>(QStringLiteral("dividePhotosPrefixEdit"));
    auto* format_combo = dialog->findChild<QComboBox*>(QStringLiteral("dividePhotosFormatCombo"));
    auto* existing_combo = dialog->findChild<QComboBox*>(QStringLiteral("dividePhotosExistingCombo"));
    auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("dividePhotosButtonBox"));
    CHECK(open_radio != nullptr);
    CHECK(folder_radio != nullptr);
    CHECK(folder_edit != nullptr);
    CHECK(browse_button != nullptr);
    CHECK(prefix_edit != nullptr);
    CHECK(format_combo != nullptr);
    CHECK(existing_combo != nullptr);
    CHECK(buttons != nullptr);
    // Open mode: every save detail is grayed out; the defaults are seeded.
    CHECK(open_radio->isChecked());
    CHECK(!folder_edit->isEnabled());
    CHECK(!browse_button->isEnabled());
    CHECK(!prefix_edit->isEnabled());
    CHECK(!format_combo->isEnabled());
    CHECK(!existing_combo->isEnabled());
    CHECK(prefix_edit->text() == QStringLiteral("photo_"));
    CHECK(format_combo->currentData().toString() == QStringLiteral("png"));
    // A save mode enables them, and an empty folder gates OK.
    folder_radio->setChecked(true);
    CHECK(folder_edit->isEnabled());
    CHECK(browse_button->isEnabled());
    CHECK(prefix_edit->isEnabled());
    CHECK(format_combo->isEnabled());
    CHECK(existing_combo->isEnabled());
    folder_edit->setText(QString());
    CHECK(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
    folder_edit->setText(QStringLiteral("test-artifacts"));
    CHECK(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    saw_dialog = true;
    dialog->reject();
  });
  patchy::ui::MainWindowTestAccess::import_and_divide_from_scanner(window);
  QApplication::processEvents();
  CHECK(saw_dialog);
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
      {"ui_divide_add_mode_continues_numbering_past_existing",
       ui_divide_add_mode_continues_numbering_past_existing},
      {"ui_divide_overwrite_mode_prompts_once_naming_first_conflict",
       ui_divide_overwrite_mode_prompts_once_naming_first_conflict},
      {"ui_divide_up_direction_rotates_photos", ui_divide_up_direction_rotates_photos},
      {"ui_divide_save_and_open_opens_unmodified_sessions",
       ui_divide_save_and_open_opens_unmodified_sessions},
      {"ui_divide_output_controls_follow_output_mode",
       ui_divide_output_controls_follow_output_mode},
  };
}
