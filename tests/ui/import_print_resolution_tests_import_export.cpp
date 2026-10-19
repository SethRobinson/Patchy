#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "core/layer_metadata.hpp"
#include "core/vector_shape.hpp"
#include "core/text_warp.hpp"
#include "core/palette_presets.hpp"
#include "ui/pdf_export.hpp"
#include "formats/pdf_document_io.hpp"
#include "formats/pdf_file.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/print_dialog.hpp"
#include "ui/image_sequence_dialog.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/tile_preview_window.hpp"
#include "ui/app_settings.hpp"
#include "ui/script_engine.hpp"
#include "synthetic_dng.hpp"
#include "unicode_path_names.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QTemporaryDir>
#include <QMetaObject>
#include <QMenu>
#include <QIODevice>
#include <QPainter>
#include <QPointer>
#include <QThread>
#if defined(PATCHY_HAVE_QT_PDF)
#include <QPdfDocument>
#endif
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "import_print_resolution_test_support.hpp"

namespace {
using namespace patchy::test::ui;
using namespace patchy::test::ui::import_print_resolution_support;

void ui_single_text_layer_psb_keeps_transparency_without_mask() {
  const auto path = patchy::test::local_psd_fixture_path("PSBtest/Content.psb");
  patchy::test::skip_unless_exists(path);
  // The table-tent child: one text layer on a transparent canvas. TWO code paths used
  // to invent a layer mask Photoshop never shows (the reader adopting the composite
  // "Transparency" channel, then the flat-import alpha promotion stripping the glyph
  // alpha); the UI open path must produce neither.
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, QString::fromStdWString(path.wstring()));
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.layers().size() == 1);
  const auto& layer = document.layers().front();
  CHECK(patchy::layer_is_text(layer));
  CHECK(!layer.mask().has_value());
  // The glyph transparency stays per-pixel alpha, never stripped into a mask.
  CHECK(layer.pixels().format().channels == 4);
}

void ui_layer_context_menu_keeps_edit_styles_on_top() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::Document built(32, 24, patchy::PixelFormat::rgba8());
  built.add_pixel_layer("layer", solid_pixels(32, 24, patchy::PixelFormat::rgba8(), QColor(90, 90, 90, 255)));
  window.add_document_session(std::move(built), QStringLiteral("Menu"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr && layer_list->count() > 0);

  bool saw_menu = false;
  QString first_action_name;
  QStringList submenu_names;
  int poll_attempts = 0;
  QTimer poller;
  QObject::connect(&poller, &QTimer::timeout, [&] {
    if (++poll_attempts > 500) {
      poller.stop();
      return;
    }
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu != nullptr && menu->objectName() == QStringLiteral("layerContextMenu") && menu->isVisible()) {
        saw_menu = true;
        const auto actions = menu->actions();
        if (!actions.isEmpty()) {
          first_action_name = actions.front()->objectName();
        }
        for (auto* action : actions) {
          if (action->menu() != nullptr) {
            submenu_names << action->menu()->objectName();
          }
        }
        menu->close();
        poller.stop();
        return;
      }
    }
  });
  poller.start(10);
  QMetaObject::invokeMethod(
      &window,
      [&window, layer_list] {
        patchy::ui::MainWindowTestAccess::show_layer_context_menu(
            window, layer_list->visualItemRect(layer_list->item(0)).center());
      },
      Qt::QueuedConnection);
  QApplication::processEvents();
  for (int i = 0; i < 200 && !saw_menu && poll_attempts <= 500; ++i) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  poller.stop();
  CHECK(saw_menu);
  // Edit Layer Styles... stays the FIRST item, always; the bulky groups live in
  // submenus now.
  CHECK(first_action_name == QStringLiteral("layerBlendingOptionsAction"));
  CHECK(submenu_names.contains(QStringLiteral("layerContextStyleMenu")));
  CHECK(submenu_names.contains(QStringLiteral("layerContextNewMenu")));
  CHECK(submenu_names.contains(QStringLiteral("layerContextSmartObjectsMenu")));
  CHECK(submenu_names.contains(QStringLiteral("layerContextMaskMenu")));
}

void ui_file_import_menu_actions_registered() {
  patchy::ui::MainWindow window;
  show_window(window);

  auto* import_menu = window.findChild<QMenu*>(QStringLiteral("fileImportMenu"));
  CHECK(import_menu != nullptr);
  auto* scanner_action = window.findChild<QAction*>(QStringLiteral("fileImportScannerAction"));
  auto* photocopy_action = window.findChild<QAction*>(QStringLiteral("fileImportPhotocopyAction"));
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  // Native scanner acquisition exists on Windows and macOS and keeps one persisted id.
  CHECK(scanner_action != nullptr);
  CHECK(import_menu->actions().contains(scanner_action));
  const auto* scanner_command = window.hotkey_registry().find_command(QStringLiteral("file.import_scanner"));
  CHECK(scanner_command != nullptr);
  CHECK(scanner_command->action == scanner_action);
  // Photocopy rides the same native acquisition, so it exists exactly where the
  // scanner import does and keeps its own persisted id.
  CHECK(photocopy_action != nullptr);
  CHECK(import_menu->actions().contains(photocopy_action));
  const auto* photocopy_command = window.hotkey_registry().find_command(QStringLiteral("file.import_photocopy"));
  CHECK(photocopy_command != nullptr);
  CHECK(photocopy_command->action == photocopy_action);
#ifdef Q_OS_MACOS
  CHECK(scanner_action->text() == QStringLiteral("From &Scanner..."));
  CHECK(photocopy_action->text() == QStringLiteral("&Photocopy (Scanner to Printer)..."));
#else
  CHECK(scanner_action->text() == QStringLiteral("From &Scanner or Camera..."));
  CHECK(photocopy_action->text() == QStringLiteral("&Photocopy (Scanner or Camera to Printer)..."));
#endif
#else
  CHECK(scanner_action == nullptr);
  CHECK(window.hotkey_registry().find_command(QStringLiteral("file.import_scanner")) == nullptr);
  CHECK(photocopy_action == nullptr);
  CHECK(window.hotkey_registry().find_command(QStringLiteral("file.import_photocopy")) == nullptr);
#endif
}

void ui_scanner_import_creates_untitled_document() {
  // PATCHY_FAKE_SCANNER_FILE bypasses native acquisition so the session/cleanup plumbing
  // runs offscreen; real WIA/ImageKit acquisition needs physical hardware.
  std::filesystem::create_directories("test-artifacts");
  const auto scan_path = QFileInfo(QStringLiteral("test-artifacts/ui_fake_scan.png")).absoluteFilePath();
  {
    QImage scan(40, 30, QImage::Format_RGB888);
    scan.fill(QColor(180, 150, 120));
    // Absurd DPI: the import must clamp it to 300.
    scan.setDotsPerMeterX(400000);
    scan.setDotsPerMeterY(400000);
    CHECK(scan.save(scan_path));
  }
  qputenv("PATCHY_FAKE_SCANNER_FILE", scan_path.toUtf8());
  const auto env_guard = qScopeGuard([] { qunsetenv("PATCHY_FAKE_SCANNER_FILE"); });

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::import_from_scanner(window);
  QApplication::processEvents();

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 40);
  CHECK(document.height() == 30);
  CHECK(document.print_settings().horizontal_ppi == 300.0);
  CHECK(document.print_settings().vertical_ppi == 300.0);
  // Untitled + modified: the tab shows "Scanned Image" with no backing path, so Save must
  // route to Save As and closing warns.
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  CHECK(tabs->tabText(tabs->currentIndex()).contains(QStringLiteral("Scanned Image")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window).isEmpty());
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  // Fake fixtures are retained; only files returned by native acquisition are temporary.
  CHECK(QFileInfo::exists(scan_path));
}

void ui_photocopy_dialog_previews_clipping_at_actual_size() {
  std::filesystem::create_directories("test-artifacts");
  // Pin the ruler unit: the photocopy size labels follow it (inches for px).
  SettingsValueRestorer unit_restorer(QStringLiteral("view/rulerUnits"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("view/rulerUnits"), QStringLiteral("px"));
  }
  const auto write_fake_scan = [](const QString& name, int width, int height) {
    const auto path = QFileInfo(QStringLiteral("test-artifacts/") + name).absoluteFilePath();
    QImage scan(width, height, QImage::Format_RGB888);
    scan.fill(QColor(120, 160, 200));
    // 11811 dots/m = 300 ppi, so pixel sizes below read directly as inches.
    scan.setDotsPerMeterX(11811);
    scan.setDotsPerMeterY(11811);
    CHECK(scan.save(path));
    return path;
  };
  const auto env_guard = qScopeGuard([] { qunsetenv("PATCHY_FAKE_SCANNER_FILE"); });

  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  const auto tab_count_before = tabs->count();

  // A 6000x1200 px scan at 300 ppi is 20 x 4 inches: too wide for any sheet paper in
  // either orientation, but landscape always loses less of it, so the dialog must
  // auto-rotate the paper AND warn about the cut-off parts regardless of which printer
  // and paper size this machine defaults to.
  qputenv("PATCHY_FAKE_SCANNER_FILE", write_fake_scan(QStringLiteral("ui_fake_photocopy_large.png"), 6000, 1200).toUtf8());
  bool saw_clipped_dialog = false;
  QTimer::singleShot(0, [&saw_clipped_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("photocopyDialog"));
    CHECK(dialog != nullptr);
    auto* printer = dialog->findChild<QComboBox*>(QStringLiteral("photocopyPrinterCombo"));
    auto* copies = dialog->findChild<QSpinBox*>(QStringLiteral("photocopyCopiesSpin"));
    auto* print_button = dialog->findChild<QPushButton*>(QStringLiteral("photocopyPrintButton"));
    auto* scan_size = dialog->findChild<QLabel*>(QStringLiteral("photocopyScanSizeLabel"));
    auto* paper_size = dialog->findChild<QLabel*>(QStringLiteral("photocopyPaperSizeLabel"));
    auto* warning = dialog->findChild<QLabel*>(QStringLiteral("photocopyClipWarningLabel"));
    auto* preview = dialog->findChild<QWidget*>(QStringLiteral("photocopyPreviewPane"));
    CHECK(printer != nullptr);
    CHECK(copies != nullptr);
    CHECK(print_button != nullptr);
    CHECK(scan_size != nullptr);
    CHECK(paper_size != nullptr);
    CHECK(warning != nullptr);
    CHECK(preview != nullptr);
    CHECK(printer->count() >= 1);
    // Print is gated on a usable printer, exactly like the print dialog.
    CHECK(print_button->isEnabled() == printer->isEnabled());
    // Copies is the one adjustable output option; there is deliberately no scale control
    // anywhere in the dialog (the copy must match the original's physical size).
    CHECK(copies->value() == 1);
    CHECK(copies->minimum() == 1);
    CHECK(copies->maximum() >= 99);
    copies->setValue(3);
    CHECK(copies->value() == 3);
    CHECK(dialog->findChild<QDoubleSpinBox*>(QStringLiteral("printScalePercentSpin")) == nullptr);
    // Actual size: the label reports the scan's true physical size.
    CHECK(scan_size->text().contains(QStringLiteral("20.00")));
    CHECK(scan_size->text().contains(QStringLiteral("4.00")));
    // The 20-inch-wide scan forces landscape paper regardless of the installed paper size.
    const auto paper_parts = paper_size->text().split(QStringLiteral(" x "));
    CHECK(paper_parts.size() == 2);
    CHECK(paper_parts[0].toDouble() >= paper_parts[1].split(QLatin1Char(' ')).first().toDouble());
    CHECK(warning->isVisible());
    // Anchored to the paper's top-right corner (the platen registration corner): the
    // overflow of a too-wide scan hangs off the LEFT edge, so the offset is negative in
    // x and zero in y.
    const auto anchored = preview->property("scanOffsetInches").toPointF();
    CHECK(anchored.x() < -0.5);
    CHECK(std::abs(anchored.y()) < 0.01);
    save_widget_artifact("ui_photocopy_dialog_clipped", *dialog);
    // Dragging inside the crop rect (away from its resize handles) slides the scan
    // across the paper so the user picks which part prints. Mid-drag the view
    // transform is frozen, so the crop follows the pointer 1:1 on screen; the
    // fit-to-content refit used to swallow the motion and make the page slide the
    // other way instead.
    const auto crop_view_before = preview->property("cropRectView").toRect();
    CHECK(!crop_view_before.isEmpty());
    const QPoint move_grip(crop_view_before.center().x() + crop_view_before.width() / 4,
                           crop_view_before.center().y());
    send_mouse(*preview, QEvent::MouseButtonPress, move_grip, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    send_mouse(*preview, QEvent::MouseMove, move_grip + QPoint(40, 20), Qt::NoButton, Qt::LeftButton,
               Qt::NoModifier);
    const auto mid_drag_view = preview->property("cropRectView").toRect();
    CHECK(std::abs(mid_drag_view.center().x() - crop_view_before.center().x() - 40) <= 2);
    CHECK(std::abs(mid_drag_view.center().y() - crop_view_before.center().y() - 20) <= 2);
    send_mouse(*preview, QEvent::MouseButtonRelease, move_grip + QPoint(40, 20), Qt::LeftButton, Qt::NoButton,
               Qt::NoModifier);
    const auto dragged = preview->property("scanOffsetInches").toPointF();
    CHECK(dragged.x() > anchored.x() + 0.1);
    CHECK(dragged.y() > anchored.y());
    // Dragging a corner handle resizes the crop: only that part prints (still at
    // actual size), e.g. just the driver's license on the platen.
    const auto crop_before = preview->property("cropRectPixels").toRect();
    CHECK(crop_before == QRect(0, 0, 6000, 1200));
    const auto crop_view = preview->property("cropRectView").toRect();
    CHECK(!crop_view.isEmpty());
    drag(*preview, crop_view.bottomRight(), crop_view.bottomRight() - QPoint(40, 10));
    const auto crop_after = preview->property("cropRectPixels").toRect();
    CHECK(crop_after.width() < crop_before.width());
    CHECK(crop_after.height() < crop_before.height());
    CHECK(crop_after.topLeft() == QPoint(0, 0));
    save_widget_artifact("ui_photocopy_dialog_cropped", *dialog);
    saw_clipped_dialog = true;
    dialog->reject();
  });
  patchy::ui::MainWindowTestAccess::photocopy_from_scanner(window);
  QApplication::processEvents();
  CHECK(saw_clipped_dialog);
  // A photocopy is a throwaway: cancelling (or printing) never creates a document session.
  CHECK(tabs->count() == tab_count_before);

  // A 1 x 0.8 inch scan fits every paper: no warning, portrait stays.
  qputenv("PATCHY_FAKE_SCANNER_FILE", write_fake_scan(QStringLiteral("ui_fake_photocopy_small.png"), 300, 240).toUtf8());
  bool saw_fitting_dialog = false;
  QTimer::singleShot(0, [&saw_fitting_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("photocopyDialog"));
    CHECK(dialog != nullptr);
    auto* scan_size = dialog->findChild<QLabel*>(QStringLiteral("photocopyScanSizeLabel"));
    auto* paper_size = dialog->findChild<QLabel*>(QStringLiteral("photocopyPaperSizeLabel"));
    auto* warning = dialog->findChild<QLabel*>(QStringLiteral("photocopyClipWarningLabel"));
    auto* preview = dialog->findChild<QWidget*>(QStringLiteral("photocopyPreviewPane"));
    CHECK(scan_size != nullptr);
    CHECK(paper_size != nullptr);
    CHECK(warning != nullptr);
    CHECK(preview != nullptr);
    CHECK(scan_size->text().contains(QStringLiteral("1.00")));
    CHECK(scan_size->text().contains(QStringLiteral("0.80")));
    const auto paper_parts = paper_size->text().split(QStringLiteral(" x "));
    CHECK(paper_parts.size() == 2);
    CHECK(paper_parts[0].toDouble() <= paper_parts[1].split(QLatin1Char(' ')).first().toDouble());
    CHECK(!warning->isVisible());
    // A small scan anchors top-right too: positive x offset (paper is wider), zero y.
    const auto anchored = preview->property("scanOffsetInches").toPointF();
    CHECK(anchored.x() > 0.5);
    CHECK(std::abs(anchored.y()) < 0.01);
    saw_fitting_dialog = true;
    dialog->reject();
  });
  patchy::ui::MainWindowTestAccess::photocopy_from_scanner(window);
  QApplication::processEvents();
  CHECK(saw_fitting_dialog);
  CHECK(tabs->count() == tab_count_before);
}

