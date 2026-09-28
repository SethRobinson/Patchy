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
#include "ui/edit_conversions.hpp"
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
#include <QRegion>
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

void ui_copy_paste_and_transform_pasted_layer_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->fit_to_view();
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
  CHECK(pasted_rect->topLeft() == QPoint(qRound((1024 - copied_selection_rect->width()) / 2.0),
                                        qRound((768 - copied_selection_rect->height()) / 2.0)));
  CHECK(pasted_rect->size() == copied_selection_rect->size());
  CHECK(!canvas->has_selection());
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(false);
  const auto pasted_center = canvas->widget_position_for_document_point(pasted_rect->center());
  drag(*canvas, pasted_center, pasted_center + QPoint(30, 30));
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
  // No Shift: a plain corner drag holds the aspect ratio by default.
  drag(*canvas, bottom_right, bottom_right + QPoint(180, 40));
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

void ui_paste_clears_selection_and_undo_restores_it() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(255, 80, 20));
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, QPoint(60, 60), QPoint(180, 140));
  const auto copied_selection_rect = canvas->selected_document_rect();
  CHECK(copied_selection_rect.has_value());
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  // Photoshop parity: the marquee does not stay live over the pasted layer.
  CHECK(!canvas->has_selection());

  // The deselect rides the Paste undo entry.
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_rect() == copied_selection_rect);
}

void ui_external_clipboard_image_paste_creates_centered_layer() {
  QApplication::clipboard()->clear();

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->fit_to_view();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  QImage image(18, 12, QImage::Format_RGBA8888);
  image.fill(QColor(20, 180, 80, 255));
  QApplication::clipboard()->setImage(image);
  QApplication::processEvents();

  const auto layers_before = layer_list->count();
  require_action(window, "editPasteAction")->trigger();
  // At fit zoom this tiny image can be covered by the Move tool's handles.
  canvas->set_show_transform_controls(false);
  QApplication::processEvents();

  CHECK(layer_list->count() == layers_before + 1);
  const auto pasted_rect = canvas->active_layer_document_rect();
  CHECK(pasted_rect.has_value());
  CHECK(pasted_rect->topLeft() == QPoint((1024 - image.width()) / 2, (768 - image.height()) / 2));
  CHECK(pasted_rect->size() == image.size());
  CHECK(color_close(canvas_pixel(*canvas, pasted_rect->center()), QColor(20, 180, 80), 35));
  QApplication::clipboard()->clear();
}

// A file copied in a file manager puts URLs and no bitmap on the clipboard: Paste
// adds the supported image files as layers (Files as Layers, docs/import.md), one
// undo step, and a non-image file keeps the usual "no image" refusal.
void ui_paste_file_urls_adds_layers() {
  QApplication::clipboard()->clear();
  ensure_artifact_dir();
  const auto dir = QFileInfo(QStringLiteral("test-artifacts/paste-files")).absoluteFilePath();
  CHECK(QDir().mkpath(dir));
  const auto write_png = [&](const QString& name, QColor color) {
    QImage image(6, 4, QImage::Format_RGBA8888);
    image.fill(color);
    const auto path = QDir::toNativeSeparators(dir + QLatin1Char('/') + name);
    QFile::remove(path);
    CHECK(image.save(path));
    return path;
  };
  const auto first = write_png(QStringLiteral("first.png"), QColor(200, 20, 20, 255));
  const auto second = write_png(QStringLiteral("second.png"), QColor(20, 200, 20, 255));
  const auto note = QDir::toNativeSeparators(dir + QStringLiteral("/note.txt"));
  {
    QFile file(note);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("not an image");
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  const auto layers_before = layer_list->count();
  const auto undo_before = patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, canvas);

  auto* files = new QMimeData();
  files->setUrls({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)});
  QApplication::clipboard()->setMimeData(files);
  QApplication::processEvents();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 2);
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, canvas) == undo_before + 1);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Added 2 layer")));
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto& layers = std::as_const(document).layers();
  CHECK(layers.size() >= 2);
  CHECK(layers[layers.size() - 2].name() == "first");
  CHECK(layers.back().name() == "second");
  CHECK(document.active_layer_id() == layers.back().id());
  CHECK(layer_list->selectedItems().size() == 2);

  auto* text_file = new QMimeData();
  text_file->setUrls({QUrl::fromLocalFile(note)});
  QApplication::clipboard()->setMimeData(text_file);
  QApplication::processEvents();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 2);
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, canvas) == undo_before + 1);
  QApplication::clipboard()->clear();
}

void ui_external_clipboard_image_paste_overrides_internal_payload() {
  QApplication::clipboard()->clear();

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->fit_to_view();
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
  canvas->set_show_transform_controls(false);
  QApplication::processEvents();

  CHECK(layer_list->count() == layers_before + 1);
  const auto pasted_rect = canvas->active_layer_document_rect();
  CHECK(pasted_rect.has_value());
  CHECK(pasted_rect->topLeft() == QPoint((1024 - image.width()) / 2, (768 - image.height()) / 2));
  CHECK(pasted_rect->size() == image.size());
  CHECK(color_close(canvas_pixel(*canvas, pasted_rect->center()), QColor(220, 40, 140), 35));
  QApplication::clipboard()->clear();
}

enum class PasteOrderPayload { InternalPixels, LayerStack, ExternalImage, SvgStack };

std::vector<std::string> prepare_paste_order_clipboard(patchy::ui::MainWindow& window,
                                                      PasteOrderPayload payload) {
  QApplication::clipboard()->clear();
  patchy::Document source(48, 32, patchy::PixelFormat::rgba8());
  source.add_pixel_layer("Source lower", solid_pixels(48, 32, patchy::PixelFormat::rgba8(), QColor(Qt::red)));
  source.add_pixel_layer("Source upper", solid_pixels(48, 32, patchy::PixelFormat::rgba8(), QColor(Qt::green)));
  window.add_document_session(std::move(source), QStringLiteral("Clipboard source"));
  show_window(window);
  switch (payload) {
    case PasteOrderPayload::InternalPixels:
      require_action(window, "editCopyMergedAction")->trigger();
      return {"Pasted Layer"};
    case PasteOrderPayload::LayerStack: {
      auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
      CHECK(list != nullptr);
      require_layer_item(*list, QStringLiteral("Source lower"))->setSelected(true);
      CHECK(list->selectedItems().size() == 2);
      require_action(window, "editCopyAction")->trigger();
      return {"Source lower copy", "Source upper copy"};
    }
    case PasteOrderPayload::ExternalImage: {
      QImage image(12, 10, QImage::Format_RGBA8888);
      image.fill(Qt::red);
      QApplication::clipboard()->setImage(image);
      return {"Pasted Layer"};
    }
    case PasteOrderPayload::SvgStack:
      QApplication::clipboard()->setText(QStringLiteral(
          "<svg xmlns='http://www.w3.org/2000/svg' width='48' height='32'>"
          "<rect id='SvgLower' width='20' height='20' fill='red'/>"
          "<rect id='SvgUpper' x='10' width='20' height='20' fill='green'/></svg>"));
      return {"SvgLower", "SvgUpper"};
  }
  throw std::runtime_error("Unknown clipboard payload");
}

