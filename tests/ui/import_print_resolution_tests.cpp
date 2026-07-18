#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "core/contour_presets.hpp"
#include "core/gradient_presets.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_presets.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_filter_effects.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "ui/smart_object_render.hpp"
#include "core/layer_tree.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/pattern_manager_dialog.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_browser.hpp"
#include "ui/style_library.hpp"
#include "ui/style_manager_dialog.hpp"
#include "psd/asl_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_layer_effects.hpp"
#include "core/style_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/blend_if_range_editor.hpp"
#include "ui/color_panel.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/curves_editor.hpp"
#include "ui/curves_presets.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/filter_look_library.hpp"
#include "ui/font_picker.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/print_dialog.hpp"
#include "ui/selection_outline.hpp"
#include "ui/image_sequence_dialog.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/app_settings.hpp"
#include "ui/update_checker.hpp"
#include "ui/visual_filter_gallery_dialog.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui/zoom_status_bar.hpp"
#include "filters/builtin_filters.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "render/compositor.hpp"
#include "synthetic_dng.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"

#include <QAbstractItemModel>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDataStream>
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
#include <QGroupBox>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QInputDevice>
#include <QInputDialog>
#include <QKeyEvent>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListView>
#include <QLayout>
#include <QListWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QLocale>
#include <QSizeGrip>
#include <QMetaObject>
#include <QMouseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QIODevice>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPolygonF>
#include <QThread>
#include <QPaintEvent>
#include <QPixmap>
#include <QPointingDevice>
#include <QProgressDialog>
#include <QPushButton>
#include <QStackedWidget>
#include <QRadioButton>
#include <QSpinBox>
#include <QStringList>
#include <QScrollBar>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTabletEvent>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVariant>
#include <QWheelEvent>
#include <QWindow>
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

namespace {

using namespace patchy::test::ui;

void ui_single_text_layer_psb_keeps_transparency_without_mask() {
  const auto path = patchy::test::local_psd_fixture_path("PSBtest/Content.psb");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] PSBtest fixture missing\n";
    return;
  }
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
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  // Native scanner acquisition exists on Windows and macOS and keeps one persisted id.
  CHECK(scanner_action != nullptr);
  CHECK(import_menu->actions().contains(scanner_action));
  const auto* scanner_command = window.hotkey_registry().find_command(QStringLiteral("file.import_scanner"));
  CHECK(scanner_command != nullptr);
  CHECK(scanner_command->action == scanner_action);
#ifdef Q_OS_MACOS
  CHECK(scanner_action->text() == QStringLiteral("From &Scanner..."));
#else
  CHECK(scanner_action->text() == QStringLiteral("From &Scanner or Camera..."));
#endif
#else
  CHECK(scanner_action == nullptr);
  CHECK(window.hotkey_registry().find_command(QStringLiteral("file.import_scanner")) == nullptr);
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

  // Dialog round trip: the start spin and naming radios drive the live preview.
  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    auto* dialog = find_top_level_dialog(QStringLiteral("imageSequenceExportDialog"));
    CHECK(dialog != nullptr);
    auto* preview = dialog->findChild<QLabel*>(QStringLiteral("imageSequencePreviewLabel"));
    CHECK(preview != nullptr);
    CHECK(preview->text().contains(QStringLiteral("shot_002.png")));
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
      nullptr, {QStringLiteral("hero"), QStringLiteral("mid"), QStringLiteral("end")}, suggested,
      QStringLiteral("png"));
  CHECK(saw_dialog);
  CHECK(chosen.has_value());
  CHECK(chosen->use_layer_names);
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
}

