#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "core/contour_presets.hpp"
#include "core/gradient_presets.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_presets.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
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
#include "ui/pdf_export.hpp"
#include "ui/pdf_import.hpp"
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
#include "formats/pdf_document_io.hpp"
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
#include "ui/tile_preview_window.hpp"
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
#if defined(PATCHY_HAVE_QT_PDF)
#include <QPdfDocument>
#endif
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

#if defined(PATCHY_HAVE_QT_PDF)
// A two-page PDF built byte by byte, the way the other adversarial format fixtures are
// synthesized in-test: no binary fixture to maintain and no external generator. Page 1 is
// 144 x 72 pt of red, page 2 is 72 x 144 pt of blue, so the layer canvas has to come out
// 144 x 144 (the per-axis maximum) with the second page top-left aligned.
QByteArray two_page_pdf_bytes() {
  const QByteArray objects[6] = {
      QByteArrayLiteral("<</Type/Catalog/Pages 2 0 R>>"),
      QByteArrayLiteral("<</Type/Pages/Kids[3 0 R 5 0 R]/Count 2>>"),
      QByteArrayLiteral("<</Type/Page/Parent 2 0 R/MediaBox[0 0 144 72]/Contents 4 0 R>>"),
      QByteArrayLiteral("<</Length 24>>\nstream\n1 0 0 rg 0 0 144 72 re f\nendstream"),
      QByteArrayLiteral("<</Type/Page/Parent 2 0 R/MediaBox[0 0 72 144]/Contents 6 0 R>>"),
      QByteArrayLiteral("<</Length 24>>\nstream\n0 0 1 rg 0 0 72 144 re f\nendstream"),
  };
  QByteArray pdf = QByteArrayLiteral("%PDF-1.4\n");
  std::array<int, 6> offsets{};
  for (int index = 0; index < 6; ++index) {
    offsets[static_cast<std::size_t>(index)] = static_cast<int>(pdf.size());
    pdf += QByteArray::number(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
  }
  const int xref_offset = static_cast<int>(pdf.size());
  pdf += "xref\n0 7\n0000000000 65535 f \n";
  for (const int offset : offsets) {
    pdf += QStringLiteral("%1 00000 n \n").arg(offset, 10, 10, QLatin1Char('0')).toLatin1();
  }
  pdf += "trailer\n<</Size 7/Root 1 0 R>>\nstartxref\n" + QByteArray::number(xref_offset) + "\n%%EOF\n";
  return pdf;
}

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

// --- editable PDF export ---------------------------------------------------------

namespace {

QByteArray read_file_bytes(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// PDFium renders onto a transparent page; Patchy's composite over white is the
// reference, so both sides are flattened onto white before comparing.
QImage over_white(const QImage& image) {
  QImage result(image.size(), QImage::Format_RGB32);
  result.fill(Qt::white);
  QPainter painter(&result);
  painter.drawImage(0, 0, image);
  painter.end();
  return result;
}

double mean_rgb_delta_over_white(const QImage& a, const QImage& b) {
  const QImage flat_a = over_white(a);
  const QImage flat_b = over_white(b);
  CHECK(flat_a.size() == flat_b.size());
  double total = 0.0;
  for (int y = 0; y < flat_a.height(); ++y) {
    for (int x = 0; x < flat_a.width(); ++x) {
      const auto ca = flat_a.pixelColor(x, y);
      const auto cb = flat_b.pixelColor(x, y);
      total += std::abs(ca.red() - cb.red()) + std::abs(ca.green() - cb.green()) + std::abs(ca.blue() - cb.blue());
    }
  }
  return total / (static_cast<double>(flat_a.width()) * flat_a.height() * 3.0);
}

patchy::Layer solid_rect_shape_layer(patchy::Document& document, const char* name, int left, int top, int right,
                                     int bottom, patchy::RgbColor fill, double stroke_width) {
  patchy::LiveShapeParams params;
  params.kind = patchy::LiveShapeKind::Rectangle;
  params.left = left;
  params.top = top;
  params.right = right;
  params.bottom = bottom;
  params.index = 0;
  patchy::populate_live_shape_box_corners(params);
  patchy::VectorShapeContent content;
  content.path.subpaths = patchy::generate_live_shape_subpaths(params);
  content.origination = {params};
  content.fill.kind = patchy::VectorFillKind::Solid;
  content.fill.color = fill;
  if (stroke_width > 0.0) {
    content.stroke.enabled = true;
    content.stroke.width = stroke_width;
    content.stroke.content.kind = patchy::VectorFillKind::Solid;
    content.stroke.content.color = {20, 20, 20};
  }
  patchy::Layer layer(document.allocate_layer_id(), name, patchy::LayerKind::Pixel);
  layer.metadata()[patchy::kLayerMetadataVectorShape] = "1";
  patchy::mark_layer_vector_block_dirty(layer);
  layer.set_vector_shape(std::move(content));
  patchy::update_vector_shape_raster(layer, patchy::Rect::from_size(document.width(), document.height()),
                                     &document.metadata().patterns);
  return layer;
}

}  // namespace

// The editable mode's promise: a shape layer comes back as a path, a text layer as real
// text with an embedded font, a pixel layer as an image, and the page still looks like
// the canvas. Verified through two decoders that are not the writer: Patchy's own Qt-free
// reader for the structure, PDFium for the pixels.
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

// The features that ride Qt's PDF engine rather than an image: a gradient fill, an
// inside-aligned stroke (double width under a self clip), a group clipped by a vector
// mask, and constant layer opacity. No notices, and PDFium agrees with the canvas.
void ui_pdf_export_editable_gradients_clips_and_opacity_render_like_canvas() {
  ensure_artifact_dir();
  patchy::Document document(240, 160, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 72.0;
  document.add_pixel_layer("Paper", solid_pixels(240, 160, patchy::PixelFormat::rgba8(), QColor(250, 250, 245)));

  // A left-to-right red -> blue gradient inside an ellipse with an inside stroke.
  {
    patchy::LiveShapeParams params;
    params.kind = patchy::LiveShapeKind::Ellipse;
    params.left = 20;
    params.top = 20;
    params.right = 120;
    params.bottom = 100;
    params.index = 0;
    patchy::populate_live_shape_box_corners(params);
    patchy::VectorShapeContent content;
    content.path.subpaths = patchy::generate_live_shape_subpaths(params);
    content.origination = {params};
    content.fill.kind = patchy::VectorFillKind::Gradient;
    content.fill.gradient.type = patchy::LayerStyleGradientType::Linear;
    content.fill.gradient.angle_degrees = 0.0F;
    content.fill.gradient.interpolation = patchy::GradientInterpolationMethod::Linear;
    content.fill.gradient.color_stops = {{0.0F, {220, 30, 30}}, {1.0F, {30, 30, 220}}};
    content.fill.gradient.alpha_stops = {{0.0F, 1.0F}, {1.0F, 1.0F}};
    content.stroke.enabled = true;
    content.stroke.width = 6.0;
    content.stroke.alignment = patchy::VectorStrokeAlignment::Inside;
    content.stroke.content.kind = patchy::VectorFillKind::Solid;
    content.stroke.content.color = {20, 120, 20};
    patchy::Layer layer(document.allocate_layer_id(), "Gradient Ellipse", patchy::LayerKind::Pixel);
    layer.metadata()[patchy::kLayerMetadataVectorShape] = "1";
    patchy::mark_layer_vector_block_dirty(layer);
    layer.set_vector_shape(std::move(content));
    patchy::update_vector_shape_raster(layer, patchy::Rect::from_size(document.width(), document.height()),
                                       &document.metadata().patterns);
    document.add_layer(std::move(layer));
  }

  // A group whose vector mask (a square) clips a larger shape child.
  {
    patchy::Layer group(document.allocate_layer_id(), "Clipped Group", patchy::LayerKind::Group);
    group.add_child(solid_rect_shape_layer(document, "Wide", 130, 20, 230, 100, {240, 180, 20}, 0.0));
    patchy::LiveShapeParams mask_params;
    mask_params.kind = patchy::LiveShapeKind::Rectangle;
    mask_params.left = 150;
    mask_params.top = 40;
    mask_params.right = 210;
    mask_params.bottom = 80;
    mask_params.index = 0;
    patchy::populate_live_shape_box_corners(mask_params);
    patchy::LayerVectorMask mask;
    mask.path.subpaths = patchy::generate_live_shape_subpaths(mask_params);
    group.set_vector_mask(std::move(mask));
    // The compositor reads the mask's baked coverage cache, never the path.
    patchy::update_vector_mask_raster(group, patchy::Rect::from_size(document.width(), document.height()));
    document.add_layer(std::move(group));
  }

  // A half-transparent shape over the paper.
  {
    auto layer = solid_rect_shape_layer(document, "Ghost", 20, 110, 220, 150, {0, 0, 0}, 0.0);
    layer.set_opacity(0.5F);
    document.add_layer(std::move(layer));
  }

  const auto path = QStringLiteral("test-artifacts/ui_pdf_export_editable_paint.pdf");
  QFile::remove(path);
  std::vector<std::string> notices;
  patchy::ui::write_pdf_document_file(document, path, patchy::ui::PdfExportOptions{true, true}, &notices);
  for (const auto& notice : notices) {
    std::printf("[pdf] unexpected notice: %s\n", notice.c_str());
  }
  CHECK(notices.empty());
  const QByteArray bytes = read_file_bytes(path);
  CHECK(bytes.contains("/Shading"));  // the gradient is a real shading, not a picture

  QPdfDocument reader;
  CHECK(reader.load(path) == QPdfDocument::Error::None);
  const QImage rendered = reader.render(0, QSize(240, 160));
  CHECK(!rendered.isNull());
  const QImage composite = patchy::ui::qimage_from_document(document, true);
  const double delta = mean_rgb_delta_over_white(rendered, composite);
  if (delta >= 6.0) {
    std::fprintf(stderr, "[pdf] editable paint export mean delta %f\n", delta);
    rendered.save(QStringLiteral("test-artifacts/ui_pdf_export_editable_paint_pdfium.png"));
    composite.save(QStringLiteral("test-artifacts/ui_pdf_export_editable_paint_composite.png"));
  }
  CHECK(delta < 6.0);
  // Gradient ends inside the ellipse, inside stroke at the rim, mask clip, and opacity.
  CHECK(rendered.pixelColor(34, 60).red() > rendered.pixelColor(106, 60).red());
  CHECK(rendered.pixelColor(106, 60).blue() > rendered.pixelColor(34, 60).blue());
  CHECK(color_close(rendered.pixelColor(70, 22), QColor(20, 120, 20), 6));  // the inside stroke band
  CHECK(color_close(rendered.pixelColor(180, 60), QColor(240, 180, 20), 2));  // inside the mask
  CHECK(color_close(rendered.pixelColor(140, 30), QColor(250, 250, 245), 2));  // clipped away
  CHECK(color_close(over_white(rendered).pixelColor(120, 130), QColor(125, 125, 122), 3));
}

// Qt's PDF engine writes no blend modes, so a Multiply layer is a barrier: everything
// below it merges into one image (reported), hidden layers vanish, and the page still
// composites exactly like the canvas.
void ui_pdf_export_editable_flattens_blend_modes_with_notice() {
  ensure_artifact_dir();
  patchy::Document document(100, 100, patchy::PixelFormat::rgba8());
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 72.0;
  document.add_pixel_layer("Base", solid_pixels(100, 100, patchy::PixelFormat::rgba8(), QColor(200, 200, 200)));
  auto hidden_pixels = solid_pixels(100, 100, patchy::PixelFormat::rgba8(), QColor(255, 0, 0));
  patchy::Layer hidden(document.allocate_layer_id(), "Hidden", std::move(hidden_pixels));
  hidden.set_visible(false);
  document.add_layer(std::move(hidden));
  auto multiply_pixels = solid_pixels(100, 100, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(multiply_pixels, QRect(20, 20, 60, 60), QColor(128, 128, 128));
  patchy::Layer multiply(document.allocate_layer_id(), "Darken", std::move(multiply_pixels));
  multiply.set_blend_mode(patchy::BlendMode::Multiply);
  document.add_layer(std::move(multiply));
  document.add_layer(solid_rect_shape_layer(document, "Badge", 70, 70, 95, 95, {10, 200, 30}, 0.0));

  const auto path = QStringLiteral("test-artifacts/ui_pdf_export_editable_blend.pdf");
  QFile::remove(path);
  std::vector<std::string> notices;
  patchy::ui::write_pdf_document_file(document, path, patchy::ui::PdfExportOptions{true, true}, &notices);
  bool merged_notice = false;
  for (const auto& notice : notices) {
    merged_notice = merged_notice || (notice.find("Merged") != std::string::npos &&
                                      notice.find("Darken") != std::string::npos);
    CHECK(notice.find("Hidden") == std::string::npos);  // hidden layers are not "lost", they were never drawn
  }
  CHECK(merged_notice);

  const QByteArray bytes = read_file_bytes(path);
  CHECK(bytes.contains("/Image"));
  CHECK(!bytes.contains("/FontFile"));  // no text on this page, so no embedded face

  QPdfDocument reader;
  CHECK(reader.load(path) == QPdfDocument::Error::None);
  const QImage rendered = reader.render(0, QSize(100, 100));
  CHECK(!rendered.isNull());
  // 200 x 128 / 255 = 100: the multiply result survives inside the merged chunk.
  CHECK(color_close(rendered.pixelColor(50, 50), QColor(100, 100, 100), 2));
  CHECK(color_close(rendered.pixelColor(5, 5), QColor(200, 200, 200), 2));
  // The shape above the barrier stays a real path drawn on top.
  CHECK(color_close(rendered.pixelColor(82, 82), QColor(10, 200, 30), 2));
  // The flat mode is untouched by the option: no notices, still one picture.
  std::vector<std::string> flat_notices;
  patchy::ui::write_pdf_document_file(document, QStringLiteral("test-artifacts/ui_pdf_export_flat_check.pdf"),
                                      patchy::ui::PdfExportOptions{true, false}, &flat_notices);
  CHECK(flat_notices.empty());
}

// The PDF Options dialog after the flatten-or-keep choice: the fidelity warning is
// visible exactly when layers are kept, and the export Scale combo (pixel-only) grays out
// with it; the choice itself is never persisted as a save default.
void ui_pdf_options_dialog_shows_editable_warning() {
  for (const bool keep_layers : {true, false}) {
    patchy::ui::ImageSaveOptions defaults;
    defaults.pdf_editable_layers = keep_layers;
    bool saw_dialog = false;
    QTimer::singleShot(0, [&saw_dialog, keep_layers] {
      auto* dialog = find_top_level_dialog(QStringLiteral("pdfSaveOptionsDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      auto* warning = dialog->findChild<QLabel*>(QStringLiteral("pdfEditableLayersWarning"));
      auto* scale = dialog->findChild<QComboBox*>(QStringLiteral("exportScaleCombo"));
      CHECK(warning != nullptr);
      CHECK(scale != nullptr);
      if (warning == nullptr || scale == nullptr) {
        dialog->reject();
        return;
      }
      CHECK(dialog->findChild<QCheckBox*>(QStringLiteral("pdfEditableLayersCheck")) == nullptr);
      CHECK(warning->isVisible() == keep_layers);
      CHECK(warning->text().contains(QStringLiteral("may not look")));
      CHECK(scale->isEnabled() == !keep_layers);
      scale->setCurrentIndex(std::max(0, scale->findData(4)));
      saw_dialog = true;
      dialog->accept();
    });
    const auto chosen =
        patchy::ui::prompt_image_save_options(nullptr, QStringLiteral("pdf"), defaults, /*for_export*/ true);
    CHECK(saw_dialog);
    CHECK(chosen.has_value());
    if (!chosen.has_value()) {
      continue;
    }
    CHECK(chosen->pdf_editable_layers == keep_layers);
    // Vectors scale with the page; the pixel scale only applies to the flattened image.
    CHECK(chosen->export_scale == (keep_layers ? 1 : 4));
  }
  patchy::ui::app_settings().setValue(QStringLiteral("saveOptions/exportScale"), 1);  // leave no 4x behind
}

// The flatten-or-keep question and its preference: "ask" raises the three-way dialog
// (Remember writes the policy), a set policy answers silently, Cancel answers nothing.
void ui_pdf_layer_choice_dialog_and_preference() {
  auto settings = patchy::ui::app_settings();
  const auto previous = settings.value(QStringLiteral("saveOptions/pdfLayerPolicy"));
  settings.setValue(QStringLiteral("saveOptions/pdfLayerPolicy"), QStringLiteral("ask"));
  patchy::ui::MainWindow window;
  show_window(window);

  const auto drive = [](const char* button_text, bool remember) {
    QTimer::singleShot(0, [button_text, remember] {
      auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("pdfLayersMessageBox")));
      CHECK(box != nullptr);
      if (box == nullptr) {
        return;
      }
      CHECK(box->informativeText().contains(QStringLiteral("may not look exactly like the canvas")));
      if (auto* check = box->checkBox()) {
        check->setChecked(remember);
      }
      if (button_text == nullptr) {
        box->reject();
        return;
      }
      for (auto* button : box->buttons()) {
        if (button->text().contains(QString::fromUtf8(button_text))) {
          button->click();
          return;
        }
      }
      CHECK(false);  // button not found
    });
  };

  drive(nullptr, false);
  auto choice = patchy::ui::MainWindowTestAccess::resolve_pdf_layer_choice(window, false);
  CHECK(!choice.has_value());
  CHECK(settings.value(QStringLiteral("saveOptions/pdfLayerPolicy")).toString() == QStringLiteral("ask"));

  drive("Flatten", false);
  choice = patchy::ui::MainWindowTestAccess::resolve_pdf_layer_choice(window, true);
  CHECK(choice.has_value() && !*choice);
  CHECK(settings.value(QStringLiteral("saveOptions/pdfLayerPolicy")).toString() == QStringLiteral("ask"));

  drive("Keep", true);
  choice = patchy::ui::MainWindowTestAccess::resolve_pdf_layer_choice(window, false);
  CHECK(choice.has_value() && *choice);
  CHECK(settings.value(QStringLiteral("saveOptions/pdfLayerPolicy")).toString() == QStringLiteral("editable"));

  // A remembered policy answers without any dialog (a stray box would fail the lookup below).
  bool stray_dialog = false;
  QTimer::singleShot(0, [&stray_dialog] {
    stray_dialog = find_top_level_dialog(QStringLiteral("pdfLayersMessageBox")) != nullptr;
  });
  choice = patchy::ui::MainWindowTestAccess::resolve_pdf_layer_choice(window, false);
  QApplication::processEvents();
  CHECK(choice.has_value() && *choice);
  CHECK(!stray_dialog);
  settings.setValue(QStringLiteral("saveOptions/pdfLayerPolicy"), QStringLiteral("flatten"));
  choice = patchy::ui::MainWindowTestAccess::resolve_pdf_layer_choice(window, true);
  CHECK(choice.has_value() && !*choice);

  if (previous.isValid()) {
    settings.setValue(QStringLiteral("saveOptions/pdfLayerPolicy"), previous);
  } else {
    settings.remove(QStringLiteral("saveOptions/pdfLayerPolicy"));
  }
}

// End to end through save_document_to_path: a layered document saved to .pdf follows the
// policy. "editable" writes the shape as a path; "flatten" writes one picture.
void ui_pdf_save_follows_layer_policy() {
  ensure_artifact_dir();
  auto settings = patchy::ui::app_settings();
  const auto previous = settings.value(QStringLiteral("saveOptions/pdfLayerPolicy"));
  patchy::Document built(120, 80, patchy::PixelFormat::rgba8());
  built.print_settings().horizontal_ppi = 72.0;
  built.print_settings().vertical_ppi = 72.0;
  built.add_pixel_layer("Paper", solid_pixels(120, 80, patchy::PixelFormat::rgba8(), QColor(240, 240, 240)));
  built.add_layer(solid_rect_shape_layer(built, "Badge", 10, 10, 60, 50, {200, 30, 30}, 0.0));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built), QStringLiteral("PDF Policy"));
  show_window(window);

  const auto read_back = [](const QString& path) {
    const QByteArray bytes = read_file_bytes(path);
    patchy::pdf::VectorReadOptions read_options;
    read_options.pixels_per_point = 1.0;
    return patchy::pdf::read_page_as_vectors(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                      static_cast<std::size_t>(bytes.size())),
        read_options);
  };
  patchy::ui::ImageSaveOptions options;

  settings.setValue(QStringLiteral("saveOptions/pdfLayerPolicy"), QStringLiteral("editable"));
  const auto editable_path = QStringLiteral("test-artifacts/ui_pdf_policy_editable.pdf");
  QFile::remove(editable_path);
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, editable_path, options));
  CHECK(QFileInfo::exists(editable_path));
  const auto editable = read_back(editable_path);
  CHECK(editable.shape_layers >= 1);
  CHECK(editable.image_layers >= 1);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("copy")));

  settings.setValue(QStringLiteral("saveOptions/pdfLayerPolicy"), QStringLiteral("flatten"));
  const auto flat_path = QStringLiteral("test-artifacts/ui_pdf_policy_flat.pdf");
  QFile::remove(flat_path);
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, flat_path, options));
  CHECK(QFileInfo::exists(flat_path));
  const auto flat = read_back(flat_path);
  CHECK(flat.shape_layers == 0);
  CHECK(flat.image_layers == 1);

  if (previous.isValid()) {
    settings.setValue(QStringLiteral("saveOptions/pdfLayerPolicy"), previous);
  } else {
    settings.remove(QStringLiteral("saveOptions/pdfLayerPolicy"));
  }
}