void ui_paste_inserts_above_topmost_selected_layer() {
  for (const auto payload : {PasteOrderPayload::InternalPixels, PasteOrderPayload::LayerStack,
                             PasteOrderPayload::ExternalImage, PasteOrderPayload::SvgStack}) {
    for (const bool in_place : {false, true}) {
      for (const bool nested : {false, true}) {
        for (const bool multiple : {false, true}) {
          patchy::ui::MainWindow window;
          const auto pasted_names = prepare_paste_order_clipboard(window, payload);
          patchy::Document target(48, 32, patchy::PixelFormat::rgba8());
          patchy::Layer folder(target.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
          const auto folder_id = folder.id();
          std::array<patchy::LayerId, 3> ids{};
          const std::array<const char*, 3> names{"Lower", "Middle", "Upper"};
          for (std::size_t index = 0; index < names.size(); ++index) {
            patchy::Layer layer(target.allocate_layer_id(), names[index],
                                solid_pixels(48, 32, patchy::PixelFormat::rgba8(), QColor(Qt::blue)));
            ids[index] = layer.id();
            if (nested) folder.add_child(std::move(layer));
            else target.add_layer(std::move(layer));
          }
          if (nested) target.add_layer(std::move(folder));
          target.set_active_layer(ids[1]);
          window.add_document_session(std::move(target), QStringLiteral("Paste layer order"));
          auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
          CHECK(list != nullptr);
          auto& document = patchy::ui::MainWindowTestAccess::document(window);
          const auto& read_only = std::as_const(document);
          if (multiple) {
            list->setCurrentItem(require_layer_item(*list, QStringLiteral("Lower")),
                                 QItemSelectionModel::ClearAndSelect);
            require_layer_item(*list, QStringLiteral("Middle"))->setSelected(true);
            CHECK(document.active_layer_id() == ids[0]);
            CHECK(list->selectedItems().size() == 2);
          }
          const auto before = patchy::layer_tree_signature(read_only.layers());
          const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
          require_action(window, in_place ? "editPasteInPlaceAction" : "editPasteAction")->trigger();
          QApplication::processEvents();
          const auto pasted_id = document.active_layer_id();
          CHECK(pasted_id.has_value());
          const auto check_pasted = [&] {
            const auto& siblings = nested ? read_only.find_layer(folder_id)->children() : read_only.layers();
            CHECK(siblings.size() == 3 + pasted_names.size());
            CHECK(siblings[0].id() == ids[0] && siblings[1].id() == ids[1]);
            CHECK(siblings.back().id() == ids[2]);
            for (std::size_t index = 0; index < pasted_names.size(); ++index) {
              CHECK(siblings[2 + index].name() == pasted_names[index]);
            }
            CHECK(siblings[1 + pasted_names.size()].id() == *pasted_id);
            CHECK(document.active_layer_id() == pasted_id);
            CHECK(list->selectedItems().size() == 1);
            auto* top = require_layer_item(*list, QString::fromStdString(pasted_names.back()));
            CHECK(top->isSelected());
            auto* bottom = require_layer_item(*list, QString::fromStdString(pasted_names.front()));
            CHECK(list->row(bottom) + 1 == list->row(require_layer_item(*list, QStringLiteral("Middle"))));
          };
          check_pasted();
          CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
          patchy::ui::MainWindowTestAccess::undo(window);
          CHECK(patchy::layer_tree_signature(read_only.layers()) == before);
          patchy::ui::MainWindowTestAccess::redo(window);
          check_pasted();
        }
      }
    }
  }
  QApplication::clipboard()->clear();
}

void ui_paste_above_selected_folder_or_at_top_without_selection() {
  for (const bool selected : {false, true}) {
    patchy::ui::MainWindow window;
    prepare_paste_order_clipboard(window, PasteOrderPayload::ExternalImage);
    patchy::Document target(48, 32, patchy::PixelFormat::rgba8());
    patchy::Layer folder(target.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
    const auto folder_id = folder.id();
    patchy::Layer child(target.allocate_layer_id(), "Child",
                        solid_pixels(48, 32, patchy::PixelFormat::rgba8(), QColor(Qt::blue)));
    const auto child_id = child.id();
    folder.add_child(std::move(child));
    target.add_layer(std::move(folder));
    const auto upper_id = target.add_pixel_layer(
        "Upper", solid_pixels(48, 32, patchy::PixelFormat::rgba8(), QColor(Qt::white))).id();
    target.set_active_layer(child_id);
    window.add_document_session(std::move(target), QStringLiteral("Paste folder anchor"));
    auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(list != nullptr);
    if (selected) {
      // A selected ancestor is above its child, even when the child is active.
      require_layer_item(*list, QStringLiteral("Folder"))->setSelected(true);
      CHECK(list->selectedItems().size() == 2);
    } else {
      list->clearSelection();
      CHECK(list->selectedItems().empty());
    }
    require_action(window, "editPasteAction")->trigger();
    const auto& document = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(document.layers().size() == 3);
    CHECK(document.layers()[0].id() == folder_id);
    CHECK(document.layers()[selected ? 2 : 1].id() == upper_id);
    CHECK(document.layers()[selected ? 1 : 2].id() == document.active_layer_id());
    CHECK(document.find_layer(folder_id)->children().size() == 1);
    CHECK(document.find_layer(folder_id)->children()[0].id() == child_id);
  }
  QApplication::clipboard()->clear();
}

void prepare_paste_selection(patchy::ui::MainWindow& window, const char* copy_action) {
  patchy::Document document(1200, 900, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(220, 50, 30));
  patchy::Layer layer(document.allocate_layer_id(), "Clipboard source", std::move(pixels));
  layer.set_bounds(patchy::Rect{860, 660, 80, 60});
  document.add_layer(std::move(layer));
  window.add_document_session(std::move(document), QStringLiteral("Clipboard source"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->select_active_layer_opaque_pixels();
  CHECK(canvas->selected_document_rect() == QRect(860, 660, 80, 60));
  require_action(window, copy_action)->trigger();
  QApplication::processEvents();
}

void ui_paste_selection_centers_in_panned_zoomed_view() {
  for (const auto* copy_action : {"editCopyAction", "editCopyMergedAction", "editCutAction"}) {
    patchy::ui::MainWindow window;
    prepare_paste_selection(window, copy_action);
    auto* canvas = require_canvas(window);
    const auto& source = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(source.layers().front().pixels().pixel(0, 0)[3] ==
          (std::strcmp(copy_action, "editCutAction") == 0 ? 0 : 255));
    canvas->zoom_to_document_rect(QRect(180, 120, 400, 300));
    require_action(window, "editPasteAction")->trigger();
    QApplication::processEvents();
    CHECK(canvas->active_layer_document_rect() == QRect(340, 240, 80, 60));
    CHECK(!canvas->has_selection());
    const auto& doc = patchy::ui::MainWindowTestAccess::document(window);
    const auto* pasted = doc.find_layer(*doc.active_layer_id());
    CHECK(pasted != nullptr);
    CHECK(pasted->pixels().width() == 80 && pasted->pixels().height() == 60);
    const auto expected = solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(220, 50, 30));
    CHECK(pasted->pixels().byte_size() == expected.byte_size());
    CHECK(std::memcmp(pasted->pixels().data().data(), expected.data().data(), expected.byte_size()) == 0);

    require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
    CHECK(canvas->selected_document_rect() == QRect(860, 660, 80, 60));
    require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
    CHECK(canvas->active_layer_document_rect() == QRect(340, 240, 80, 60));
    CHECK(!canvas->has_selection());
  }
}

void ui_paste_selection_clamps_to_each_canvas_edge() {
  patchy::ui::MainWindow window;
  prepare_paste_selection(window, "editCopyAction");
  auto* canvas = require_canvas(window);
  const std::array<std::pair<QRect, QPoint>, 4> cases{{
      {QRect(0, 0, 20, 20), QPoint(0, 0)},
      {QRect(1180, 0, 20, 20), QPoint(1120, 0)},
      {QRect(0, 880, 20, 20), QPoint(0, 840)},
      {QRect(1180, 880, 20, 20), QPoint(1120, 840)},
  }};
  for (const auto& [view, expected_origin] : cases) {
    canvas->zoom_to_document_rect(view);
    require_action(window, "editPasteAction")->trigger();
    QApplication::processEvents();
    CHECK(canvas->active_layer_document_rect() == QRect(expected_origin, QSize(80, 60)));
  }
}

void ui_paste_in_place_shortcut_restores_cut_coordinates() {
  patchy::ui::MainWindow window;
  prepare_paste_selection(window, "editCutAction");
  auto* canvas = require_canvas(window);
  canvas->zoom_to_document_rect(QRect(180, 120, 400, 300));
  auto* paste_in_place = require_hotkey_action(window, QStringLiteral("edit.paste_in_place"));
  CHECK(paste_in_place->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
  window.activateWindow();
  canvas->setFocus();
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::document(window).layers().size() == 2U);
  CHECK(canvas->active_layer_document_rect() == QRect(860, 660, 80, 60));
  CHECK(!canvas->has_selection());
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  CHECK(canvas->selected_document_rect() == QRect(860, 660, 80, 60));
  require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
  CHECK(canvas->active_layer_document_rect() == QRect(860, 660, 80, 60));
  CHECK(!canvas->has_selection());
}

void ui_paste_selection_between_documents_clamps_and_preserves_size() {
  patchy::ui::MainWindow window;
  prepare_paste_selection(window, "editCopyAction");
  struct Destination {
    QSize canvas;
    QPoint centered;
    QPoint in_place;
  };
  const std::array<Destination, 4> cases{{
      {QSize(1600, 1200), QPoint(760, 570), QPoint(860, 660)},
      {QSize(200, 150), QPoint(60, 45), QPoint(120, 90)},
      {QSize(40, 120), QPoint(-20, 30), QPoint(-20, 60)},
      {QSize(120, 30), QPoint(20, -15), QPoint(40, -15)},
  }};
  for (const auto& destination : cases) {
    window.add_document_session(
        patchy::Document(destination.canvas.width(), destination.canvas.height(), patchy::PixelFormat::rgba8()),
        QStringLiteral("Paste destination"));
    QApplication::processEvents();
    auto* canvas = require_canvas(window);
    canvas->fit_to_view();
    require_action(window, "editPasteAction")->trigger();
    CHECK(canvas->active_layer_document_rect() == QRect(destination.centered, QSize(80, 60)));
    require_action(window, "editPasteInPlaceAction")->trigger();
    CHECK(canvas->active_layer_document_rect() == QRect(destination.in_place, QSize(80, 60)));
  }
}

void ui_external_clipboard_paste_in_place_falls_back_to_view_center() {
  patchy::ui::MainWindow window;
  prepare_paste_selection(window, "editCopyAction");
  QImage image(80, 60, QImage::Format_RGBA8888);
  image.fill(QColor(30, 190, 80));
  QApplication::clipboard()->setImage(image);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  for (const auto* action : {"editPasteAction", "editPasteInPlaceAction"}) {
    // A paste activates the Move tool, whose options row can differ in height
    // from the previous tool's and resize the viewport, so re-frame the view
    // before each paste: the check is about the fallback to the view center.
    canvas->zoom_to_document_rect(QRect(180, 120, 400, 300));
    require_action(window, action)->trigger();
    CHECK(canvas->active_layer_document_rect() == QRect(340, 240, 80, 60));
  }
  QApplication::clipboard()->clear();
}

void ui_paste_in_place_obeys_document_and_channel_guards() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  auto* action = require_action(window, "editPasteInPlaceAction");
  CHECK(!action->isEnabled());
  patchy::ui::MainWindowTestAccess::create_default_document(window);
  QApplication::processEvents();
  CHECK(action->isEnabled());
  require_hotkey_action(window, QStringLiteral("select.quick_mask"))->trigger();
  CHECK(!action->isEnabled());
  require_hotkey_action(window, QStringLiteral("select.quick_mask"))->trigger();
  CHECK(action->isEnabled());
}

// Paints an opaque 60x45 rect into the startup document and returns its bounds,
// so a transform session starts from a known, deliberately non-square aspect.
std::optional<QRect> fill_aspect_probe_rect(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas) {
  canvas.set_tool(patchy::ui::CanvasTool::Marquee);
  drag(canvas, canvas.widget_position_for_document_point(QPoint(80, 70)),
       canvas.widget_position_for_document_point(QPoint(140, 115)));
  const auto rect = canvas.selected_document_rect();
  canvas.set_primary_color(QColor(230, 60, 35));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  return rect;
}

// Drags the bottom-right transform handle by the given document-space delta and
// commits, returning the committed opaque bounds.
std::optional<QRect> transform_bottom_right_by(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas,
                                               QRect from, QPoint delta, Qt::KeyboardModifiers modifiers) {
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas.free_transform_active());
  drag(canvas, canvas.widget_position_for_document_point(from.bottomRight() + QPoint(1, 1)),
       canvas.widget_position_for_document_point(from.bottomRight() + delta), modifiers);
  QApplication::processEvents();
  send_key(canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas.free_transform_active());
  return canvas.active_layer_document_rect();
}

// Photoshop CC 2019 reversed this: a plain corner drag scales proportionally and
// Shift is what releases the lock. The drag delta below is deliberately lopsided
// (+90 wide, +4 tall) so the two modes cannot produce a similar rectangle.
void ui_transform_shift_frees_aspect_ratio_by_default() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  CHECK(!canvas->shift_keeps_transform_aspect());

  const auto filled_rect = fill_aspect_probe_rect(window, *canvas);
  CHECK(filled_rect.has_value());
  const auto source_ratio = static_cast<double>(filled_rect->width()) / filled_rect->height();

  const auto locked = transform_bottom_right_by(window, *canvas, *filled_rect, QPoint(90, 4), Qt::NoModifier);
  CHECK(locked.has_value());
  CHECK(locked->width() > filled_rect->width() + 40);
  const auto locked_ratio = static_cast<double>(locked->width()) / locked->height();
  CHECK(std::abs(locked_ratio - source_ratio) < 0.2);

  const auto freed = transform_bottom_right_by(window, *canvas, *locked, QPoint(90, 4), Qt::ShiftModifier);
  CHECK(freed.has_value());
  CHECK(freed->width() > locked->width() + 60);
  CHECK(freed->height() < locked->height() + 20);
  const auto freed_ratio = static_cast<double>(freed->width()) / freed->height();
  CHECK(freed_ratio > locked_ratio + 0.5);
}

