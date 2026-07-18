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
  const auto layer_count_before_click = layer_list->count();
  const QPoint text_document_point(100, 105);
  const auto text_widget_point = canvas->widget_position_for_document_point(text_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  // The layer row appears the moment the tool clicks (Photoshop behavior), before any commit.
  CHECK(layer_list->count() == layer_count_before_click + 1);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Type"));
  CHECK(layer_list->currentRow() == 0);
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
  editor->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  canvas->set_show_transform_controls(false);

  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Patchy Type"));
  CHECK(layer_list->count() == layer_count_before_click + 1);
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
  process_events_for(120);
  // Plain point text now edits through the live baked preview (render_text_pixels every keystroke) so
  // the glyphs match the committed layer's renderer -- no shift/antialiasing change on enter/leave edit.
  CHECK(reedit->property("patchy.previewPaintsText").toBool());
  CHECK(reedit->property("patchy.textPreviewLayerId").isValid());
  CHECK(reedit->toPlainText() == QStringLiteral("Patchy Type"));
  CHECK(!reedit->textCursor().hasSelection());
  CHECK(reedit->textCursor().position() <= 2);
  CHECK(reedit->pos() == text_widget_point);
  reedit->setPlainText(QStringLiteral("Canceled Type"));
  send_key(*reedit, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->count() == layer_count_before_click + 1);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Patchy Type"));

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
  CHECK(layer_list->count() == layer_count_before_click + 1);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Updated Type"));

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

void ui_text_editor_ctrl_b_and_ctrl_i_toggle_formatting() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* text_bold = window.findChild<QPushButton*>(QStringLiteral("textBoldButton"));
  auto* text_italic = window.findChild<QPushButton*>(QStringLiteral("textItalicButton"));
  CHECK(text_bold != nullptr);
  CHECK(text_italic != nullptr);

  text_bold->setChecked(false);
  text_italic->setChecked(false);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto text_widget_point = canvas->widget_position_for_document_point(QPoint(100, 105));
  send_mouse(*canvas, QEvent::MouseButtonPress, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  QPointer<QTextEdit> editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setFocus(Qt::OtherFocusReason);
  editor->selectAll();
  QTest::keyClick(editor.data(), Qt::Key_B, Qt::ControlModifier);
  QTest::keyClick(editor.data(), Qt::Key_I, Qt::ControlModifier);
  QApplication::processEvents();

  const bool editor_stayed_open = editor != nullptr &&
                                  canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == editor;
  const auto format = editor_stayed_open ? editor->textCursor().charFormat() : QTextCharFormat{};
  const bool bold_checked = text_bold->isChecked();
  const bool italic_checked = text_italic->isChecked();
  if (editor != nullptr) {
    send_key(*editor, Qt::Key_Escape);
  }
  QApplication::processEvents();

  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(editor_stayed_open);
  CHECK(bold_checked);
  CHECK(italic_checked);
  CHECK(format.font().bold());
  CHECK(format.font().italic());
}

void ui_text_tool_outside_click_commits_without_new_text_editor() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  auto* type_action = require_action_by_text(window, QStringLiteral("Type"));
  type_action->trigger();
  const auto layer_count_before_click = layer_list->count();
  const QPoint text_document_point(90, 90);
  const auto text_widget_point = canvas->widget_position_for_document_point(text_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(layer_list->count() == layer_count_before_click + 1);
  editor->setPlainText(QStringLiteral("Outside Commit"));
  QApplication::processEvents();

  const auto outside_widget_point = canvas->widget_position_for_document_point(QPoint(310, 220));
  send_mouse(*canvas, QEvent::MouseButtonPress, outside_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, outside_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(type_action->isChecked());
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->count() == layer_count_before_click + 1);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Outside Commit"));
  save_widget_artifact("ui_text_outside_click_commit", window);
}

void ui_delete_key_action_removes_text_layer_object() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_count_before = layer_list->count();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const QPoint text_document_point(90, 90);
  const auto text_widget_point = canvas->widget_position_for_document_point(text_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("Delete Me"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  canvas->set_show_transform_controls(false);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->count() == layer_count_before + 1);
  const auto text_layer_id = document.active_layer_id();
  CHECK(text_layer_id.has_value());
  const auto* text_layer = document.find_layer(*text_layer_id);
  CHECK(text_layer != nullptr);
  CHECK(patchy::layer_is_text(*text_layer));

  // While a text edit is in progress the clear action leaves the layer alone;
  // Delete belongs to typing.
  send_mouse(*canvas, QEvent::MouseButtonDblClick, text_widget_point + QPoint(12, 12), Qt::LeftButton,
             Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr);
  require_action(window, "layerClearAction")->trigger();
  QApplication::processEvents();
  auto* reedit = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(reedit != nullptr);
  CHECK(document.find_layer(*text_layer_id) != nullptr);
  send_key(*reedit, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  // With a marquee selection the clear action refuses instead of erasing the
  // glyph pixels out from under the still-live text object.
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 70)),
       canvas->widget_position_for_document_point(QPoint(230, 170)));
  CHECK(canvas->has_selection());
  require_action(window, "layerClearAction")->trigger();
  QApplication::processEvents();
  CHECK(document.find_layer(*text_layer_id) != nullptr);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Deselect")));
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());

  // Delete on the committed text object removes the whole layer, Photoshop-style.
  require_action(window, "layerClearAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layer_count_before);
  CHECK(document.find_layer(*text_layer_id) == nullptr);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Deleted layer")));

  // The text tool finds nothing left there: a click starts a fresh empty editor
  // instead of resurrecting the deleted text.
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  send_mouse(*canvas, QEvent::MouseButtonPress, text_widget_point + QPoint(12, 12), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_widget_point + QPoint(12, 12), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* fresh = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(fresh != nullptr);
  CHECK(!fresh->property("patchy.editingLayerId").isValid());
  CHECK(fresh->toPlainText() != QStringLiteral("Delete Me"));
  send_key(*fresh, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  // The deletion is a single undoable step.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layer_count_before + 1);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Delete Me"));
}

void ui_text_tool_click_creates_provisional_layer() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* undo_action = require_action_by_text(window, QStringLiteral("Undo"));
  const auto layer_count_before = layer_list->count();
  const auto active_before = document.active_layer_id();
  CHECK(active_before.has_value());
  CHECK(!undo_action->isEnabled());

  // Clicking with the Type tool shows the new layer immediately (Photoshop behavior): the row
  // appears named after the placeholder and selected, before anything is typed or committed.
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const QPoint text_document_point(80, 80);
  const auto text_widget_point = canvas->widget_position_for_document_point(text_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(layer_list->count() == layer_count_before + 1);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Type"));
  CHECK(layer_list->currentRow() == 0);
  CHECK(document.active_layer_id().has_value());
  CHECK(document.active_layer_id() != active_before);

  // Escape removes the just-created layer, restores the previously active layer, and leaves
  // history untouched: no phantom undo step.
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->count() == layer_count_before);
  CHECK(document.active_layer_id() == active_before);
  CHECK(!undo_action->isEnabled());

  // An empty commit (text cleared, then committing by switching tools) removes it too.
  send_mouse(*canvas, QEvent::MouseButtonPress, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(layer_list->count() == layer_count_before + 1);
  editor->selectAll();
  send_key(*editor, Qt::Key_Backspace);
  QApplication::processEvents();
  CHECK(editor->toPlainText().isEmpty());
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  canvas->set_show_transform_controls(false);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->count() == layer_count_before);
  CHECK(document.active_layer_id() == active_before);
  CHECK(!undo_action->isEnabled());

  // A real commit keeps the layer (renamed to the text), under the same id the row has had
  // since the click, and is ONE undo step back to the pre-click document.
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  send_mouse(*canvas, QEvent::MouseButtonPress, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  const auto provisional_id = document.active_layer_id();
  CHECK(provisional_id.has_value());
  editor->setPlainText(QStringLiteral("Immediate Layer"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->count() == layer_count_before + 1);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Immediate Layer"));
  CHECK(document.active_layer_id() == provisional_id);
  CHECK(undo_action->isEnabled());
  undo_action->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layer_count_before);
  CHECK(document.find_layer(*provisional_id) == nullptr);
  CHECK(!undo_action->isEnabled());
}

void ui_text_options_bar_accept_cancel_buttons() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* undo_action = require_action_by_text(window, QStringLiteral("Undo"));
  auto* apply_button = window.findChild<QPushButton*>(QStringLiteral("textApplyButton"));
  auto* cancel_button = window.findChild<QPushButton*>(QStringLiteral("textCancelButton"));
  auto* font_combo = window.findChild<QFontComboBox*>(QStringLiteral("textFontCombo"));
  CHECK(apply_button != nullptr);
  CHECK(cancel_button != nullptr);
  CHECK(font_combo != nullptr);
  // NoFocus is load-bearing: a focus-taking button would fire the editor's
  // focus-loss auto-commit on mouse press, so Cancel would commit instead.
  CHECK(apply_button->focusPolicy() == Qt::NoFocus);
  CHECK(cancel_button->focusPolicy() == Qt::NoFocus);
  // All session apply/cancel buttons (text and transform) render 20px icons; the
  // QPushButton default of 16px read tiny on the bar.
  CHECK(apply_button->iconSize() == QSize(20, 20));
  CHECK(cancel_button->iconSize() == QSize(20, 20));
  CHECK(window.findChild<QPushButton*>(QStringLiteral("freeTransformApplyButton"))->iconSize() == QSize(20, 20));
  CHECK(window.findChild<QPushButton*>(QStringLiteral("freeTransformCancelButton"))->iconSize() == QSize(20, 20));
  CHECK(!apply_button->isVisible());
  CHECK(!cancel_button->isVisible());
  const auto layer_count_before = layer_list->count();

  // Selecting the Type tool alone shows the text controls but not the session pair.
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  CHECK(font_combo->isVisible());
  CHECK(!apply_button->isVisible());
  CHECK(!cancel_button->isVisible());

  // Starting an edit session shows apply/cancel while keeping the text controls
  // visible (unlike a transform session, they apply live to the editor).
  const auto commit_widget_point = canvas->widget_position_for_document_point(QPoint(80, 80));
  send_mouse(*canvas, QEvent::MouseButtonPress, commit_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, commit_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  // A failing CHECK while the editor is alive aborts the suite during unwind, so
  // capture the live-session state into locals and assert after the session ends.
  const bool session_showed_buttons = apply_button != nullptr && apply_button->isVisible() &&
                                      cancel_button != nullptr && cancel_button->isVisible();
  const bool session_enabled_buttons = apply_button != nullptr && apply_button->isEnabled() &&
                                       cancel_button != nullptr && cancel_button->isEnabled();
  const bool session_kept_text_controls = font_combo != nullptr && font_combo->isVisible();
  if (editor != nullptr) {
    editor->setPlainText(QStringLiteral("Button Commit"));
    QApplication::processEvents();
    apply_button->click();
    QApplication::processEvents();
  }
  CHECK(editor != nullptr);
  CHECK(session_showed_buttons);
  CHECK(session_enabled_buttons);
  CHECK(session_kept_text_controls);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->count() == layer_count_before + 1);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Button Commit"));
  CHECK(undo_action->isEnabled());
  CHECK(!apply_button->isVisible());
  CHECK(!cancel_button->isVisible());

  // Cancel on a NEW session discards the provisional layer and adds no history:
  // the commit above stays the only undo step.
  const auto cancel_widget_point = canvas->widget_position_for_document_point(QPoint(200, 400));
  send_mouse(*canvas, QEvent::MouseButtonPress, cancel_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, cancel_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  const bool cancel_session_showed_buttons = apply_button->isVisible() && cancel_button->isVisible();
  if (editor != nullptr) {
    editor->setPlainText(QStringLiteral("Discard Me"));
    QApplication::processEvents();
    cancel_button->click();
    QApplication::processEvents();
  }
  CHECK(editor != nullptr);
  CHECK(cancel_session_showed_buttons);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->count() == layer_count_before + 1);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Button Commit"));
  CHECK(!apply_button->isVisible());
  CHECK(!cancel_button->isVisible());
  undo_action->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layer_count_before);
  CHECK(!undo_action->isEnabled());
}