void ui_pdf_import_builds_one_layer_per_page() {
  ensure_artifact_dir();
  const auto path = QStringLiteral("test-artifacts/ui_pdf_import_two_pages.pdf");
  QFile::remove(path);
  {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(two_page_pdf_bytes());
  }
  CHECK(patchy::ui::pdf_import_is_available());
  CHECK(patchy::ui::is_pdf_extension(QStringLiteral("PDF")));
  const auto bytes = two_page_pdf_bytes();
  CHECK(patchy::ui::bytes_look_like_pdf(
      std::span(reinterpret_cast<const std::uint8_t*>(bytes.constData()), static_cast<std::size_t>(bytes.size()))));

  patchy::ui::PdfImportOptions options;
  options.pages = {0, 1};
  options.resolution_ppi = 72;
  QString error;
  auto result = patchy::ui::load_pdf_document(path, options, QString(), &error);
  CHECK(result.has_value());
  if (!result.has_value()) {
    return;
  }
  const auto& document = result->document;
  // Canvas is the per-axis maximum across pages; page 2 is narrower and taller.
  CHECK(document.width() == 144);
  CHECK(document.height() == 144);
  CHECK(document.layers().size() == 2);
  CHECK(document.layers()[0].name() == "Page 1");
  CHECK(document.layers()[1].name() == "Page 2");
  // Only the first page starts visible, matching the image-sequence import.
  CHECK(document.layers()[0].visible());
  CHECK(!document.layers()[1].visible());
  // The import resolution becomes the document's, so Image Size and print agree with it.
  CHECK(std::abs(document.print_settings().horizontal_ppi - 72.0) < 0.01);
  CHECK(std::abs(document.print_settings().vertical_ppi - 72.0) < 0.01);
  CHECK(!result->notices.empty());
  CHECK(result->notices.front().find("rasterized") != std::string::npos);

  // A single-page selection still works and names the layer after the real page number.
  patchy::ui::PdfImportOptions second_only;
  second_only.pages = {1};
  second_only.resolution_ppi = 72;
  auto single = patchy::ui::load_pdf_document(path, second_only, QString(), &error);
  CHECK(single.has_value());
  if (single.has_value()) {
    CHECK(single->document.layers().size() == 1);
    CHECK(single->document.layers()[0].name() == "Page 2");
    CHECK(single->document.width() == 72);
    CHECK(single->document.height() == 144);
  }

}