void ui_print_dialog_exposes_printer_and_visible_checkboxes() {
  patchy::ui::MainWindow window;
  show_window(window);
  // The dialog's opening state depends on the print size: pin the startup document
  // (72 ppi since the New Document redesign) to 300 ppi so 1024x768 px is
  // 3.41 x 2.56 in, which fits Letter at actual size.
  {
    auto& document = patchy::ui::MainWindowTestAccess::document(window);
    document.print_settings().horizontal_ppi = 300.0;
    document.print_settings().vertical_ppi = 300.0;
  }

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
      auto* resolution = dialog->findChild<QLabel*>(QStringLiteral("printResolutionValueLabel"));
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
      // The 1024x768 document pinned to 300 ppi fits Letter at actual size, so the
      // dialog opens at 100% (Photoshop's default) with fit-to-media unchecked and
      // the derived print resolution equal to the document resolution.
      CHECK(!scale_to_fit->isChecked());
      CHECK(scale->isEnabled());
      CHECK(std::abs(scale->value() - 100.0) < 0.01);
      CHECK(resolution->text().contains(QStringLiteral("300")));
      CHECK(units->currentData().toString() == QStringLiteral("in"));
      CHECK(scale_size->text().contains(QStringLiteral("in")));
      CHECK(image_size->text().contains(QStringLiteral("in")));
      scale_to_fit->setChecked(true);
      QApplication::processEvents();
      CHECK(!scale->isEnabled());
      // Fit-to-media on Letter enlarges the 3.41 x 2.56 in document, and the derived
      // print resolution drops below the stored 300 accordingly.
      CHECK(scale->value() > 100.0);
      CHECK(!resolution->text().startsWith(QStringLiteral("300")));
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

void ui_image_size_dialog_unit_and_resolution_links_work() {
  patchy::ui::MainWindow window;  // default document: 1024x768 at 72 ppi
  show_window(window);

  QTimer::singleShot(0, [] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyImageSizeDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("imageSizeWidthSpin"));
      auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("imageSizeHeightSpin"));
      auto* resolution = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("imageSizeResolutionSpin"));
      auto* width_unit = dialog->findChild<QComboBox*>(QStringLiteral("imageSizeWidthUnitCombo"));
      auto* height_unit = dialog->findChild<QComboBox*>(QStringLiteral("imageSizeHeightUnitCombo"));
      auto* resolution_unit = dialog->findChild<QComboBox*>(QStringLiteral("imageSizeResolutionUnitCombo"));
      auto* dimensions = dialog->findChild<QLabel*>(QStringLiteral("imageSizeDimensionsLabel"));
      auto* resample = dialog->findChild<QCheckBox*>(QStringLiteral("imageSizeResampleCheck"));
      auto* link = dialog->findChild<QToolButton*>(QStringLiteral("imageSizeLinkButton"));
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      CHECK(resolution != nullptr);
      CHECK(width_unit != nullptr);
      CHECK(height_unit != nullptr);
      CHECK(resolution_unit != nullptr);
      CHECK(dimensions != nullptr);
      CHECK(resample != nullptr);
      CHECK(link != nullptr);

      CHECK(width_unit->currentText() == QStringLiteral("Pixels"));
      CHECK(width->value() == 1024.0);
      CHECK(std::abs(resolution->value() - 72.0) < 0.01);

      // Physical units display through the resolution; the two unit combos stay in step.
      width_unit->setCurrentIndex(width_unit->findText(QStringLiteral("Inches")));
      QApplication::processEvents();
      CHECK(height_unit->currentText() == QStringLiteral("Inches"));
      CHECK(std::abs(width->value() - 1024.0 / 72.0) < 0.005);
      CHECK(std::abs(height->value() - 768.0 / 72.0) < 0.005);

      // Resample ON + physical units: a resolution change keeps the print size and
      // re-derives the pixel dimensions (72 -> 36 halves them).
      resolution->setValue(36.0);
      QApplication::processEvents();
      CHECK(dimensions->text().contains(QStringLiteral("512 px x 384 px")));
      CHECK(std::abs(width->value() - 1024.0 / 72.0) < 0.005);

      // The resolution unit combo only changes the display of the same stored PPI.
      resolution_unit->setCurrentIndex(resolution_unit->findText(QStringLiteral("Pixels/Centimeter")));
      QApplication::processEvents();
      CHECK(std::abs(resolution->value() - 36.0 / 2.54) < 0.01);
      resolution_unit->setCurrentIndex(resolution_unit->findText(QStringLiteral("Pixels/Inch")));
      QApplication::processEvents();
      CHECK(std::abs(resolution->value() - 36.0) < 0.01);

      // Resample OFF: pending resamples revert to the document's pixels, the link
      // and pixel units disable, and W/H/Resolution become the Photoshop tri-link.
      resample->setChecked(false);
      QApplication::processEvents();
      CHECK(!link->isEnabled());
      CHECK(width_unit->currentText() == QStringLiteral("Inches"));
      CHECK(dimensions->text().contains(QStringLiteral("1024 px x 768 px")));
      CHECK(std::abs(width->value() - 1024.0 / 36.0) < 0.005);
      width->setValue(5.12);
      QApplication::processEvents();
      CHECK(std::abs(resolution->value() - 200.0) < 0.05);
      CHECK(dimensions->text().contains(QStringLiteral("1024 px x 768 px")));
      widget->grab().save(QStringLiteral("test-artifacts/ui_image_size_dialog_units.png"));
      dialog->accept();
      return;
    }
    CHECK(false);
  });
  require_action(window, "imageSizeAction")->trigger();
  QApplication::processEvents();

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 1024);
  CHECK(document.height() == 768);
  CHECK(std::abs(document.print_settings().horizontal_ppi - 200.0) < 0.05);
  CHECK(std::abs(document.print_settings().vertical_ppi - 200.0) < 0.05);
}