// A proportional corner drag scales by the pointer's distance from the anchor
// projected onto the box diagonal. Along the diagonal the corner lands under
// the pointer; a pull that leans against the diagonal shrinks the box. The old
// rule let the axis pulled harder win, so the (-30, +30) pull below grew the
// box, and the short side scaled faster than the long one.
void ui_transform_proportional_corner_follows_diagonal_projection() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  CHECK(!canvas->shift_keeps_transform_aspect());

  const auto filled_rect = fill_aspect_probe_rect(window, *canvas);
  CHECK(filled_rect.has_value());
  if (!filled_rect.has_value()) {
    return;
  }
  const auto source_ratio = static_cast<double>(filled_rect->width()) / filled_rect->height();

  // Along the diagonal the corner lands under the pointer: twice the size.
  const auto doubled = transform_bottom_right_by(window, *canvas, *filled_rect,
                                                 QPoint(filled_rect->width(), filled_rect->height()), Qt::NoModifier);
  CHECK(doubled.has_value());
  if (!doubled.has_value()) {
    return;
  }
  CHECK(std::abs(doubled->left() - filled_rect->left()) <= 1 && std::abs(doubled->top() - filled_rect->top()) <= 1);
  CHECK(std::abs(doubled->width() - 2 * filled_rect->width()) <= 2);
  CHECK(std::abs(doubled->height() - 2 * filled_rect->height()) <= 2);

  // Pulled in by 30 and out by 30: the old rule grew the box to the vertical
  // pull (about +40 wide); the projection shrinks it slightly.
  const auto leaned = transform_bottom_right_by(window, *canvas, *doubled, QPoint(-30, 30), Qt::NoModifier);
  CHECK(leaned.has_value());
  if (!leaned.has_value()) {
    return;
  }
  CHECK(leaned->width() < doubled->width());
  const auto leaned_ratio = static_cast<double>(leaned->width()) / leaned->height();
  CHECK(std::abs(leaned_ratio - source_ratio) < 0.05);
}

void ui_transform_shift_aspect_preference_restores_legacy() {
  SettingsValueRestorer restore_preference(QStringLiteral("input/shiftKeepsTransformAspect"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("input/shiftKeepsTransformAspect"), true);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  CHECK(canvas->shift_keeps_transform_aspect());

  const auto filled_rect = fill_aspect_probe_rect(window, *canvas);
  CHECK(filled_rect.has_value());
  const auto source_ratio = static_cast<double>(filled_rect->width()) / filled_rect->height();

  // The pairing is inverted: the plain drag now distorts.
  const auto freed = transform_bottom_right_by(window, *canvas, *filled_rect, QPoint(90, 4), Qt::NoModifier);
  CHECK(freed.has_value());
  const auto freed_ratio = static_cast<double>(freed->width()) / freed->height();
  CHECK(freed_ratio > source_ratio + 0.5);

  const auto locked = transform_bottom_right_by(window, *canvas, *freed, QPoint(90, 4), Qt::ShiftModifier);
  CHECK(locked.has_value());
  const auto locked_ratio = static_cast<double>(locked->width()) / locked->height();
  CHECK(std::abs(locked_ratio - freed_ratio) < 0.2);
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
  drag(*canvas, handle, expanded);
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

// Arrow keys during a Free Transform (Ctrl+T) must nudge the bounding box (and the
// previewed pixels) together. The box used to stay put while the keys fell through to a
// destructive layer move — regression for the Ctrl+T pixel-nudge desync.
void ui_free_transform_arrow_keys_nudge_bounding_box() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 70)),
       canvas->widget_position_for_document_point(QPoint(140, 115)));
  const auto filled_rect = canvas->selected_document_rect();
  CHECK(filled_rect.has_value());
  canvas->set_primary_color(QColor(60, 200, 120));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  const auto before = canvas->transform_controls_state();
  CHECK(before.has_value());
  CHECK(before->active);

  // 3px right + 2px down: the box reference position must follow the nudge exactly.
  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Down);
  send_key(*canvas, Qt::Key_Down);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  const auto after = canvas->transform_controls_state();
  CHECK(after.has_value());
  CHECK(std::abs((after->reference_position.x() - before->reference_position.x()) - 3.0) < 0.001);
  CHECK(std::abs((after->reference_position.y() - before->reference_position.y()) - 2.0) < 0.001);

  // Shift makes each step 10px.
  send_key(*canvas, Qt::Key_Left, Qt::ShiftModifier);
  QApplication::processEvents();
  const auto after_shift = canvas->transform_controls_state();
  CHECK(after_shift.has_value());
  CHECK(std::abs((after_shift->reference_position.x() - after->reference_position.x()) + 10.0) < 0.001);
  CHECK(std::abs(after_shift->reference_position.y() - after->reference_position.y()) < 0.001);

  // Net nudge is (-7, +2). Committing applies it as a translation of the pending
  // transform: the layer moves with the box and keeps its size (no destructive resize).
  const auto box_center_before_commit = after_shift->reference_position;
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  const auto committed_rect = canvas->active_layer_document_rect();
  CHECK(committed_rect.has_value());
  CHECK(committed_rect->width() == filled_rect->width());
  CHECK(committed_rect->height() == filled_rect->height());
  CHECK(std::abs(QRectF(*committed_rect).center().x() - box_center_before_commit.x()) <= 1.0);
  CHECK(std::abs(QRectF(*committed_rect).center().y() - box_center_before_commit.y()) <= 1.0);

  save_widget_artifact("ui_free_transform_arrow_nudge", window);
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
  // The passive Move box no longer shows the numeric controls (Photoshop): they
  // appear the moment a session is actually active.
  CHECK(!x->isVisible());
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
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

// Photoshop accepts a unit token in every numeric field: the transform X/Y fields
// display pixels but take "50%" (of the document extent) or "2 in" (through the
// document PPI), the W/H percent fields take "200 px" relative to the original
// extent, and the angle field refuses lengths.
void ui_transform_fields_accept_unit_tokens() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  document.print_settings().horizontal_ppi = 300.0;
  document.print_settings().vertical_ppi = 300.0;
  const auto document_width = static_cast<double>(document.width());
  const auto document_height = static_cast<double>(document.height());

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
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  auto* x = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformXSpin"));
  auto* y = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformYSpin"));
  auto* scale_x = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleXSpin"));
  auto* rotation = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformRotationSpin"));
  CHECK(x != nullptr);
  CHECK(y != nullptr);
  CHECK(scale_x != nullptr);
  CHECK(rotation != nullptr);
  const auto commit_text = [](QDoubleSpinBox& spin, const QString& text) {
    auto* editor = spin.findChild<QLineEdit*>();
    CHECK(editor != nullptr);
    editor->setText(text);
    send_key(spin, Qt::Key_Return);
    QApplication::processEvents();
  };
  const auto state = [canvas] {
    const auto controls = canvas->transform_controls_state();
    CHECK(controls.has_value());
    return *controls;
  };

  // The box snaps its edges to the pixel grid (Photoshop rounds each edge, halves up), so a
  // Center reference typed for an odd extent re-reads .50: e.g. 192 on a 45 px box lands on
  // 192.5 (top 169.5 -> 170, bottom 214.5 -> 215).
  const auto snapped_reference = [&state](double target, bool horizontal) {
    const auto extent = horizontal ? state().original_size.width() : state().original_size.height();
    return std::floor(target - extent / 2.0 + 0.5) + extent / 2.0;
  };
  commit_text(*x, QStringLiteral("50%"));
  CHECK(std::abs(x->value() - snapped_reference(document_width / 2.0, true)) < 0.01);
  CHECK(std::abs(state().reference_position.x() - snapped_reference(document_width / 2.0, true)) < 0.01);
  commit_text(*y, QStringLiteral("25%"));
  CHECK(std::abs(y->value() - snapped_reference(document_height / 4.0, false)) < 0.01);
  CHECK(std::abs(state().reference_position.y() - snapped_reference(document_height / 4.0, false)) < 0.01);
  commit_text(*x, QStringLiteral("2 in"));
  CHECK(std::abs(x->value() - snapped_reference(600.0, true)) < 0.01);
  CHECK(std::abs(state().reference_position.x() - snapped_reference(600.0, true)) < 0.01);
  commit_text(*y, QStringLiteral("2.54 cm"));
  CHECK(std::abs(y->value() - snapped_reference(300.0, false)) < 0.01);

  const auto original_width = state().original_size.width();
  CHECK(original_width > 0.0);
  commit_text(*scale_x, QStringLiteral("200 px"));
  const auto expected_percent = 200.0 / original_width * 100.0;
  CHECK(std::abs(scale_x->value() - expected_percent) < 0.01);
  CHECK(std::abs(state().scale_x_percent - expected_percent) < 0.01);
  // The field now displays pixels (Photoshop), the linked H field follows, and a
  // plain number is read as pixels until a percent is typed again.
  auto* scale_y = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleYSpin"));
  CHECK(scale_y != nullptr);
  CHECK(scale_x->text() == QStringLiteral("200.00") + patchy::ui::pixel_suffix());
  CHECK(scale_y->suffix() == patchy::ui::pixel_suffix());
  commit_text(*scale_x, QStringLiteral("100"));
  CHECK(std::abs(state().scale_x_percent - 100.0 / original_width * 100.0) < 0.01);
  commit_text(*scale_x, QStringLiteral("100%"));
  CHECK(std::abs(state().scale_x_percent - 100.0) < 0.01);
  CHECK(scale_x->text() == QStringLiteral("100.00%"));
  CHECK(scale_y->suffix() == patchy::ui::percent_suffix());
  commit_text(*x, QStringLiteral("1 in"));
  CHECK(x->text() == QStringLiteral("1.00") + patchy::ui::inch_suffix());
  CHECK(std::abs(state().reference_position.x() - snapped_reference(300.0, true)) < 0.01);
  commit_text(*x, QStringLiteral("600 px"));
  CHECK(x->text() == QString::number(snapped_reference(600.0, true), 'f', 2) + patchy::ui::pixel_suffix());

  commit_text(*rotation, QStringLiteral("2 in"));
  CHECK(std::abs(rotation->value()) < 0.01);
  CHECK(std::abs(state().rotation_degrees) < 0.01);
  commit_text(*rotation, QStringLiteral("30 deg"));
  CHECK(std::abs(rotation->value() - 30.0) < 0.01);
  CHECK(std::abs(state().rotation_degrees - 30.0) < 0.01);
  CHECK(rotation->text() == QStringLiteral("30.00") + patchy::ui::degree_suffix());

  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