// Repeatedly runs `step` on a short timer while open_document_path blocks in the import
// dialog's exec() loop; `step` returns true when its work is done. Same shape as the raw
// develop dialog's driver in camera_raw_heif_tests.cpp.
void drive_modal_dialog(const std::shared_ptr<std::function<bool()>>& step, int attempts = 2400) {
  QTimer::singleShot(25, [step, attempts] {
    if (step == nullptr || !static_cast<bool>(*step)) {
      return;
    }
    if ((*step)()) {
      return;
    }
    if (attempts > 0) {
      drive_modal_dialog(step, attempts - 1);
    }
  });
}

void ui_pdf_import_dialog_opens_selected_pages() {
  const auto path = QStringLiteral("test-artifacts/ui_pdf_import_dialog.pdf");
  QFile::remove(path);
  {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(two_page_pdf_bytes());
  }
  patchy::ui::MainWindow window;
  show_window(window);

  // The Open command routes a .pdf through the page picker, so the dialog has to be
  // driven from a timer while open_document_path blocks in its exec() loop.
  auto clicked = std::make_shared<bool>(false);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [clicked] {
    auto* dialog = find_top_level_dialog(QStringLiteral("pdfImportDialog"));
    if (dialog == nullptr) {
      return false;
    }
    auto* pages = dialog->findChild<QListWidget*>(QStringLiteral("pdfImportPagesList"));
    auto* resolution = dialog->findChild<QSpinBox*>(QStringLiteral("pdfImportResolutionSpin"));
    auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportModeCombo"));
    auto* import_button = dialog->findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
    if (pages == nullptr || resolution == nullptr || mode == nullptr || import_button == nullptr) {
      return false;
    }
    CHECK(pages->count() == 2);
    // This test pins the FLATTEN path (layer-per-page raster); the mode persists in
    // settings and editable is the default, so it is selected explicitly.
    mode->setCurrentIndex(std::max(0, mode->findData(QStringLiteral("flatten"))));
    resolution->setValue(72);
    pages->selectAll();
    CHECK(import_button->isEnabled());
    import_button->click();
    *clicked = true;
    return true;
  };
  drive_modal_dialog(step);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  CHECK(*clicked);

  auto& opened = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(opened.width() == 144);
  CHECK(opened.height() == 144);
  CHECK(opened.layers().size() == 2);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == path);
}