QWidget* find_font_picker_popup() {
  QWidget* popup = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("textFontPickerPopup") && widget->isVisible()) {
      popup = widget;
    }
  }
  return popup;
}

void ui_text_font_picker_popup_filters_and_commits() {
  register_test_fonts(TestFontRole::Verdana);
  if (!QFontDatabase::families().contains(QStringLiteral("Verdana"))) {
    std::cout << "[SKIP] Verdana unavailable on this machine\n";
    return;
  }
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  // The picker must still BE a QFontComboBox named textFontCombo: all existing wiring and
  // tests find it that way.
  auto* combo = window.findChild<QFontComboBox*>(QStringLiteral("textFontCombo"));
  CHECK(combo != nullptr);
  auto* picker = qobject_cast<patchy::ui::FontPickerCombo*>(combo);
  CHECK(picker != nullptr);

  int font_changes = 0;
  QObject::connect(picker, &QFontComboBox::currentFontChanged, picker,
                   [&font_changes](const QFont&) { ++font_changes; });

  picker->showPopup();
  QApplication::processEvents();
  auto* popup = find_font_picker_popup();
  CHECK(popup != nullptr);
  auto* search = popup->findChild<QLineEdit*>(QStringLiteral("textFontPickerSearchEdit"));
  auto* list = popup->findChild<QListView*>(QStringLiteral("textFontPickerList"));
  CHECK(search != nullptr);
  CHECK(list != nullptr);
  const auto unfiltered_rows = list->model()->rowCount();

  QTest::keyClicks(search, QStringLiteral("erda"));
  QApplication::processEvents();
  auto* model = list->model();
  CHECK(model->rowCount() >= 1);
  CHECK(model->rowCount() < unfiltered_rows);  // substring filter actually narrowed the list
  int verdana_row = -1;
  for (int row = 0; row < model->rowCount(); ++row) {
    const auto family = model->index(row, 0).data(Qt::DisplayRole).toString();
    CHECK(family.contains(QStringLiteral("erda"), Qt::CaseInsensitive));
    if (family == QStringLiteral("Verdana")) {
      verdana_row = row;
    }
  }
  CHECK(verdana_row >= 0);
  list->setCurrentIndex(model->index(verdana_row, 0));
  QApplication::processEvents();
  QTest::keyClick(search, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(find_font_picker_popup() == nullptr);
  CHECK(picker->currentFont().family() == QStringLiteral("Verdana"));
  CHECK(font_changes == 1);
}

void ui_text_font_picker_rows_render_in_their_own_font() {
  register_test_fonts(TestFontRole::Verdana);
  register_test_fonts(TestFontRole::Wingdings);
  patchy::ui::MainWindow window;
  show_window(window);
  auto* picker = window.findChild<patchy::ui::FontPickerCombo*>(QStringLiteral("textFontCombo"));
  CHECK(picker != nullptr);

  if (QFontDatabase::families().contains(QStringLiteral("Verdana"))) {
    const auto info = picker->family_render_info(QStringLiteral("Verdana"));
    CHECK(info.latin_capable);
    CHECK(info.display_font.family() == QStringLiteral("Verdana"));  // row renders in its own face
    CHECK(info.systems.contains(QFontDatabase::Latin));
    CHECK(info.row_sample.isEmpty());
  } else {
    std::cout << "[SKIP] Verdana unavailable; latin-row checks skipped\n";
  }
  if (QFontDatabase::families().contains(QStringLiteral("Wingdings"))) {
    const auto info = picker->family_render_info(QStringLiteral("Wingdings"));
    CHECK(!info.latin_capable);
    // A symbol face cannot draw its own Latin name: the row keeps the UI font for the name
    // and shows a short run of the family's actual glyphs beside it.
    CHECK(info.display_font.family() != QStringLiteral("Wingdings"));
    CHECK(info.systems.contains(QFontDatabase::Symbol));
    CHECK(!info.row_sample.isEmpty());
    CHECK(info.row_sample.size() <= 9);  // decoration, not a wall of dingbats
  } else {
    std::cout << "[SKIP] Wingdings unavailable; symbol-row checks skipped\n";
  }
}

void ui_text_font_picker_preview_shows_supported_scripts() {
  register_test_fonts(TestFontRole::UiDefault);
  register_test_fonts(TestFontRole::Verdana);
  register_test_fonts(TestFontRole::JapaneseGothic);
  const auto japanese_families = QFontDatabase::families(QFontDatabase::Japanese);
  if (japanese_families.isEmpty()) {
    std::cout << "[SKIP] no Japanese-capable font available on this machine\n";
    return;
  }
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  auto* picker = window.findChild<patchy::ui::FontPickerCombo*>(QStringLiteral("textFontCombo"));
  CHECK(picker != nullptr);
  picker->showPopup();
  QApplication::processEvents();
  auto* popup = find_font_picker_popup();
  CHECK(popup != nullptr);
  auto* list = popup->findChild<QListView*>(QStringLiteral("textFontPickerList"));
  auto* preview = popup->findChild<patchy::ui::FontPreviewPane*>(QStringLiteral("textFontPickerPreview"));
  CHECK(list != nullptr);
  CHECK(preview != nullptr);

  const auto select_family = [list](const QString& family) {
    auto* model = list->model();
    for (int row = 0; row < model->rowCount(); ++row) {
      if (model->index(row, 0).data(Qt::DisplayRole).toString() == family) {
        // Keyboard/selection navigation drives the same preview update as mouse hover.
        list->setCurrentIndex(model->index(row, 0));
        QApplication::processEvents();
        return true;
      }
    }
    return false;
  };

  const auto& japanese_family = japanese_families.front();
  CHECK(select_family(japanese_family));
  CHECK(preview->family() == japanese_family);
  bool has_japanese_line = false;
  for (const auto& text : preview->line_texts()) {
    for (const auto& ch : text) {
      if (ch.unicode() >= 0x3040 && ch.unicode() <= 0x30FF) {  // hiragana/katakana
        has_japanese_line = true;
        break;
      }
    }
  }
  CHECK(has_japanese_line);
  save_widget_artifact("font_picker_preview_japanese", *popup);

  if (QFontDatabase::families().contains(QStringLiteral("Verdana"))) {
    CHECK(select_family(QStringLiteral("Verdana")));
    CHECK(preview->family() == QStringLiteral("Verdana"));
    const auto lines = preview->line_texts();
    CHECK(lines.size() >= 2);
    CHECK(lines.front() == QStringLiteral("Verdana"));  // type-specimen name line leads
    bool has_pangram = false;
    for (const auto& text : lines) {
      if (text.contains(QStringLiteral("quick brown fox"))) {
        has_pangram = true;
      }
    }
    CHECK(has_pangram);
    // Minor European scripts collapse into the "Also supports" footer instead of rendering
    // cryptic sample lines. Expectation comes from what THIS build's font database claims
    // for Verdana (FreeType and DirectWrite report different sets for the same file).
    const auto verdana_systems = QFontDatabase::writingSystems(QStringLiteral("Verdana"));
    QStringList expected_footer_names;
    for (const auto ws : {QFontDatabase::Greek, QFontDatabase::Cyrillic, QFontDatabase::Armenian,
                          QFontDatabase::Vietnamese}) {
      if (verdana_systems.contains(ws)) {
        expected_footer_names.append(QFontDatabase::writingSystemName(ws));
      }
    }
    if (!expected_footer_names.isEmpty()) {
      CHECK(!preview->footer_text().isEmpty());
      CHECK(preview->footer_text().contains(expected_footer_names.front()));
    }
  }
  popup->close();
  QApplication::processEvents();
}

void ui_text_font_picker_popup_resizes_and_persists() {
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("ui/textFontPickerPopupSize"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  auto* picker = window.findChild<patchy::ui::FontPickerCombo*>(QStringLiteral("textFontCombo"));
  CHECK(picker != nullptr);

  picker->showPopup();
  QApplication::processEvents();
  auto* popup = find_font_picker_popup();
  CHECK(popup != nullptr);
  CHECK(popup->findChild<QSizeGrip*>() != nullptr);  // the resize handle
  CHECK(popup->width() >= 360);                      // first-open default browse size
  CHECK(popup->height() >= 520);

  popup->resize(500, 600);
  QApplication::processEvents();
  popup->close();
  QApplication::processEvents();
  CHECK(patchy::ui::app_settings().value(QStringLiteral("ui/textFontPickerPopupSize")).toSize() ==
        QSize(500, 600));

  // Programmatic reopen: the dismiss-toggle guard only arms when the pointer is over the combo.
  picker->showPopup();
  QApplication::processEvents();
  auto* reopened = find_font_picker_popup();
  CHECK(reopened != nullptr);
  CHECK(reopened->size() == QSize(500, 600));
  reopened->close();
  QApplication::processEvents();
}

void ui_text_font_picker_open_while_editing_keeps_text_session() {
  register_test_fonts(TestFontRole::UiDefault);
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  auto* picker = window.findChild<patchy::ui::FontPickerCombo*>(QStringLiteral("textFontCombo"));
  const bool picker_found = picker != nullptr;

  const auto editor_point = canvas->widget_position_for_document_point(QPoint(80, 80));
  send_mouse(*canvas, QEvent::MouseButtonPress, editor_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, editor_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  // A failing CHECK while the editor is alive aborts the suite during unwind, so capture the
  // live-session state into locals and assert after the session ends.
  const bool editor_opened = editor != nullptr;
  bool popup_seen = false;
  bool search_focused = false;
  bool editor_survived_popup_focus = false;
  if (editor_opened && picker_found) {
    editor->setPlainText(QStringLiteral("Keep Me"));
    QApplication::processEvents();
    picker->showPopup();  // moves focus into the popup's search box
    QApplication::processEvents();
    auto* popup = find_font_picker_popup();
    popup_seen = popup != nullptr;
    if (popup_seen) {
      auto* search = popup->findChild<QLineEdit*>(QStringLiteral("textFontPickerSearchEdit"));
      if (search != nullptr) {
        search->setFocus();
        QApplication::processEvents();
        search_focused = QApplication::focusWidget() == search;
      }
      // The popup is a Qt::Popup window, so is_text_option_widget must match it by name;
      // without that, the focus change above auto-commits and closes the session.
      editor_survived_popup_focus =
          canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == editor;
      popup->close();
      QApplication::processEvents();
    }
    auto* cancel_button = window.findChild<QPushButton*>(QStringLiteral("textCancelButton"));
    if (cancel_button != nullptr && cancel_button->isVisible()) {
      cancel_button->click();
    } else {
      require_action_by_text(window, QStringLiteral("Move"))->trigger();  // ends any live session
    }
    QApplication::processEvents();
  }
  CHECK(picker_found);
  CHECK(editor_opened);
  CHECK(popup_seen);
  CHECK(search_focused);
  CHECK(editor_survived_popup_focus);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
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
  // Directly-constructed Document: core default 300 ppi, not the startup doc's 72.
  text_size->setValue(text_points_for_pixels(52, 300.0));
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
  // The box drag also creates its layer row immediately, like a point-text click.
  CHECK(layer_list->count() == layer_count_before_cancel_drag + 1);
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
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->count() == layer_count_before_cancel_drag + 1);

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

void ui_text_size_popup_slider_caps_at_200pt() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* text_size = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  CHECK(text_size != nullptr);
  CHECK(text_size->maximum() == 10000.0);

  const auto open_popup = [&window]() -> QSlider* {
    auto* action = window.findChild<QAction*>(QStringLiteral("textSizePopupAction"));
    CHECK(action != nullptr);
    action->trigger();
    QApplication::processEvents();
    auto* popup = window.findChild<QFrame*>(QStringLiteral("textSizePopup"));
    auto* slider = window.findChild<QSlider*>(QStringLiteral("textSizePopupSlider"));
    CHECK(popup != nullptr && popup->isVisible());
    CHECK(slider != nullptr);
    return slider;
  };
  const auto close_popup = [&window] {
    auto* popup = window.findChild<QFrame*>(QStringLiteral("textSizePopup"));
    CHECK(popup != nullptr);
    popup->close();
    QApplication::processEvents();
  };

  // The spin box accepts up to 10000 pt typed, but the slider caps at 200 pt
  // (decimals=3, so slider units are thousandths of a point).
  const double value_before = text_size->value();
  auto* slider = open_popup();
  CHECK(slider->maximum() == 200000);
  CHECK(text_size->value() == value_before);
  slider->setValue(150000);
  CHECK(text_size->value() == 150.0);
  close_popup();

  // A typed value above the cap extends the slider to reach it instead of
  // clamping the value down when the popup opens.
  text_size->setValue(500.0);
  slider = open_popup();
  CHECK(slider->maximum() == 500000);
  CHECK(slider->value() == 500000);
  CHECK(text_size->value() == 500.0);
  close_popup();

  text_size->setValue(48.0);
}

}  // namespace

std::vector<patchy::test::TestCase> text_editor_font_picker_tests() {
  return {
      {"ui_text_tool_creates_visible_text_layer", ui_text_tool_creates_visible_text_layer},
      {"ui_text_editor_ctrl_b_and_ctrl_i_toggle_formatting",
       ui_text_editor_ctrl_b_and_ctrl_i_toggle_formatting},
      {"ui_text_tool_outside_click_commits_without_new_text_editor",
       ui_text_tool_outside_click_commits_without_new_text_editor},
      {"ui_delete_key_action_removes_text_layer_object", ui_delete_key_action_removes_text_layer_object},
      {"ui_text_tool_click_creates_provisional_layer", ui_text_tool_click_creates_provisional_layer},
      {"ui_text_options_bar_accept_cancel_buttons", ui_text_options_bar_accept_cancel_buttons},
      {"ui_text_font_picker_popup_filters_and_commits", ui_text_font_picker_popup_filters_and_commits},
      {"ui_text_font_picker_rows_render_in_their_own_font", ui_text_font_picker_rows_render_in_their_own_font},
      {"ui_text_font_picker_preview_shows_supported_scripts", ui_text_font_picker_preview_shows_supported_scripts},
      {"ui_text_font_picker_popup_resizes_and_persists", ui_text_font_picker_popup_resizes_and_persists},
      {"ui_text_font_picker_open_while_editing_keeps_text_session",
       ui_text_font_picker_open_while_editing_keeps_text_session},
      {"ui_text_edit_hides_editor_glyphs_and_shows_selection_over_style_preview",
       ui_text_edit_hides_editor_glyphs_and_shows_selection_over_style_preview},
      {"ui_expensive_text_style_preview_debounces_to_plain_live_text",
       ui_expensive_text_style_preview_debounces_to_plain_live_text},
      {"ui_text_editor_paste_uses_current_format_for_rich_emoji_clipboard",
       ui_text_editor_paste_uses_current_format_for_rich_emoji_clipboard},
      {"ui_text_tool_drag_creates_resizable_wrapped_text_box",
       ui_text_tool_drag_creates_resizable_wrapped_text_box},
      {"ui_text_size_popup_slider_caps_at_200pt", ui_text_size_popup_slider_caps_at_200pt},
  };
}