// Shared setup for the handle-drag tests: a filled 60 x 45 rect at (90, 80) and an
// active Free Transform session on it (Move tool selected).
std::optional<QRect> begin_probe_transform_session(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas) {
  canvas.set_tool(patchy::ui::CanvasTool::Marquee);
  drag(canvas, canvas.widget_position_for_document_point(QPoint(90, 80)),
       canvas.widget_position_for_document_point(QPoint(150, 125)));
  const auto filled_rect = canvas.selected_document_rect();
  CHECK(filled_rect.has_value());
  canvas.set_primary_color(QColor(40, 130, 230));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas.free_transform_active());
  return filled_rect;
}

patchy::ui::CanvasWidget::TransformControlsState require_transform_state(patchy::ui::CanvasWidget& canvas) {
  const auto state = canvas.transform_controls_state();
  CHECK(state.has_value());
  return state.value_or(patchy::ui::CanvasWidget::TransformControlsState{});
}

// The rotate handle turns the box about the reference point, not the center
// (Photoshop). With the reference at Top Left, a quarter turn keeps that corner
// pinned and the committed layer stays attached to it.
void ui_transform_rotate_drag_pivots_on_reference_point() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  const auto filled_rect = begin_probe_transform_session(window, *canvas);
  CHECK(filled_rect.has_value());

  // Changing the pivot repaints the box at once (the marker moves), and the
  // options bar labels the combo.
  PaintRegionRecorder recorder(canvas);
  canvas->installEventFilter(&recorder);
  recorder.reset();
  canvas->set_transform_reference_point(patchy::CanvasAnchor::TopLeft);
  QApplication::processEvents();
  canvas->removeEventFilter(&recorder);
  const QRect box_widget_rect(canvas->widget_position_for_document_point(filled_rect->topLeft()),
                              canvas->widget_position_for_document_point(filled_rect->bottomRight()));
  CHECK(QRegion(box_widget_rect).subtracted(recorder.region()).isEmpty());
  auto* pivot_label = window.findChild<QLabel*>(QStringLiteral("freeTransformPivotLabel"));
  CHECK(pivot_label != nullptr);
  CHECK(pivot_label->isVisible());
  CHECK(pivot_label->text() == QStringLiteral("Pivot:"));
  const auto before = require_transform_state(*canvas);
  const QPointF pivot_document(filled_rect->x(), filled_rect->y());
  CHECK(std::abs(before.reference_position.x() - pivot_document.x()) < 0.01);
  CHECK(std::abs(before.reference_position.y() - pivot_document.y()) < 0.01);

  // The rotate handle sits 32 widget px above the top-center handle. Drag it a
  // quarter turn around the pivot (rotation commutes with the view's uniform
  // scale, so the geometry can be built in widget space).
  const auto pivot_widget = canvas->widget_position_for_document_point(QPoint(filled_rect->x(), filled_rect->y()));
  const auto top_center = canvas->widget_position_for_document_point(
      QPoint(filled_rect->x() + filled_rect->width() / 2, filled_rect->y()));
  const QPoint handle(top_center.x(), top_center.y() - 32);
  const auto vector = handle - pivot_widget;
  const QPoint target(pivot_widget.x() - vector.y(), pivot_widget.y() + vector.x());
  drag(*canvas, handle, target);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  const auto after = require_transform_state(*canvas);
  CHECK(std::abs(std::abs(after.rotation_degrees) - 90.0) < 1.5);
  CHECK(std::abs(after.reference_position.x() - pivot_document.x()) < 0.5);
  CHECK(std::abs(after.reference_position.y() - pivot_document.y()) < 0.5);
  save_widget_artifact("ui_transform_rotate_about_top_left", window);

  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  const auto committed = canvas->active_layer_document_rect();
  CHECK(committed.has_value());
  // Width and height swap, and one corner of the result still touches the pivot.
  CHECK(std::abs(committed->width() - filled_rect->height()) <= 2);
  CHECK(std::abs(committed->height() - filled_rect->width()) <= 2);
  const bool clockwise = std::abs(committed->right() + 1 - filled_rect->x()) <= 2 &&
                         std::abs(committed->top() - filled_rect->y()) <= 2;
  const bool counter_clockwise = std::abs(committed->left() - filled_rect->x()) <= 2 &&
                                 std::abs(committed->bottom() + 1 - filled_rect->y()) <= 2;
  CHECK(clockwise || counter_clockwise);
}

// Alt on a scale handle scales about the reference point: with Center the box
// grows both ways and the center stays put; with the reference on the far edge
// the result equals the plain drag.
void ui_transform_alt_drag_scales_about_reference_point() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  const auto filled_rect = begin_probe_transform_session(window, *canvas);
  CHECK(filled_rect.has_value());
  const auto right_handle = canvas->widget_position_for_document_point(
      QPoint(filled_rect->x() + filled_rect->width(), filled_rect->y() + filled_rect->height() / 2));

  // Plain drag: the left edge anchors, the center shifts right.
  const auto start = require_transform_state(*canvas);
  drag(*canvas, right_handle, right_handle + QPoint(40, 0));
  QApplication::processEvents();
  const auto plain = require_transform_state(*canvas);
  CHECK(plain.scale_x_percent > 110.0);
  CHECK(std::abs(plain.scale_y_percent - 100.0) < 0.5);
  CHECK(plain.reference_position.x() > start.reference_position.x() + 1.0);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();

  // Alt drag with the Center reference: twice the growth, center unchanged.
  begin_probe_transform_session(window, *canvas);
  drag(*canvas, right_handle, right_handle + QPoint(40, 0), Qt::AltModifier);
  QApplication::processEvents();
  const auto symmetric = require_transform_state(*canvas);
  const auto plain_growth = plain.scale_x_percent - 100.0;
  CHECK(std::abs((symmetric.scale_x_percent - 100.0) - 2.0 * plain_growth) < 2.0);
  CHECK(std::abs(symmetric.scale_y_percent - 100.0) < 0.5);
  CHECK(std::abs(symmetric.reference_position.x() - start.reference_position.x()) < 0.5);
  CHECK(std::abs(symmetric.reference_position.y() - start.reference_position.y()) < 0.5);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();

  // Alt drag with the reference on the left edge: identical to the plain drag.
  begin_probe_transform_session(window, *canvas);
  canvas->set_transform_reference_point(patchy::CanvasAnchor::Left);
  drag(*canvas, right_handle, right_handle + QPoint(40, 0), Qt::AltModifier);
  QApplication::processEvents();
  const auto anchored = require_transform_state(*canvas);
  CHECK(std::abs(anchored.scale_x_percent - plain.scale_x_percent) < 1.0);
  CHECK(std::abs(anchored.scale_y_percent - 100.0) < 0.5);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

// Handle drags on an already-rotated box follow the box's own axes: after a
// quarter turn the local Right handle sits below the box, and pulling it down
// widens the box (local width) without touching its height.
void ui_transform_handle_drag_on_rotated_box_uses_local_axes() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  const auto filled_rect = begin_probe_transform_session(window, *canvas);
  CHECK(filled_rect.has_value());

  const auto start = require_transform_state(*canvas);
  CHECK(canvas->set_transform_controls_state(start.reference_position, 100.0, 100.0, 90.0));
  QApplication::processEvents();
  const auto rotated = require_transform_state(*canvas);
  CHECK(std::abs(rotated.rotation_degrees - 90.0) < 0.01);

  // Local +x rotated by 90 degrees points down the screen: the Right handle is
  // half the width below the center.
  const QPointF center(filled_rect->x() + filled_rect->width() / 2.0, filled_rect->y() + filled_rect->height() / 2.0);
  const QPointF right_handle_document(center.x(), center.y() + filled_rect->width() / 2.0);
  const auto right_handle = canvas->widget_position_for_document_point(right_handle_document.toPoint());
  drag(*canvas, right_handle, right_handle + QPoint(0, 30));
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  const auto after = require_transform_state(*canvas);
  CHECK(after.scale_x_percent > 115.0);
  CHECK(std::abs(after.scale_y_percent - 100.0) < 0.5);
  CHECK(std::abs(after.rotation_degrees - 90.0) < 0.01);
  save_widget_artifact("ui_transform_rotated_box_local_axes", window);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