void ui_imported_image_density_follows_photoshop_conventions() {
  QImage source(8, 6, QImage::Format_RGB32);
  source.fill(QColor(10, 20, 30));

  QByteArray png_bytes;
  {
    QBuffer buffer(&png_bytes);
    buffer.open(QIODevice::WriteOnly);
    QImage tagged = source;
    tagged.setDotsPerMeterX(11811);  // 300 ppi
    tagged.setDotsPerMeterY(5906);   // 150 ppi
    CHECK(tagged.save(&buffer, "png"));
  }
  const auto png_span = std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(png_bytes.constData()), static_cast<std::size_t>(png_bytes.size()));

  auto tagged_document = patchy::ui::document_from_qimage(source, "Tagged");
  patchy::ui::apply_imported_image_density(tagged_document, png_span, source);
  CHECK(std::abs(tagged_document.print_settings().horizontal_ppi - 11811.0 * 0.0254) < 0.001);
  CHECK(std::abs(tagged_document.print_settings().vertical_ppi - 5906.0 * 0.0254) < 0.001);

  // Strip the pHYs chunk (4 length + 4 type + 9 payload + 4 crc bytes): the file is
  // untagged and must open at Photoshop's 72 ppi, never Qt's screen-derived default.
  auto untagged_bytes = png_bytes;
  const auto phys_index = untagged_bytes.indexOf(QByteArrayLiteral("pHYs"));
  CHECK(phys_index > 4);
  untagged_bytes.remove(phys_index - 4, 21);
  const auto untagged_span = std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(untagged_bytes.constData()),
      static_cast<std::size_t>(untagged_bytes.size()));
  auto untagged_document = patchy::ui::document_from_qimage(source, "Untagged");
  patchy::ui::apply_imported_image_density(untagged_document, untagged_span, source);
  CHECK(untagged_document.print_settings().horizontal_ppi == 72.0);
  CHECK(untagged_document.print_settings().vertical_ppi == 72.0);

  // JPEG JFIF densities are honored exactly.
  QByteArray jpeg_bytes;
  {
    QBuffer buffer(&jpeg_bytes);
    buffer.open(QIODevice::WriteOnly);
    QImage tagged = source;
    tagged.setDotsPerMeterX(9449);  // 240 ppi
    tagged.setDotsPerMeterY(9449);
    CHECK(tagged.save(&buffer, "jpg"));
  }
  const auto jpeg_span = std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(jpeg_bytes.constData()), static_cast<std::size_t>(jpeg_bytes.size()));
  auto jpeg_document = patchy::ui::document_from_qimage(source, "Jpeg");
  patchy::ui::apply_imported_image_density(jpeg_document, jpeg_span, source);
  CHECK(std::abs(jpeg_document.print_settings().horizontal_ppi - 240.0) < 0.5);
}