void ui_aseprite_open_adopts_palette_and_builds_layer_tree() {
  SettingsValueRestorer policy_restorer(QStringLiteral("imports/adoptIndexedPalette"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/adoptIndexedPalette"), QStringLiteral("always"));
  }
  const auto path = QString::fromStdWString(
      patchy::test::committed_format_fixture_path("aseprite", "aseprite-indexed-frames.aseprite").wstring());
  CHECK(QFileInfo::exists(path));

  patchy::ui::MainWindow window;
  show_window(window);

  // The multi-frame fixture raises an import note; it lands in the status bar
  // (no popup unless imports/showPsdWarningsAndInfo is enabled).
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("first frame")));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 16);
  CHECK(document.layers().size() == 1);
  CHECK(document.layers().front().name() == "Pixels");
  // The 4-color Aseprite palette was adopted into palette mode.
  CHECK(document.palette_editing().has_value());
  CHECK(document.palette_editing()->palette.colors.size() == 4);
}

void ui_export_scale_writes_nearest_neighbor_pixels() {
  std::filesystem::create_directories("test-artifacts");
  patchy::Document document(6, 4, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(6, 4, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < 4; ++y) {
    for (std::int32_t x = 0; x < 6; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x * 40);
      px[1] = static_cast<std::uint8_t>(y * 60);
      px[2] = static_cast<std::uint8_t>(255 - x * 30);
    }
  }
  document.add_pixel_layer("Art", pixels);
  patchy::ui::ImageSaveOptions options;
  options.export_scale = 2;
  const auto path = QStringLiteral("test-artifacts/ui_export_scaled.png");
  patchy::ui::write_flat_image_file(document, path, QStringLiteral("png"), options);

  QImageReader reader(path);
  const auto image = reader.read().convertToFormat(QImage::Format_RGB888);
  CHECK(image.width() == 12);
  CHECK(image.height() == 8);
  for (std::int32_t y = 0; y < 4; ++y) {
    for (std::int32_t x = 0; x < 6; ++x) {
      const auto* expected = pixels.pixel(x, y);
      // Every source pixel becomes an exact 2x2 block (nearest neighbor, no filtering).
      for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
          const auto actual = image.pixelColor(x * 2 + dx, y * 2 + dy);
          CHECK(actual.red() == expected[0]);
          CHECK(actual.green() == expected[1]);
          CHECK(actual.blue() == expected[2]);
        }
      }
    }
  }
}