// The drag readout (Photoshop's transformation values) appears beside the
// pointer while a handle is dragged, reports the geometry, mirrors to the status
// bar, and leaves no stale pixels behind after release.
void ui_transform_drag_readout_shows_values() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  const auto filled_rect = begin_probe_transform_session(window, *canvas);
  CHECK(filled_rect.has_value());
  CHECK(canvas->show_transform_drag_values());
  CHECK(!canvas->transform_drag_readout().has_value());

  const auto corner = canvas->widget_position_for_document_point(
      QPoint(filled_rect->x() + filled_rect->width(), filled_rect->y() + filled_rect->height()));
  send_mouse(*canvas, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, corner + QPoint(20, 10), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();

  const auto readout = canvas->transform_drag_readout();
  CHECK(readout.has_value());
  CHECK(readout->lines.size() == 2);
  const auto state = require_transform_state(*canvas);
  const auto width = state.scale_x_percent / 100.0 * state.original_size.width();
  const auto height = state.scale_y_percent / 100.0 * state.original_size.height();
  CHECK(readout->lines[0].contains(QStringLiteral("W:")));
  CHECK(readout->lines[0].contains(patchy::ui::format_pixels(width, 1)));
  CHECK(readout->lines[0].contains(patchy::ui::format_pixels(height, 1)));
  CHECK(readout->lines[1].contains(patchy::ui::format_percent(state.scale_x_percent)));
  // The canvas panel carries only the change line; the status bar gets both.
  CHECK(readout->canvas_lines.size() == 1);
  CHECK(readout->canvas_lines[0] == readout->lines[1]);
  const auto panel = canvas->drag_readout_widget_rect();
  CHECK(!panel.isEmpty());
  CHECK(canvas->rect().contains(panel));
  // Below-right of the pointer, like the marquee's W x H readout.
  CHECK(panel.left() > corner.x() + 20);
  CHECK(panel.top() > corner.y() + 10);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("W:")));
  save_widget_artifact("ui_transform_drag_readout", window);

  // The release repaint must cover the panel: it sits outside the transform's
  // bounded preview repaint, so record the real paint regions.
  PaintRegionRecorder recorder(canvas);
  canvas->installEventFilter(&recorder);
  recorder.reset();
  send_mouse(*canvas, QEvent::MouseButtonRelease, corner + QPoint(20, 10), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  canvas->removeEventFilter(&recorder);
  CHECK(QRegion(panel).subtracted(recorder.region()).isEmpty());
  CHECK(!canvas->transform_drag_readout().has_value());
  CHECK(canvas->drag_readout_widget_rect().isEmpty());

  // Rotating reports the angle and its delta (fresh session, so the handle
  // positions are the original rect's again).
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  begin_probe_transform_session(window, *canvas);
  const auto top_center = canvas->widget_position_for_document_point(
      QPoint(filled_rect->x() + filled_rect->width() / 2, filled_rect->y()));
  const QPoint rotate_handle(top_center.x(), top_center.y() - 32);
  send_mouse(*canvas, QEvent::MouseButtonPress, rotate_handle, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, rotate_handle + QPoint(40, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  const auto rotating = canvas->transform_drag_readout();
  CHECK(rotating.has_value());
  CHECK(rotating->lines.size() == 2);
  CHECK(rotating->lines[0].contains(QStringLiteral("Angle:")));
  CHECK(rotating->lines[0].contains(patchy::ui::degree_suffix()));
  CHECK(rotating->canvas_lines.size() == 1);
  CHECK(rotating->canvas_lines[0].endsWith(patchy::ui::degree_suffix()));
  send_mouse(*canvas, QEvent::MouseButtonRelease, rotate_handle + QPoint(40, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

// The Move tool's readout reports the reference point's position (Top Left
// here) after the drag plus the offset, and the bounded release repaint clears it.
void ui_move_drag_readout_reports_reference_and_delta() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  const auto filled_rect = begin_probe_transform_session(window, *canvas);
  CHECK(filled_rect.has_value());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  canvas->set_transform_reference_point(patchy::CanvasAnchor::TopLeft);
  const auto before = canvas->active_layer_document_rect();
  CHECK(before.has_value());

  const auto from = canvas->widget_position_for_document_point(QPoint(120, 100));
  const auto to = canvas->widget_position_for_document_point(QPoint(150, 88));
  send_mouse(*canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();

  const auto readout = canvas->transform_drag_readout();
  CHECK(readout.has_value());
  CHECK(readout->lines.size() == 2);
  CHECK(readout->lines[0].contains(patchy::ui::format_pixels(filled_rect->x() + 30.0)));
  CHECK(readout->lines[0].contains(patchy::ui::format_pixels(filled_rect->y() - 12.0)));
  CHECK(readout->lines[1].contains(patchy::ui::format_pixels(30.0, 0, true)));
  CHECK(readout->lines[1].contains(patchy::ui::format_pixels(-12.0, 0, true)));
  CHECK(readout->canvas_lines.size() == 1);
  CHECK(readout->canvas_lines[0].contains(patchy::ui::format_pixels(30.0, 0, true)));
  CHECK(!readout->canvas_lines[0].contains(readout->lines[0]));
  const auto panel = canvas->drag_readout_widget_rect();
  CHECK(!panel.isEmpty());
  save_widget_artifact("ui_move_drag_readout", window);

  PaintRegionRecorder recorder(canvas);
  canvas->installEventFilter(&recorder);
  recorder.reset();
  send_mouse(*canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  canvas->removeEventFilter(&recorder);
  CHECK(QRegion(panel).subtracted(recorder.region()).isEmpty());
  CHECK(!canvas->transform_drag_readout().has_value());
  const auto moved = canvas->active_layer_document_rect();
  CHECK(moved.has_value());
  CHECK(moved->x() - before->x() == 30);
  CHECK(moved->y() - before->y() == -12);
}

// Preferences > Application can hide the readout (view/showTransformValues).
void ui_transform_readout_preference_hides_hud() {
  SettingsValueRestorer restore_preference(QStringLiteral("view/showTransformValues"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("view/showTransformValues"), false);
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  CHECK(!canvas->show_transform_drag_values());
  const auto filled_rect = begin_probe_transform_session(window, *canvas);
  CHECK(filled_rect.has_value());
  const auto corner = canvas->widget_position_for_document_point(
      QPoint(filled_rect->x() + filled_rect->width(), filled_rect->y() + filled_rect->height()));
  send_mouse(*canvas, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, corner + QPoint(20, 10), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(!canvas->transform_drag_readout().has_value());
  CHECK(canvas->drag_readout_widget_rect().isEmpty());
  send_mouse(*canvas, QEvent::MouseButtonRelease, corner + QPoint(20, 10), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

void ui_free_transform_preview_follows_live_layer_style_changes() {
  patchy::Document document(220, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(220, 160, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Styled Transform",
                      solid_pixels(60, 40, patchy::PixelFormat::rgba8(), QColor(40, 130, 230, 255)));
  layer.set_bounds(patchy::Rect{60, 50, 60, 40});
  const auto styled_id = layer.id();
  document.add_layer(std::move(layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Transform Style Preview"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  layer_list->setCurrentItem(require_layer_item(*layer_list, QStringLiteral("Styled Transform")));
  QApplication::processEvents();

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  // Drag a handle so the transform preview snapshot gets baked.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(120, 90)),
       canvas->widget_position_for_document_point(QPoint(132, 98)));
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(90, 70)), QColor(40, 130, 230), 35));

  // Simulate the Layer Style dialog's live preview while the transform is
  // still active: mutate the style and announce it through the async path.
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  auto* styled = doc.find_layer(styled_id);
  CHECK(styled != nullptr);
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = patchy::BlendMode::Normal;
  overlay.color = patchy::RgbColor{210, 20, 20};
  overlay.opacity = 1.0F;
  styled->layer_style().color_overlays.push_back(overlay);
  canvas->document_changed_async_preview();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(90, 70)), QColor(210, 20, 20), 35));

  // A follow-up tweak to the same effect must also show through immediately.
  styled = doc.find_layer(styled_id);
  CHECK(styled != nullptr);
  CHECK(!styled->layer_style().color_overlays.empty());
  styled->layer_style().color_overlays.front().color = patchy::RgbColor{30, 170, 60};
  canvas->document_changed_async_preview();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(90, 70)), QColor(30, 170, 60), 35));
  save_widget_artifact("ui_transform_live_style_preview", window);

  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

void ui_options_bar_overflow_button_reveals_hidden_controls() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(90, 80)),
       canvas->widget_position_for_document_point(QPoint(150, 125)));
  canvas->set_primary_color(QColor(40, 130, 230));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();
  // The numeric controls only show during an active session now.
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  CHECK(options_bar != nullptr);
  auto* apply = window.findChild<QPushButton*>(QStringLiteral("freeTransformApplyButton"));
  CHECK(apply != nullptr);
  auto* xspin = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformXSpin"));
  CHECK(xspin != nullptr);

  // Wide enough to lay every transform control out on a single row.
  window.resize(1500, 800);
  QApplication::processEvents();
  process_events_for(60);
  CHECK(xspin->isVisible());
  CHECK(apply->isVisible());
  const int single_row_height = options_bar->height();

  // Too narrow for one row: the controls must fold onto additional rows and the
  // toolbar must grow taller, keeping every control (including Apply) visible.
  window.resize(720, 800);
  QApplication::processEvents();
  process_events_for(60);
  CHECK(apply->isVisible());
  CHECK(xspin->isVisible());
  const int wrapped_height = options_bar->height();
  CHECK(wrapped_height > single_row_height);

  save_widget_artifact("ui_options_bar_overflow", window);
}

// A probe layer for the pixel-grid tests: an opaque, non-uniform block whose bytes are
// compared before and after a numeric Free Transform.
patchy::LayerId add_pixel_grid_probe_layer(patchy::ui::MainWindow& window, patchy::Rect bounds) {
  patchy::Document document(400, 300, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(400, 300, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(bounds.width, bounds.height, patchy::PixelFormat::rgba8(), QColor(40, 130, 230, 255));
  fill_pixel_rect(pixels, QRect(0, 0, bounds.width / 2, bounds.height / 2), QColor(230, 60, 35, 255));
  fill_pixel_rect(pixels, QRect(bounds.width / 3, bounds.height / 3, 7, 11), QColor(20, 200, 90, 255));
  patchy::Layer layer(document.allocate_layer_id(), "Probe", std::move(pixels));
  const auto id = layer.id();
  layer.set_bounds(bounds);
  document.add_layer(std::move(layer));
  document.set_active_layer(id);
  window.add_document_session(std::move(document), QStringLiteral("Pixel Grid Probe"));
  QApplication::processEvents();
  return id;
}

std::vector<std::uint8_t> pixel_grid_probe_bytes(patchy::ui::MainWindow& window, patchy::LayerId id) {
  const auto* layer = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(id);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return {};
  }
  const auto data = layer->pixels().data();
  return std::vector<std::uint8_t>(data.begin(), data.end());
}

struct PixelGridSession {
  QDoubleSpinBox* x{nullptr};
  QDoubleSpinBox* y{nullptr};
  QDoubleSpinBox* rotation{nullptr};
  QPushButton* apply{nullptr};
};

PixelGridSession begin_pixel_grid_session(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas) {
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas.free_transform_active());
  PixelGridSession session;
  session.x = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformXSpin"));
  session.y = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformYSpin"));
  session.rotation = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformRotationSpin"));
  session.apply = window.findChild<QPushButton*>(QStringLiteral("freeTransformApplyButton"));
  CHECK(session.x != nullptr);
  CHECK(session.y != nullptr);
  CHECK(session.rotation != nullptr);
  CHECK(session.apply != nullptr);
  return session;
}

// Photoshop lands a typed X of 3.4 on 3 (halves round up: 0.5 -> 1) and the pixels stay
// crisp; the field re-displays the snapped value so what it shows is what was applied.
void ui_transform_numeric_fraction_snaps_to_pixel_grid() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto id = add_pixel_grid_probe_layer(window, patchy::Rect{100, 80, 60, 45});
  auto* canvas = require_canvas(window);
  const auto original = pixel_grid_probe_bytes(window, id);
  CHECK(canvas->snap_transforms_to_pixel_grid());

  auto session = begin_pixel_grid_session(window, *canvas);
  const auto start_x = session.x->value();
  const auto start_y = session.y->value();
  CHECK(std::abs(start_x - 130.0) < 1e-6);
  CHECK(std::abs(start_y - 102.5) < 1e-6);
  session.x->setValue(start_x + 0.4);
  QApplication::processEvents();
  CHECK(std::abs(session.x->value() - start_x) < 1e-6);
  session.x->setValue(start_x + 3.4);
  session.y->setValue(start_y + 0.5);
  QApplication::processEvents();
  CHECK(std::abs(session.x->value() - (start_x + 3.0)) < 1e-6);
  CHECK(std::abs(session.y->value() - (start_y + 1.0)) < 1e-6);
  const auto state = canvas->transform_controls_state();
  CHECK(state.has_value());
  if (state.has_value()) {
    CHECK(std::abs(state->reference_position.x() - (start_x + 3.0)) < 1e-6);
  }
  session.apply->click();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  const auto* layer = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(id);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  CHECK(layer->bounds().x == 103);
  CHECK(layer->bounds().y == 81);
  CHECK(layer->bounds().width == 60);
  CHECK(layer->bounds().height == 45);
  CHECK(pixel_grid_probe_bytes(window, id) == original);
}

