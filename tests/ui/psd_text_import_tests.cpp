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

void ui_imported_psd_text_uses_photoshop_frame_after_commit() {
  ensure_artifact_dir();
  const auto saved_path =
      QFileInfo(QStringLiteral("test-artifacts/imported-psd-text-frame-roundtrip.psd")).absoluteFilePath();
  QFile::remove(saved_path);

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
  text_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "320";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "80";
  text_layer.metadata()[patchy::kLayerMetadataTextSourceBlock] = "TySh";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextTransform] = "1 0 0 1 0 0";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoxBounds] = "40 70 360 150";
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
  const QPoint expected_editor_origin(40, 70);
  CHECK(editor->property("patchy.documentTextX").toInt() == expected_editor_origin.x());
  CHECK(editor->property("patchy.documentTextY").toInt() == expected_editor_origin.y());
  CHECK(editor->property("patchy.documentTextWidth").toInt() == 320);
  CHECK(editor->property("patchy.documentTextHeight").toInt() == 80);
  CHECK(editor->property("patchy.documentTextAntiAlias").toInt() == 0);
  CHECK((editor->alignment() & Qt::AlignHCenter) != 0);

  auto* bottom_right = canvas->findChild<QWidget*>(QStringLiteral("textBoxResizeHandleBottomRight"));
  CHECK(bottom_right != nullptr);
  const auto width_before_resize = editor->property("patchy.documentTextWidth").toInt();
  const auto height_before_resize = editor->property("patchy.documentTextHeight").toInt();
  const auto handle_center = bottom_right->geometry().center();
  drag(*canvas, handle_center, handle_center + QPoint(44, 24));
  QApplication::processEvents();
  const QPoint committed_editor_origin(editor->property("patchy.documentTextX").toInt(),
                                       editor->property("patchy.documentTextY").toInt());
  const auto committed_editor_width = editor->property("patchy.documentTextWidth").toInt();
  const auto committed_editor_height = editor->property("patchy.documentTextHeight").toInt();
  CHECK(committed_editor_origin == expected_editor_origin);
  CHECK(committed_editor_width > width_before_resize);
  CHECK(committed_editor_height > height_before_resize);

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
  auto* text_item = require_layer_item(*layer_list, QStringLiteral("Imported!"));
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
  CHECK(editor->property("patchy.documentTextX").toInt() == committed_editor_origin.x());
  CHECK(editor->property("patchy.documentTextY").toInt() == committed_editor_origin.y());
  CHECK(editor->property("patchy.documentTextWidth").toInt() == committed_editor_width);
  CHECK(editor->property("patchy.documentTextHeight").toInt() == committed_editor_height);
  CHECK((editor->alignment() & Qt::AlignHCenter) != 0);
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();

  bool saved = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    dialog->selectFile(saved_path);
    saved = true;
    static_cast<QDialog*>(dialog)->accept();
  });
  require_action(window, "fileSaveAsAction")->trigger();
  CHECK(saved);
  CHECK(QFileInfo::exists(saved_path));

  auto reopened_document = patchy::psd::DocumentIo::read_file(std::filesystem::path(saved_path.toStdString()));
  patchy::ui::MainWindow reopened_window;
  show_window(reopened_window);
  reopened_window.add_document_session(std::move(reopened_document), QStringLiteral("Reopened Imported PSD Text"),
                                       saved_path);
  auto* reopened_canvas = require_canvas(reopened_window);
  reopened_canvas->set_zoom(1.0);
  QApplication::processEvents();
  auto* reopened_layer_list = reopened_window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(reopened_layer_list != nullptr);
  auto* reopened_text_item = require_layer_item(*reopened_layer_list, QStringLiteral("Imported!"));
  reopened_layer_list->clearSelection();
  reopened_layer_list->setCurrentItem(reopened_text_item);
  reopened_text_item->setSelected(true);
  QApplication::processEvents();

  require_action_by_text(reopened_window, QStringLiteral("Type"))->trigger();
  const auto reopened_hit_point =
      reopened_canvas->widget_position_for_document_point(committed_editor_origin + QPoint(8, 8));
  send_mouse(*reopened_canvas, QEvent::MouseButtonPress, reopened_hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*reopened_canvas, QEvent::MouseButtonRelease, reopened_hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* reopened_editor = reopened_canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(reopened_editor != nullptr);
  CHECK(reopened_editor->toPlainText() == QStringLiteral("Imported!"));
  CHECK(reopened_editor->property("patchy.documentTextX").toInt() == committed_editor_origin.x());
  CHECK(reopened_editor->property("patchy.documentTextY").toInt() == committed_editor_origin.y());
  CHECK(reopened_editor->property("patchy.documentTextWidth").toInt() == committed_editor_width);
  CHECK(reopened_editor->property("patchy.documentTextHeight").toInt() == committed_editor_height);
  send_key(*reopened_editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_psd_point_text_edit_origin_stays_at_glyph_top_after_transform() {
  // Regression: an imported PSD point-text layer stores its text transform translation at the
  // typographic baseline, which sits well below the visible glyph top.  Re-editing a freshly
  // imported layer correctly anchors the editor at the glyph top, but after applying any free
  // transform the editor anchor used to leap down to the baseline (~40-48px), making the text
  // jump on edit.  This locks the editor origin at the glyph top before and after a transform.
  patchy::Document document(420, 260, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(420, 260, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  // Visible glyphs occupy the top of the layer; the baseline is 48px below the visible top.
  auto pixels = solid_pixels(240, 70, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 200, 44), QColor(24, 24, 24, 255));

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Continue", std::move(pixels));
  const auto text_layer_id = text_layer.id();
  text_layer.set_bounds(patchy::Rect{100, 100, 240, 70});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Continue";
  text_layer.metadata()[patchy::kLayerMetadataTextRuns] = "v1\n0\t8\t48\t0\t0\t#181818\tArial";
  text_layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = "v1\n0\t8\tleft";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "48";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#181818";
  text_layer.metadata()[patchy::kLayerMetadataTextBold] = "false";
  text_layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  text_layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "3";
  // Baseline origin: Y = 148 = visible top (100) + 48px ascent.  Both transforms match on import.
  text_layer.metadata()[patchy::kLayerMetadataTextTransform] = "1 0 0 1 100 148";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextTransform] = "1 0 0 1 100 148";
  text_layer.metadata()[patchy::kLayerMetadataTextSourceBlock] = "TySh";
  document.add_layer(std::move(text_layer));
  document.set_active_layer(text_layer_id);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("PSD Point Text Transform Edit"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  // Before any transform the editor anchors at the visible glyph top (~100), not the baseline (148).
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto initial_hit = canvas->widget_position_for_document_point(QPoint(140, 118));
  send_mouse(*canvas, QEvent::MouseButtonPress, initial_hit, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, initial_hit, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->toPlainText() == QStringLiteral("Continue"));
  const int initial_y = editor->property("patchy.documentTextY").toInt();
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  // The editor anchors at the visible glyph top (~100), well above the baseline origin (148).
  CHECK(initial_y <= 118);

  // Apply a free transform via the numeric Move controls; the exact delta does not matter, only
  // that it routes through commit_free_transform's text branch (status -> patchy_raster) which is
  // the path that used to flip the editor anchor down to the baseline.
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();
  auto* x_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformXSpin"));
  auto* apply = window.findChild<QPushButton*>(QStringLiteral("freeTransformApplyButton"));
  CHECK(x_spin != nullptr);
  CHECK(apply != nullptr);
  x_spin->setValue(x_spin->value() + 40.0);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  apply->click();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  const auto* moved = patchy::ui::MainWindowTestAccess::document(window).find_layer(text_layer_id);
  CHECK(moved != nullptr);
  CHECK(moved->metadata().at(patchy::kLayerMetadataTextRasterStatus) == "patchy_raster");
  const auto moved_bounds = moved->bounds();
  // The composed text transform's translation Y is the (post-move) baseline origin.
  const auto moved_xform =
      QString::fromStdString(moved->metadata().at(patchy::kLayerMetadataTextTransform)).split(QLatin1Char(' '));
  CHECK(moved_xform.size() == 6);
  const int baseline_y = static_cast<int>(std::lround(moved_xform.at(5).toDouble()));
  // The glyphs still occupy the top of the layer, so the baseline sits well below the glyph top.
  CHECK(baseline_y - moved_bounds.y >= 30);

  // Re-open the editor over the moved glyphs.  The origin must still sit at the glyph top, not jump
  // down to the baseline the way it did before the fix.
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto reedit_hit =
      canvas->widget_position_for_document_point(QPoint(moved_bounds.x + 40, moved_bounds.y + 18));
  send_mouse(*canvas, QEvent::MouseButtonPress, reedit_hit, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, reedit_hit, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* reedit = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(reedit != nullptr);
  CHECK(reedit->toPlainText() == QStringLiteral("Continue"));
  const int after_y = reedit->property("patchy.documentTextY").toInt();
  send_key(*reedit, Qt::Key_Escape);
  QApplication::processEvents();
  // Before the fix this parked the editor on the baseline (~the +48px jump the bug report describes).
  // The origin must stay near the glyph top, comfortably above the baseline.
  CHECK(after_y <= baseline_y - 30);
  CHECK(after_y <= moved_bounds.y + 16);
}

void ui_imported_psd_text_preview_preserves_paragraph_layout() {
  if (skip_without_arial_for_psd_text_preview()) {
    return;
  }
  patchy::Document document(520, 280, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(520, 280, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(340, 150, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 300, 120), QColor(20, 20, 20, 255));

  const std::string first = "Speed Mode - Hold down TAB and the entire game will run faster than usual.";
  const std::string second = "Saving your game - Find a Save Machine and use it.";
  const std::string text = first + "\n" + second;
  const auto first_length = static_cast<int>(first.size()) + 1;
  const auto second_length = static_cast<int>(second.size());

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Imported Paragraph Layout", std::move(pixels));
  const auto text_layer_id = text_layer.id();
  text_layer.set_bounds(patchy::Rect{80, 80, 340, 150});
  text_layer.metadata()[patchy::kLayerMetadataText] = text;
  text_layer.metadata()[patchy::kLayerMetadataTextRuns] =
      "v2\n0\t" + std::to_string(text.size()) + "\t28\t1\t0\t#202020\tArial\t86.4";
  text_layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      "v2\n0\t" + std::to_string(first_length) + "\tleft\t-24\t24\t0\t0\t24\n" +
      std::to_string(first_length) + '\t' + std::to_string(second_length) + "\tleft\t-24\t24\t0\t0\t0";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  text_layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  text_layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  text_layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "3";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "300";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "150";
  text_layer.metadata()[patchy::kLayerMetadataTextSourceBlock] = "TySh";
  text_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "psd_raster_preview";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextTransform] = "1 0 0 1 0 0";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoxBounds] = "80 80 380 230";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoundingBox] = "80 80 380 210";
  document.add_layer(std::move(text_layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Imported PSD Paragraph Layout"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(90, 90));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  // Raster-preview sessions render live from entry.
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  const auto first_block = editor->document()->begin();
  CHECK(first_block.isValid());
  const auto first_format = first_block.blockFormat();
  CHECK(std::abs(first_format.leftMargin() - 24.0) < 0.5);
  CHECK(std::abs(first_format.textIndent() + 24.0) < 0.5);
  CHECK(std::abs(first_format.bottomMargin() - 24.0) < 0.5);

  QTextCursor cursor(editor->document());
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  editor->insertPlainText(QStringLiteral("!"));
  cursor = editor->textCursor();
  cursor.deletePreviousChar();
  editor->setTextCursor(cursor);
  process_events_for(120);
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());

  const auto* layout = editor->document()->documentLayout();
  CHECK(layout != nullptr);
  const auto preview_first_block = editor->document()->begin();
  CHECK(preview_first_block.isValid());
  const auto* first_text_layout = preview_first_block.layout();
  CHECK(first_text_layout != nullptr);
  CHECK(first_text_layout->lineCount() >= 2);
  const auto second_block = preview_first_block.next();
  CHECK(second_block.isValid());
  const auto* second_text_layout = second_block.layout();
  CHECK(second_text_layout != nullptr);
  CHECK(second_text_layout->lineCount() >= 1);

  const auto first_block_rect = layout->blockBoundingRect(preview_first_block);
  const auto second_block_rect = layout->blockBoundingRect(second_block);
  const auto first_line = first_text_layout->lineAt(0);
  const auto wrapped_line = first_text_layout->lineAt(1);
  const auto last_wrapped_line = first_text_layout->lineAt(first_text_layout->lineCount() - 1);
  const auto second_line = second_text_layout->lineAt(0);
  const auto first_y = static_cast<int>(std::floor(first_block_rect.top() + first_line.y()));
  const auto wrapped_y = static_cast<int>(std::floor(first_block_rect.top() + wrapped_line.y()));
  const auto last_wrapped_y = static_cast<int>(std::floor(first_block_rect.top() + last_wrapped_line.y()));
  const auto second_y = static_cast<int>(std::floor(second_block_rect.top() + second_line.y()));
  CHECK(second_y - last_wrapped_y > static_cast<int>(std::round(last_wrapped_line.height())) + 12);

  const QPoint preview_origin(editor->property("patchy.documentTextX").toInt(),
                              editor->property("patchy.documentTextY").toInt());
  const auto preview_width = std::max(1, editor->property("patchy.documentTextWidth").toInt());
  const auto first_line_bounds =
      dark_document_bounds(*canvas, QRect(preview_origin.x(), preview_origin.y() + std::max(0, first_y - 2),
                                          preview_width, 36));
  const auto wrapped_line_bounds =
      dark_document_bounds(*canvas, QRect(preview_origin.x(), preview_origin.y() + std::max(0, wrapped_y - 2),
                                          preview_width, 40));
  const auto last_wrapped_line_bounds =
      dark_document_bounds(*canvas, QRect(preview_origin.x(), preview_origin.y() + std::max(0, last_wrapped_y - 2),
                                          preview_width, 40));
  const auto second_line_bounds =
      dark_document_bounds(*canvas, QRect(preview_origin.x(), preview_origin.y() + std::max(0, second_y - 2),
                                          preview_width, 40));
  CHECK(first_line_bounds.has_value());
  CHECK(wrapped_line_bounds.has_value());
  CHECK(last_wrapped_line_bounds.has_value());
  CHECK(second_line_bounds.has_value());
  CHECK(wrapped_line_bounds->left() >= first_line_bounds->left() + 16);
  CHECK(second_line_bounds->top() >= last_wrapped_line_bounds->bottom() + 12);

  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
  const auto* committed_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(text_layer_id);
  CHECK(committed_layer != nullptr);
  const auto& committed_runs = committed_layer->metadata().at(patchy::kLayerMetadataTextRuns);
  CHECK(committed_runs.find("v2\n") == 0);
  CHECK(committed_runs.find("\t86.4") != std::string::npos ||
        committed_runs.find("\t86.400000000000006") != std::string::npos);
}

void ui_imported_psd_box_text_preview_uses_visual_bounds_after_edit() {
  if (skip_without_arial_for_psd_text_preview()) {
    return;
  }
  patchy::Document document(360, 220, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(360, 220, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(220, 115, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 150, 95), QColor(24, 24, 24, 255));

  const std::string text = "Alpha Alpha\nBeta Beta\nGamma Gamma";
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Imported Visual Bounds", std::move(pixels));
  text_layer.set_bounds(patchy::Rect{50, 50, 220, 115});
  text_layer.metadata()[patchy::kLayerMetadataText] = text;
  text_layer.metadata()[patchy::kLayerMetadataTextRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\t28\t1\t0\t#202020\tArial";
  text_layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\tleft";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  text_layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  text_layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  text_layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "3";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "220";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "80";
  text_layer.metadata()[patchy::kLayerMetadataTextSourceBlock] = "TySh";
  text_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "psd_raster_preview";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextTransform] = "1 0 0 1 50 60";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoxBounds] = "0 0 220 80";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoundingBox] = "0 -10 180 105";
  document.add_layer(std::move(text_layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Imported PSD Visual Bounds"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(58, 64));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  // Raster-preview sessions render live from entry.
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.documentTextX").toInt() == 50);
  CHECK(editor->property("patchy.documentTextY").toInt() == 60);
  CHECK(editor->property("patchy.documentTextWidth").toInt() == 220);
  CHECK(editor->property("patchy.documentTextHeight").toInt() == 80);

  QTextCursor cursor(editor->document());
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  editor->insertPlainText(QStringLiteral("!"));
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  process_events_for(160);
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.previewPaintsText").toBool());

  const auto bounds = dark_document_bounds(*canvas, QRect(40, 40, 250, 125));
  CHECK(bounds.has_value());
  CHECK(bounds->top() <= 52);
  CHECK(bounds->bottom() > 140);

  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_imported_psd_box_text_preview_preserves_descender_bleed_after_edit() {
  if (skip_without_arial_for_psd_text_preview()) {
    return;
  }
  patchy::Document document(320, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(320, 160, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(180, 68, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 160, 56), QColor(24, 24, 24, 255));

  const std::string text = "play";
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Imported Descender", std::move(pixels));
  text_layer.set_bounds(patchy::Rect{40, 50, 180, 68});
  text_layer.metadata()[patchy::kLayerMetadataText] = text;
  text_layer.metadata()[patchy::kLayerMetadataTextRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\t52\t1\t0\t#202020\tArial";
  text_layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\tleft";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "52";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  text_layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  text_layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  text_layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "3";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "180";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "48";
  text_layer.metadata()[patchy::kLayerMetadataTextSourceBlock] = "TySh";
  text_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "psd_raster_preview";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextTransform] = "1 0 0 1 40 50";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoxBounds] = "0 0 180 48";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoundingBox] = "0 -2 160 48";
  document.add_layer(std::move(text_layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Imported PSD Descender Bounds"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(48, 58));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  // Raster-preview sessions render live from entry.
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());

  QTextCursor cursor(editor->document());
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  editor->insertPlainText(QStringLiteral("!"));
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  process_events_for(160);

  constexpr int kFrameBottom = 50 + 48;
  const auto descender_bounds = dark_document_bounds(*canvas, QRect(40, kFrameBottom, 180, 24));
  CHECK(descender_bounds.has_value());
  CHECK(descender_bounds->bottom() >= kFrameBottom + 4);

  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_imported_psd_box_text_reedit_after_commit_preserves_descender_bleed() {
  if (skip_without_arial_for_psd_text_preview()) {
    return;
  }
  patchy::Document document(340, 180, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(340, 180, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(190, 72, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 170, 58), QColor(24, 24, 24, 255));

  const std::string text = "play CD-i";
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Imported Reedit Descender", std::move(pixels));
  text_layer.set_bounds(patchy::Rect{40, 50, 190, 72});
  text_layer.metadata()[patchy::kLayerMetadataText] = text;
  text_layer.metadata()[patchy::kLayerMetadataTextRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\t52\t1\t0\t#202020\tArial";
  text_layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\tleft";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "52";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  text_layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  text_layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  text_layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "3";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "190";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "48";
  text_layer.metadata()[patchy::kLayerMetadataTextSourceBlock] = "TySh";
  text_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "psd_raster_preview";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextTransform] = "1 0 0 1 40 50";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoxBounds] = "0 0 190 48";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoundingBox] = "0 -2 170 48";
  document.add_layer(std::move(text_layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Imported PSD Reedit Descender Bounds"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(48, 58));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  const auto cd_i_index = editor->toPlainText().indexOf(QStringLiteral(" CD-i"));
  CHECK(cd_i_index >= 0);
  QTextCursor cursor(editor->document());
  cursor.setPosition(cd_i_index);
  cursor.setPosition(cd_i_index + 5, QTextCursor::KeepAnchor);
  editor->setTextCursor(cursor);
  send_key(*editor, Qt::Key_Backspace);
  CHECK(editor->toPlainText() == QStringLiteral("play"));
  process_events_for(160);
  CHECK(editor->property("patchy.previewPaintsText").toBool());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  canvas->set_show_transform_controls(false);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  constexpr int kFrameBottom = 50 + 48;
  save_widget_artifact("ui_imported_psd_reedit_descender_committed", *canvas);
  const auto first_committed_bounds = dark_document_bounds(*canvas, QRect(35, 35, 230, 100));
  CHECK(first_committed_bounds.has_value());
  const auto committed_descender_bounds = dark_document_bounds(*canvas, QRect(40, kFrameBottom, 190, 24));
  CHECK(committed_descender_bounds.has_value());
  CHECK(committed_descender_bounds->bottom() >= kFrameBottom);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->toPlainText() == QStringLiteral("play"));
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").isValid());
  process_events_for(160);
  const auto reedit_bounds = dark_document_bounds(*canvas, QRect(35, 35, 230, 100));
  CHECK(reedit_bounds.has_value());
  CHECK(std::abs(reedit_bounds->top() - first_committed_bounds->top()) <= 1);
  CHECK(std::abs(reedit_bounds->bottom() - first_committed_bounds->bottom()) <= 1);

  const auto reedit_descender_bounds = dark_document_bounds(*canvas, QRect(40, kFrameBottom, 190, 24));
  CHECK(reedit_descender_bounds.has_value());
  CHECK(reedit_descender_bounds->bottom() >= kFrameBottom);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  canvas->set_show_transform_controls(false);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  const auto second_committed_bounds = dark_document_bounds(*canvas, QRect(35, 35, 230, 100));
  CHECK(second_committed_bounds.has_value());
  CHECK(std::abs(second_committed_bounds->top() - first_committed_bounds->top()) <= 1);
  CHECK(std::abs(second_committed_bounds->bottom() - first_committed_bounds->bottom()) <= 1);
}

void ui_imported_psd_box_text_line_clip_renders_full_visible_line_after_edit() {
#ifndef Q_OS_WIN
  // Pins exact line y-positions of 32pt Arial as laid out by the Windows font stack; the
  // CoreText/fontconfig databases produce slightly different line metrics, shifting which
  // line straddles the box edge. The behavior under test is platform-independent; the
  // pinned numbers are not.
  std::cout << "[SKIP] pins Windows Arial line metrics\n";
  return;
#endif
  patchy::Document document(420, 220, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(420, 220, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(320, 74, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 250, 74), QColor(24, 24, 24, 255));

  const std::string text = "Alpha line\nBeta line\nQuick state saves";
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Imported Full Line Clip", std::move(pixels));
  text_layer.set_bounds(patchy::Rect{50, 50, 320, 74});
  text_layer.metadata()[patchy::kLayerMetadataText] = text;
  text_layer.metadata()[patchy::kLayerMetadataTextRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\t32\t1\t0\t#202020\tArial";
  text_layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\tleft";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "32";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  text_layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  text_layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  text_layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "3";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "320";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "74";
  text_layer.metadata()[patchy::kLayerMetadataTextSourceBlock] = "TySh";
  text_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "psd_raster_preview";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextTransform] = "1 0 0 1 50 50";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoxBounds] = "0 0 320 74";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoundingBox] = "0 -2 250 74";
  document.add_layer(std::move(text_layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Imported PSD Full Line Clip"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(58, 58));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  // Raster-preview sessions render live from entry.
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());

  QTextCursor cursor(editor->document());
  cursor.setPosition(0);
  cursor.setPosition(1, QTextCursor::KeepAnchor);
  editor->setTextCursor(cursor);
  send_key(*editor, Qt::Key_Backspace);
  process_events_for(160);
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.previewPaintsText").toBool());

  const auto quick_line = editor_document_line_rect_containing(*editor, QStringLiteral("Quick state saves"));
  CHECK(quick_line.has_value());
  CHECK(quick_line->top() < 50.0 + 74.0);
  CHECK(quick_line->bottom() > 50.0 + 74.0);
  const auto quick_lower_bounds = dark_bounds_in_editor_line_lower_half(*canvas, *quick_line);
  CHECK(quick_lower_bounds.has_value());
  CHECK(quick_lower_bounds->bottom() >= 50 + 74 + 18);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  canvas->set_show_transform_controls(false);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  const auto committed_bounds = dark_document_bounds(*canvas, QRect(40, 40, 340, 135));
  CHECK(committed_bounds.has_value());

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  process_events_for(160);
  const auto reedit_line = editor_document_line_rect_containing(*editor, QStringLiteral("Quick state saves"));
  CHECK(reedit_line.has_value());
  const auto reedit_lower_bounds = dark_bounds_in_editor_line_lower_half(*canvas, *reedit_line);
  CHECK(reedit_lower_bounds.has_value());
  const auto reedit_bounds = dark_document_bounds(*canvas, QRect(40, 40, 340, 135));
  CHECK(reedit_bounds.has_value());
  CHECK(std::abs(reedit_bounds->top() - committed_bounds->top()) <= 1);
  CHECK(std::abs(reedit_bounds->bottom() - committed_bounds->bottom()) <= 1);

  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_imported_psd_box_text_line_clip_hides_overflow_after_edit() {
#ifndef Q_OS_WIN
  // Same Windows-Arial line-metric pinning as
  // ui_imported_psd_box_text_line_clip_renders_full_visible_line_after_edit above.
  std::cout << "[SKIP] pins Windows Arial line metrics\n";
  return;
#endif
  patchy::Document document(460, 250, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(460, 250, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(340, 74, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 260, 74), QColor(24, 24, 24, 255));

  const std::string text = "Alpha line\nBeta line\nQuick state saves\nHidden overflow text";
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Imported Hidden Overflow", std::move(pixels));
  text_layer.set_bounds(patchy::Rect{50, 50, 340, 74});
  text_layer.metadata()[patchy::kLayerMetadataText] = text;
  text_layer.metadata()[patchy::kLayerMetadataTextRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\t32\t1\t0\t#202020\tArial";
  text_layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      "v1\n0\t" + std::to_string(text.size()) + "\tleft";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "32";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  text_layer.metadata()[patchy::kLayerMetadataTextBold] = "true";
  text_layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  text_layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "3";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "340";
  text_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "74";
  text_layer.metadata()[patchy::kLayerMetadataTextSourceBlock] = "TySh";
  text_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "psd_raster_preview";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextTransform] = "1 0 0 1 50 50";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoxBounds] = "0 0 340 74";
  text_layer.metadata()[patchy::kLayerMetadataPsdTextBoundingBox] = "0 -2 260 74";
  document.add_layer(std::move(text_layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Imported PSD Hidden Overflow"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(58, 58));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  QTextCursor cursor(editor->document());
  cursor.setPosition(0);
  cursor.setPosition(1, QTextCursor::KeepAnchor);
  editor->setTextCursor(cursor);
  send_key(*editor, Qt::Key_Backspace);
  process_events_for(160);
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.previewPaintsText").toBool());

  const auto quick_line = editor_document_line_rect_containing(*editor, QStringLiteral("Quick state saves"));
  CHECK(quick_line.has_value());
  const auto quick_lower_bounds = dark_bounds_in_editor_line_lower_half(*canvas, *quick_line);
  CHECK(quick_lower_bounds.has_value());

  const auto hidden_line = editor_document_line_rect_containing(*editor, QStringLiteral("Hidden overflow"));
  CHECK(hidden_line.has_value());
  CHECK(hidden_line->top() >= 50.0 + 74.0);
  const QRect hidden_sample(static_cast<int>(std::floor(hidden_line->left())) - 4,
                            static_cast<int>(std::floor(hidden_line->top())) - 4,
                            static_cast<int>(std::ceil(hidden_line->width())) + 8,
                            static_cast<int>(std::ceil(hidden_line->height())) + 12);
  CHECK(!dark_document_bounds(*canvas, hidden_sample).has_value());

  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_cdi_a4_title_text_import_edit_visual_bounds_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("CDi_A4.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  auto document = patchy::psd::DocumentIo::read_file(path);
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("CDi A4 Text Import"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(0.25);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(230, 286));
  accept_missing_psd_text_font_warning_if_present();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  // Raster-preview sessions render live from entry.
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.documentTextWidth").toInt() > 2000);
  CHECK(editor->property("patchy.documentTextHeight").toInt() > 290);

  const auto hyphen_index = editor->toPlainText().indexOf(QStringLiteral("CD-i"));
  CHECK(hyphen_index >= 0);
  QTextCursor cursor(editor->document());
  cursor.setPosition(hyphen_index);
  cursor.setPosition(hyphen_index + 4, QTextCursor::KeepAnchor);
  editor->setTextCursor(cursor);
  send_key(*editor, Qt::Key_Backspace);
  CHECK(!editor->toPlainText().contains(QStringLiteral("CD-i")));
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  process_events_for(500);
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.previewPaintsText").toBool());

  const auto title_bounds = dark_document_bounds(*canvas, QRect(190, 245, 2050, 380));
  CHECK(title_bounds.has_value());
  CHECK(title_bounds->top() <= 270);
  CHECK(title_bounds->bottom() >= 570);
  save_widget_artifact("ui_cdi_a4_title_text_after_edit", *canvas);
  if (QFontDatabase::families().contains(QStringLiteral("BookmaniaW02"))) {
    const auto descender_bounds = dark_document_bounds(*canvas, QRect(1500, 560, 650, 36));
    CHECK(descender_bounds.has_value());
    CHECK(descender_bounds->bottom() >= 575);
  }

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  canvas->set_show_transform_controls(false);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(!editor->toPlainText().contains(QStringLiteral("CD-i")));
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").isValid());
  process_events_for(500);
  save_widget_artifact("ui_cdi_a4_title_text_reedit_after_commit", *canvas);

  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_psd_point_text_edit_origin_survives_scale_transform_if_available() {
  // Regression (the reported repro): open ipad_main_v04.psd, scale/transform the "Continue" point
  // text layer, then re-edit it with the Type tool.  The editor origin must stay on the glyphs.
  // The bug parked it on the PSD baseline (~one ascent below the glyph top), so the text leapt down
  // ~40px on edit.  A scaled transform exercises the non-translation alignment path
  // (psd_point_text_local_bounds_transform_for_pixels), which used to bail once the user's transform
  // diverged from the PSD source.
  const auto path = patchy::test::local_psd_fixture_path("ipad_main_v04.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  auto document = patchy::psd::DocumentIo::read_file(path);
  std::function<const patchy::Layer*(const std::vector<patchy::Layer>&)> find_continue =
      [&](const std::vector<patchy::Layer>& layers) -> const patchy::Layer* {
    for (const auto& layer : layers) {
      const auto text_it = layer.metadata().find(patchy::kLayerMetadataText);
      const bool is_continue =
          text_it != layer.metadata().end() &&
          (QString::fromStdString(text_it->second).trimmed().compare(QStringLiteral("Continue"),
                                                                     Qt::CaseInsensitive) == 0 ||
           QString::fromStdString(layer.name()).contains(QStringLiteral("Continue"), Qt::CaseInsensitive));
      if (is_continue) {
        return &layer;
      }
      if (const auto* found = find_continue(layer.children()); found != nullptr) {
        return found;
      }
    }
    return nullptr;
  };
  const auto* continue_layer = find_continue(document.layers());
  if (continue_layer == nullptr || !continue_layer->metadata().contains(patchy::kLayerMetadataPsdTextTransform)) {
    return;  // fixture layout changed
  }
  const auto text_layer_id = continue_layer->id();

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("iPad Continue Scale Edit"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  // Opens the Type editor over the (current) glyph centre and returns its document-Y origin, then
  // dismisses the editor and returns to the Move tool.
  auto open_editor_y = [&]() -> int {
    require_action_by_text(window, QStringLiteral("Type"))->trigger();
    const auto* live = patchy::ui::MainWindowTestAccess::document(window).find_layer(text_layer_id);
    CHECK(live != nullptr);
    const auto b = live->bounds();
    const QPoint click_doc(b.x + b.width / 2, b.y + b.height / 2);
    const auto hit_point = canvas->widget_position_for_document_point(click_doc);
    accept_missing_psd_text_font_warning_if_present();
    send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    CHECK(editor != nullptr);
    const int doc_y = editor->property("patchy.documentTextY").toInt();
    send_key(*editor, Qt::Key_Escape);
    QApplication::processEvents();
    require_action_by_text(window, QStringLiteral("Move"))->trigger();
    QApplication::processEvents();
    return doc_y;
  };

  const int pre_y = open_editor_y();

  // Scale the layer (non-translation transform) and commit through the free-transform text branch.
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();
  auto* scale_y_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleYSpin"));
  auto* apply = window.findChild<QPushButton*>(QStringLiteral("freeTransformApplyButton"));
  CHECK(scale_y_spin != nullptr);
  CHECK(apply != nullptr);
  scale_y_spin->setValue(120.0);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  apply->click();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  const auto* moved = patchy::ui::MainWindowTestAccess::document(window).find_layer(text_layer_id);
  CHECK(moved != nullptr);
  CHECK(moved->metadata().at(patchy::kLayerMetadataTextRasterStatus) == "patchy_raster");
  // The composed transform translation Y is the (scaled) PSD baseline origin.
  const auto moved_xform =
      QString::fromStdString(moved->metadata().at(patchy::kLayerMetadataTextTransform)).split(QLatin1Char(' '));
  CHECK(moved_xform.size() == 6);
  const int baseline_y = static_cast<int>(std::lround(moved_xform.at(5).toDouble()));

  const int post_y = open_editor_y();

  // The editor must re-open on the glyphs, well above the baseline, and must not have leapt down by
  // roughly an ascent relative to where it opened before the transform.
  CHECK(baseline_y - post_y >= 30);
  CHECK(std::abs(post_y - pre_y) <= 30);
}

void ui_psd_point_text_installed_font_scales_and_edits_crisply() {
  // The user's real scenario (FZ SCRIPT 25 installed): a PSD point-text layer scales crisply, its
  // free-transform bounds hug the glyphs (not ~3x wider), and EDITING it stays crisp instead of
  // dropping to a blocky resample.  The test machine lacks that font, so retarget the "Continue" layer
  // to an installed font (Arial) to exercise the installed-font PSD path end to end.
  const auto path = patchy::test::local_psd_fixture_path("ipad_main_v04.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  auto document = patchy::psd::DocumentIo::read_file(path);
  std::function<const patchy::Layer*(const std::vector<patchy::Layer>&)> find_continue =
      [&](const std::vector<patchy::Layer>& layers) -> const patchy::Layer* {
    for (const auto& layer : layers) {
      const auto it = layer.metadata().find(patchy::kLayerMetadataText);
      if (it != layer.metadata().end() &&
          QString::fromStdString(it->second).trimmed().compare(QStringLiteral("Continue"), Qt::CaseInsensitive) ==
              0) {
        return &layer;
      }
      if (const auto* found = find_continue(layer.children()); found != nullptr) {
        return found;
      }
    }
    return nullptr;
  };
  const auto* continue_layer = find_continue(document.layers());
  if (continue_layer == nullptr || !continue_layer->metadata().contains(patchy::kLayerMetadataPsdTextTransform)) {
    return;
  }
  const auto id = continue_layer->id();

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("iPad Continue Installed Font"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  // Retarget the glyphs to an installed font so the crisp PSD path (font-gated) runs.
  if (auto* l = patchy::ui::MainWindowTestAccess::document(window).find_layer(id); l != nullptr) {
    l->metadata()[patchy::kLayerMetadataTextFont] = "Arial";
    l->metadata().erase(patchy::kLayerMetadataTextRuns);
    l->metadata().erase(patchy::kLayerMetadataTextHtml);
  }

  const auto sharpest_ramp = [](const QImage& img) {
    int sharpest = 9999;
    for (int y = 0; y < img.height(); ++y) {
      int x = 0;
      while (x < img.width()) {
        if (qAlpha(img.pixel(x, y)) <= 20) {
          ++x;
          continue;
        }
        int ramp = 0;
        while (x < img.width() && qAlpha(img.pixel(x, y)) > 20 && qAlpha(img.pixel(x, y)) < 235) {
          ++ramp;
          ++x;
        }
        if (x < img.width() && qAlpha(img.pixel(x, y)) >= 235 && ramp > 0) {
          sharpest = std::min(sharpest, ramp);
        }
        while (x < img.width() && qAlpha(img.pixel(x, y)) > 20) {
          ++x;
        }
      }
    }
    return sharpest;
  };
  const auto visible_width = [](const QImage& img) {
    int minx = img.width(), maxx = -1;
    for (int y = 0; y < img.height(); ++y) {
      for (int x = 0; x < img.width(); ++x) {
        if (qAlpha(img.pixel(x, y)) > 20) {
          minx = std::min(minx, x);
          maxx = std::max(maxx, x);
        }
      }
    }
    return maxx >= minx ? maxx - minx + 1 : 0;
  };

  // Make the layer active.
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  {
    const auto* live = patchy::ui::MainWindowTestAccess::document(window).find_layer(id);
    const auto b = live->bounds();
    const auto hit = canvas->widget_position_for_document_point(QPoint(b.x + b.width / 2, b.y + b.height / 2));
    accept_missing_psd_text_font_warning_if_present();
    send_mouse(*canvas, QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    if (auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")); editor != nullptr) {
      send_key(*editor, Qt::Key_Escape);
      QApplication::processEvents();
    }
  }

  const auto scale = [&](double pct) {
    require_action_by_text(window, QStringLiteral("Move"))->trigger();
    canvas->set_show_transform_controls(true);
    QApplication::processEvents();
    require_action(window, "editFreeTransformAction")->trigger();
    QApplication::processEvents();
    CHECK(canvas->free_transform_active());
    window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleXSpin"))->setValue(pct);
    window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleYSpin"))->setValue(pct);
    QApplication::processEvents();
    send_key(*canvas, Qt::Key_Return);
    QApplication::processEvents();
    CHECK(!canvas->free_transform_active());
  };

  scale(250.0);
  const auto* scaled = patchy::ui::MainWindowTestAccess::document(window).find_layer(id);
  CHECK(scaled != nullptr);
  const auto scaled_img = image_from_pixels_for_visuals(scaled->pixels());
  ensure_artifact_dir();
  CHECK(flattened_on_white(scaled->pixels())
            .save(QStringLiteral("test-artifacts/ui_psd_continue_installed_font_scaled.png")));
  CHECK(scaled_img.height() > 40);
  // Crisp (sharp alpha edge somewhere) ...
  CHECK(sharpest_ramp(scaled_img) <= 3);
  // ... and the bounds hug the glyphs (no ~3x transparent margin): visible width ~ full image width.
  CHECK(scaled_img.width() - visible_width(scaled_img) <= 6);

  // Now EDIT it and commit: must stay crisp (the S3 fix), not drop to a blocky resample.
  const auto scaled_rect = canvas->active_layer_document_rect();
  CHECK(scaled_rect.has_value());
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  accept_missing_psd_text_font_warning_if_present();
  const auto edit_hit = canvas->widget_position_for_document_point(scaled_rect->center());
  send_mouse(*canvas, QEvent::MouseButtonPress, edit_hit, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, edit_hit, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();

  const auto* edited = patchy::ui::MainWindowTestAccess::document(window).find_layer(id);
  CHECK(edited != nullptr);
  const auto edited_img = image_from_pixels_for_visuals(edited->pixels());
  CHECK(flattened_on_white(edited->pixels())
            .save(QStringLiteral("test-artifacts/ui_psd_continue_installed_font_edited.png")));
  CHECK(sharpest_ramp(edited_img) <= 3);
}

void ui_psd_point_text_transform_scales_crisply() {
  // PSD-imported type with an INSTALLED font is re-rasterized crisply through the glyph-top-aligned
  // transform; with a MISSING font it keeps the resampled bitmap so the decorative glyph shapes are
  // preserved rather than swapped for a substitute face.  ipad_main_v04.psd uses a game font that is
  // not installed in the test environment, so this exercises the missing-font path: the scaled layer
  // must NOT become a crisp substitute render.  Before/after artifacts are saved for inspection.
  const auto path = patchy::test::local_psd_fixture_path("ipad_main_v04.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  auto document = patchy::psd::DocumentIo::read_file(path);
  std::function<const patchy::Layer*(const std::vector<patchy::Layer>&)> find_continue =
      [&](const std::vector<patchy::Layer>& layers) -> const patchy::Layer* {
    for (const auto& layer : layers) {
      const auto text_it = layer.metadata().find(patchy::kLayerMetadataText);
      if (text_it != layer.metadata().end() &&
          QString::fromStdString(text_it->second).trimmed().compare(QStringLiteral("Continue"),
                                                                    Qt::CaseInsensitive) == 0) {
        return &layer;
      }
      if (const auto* found = find_continue(layer.children()); found != nullptr) {
        return found;
      }
    }
    return nullptr;
  };
  const auto* continue_layer = find_continue(document.layers());
  if (continue_layer == nullptr || !continue_layer->metadata().contains(patchy::kLayerMetadataPsdTextTransform)) {
    return;
  }
  const auto text_layer_id = continue_layer->id();

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("iPad Continue Crisp Scale"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  ensure_artifact_dir();
  {
    const auto* before = patchy::ui::MainWindowTestAccess::document(window).find_layer(text_layer_id);
    CHECK(before != nullptr);
    CHECK(flattened_on_white(before->pixels())
              .save(QStringLiteral("test-artifacts/ui_psd_continue_before_scale.png")));
  }

  // Make the layer active by briefly opening its editor, then scale it through the free-transform
  // text branch.
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  {
    const auto* live = patchy::ui::MainWindowTestAccess::document(window).find_layer(text_layer_id);
    const auto b = live->bounds();
    const auto hit = canvas->widget_position_for_document_point(QPoint(b.x + b.width / 2, b.y + b.height / 2));
    accept_missing_psd_text_font_warning_if_present();
    send_mouse(*canvas, QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    if (auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")); editor != nullptr) {
      send_key(*editor, Qt::Key_Escape);
      QApplication::processEvents();
    }
  }
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();
  auto* scale_x_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleXSpin"));
  auto* scale_y_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleYSpin"));
  auto* apply = window.findChild<QPushButton*>(QStringLiteral("freeTransformApplyButton"));
  CHECK(scale_x_spin != nullptr);
  CHECK(scale_y_spin != nullptr);
  CHECK(apply != nullptr);
  scale_x_spin->setValue(250.0);
  scale_y_spin->setValue(250.0);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  apply->click();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  const auto* scaled = patchy::ui::MainWindowTestAccess::document(window).find_layer(text_layer_id);
  CHECK(scaled != nullptr);
  const auto image = image_from_pixels_for_visuals(scaled->pixels());
  CHECK(flattened_on_white(scaled->pixels())
            .save(QStringLiteral("test-artifacts/ui_psd_continue_after_scale.png")));
  // The layer scaled up (~2.5x taller than the imported ~36px glyphs).
  CHECK(image.height() > 70);
  CHECK(image.width() > 70);

  // Missing-font safeguard: the result must NOT be a crisp substitute render.  A 2.5x bitmap upscale
  // leaves soft, multi-pixel anti-aliased edges everywhere; a crisp vector render would leave many
  // hard 1px edges.  Count rows whose sharpest alpha transition is a single pixel -- there should be
  // very few if the imported bitmap was preserved.
  int hard_edge_rows = 0;
  for (int y = 0; y < image.height(); ++y) {
    int x = 0;
    int row_sharpest = 9999;
    while (x < image.width()) {
      if (qAlpha(image.pixel(x, y)) <= 20) {
        ++x;
        continue;
      }
      int ramp = 0;
      while (x < image.width() && qAlpha(image.pixel(x, y)) > 20 && qAlpha(image.pixel(x, y)) < 235) {
        ++ramp;
        ++x;
      }
      if (x < image.width() && qAlpha(image.pixel(x, y)) >= 235 && ramp > 0) {
        row_sharpest = std::min(row_sharpest, ramp);
      }
      while (x < image.width() && qAlpha(image.pixel(x, y)) > 20) {
        ++x;
      }
    }
    if (row_sharpest <= 1) {
      ++hard_edge_rows;
    }
  }
  CHECK(hard_edge_rows < image.height() / 4);
}

void ui_imported_psd_box_text_follows_markers_after_edit_if_available() {
  // Regression: editing an imported PSD box text layer and then dragging its bounding-box markers
  // must move the rendered glyphs together with the caret/markers. The bug pinned the glyphs at
  // their original import position because the marker-drag handler dropped the editor's transform
  // override, so the preview fell back to the layer's original Photoshop transform.
  const auto path = patchy::test::local_psd_fixture_path("ipad_main_v04.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  auto document = patchy::psd::DocumentIo::read_file(path);

  // Locate the "Continue" text layer (the reported repro) anywhere in the layer/group tree.
  std::function<const patchy::Layer*(const std::vector<patchy::Layer>&)> find_continue =
      [&](const std::vector<patchy::Layer>& layers) -> const patchy::Layer* {
    for (const auto& layer : layers) {
      const auto text_it = layer.metadata().find(patchy::kLayerMetadataText);
      const bool is_continue =
          (text_it != layer.metadata().end() &&
           QString::fromStdString(text_it->second).contains(QStringLiteral("Continue"), Qt::CaseInsensitive)) ||
          QString::fromStdString(layer.name()).contains(QStringLiteral("Continue"), Qt::CaseInsensitive);
      if (text_it != layer.metadata().end() && is_continue) {
        return &layer;
      }
      if (const auto* found = find_continue(layer.children()); found != nullptr) {
        return found;
      }
    }
    return nullptr;
  };
  const auto* continue_layer = find_continue(document.layers());
  if (continue_layer == nullptr) {
    return;  // fixture layout changed; nothing to assert against
  }
  const auto layer_bounds = continue_layer->bounds();
  if (layer_bounds.width <= 0 || layer_bounds.height <= 0) {
    return;
  }

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("iPad Continue Marker Move"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const QPoint click_doc(layer_bounds.x + layer_bounds.width / 2, layer_bounds.y + layer_bounds.height / 2);
  const auto hit_point = canvas->widget_position_for_document_point(click_doc);
  accept_missing_psd_text_font_warning_if_present();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);

  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  constexpr int kDragUp = 60;  // canvas px == document px at zoom 1.0

  auto drag_top_marker_up = [&]() {
    auto* handle = canvas->findChild<QWidget*>(QStringLiteral("textBoxResizeHandleTopLeft"));
    CHECK(handle != nullptr);
    const auto handle_center = handle->geometry().center();
    drag(*canvas, handle_center, handle_center + QPoint(0, -kDragUp));
    process_events_for(200);
  };

  // First drag converts the point text to a moved/resized box and builds the live glyph preview.
  drag_top_marker_up();
  auto* preview1 = preview_layer_for_editor(live_document, *editor);
  CHECK(preview1 != nullptr);
  const auto preview_y1 = preview1->bounds().y;
  const auto doc_y1 = editor->property("patchy.documentTextY").toInt();

  // Second identical drag moves the box origin up by another kDragUp.
  drag_top_marker_up();
  auto* preview2 = preview_layer_for_editor(live_document, *editor);
  CHECK(preview2 != nullptr);
  const auto preview_y2 = preview2->bounds().y;
  const auto doc_y2 = editor->property("patchy.documentTextY").toInt();
  save_widget_artifact("ui_ipad_continue_after_marker_move", *canvas);

  // The markers/caret followed the second drag (box origin moved up). This always holds; the
  // regression is specifically that the rendered glyphs did NOT follow.
  const auto origin_delta = doc_y1 - doc_y2;
  CHECK(origin_delta >= kDragUp / 2);

  // The rendered glyph layer must move up with the box. Before the fix the marker-drag handler
  // dropped the text transform override, so the preview fell back to the layer's original import
  // transform and stayed pinned (preview_delta ~ 0 while origin_delta ~ kDragUp).
  const auto preview_delta = preview_y1 - preview_y2;
  CHECK(std::abs(preview_delta - origin_delta) <= 12);

  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_tips_psd_speed_mode_line_clip_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("tips.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  auto document = patchy::psd::DocumentIo::read_file(path);
  std::vector<AlphaRowBand> source_bands;
  std::function<bool(const std::vector<patchy::Layer>&)> find_source_bands =
      [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          if (const auto found = layer.metadata().find(patchy::kLayerMetadataText);
              found != layer.metadata().end() && found->second.find("Hold down TAB") != std::string::npos) {
            source_bands = alpha_row_bands(layer.pixels());
            return true;
          }
          if (find_source_bands(layer.children())) {
            return true;
          }
        }
        return false;
      };
  CHECK(find_source_bands(document.layers()));
  CHECK(source_bands.size() >= 5U);
  const auto source_span = alpha_row_band_span(source_bands);
  CHECK(source_span > 100);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Tips PSD Text Import"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  QListWidgetItem* speed_item = nullptr;
  for (int row = 0; row < layer_list->count(); ++row) {
    if (layer_list->item(row)->text().contains(QStringLiteral("Hold down TAB"), Qt::CaseInsensitive)) {
      speed_item = layer_list->item(row);
      break;
    }
  }
  CHECK(speed_item != nullptr);
  layer_list->clearSelection();
  layer_list->setCurrentItem(speed_item);
  speed_item->setSelected(true);
  QApplication::processEvents();
  const auto edited_layer_id =
      static_cast<patchy::LayerId>(speed_item->data(patchy::ui::kLayerIdRole).toULongLong());

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(116, 166));
  accept_missing_psd_text_font_warning_if_present();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->toPlainText().contains(QStringLiteral("Speed Mode")));
  CHECK(editor->toPlainText().contains(QStringLiteral("Hold down TAB")));
  CHECK(editor->toPlainText().contains(QStringLiteral("Quick state saves")));
  CHECK(editor->property("patchy.textMetricScale").isValid());
  CHECK(editor->property("patchy.textMetricScale").toDouble() < 0.98);

  const auto mode_index = editor->toPlainText().indexOf(QStringLiteral("Speed Mode"));
  CHECK(mode_index >= 0);
  QTextCursor cursor(editor->document());
  cursor.setPosition(mode_index + QStringLiteral("Speed Mode").size() - 1);
  cursor.setPosition(mode_index + QStringLiteral("Speed Mode").size(), QTextCursor::KeepAnchor);
  editor->setTextCursor(cursor);
  send_key(*editor, Qt::Key_Backspace);
  process_events_for(500);
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  auto* preview_layer =
      preview_layer_for_editor(patchy::ui::MainWindowTestAccess::document(window), *editor);
  CHECK(preview_layer != nullptr);
  const auto preview_bands = alpha_row_bands(preview_layer->pixels());
  CHECK(preview_bands.size() == source_bands.size());
  CHECK(std::abs(alpha_row_band_span(preview_bands) - source_span) <= 24);
  CHECK(preview_bands.back().bottom - preview_bands.back().top >=
        source_bands.back().bottom - source_bands.back().top - 4);
  save_widget_artifact("ui_tips_psd_speed_mode_after_edit", *canvas);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  canvas->set_show_transform_controls(false);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  auto* committed_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(edited_layer_id);
  CHECK(committed_layer != nullptr);
  const auto committed_bands = alpha_row_bands(committed_layer->pixels());
  CHECK(committed_bands.size() == source_bands.size());
  CHECK(std::abs(alpha_row_band_span(committed_bands) - source_span) <= 24);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->toPlainText().contains(QStringLiteral("Speed Mod")));
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  process_events_for(500);
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.textMetricScale").isValid());
  CHECK(editor->property("patchy.textMetricScale").toDouble() < 0.98);
  preview_layer = preview_layer_for_editor(patchy::ui::MainWindowTestAccess::document(window), *editor);
  CHECK(preview_layer != nullptr);
  const auto reedit_bands = alpha_row_bands(preview_layer->pixels());
  CHECK(reedit_bands.size() == committed_bands.size());
  CHECK(std::abs(alpha_row_band_span(reedit_bands) - alpha_row_band_span(committed_bands)) <= 4);
  CHECK(reedit_bands.back().bottom - reedit_bands.back().top >=
        committed_bands.back().bottom - committed_bands.back().top - 2);
  save_widget_artifact("ui_tips_psd_speed_mode_reedit", *canvas);

  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_horror_virtualboy_caret_tracks_zoom_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("Horror VirtualBoy.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  auto document = patchy::psd::DocumentIo::read_file(path);
  patchy::Rect text_bounds{};
  std::string body_text;
  std::function<bool(const std::vector<patchy::Layer>&)> find_body =
      [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          if (const auto found = layer.metadata().find(patchy::kLayerMetadataText);
              found != layer.metadata().end() && found->second.find("Necronomicon") != std::string::npos) {
            text_bounds = layer.bounds();
            body_text = found->second;
            return true;
          }
          if (find_body(layer.children())) {
            return true;
          }
        }
        return false;
      };
  CHECK(find_body(document.layers()));
  CHECK(text_bounds.width > 0 && text_bounds.height > 0);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Horror VirtualBoy Caret"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(0.3);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const QPoint document_click(text_bounds.x + text_bounds.width / 2, text_bounds.y + 12);
  const auto hit_point = canvas->widget_position_for_document_point(document_click);
  accept_missing_psd_text_font_warning_if_present();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->property("patchy.previewPaintsText").toBool());

  const auto caret_document_position = [&](double zoom) -> QPointF {
    canvas->set_zoom(zoom);
    QApplication::processEvents();
    QTextCursor cursor(editor->document());
    cursor.movePosition(QTextCursor::End);
    editor->setTextCursor(cursor);
    QApplication::processEvents();
    auto caret = editor->property("patchy.previewCaretRect").toRect();
    if (caret.isEmpty()) {
      caret = editor->cursorRect();
    }
    const double doc_x = editor->property("patchy.documentTextX").toInt() + caret.center().x() / zoom;
    const double doc_y = editor->property("patchy.documentTextY").toInt() + caret.center().y() / zoom;
    return QPointF(doc_x, doc_y);
  };

  const auto caret_full = caret_document_position(1.0);
  const auto caret_zoomed = caret_document_position(0.3);
  // The caret marks the same character in document space regardless of zoom, so its
  // document-space position must stay stable across zoom levels.
  CHECK(std::abs(caret_full.x() - caret_zoomed.x()) <= 3.0);
  CHECK(std::abs(caret_full.y() - caret_zoomed.y()) <= 3.0);

  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_imported_psd_raster_point_text_renders_live_when_font_available_if_available() {
  // Regression (reported repro): open a PSD whose imported point-text layer is a "psd_raster_preview"
  // (no regeneratable outer effect) and edit it with the Type tool.  The layer kept Photoshop's baked
  // glyphs on screen while the caret/selection were derived from Patchy's own Qt layout of the same
  // installed font; Photoshop and Qt advance the glyphs slightly differently, so the caret/selection
  // drifted against the visible text until the first edit swapped the display to Patchy's live render.
  // When the font is available, render live from the start so the caret lands on the glyphs -- but
  // still restore Photoshop's original pixels if the session ends without a real edit.
  const auto path = patchy::test::local_psd_fixture_path("Horror VirtualBoy.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  // The reported machine has Verdana installed (it ships with Windows).  The offscreen Qt platform
  // does not auto-enumerate system fonts, so register Verdana before import to take the live path.
  // (Leaving it registered is harmless: the only test that relies on Verdana being absent --
  // ui_horror_virtualboy_caret_tracks_zoom -- runs earlier, and QFontDatabase::removeApplicationFont
  // can crash when invalidating an in-use font cache.)
  register_test_fonts(TestFontRole::Verdana);
  if (!QFontDatabase::families().contains(QStringLiteral("Verdana"))) {
    return;  // font genuinely unavailable; the live path can't be exercised
  }
  auto document = patchy::psd::DocumentIo::read_file(path);

  patchy::Rect text_bounds{};
  patchy::LayerId body_id = 0;
  bool found = false;
  std::function<void(const std::vector<patchy::Layer>&)> find_body =
      [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          if (!found) {
            const auto flow = layer.metadata().count(patchy::kLayerMetadataTextFlow) != 0
                                  ? layer.metadata().at(patchy::kLayerMetadataTextFlow)
                                  : std::string("point");
            if (const auto it = layer.metadata().find(patchy::kLayerMetadataText);
                it != layer.metadata().end() && it->second.find("Did you know") != std::string::npos &&
                layer.metadata().count(patchy::kLayerMetadataTextRasterStatus) != 0 &&
                layer.metadata().at(patchy::kLayerMetadataTextRasterStatus) == "psd_raster_preview" &&
                flow != "box") {
              text_bounds = layer.bounds();
              body_id = layer.id();
              found = true;
            }
          }
          find_body(layer.children());
        }
      };
  find_body(document.layers());
  if (!found) {
    return;  // fixture layout changed; nothing to assert against
  }
  CHECK(text_bounds.width > 0 && text_bounds.height > 0);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Horror Did You Know Live Edit"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  auto* body_before = live_document.find_layer(body_id);
  CHECK(body_before != nullptr);
  const bool body_was_visible = body_before->visible();
  // Deep-copy the imported (Photoshop-rendered) pixels so we can prove a no-edit session preserves them.
  const std::vector<std::uint8_t> original_bytes(body_before->pixels().data().begin(),
                                                 body_before->pixels().data().end());
  const auto original_w = body_before->pixels().width();
  const auto original_h = body_before->pixels().height();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const QPoint click_doc(text_bounds.x + text_bounds.width / 2, text_bounds.y + 12);
  const auto hit_point = canvas->widget_position_for_document_point(click_doc);
  accept_missing_psd_text_font_warning_if_present();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  process_events_for(200);

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(QString::fromStdString(editor->toPlainText().toStdString()).contains(QStringLiteral("Did you know")));
  process_events_for(300);  // let the live baked preview render

  // The fix: with the font available, the edit is driven through the live baked-preview path (the same
  // render_text_pixels rasterizer the committed layer uses), so the caret/selection track the glyphs AND
  // the text does not shift or change antialiasing on entering/leaving edit.
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.forceBakedPreview").toBool());
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  auto* preview = preview_layer_for_editor(live_document, *editor);
  CHECK(preview != nullptr);
  // The live preview must land on the original glyphs, not jump: its top-left stays near the imported
  // layer's, so entering edit doesn't visibly move the text.
  CHECK(std::abs(preview->bounds().x - text_bounds.x) <= 24);
  CHECK(std::abs(preview->bounds().y - text_bounds.y) <= 24);
  // The original Photoshop layer is hidden while the live baked preview is shown in its place.
  auto* body_during = live_document.find_layer(body_id);
  CHECK(body_during != nullptr);
  CHECK(!body_during->visible());
  save_widget_artifact("ui_horror_did_you_know_live_edit", *canvas);

  // Escape the session: Photoshop's original pixels must come back byte-for-byte -- Escape is the
  // way to leave the imported render untouched.
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
  process_events_for(100);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  auto* body_after = live_document.find_layer(body_id);
  CHECK(body_after != nullptr);
  CHECK(body_after->visible() == body_was_visible);
  CHECK(body_after->pixels().width() == original_w);
  CHECK(body_after->pixels().height() == original_h);
  const std::vector<std::uint8_t> after_bytes(body_after->pixels().data().begin(),
                                              body_after->pixels().data().end());
  CHECK(after_bytes == original_bytes);

  // Re-enter and APPLY without a real edit (switching tools applies).  Applying keeps the live
  // render the session was showing -- the layer becomes a Patchy raster in place, instead of
  // snapping back to Photoshop's pixels.
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  accept_missing_psd_text_font_warning_if_present();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  process_events_for(300);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  process_events_for(100);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  auto* body_applied = live_document.find_layer(body_id);
  CHECK(body_applied != nullptr);
  if (body_applied == nullptr) {
    return;
  }
  CHECK(body_applied->visible() == body_was_visible);
  CHECK(body_applied->metadata().at(patchy::kLayerMetadataTextRasterStatus) == "patchy_raster");
  // The committed render stays on the original glyphs.
  CHECK(std::abs(body_applied->bounds().x - text_bounds.x) <= 24);
  CHECK(std::abs(body_applied->bounds().y - text_bounds.y) <= 24);
}

void ui_imported_psd_raster_preview_keeps_layer_fx_on_entry() {
  if (skip_without_arial_for_psd_text_preview()) {
    return;
  }
  patchy::Document document(340, 220, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(340, 220, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto pixels = solid_pixels(120, 70, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(20, 20, 70, 32), QColor(25, 25, 25, 255));

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Imported Styled", std::move(pixels));
  text_layer.set_bounds(patchy::Rect{90, 70, 120, 70});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Styled";
  text_layer.metadata()[patchy::kLayerMetadataTextFlow] = "point";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "34";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#191919";
  text_layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "psd_raster_preview";
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = patchy::BlendMode::Normal;
  stroke.color = patchy::RgbColor{230, 35, 45};
  stroke.opacity = 1.0F;
  stroke.size = 6.0F;
  stroke.position = patchy::LayerStrokePosition::Outside;
  text_layer.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(text_layer));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Imported PSD Styled Raster Preview"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  const QPoint stroke_sample(107, 100);
  CHECK(color_close(canvas_pixel(*canvas, stroke_sample), QColor(230, 35, 45), 70));

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(120, 94));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  process_events_for(150);
  // Point text now shows the live render from session entry (no waiting for the first
  // keystroke); the preview layer carries the source's layer style, so the stroke must still be
  // visible on the canvas the moment the edit opens.
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").isValid());
  CHECK(color_close(canvas_pixel(*canvas, stroke_sample), QColor(230, 35, 45), 70));

  QTextCursor cursor(editor->document());
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  editor->insertPlainText(QStringLiteral("!"));
  process_events_for(120);
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").isValid());

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
  // The name does not match the auto-derived form of the layer's text, so the commit keeps it.
  auto* text_item = require_layer_item(*layer_list, QStringLiteral("Text: Imported Point"));
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
  if (skip_without_arial_for_psd_text_preview()) {
    return;
  }
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
  auto* text_item = require_layer_item(*layer_list, QStringLiteral("Continue!"));
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
  if (skip_without_arial_for_psd_text_preview()) {
    return;
  }
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
  auto* text_item = require_layer_item(*layer_list, QStringLiteral("C2KYOTO SIMULATOR"));
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

}  // namespace

std::vector<patchy::test::TestCase> psd_text_import_tests() {
  return {
      {"ui_imported_psd_text_uses_photoshop_frame_after_commit",
       ui_imported_psd_text_uses_photoshop_frame_after_commit},
      {"ui_psd_point_text_edit_origin_stays_at_glyph_top_after_transform",
       ui_psd_point_text_edit_origin_stays_at_glyph_top_after_transform},
      {"ui_psd_point_text_edit_origin_survives_scale_transform_if_available",
       ui_psd_point_text_edit_origin_survives_scale_transform_if_available},
      {"ui_psd_point_text_transform_scales_crisply", ui_psd_point_text_transform_scales_crisply},
      {"ui_psd_point_text_installed_font_scales_and_edits_crisply",
       ui_psd_point_text_installed_font_scales_and_edits_crisply},
      {"ui_imported_psd_text_preview_preserves_paragraph_layout",
       ui_imported_psd_text_preview_preserves_paragraph_layout},
      {"ui_imported_psd_box_text_preview_uses_visual_bounds_after_edit",
       ui_imported_psd_box_text_preview_uses_visual_bounds_after_edit},
      {"ui_imported_psd_box_text_preview_preserves_descender_bleed_after_edit",
       ui_imported_psd_box_text_preview_preserves_descender_bleed_after_edit},
      {"ui_imported_psd_box_text_reedit_after_commit_preserves_descender_bleed",
       ui_imported_psd_box_text_reedit_after_commit_preserves_descender_bleed},
      {"ui_imported_psd_box_text_line_clip_renders_full_visible_line_after_edit",
       ui_imported_psd_box_text_line_clip_renders_full_visible_line_after_edit},
      {"ui_imported_psd_box_text_line_clip_hides_overflow_after_edit",
       ui_imported_psd_box_text_line_clip_hides_overflow_after_edit},
      {"ui_cdi_a4_title_text_import_edit_visual_bounds_if_available",
       ui_cdi_a4_title_text_import_edit_visual_bounds_if_available},
      {"ui_imported_psd_box_text_follows_markers_after_edit_if_available",
       ui_imported_psd_box_text_follows_markers_after_edit_if_available},
      {"ui_tips_psd_speed_mode_line_clip_if_available",
       ui_tips_psd_speed_mode_line_clip_if_available},
      {"ui_horror_virtualboy_caret_tracks_zoom_if_available",
       ui_horror_virtualboy_caret_tracks_zoom_if_available},
      {"ui_imported_psd_raster_point_text_renders_live_when_font_available_if_available",
       ui_imported_psd_raster_point_text_renders_live_when_font_available_if_available},
      {"ui_imported_psd_raster_preview_keeps_layer_fx_on_entry",
       ui_imported_psd_raster_preview_keeps_layer_fx_on_entry},
      {"ui_imported_psd_point_text_reedit_uses_auto_width",
       ui_imported_psd_point_text_reedit_uses_auto_width},
      {"ui_imported_psd_point_text_baseline_origin_converts_in_place",
       ui_imported_psd_point_text_baseline_origin_converts_in_place},
      {"ui_imported_psd_mirrored_point_text_uses_local_bounds",
       ui_imported_psd_mirrored_point_text_uses_local_bounds},
      {"ui_imported_psd_raster_preview_warns_before_missing_font_substitution",
       ui_imported_psd_raster_preview_warns_before_missing_font_substitution},
  };
}