void ui_png8_export_scaled_stays_indexed() {
  std::filesystem::create_directories("test-artifacts");
  patchy::Document document(8, 8, patchy::PixelFormat::rgb8());
  const auto* preset = patchy::find_builtin_palette_preset("gameboy");
  CHECK(preset != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  editing.palette_revision = 1;
  document.palette_editing() = editing;
  patchy::PixelBuffer pixels(8, 8, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < 8; ++y) {
    for (std::int32_t x = 0; x < 8; ++x) {
      const auto& color = preset->colors[static_cast<std::size_t>(x % 4)];
      auto* px = pixels.pixel(x, y);
      px[0] = color.red;
      px[1] = color.green;
      px[2] = color.blue;
    }
  }
  document.add_pixel_layer("Pixels", std::move(pixels));
  patchy::ui::ImageSaveOptions options;
  options.export_scale = 4;
  const auto path = QStringLiteral("test-artifacts/ui_export_scaled_indexed.png");
  patchy::ui::write_flat_image_file(document, path, QStringLiteral("png"), options);

  QImageReader reader(path);
  const auto image = reader.read();
  CHECK(image.width() == 32);
  CHECK(image.height() == 32);
  // The scaled export must still hit the indexed PNG-8 path with the document palette.
  CHECK(image.format() == QImage::Format_Indexed8);
  CHECK(image.colorCount() <= 5);
  const auto rgb = image.convertToFormat(QImage::Format_RGB888);
  for (std::int32_t x = 0; x < 32; ++x) {
    const auto& expected = preset->colors[static_cast<std::size_t>((x / 4) % 4)];
    const auto actual = rgb.pixelColor(x, 16);
    CHECK(actual.red() == expected.red);
    CHECK(actual.green() == expected.green);
    CHECK(actual.blue() == expected.blue);
  }
}

// The export transforms through write_flat_image_file with explicit options, read back from
// the written PNG so every assertion covers the whole writer path.
void ui_export_trim_transparent_crops_to_visible_alpha() {
  std::filesystem::create_directories("test-artifacts");
  patchy::Document document(10, 8, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(10, 8, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  for (std::int32_t y = 2; y < 5; ++y) {
    for (std::int32_t x = 3; x < 7; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 200;
      px[1] = 30;
      px[2] = 40;
      px[3] = 255;
    }
  }
  document.add_pixel_layer("Sprite", std::move(pixels));
  patchy::ui::ImageSaveOptions options;
  options.export_trim_transparent = true;
  std::vector<std::string> notices;
  const auto path = QStringLiteral("test-artifacts/ui_export_trimmed.png");
  patchy::ui::write_flat_image_file(document, path, QStringLiteral("png"), options, &notices);
  const auto image = QImage(path).convertToFormat(QImage::Format_RGBA8888);
  CHECK(image.width() == 4);
  CHECK(image.height() == 3);
  CHECK(image.pixelColor(0, 0) == QColor(200, 30, 40, 255));
  CHECK(image.pixelColor(3, 2) == QColor(200, 30, 40, 255));
  CHECK(notices.empty());

  // Nothing visible: the canvas is kept whole and the writer says so.
  patchy::Document empty(5, 5, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer clear(5, 5, patchy::PixelFormat::rgba8());
  clear.clear(0);
  empty.add_pixel_layer("Empty", std::move(clear));
  notices.clear();
  const auto empty_path = QStringLiteral("test-artifacts/ui_export_trimmed_empty.png");
  patchy::ui::write_flat_image_file(empty, empty_path, QStringLiteral("png"), options, &notices);
  const QImage kept(empty_path);
  CHECK(kept.width() == 5);
  CHECK(kept.height() == 5);
  CHECK(notices.size() == 1);
  CHECK(!notices.empty() && notices.front().find("kept the full canvas") != std::string::npos);
}

void ui_export_resize_resamples_bilinear_to_target() {
  std::filesystem::create_directories("test-artifacts");
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(2, 2, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < 2; ++y) {
    for (std::int32_t x = 0; x < 2; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x == 0 ? 0 : 100);
      px[1] = 0;
      px[2] = 0;
    }
  }
  document.add_pixel_layer("Ramp", std::move(pixels));
  patchy::ui::ImageSaveOptions options;
  options.export_width = 4;
  options.export_height = 2;
  const auto path = QStringLiteral("test-artifacts/ui_export_resized.png");
  patchy::ui::write_flat_image_file(document, path, QStringLiteral("png"), options);
  const auto image = QImage(path).convertToFormat(QImage::Format_RGB888);
  CHECK(image.width() == 4);
  CHECK(image.height() == 2);
  // Bilinear with clamped edges (the Image Size resampler): source x = (x + 0.5) / 2 - 0.5
  // gives 0, 25, 75, 100 exactly.
  const int expected_red[4] = {0, 25, 75, 100};
  for (int x = 0; x < 4; ++x) {
    CHECK(image.pixelColor(x, 0).red() == expected_red[x]);
    CHECK(image.pixelColor(x, 1).red() == expected_red[x]);
  }

  // One zero side follows the aspect.
  options.export_height = 0;
  const auto aspect_path = QStringLiteral("test-artifacts/ui_export_resized_aspect.png");
  patchy::ui::write_flat_image_file(document, aspect_path, QStringLiteral("png"), options);
  const QImage aspect(aspect_path);
  CHECK(aspect.width() == 4);
  CHECK(aspect.height() == 4);
}

void ui_export_fill_transparent_mattes_over_background() {
  std::filesystem::create_directories("test-artifacts");
  patchy::Document document(2, 1, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(2, 1, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  auto* half = pixels.pixel(0, 0);
  half[0] = 200;
  half[1] = 100;
  half[2] = 0;
  half[3] = 128;
  document.add_pixel_layer("Half", std::move(pixels));
  patchy::ui::ImageSaveOptions options;
  options.export_fill_transparent = true;
  options.export_background_color = QColor(10, 20, 250);
  const auto path = QStringLiteral("test-artifacts/ui_export_matted.png");
  patchy::ui::write_flat_image_file(document, path, QStringLiteral("png"), options);
  const auto image = QImage(path).convertToFormat(QImage::Format_RGBA8888);
  CHECK(image.width() == 2);
  // Straight-alpha over with the writer's rounding: (200 * 128 + 10 * 127 + 127) / 255 = 105.
  CHECK(image.pixelColor(0, 0) == QColor(105, 60, 125, 255));
  CHECK(image.pixelColor(1, 0) == QColor(10, 20, 250, 255));

  // A palette-mode document stays an indexed PNG-8, now without a transparent slot.
  patchy::Document indexed(4, 4, patchy::PixelFormat::rgba8());
  const auto* preset = patchy::find_builtin_palette_preset("gameboy");
  CHECK(preset != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  editing.palette_revision = 1;
  indexed.palette_editing() = editing;
  patchy::PixelBuffer indexed_pixels(4, 4, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 4; ++y) {
    for (std::int32_t x = 0; x < 4; ++x) {
      const auto& color = preset->colors[static_cast<std::size_t>(x)];
      auto* px = indexed_pixels.pixel(x, y);
      px[0] = color.red;
      px[1] = color.green;
      px[2] = color.blue;
      px[3] = (x == 0 && y == 0) ? 0 : 255;
    }
  }
  indexed.add_pixel_layer("Pixels", std::move(indexed_pixels));
  options.export_background_color = QColor(preset->colors[3].red, preset->colors[3].green, preset->colors[3].blue);
  const auto indexed_path = QStringLiteral("test-artifacts/ui_export_matted_indexed.png");
  patchy::ui::write_flat_image_file(indexed, indexed_path, QStringLiteral("png"), options);
  const QImage indexed_image(indexed_path);
  CHECK(indexed_image.format() == QImage::Format_Indexed8);
  CHECK(indexed_image.colorCount() <= 4);
  const auto rgba = indexed_image.convertToFormat(QImage::Format_RGBA8888);
  CHECK(rgba.pixelColor(0, 0).alpha() == 255);
  CHECK(rgba.pixelColor(0, 0).red() == preset->colors[3].red);
}

void ui_export_transforms_apply_trim_resize_scale_matte_in_order() {
  std::filesystem::create_directories("test-artifacts");
  // Row 1 holds two opaque red pixels around a transparent one; everything else is clear.
  patchy::Document document(7, 3, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(7, 3, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  for (const std::int32_t x : {2, 4}) {
    auto* px = pixels.pixel(x, 1);
    px[0] = 252;
    px[3] = 252;
  }
  document.add_pixel_layer("Dots", std::move(pixels));
  patchy::ui::ImageSaveOptions options;
  options.export_trim_transparent = true;
  // The target describes the full 7x3 canvas; the 3x1 trim scales by the same 2x factor.
  options.export_width = 14;
  options.export_height = 6;
  options.export_scale = 2;
  options.export_fill_transparent = true;
  options.export_background_color = QColor(0, 0, 255);
  const auto path = QStringLiteral("test-artifacts/ui_export_transform_order.png");
  patchy::ui::write_flat_image_file(document, path, QStringLiteral("png"), options);
  const auto image = QImage(path).convertToFormat(QImage::Format_RGBA8888);
  CHECK(image.width() == 12);
  CHECK(image.height() == 4);
  // trim -> 3x1 [red, clear, red]; bilinear to 6x2 -> alpha 252, 189, 63, 63, 189, 252 on
  // both rows, and premultiplied interpolation (docs/resampling.md) keeps the fringe pure
  // red (252) at that reduced coverage; 2x replication; then the matte over blue:
  // (252 * a + 255 * (255 - a) + 127) / 255 for blue, (252 * a + 127) / 255 for red.
  // Matting before the resize would blend the opaque blue into the fringe colors instead
  // of thinning red over blue; skipping the trim would give a mostly blue image.
  const QColor expected[6] = {QColor(249, 0, 3),  QColor(187, 0, 66), QColor(62, 0, 192),
                              QColor(62, 0, 192), QColor(187, 0, 66), QColor(249, 0, 3)};
  for (int x = 0; x < 12; ++x) {
    for (int y = 0; y < 4; ++y) {
      CHECK(image.pixelColor(x, y) == expected[x / 2]);
    }
  }
}

void ui_export_option_defaults_leave_output_untransformed() {
  std::filesystem::create_directories("test-artifacts");
  patchy::Document document(6, 4, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(6, 4, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  for (std::int32_t y = 1; y < 3; ++y) {
    for (std::int32_t x = 1; x < 5; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 255;
      px[3] = 255;
    }
  }
  pixels.pixel(2, 1)[3] = 128;
  document.add_pixel_layer("Art", std::move(pixels));
  const patchy::ui::ImageSaveOptions defaults;
  const auto path = QStringLiteral("test-artifacts/ui_export_defaults.png");
  patchy::ui::write_flat_image_file(document, path, QStringLiteral("png"), defaults);
  const auto image = QImage(path).convertToFormat(QImage::Format_RGBA8888);
  CHECK(image.width() == 6);
  CHECK(image.height() == 4);
  CHECK(image.pixelColor(0, 0).alpha() == 0);
  CHECK(image.pixelColor(2, 1).alpha() == 128);
  CHECK(image.pixelColor(2, 1).red() == 255);
}

void ui_sprite_sheet_export_grid_layout_and_padding() {
  // 3 visible layers + 1 hidden: the sheet holds exactly the visible ones in grid order.
  patchy::Document document(10, 6, patchy::PixelFormat::rgba8());
  const std::array<QColor, 3> colors = {QColor(200, 30, 30), QColor(30, 200, 30), QColor(30, 30, 200)};
  for (int i = 0; i < 3; ++i) {
    document.add_pixel_layer(("Frame " + std::to_string(i + 1)).c_str(),
                             solid_pixels(10, 6, patchy::PixelFormat::rgba8(), colors[static_cast<std::size_t>(i)]));
  }
  {
    patchy::Layer hidden(document.allocate_layer_id(), "Hidden",
                         solid_pixels(10, 6, patchy::PixelFormat::rgba8(), QColor(255, 255, 0)));
    hidden.set_visible(false);
    document.add_layer(std::move(hidden));
  }
  patchy::ui::SpriteSheetExportOptions options;
  options.columns = 2;
  options.padding = 3;
  options.transparent_background = true;
  const auto sheet = patchy::ui::compose_sprite_sheet(document, options);
  // 2 columns x 2 rows: width = 2*10 + 3*3, height = 2*6 + 3*3.
  CHECK(sheet.width() == 29);
  CHECK(sheet.height() == 21);
  CHECK(sheet.pixelColor(0, 0).alpha() == 0);  // padding stays transparent
  CHECK(sheet.pixelColor(3 + 5, 3 + 3) == colors[0]);
  CHECK(sheet.pixelColor(3 + 10 + 3 + 5, 3 + 3) == colors[1]);
  CHECK(sheet.pixelColor(3 + 5, 3 + 6 + 3 + 3) == colors[2]);

  // The options dialog round trip.
  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("spriteSheetExportDialog"));
    CHECK(dialog != nullptr);
    dialog->findChild<QSpinBox*>(QStringLiteral("spriteSheetColumnsSpin"))->setValue(5);
    dialog->findChild<QSpinBox*>(QStringLiteral("spriteSheetPaddingSpin"))->setValue(7);
    dialog->findChild<QCheckBox*>(QStringLiteral("spriteSheetTransparentCheck"))->setChecked(false);
    saw_dialog = true;
    dialog->accept();
  });
  const auto chosen = patchy::ui::prompt_sprite_sheet_export_options(nullptr, 9);
  CHECK(saw_dialog);
  CHECK(chosen.has_value());
  CHECK(chosen->columns == 5);
  CHECK(chosen->padding == 7);
  CHECK(!chosen->transparent_background);
}

void ui_sprite_sheet_import_slices_cells_into_layers() {
  // A 2x2 sheet of 8x6 cells with margin 2 and spacing 1; one cell left empty.
  QImage sheet(2 + 8 + 1 + 8 + 2, 2 + 6 + 1 + 6 + 2, QImage::Format_RGBA8888);
  sheet.fill(Qt::transparent);
  QPainter painter(&sheet);
  painter.fillRect(2, 2, 8, 6, QColor(200, 30, 30));
  painter.fillRect(2 + 8 + 1, 2, 8, 6, QColor(30, 200, 30));
  painter.fillRect(2, 2 + 6 + 1, 8, 6, QColor(30, 30, 200));
  painter.end();  // bottom-right cell stays empty

  patchy::ui::SpriteSheetImportOptions options;
  options.cell_width = 8;
  options.cell_height = 6;
  options.margin = 2;
  options.spacing = 1;
  const auto sliced = patchy::ui::slice_sprite_sheet(sheet, options, QStringLiteral("Frame %1"));
  CHECK(sliced.has_value());
  CHECK(sliced->width() == 8);
  CHECK(sliced->height() == 6);
  CHECK(sliced->layers().size() == 3);  // the empty cell is skipped
  CHECK(sliced->layers()[0].name() == "Frame 1");
  CHECK(sliced->layers()[0].visible());
  CHECK(!sliced->layers()[1].visible());
  CHECK(sliced->layers()[0].pixels().pixel(4, 3)[0] == 200);
  CHECK(sliced->layers()[1].pixels().pixel(4, 3)[1] == 200);
  CHECK(sliced->layers()[2].pixels().pixel(4, 3)[2] == 200);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("spriteSheetImportDialog"));
    CHECK(dialog != nullptr);
    dialog->findChild<QSpinBox*>(QStringLiteral("spriteSheetCellWidthSpin"))->setValue(8);
    dialog->findChild<QSpinBox*>(QStringLiteral("spriteSheetCellHeightSpin"))->setValue(6);
    dialog->findChild<QSpinBox*>(QStringLiteral("spriteSheetMarginSpin"))->setValue(2);
    dialog->findChild<QSpinBox*>(QStringLiteral("spriteSheetSpacingSpin"))->setValue(1);
    auto* label = dialog->findChild<QLabel*>(QStringLiteral("spriteSheetCountLabel"));
    CHECK(label != nullptr);
    CHECK(label->text().contains(QStringLiteral("= 4")));
    saw_dialog = true;
    dialog->accept();
  });
  const auto chosen = patchy::ui::prompt_sprite_sheet_import_options(nullptr, sheet.size());
  CHECK(saw_dialog);
  CHECK(chosen.has_value());
  CHECK(chosen->cell_width == 8);
  CHECK(chosen->spacing == 1);
}

void ui_image_sequence_ordering_and_numbered_expansion() {
  // Natural ordering: numeric runs compare by value, not lexically.
  const auto sorted = patchy::ui::sorted_sequence_paths(
      {QStringLiteral("d:/x/crap10.bmp"), QStringLiteral("d:/x/crap1.bmp"), QStringLiteral("d:/x/crap2.bmp")});
  CHECK(sorted == QStringList({QStringLiteral("d:/x/crap1.bmp"), QStringLiteral("d:/x/crap2.bmp"),
                               QStringLiteral("d:/x/crap10.bmp")}));

  QTemporaryDir dir;
  CHECK(dir.isValid());
  const auto write_png = [&dir](const QString& name) {
    QImage image(4, 4, QImage::Format_RGBA8888);
    image.fill(QColor(120, 40, 40));
    CHECK(image.save(dir.filePath(name)));
  };
  write_png(QStringLiteral("walk_001.png"));
  write_png(QStringLiteral("walk_002.png"));
  write_png(QStringLiteral("walk_010.png"));
  write_png(QStringLiteral("walk_x.png"));  // non-digit remainder: not part of the run
  write_png(QStringLiteral("other.png"));

  // One numbered file stands for the whole natural-sorted sibling run.
  const auto run = patchy::ui::expand_numbered_sequence(dir.filePath(QStringLiteral("walk_002.png")));
  CHECK(run.size() == 3);
  CHECK(QFileInfo(run[0]).fileName() == QStringLiteral("walk_001.png"));
  CHECK(QFileInfo(run[1]).fileName() == QStringLiteral("walk_002.png"));
  CHECK(QFileInfo(run[2]).fileName() == QStringLiteral("walk_010.png"));

  // A file without a trailing number expands to just itself.
  const auto single = patchy::ui::expand_numbered_sequence(dir.filePath(QStringLiteral("other.png")));
  CHECK(single == QStringList(dir.filePath(QStringLiteral("other.png"))));
}

void ui_image_sequence_import_builds_layers() {
  QTemporaryDir dir;
  CHECK(dir.isValid());
  // Three differently sized frames: the canvas is the max in each dimension and
  // smaller frames top-left align.
  const auto write_png = [&dir](const QString& name, int width, int height, QColor color) {
    QImage image(width, height, QImage::Format_RGBA8888);
    image.fill(color);
    CHECK(image.save(dir.filePath(name)));
  };
  write_png(QStringLiteral("a.png"), 8, 6, QColor(200, 30, 30));
  write_png(QStringLiteral("b.png"), 4, 10, QColor(30, 200, 30));
  write_png(QStringLiteral("c.png"), 6, 5, QColor(30, 30, 200));
  const QStringList paths = {dir.filePath(QStringLiteral("a.png")), dir.filePath(QStringLiteral("b.png")),
                             dir.filePath(QStringLiteral("c.png"))};

  QString error;
  const auto imported = patchy::ui::document_from_image_sequence(paths, &error);
  CHECK(error.isEmpty());
  CHECK(imported.has_value());
  CHECK(imported->width() == 8);
  CHECK(imported->height() == 10);
  CHECK(imported->layers().size() == 3);
  CHECK(imported->layers()[0].name() == "a");
  CHECK(imported->layers()[1].name() == "b");
  CHECK(imported->layers()[2].name() == "c");
  CHECK(imported->layers()[0].visible());
  CHECK(!imported->layers()[1].visible());
  CHECK(!imported->layers()[2].visible());
  // Frame pixels sit at the top-left; the canvas area outside each frame stays transparent.
  CHECK(imported->layers()[0].pixels().pixel(7, 5)[0] == 200);
  CHECK(imported->layers()[0].pixels().pixel(7, 9)[3] == 0);
  CHECK(imported->layers()[1].pixels().pixel(0, 9)[1] == 200);
  CHECK(imported->layers()[1].pixels().pixel(7, 0)[3] == 0);

  // An unreadable file aborts the import with its name in the error.
  const auto failed =
      patchy::ui::document_from_image_sequence({dir.filePath(QStringLiteral("missing.png"))}, &error);
  CHECK(!failed.has_value());
  CHECK(error.contains(QStringLiteral("missing.png")));

  // The confirmation dialog lists the ordered files and states the canvas size.
  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("imageSequenceImportDialog"));
    CHECK(dialog != nullptr);
    auto* list = dialog->findChild<QListWidget*>(QStringLiteral("imageSequenceFileList"));
    CHECK(list != nullptr);
    CHECK(list->count() == 3);
    CHECK(list->item(0)->text() == QStringLiteral("a.png"));
    CHECK(list->item(2)->text() == QStringLiteral("c.png"));
    auto* label = dialog->findChild<QLabel*>(QStringLiteral("imageSequenceCountLabel"));
    CHECK(label != nullptr);
    CHECK(label->text().contains(QStringLiteral("8 x 10")));
    saw_dialog = true;
    dialog->accept();
  });
  const auto accepted = patchy::ui::prompt_image_sequence_import_options(nullptr, paths, QSize(8, 10));
  CHECK(saw_dialog);
  CHECK(accepted);
}

void ui_image_sequence_export_names_and_dialog() {
  // Numbered naming: the typed save name's trailing digits set prefix, start, padding.
  auto naming = patchy::ui::naming_from_save_base_name(QStringLiteral("shot_07"));
  CHECK(naming.prefix == QStringLiteral("shot_"));
  CHECK(naming.start == 7);
  CHECK(naming.padding == 2);
  const auto numbered = patchy::ui::image_sequence_file_names(
      {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}, naming, QStringLiteral("png"));
  CHECK(numbered == QStringList({QStringLiteral("shot_07.png"), QStringLiteral("shot_08.png"),
                                 QStringLiteral("shot_09.png")}));

  // No trailing digits: numbering is appended and starts at 001.
  naming = patchy::ui::naming_from_save_base_name(QStringLiteral("photo"));
  CHECK(naming.prefix == QStringLiteral("photo_"));
  CHECK(naming.start == 1);
  CHECK(naming.padding == 3);

  // Layer-name mode sanitizes, fills empty names, and dedupes case-insensitively.
  patchy::ui::ImageSequenceNaming by_name;
  by_name.use_layer_names = true;
  const auto named = patchy::ui::image_sequence_file_names(
      {QStringLiteral("walk"), QStringLiteral("Walk"), QStringLiteral("a/b:c"), QString()}, by_name,
      QStringLiteral("png"));
  CHECK(named == QStringList({QStringLiteral("walk.png"), QStringLiteral("Walk 2.png"),
                              QStringLiteral("a_b_c.png"), QStringLiteral("Frame 4.png")}));

  // Dialog round trip: the scope radios, start spin, and naming radios drive the
  // live count and preview.
  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("imageSequenceExportDialog"));
    CHECK(dialog != nullptr);
    auto* info = dialog->findChild<QLabel*>(QStringLiteral("imageSequenceInfoLabel"));
    auto* preview = dialog->findChild<QLabel*>(QStringLiteral("imageSequencePreviewLabel"));
    CHECK(info != nullptr);
    CHECK(preview != nullptr);
    // Visible-only default: two frames, shot_002-shot_003.
    CHECK(dialog->findChild<QRadioButton*>(QStringLiteral("imageSequenceVisibleLayersRadio"))->isChecked());
    CHECK(info->text().contains(QStringLiteral("2")));
    CHECK(preview->text().contains(QStringLiteral("shot_002.png")));
    CHECK(preview->text().contains(QStringLiteral("shot_003.png")));
    // All layers: the hidden layer joins in and the count/preview follow.
    dialog->findChild<QRadioButton*>(QStringLiteral("imageSequenceAllLayersRadio"))->setChecked(true);
    CHECK(info->text().contains(QStringLiteral("3")));
    CHECK(preview->text().contains(QStringLiteral("shot_004.png")));
    dialog->findChild<QSpinBox*>(QStringLiteral("imageSequenceStartSpin"))->setValue(5);
    CHECK(preview->text().contains(QStringLiteral("shot_005.png")));
    dialog->findChild<QRadioButton*>(QStringLiteral("imageSequenceLayerNamesRadio"))->setChecked(true);
    CHECK(preview->text().contains(QStringLiteral("hero.png")));
    CHECK(preview->text().contains(QStringLiteral("end.png")));
    saw_dialog = true;
    dialog->accept();
  });
  patchy::ui::ImageSequenceNaming suggested;
  suggested.prefix = QStringLiteral("shot_");
  suggested.start = 2;
  suggested.padding = 3;
  const auto chosen = patchy::ui::prompt_image_sequence_export_options(
      nullptr, {QStringLiteral("hero"), QStringLiteral("end")},
      {QStringLiteral("hero"), QStringLiteral("hidden"), QStringLiteral("end")}, suggested, QStringLiteral("png"));
  CHECK(saw_dialog);
  CHECK(chosen.has_value());
  CHECK(chosen->naming.use_layer_names);
  CHECK(!chosen->visible_layers_only);
}