void ui_ruler_unit_preference_changes_ruler_ticks() {
  ensure_artifact_dir();

  // Rendering: a standalone canvas at 100 ppi so 1 in = 100 doc px exactly.
  patchy::Document document(300, 200, patchy::PixelFormat::rgb8());
  document.print_settings().horizontal_ppi = 100.0;
  document.print_settings().vertical_ppi = 100.0;
  document.add_pixel_layer("Background", solid_pixels(300, 200, patchy::PixelFormat::rgb8(), Qt::white));
  patchy::ui::CanvasWidget canvas;
  canvas.resize(420, 300);
  canvas.set_document(&document);
  canvas.set_zoom(1.0);
  canvas.set_rulers_visible(true);
  canvas.show();
  QApplication::processEvents();

  const auto ruler_tick_pixels = [&canvas] {
    const auto strip = canvas.grab(QRect(0, 0, canvas.width(), 24)).toImage();
    int ticks = 0;
    for (int y = 0; y < strip.height(); ++y) {
      for (int x = 0; x < strip.width(); ++x) {
        if (color_close(strip.pixelColor(x, y), QColor(185, 190, 198), 40)) {
          ++ticks;
        }
      }
    }
    return std::pair<QImage, int>(strip, ticks);
  };
  CHECK(canvas.ruler_unit() == patchy::ui::MeasurementUnit::Pixels);
  const auto [pixel_ruler, pixel_ticks] = ruler_tick_pixels();
  CHECK(pixel_ticks > 0);

  canvas.set_ruler_unit(patchy::ui::MeasurementUnit::Inches);
  QApplication::processEvents();
  const auto [inch_ruler, inch_ticks] = ruler_tick_pixels();
  CHECK(inch_ticks > 0);
  CHECK(pixel_ruler != inch_ruler);
  pixel_ruler.save(QStringLiteral("test-artifacts/ui_ruler_units_pixels.png"));
  inch_ruler.save(QStringLiteral("test-artifacts/ui_ruler_units_inches.png"));

  // Preference propagation: the window-level setter reaches the session canvas and
  // persists the settings token.
  SettingsValueRestorer restore_units(QStringLiteral("view/rulerUnits"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* session_canvas = require_canvas(window);
  patchy::ui::MainWindowTestAccess::set_ruler_unit_preference(window, patchy::ui::MeasurementUnit::Inches);
  QApplication::processEvents();
  CHECK(session_canvas->ruler_unit() == patchy::ui::MeasurementUnit::Inches);
  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("view/rulerUnits")).toString() == QStringLiteral("in"));
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
  // macOS titles carry the [*] windowModified placeholder (refresh_document_window_title).
  auto dragged_window_title = window.windowTitle();
  dragged_window_title.remove(QStringLiteral("[*]"));
  CHECK(dragged_window_title == QStringLiteral("drag-open.png"));
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
  const auto psd_path = QString::fromStdString(
      patchy::test::local_psd_fixture_path("C2Kyoto Nintendo NES Cartridge Label Template (Front).psd").string());
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

  // The template is full of placed smart objects; their import notes ride the status
  // bar (the popup only appears when imports/showPsdWarningsAndInfo is enabled).
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

void ui_qimage_region_render_matches_full_with_clipping() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer background(64, 48, patchy::PixelFormat::rgba8());
  background.clear(255);
  document.add_pixel_layer("Background", std::move(background));

  patchy::Layer base(document.allocate_layer_id(), "Base",
                     solid_pixels(28, 20, patchy::PixelFormat::rgba8(), QColor(190, 40, 40, 255)));
  base.set_bounds(patchy::Rect{12, 10, 28, 20});
  base.set_opacity(0.8F);
  document.add_layer(std::move(base));

  patchy::Layer member(document.allocate_layer_id(), "Member",
                       solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(30, 120, 220, 255)));
  member.set_clipped(true);
  member.set_blend_mode(patchy::BlendMode::Multiply);
  document.add_layer(std::move(member));

  patchy::AdjustmentSettings warm;
  warm.kind = patchy::AdjustmentKind::ColorBalance;
  warm.color_balance = patchy::ColorBalanceAdjustment{35, 0, 0};
  patchy::Layer adjustment(document.allocate_layer_id(), "Warmth", patchy::LayerKind::Adjustment);
  adjustment.set_bounds(patchy::Rect::from_size(64, 48));
  patchy::configure_adjustment_layer(adjustment, warm);
  adjustment.set_clipped(true);
  document.add_layer(std::move(adjustment));

  // Patch renders through the region path must match the full render exactly,
  // including patches that slice through the clip group's interior.
  const QRect region(8, 6, 40, 30);
  const auto full = patchy::ui::qimage_from_document(document, true).copy(region);
  const auto partial = patchy::ui::qimage_from_document_rect(document, region, true);
  CHECK(partial.size() == full.size());
  for (int y = 0; y < partial.height(); ++y) {
    for (int x = 0; x < partial.width(); ++x) {
      CHECK(color_close(partial.pixelColor(x, y), full.pixelColor(x, y), 0));
    }
  }

  const QRegion disjoint_region(QRect(10, 8, 14, 12));
  auto multi_region = disjoint_region.united(QRect(30, 20, 12, 12));
  const auto full_original = patchy::ui::qimage_from_document(document, true);
  const auto patches = patchy::ui::qimage_patches_from_document_region(document, multi_region, true);
  CHECK(patches.size() == 2U);
  for (const auto& patch : patches) {
    CHECK(patch.image.size() == patch.document_rect.size());
    const auto expected_patch = full_original.copy(patch.document_rect);
    CHECK(images_equal_rgba(patch.image, expected_patch));
  }
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