// A one-page PDF with a filled rectangle and a text run, xref offsets computed.
QByteArray editable_pdf_bytes() {
  const std::string content =
      "0 0 1 rg 10 20 100 50 re f "
      "BT /F1 18 Tf 1 0 0 1 20 60 Tm 1 0 0 rg (Hello PDF) Tj ET";
  const std::vector<std::string> objects = {
      "<</Type/Catalog/Pages 2 0 R>>",
      "<</Type/Pages/Kids[3 0 R]/Count 1>>",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]/Contents 4 0 R"
      "/Resources<</Font<</F1 5 0 R>>>>>>",
      "<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream",
      "<</Type/Font/Subtype/Type1/BaseFont/Helvetica/Encoding/WinAnsiEncoding"
      "/FirstChar 32/LastChar 122/Widths[500]>>",
  };
  std::string pdf = "%PDF-1.7\n";
  std::vector<std::size_t> offsets;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    offsets.push_back(pdf.size());
    pdf += std::to_string(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
  }
  const std::size_t xref_offset = pdf.size();
  pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
  for (const auto offset : offsets) {
    pdf += QStringLiteral("%1 00000 n \n").arg(offset, 10, 10, QLatin1Char('0')).toStdString();
  }
  pdf += "trailer\n<</Size " + std::to_string(objects.size() + 1) +
         "/Root 1 0 R>>\nstartxref\n" + std::to_string(xref_offset) + "\n%%EOF\n";
  return QByteArray::fromStdString(pdf);
}