void ui_tile_preview_window_tracks_document_edits() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  document.layers().front().pixels().clear(0);
  {
    auto& pixels = document.layers().front().pixels();
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        px[0] = 40;
        px[1] = 90;
        px[2] = 200;
        if (pixels.format().channels >= 4) {
          px[3] = 255;
        }
      }
    }
  }

  auto* action = window.findChild<QAction*>(QStringLiteral("viewTilePreviewAction"));
  CHECK(action != nullptr);
  CHECK(action->isCheckable());
  action->setChecked(true);
  QApplication::processEvents();
  auto* preview = window.findChild<QDialog*>(QStringLiteral("tilePreviewWindow"));
  CHECK(preview != nullptr);
  CHECK(preview->isVisible());
  auto* view = preview->findChild<QWidget*>(QStringLiteral("tilePreviewView"));
  CHECK(view != nullptr);

  const auto center_color = [view] {
    const auto grab = view->grab().toImage();
    return grab.pixelColor(grab.width() / 2, grab.height() / 2);
  };
  const auto before = center_color();
  CHECK(before.blue() > before.red());

  // Recolor the document; the revision probe must trigger a live refresh.
  {
    auto& pixels = document.layers().front().pixels();
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        px[0] = 210;
        px[1] = 60;
        px[2] = 30;
      }
    }
  }
  bool refreshed = false;
  for (int attempt = 0; attempt < 40 && !refreshed; ++attempt) {
    QApplication::processEvents(QEventLoop::AllEvents, 25);
    QThread::msleep(25);
    const auto after = center_color();
    refreshed = after.red() > after.blue();
  }
  CHECK(refreshed);
  save_widget_artifact("ui_tile_preview_window", *preview);

  // Dragging pans the tiling with any mouse button; double-click recenters.
  auto* zoom_combo = preview->findChild<QComboBox*>(QStringLiteral("tilePreviewZoomCombo"));
  CHECK(zoom_combo != nullptr);
  CHECK(view->property("panOffset").toPoint() == QPoint(0, 0));
  const auto view_center = QPoint(view->width() / 2, view->height() / 2);
  drag(*view, view_center, view_center + QPoint(23, -17));
  CHECK(view->property("panOffset").toPoint() == QPoint(23, -17));
  drag(*view, view_center, view_center + QPoint(-6, 9), Qt::NoModifier, Qt::MiddleButton);
  CHECK(view->property("panOffset").toPoint() == QPoint(17, -8));
  drag(*view, view_center, view_center + QPoint(4, 3), Qt::NoModifier, Qt::RightButton);
  CHECK(view->property("panOffset").toPoint() == QPoint(21, -5));
  drag(*view, view_center, view_center + QPoint(2, -3), Qt::NoModifier, Qt::BackButton);
  CHECK(view->property("panOffset").toPoint() == QPoint(23, -8));
  send_wheel(*view, view_center + QPoint(37, 21), 120);
  CHECK(view->property("zoomPercent").toInt() > 0);
  save_widget_artifact("ui_tile_preview_window_pan_zoom", *preview);
  zoom_combo->setCurrentIndex(0);
  QApplication::processEvents();
  CHECK(view->property("zoomPercent").toInt() == 0);
  send_double_click(*view, view_center);
  CHECK(view->property("panOffset").toPoint() == QPoint(0, 0));

  // The mouse wheel zooms and the combo mirrors the resulting percent (as a placeholder
  // when it is not one of the presets).
  CHECK(view->property("zoomPercent").toInt() == 0);  // Fit
  send_wheel(*view, view_center, 120);
  const auto wheeled_percent = view->property("zoomPercent").toInt();
  CHECK(wheeled_percent > 0);
  CHECK(zoom_combo->currentIndex() == -1
            ? zoom_combo->placeholderText() == QStringLiteral("%1%").arg(wheeled_percent)
            : zoom_combo->currentData().toInt() == wheeled_percent);
  send_wheel(*view, view_center, -120);
  CHECK(view->property("zoomPercent").toInt() < wheeled_percent);
  zoom_combo->setCurrentIndex(0);
  QApplication::processEvents();
  CHECK(view->property("zoomPercent").toInt() == 0);

  // The frameless window resizes through its corner size grip.
  auto* grip = preview->findChild<QWidget*>(QStringLiteral("tilePreviewSizeGrip"));
  CHECK(grip != nullptr);
  CHECK(grip->isVisible());

  // The chrome close button (QDialog::reject) must actually dismiss the window AND uncheck
  // the View menu toggle. Both halves matter: reject() used to hide without a close event
  // (checkmark stuck), and a reject()->close() "fix" made QDialog::closeEvent veto every
  // close (window stuck). done() is the funnel that handles both.
  QPointer<QDialog> preview_guard(preview);
  auto* close_button = preview->findChild<QToolButton*>(QStringLiteral("dialogChromeCloseButton"));
  CHECK(close_button != nullptr);
  close_button->click();
  QApplication::processEvents();
  CHECK(!action->isChecked());
  CHECK(preview_guard.isNull() || !preview_guard->isVisible());

  // Re-toggling from the menu must bring it back and close it again cleanly.
  action->setChecked(true);
  QApplication::processEvents();
  auto* reopened = window.findChild<QDialog*>(QStringLiteral("tilePreviewWindow"));
  CHECK(reopened != nullptr);
  CHECK(reopened->isVisible());
  QPointer<QDialog> reopened_guard(reopened);
  action->setChecked(false);
  QApplication::processEvents();
  CHECK(reopened_guard.isNull() || !reopened_guard->isVisible());

  // Closing the main window takes an open preview down with it; a surviving preview
  // has no visible transient parent and would keep the app process alive headless.
  action->setChecked(true);
  QApplication::processEvents();
  QPointer<QDialog> final_guard(window.findChild<QDialog*>(QStringLiteral("tilePreviewWindow")));
  CHECK(!final_guard.isNull());
  CHECK(final_guard->isVisible());
  window.close();
  QApplication::processEvents();
  CHECK(final_guard.isNull() || !final_guard->isVisible());
}