// Photoshop rounds each destination EDGE, so an odd-sized layer under the Center pivot keeps
// a half-pixel reference: X 140 on a 61-wide layer puts the left edge on 110 (109.5 rounds
// up) and the field re-reads 140.50, exactly what Photoshop's options bar shows.
void ui_transform_numeric_odd_layer_keeps_half_pixel_reference() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto id = add_pixel_grid_probe_layer(window, patchy::Rect{100, 80, 61, 45});
  auto* canvas = require_canvas(window);

  auto session = begin_pixel_grid_session(window, *canvas);
  CHECK(std::abs(session.x->value() - 130.5) < 1e-6);
  session.x->setValue(140.0);
  QApplication::processEvents();
  CHECK(std::abs(session.x->value() - 140.5) < 1e-6);
  session.apply->click();
  QApplication::processEvents();

  const auto* layer = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(id);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  CHECK(layer->bounds().x == 110);
  CHECK(layer->bounds().width == 61);
  CHECK(layer->bounds().y == 80);
}

// With the preference off the typed fraction is honored: the rect keeps 3.4, the commit
// floors the origin and resamples the sub-pixel phase into one extra column.
void ui_transform_snap_preference_off_keeps_fraction() {
  SettingsValueRestorer restore_snap(QStringLiteral("input/snapTransformsToPixelGrid"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("input/snapTransformsToPixelGrid"), false);
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  const auto id = add_pixel_grid_probe_layer(window, patchy::Rect{100, 80, 60, 45});
  auto* canvas = require_canvas(window);
  CHECK(!canvas->snap_transforms_to_pixel_grid());
  const auto original = pixel_grid_probe_bytes(window, id);

  auto session = begin_pixel_grid_session(window, *canvas);
  const auto start_x = session.x->value();
  session.x->setValue(start_x + 3.4);
  QApplication::processEvents();
  CHECK(std::abs(session.x->value() - (start_x + 3.4)) < 1e-6);
  session.apply->click();
  QApplication::processEvents();

  const auto* layer = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(id);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  CHECK(layer->bounds().x == 103);
  CHECK(layer->bounds().width == 61);
  CHECK(pixel_grid_probe_bytes(window, id) != original);
}

// A rotated box cannot sit on the pixel grid, so numeric entry keeps the fraction there.
void ui_transform_snap_skips_rotated_sessions() {
  patchy::ui::MainWindow window;
  show_window(window);
  add_pixel_grid_probe_layer(window, patchy::Rect{100, 80, 60, 45});
  auto* canvas = require_canvas(window);

  auto session = begin_pixel_grid_session(window, *canvas);
  const auto start_x = session.x->value();
  session.rotation->setValue(15.0);
  session.x->setValue(start_x + 3.4);
  QApplication::processEvents();
  CHECK(std::abs(session.x->value() - (start_x + 3.4)) < 1e-6);
  const auto state = canvas->transform_controls_state();
  CHECK(state.has_value());
  if (state.has_value()) {
    CHECK(std::abs(state->reference_position.x() - (start_x + 3.4)) < 1e-6);
  }
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

// The default bicubic kernel is Catmull-Rom (interpolating), so a whole-pixel move through
// the resampler is byte-identical: no fast path is needed, and this pins that it stays so.
void ui_transform_integer_move_is_lossless_through_bicubic() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto id = add_pixel_grid_probe_layer(window, patchy::Rect{100, 80, 60, 45});
  auto* canvas = require_canvas(window);
  const auto original = pixel_grid_probe_bytes(window, id);

  auto session = begin_pixel_grid_session(window, *canvas);
  session.x->setValue(session.x->value() + 7.0);
  session.y->setValue(session.y->value() - 3.0);
  QApplication::processEvents();
  session.apply->click();
  QApplication::processEvents();

  const auto* layer = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(id);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  CHECK(layer->bounds().x == 107);
  CHECK(layer->bounds().y == 77);
  CHECK(layer->bounds().width == 60);
  CHECK(layer->bounds().height == 45);
  CHECK(pixel_grid_probe_bytes(window, id) == original);
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
  // An empty click with Auto-Select on deselects every layer (which also drops
  // the passive box); the next click on the artwork re-selects it.
  CHECK(!canvas->active_layer_document_rect().has_value());

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

patchy::LayerId prepare_off_canvas_move_scene(patchy::ui::MainWindow& window, QRect bounds,
                                             bool auto_select, bool show_controls) {
  patchy::Document document(200, 160, patchy::PixelFormat::rgba8());
  auto& background = document.add_pixel_layer(
      "Background", solid_pixels(200, 160, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::set_layer_locks_position(background, true);
  patchy::Layer layer(document.allocate_layer_id(), "Move target",
                      solid_pixels(bounds.width(), bounds.height(), patchy::PixelFormat::rgba8(), QColor(220, 50, 30)));
  const auto id = layer.id();
  layer.set_bounds(patchy::Rect{bounds.x(), bounds.y(), bounds.width(), bounds.height()});
  document.add_layer(std::move(layer));
  window.add_document_session(std::move(document), QStringLiteral("Off-canvas Move"));
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_zoom(1.0);
  canvas->center_document_in_view();
  canvas->set_snap_enabled(false);
  canvas->set_auto_select_layer(auto_select);
  canvas->set_show_transform_controls(show_controls);
  QApplication::processEvents();
  return id;
}

void ui_move_auto_select_off_recovers_offscreen_layer_from_anywhere() {
  for (const bool show_controls : {false, true}) {
    patchy::ui::MainWindow window;
    const auto id = prepare_off_canvas_move_scene(window, QRect(-900, 50, 80, 60), false, show_controls);
    auto* canvas = require_canvas(window);
    const auto origin = canvas->widget_position_for_document_point(QPoint(0, 0));
    CHECK(!canvas->rect().intersects(QRect(origin + QPoint(-900, 50), QSize(80, 60))));

    // A first drag over the locked Background moves the selected offscreen layer.
    drag(*canvas, origin + QPoint(20, 20), origin + QPoint(120, 30));
    CHECK(canvas->active_layer_document_rect() == QRect(-800, 60, 80, 60));
    CHECK(!canvas->free_transform_active());
    require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
    CHECK(canvas->active_layer_document_rect() == QRect(-900, 50, 80, 60));

    // Repeated drags in empty gray space bring it back without hitting its box.
    const auto start = origin + QPoint(-60, 20);
    const auto end = start + QPoint(300, 0);
    CHECK(canvas->rect().contains(start) && canvas->rect().contains(end));
    for (int step = 1; step <= 3; ++step) {
      drag(*canvas, start, end);
      CHECK(canvas->active_layer_document_rect() == QRect(-900 + step * 300, 50, 80, 60));
      CHECK(!canvas->free_transform_active());
    }
    CHECK(canvas->widget_position_for_document_point(QPoint(0, 0)) == origin);
    const auto& doc = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(doc.active_layer_id() == id);
    const auto expected = solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(220, 50, 30));
    CHECK(doc.find_layer(id)->pixels().byte_size() == expected.byte_size());
    CHECK(std::memcmp(doc.find_layer(id)->pixels().data().data(), expected.data().data(), expected.byte_size()) == 0);
    require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
    CHECK(canvas->active_layer_document_rect() == QRect(-300, 50, 80, 60));
    require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
    CHECK(canvas->active_layer_document_rect() == QRect(0, 50, 80, 60));
  }
}

void ui_move_auto_select_on_drags_off_canvas_box_interior() {
  patchy::ui::MainWindow window;
  const auto id = prepare_off_canvas_move_scene(window, QRect(60, 50, 80, 60), true, true);
  auto* canvas = require_canvas(window);
  const auto inside = canvas->widget_position_for_document_point(QPoint(100, 80));
  const auto outside = inside - QPoint(300, 0);
  CHECK(canvas->rect().contains(outside));
  drag(*canvas, inside, outside);
  CHECK(canvas->active_layer_document_rect() == QRect(-240, 50, 80, 60));
  send_mouse(*canvas, QEvent::MouseMove, outside, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
  send_mouse(*canvas, QEvent::MouseButtonPress, outside, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, inside, Qt::NoButton, Qt::LeftButton);
  CHECK(!canvas->free_transform_active());
  send_mouse(*canvas, QEvent::MouseButtonRelease, inside, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->active_layer_document_rect() == QRect(60, 50, 80, 60));
  CHECK(patchy::ui::MainWindowTestAccess::document(window).active_layer_id() == id);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(100, 80)), QColor(220, 50, 30), 5));
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  CHECK(canvas->active_layer_document_rect() == QRect(-240, 50, 80, 60));
  require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
  CHECK(canvas->active_layer_document_rect() == QRect(60, 50, 80, 60));
  save_widget_artifact("ui_move_off_canvas_recovered", window);
}

void ui_move_off_canvas_box_moves_folders_and_multiple_layers() {
  for (const bool grouped : {false, true}) {
    patchy::Document document(200, 160, patchy::PixelFormat::rgba8());
    patchy::Layer first(document.allocate_layer_id(), "First",
                        solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(220, 50, 30)));
    patchy::Layer second(document.allocate_layer_id(), "Second",
                         solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(30, 80, 220)));
    const auto first_id = first.id();
    const auto second_id = second.id();
    first.set_bounds(patchy::Rect{-240, 30, 80, 60});
    second.set_bounds(patchy::Rect{-180, 100, 80, 60});
    std::vector<patchy::LayerId> selection{first_id, second_id};
    if (grouped) {
      patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
      selection = {folder.id()};
      folder.add_child(std::move(first));
      folder.add_child(std::move(second));
      document.add_layer(std::move(folder));
    } else {
      document.add_layer(std::move(first));
      document.add_layer(std::move(second));
    }
    patchy::ui::CanvasWidget canvas;
    canvas.resize(800, 600);
    canvas.set_document(&document);
    canvas.set_zoom(1.0);
    canvas.center_document_in_view();
    canvas.set_tool(patchy::ui::CanvasTool::Move);
    canvas.set_auto_select_layer(true);
    canvas.set_show_transform_controls(true);
    canvas.set_snap_enabled(false);
    canvas.set_selected_layer_ids(selection);
    canvas.show();
    QApplication::processEvents();
    // This point lies in empty space inside the selected set's union box.
    const auto start = canvas.widget_position_for_document_point(QPoint(-120, 60));
    drag(canvas, start, start + QPoint(300, 0));
    const auto& doc = document;
    const auto first_bounds = doc.find_layer(first_id)->bounds();
    const auto second_bounds = doc.find_layer(second_id)->bounds();
    CHECK(first_bounds.x == 60 && first_bounds.y == 30);
    CHECK(second_bounds.x == 120 && second_bounds.y == 100);
    CHECK(first_bounds.width == 80 && first_bounds.height == 60);
    CHECK(second_bounds.width == 80 && second_bounds.height == 60);
    CHECK(!canvas.free_transform_active());
  }
}