void ui_pdf_import_editable_mode_builds_vector_and_text_layers() {
  const auto path = QStringLiteral("test-artifacts/ui_pdf_import_editable.pdf");
  QFile::remove(path);
  {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(editable_pdf_bytes());
  }
  patchy::ui::MainWindow window;
  show_window(window);

  auto clicked = std::make_shared<bool>(false);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [clicked] {
    auto* dialog = find_top_level_dialog(QStringLiteral("pdfImportDialog"));
    if (dialog == nullptr) {
      return false;
    }
    auto* resolution = dialog->findChild<QSpinBox*>(QStringLiteral("pdfImportResolutionSpin"));
    auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportModeCombo"));
    auto* annotations = dialog->findChild<QCheckBox*>(QStringLiteral("pdfImportAnnotationsCheck"));
    auto* import_button = dialog->findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
    if (resolution == nullptr || mode == nullptr || annotations == nullptr || import_button == nullptr) {
      return false;
    }
    mode->setCurrentIndex(std::max(0, mode->findData(QStringLiteral("editable"))));
    // The raster-only toggles gray out in editable mode.
    CHECK(!annotations->isEnabled());
    resolution->setValue(144);
    import_button->click();
    *clicked = true;
    return true;
  };
  drive_modal_dialog(step);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  CHECK(*clicked);

  auto& opened = patchy::ui::MainWindowTestAccess::document(window);
  // 200 x 100 points at 144 ppi doubles the canvas, and the resolution rides along.
  CHECK(opened.width() == 400);
  CHECK(opened.height() == 200);
  CHECK(std::abs(opened.print_settings().horizontal_ppi - 144.0) < 0.01);
  CHECK(opened.layers().size() == 2);
  if (opened.layers().size() != 2) {
    return;
  }

  const auto& shape = opened.layers()[0];
  CHECK(patchy::layer_is_vector_shape(shape));
  CHECK(shape.vector_shape() != nullptr);
  if (shape.vector_shape() != nullptr) {
    CHECK(shape.vector_shape()->fill.color.blue == 255);
  }

  // The text layer arrived rendered: the post-open pass rasterized it through the
  // reader's matrix and stamped the standard transformed-text metadata.
  const auto& text = opened.layers()[1];
  CHECK(patchy::layer_is_text(text));
  CHECK(text.metadata().at(patchy::kLayerMetadataText) == "Hello PDF");
  CHECK(!text.metadata().contains(patchy::kLayerMetadataPdfPendingText));
  CHECK(text.metadata().contains(patchy::kLayerMetadataTextTransform));
  CHECK(!text.pixels().empty());
  // 18 pt at 144 ppi renders 36 px tall, so the glyph block must be at least that
  // wide for a nine-character run, and its top must sit above the baseline row.
  CHECK(text.bounds().width > 36);
  const double baseline_document_y = 200.0 - 60.0 * 2.0;  // flipped, then scaled
  CHECK(text.bounds().y < static_cast<std::int32_t>(baseline_document_y));
}