void ui_tile_preview_follows_document_switches_and_large_edits() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  auto& first_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto fill_active_layer = [](patchy::Document& document, int red, int green, int blue) {
    auto& pixels = document.layers().front().pixels();
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(red);
        px[1] = static_cast<std::uint8_t>(green);
        px[2] = static_cast<std::uint8_t>(blue);
        if (pixels.format().channels >= 4) {
          px[3] = 255;
        }
      }
    }
  };
  fill_active_layer(first_document, 40, 90, 200);

  require_action(window, "viewTilePreviewAction")->setChecked(true);
  QApplication::processEvents();
  auto* preview = window.findChild<QDialog*>(QStringLiteral("tilePreviewWindow"));
  CHECK(preview != nullptr);
  CHECK(preview->isVisible());
  auto* view = preview->findChild<QWidget*>(QStringLiteral("tilePreviewView"));
  auto* status = preview->findChild<QLabel*>(QStringLiteral("tilePreviewStatusLabel"));
  CHECK(view != nullptr);
  CHECK(status != nullptr);
  const auto center_color = [view] {
    const auto grab = view->grab().toImage();
    return grab.pixelColor(grab.width() / 2, grab.height() / 2);
  };
  CHECK(process_events_until([&] {
    const auto color = center_color();
    return color.blue() > 150 && color.red() < 150;
  }));

  // A second, larger document (1100x1100 = above the immediate cap): the tab switch alone
  // must re-render the preview (this used to leave the old document's tiles on screen).
  // The new document's fill depends on persisted New Document settings, so only assert
  // that the first document's blue is gone and the status shows the new dimensions.
  accept_new_document_dialog(1100, 1100);
  require_action(window, "fileNewAction")->trigger();
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == 2);
  CHECK(process_events_until([&] {
    const auto color = center_color();
    return !(color.blue() > 150 && color.red() < 150);
  }));
  CHECK(process_events_until(
      [&] { return status->text().contains(QStringLiteral("1100")); }));

  // Content edits on the >1 Mpx document auto-refresh once the edit pauses for a tick.
  auto& second_document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(&second_document != &first_document);
  fill_active_layer(second_document, 210, 60, 30);
  CHECK(process_events_until([&] {
    const auto color = center_color();
    return color.red() > 150 && color.green() < 150 && color.blue() < 150;
  }));

  // Switching back re-renders the first document's tiles.
  tabs->setCurrentIndex(0);
  QApplication::processEvents();
  CHECK(process_events_until([&] {
    const auto color = center_color();
    return color.blue() > 150 && color.red() < 150;
  }));

  preview->close();
  QApplication::processEvents();
}

void ui_shift_seams_action_wraps_document_and_toggles_back() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto width = document.width();
  const auto height = document.height();
  {
    // Left half green, right half magenta: shifting moves the vertical seam to the middle.
    auto& pixels = document.layers().front().pixels();
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(x < width / 2 ? 0 : 200);
        px[1] = static_cast<std::uint8_t>(x < width / 2 ? 200 : 0);
        px[2] = static_cast<std::uint8_t>(x < width / 2 ? 80 : 160);
        if (pixels.format().channels >= 4) {
          px[3] = 255;
        }
      }
    }
  }

  require_action(window, "viewTilePreviewAction")->setChecked(true);
  QApplication::processEvents();
  auto* preview = window.findChild<QDialog*>(QStringLiteral("tilePreviewWindow"));
  CHECK(preview != nullptr);
  auto* seam_button = preview->findChild<QPushButton*>(QStringLiteral("tilePreviewSeamButton"));
  CHECK(seam_button != nullptr);
  CHECK(seam_button->isEnabled());
  CHECK(seam_button->text() == QStringLiteral("Shift Seams to Center"));

  auto* action = require_action(window, "imageShiftSeamsAction");
  action->trigger();
  QApplication::processEvents();
  {
    const auto& shifted = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    CHECK(shifted.metadata().values.contains(patchy::ui::kTileSeamOffsetMetadataKey));
    // Old (0,0) landed at (width/2, height/2); the old right-half start wrapped to (0,0).
    const auto* at_center = shifted.layers().front().pixels().pixel(width / 2, height / 2);
    CHECK(at_center[0] == 0 && at_center[1] == 200 && at_center[2] == 80);
    const auto* at_origin = shifted.layers().front().pixels().pixel(0, 0);
    CHECK(at_origin[0] == 200 && at_origin[1] == 0 && at_origin[2] == 160);
  }
  // The tile window's button label follows the document's parity on its poll tick.
  CHECK(process_events_until(
      [&] { return seam_button->text() == QStringLiteral("Shift Seams Back"); }));

  // Second press: exact inverse, parity cleared.
  action->trigger();
  QApplication::processEvents();
  {
    const auto& restored = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    CHECK(!restored.metadata().values.contains(patchy::ui::kTileSeamOffsetMetadataKey));
    const auto* at_origin = restored.layers().front().pixels().pixel(0, 0);
    CHECK(at_origin[0] == 0 && at_origin[1] == 200 && at_origin[2] == 80);
    const auto* past_seam = restored.layers().front().pixels().pixel(width / 2, 0);
    CHECK(past_seam[0] == 200 && past_seam[1] == 0 && past_seam[2] == 160);
  }
  CHECK(process_events_until(
      [&] { return seam_button->text() == QStringLiteral("Shift Seams to Center"); }));

  // Undo restores both the pixels and the parity metadata.
  action->trigger();
  QApplication::processEvents();
  CHECK(std::as_const(patchy::ui::MainWindowTestAccess::document(window))
            .metadata()
            .values.contains(patchy::ui::kTileSeamOffsetMetadataKey));
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  {
    const auto& undone = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    CHECK(!undone.metadata().values.contains(patchy::ui::kTileSeamOffsetMetadataKey));
    const auto* at_origin = undone.layers().front().pixels().pixel(0, 0);
    CHECK(at_origin[0] == 0 && at_origin[1] == 200 && at_origin[2] == 80);
  }
  preview->close();
  QApplication::processEvents();
}

void ui_canvas_tiling_mode_paints_ghost_tiles_live() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto width = document.width();
  const auto height = document.height();

  // Solid orange through the real edit path so the canvas render cache updates.
  canvas->set_primary_color(QColor(235, 140, 30));
  require_action_by_text(window, QStringLiteral("Fill Layer / Selection"))->trigger();
  QApplication::processEvents();

  canvas->set_zoom(0.15);
  canvas->center_document_in_view();
  QApplication::processEvents();

  auto* action = require_action(window, "viewTilingModeAction");
  CHECK(action->isCheckable());
  CHECK(!action->isChecked());
  CHECK(!canvas->tiling_preview_enabled());
  // Sample the center of the left neighbor tile.
  const auto ghost_point = canvas->widget_position_for_document_point(QPoint(-width / 2, height / 2));
  CHECK(canvas->rect().contains(ghost_point));
  {
    const auto before = render_widget_image(*canvas);
    CHECK(color_close(before.pixelColor(ghost_point), QColor(36, 38, 41), 10));
  }

  action->trigger();
  QApplication::processEvents();
  CHECK(action->isChecked());
  CHECK(canvas->tiling_preview_enabled());
  {
    const auto tiled = render_widget_image(*canvas);
    CHECK(color_close(tiled.pixelColor(ghost_point), QColor(235, 140, 30), 24));
    // The document's own pixels are untouched by the mode. Sample the document
    // center: at 15% zoom a near-corner point maps within a pixel of the ghost
    // seam and flips with canvas-centering parity.
    CHECK(color_close(canvas_pixel(*canvas, QPoint(width / 2, height / 2)), QColor(235, 140, 30), 24));
  }

  // A full-layer edit repaints the ghosts in the same pass.
  canvas->set_primary_color(QColor(40, 90, 200));
  require_action_by_text(window, QStringLiteral("Fill Layer / Selection"))->trigger();
  QApplication::processEvents();
  {
    const auto repainted = render_widget_image(*canvas);
    CHECK(color_close(repainted.pixelColor(ghost_point), QColor(40, 90, 200), 24));
  }
  save_widget_artifact("ui_canvas_tiling_mode", window);

  // Per-document state: a new tab starts with tiling off and the menu check follows.
  accept_new_document_dialog(360, 240);
  require_action(window, "fileNewAction")->trigger();
  QApplication::processEvents();
  auto* second_canvas = require_canvas(window);
  CHECK(second_canvas != canvas);
  CHECK(!action->isChecked());
  CHECK(!second_canvas->tiling_preview_enabled());

  // Partial brush updates must repaint the ghost copies too (the dirty-rect replication
  // path): record the real paint regions while stroking at 100% zoom.
  action->trigger();
  QApplication::processEvents();
  CHECK(second_canvas->tiling_preview_enabled());
  second_canvas->set_zoom(1.0);
  second_canvas->center_document_in_view();
  second_canvas->set_tool(patchy::ui::CanvasTool::Brush);
  second_canvas->set_brush_size(8);
  second_canvas->set_primary_color(QColor(20, 220, 120));
  QApplication::processEvents();
  PaintRegionRecorder recorder;
  second_canvas->installEventFilter(&recorder);
  recorder.reset();
  const auto stroke_center = second_canvas->widget_position_for_document_point(QPoint(180, 120));
  drag(*second_canvas, stroke_center, stroke_center + QPoint(6, 0));
  QApplication::processEvents();
  // Sample the LEFT neighbor tile, not the right one. The canvas overlays its scroll bars on
  // its own right and bottom edges as child widgets (sync_scroll_bars, always visible while a
  // document is open), so widget_position_for_document_point can legitimately return a point
  // underneath one, where a render reads scroll-bar chrome instead of canvas pixels. The
  // right-hand ghost landed 1 px inside the content area on Windows and 1 px under the bar on
  // macOS, where the canvas is 4 px narrower; canvas_content_rect keeps that from silently
  // coming back as a color mismatch.
  const auto content = canvas_content_rect(*second_canvas);
  const auto ghost_stroke_point =
      second_canvas->widget_position_for_document_point(QPoint(180 - 360, 120));
  CHECK(content.adjusted(8, 8, -8, -8).contains(ghost_stroke_point));
  CHECK(recorder.region().contains(ghost_stroke_point));
  second_canvas->removeEventFilter(&recorder);
  {
    const auto stroked = render_widget_image(*second_canvas);
    CHECK(color_close(stroked.pixelColor(ghost_stroke_point), QColor(20, 220, 120), 40));
  }

  // Back on the first tab the mode is still on; toggling off restores the backdrop.
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  tabs->setCurrentWidget(canvas);
  QApplication::processEvents();
  CHECK(action->isChecked());
  CHECK(canvas->tiling_preview_enabled());
  action->trigger();
  QApplication::processEvents();
  CHECK(!canvas->tiling_preview_enabled());
  {
    const auto cleared = render_widget_image(*canvas);
    CHECK(color_close(cleared.pixelColor(ghost_point), QColor(36, 38, 41), 10));
  }
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
  // Per-axis PPI: 300 px at 300 ppi is 1 in wide, 150 px at 150 ppi is 1 in tall.
  CHECK(std::abs(placement.print_size_inches.width() - 1.0) < 0.01);
  CHECK(std::abs(placement.print_size_inches.height() - 1.0) < 0.01);

  settings.scale_mode = patchy::ui::PrintScaleMode::CustomScale;
  settings.scale_percent = 50.0;
  placement = patchy::ui::calculate_print_placement(document, settings, page_layout);
  CHECK(std::abs(placement.print_size_inches.width() - 0.5) < 0.01);

  settings.area_mode = patchy::ui::PrintAreaMode::Selection;
  settings.scale_mode = patchy::ui::PrintScaleMode::ActualSize;
  placement = patchy::ui::calculate_print_placement(document, settings, page_layout);
  CHECK(placement.source_rect == QRect(0, 0, 150, 75));
  CHECK(std::abs(placement.print_size_inches.width() - 0.5) < 0.01);
  CHECK(std::abs(placement.print_size_inches.height() - 0.5) < 0.01);

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
  CHECK(patchy::ui::write_print_pdf(pdf_path, document, settings, page_layout, QStringLiteral("photo.psd")));
  CHECK(QFileInfo(pdf_path).isFile());
  CHECK(QFileInfo(pdf_path).size() > 1000);

  // Save PDF derives its suggested filename from the document title, not a fixed
  // "Patchy Print.pdf".
  CHECK(patchy::ui::default_print_pdf_filename(QStringLiteral("photo.psd")) == QStringLiteral("photo.pdf"));
  CHECK(patchy::ui::default_print_pdf_filename(QStringLiteral("Untitled-2")) == QStringLiteral("Untitled-2.pdf"));
  CHECK(patchy::ui::default_print_pdf_filename(QString()) == QObject::tr("Untitled") + QStringLiteral(".pdf"));

  // A custom sheet (100 x 200 mm) from the in-app Paper controls: exact, never snapped
  // to a named size, margins carried over, and written without any printer driver.
  const auto custom_size = patchy::ui::custom_page_size_points(QSizeF(100.0 / 25.4 * 72.0, 200.0 / 25.4 * 72.0));
  CHECK(custom_size.isValid());
  CHECK(custom_size.id() == QPageSize::Custom);
  const auto custom = patchy::ui::page_layout_with_size(page_layout, custom_size, QPageLayout::Portrait);
  CHECK(custom.isValid());
  CHECK(std::abs(custom.fullRect(QPageLayout::Point).width() - 283.46) < 0.5);
  CHECK(std::abs(custom.fullRect(QPageLayout::Point).height() - 566.93) < 0.5);
  CHECK(std::abs(custom.margins(QPageLayout::Point).left() - 36.0) < 0.01);
  const auto custom_landscape = patchy::ui::page_layout_with_size(page_layout, custom_size, QPageLayout::Landscape);
  CHECK(std::abs(custom_landscape.fullRect(QPageLayout::Point).width() - 566.93) < 0.5);
  // A sheet too small for the margins keeps the sheet and drops the margins.
  const auto tiny = patchy::ui::page_layout_with_size(
      page_layout, patchy::ui::custom_page_size_points(QSizeF(20.0, 20.0)), QPageLayout::Portrait);
  CHECK(tiny.isValid());
  CHECK(std::abs(tiny.fullRect(QPageLayout::Point).width() - 20.0) < 0.5);
  CHECK(!tiny.paintRect(QPageLayout::Point).isEmpty());
  CHECK(!patchy::ui::custom_page_size_points(QSizeF(0.0, 10.0)).isValid());
  CHECK(patchy::ui::print_page_size_choices().back() == QPageSize::Custom);

  const auto custom_pdf_path = QStringLiteral("test-artifacts/ui_print_output_custom.pdf");
  QFile::remove(custom_pdf_path);
  settings.scale_mode = patchy::ui::PrintScaleMode::FitToPage;
  CHECK(patchy::ui::write_print_pdf(custom_pdf_path, document, settings, custom, QStringLiteral("photo.psd")));
  QFile custom_file(custom_pdf_path);
  CHECK(custom_file.open(QIODevice::ReadOnly));
  const QByteArray custom_bytes = custom_file.readAll();
  const QRegularExpression media_box(QStringLiteral("/MediaBox\\s*\\[\\s*([-\\d.]+)\\s+([-\\d.]+)\\s+([-\\d.]+)\\s+([-\\d.]+)\\s*\\]"));
  const auto match = media_box.match(QString::fromLatin1(custom_bytes));
  CHECK(match.hasMatch());
  if (match.hasMatch()) {
    CHECK(std::abs(match.captured(3).toDouble() - match.captured(1).toDouble() - 283.46) < 1.0);
    CHECK(std::abs(match.captured(4).toDouble() - match.captured(2).toDouble() - 566.93) < 1.0);
  }
}