void ui_qimage_layer_bounds_override_moves_linked_masks_only() {
  {
    patchy::Document document(80, 48, patchy::PixelFormat::rgba8());
    document.add_pixel_layer("Background", solid_pixels(80, 48, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

    auto layer = patchy::Layer(document.allocate_layer_id(), "Linked Mask",
                               solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(30, 95, 230, 255)));
    const auto layer_id = layer.id();
    layer.set_bounds(patchy::Rect{10, 10, 12, 12});
    patchy::PixelBuffer mask_pixels(12, 12, patchy::PixelFormat::gray8());
    mask_pixels.clear(255);
    layer.set_mask(patchy::LayerMask{patchy::Rect{10, 10, 12, 12}, std::move(mask_pixels), 0, false});
    document.add_layer(std::move(layer));

    const auto moved_bounds = patchy::Rect{42, 10, 12, 12};
    const QRect region(0, 0, 80, 48);
    const auto preview =
        patchy::ui::qimage_from_document_rect_with_layer_bounds(document, region, true, layer_id, moved_bounds);

    auto* moved_layer = document.find_layer(layer_id);
    CHECK(moved_layer != nullptr);
    moved_layer->set_bounds(moved_bounds);
    auto& mask = *moved_layer->mask();
    mask.bounds.x += 32;
    const auto committed = patchy::ui::qimage_from_document_rect(document, region, true);

    CHECK(images_equal_rgba(preview, committed));
    CHECK(color_close(preview.pixelColor(46, 14), QColor(30, 95, 230), 0));
    CHECK(color_close(preview.pixelColor(14, 14), QColor(Qt::white), 0));
  }

  {
    patchy::Document document(80, 48, patchy::PixelFormat::rgba8());
    document.add_pixel_layer("Background", solid_pixels(80, 48, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

    auto layer = patchy::Layer(document.allocate_layer_id(), "Unlinked Mask",
                               solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(230, 80, 30, 255)));
    const auto layer_id = layer.id();
    layer.set_bounds(patchy::Rect{10, 10, 12, 12});
    patchy::PixelBuffer mask_pixels(12, 12, patchy::PixelFormat::gray8());
    mask_pixels.clear(255);
    layer.set_mask(patchy::LayerMask{patchy::Rect{10, 10, 12, 12}, std::move(mask_pixels), 0, false});
    patchy::set_layer_mask_linked(layer, false);
    document.add_layer(std::move(layer));

    const auto moved_bounds = patchy::Rect{42, 10, 12, 12};
    const QRect region(0, 0, 80, 48);
    const auto preview =
        patchy::ui::qimage_from_document_rect_with_layer_bounds(document, region, true, layer_id, moved_bounds);

    auto* moved_layer = document.find_layer(layer_id);
    CHECK(moved_layer != nullptr);
    moved_layer->set_bounds(moved_bounds);
    const auto committed = patchy::ui::qimage_from_document_rect(document, region, true);

    CHECK(images_equal_rgba(preview, committed));
    CHECK(color_close(preview.pixelColor(46, 14), QColor(Qt::white), 0));
    CHECK(color_close(preview.pixelColor(14, 14), QColor(Qt::white), 0));
  }
}

}  // namespace

std::vector<patchy::test::TestCase> import_print_resolution_tests() {
  return {
      {"ui_single_text_layer_psb_keeps_transparency_without_mask",
       ui_single_text_layer_psb_keeps_transparency_without_mask},
      {"ui_layer_context_menu_keeps_edit_styles_on_top", ui_layer_context_menu_keeps_edit_styles_on_top},
      {"ui_file_import_menu_actions_registered", ui_file_import_menu_actions_registered},
      {"ui_scanner_import_creates_untitled_document", ui_scanner_import_creates_untitled_document},
      {"ui_aseprite_open_adopts_palette_and_builds_layer_tree", ui_aseprite_open_adopts_palette_and_builds_layer_tree},
      {"ui_export_scale_writes_nearest_neighbor_pixels", ui_export_scale_writes_nearest_neighbor_pixels},
      {"ui_png8_export_scaled_stays_indexed", ui_png8_export_scaled_stays_indexed},
      {"ui_sprite_sheet_export_grid_layout_and_padding", ui_sprite_sheet_export_grid_layout_and_padding},
      {"ui_sprite_sheet_import_slices_cells_into_layers", ui_sprite_sheet_import_slices_cells_into_layers},
      {"ui_image_sequence_ordering_and_numbered_expansion", ui_image_sequence_ordering_and_numbered_expansion},
      {"ui_image_sequence_import_builds_layers", ui_image_sequence_import_builds_layers},
      {"ui_image_sequence_export_names_and_dialog", ui_image_sequence_export_names_and_dialog},
      {"ui_tile_preview_window_tracks_document_edits", ui_tile_preview_window_tracks_document_edits},
      {"ui_qimage_multiply_uses_empty_backdrop_as_transparent",
       ui_qimage_multiply_uses_empty_backdrop_as_transparent},
      {"ui_print_layout_and_pdf_output_work", ui_print_layout_and_pdf_output_work},
      {"ui_print_dialog_exposes_printer_and_visible_checkboxes",
       ui_print_dialog_exposes_printer_and_visible_checkboxes},
      {"ui_image_size_dialog_unit_and_resolution_links_work",
       ui_image_size_dialog_unit_and_resolution_links_work},
      {"ui_imported_image_density_follows_photoshop_conventions",
       ui_imported_image_density_follows_photoshop_conventions},
      {"ui_ruler_unit_preference_changes_ruler_ticks", ui_ruler_unit_preference_changes_ruler_ticks},
      {"ui_dragged_image_file_opens_document_tab", ui_dragged_image_file_opens_document_tab},
      {"ui_reported_psd_open_shows_progress_dialog_if_available",
       ui_reported_psd_open_shows_progress_dialog_if_available},
      {"ui_qimage_render_respects_hidden_layer_groups", ui_qimage_render_respects_hidden_layer_groups},
      {"ui_qimage_region_render_matches_full_layer_styles",
       ui_qimage_region_render_matches_full_layer_styles},
      {"ui_qimage_region_render_matches_full_with_clipping",
       ui_qimage_region_render_matches_full_with_clipping},
      {"ui_qimage_layer_bounds_override_moves_linked_masks_only",
       ui_qimage_layer_bounds_override_moves_linked_masks_only},
  };
}