void ui_move_off_canvas_keeps_rectangle_handles_pan_and_locks() {
  patchy::ui::MainWindow window;
  const auto id = prepare_off_canvas_move_scene(window, QRect(-240, 50, 80, 60), true, true);
  auto* canvas = require_canvas(window);
  const QRect original(-240, 50, 80, 60);
  const auto center = canvas->widget_position_for_document_point(QPoint(-200, 80));
  const auto empty = canvas->widget_position_for_document_point(QPoint(-100, 20));
  drag(*canvas, empty, empty + QPoint(30, 30));
  CHECK(canvas->active_layer_document_rect() == original);
  for (const bool auto_select : {false, true}) {
    canvas->set_auto_select_layer(auto_select);
    drag(*canvas, center, center + QPoint(30, 20), Qt::ControlModifier);
    CHECK(canvas->active_layer_document_rect() == original);
    CHECK(!canvas->free_transform_active());
    const auto corner = canvas->widget_position_for_document_point(QPoint(-160, 110));
    send_mouse(*canvas, QEvent::MouseMove, corner, Qt::NoButton, Qt::NoButton);
    CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);
    send_mouse(*canvas, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, corner, Qt::LeftButton, Qt::NoButton);
    CHECK(canvas->free_transform_active());
    send_key(*canvas, Qt::Key_Escape);
    CHECK(!canvas->free_transform_active());
    CHECK(canvas->active_layer_document_rect() == original);
  }
  canvas->set_show_transform_controls(false);
  drag(*canvas, center, center + QPoint(30, 20));
  CHECK(canvas->active_layer_document_rect() == original);
  canvas->set_show_transform_controls(true);
  canvas->set_spacebar_panning(true);
  const auto origin = canvas->widget_position_for_document_point(QPoint(0, 0));
  drag(*canvas, center, center + QPoint(30, 20));
  canvas->set_spacebar_panning(false);
  CHECK(canvas->widget_position_for_document_point(QPoint(0, 0)) == origin + QPoint(30, 20));
  CHECK(canvas->active_layer_document_rect() == original);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  patchy::set_layer_locks_position(*doc.find_layer(id), true);
  canvas->set_auto_select_layer(false);
  drag(*canvas, empty, empty + QPoint(30, 20));
  CHECK(canvas->active_layer_document_rect() == original);
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
  CHECK(process_events_until([canvas] { return canvas->render_settled(); }, 10000));
  const auto revealed = canvas_pixel(*canvas, QPoint(110, 80));
  CHECK(!color_close(revealed, QColor(245, 30, 30), 45));
  CHECK(revealed.red() > 210 && revealed.green() > 210 && revealed.blue() > 210);
  save_widget_artifact("ui_layer_via_copy_cut", window);
}