// The accepted page layout persists across runs (settings group "print"): a custom
// sheet round-trips exactly, a named sheet by id, and a missing store gives Letter.
void ui_print_page_layout_persists() {
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("print"));
  const auto restore = qScopeGuard([] { patchy::ui::app_settings().remove(QStringLiteral("print")); });
  const auto letter = patchy::ui::load_stored_print_page_layout();
  CHECK(letter.pageSize().id() == QPageSize::Letter);

  const auto custom = patchy::ui::page_layout_with_size(
      patchy::ui::default_print_page_layout(),
      patchy::ui::custom_page_size_points(QSizeF(300.0 / 25.4 * 72.0, 600.0 / 25.4 * 72.0)),
      QPageLayout::Landscape);
  patchy::ui::store_print_page_layout(custom);
  const auto restored = patchy::ui::load_stored_print_page_layout();
  CHECK(restored.pageSize().id() == QPageSize::Custom);
  CHECK(restored.orientation() == QPageLayout::Landscape);
  CHECK(std::abs(restored.pageSize().size(QPageSize::Point).width() - 300.0 / 25.4 * 72.0) < 0.5);
  CHECK(std::abs(restored.pageSize().size(QPageSize::Point).height() - 600.0 / 25.4 * 72.0) < 0.5);
  CHECK(std::abs(restored.margins(QPageLayout::Point).top() - 36.0) < 0.01);

  patchy::ui::store_print_page_layout(
      patchy::ui::page_layout_with_size(custom, QPageSize(QPageSize::A4), QPageLayout::Portrait));
  const auto a4 = patchy::ui::load_stored_print_page_layout();
  CHECK(a4.pageSize().id() == QPageSize::A4);
  CHECK(a4.orientation() == QPageLayout::Portrait);
}