// The whole feature end to end on a real document: the untracked brochure fixture
// imports editable, and the composite is saved as a visual-QA artifact.
void ui_pdf_local_brochure_editable_import_composites_if_available() {
  const auto fixture =
      QString::fromStdString(patchy::test::local_format_fixture_path("pdf", "HoloVCS_C2_A4_Brochure.pdf").string());
  if (!QFileInfo::exists(fixture)) {
    std::printf("[SKIP] ui_pdf_local_brochure_editable_import_composites_if_available (no local fixture)\n");
    return;
  }
  ensure_artifact_dir();
  patchy::ui::MainWindow window;
  show_window(window);

  auto clicked = std::make_shared<bool>(false);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [clicked] {
    auto* dialog = find_top_level_dialog(QStringLiteral("pdfImportDialog"));
    if (dialog == nullptr) {
      return false;
    }
    auto* resolution = dialog->findChild<QSpinBox*>(QStringLiteral("pdfImportResolutionSpin"));
    auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportModeCombo"));
    auto* import_button = dialog->findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
    if (resolution == nullptr || mode == nullptr || import_button == nullptr) {
      return false;
    }
    mode->setCurrentIndex(std::max(0, mode->findData(QStringLiteral("editable"))));
    resolution->setValue(96);
    import_button->click();
    *clicked = true;
    return true;
  };
  drive_modal_dialog(step);
  patchy::ui::MainWindowTestAccess::open_document_path(window, fixture);
  CHECK(*clicked);

  auto& opened = patchy::ui::MainWindowTestAccess::document(window);
  // A4 at 96 ppi.
  CHECK(std::abs(opened.width() - 794) <= 2);
  CHECK(std::abs(opened.height() - 1123) <= 2);
  CHECK(opened.layers().size() > 400);

  int rendered_text = 0;
  int pending_text = 0;
  int smart_objects_with_pixels = 0;
  for (const auto& layer : opened.layers()) {
    if (patchy::layer_is_text(layer)) {
      if (layer.metadata().contains(patchy::kLayerMetadataPdfPendingText)) {
        ++pending_text;
      } else if (!layer.pixels().empty()) {
        ++rendered_text;
      }
    } else if (patchy::layer_is_smart_object(layer) && !layer.pixels().empty()) {
      ++smart_objects_with_pixels;
    }
  }
  // Every text layer got rasterized by the post-open pass, and both images decoded.
  CHECK(pending_text == 0);
  CHECK(rendered_text >= 40);
  CHECK(smart_objects_with_pixels == 2);

  const auto composite = patchy::ui::qimage_from_document(opened, false);
  CHECK(!composite.isNull());
  CHECK(composite.save(QStringLiteral("test-artifacts/ui_pdf_brochure_editable.png")));
  // Not blank: the page background is warm off-white with dark panels and text.
  int dark_pixels = 0;
  for (int y = 0; y < composite.height(); y += 7) {
    for (int x = 0; x < composite.width(); x += 7) {
      if (qGray(composite.pixel(x, y)) < 96) {
        ++dark_pixels;
      }
    }
  }
  CHECK(dark_pixels > 200);
}
#endif  // PATCHY_HAVE_QT_PDF

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
      auto* copies = dialog->findChild<QSpinBox*>(QStringLiteral("printCopiesSpin"));
      auto* scale_to_fit = dialog->findChild<QCheckBox*>(QStringLiteral("printScaleToFitCheck"));
      auto* scale = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("printScalePercentSpin"));
      auto* resolution = dialog->findChild<QLabel*>(QStringLiteral("printResolutionValueLabel"));
      auto* units = dialog->findChild<QComboBox*>(QStringLiteral("printUnitsCombo"));
      auto* scale_size = dialog->findChild<QLabel*>(QStringLiteral("printScaleSizeLabel"));
      auto* image_size = dialog->findChild<QLabel*>(QStringLiteral("printImageSizeLabel"));
      auto* center = dialog->findChild<QCheckBox*>(QStringLiteral("printCenterCheck"));
      auto* crop_marks = dialog->findChild<QCheckBox*>(QStringLiteral("printCropMarksCheck"));
      auto* system_dialog = dialog->findChild<QPushButton*>(QStringLiteral("printSystemDialogButton"));
      CHECK(printer != nullptr);
      CHECK(print_button != nullptr);
      // Present and gated like Print; never clicked here, it would park a nested
      // modal loop inside this driver lambda.
      CHECK(system_dialog != nullptr);
      CHECK(system_dialog->isEnabled() == printer->isEnabled());
      CHECK(copies != nullptr);
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
      // Copies opens at one and never goes below it, matching Photoshop's Print dialog.
      CHECK(copies->value() == 1);
      CHECK(copies->minimum() == 1);
      CHECK(copies->maximum() >= 99);
      copies->setValue(0);
      CHECK(copies->value() == 1);
      copies->setValue(12);
      CHECK(copies->value() == 12);
      copies->setValue(1);
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