void ui_free_transform_drag_proxy_engages_above_threshold() {
  // A half-opacity layer whose transformed area crosses the unstyled
  // kTransformProxyAreaThreshold (4 Mpx): the handle drag must latch onto the
  // proxy preview exactly once, keep it for the rest of the drag, and restore
  // the accurate composited patches on release.
  patchy::Document document(2600, 1900, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(2600, 1900, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(2300, 1780, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255));
  patchy::Layer layer(document.allocate_layer_id(), "Half Opacity", std::move(pixels));
  layer.set_bounds(patchy::Rect{100, 50, 2300, 1780});
  layer.set_opacity(0.5F);
  document.add_layer(std::move(layer));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Proxy Latch"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(0.25);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();

  const auto counter_before = canvas->render_cache_diagnostics().transform_proxy_previews;
  const auto corner = canvas->widget_position_for_document_point(QPoint(2400, 1830));
  send_mouse(*canvas, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  send_mouse(*canvas, QEvent::MouseMove, corner + QPoint(14, 10), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas->render_cache_diagnostics().transform_proxy_previews == counter_before + 1);
  send_mouse(*canvas, QEvent::MouseMove, corner + QPoint(30, 22), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  // Sticky: one latch per drag, not one per mouse-move.
  CHECK(canvas->render_cache_diagnostics().transform_proxy_previews == counter_before + 1);

  // Mid-drag the proxy must draw at the ENLARGED geometry: a document point
  // outside the original layer but inside the scaled rect shows the layer's
  // half-opacity red blended over the white background (pink), not raw white.
  const QColor expected_blend(238, 148, 148);
  const auto mid_drag = canvas->grab().toImage();
  const auto probe = canvas->widget_position_for_document_point(QPoint(2450, 900));
  CHECK(mid_drag.rect().contains(probe));
  CHECK(color_close(mid_drag.pixelColor(probe), expected_blend, 45));

  send_mouse(*canvas, QEvent::MouseButtonRelease, corner + QPoint(30, 22), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(canvas->render_cache_diagnostics().transform_proxy_previews == counter_before + 1);
  // Release rendered the accurate composited patches at the final geometry.
  const auto post_release = canvas->grab().toImage();
  CHECK(color_close(post_release.pixelColor(probe), expected_blend, 45));

  auto* apply = window.findChild<QPushButton*>(QStringLiteral("freeTransformApplyButton"));
  CHECK(apply != nullptr);
  apply->click();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  const auto bounds_after = canvas->active_layer_document_rect();
  CHECK(bounds_after.has_value());
  CHECK(bounds_after->right() > 2400);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(2450, 900)), expected_blend, 45));
  save_widget_artifact("ui_free_transform_proxy_latch", window);
}

void ui_free_transform_drag_small_doc_stays_live() {
  // Styled layer far below kStyledTransformProxyAreaThreshold: handle drags
  // must keep the live composited preview (stroke visible mid-drag) and the
  // proxy counter must not move.
  patchy::Document document(360, 260, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(360, 260, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(70, 46, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(54, 8, 14, 30), QColor(35, 85, 210, 255));
  patchy::Layer styled_layer(document.allocate_layer_id(), "Styled Small", std::move(pixels));
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
  window.add_document_session(std::move(document), QStringLiteral("Small Styled Live"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();

  const auto counter_before = canvas->render_cache_diagnostics().transform_proxy_previews;
  // The passive box tracks the opaque pixels (the 14x30 bar at document
  // 174,98): press its bottom-right handle and scale outward.
  const auto corner = canvas->widget_position_for_document_point(QPoint(188, 128));
  send_mouse(*canvas, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_mouse(*canvas, QEvent::MouseMove, corner + QPoint(24, 18), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(*canvas, QEvent::MouseMove, corner + QPoint(26, 20), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas->render_cache_diagnostics().transform_proxy_previews == counter_before);

  // Live composited preview mid-drag: the red stroke renders around the
  // transformed bar, past its original right edge.
  const auto mid_drag = canvas->grab().toImage();
  int red_pixels = 0;
  for (int document_y = 96; document_y <= 150; ++document_y) {
    for (int document_x = 190; document_x <= 218; ++document_x) {
      const auto widget_point = canvas->widget_position_for_document_point(QPoint(document_x, document_y));
      if (!mid_drag.rect().contains(widget_point)) {
        continue;
      }
      const auto color = mid_drag.pixelColor(widget_point);
      if (color.red() > 190 && color.green() < 90 && color.blue() < 100) {
        ++red_pixels;
      }
    }
  }
  CHECK(red_pixels > 10);

  send_mouse(*canvas, QEvent::MouseButtonRelease, corner + QPoint(26, 20), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->render_cache_diagnostics().transform_proxy_previews == counter_before);
  auto* cancel = window.findChild<QPushButton*>(QStringLiteral("freeTransformCancelButton"));
  CHECK(cancel != nullptr);
  cancel->click();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

// The transform area gates cannot price the stack the drag crosses; a slow
// live composited-preview frame must latch the proxy on the next move. The
// zero env threshold makes any live frame count as slow, so the test is
// deterministic on every machine.
void ui_free_transform_slow_frame_latches_proxy() {
  EnvironmentVariableRestorer restore_latch("PATCHY_MOVE_LIVE_LATCH_MS");
  qputenv("PATCHY_MOVE_LIVE_LATCH_MS", QByteArray("0"));

  patchy::Document document(360, 260, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(360, 260, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(60, 40, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255));
  patchy::Layer layer(document.allocate_layer_id(), "Half Opacity Small", std::move(pixels));
  layer.set_bounds(patchy::Rect{100, 80, 60, 40});
  layer.set_opacity(0.5F);
  document.add_layer(std::move(layer));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Slow Frame Latch"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();

  const auto counter_before = canvas->render_cache_diagnostics().transform_proxy_previews;
  const auto corner = canvas->widget_position_for_document_point(QPoint(160, 120));
  send_mouse(*canvas, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  // First move renders a live composited frame (marked slow by the zero
  // threshold); the second move must latch the proxy despite the tiny area.
  send_mouse(*canvas, QEvent::MouseMove, corner + QPoint(14, 10), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(*canvas, QEvent::MouseMove, corner + QPoint(24, 18), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas->render_cache_diagnostics().transform_proxy_previews == counter_before + 1);
  send_mouse(*canvas, QEvent::MouseMove, corner + QPoint(26, 20), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas->render_cache_diagnostics().transform_proxy_previews == counter_before + 1);

  send_mouse(*canvas, QEvent::MouseButtonRelease, corner + QPoint(26, 20), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* cancel = window.findChild<QPushButton*>(QStringLiteral("freeTransformCancelButton"));
  CHECK(cancel != nullptr);
  cancel->click();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

// At zoom <= 50% the transform session builds its base cache from the
// preview-scaled document; the drag and commit stay accurate.
void ui_free_transform_scaled_base_zoomed_out() {
  patchy::Document document(800, 600, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(800, 600, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(200, 160, patchy::PixelFormat::rgba8(), QColor(210, 40, 40, 255));
  patchy::Layer layer(document.allocate_layer_id(), "Scaled Base Layer", std::move(pixels));
  layer.set_bounds(patchy::Rect{150, 120, 200, 160});
  layer.set_opacity(0.5F);
  document.add_layer(std::move(layer));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Scaled Base"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(0.5);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();

  const auto scaled_before = canvas->render_cache_diagnostics().transform_scaled_bases;
  const auto corner = canvas->widget_position_for_document_point(QPoint(350, 280));
  send_mouse(*canvas, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(canvas->render_cache_diagnostics().transform_scaled_bases == scaled_before + 1);

  send_mouse(*canvas, QEvent::MouseMove, corner + QPoint(30, 20), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(*canvas, QEvent::MouseButtonRelease, corner + QPoint(30, 20), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* apply = window.findChild<QPushButton*>(QStringLiteral("freeTransformApplyButton"));
  CHECK(apply != nullptr);
  apply->click();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  // The enlarged layer covers document points past the original right edge;
  // half-opacity red over white reads as pink.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(380, 200)), QColor(232, 147, 147), 45));
  save_widget_artifact("ui_free_transform_scaled_base", window);
}

void ui_edit_conversion_scanline_rewrites_are_byte_identical() {
  // Odd sizes plus alphas {0, 1, 127, 255} pin the scanline conversions to the
  // per-pixel QColor semantics they replaced, for both the 4-channel memcpy
  // path and the 3-channel alpha-expansion path.
  constexpr std::uint8_t kAlphas[] = {0U, 1U, 127U, 255U};
  patchy::PixelBuffer rgba(5, 3, patchy::PixelFormat::rgba8());
  for (int y = 0; y < rgba.height(); ++y) {
    for (int x = 0; x < rgba.width(); ++x) {
      auto* px = rgba.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(10 + x * 40 + y);
      px[1] = static_cast<std::uint8_t>(200 - x * 30 + y * 2);
      px[2] = static_cast<std::uint8_t>(x * 17 + y * 50);
      px[3] = kAlphas[static_cast<std::size_t>(x + y) % 4U];
    }
  }
  const auto& const_rgba = rgba;
  const auto rgba_image = patchy::ui::qimage_from_pixel_buffer(rgba);
  CHECK(rgba_image.format() == QImage::Format_RGBA8888);
  bool rgba_matches = true;
  for (int y = 0; y < rgba.height(); ++y) {
    for (int x = 0; x < rgba.width(); ++x) {
      const auto* px = const_rgba.pixel(x, y);
      rgba_matches = rgba_matches && rgba_image.pixelColor(x, y) == QColor(px[0], px[1], px[2], px[3]);
    }
  }
  CHECK(rgba_matches);

  const auto round_trip = patchy::ui::pixels_from_image_rgba(rgba_image);
  CHECK(round_trip.width() == rgba.width());
  CHECK(round_trip.height() == rgba.height());
  CHECK(round_trip.byte_size() == rgba.byte_size());
  CHECK(std::memcmp(round_trip.data().data(), const_rgba.data().data(), rgba.byte_size()) == 0);

  patchy::PixelBuffer rgb(3, 5, patchy::PixelFormat::rgb8());
  for (int y = 0; y < rgb.height(); ++y) {
    for (int x = 0; x < rgb.width(); ++x) {
      auto* px = rgb.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(5 + x * 60 + y * 3);
      px[1] = static_cast<std::uint8_t>(120 + x * 11 + y * 7);
      px[2] = static_cast<std::uint8_t>(250 - x * 45 - y * 9);
    }
  }
  const auto& const_rgb = rgb;
  const auto rgb_image = patchy::ui::qimage_from_pixel_buffer(rgb);
  bool rgb_matches = true;
  for (int y = 0; y < rgb.height(); ++y) {
    for (int x = 0; x < rgb.width(); ++x) {
      const auto* px = const_rgb.pixel(x, y);
      rgb_matches = rgb_matches && rgb_image.pixelColor(x, y) == QColor(px[0], px[1], px[2], 255);
    }
  }
  CHECK(rgb_matches);
}

}  // namespace

std::vector<patchy::test::TestCase> clipboard_free_transform_tests() {
  return {
      {"ui_copy_paste_and_transform_pasted_layer_work", ui_copy_paste_and_transform_pasted_layer_work},
      {"ui_paste_clears_selection_and_undo_restores_it", ui_paste_clears_selection_and_undo_restores_it},
      {"ui_paste_file_urls_adds_layers", ui_paste_file_urls_adds_layers},
      {"ui_external_clipboard_image_paste_creates_centered_layer",
       ui_external_clipboard_image_paste_creates_centered_layer},
      {"ui_external_clipboard_image_paste_overrides_internal_payload",
       ui_external_clipboard_image_paste_overrides_internal_payload},
      {"ui_paste_inserts_above_topmost_selected_layer", ui_paste_inserts_above_topmost_selected_layer},
      {"ui_paste_above_selected_folder_or_at_top_without_selection",
       ui_paste_above_selected_folder_or_at_top_without_selection},
      {"ui_paste_selection_centers_in_panned_zoomed_view", ui_paste_selection_centers_in_panned_zoomed_view},
      {"ui_paste_selection_clamps_to_each_canvas_edge", ui_paste_selection_clamps_to_each_canvas_edge},
      {"ui_paste_in_place_shortcut_restores_cut_coordinates", ui_paste_in_place_shortcut_restores_cut_coordinates},
      {"ui_paste_selection_between_documents_clamps_and_preserves_size",
       ui_paste_selection_between_documents_clamps_and_preserves_size},
      {"ui_external_clipboard_paste_in_place_falls_back_to_view_center",
       ui_external_clipboard_paste_in_place_falls_back_to_view_center},
      {"ui_paste_in_place_obeys_document_and_channel_guards", ui_paste_in_place_obeys_document_and_channel_guards},
      {"ui_free_transform_uses_opaque_pixel_bounds", ui_free_transform_uses_opaque_pixel_bounds},
      {"ui_transform_shift_frees_aspect_ratio_by_default",
       ui_transform_shift_frees_aspect_ratio_by_default},
      {"ui_transform_proportional_corner_follows_diagonal_projection",
       ui_transform_proportional_corner_follows_diagonal_projection},
      {"ui_transform_shift_aspect_preference_restores_legacy",
       ui_transform_shift_aspect_preference_restores_legacy},
      {"ui_free_transform_arrow_keys_nudge_bounding_box", ui_free_transform_arrow_keys_nudge_bounding_box},
      {"ui_transform_numeric_controls_apply_values", ui_transform_numeric_controls_apply_values},
      {"ui_transform_fields_accept_unit_tokens", ui_transform_fields_accept_unit_tokens},
      {"ui_transform_numeric_fraction_snaps_to_pixel_grid", ui_transform_numeric_fraction_snaps_to_pixel_grid},
      {"ui_transform_numeric_odd_layer_keeps_half_pixel_reference",
       ui_transform_numeric_odd_layer_keeps_half_pixel_reference},
      {"ui_transform_snap_preference_off_keeps_fraction", ui_transform_snap_preference_off_keeps_fraction},
      {"ui_transform_snap_skips_rotated_sessions", ui_transform_snap_skips_rotated_sessions},
      {"ui_transform_integer_move_is_lossless_through_bicubic",
       ui_transform_integer_move_is_lossless_through_bicubic},
      {"ui_transform_rotate_drag_pivots_on_reference_point", ui_transform_rotate_drag_pivots_on_reference_point},
      {"ui_transform_alt_drag_scales_about_reference_point", ui_transform_alt_drag_scales_about_reference_point},
      {"ui_transform_handle_drag_on_rotated_box_uses_local_axes",
       ui_transform_handle_drag_on_rotated_box_uses_local_axes},
      {"ui_transform_drag_readout_shows_values", ui_transform_drag_readout_shows_values},
      {"ui_move_drag_readout_reports_reference_and_delta", ui_move_drag_readout_reports_reference_and_delta},
      {"ui_transform_readout_preference_hides_hud", ui_transform_readout_preference_hides_hud},
      {"ui_free_transform_preview_follows_live_layer_style_changes",
       ui_free_transform_preview_follows_live_layer_style_changes},
      {"ui_options_bar_overflow_button_reveals_hidden_controls",
       ui_options_bar_overflow_button_reveals_hidden_controls},
      {"ui_transform_numeric_controls_accept_negative_scale",
       ui_transform_numeric_controls_accept_negative_scale},
      {"ui_transform_numeric_preview_renders_layer_styles",
       ui_transform_numeric_preview_renders_layer_styles},
      {"ui_move_show_transform_controls_click_shows_passive_transform",
       ui_move_show_transform_controls_click_shows_passive_transform},
      {"ui_move_auto_select_off_recovers_offscreen_layer_from_anywhere",
       ui_move_auto_select_off_recovers_offscreen_layer_from_anywhere},
      {"ui_move_auto_select_on_drags_off_canvas_box_interior",
       ui_move_auto_select_on_drags_off_canvas_box_interior},
      {"ui_move_off_canvas_box_moves_folders_and_multiple_layers",
       ui_move_off_canvas_box_moves_folders_and_multiple_layers},
      {"ui_move_off_canvas_keeps_rectangle_handles_pan_and_locks",
       ui_move_off_canvas_keeps_rectangle_handles_pan_and_locks},
      {"ui_transform_controls_finish_on_tool_layer_and_duplicate_changes",
       ui_transform_controls_finish_on_tool_layer_and_duplicate_changes},
      {"ui_layer_via_copy_and_cut_match_photoshop_shortcuts",
       ui_layer_via_copy_and_cut_match_photoshop_shortcuts},
      {"ui_free_transform_drag_proxy_engages_above_threshold",
       ui_free_transform_drag_proxy_engages_above_threshold},
      {"ui_free_transform_drag_small_doc_stays_live", ui_free_transform_drag_small_doc_stays_live},
      {"ui_free_transform_slow_frame_latches_proxy", ui_free_transform_slow_frame_latches_proxy},
      {"ui_free_transform_scaled_base_zoomed_out", ui_free_transform_scaled_base_zoomed_out},
      {"ui_edit_conversion_scanline_rewrites_are_byte_identical",
       ui_edit_conversion_scanline_rewrites_are_byte_identical},
  };
}