// File > Export Multi-Page PDF: the dialog lists every open document (checked, in
// order), offers the group source only when the active document has a top-level group,
// and gates the controls per source.
void ui_multipage_pdf_dialog_lists_documents_and_groups() {
  patchy::ui::app_settings().remove(QStringLiteral("exportOptions/multiPagePdfEditableLayers"));
  patchy::ui::MainWindow window;
  show_window(window);
  {
    auto& document = patchy::ui::MainWindowTestAccess::document(window);
    patchy::Layer group(document.allocate_layer_id(), "Page 1", patchy::LayerKind::Group);
    document.add_layer(std::move(group));
  }
  patchy::ui::MainWindowTestAccess::create_default_document(window);
  const auto session_count = patchy::ui::MainWindowTestAccess::session_count(window);
  CHECK(session_count >= 2);
  // The second (group-less) document is active now; the groups source must be off.
  auto seen = std::make_shared<int>(0);
  // The file first, then PDF Options (the order Save As uses).
  accept_save_file_dialog_then(QStringLiteral("exportMultiPagePdfFileDialog"), QDir::current().absoluteFilePath(QStringLiteral("test-artifacts/multipage_unused.pdf")), [&window, seen] {
    auto* dialog = find_top_level_dialog(QStringLiteral("multiPagePdfExportDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* list = dialog->findChild<QListWidget*>(QStringLiteral("multiPagePdfDocumentsList"));
    auto* groups = dialog->findChild<QRadioButton*>(QStringLiteral("multiPagePdfGroupsRadio"));
    auto* documents_radio = dialog->findChild<QRadioButton*>(QStringLiteral("multiPagePdfDocumentsRadio"));
    auto* ungrouped = dialog->findChild<QCheckBox*>(QStringLiteral("multiPagePdfUngroupedCheck"));
    auto* summary = dialog->findChild<QLabel*>(QStringLiteral("multiPagePdfSummaryLabel"));
    auto* export_button = dialog->findChild<QPushButton*>(QStringLiteral("multiPagePdfExportButton"));
    auto* editable = dialog->findChild<QCheckBox*>(QStringLiteral("multiPagePdfEditableCheck"));
    // Editable layers are the default for a fresh install.
    CHECK(editable != nullptr && editable->isChecked());
    // The image-quality presets replaced the lossless checkbox (shared with PDF Options).
    auto* quality = dialog->findChild<QComboBox*>(QStringLiteral("multiPagePdfImageQualityCombo"));
    CHECK(quality != nullptr && quality->count() == 4);
    CHECK(quality != nullptr && quality->currentData().toString() == patchy::ui::stored_pdf_image_quality_id());
    CHECK(dialog->findChild<QCheckBox*>(QStringLiteral("multiPagePdfLosslessCheck")) == nullptr);
    CHECK(list != nullptr && groups != nullptr && documents_radio != nullptr && ungrouped != nullptr &&
          summary != nullptr && export_button != nullptr);
    if (list == nullptr || groups == nullptr || documents_radio == nullptr || ungrouped == nullptr ||
        summary == nullptr || export_button == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(static_cast<std::size_t>(list->count()) == patchy::ui::MainWindowTestAccess::session_count(window));
    CHECK(documents_radio->isChecked());
    CHECK(!groups->isEnabled());
    CHECK(!ungrouped->isEnabled());
    CHECK(export_button->isEnabled());
    CHECK(summary->text().contains(QString::number(list->count())));
    // Every document starts selected; deselecting them all disables Export.
    CHECK(list->selectedItems().size() == list->count());
    list->clearSelection();
    QApplication::processEvents();
    CHECK(!export_button->isEnabled());
    list->item(0)->setSelected(true);
    QApplication::processEvents();
    CHECK(export_button->isEnabled());
    CHECK(summary->text().contains(QStringLiteral("1")));
    *seen = 1;
    dialog->reject();
  });
  require_action(window, "fileExportMultiPagePdfAction")->trigger();
  CHECK(*seen == 1);

  // Back on the document with the group: the group source is offered and gates the
  // list off and the ungrouped checkbox on.
  patchy::ui::MainWindowTestAccess::activate_session(window, 0);
  QApplication::processEvents();
  CHECK(&patchy::ui::MainWindowTestAccess::document(window) ==
        &patchy::ui::MainWindowTestAccess::session_document(window, 0));
  accept_save_file_dialog_then(QStringLiteral("exportMultiPagePdfFileDialog"), QDir::current().absoluteFilePath(QStringLiteral("test-artifacts/multipage_unused.pdf")), [seen] {
    auto* dialog = find_top_level_dialog(QStringLiteral("multiPagePdfExportDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* list = dialog->findChild<QListWidget*>(QStringLiteral("multiPagePdfDocumentsList"));
    auto* groups = dialog->findChild<QRadioButton*>(QStringLiteral("multiPagePdfGroupsRadio"));
    auto* ungrouped = dialog->findChild<QCheckBox*>(QStringLiteral("multiPagePdfUngroupedCheck"));
    auto* summary = dialog->findChild<QLabel*>(QStringLiteral("multiPagePdfSummaryLabel"));
    if (list == nullptr || groups == nullptr || ungrouped == nullptr || summary == nullptr) {
      CHECK(false);
      dialog->reject();
      return;
    }
    CHECK(groups->isEnabled());
    groups->setChecked(true);
    QApplication::processEvents();
    CHECK(!list->isEnabled());
    // The whole order control (Move Up/Down, Auto Sort, Reverse) grays out with it.
    for (const auto* button_name : {"multiPagePdfMoveUpButton", "multiPagePdfMoveDownButton",
                                    "multiPagePdfAutoSortButton", "multiPagePdfReverseButton"}) {
      auto* button = dialog->findChild<QPushButton*>(QLatin1String(button_name));
      CHECK(button != nullptr);
      CHECK(button != nullptr && !button->isEnabled());
    }
    CHECK(ungrouped->isEnabled());
    CHECK(summary->text().contains(QStringLiteral("1")));
    *seen = 2;
    dialog->reject();
  });
  require_action(window, "fileExportMultiPagePdfAction")->trigger();
  CHECK(*seen == 2);
}

#if defined(PATCHY_HAVE_QT_PDF)
void ui_pdf_export_page_size_and_round_trip() {
  ensure_artifact_dir();
  // 600 x 300 px at 300 ppi is exactly 2 x 1 inches, i.e. a 144 x 72 pt page.
  patchy::Document document(600, 300, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 300.0;
  document.print_settings().vertical_ppi = 300.0;
  document.add_pixel_layer("Page",
                           solid_pixels(600, 300, patchy::PixelFormat::rgba8(), QColor(200, 20, 30)));

  const auto lossless_path = QStringLiteral("test-artifacts/ui_pdf_export_lossless.pdf");
  const auto lossy_path = QStringLiteral("test-artifacts/ui_pdf_export_lossy.pdf");
  QFile::remove(lossless_path);
  QFile::remove(lossy_path);
  patchy::ui::write_pdf_document_file(document, lossless_path, patchy::ui::PdfExportOptions{true});
  patchy::ui::write_pdf_document_file(document, lossy_path, patchy::ui::PdfExportOptions{false});
  CHECK(QFileInfo(lossless_path).isFile());
  CHECK(QFileInfo(lossy_path).isFile());
  // The whole point of the lossless option: Qt's PDF engine re-encodes images as JPEG
  // quality 94 unless the painter sets QPainter::LosslessImageRendering. Assert the
  // filter that actually lands in the file rather than comparing sizes, which flips
  // sign with the content (Flate beats JPEG on flat color, loses on a photograph).
  const auto read_all = [](const QString& file_path) {
    QFile file(file_path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  };
  const QByteArray lossless_bytes = read_all(lossless_path);
  const QByteArray lossy_bytes = read_all(lossy_path);
  CHECK(lossless_bytes.contains("/FlateDecode"));
  CHECK(!lossless_bytes.contains("/DCTDecode"));
  CHECK(lossy_bytes.contains("/DCTDecode"));

  // Verified with a decoder that is not the writer: Qt PDF (PDFium) reads it back.
  QPdfDocument reader;
  CHECK(reader.load(lossless_path) == QPdfDocument::Error::None);
  CHECK(reader.pageCount() == 1);
  const QSizeF page_points = reader.pagePointSize(0);
  CHECK(std::abs(page_points.width() - 144.0) < 0.5);
  CHECK(std::abs(page_points.height() - 72.0) < 0.5);
  const QImage rendered = reader.render(0, QSize(600, 300));
  CHECK(!rendered.isNull());
  CHECK(rendered.size() == QSize(600, 300));
  CHECK(color_close(rendered.pixelColor(300, 150), QColor(200, 20, 30), 1));
  CHECK(color_close(rendered.pixelColor(5, 5), QColor(200, 20, 30), 1));
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// One page per document, each at its own size; read back through Patchy's Qt-free
// parser for the structure and through PDFium for the pixels.
void ui_pdf_export_multipage_writes_one_page_per_document() {
  ensure_artifact_dir();
  const std::array<QColor, 3> colors{QColor(200, 20, 30), QColor(20, 200, 30), QColor(30, 20, 200)};
  const std::array<QSize, 3> sizes{QSize(144, 72), QSize(72, 144), QSize(100, 100)};
  std::vector<patchy::Document> documents;
  for (std::size_t index = 0; index < 3; ++index) {
    patchy::Document document(sizes[index].width(), sizes[index].height(), patchy::PixelFormat::rgba8());
    document.print_settings().horizontal_ppi = 72.0;
    document.print_settings().vertical_ppi = 72.0;
    document.add_pixel_layer("Page", solid_pixels(sizes[index].width(), sizes[index].height(),
                                                  patchy::PixelFormat::rgba8(), colors[index]));
    documents.push_back(std::move(document));
  }
  std::vector<const patchy::Document*> pages;
  for (const auto& document : documents) {
    pages.push_back(&document);
  }

  const auto path = QStringLiteral("test-artifacts/ui_pdf_export_multipage.pdf");
  QFile::remove(path);
  patchy::ui::write_multipage_pdf_file(pages, path, patchy::ui::PdfExportOptions{true});
  QFile file(path);
  CHECK(file.open(QIODevice::ReadOnly));
  const QByteArray bytes = file.readAll();
  const std::span<const std::uint8_t> span(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                           static_cast<std::size_t>(bytes.size()));
  CHECK(patchy::pdf::page_count(span) == 3);
  for (int page = 0; page < 3; ++page) {
    const auto pixels = patchy::pdf::page_size_in_pixels(span, page, 1.0);
    CHECK(pixels[0] == sizes[static_cast<std::size_t>(page)].width());
    CHECK(pixels[1] == sizes[static_cast<std::size_t>(page)].height());
  }

  QPdfDocument reader;
  CHECK(reader.load(path) == QPdfDocument::Error::None);
  CHECK(reader.pageCount() == 3);
  for (int page = 0; page < 3; ++page) {
    const auto expected = sizes[static_cast<std::size_t>(page)];
    CHECK(std::abs(reader.pagePointSize(page).width() - expected.width()) < 0.5);
    CHECK(std::abs(reader.pagePointSize(page).height() - expected.height()) < 0.5);
    const QImage rendered = reader.render(page, expected);
    CHECK(!rendered.isNull());
    if (!rendered.isNull()) {
      CHECK(color_close(rendered.pixelColor(expected.width() / 2, expected.height() / 2),
                        colors[static_cast<std::size_t>(page)], 2));
      CHECK(color_close(rendered.pixelColor(2, expected.height() - 3), colors[static_cast<std::size_t>(page)], 2));
    }
  }

  // Progress runs once per page in order; cancelling removes the partial file.
  std::vector<int> progress_pages;
  const auto cancel_path = QStringLiteral("test-artifacts/ui_pdf_export_multipage_cancel.pdf");
  QFile::remove(cancel_path);
  CHECK(!patchy::ui::write_multipage_pdf_file(pages, cancel_path, {}, nullptr, [&progress_pages](int page, int count) {
    CHECK(count == 3);
    progress_pages.push_back(page);
    return page < 2;
  }));
  CHECK(progress_pages == (std::vector<int>{1, 2}));
  CHECK(!QFileInfo::exists(cancel_path));
  CHECK(patchy::ui::write_multipage_pdf_file(pages, cancel_path, {}, nullptr, [](int, int) { return true; }));
  CHECK(QFileInfo::exists(cancel_path));

  // Empty and unusable inputs refuse instead of writing a broken file.
  bool threw = false;
  try {
    patchy::ui::write_multipage_pdf_file(std::span<const patchy::Document* const>(), path);
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// "Print a folder as a page": every top-level group becomes a page (hidden ones unless
// told otherwise), top of the stack first, and ungrouped root layers ride along on every
// page when asked.
void ui_pdf_export_multipage_groups_become_pages() {
  ensure_artifact_dir();
  patchy::Document document(100, 100, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 72.0;
  document.add_pixel_layer("Background", solid_pixels(100, 100, patchy::PixelFormat::rgba8(), QColor(0, 255, 0)));
  {
    patchy::Layer group(document.allocate_layer_id(), "Page A", patchy::LayerKind::Group);
    patchy::Layer red(document.allocate_layer_id(), "Red",
                      solid_pixels(100, 100, patchy::PixelFormat::rgba8(), QColor(255, 0, 0)));
    red.set_opacity(0.5F);
    group.add_child(std::move(red));
    document.add_layer(std::move(group));
  }
  {
    patchy::Layer hidden(document.allocate_layer_id(), "Hidden", patchy::LayerKind::Group);
    hidden.set_visible(false);
    document.add_layer(std::move(hidden));
  }
  {
    patchy::Layer group(document.allocate_layer_id(), "Page B", patchy::LayerKind::Group);
    patchy::Layer blue(document.allocate_layer_id(), "Blue",
                       solid_pixels(100, 100, patchy::PixelFormat::rgba8(), QColor(0, 0, 255)));
    blue.set_opacity(0.5F);
    group.add_child(std::move(blue));
    document.add_layer(std::move(group));
  }

  // Hidden groups are pages by default (a PDF imported as page folders hides pages 2..N).
  CHECK(patchy::ui::documents_for_top_level_groups(document, true).size() == 3);
  const auto with_background = patchy::ui::documents_for_top_level_groups(document, true, false);
  CHECK(with_background.size() == 2);
  if (with_background.size() != 2) {
    return;
  }
  // Top of the stack first: Page B, then Page A. Only the page's group and the
  // ungrouped background stay visible.
  const auto visible_names = [](const patchy::Document& page) {
    QStringList names;
    for (const auto& layer : page.layers()) {
      if (layer.visible()) {
        names.push_back(QString::fromStdString(layer.name()));
      }
    }
    return names;
  };
  CHECK(visible_names(with_background[0]) == (QStringList{QStringLiteral("Background"), QStringLiteral("Page B")}));
  CHECK(visible_names(with_background[1]) == (QStringList{QStringLiteral("Background"), QStringLiteral("Page A")}));
  const auto without_background = patchy::ui::documents_for_top_level_groups(document, false, false);
  CHECK(without_background.size() == 2);
  if (without_background.size() != 2) {
    return;
  }
  CHECK(visible_names(without_background[0]) == QStringList{QStringLiteral("Page B")});

  const auto render_first_page = [](const std::vector<patchy::Document>& pages, const QString& path) {
    std::vector<const patchy::Document*> pointers;
    for (const auto& page : pages) {
      pointers.push_back(&page);
    }
    QFile::remove(path);
    patchy::ui::write_multipage_pdf_file(pointers, path);
    QPdfDocument reader;
    CHECK(reader.load(path) == QPdfDocument::Error::None);
    CHECK(reader.pageCount() == 2);
    return reader.render(0, QSize(100, 100));
  };
  const QImage shared = render_first_page(with_background, QStringLiteral("test-artifacts/ui_pdf_export_groups.pdf"));
  CHECK(!shared.isNull());
  if (!shared.isNull()) {
    // Half-opaque blue over the green background: opaque, green and blue halves.
    const auto pixel = shared.pixelColor(50, 50);
    CHECK(pixel.alpha() == 255);
    CHECK(pixel.blue() > 100 && pixel.green() > 100 && pixel.red() < 30);
  }
  const QImage alone =
      render_first_page(without_background, QStringLiteral("test-artifacts/ui_pdf_export_groups_alone.pdf"));
  CHECK(!alone.isNull());
  if (!alone.isNull()) {
    // Without the background the page is half-opaque blue over nothing.
    const auto pixel = alone.pixelColor(50, 50);
    CHECK(pixel.alpha() < 200);
    CHECK(pixel.blue() > 200 && pixel.green() < 30);
  }
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
// The editable mode's promise: a shape layer comes back as a path, a text layer as real
// text with an embedded font, a pixel layer as an image, and the page still looks like
// the canvas. Verified through two decoders that are not the writer: Patchy's own Qt-free
// reader for the structure, PDFium for the pixels.
// Editable mode per page: a real vector shape on every page of the file, verified
// through the Qt-free reader.
void ui_pdf_export_multipage_editable_keeps_text_on_every_page() {
  ensure_artifact_dir();
  std::vector<patchy::Document> documents;
  for (int index = 0; index < 2; ++index) {
    patchy::Document document(200, 100, patchy::PixelFormat::rgba8());
    document.print_settings().horizontal_ppi = 72.0;
    document.print_settings().vertical_ppi = 72.0;
    document.add_pixel_layer("Fill", solid_pixels(200, 100, patchy::PixelFormat::rgba8(), QColor(255, 255, 255)));
    const patchy::RgbColor color = index == 0 ? patchy::RgbColor{255, 0, 0} : patchy::RgbColor{0, 0, 255};
    document.add_layer(solid_rect_shape_layer(document, "Box", 20, 20, 80, 60, color, 0.0));
    documents.push_back(std::move(document));
  }
  std::vector<const patchy::Document*> pages;
  for (const auto& document : documents) {
    pages.push_back(&document);
  }
  const auto path = QStringLiteral("test-artifacts/ui_pdf_export_multipage_editable.pdf");
  QFile::remove(path);
  patchy::ui::PdfExportOptions options;
  options.editable_layers = true;
  std::vector<std::string> notices;
  patchy::ui::write_multipage_pdf_file(pages, path, options, &notices);

  QFile file(path);
  CHECK(file.open(QIODevice::ReadOnly));
  const QByteArray bytes = file.readAll();
  const std::span<const std::uint8_t> span(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                           static_cast<std::size_t>(bytes.size()));
  CHECK(patchy::pdf::page_count(span) == 2);
  for (int page = 0; page < 2; ++page) {
    patchy::pdf::VectorReadOptions read_options;
    read_options.page = page;
    read_options.pixels_per_point = 1.0;
    const auto vectors = patchy::pdf::read_page_as_vectors(span, read_options);
    int shapes = 0;
    for (const auto& layer : vectors.document.layers()) {
      if (patchy::layer_is_vector_shape(layer) && layer.vector_shape() != nullptr) {
        ++shapes;
        CHECK(layer.vector_shape()->fill.color.red == (page == 0 ? 255 : 0));
      }
    }
    CHECK(shapes >= 1);
  }
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
void ui_pdf_export_writes_transparency_as_soft_mask() {
  ensure_artifact_dir();
  // Three alpha levels (clear margin, half-transparent band, opaque square) so Qt has to
  // write a real 8-bit /SMask image; a two-level alpha would collapse into a 1-bit
  // /ImageMask stencil instead and never exercise the soft-mask path.
  patchy::Document document(200, 200, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 200.0;
  document.print_settings().vertical_ppi = 200.0;
  auto pixels = solid_pixels(200, 200, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  const auto paint_block = [&pixels](std::int32_t top, std::int32_t bottom, QColor color, std::uint8_t alpha) {
    for (std::int32_t y = top; y < bottom; ++y) {
      auto row = pixels.row(y);
      for (std::int32_t x = 40; x < 160; ++x) {
        row[static_cast<std::size_t>(x) * 4 + 0] = static_cast<std::uint8_t>(color.red());
        row[static_cast<std::size_t>(x) * 4 + 1] = static_cast<std::uint8_t>(color.green());
        row[static_cast<std::size_t>(x) * 4 + 2] = static_cast<std::uint8_t>(color.blue());
        row[static_cast<std::size_t>(x) * 4 + 3] = alpha;
      }
    }
  };
  paint_block(40, 100, QColor(20, 190, 60), 255);
  paint_block(100, 160, QColor(30, 60, 200), 128);
  document.add_pixel_layer("Blocks", std::move(pixels));

  const auto path = QStringLiteral("test-artifacts/ui_pdf_export_alpha.pdf");
  QFile::remove(path);
  patchy::ui::write_pdf_document_file(document, path, patchy::ui::PdfExportOptions{true});

  // Structural check, independent of how any viewer paints the page behind the image:
  // the image XObject must carry a soft mask, or the transparency was silently dropped.
  QFile file(path);
  CHECK(file.open(QIODevice::ReadOnly));
  const QByteArray bytes = file.readAll();
  CHECK(bytes.contains("/SMask ") || bytes.contains("/ImageMask true"));
  CHECK(bytes.contains("/DeviceGray"));

  QPdfDocument reader;
  CHECK(reader.load(path) == QPdfDocument::Error::None);
  const QImage rendered = reader.render(0, QSize(200, 200));
  CHECK(!rendered.isNull());
  // PDFium renders onto a transparent page, so all three alpha levels survive the round
  // trip and the colors underneath stay unblended.
  CHECK(color_close(rendered.pixelColor(100, 70), QColor(20, 190, 60), 2));
  CHECK(rendered.pixelColor(100, 70).alpha() == 255);
  CHECK(std::abs(rendered.pixelColor(100, 130).alpha() - 128) <= 2);
  CHECK(rendered.pixelColor(10, 10).alpha() == 0);
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
void ui_pdf_export_editable_keeps_layers_and_matches_composite() {
  ensure_artifact_dir();
  patchy::Document built(300, 200, patchy::PixelFormat::rgba8());
  built.print_settings().horizontal_ppi = 72.0;  // one point per pixel: the reimport is 1:1
  built.print_settings().vertical_ppi = 72.0;
  auto photo = solid_pixels(300, 200, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(photo, QRect(20, 100, 120, 80), QColor(60, 120, 200));
  built.add_pixel_layer("Photo", std::move(photo));
  built.add_layer(solid_rect_shape_layer(built, "Hero Rect", 160, 30, 280, 120, {220, 40, 40}, 4.0));
  patchy::Layer text_layer(built.allocate_layer_id(), "Title",
                           solid_pixels(1, 1, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto text_id = text_layer.id();
  text_layer.set_bounds(patchy::Rect{20, 20, 1, 1});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Patchy PDF";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#101010";
  built.add_layer(std::move(text_layer));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built), QStringLiteral("Editable PDF"));
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* text = document.find_layer(text_id);
  CHECK(text != nullptr);
  if (text == nullptr) {
    return;
  }
  // Give the text layer its real render (what a committed layer holds).
  CHECK(patchy::ui::MainWindowTestAccess::apply_text_warp(window, *text, patchy::TextWarp{}));
  CHECK(!text->pixels().empty());

  const auto path = QStringLiteral("test-artifacts/ui_pdf_export_editable.pdf");
  QFile::remove(path);
  std::vector<std::string> notices;
  patchy::ui::write_pdf_document_file(std::as_const(document), path, patchy::ui::PdfExportOptions{true, true},
                                      &notices);
  CHECK(QFileInfo(path).isFile());
  for (const auto& notice : notices) {
    std::printf("[pdf] unexpected notice: %s\n", notice.c_str());
  }
  CHECK(notices.empty());  // nothing here needed flattening

  const QByteArray bytes = read_file_bytes(path);
  CHECK(bytes.contains("/Font"));
  CHECK(bytes.contains("/FontFile"));  // the face travels with the file
  CHECK(bytes.contains("/Image"));
  CHECK(bytes.contains("/FlateDecode"));
  CHECK(!bytes.contains("/DCTDecode"));

  // Structure: Patchy's own reader (formats/pdf_document_io, a different code path from
  // the Qt writer) sees the pieces, not one picture.
  patchy::pdf::VectorReadOptions read_options;
  read_options.pixels_per_point = 1.0;
  const auto read = patchy::pdf::read_page_as_vectors(
      std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                    static_cast<std::size_t>(bytes.size())),
      read_options);
  CHECK(read.document.width() == 300);
  CHECK(read.document.height() == 200);
  CHECK(read.shape_layers >= 1);
  CHECK(read.text_layers >= 1);
  CHECK(read.image_layers >= 1);
  bool saw_red_shape = false;
  std::string text_seen;
  const std::function<void(const std::vector<patchy::Layer>&)> visit = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      if (const auto* shape = layer.vector_shape();
          shape != nullptr && shape->fill.kind == patchy::VectorFillKind::Solid && shape->fill.color.red == 220 &&
          shape->fill.color.green == 40) {
        saw_red_shape = true;
      }
      if (const auto found = layer.metadata().find(patchy::kLayerMetadataText); found != layer.metadata().end()) {
        text_seen += found->second;
      }
      visit(layer.children());
    }
  };
  visit(std::as_const(read.document).layers());
  CHECK(saw_red_shape);
  // One run per line of text, not one object per letter: Qt's per-glyph Tj output is
  // folded back into a single TJ by formats/pdf_text_merge.
  CHECK(read.text_layers == 1);
  if (text_seen != "Patchy PDF") {
    std::printf("[pdf] text read back: '%s'\n", text_seen.c_str());
  }
  CHECK(text_seen == "Patchy PDF");

  // Pixels: PDFium's rendering of the page against Patchy's composite.
  QPdfDocument reader;
  CHECK(reader.load(path) == QPdfDocument::Error::None);
  const QImage rendered = reader.render(0, QSize(300, 200));
  CHECK(!rendered.isNull());
  const QImage composite = patchy::ui::qimage_from_document(std::as_const(document), true);
  const double delta = mean_rgb_delta_over_white(rendered, composite);
  if (delta >= 6.0) {
    std::fprintf(stderr, "[pdf] editable export mean delta %f\n", delta);
    rendered.save(QStringLiteral("test-artifacts/ui_pdf_export_editable_pdfium.png"));
    composite.save(QStringLiteral("test-artifacts/ui_pdf_export_editable_composite.png"));
  }
  CHECK(delta < 6.0);
  // Spot checks on the three objects: photo, shape interior, and a text-free margin.
  CHECK(color_close(rendered.pixelColor(80, 140), QColor(60, 120, 200), 2));
  CHECK(color_close(rendered.pixelColor(220, 75), QColor(220, 40, 40), 2));
  CHECK(rendered.pixelColor(290, 190).alpha() == 0);
}
#endif

}  // namespace

std::vector<patchy::test::TestCase> import_print_resolution_tests_part1() {
  return {
      {"ui_single_text_layer_psb_keeps_transparency_without_mask", ui_single_text_layer_psb_keeps_transparency_without_mask},
      {"ui_layer_context_menu_keeps_edit_styles_on_top", ui_layer_context_menu_keeps_edit_styles_on_top},
      {"ui_file_import_menu_actions_registered", ui_file_import_menu_actions_registered},
      {"ui_scanner_import_creates_untitled_document", ui_scanner_import_creates_untitled_document},
      {"ui_photocopy_dialog_previews_clipping_at_actual_size", ui_photocopy_dialog_previews_clipping_at_actual_size},
      {"ui_aseprite_open_adopts_palette_and_builds_layer_tree", ui_aseprite_open_adopts_palette_and_builds_layer_tree},
      {"ui_export_scale_writes_nearest_neighbor_pixels", ui_export_scale_writes_nearest_neighbor_pixels},
      {"ui_png8_export_scaled_stays_indexed", ui_png8_export_scaled_stays_indexed},
      {"ui_export_trim_transparent_crops_to_visible_alpha", ui_export_trim_transparent_crops_to_visible_alpha},
      {"ui_export_resize_resamples_bilinear_to_target", ui_export_resize_resamples_bilinear_to_target},
      {"ui_export_fill_transparent_mattes_over_background", ui_export_fill_transparent_mattes_over_background},
      {"ui_export_transforms_apply_trim_resize_scale_matte_in_order", ui_export_transforms_apply_trim_resize_scale_matte_in_order},
      {"ui_export_option_defaults_leave_output_untransformed", ui_export_option_defaults_leave_output_untransformed},
      {"ui_sprite_sheet_export_grid_layout_and_padding", ui_sprite_sheet_export_grid_layout_and_padding},
      {"ui_sprite_sheet_import_slices_cells_into_layers", ui_sprite_sheet_import_slices_cells_into_layers},
      {"ui_image_sequence_ordering_and_numbered_expansion", ui_image_sequence_ordering_and_numbered_expansion},
      {"ui_image_sequence_import_builds_layers", ui_image_sequence_import_builds_layers},
      {"ui_image_sequence_export_names_and_dialog", ui_image_sequence_export_names_and_dialog},
      {"ui_tile_preview_window_tracks_document_edits", ui_tile_preview_window_tracks_document_edits},
      {"ui_tile_preview_follows_document_switches_and_large_edits", ui_tile_preview_follows_document_switches_and_large_edits},
      {"ui_shift_seams_action_wraps_document_and_toggles_back", ui_shift_seams_action_wraps_document_and_toggles_back},
      {"ui_canvas_tiling_mode_paints_ghost_tiles_live", ui_canvas_tiling_mode_paints_ghost_tiles_live},
      {"ui_qimage_multiply_uses_empty_backdrop_as_transparent", ui_qimage_multiply_uses_empty_backdrop_as_transparent},
      {"ui_print_layout_and_pdf_output_work", ui_print_layout_and_pdf_output_work},
      {"ui_print_page_layout_persists", ui_print_page_layout_persists},
      {"ui_multipage_pdf_dialog_lists_documents_and_groups", ui_multipage_pdf_dialog_lists_documents_and_groups},
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_export_page_size_and_round_trip", ui_pdf_export_page_size_and_round_trip},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_export_multipage_writes_one_page_per_document", ui_pdf_export_multipage_writes_one_page_per_document},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_export_multipage_groups_become_pages", ui_pdf_export_multipage_groups_become_pages},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_export_multipage_editable_keeps_text_on_every_page", ui_pdf_export_multipage_editable_keeps_text_on_every_page},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_export_writes_transparency_as_soft_mask", ui_pdf_export_writes_transparency_as_soft_mask},
#endif
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_export_editable_keeps_layers_and_matches_composite", ui_pdf_export_editable_keeps_layers_and_matches_composite},
#endif
  };
}