void ui_tile_preview_follows_document_switches_and_large_edits() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
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
  const auto ghost_stroke_point =
      second_canvas->widget_position_for_document_point(QPoint(180 + 360, 120));
  CHECK(second_canvas->rect().contains(ghost_stroke_point));
  CHECK(recorder.region().contains(ghost_stroke_point));
  second_canvas->removeEventFilter(&recorder);
  {
    const auto stroked = render_widget_image(*second_canvas);
    CHECK(color_close(stroked.pixelColor(ghost_stroke_point), QColor(20, 220, 120), 40));
  }

  // Back on the first tab the mode is still on; toggling off restores the backdrop.
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
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
      {"ui_tile_preview_follows_document_switches_and_large_edits",
       ui_tile_preview_follows_document_switches_and_large_edits},
      {"ui_shift_seams_action_wraps_document_and_toggles_back",
       ui_shift_seams_action_wraps_document_and_toggles_back},
      {"ui_canvas_tiling_mode_paints_ghost_tiles_live", ui_canvas_tiling_mode_paints_ghost_tiles_live},
      {"ui_qimage_multiply_uses_empty_backdrop_as_transparent",
       ui_qimage_multiply_uses_empty_backdrop_as_transparent},
      {"ui_print_layout_and_pdf_output_work", ui_print_layout_and_pdf_output_work},
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_export_page_size_and_round_trip", ui_pdf_export_page_size_and_round_trip},
      {"ui_pdf_export_writes_transparency_as_soft_mask", ui_pdf_export_writes_transparency_as_soft_mask},
      {"ui_pdf_export_editable_keeps_layers_and_matches_composite",
       ui_pdf_export_editable_keeps_layers_and_matches_composite},
      {"ui_pdf_export_editable_gradients_clips_and_opacity_render_like_canvas",
       ui_pdf_export_editable_gradients_clips_and_opacity_render_like_canvas},
      {"ui_pdf_export_editable_flattens_blend_modes_with_notice",
       ui_pdf_export_editable_flattens_blend_modes_with_notice},
      {"ui_pdf_options_dialog_shows_editable_warning", ui_pdf_options_dialog_shows_editable_warning},
      {"ui_pdf_layer_choice_dialog_and_preference", ui_pdf_layer_choice_dialog_and_preference},
      {"ui_pdf_save_follows_layer_policy", ui_pdf_save_follows_layer_policy},
      {"ui_pdf_import_builds_one_layer_per_page", ui_pdf_import_builds_one_layer_per_page},
      {"ui_pdf_import_dialog_opens_selected_pages", ui_pdf_import_dialog_opens_selected_pages},
      {"ui_pdf_import_editable_mode_builds_vector_and_text_layers",
       ui_pdf_import_editable_mode_builds_vector_and_text_layers},
      {"ui_pdf_local_brochure_editable_import_composites_if_available",
       ui_pdf_local_brochure_editable_import_composites_if_available},
#endif
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
