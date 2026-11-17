// The contour editor, the contour library and its Photoshop .shc files, the
// Layer Style dialog's contour tools and style preview swatch, and the Contour
// Manager (GitHub issue 726 and its follow-up).
#include "core/contour_editing.hpp"
#include "core/contour_presets.hpp"
#include "core/document.hpp"
#include "core/style_contour.hpp"
#include "psd/shc_io.hpp"
#include "ui/contour_editor_dialog.hpp"
#include "ui/contour_library.hpp"
#include "ui/contour_manager_dialog.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/preset_grid_widget.hpp"
#include "ui/qt_paths.hpp"
#include "ui/style_library.hpp"

#include "test_harness.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace patchy::test::ui;

patchy::StyleContourPoint contour_point(float x, float y, bool corner = false) {
  patchy::StyleContourPoint point;
  point.x = x;
  point.y = y;
  point.corner = corner;
  return point;
}

patchy::StyleContour ridge_contour(std::string name) {
  patchy::StyleContour contour;
  contour.name = std::move(name);
  contour.points = {contour_point(0.0F, 12.0F, true), contour_point(40.0F, 200.0F, true),
                    contour_point(100.0F, 30.0F, false), contour_point(180.0F, 240.0F, true),
                    contour_point(250.0F, 128.0F, false)};
  return contour;
}

QPoint graph_position(const patchy::ui::ContourGraphWidget& graph, double input, double output) {
  return graph.position_for_levels(input, output).toPoint();
}

void click_graph(patchy::ui::ContourGraphWidget& graph, double input, double output) {
  const auto position = graph_position(graph, input, output);
  send_mouse(graph, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
  send_mouse(graph, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
}

patchy::ui::ContourGraphWidget* editor_graph(QDialog& dialog) {
  // The graph has no Q_OBJECT of its own, so it is found as a QWidget by its unique name.
  return static_cast<patchy::ui::ContourGraphWidget*>(dialog.findChild<QWidget*>(QStringLiteral("contourEditorGraph")));
}

// A layer with a bevel and a drop shadow, the two effects that carry contours.
patchy::Layer contour_test_layer(patchy::Document& document, const patchy::StyleContour& gloss = {}) {
  patchy::Layer layer(document.allocate_layer_id(), "Styled",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.size = 6.0F;
  bevel.gloss_contour = gloss;
  layer.layer_style().bevels.push_back(bevel);
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  layer.layer_style().drop_shadows.push_back(shadow);
  return layer;
}

void select_category(QDialog& dialog, const QString& text) {
  auto* categories = dialog.findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
  CHECK(categories != nullptr);
  const auto items = categories->findItems(text, Qt::MatchExactly);
  CHECK(!items.empty());
  categories->setCurrentItem(items.front());
  QApplication::processEvents();
}

void answer_name(const QString& name) {
  when_dialog_visible(QStringLiteral("layerStyleContourNameDialog"), [name](QDialog& dialog) {
    auto* input = dialog.findChild<QLineEdit*>(QStringLiteral("contourNameEdit"));
    CHECK(input != nullptr);
    if (input != nullptr) {
      input->setText(name);
    }
    dialog.accept();
  });
}

// The curve the editor tests draw: a corner peak and a smooth dip.
void draw_peak_and_dip(QDialog& editor) {
  auto* graph = editor_graph(editor);
  auto* corner = editor.findChild<QCheckBox*>(QStringLiteral("contourEditorCornerCheck"));
  CHECK(graph != nullptr && corner != nullptr);
  click_graph(*graph, 64.0, 220.0);
  corner->click();
  click_graph(*graph, 150.0, 40.0);
  CHECK(graph->contour().points.size() == 4U);
}

// The curve draw_peak_and_dip makes in an editor that opened on a straight line.
patchy::StyleContour peak_and_dip_contour() {
  patchy::StyleContour contour;
  contour.points = {contour_point(0.0F, 0.0F), contour_point(64.0F, 220.0F, true), contour_point(150.0F, 40.0F),
                    contour_point(255.0F, 255.0F)};
  return contour;
}

// Answers the New Contour prompt with `name`, then checks the editor opens on a straight
// line titled for the new contour and runs `drive` in it.
void answer_new_contour(const QString& name, std::function<void(QDialog&)> drive) {
  answer_name(name);
  when_dialog_visible(QStringLiteral("contourEditorDialog"), [name, drive = std::move(drive)](QDialog& editor) {
    auto* graph = editor_graph(editor);
    CHECK(graph != nullptr);
    CHECK(editor.windowTitle() == QStringLiteral("New Contour: %1").arg(name));
    // It starts from a straight line, and has no second way to save.
    CHECK(graph != nullptr && graph->contour().points.size() == 2U);
    CHECK(graph != nullptr && patchy::style_contour_is_linear(graph->contour()));
    CHECK(editor.findChild<QPushButton*>(QStringLiteral("contourEditorSaveAsNewButton")) == nullptr);
    drive(editor);
  });
}

// The open grid of a contour picker, or nullptr.
patchy::ui::PresetGridWidget* contour_grid(QComboBox& combo) {
  QWidget* popup = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == combo.objectName() + QStringLiteral("Popup") && widget->isVisible()) {
      popup = widget;
    }
  }
  if (popup == nullptr) {
    return nullptr;
  }
  return static_cast<patchy::ui::PresetGridWidget*>(
      popup->findChild<QListWidget*>(combo.objectName() + QStringLiteral("Grid")));
}

QTreeWidgetItem* manager_row(QTreeWidget& tree, const QString& name) {
  const auto items = tree.findItems(name, Qt::MatchExactly, 0);
  return items.isEmpty() ? nullptr : items.front();
}

void answer_file_when_visible(const QString& object_name, const QString& path) {
  when_dialog_visible(object_name, [path](QDialog& dialog) {
    auto* files = qobject_cast<QFileDialog*>(&dialog);
    CHECK(files != nullptr);
    // selectFile leaves a focused name field alone, so the name is typed as well.
    files->selectFile(path);
    if (auto* name_edit = files->findChild<QLineEdit*>(QStringLiteral("fileNameEdit")); name_edit != nullptr) {
      name_edit->setText(path);
    }
    QApplication::processEvents();
    dialog.accept();
    if (dialog.isVisible()) {
      CHECK(false);  // the dialog refused the name
      dialog.reject();
    }
  });
}

QAction* library_action(QDialog& dialog, const QString& combo_name, const QString& action_name) {
  auto* menu = dialog.findChild<QMenu*>(combo_name + QStringLiteral("LibraryMenu"));
  CHECK(menu != nullptr);
  // The menu ticks its entries on and off when it opens.
  menu->aboutToShow();
  auto* action = menu->findChild<QAction*>(action_name);
  CHECK(action != nullptr);
  return action;
}

void ui_contour_editor_adds_moves_deletes_and_toggles_corners() {
  int changes = 0;
  patchy::StyleContour last_change;
  when_dialog_visible(QStringLiteral("contourEditorDialog"), [&](QDialog& dialog) {
    auto* graph = editor_graph(dialog);
    auto* input = dialog.findChild<QSpinBox*>(QStringLiteral("contourEditorInputSpin"));
    auto* output = dialog.findChild<QSpinBox*>(QStringLiteral("contourEditorOutputSpin"));
    auto* corner = dialog.findChild<QCheckBox*>(QStringLiteral("contourEditorCornerCheck"));
    auto* delete_point = dialog.findChild<QPushButton*>(QStringLiteral("contourEditorDeletePointButton"));
    auto* reset = dialog.findChild<QPushButton*>(QStringLiteral("contourEditorResetButton"));
    CHECK(graph != nullptr && input != nullptr && output != nullptr && corner != nullptr);
    CHECK(delete_point != nullptr && reset != nullptr);
    // No library was handed in, so there is nothing to save a new contour into.
    CHECK(dialog.findChild<QPushButton*>(QStringLiteral("contourEditorSaveAsNewButton")) == nullptr);
    const auto& points = graph->contour().points;
    CHECK(points.size() == 2U);
    CHECK(!delete_point->isEnabled());  // two points are the minimum

    // A click on empty graph adds a point there and selects it.
    click_graph(*graph, 128.0, 30.0);
    CHECK(points.size() == 3U);
    CHECK(graph->selected_point() == 1);
    CHECK(points[1] == contour_point(128.0F, 30.0F));
    CHECK(input->value() == 128 && output->value() == 30 && !corner->isChecked());
    CHECK(delete_point->isEnabled());
    CHECK(changes == 1);
    CHECK(last_change.name == "Custom" && last_change.points.size() == 3U);

    // Dragging moves it.
    send_mouse(*graph, QEvent::MouseButtonPress, graph_position(*graph, 128.0, 30.0), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*graph, QEvent::MouseMove, graph_position(*graph, 110.0, 120.0), Qt::NoButton, Qt::LeftButton);
    send_mouse(*graph, QEvent::MouseMove, graph_position(*graph, 100.0, 200.0), Qt::NoButton, Qt::LeftButton);
    send_mouse(*graph, QEvent::MouseButtonRelease, graph_position(*graph, 100.0, 200.0), Qt::LeftButton, Qt::NoButton);
    CHECK(points.size() == 3U && points[1] == contour_point(100.0F, 200.0F));
    CHECK(input->value() == 100 && output->value() == 200);

    // Corner, and the Input and Output fields.
    corner->click();
    CHECK(points[1].corner);
    output->setValue(180);
    CHECK(points[1] == contour_point(100.0F, 180.0F, true));
    input->setValue(0);  // held one level clear of the first point
    CHECK(points[1].x == 1.0F && input->value() == 1);
    input->setValue(100);
    CHECK(points[1].x == 100.0F);

    // Arrow keys nudge the selected point; Shift steps by ten.
    graph->setFocus();
    QTest::keyClick(graph, Qt::Key_Right);
    QTest::keyClick(graph, Qt::Key_Up, Qt::ShiftModifier);
    CHECK(points[1] == contour_point(101.0F, 190.0F, true));
    QTest::keyClick(graph, Qt::Key_Left);
    QTest::keyClick(graph, Qt::Key_Down, Qt::ShiftModifier);
    CHECK(points[1] == contour_point(100.0F, 180.0F, true));

    // A second point, removed again with Delete.
    click_graph(*graph, 200.0, 60.0);
    CHECK(points.size() == 4U && graph->selected_point() == 2);
    QTest::keyClick(graph, Qt::Key_Delete);
    CHECK(points.size() == 3U);

    // A click on a point selects it without changing the curve.
    const auto before_select = changes;
    click_graph(*graph, 100.0, 180.0);
    CHECK(graph->selected_point() == 1 && changes == before_select);
    CHECK(corner->isChecked() && input->value() == 100);

    // Dragging a point off the graph removes it; back inside before the release it returns.
    const QPoint outside(graph->width() / 2, graph->height() + 80);
    send_mouse(*graph, QEvent::MouseButtonPress, graph_position(*graph, 100.0, 180.0), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*graph, QEvent::MouseMove, outside, Qt::NoButton, Qt::LeftButton);
    CHECK(points.size() == 2U);
    send_mouse(*graph, QEvent::MouseMove, graph_position(*graph, 90.0, 150.0), Qt::NoButton, Qt::LeftButton);
    CHECK(points.size() == 3U && points[1] == contour_point(90.0F, 150.0F, true));
    send_mouse(*graph, QEvent::MouseMove, outside, Qt::NoButton, Qt::LeftButton);
    send_mouse(*graph, QEvent::MouseButtonRelease, outside, Qt::LeftButton, Qt::NoButton);
    CHECK(points.size() == 2U);
    CHECK(!delete_point->isEnabled());
    QTest::keyClick(graph, Qt::Key_Delete);  // refused at two points
    CHECK(points.size() == 2U);
    CHECK(last_change.name == "Linear");  // back on the curve the dialog opened with

    // Reset puts the opening curve back.
    click_graph(*graph, 60.0, 240.0);
    CHECK(points.size() == 3U && last_change.name == "Custom");
    reset->click();
    CHECK(points.size() == 2U && last_change.name == "Linear");

    // The curve this test accepts: a corner peak and a smooth dip.
    click_graph(*graph, 64.0, 220.0);
    corner->click();
    click_graph(*graph, 150.0, 40.0);
    CHECK(points.size() == 4U);
    QApplication::processEvents();
    (void)capture_widget_artifact("ui_contour_editor_custom_curve", dialog);
    dialog.accept();
  });
  const auto edited = patchy::ui::request_contour_edit(nullptr, patchy::StyleContour{},
                                                        [&](const patchy::StyleContour& value) {
                                                          ++changes;
                                                          last_change = value;
                                                        });
  CHECK(edited.has_value());
  if (!edited.has_value()) {
    return;
  }
  CHECK(edited->name == "Custom");
  CHECK(edited->points.size() == 4U);
  CHECK(edited->points[0] == contour_point(0.0F, 0.0F));
  CHECK(edited->points[1] == contour_point(64.0F, 220.0F, true));
  CHECK(edited->points[2] == contour_point(150.0F, 40.0F));
  CHECK(edited->points[3] == contour_point(255.0F, 255.0F));
  CHECK(*edited == last_change);
  // The curve the editor drew is the one the effects render: both read this table.
  const auto lut = patchy::build_style_contour_lut(*edited);
  CHECK(lut[64] == 220U && lut[150] == 40U);
}

void ui_contour_editor_cancel_and_untouched_curve_keep_the_original() {
  // An imported curve with a fractional point and its own name.
  auto imported = ridge_contour("Imported Ridge");
  imported.points[2].x = 100.5F;

  // Cancel hands back nothing, even after edits.
  when_dialog_visible(QStringLiteral("contourEditorDialog"), [](QDialog& dialog) {
    auto* graph = editor_graph(dialog);
    CHECK(graph != nullptr);
    CHECK(graph->contour().points.size() == 5U);
    click_graph(*graph, 140.0, 90.0);
    CHECK(graph->contour().points.size() == 6U);
    dialog.reject();
  });
  CHECK(!patchy::ui::request_contour_edit(nullptr, imported).has_value());

  // OK on an untouched curve returns it exactly: name, fractional point and all.
  when_dialog_visible(QStringLiteral("contourEditorDialog"), [](QDialog& dialog) { dialog.accept(); });
  const auto untouched = patchy::ui::request_contour_edit(nullptr, imported);
  CHECK(untouched.has_value() && *untouched == imported);

  // Editing one point leaves the others' exact values alone.
  when_dialog_visible(QStringLiteral("contourEditorDialog"), [](QDialog& dialog) {
    auto* graph = editor_graph(dialog);
    CHECK(graph != nullptr);
    graph->set_selected_point(1);
    auto* output = dialog.findChild<QSpinBox*>(QStringLiteral("contourEditorOutputSpin"));
    CHECK(output != nullptr && output->value() == 200);
    output->setValue(150);
    dialog.accept();
  });
  const auto nudged = patchy::ui::request_contour_edit(nullptr, imported);
  CHECK(nudged.has_value());
  if (nudged.has_value()) {
    CHECK(nudged->name == "Custom");
    CHECK(nudged->points.size() == 5U);
    CHECK(nudged->points[1] == contour_point(40.0F, 150.0F, true));
    CHECK(nudged->points[2] == contour_point(100.5F, 30.0F, false));
  }
}

void deliver_graph_move(QWidget& widget, QPoint position) {
  QMouseEvent event(QEvent::MouseMove, QPointF(position), QPointF(widget.mapToGlobal(position)), Qt::NoButton,
                    Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&widget, &event);
}

// A point drag re-renders the layer's effects through the Layer Style preview, so its moves
// coalesce (docs/interactive-previews-drag-coalescing.md, "Rule for new drags").
void ui_contour_graph_drag_coalesces_queued_moves() {
  struct Result {
    patchy::StyleContourPoint after_burst;
    patchy::StyleContourPoint after_release;
    int coalesced{0};
    int changes_in_burst{0};
  };
  const auto run = [](bool queued) {
    Result result;
    patchy::ui::ContourGraphWidget graph;
    graph.resize(320, 320);
    graph.show();
    QApplication::processEvents();
    int changes = 0;
    graph.contour_changed = [&changes] { ++changes; };
    click_graph(graph, 60.0, 60.0);
    CHECK(graph.contour().points.size() == 3U);
    patchy::ui::set_slider_drag_coalescing_all_moves_for_testing(queued);
    const auto restore = qScopeGuard([] { patchy::ui::set_slider_drag_coalescing_all_moves_for_testing(false); });
    send_mouse(graph, QEvent::MouseButtonPress, graph_position(graph, 60.0, 60.0), Qt::LeftButton, Qt::LeftButton);
    const auto before = patchy::ui::slider_drag_moves_coalesced_for_testing();
    const auto changes_before = changes;
    for (int move = 1; move <= 30; ++move) {
      deliver_graph_move(graph, graph_position(graph, 60.0 + move * 4.0, 60.0 + move * 2.0));
    }
    process_events_for(50);
    result.changes_in_burst = changes - changes_before;
    result.after_burst = graph.contour().points[1];
    deliver_graph_move(graph, graph_position(graph, 190.0, 200.0));  // applied, opens a pass
    deliver_graph_move(graph, graph_position(graph, 200.0, 210.0));  // recorded when queued
    result.coalesced = patchy::ui::slider_drag_moves_coalesced_for_testing() - before;
    send_mouse(graph, QEvent::MouseButtonRelease, graph_position(graph, 200.0, 210.0), Qt::LeftButton, Qt::NoButton);
    process_events_for(20);
    result.after_release = graph.contour().points[1];
    return result;
  };
  const auto sent = run(false);
  const auto queued = run(true);
  CHECK(sent.coalesced == 0);
  CHECK(sent.changes_in_burst == 30);
  CHECK(queued.coalesced == 30);  // 29 of the burst, and the move right before the release
  CHECK(queued.changes_in_burst <= 2);
  CHECK(sent.after_burst == contour_point(180.0F, 120.0F));
  CHECK(queued.after_burst == sent.after_burst);
  CHECK(sent.after_release == contour_point(200.0F, 210.0F));
  CHECK(queued.after_release == sent.after_release);
}

void ui_layer_style_contour_editor_previews_and_cancel_restores() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  const auto layer = contour_test_layer(document);
  std::vector<patchy::LayerStyle> previews;
  const auto last_gloss = [&previews] {
    CHECK(!previews.empty() && !previews.back().bevels.empty());
    return previews.back().bevels.front().gloss_contour;
  };
  when_dialog_visible(QStringLiteral("patchyLayerStyleDialog"), [&](QDialog& dialog) {
    select_category(dialog, QStringLiteral("Bevel & Emboss"));
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("layerStyleBevelGlossContourCombo"));
    auto* edit = dialog.findChild<QPushButton*>(QStringLiteral("layerStyleBevelGlossContourComboEditButton"));
    auto* library = dialog.findChild<QPushButton*>(QStringLiteral("layerStyleBevelGlossContourComboLibraryButton"));
    CHECK(combo != nullptr && edit != nullptr && library != nullptr);
    CHECK(edit->isVisible());
    CHECK(!library->isEnabled());  // no contour library was handed in
    CHECK(combo->currentData().toString() == QStringLiteral("contour.linear"));
    CHECK(combo->findData(QStringLiteral("custom")) < 0);

    // Edit, then Cancel: the canvas preview follows the edit and goes back.
    when_dialog_visible(QStringLiteral("contourEditorDialog"), [&](QDialog& editor) {
      auto* graph = editor_graph(editor);
      CHECK(graph != nullptr);
      click_graph(*graph, 128.0, 30.0);
      CHECK(combo->currentData().toString() == QStringLiteral("custom"));
      CHECK(process_events_until([&] { return !previews.empty() && last_gloss().points.size() == 3U; }));
      CHECK(last_gloss().points[1] == contour_point(128.0F, 30.0F));
      editor.reject();
    });
    edit->click();
    CHECK(combo->currentData().toString() == QStringLiteral("contour.linear"));
    CHECK(combo->findData(QStringLiteral("custom")) < 0);
    CHECK(patchy::style_contour_is_linear(last_gloss()));

    // A click on the picker's curve swatch opens the editor too; this time OK.
    bool edited = false;
    when_dialog_visible(QStringLiteral("contourEditorDialog"), [&](QDialog& editor) {
      auto* graph = editor_graph(editor);
      CHECK(graph != nullptr);
      click_graph(*graph, 128.0, 30.0);
      auto* corner = editor.findChild<QCheckBox*>(QStringLiteral("contourEditorCornerCheck"));
      CHECK(corner != nullptr);
      corner->click();
      edited = true;
      editor.accept();
    });
    send_mouse(*combo, QEvent::MouseButtonPress, QPoint(8, combo->height() / 2), Qt::LeftButton, Qt::LeftButton);
    CHECK(process_events_until([&] { return edited && find_top_level_dialog(QStringLiteral("contourEditorDialog")) == nullptr; }));
    CHECK(combo->currentData().toString() == QStringLiteral("custom"));
    CHECK(combo->currentIndex() == 0);
    CHECK(process_events_until([&] { return last_gloss().points.size() == 3U && last_gloss().points[1].corner; }));

    // Picking a built-in keeps Custom on the list, so the drawn curve can be picked again.
    combo->setCurrentIndex(combo->findData(QStringLiteral("contour.ring")));
    CHECK(combo->findData(QStringLiteral("custom")) == 0);
    combo->setCurrentIndex(0);
    dialog.accept();
  });
  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, [&previews](const patchy::ui::LayerStyleSettings& value) { previews.push_back(value.style); });
  CHECK(settings.has_value());
  if (!settings.has_value() || settings->style.bevels.empty()) {
    CHECK(false);
    return;
  }
  const auto& gloss = settings->style.bevels.front().gloss_contour;
  CHECK(gloss.name == "Custom");
  CHECK(gloss.points.size() == 3U);
  CHECK(gloss.points[1] == contour_point(128.0F, 30.0F, true));
}

void ui_layer_style_contour_library_new_rename_delete_load_save() {
  QTemporaryDir directory;
  patchy::ui::ContourLibrary library(directory.filePath(QStringLiteral("contours")));
  const auto shc_path = directory.filePath(QStringLiteral("saved contours.shc"));
  const auto custom = ridge_contour("Custom");
  const auto drawn = peak_and_dip_contour();
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  const auto layer = contour_test_layer(document, custom);
  const auto gloss_name = QStringLiteral("layerStyleBevelGlossContourCombo");
  when_dialog_visible(QStringLiteral("patchyLayerStyleDialog"), [&](QDialog& dialog) {
    select_category(dialog, QStringLiteral("Bevel & Emboss"));
    auto* gloss = dialog.findChild<QComboBox*>(gloss_name);
    auto* shadow = dialog.findChild<QComboBox*>(QStringLiteral("layerStyleDropShadowContourCombo"));
    auto* library_button = dialog.findChild<QPushButton*>(gloss_name + QStringLiteral("LibraryButton"));
    CHECK(gloss != nullptr && shadow != nullptr && library_button != nullptr);
    CHECK(library_button->isEnabled());
    CHECK(gloss->currentData().toString() == QStringLiteral("custom"));
    CHECK(!library_action(dialog, gloss_name, QStringLiteral("contourLibraryRenameAction"))->isEnabled());
    CHECK(!library_action(dialog, gloss_name, QStringLiteral("contourLibraryDeleteAction"))->isEnabled());
    CHECK(!library_action(dialog, gloss_name, QStringLiteral("contourLibrarySaveAction"))->isEnabled());

    // New Contour names a contour, then draws it from a straight line; every picker lists it.
    answer_new_contour(QStringLiteral("My Ridge"), [](QDialog& editor) {
      draw_peak_and_dip(editor);
      editor.accept();
    });
    library_action(dialog, gloss_name, QStringLiteral("contourLibraryNewAction"))->trigger();
    CHECK(library.entries().size() == 1U);
    if (library.entries().size() != 1U) {
      dialog.reject();
      return;
    }
    const auto user_id = QStringLiteral("user:") + library.entries().front().storage_id;
    CHECK(library.entries().front().name == QStringLiteral("My Ridge"));
    CHECK(patchy::style_contours_same_shape(library.entries().front().contour, drawn));
    CHECK(gloss->currentData().toString() == user_id);
    CHECK(gloss->currentText() == QStringLiteral("My Ridge"));
    // The layer's own curve stays on the list as Custom.
    CHECK(gloss->findData(QStringLiteral("custom")) == 0);
    CHECK(shadow->findData(user_id) > 0);
    CHECK(shadow->currentData().toString() == QStringLiteral("contour.linear"));

    // The Bevel page with the saved contour picked, and the Library menu open.
    QApplication::processEvents();
    (void)capture_widget_artifact("ui_layer_style_contour_tools", dialog);
    auto* menu = dialog.findChild<QMenu*>(gloss_name + QStringLiteral("LibraryMenu"));
    CHECK(menu != nullptr);
    menu->popup(library_button->mapToGlobal(QPoint(0, library_button->height())));
    QApplication::processEvents();
    save_window_popup_artifact("ui_layer_style_contour_library_menu", dialog, *gloss, *menu);
    menu->close();
    QApplication::processEvents();

    // Rename.
    answer_name(QStringLiteral("Ridge Two"));
    library_action(dialog, gloss_name, QStringLiteral("contourLibraryRenameAction"))->trigger();
    CHECK(library.entries().front().name == QStringLiteral("Ridge Two"));
    CHECK(gloss->currentText() == QStringLiteral("Ridge Two"));
    CHECK(gloss->currentData().toString() == user_id);

    // Save Contours writes the ticked contours to a .shc file.
    when_dialog_visible(QStringLiteral("saveContoursDialog"), [&](QDialog& chooser) {
      auto* list = chooser.findChild<QListWidget*>(QStringLiteral("saveContoursList"));
      CHECK(list != nullptr && list->count() == 1);
      CHECK(list != nullptr && list->item(0)->checkState() == Qt::Checked);
      answer_file_when_visible(QStringLiteral("layerStyleSaveContoursFileDialog"), shc_path);
      chooser.accept();
    });
    library_action(dialog, gloss_name, QStringLiteral("contourLibrarySaveAction"))->trigger();
    CHECK(QFileInfo::exists(shc_path));
    if (QFile saved(shc_path); saved.open(QIODevice::ReadOnly)) {
      const auto bytes = saved.readAll();
      std::string error;
      const auto parsed = patchy::psd::read_shc(
          std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                        static_cast<std::size_t>(bytes.size())),
          error);
      CHECK(parsed.has_value() && parsed->contours.size() == 1U);
      CHECK(parsed.has_value() && parsed->contours[0].name == "Ridge Two");
      CHECK(parsed.has_value() && patchy::style_contours_same_shape(parsed->contours[0], drawn));
    } else {
      CHECK(false);
    }

    // Delete takes it off every list; the effect keeps the curve as Custom.
    library_action(dialog, gloss_name, QStringLiteral("contourLibraryDeleteAction"))->trigger();
    CHECK(library.entries().empty());
    CHECK(gloss->currentData().toString() == QStringLiteral("custom"));
    CHECK(shadow->findData(user_id) < 0);
    CHECK(QDir(library.storage_dir()).entryList({QStringLiteral("*.shc")}, QDir::Files).isEmpty());

    // Load Contours appends the file; the picker finds its curve on the list again.
    answer_file_when_visible(QStringLiteral("layerStyleLoadContoursFileDialog"), shc_path);
    library_action(dialog, gloss_name, QStringLiteral("contourLibraryLoadAction"))->trigger();
    CHECK(library.entries().size() == 1U);
    CHECK(gloss->currentText() == QStringLiteral("Ridge Two"));
    CHECK(gloss->currentData().toString().startsWith(QStringLiteral("user:")));
    // Loading the same file again adds nothing.
    answer_file_when_visible(QStringLiteral("layerStyleLoadContoursFileDialog"), shc_path);
    library_action(dialog, gloss_name, QStringLiteral("contourLibraryLoadAction"))->trigger();
    CHECK(library.entries().size() == 1U);
    dialog.accept();
  });
  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, {}, nullptr, nullptr, nullptr, {}, nullptr, patchy::RgbColor{0, 0, 0},
      patchy::RgbColor{255, 255, 255}, nullptr, patchy::BitDepth::UInt8, std::nullopt, 0, true, &library);
  CHECK(settings.has_value());
  if (!settings.has_value() || settings->style.bevels.empty()) {
    CHECK(false);
    return;
  }
  const auto& gloss = settings->style.bevels.front().gloss_contour;
  CHECK(gloss.name == "Ridge Two");
  CHECK(patchy::style_contours_same_shape(gloss, drawn));
  // A new library on the same folder reads the contour back.
  patchy::ui::ContourLibrary reopened(library.storage_dir());
  CHECK(reopened.entries().size() == 1U);
  CHECK(reopened.entries().size() == 1U && reopened.entries().front().name == QStringLiteral("Ridge Two"));
  CHECK(reopened.entries().size() == 1U && reopened.entries().front().contour == library.entries().front().contour);
}

void ui_contour_library_persists_and_reads_unicode_paths() {
  const auto root = patchy::test::unicode_artifact_dir(u8"contour-library");
  const auto storage = patchy::ui::to_qstring(root / "library");
  const auto ridge = ridge_contour("ignored");
  QString ridge_id;
  QString ring_id;
  {
    patchy::ui::ContourLibrary library(storage);
    CHECK(library.entries().empty());
    ridge_id = library.add_contour(QStringLiteral("  Ridge 日本  "), ridge);
    const auto* ring = patchy::find_builtin_contour_preset("contour.ring");
    CHECK(ring != nullptr);
    ring_id = library.add_contour(QString(), ring->contour);  // unnamed
    CHECK(!ridge_id.isEmpty() && !ring_id.isEmpty() && ridge_id != ring_id);
    CHECK(library.entries().size() == 2U);
    const auto* ridge_entry = library.find_entry(ridge_id);
    CHECK(ridge_entry != nullptr && ridge_entry->name == QStringLiteral("Ridge 日本"));
    CHECK(ridge_entry != nullptr && ridge_entry->contour.name == "Ridge \xE6\x97\xA5\xE6\x9C\xAC");
    const auto* ring_entry = library.find_entry(ring_id);
    CHECK(ring_entry != nullptr && ring_entry->name == QStringLiteral("Contour 2"));
    CHECK(library.find_matching(ridge) == ridge_entry);
    CHECK(library.find_matching(patchy::StyleContour{}) == nullptr);
    // Fractional points are stored as whole levels, the way a .shc file holds them.
    auto fractional = ridge;
    fractional.points[2].y = 30.4F;
    const auto fractional_id = library.add_contour(QStringLiteral("Fractional"), fractional);
    CHECK(library.find_entry(fractional_id) != nullptr);
    CHECK(library.find_entry(fractional_id)->contour.points[2].y == 30.0F);
    CHECK(library.remove_contour(fractional_id));
    CHECK(!library.remove_contour(fractional_id));
    CHECK(library.rename_contour(ring_id, QStringLiteral("Ring \U0001F3A8")));
    CHECK(!library.rename_contour(ring_id, QStringLiteral("   ")));
    CHECK(!library.rename_contour(QStringLiteral("no-such-id"), QStringLiteral("x")));
  }
  // A new library on the same (Unicode) folder reads everything back.
  patchy::ui::ContourLibrary library(storage);
  CHECK(library.entries().size() == 2U);
  const auto* ridge_entry = library.find_entry(ridge_id);
  const auto* ring_entry = library.find_entry(ring_id);
  CHECK(ridge_entry != nullptr && ring_entry != nullptr);
  if (ridge_entry == nullptr || ring_entry == nullptr) {
    return;
  }
  CHECK(ridge_entry->name == QStringLiteral("Ridge 日本"));
  CHECK(patchy::style_contours_same_shape(ridge_entry->contour, ridge));
  CHECK(ring_entry->name == QStringLiteral("Ring \U0001F3A8"));
  CHECK(ring_entry->contour.name == "Ring \xF0\x9F\x8E\xA8");

  // Export and import through every class of special character in a file name.
  QString error;
  for (const auto stem : patchy::test::kUnicodePathStems) {
    const auto path = root / patchy::test::unicode_path_piece(std::u8string(stem) + u8".shc");
    const auto file = patchy::ui::to_qstring(path);
    CHECK(library.export_shc({ridge_id, ring_id}, file, error));
    CHECK(error.isEmpty());
    CHECK(std::filesystem::exists(path));
    QTemporaryDir scratch;
    patchy::ui::ContourLibrary other(scratch.filePath(QStringLiteral("contours")));
    QStringList warnings;
    const auto ids = other.import_shc(file, error, warnings);
    CHECK(error.isEmpty() && warnings.isEmpty());
    CHECK(ids.size() == 2 && other.entries().size() == 2U);
    CHECK(other.find_matching(ridge) != nullptr);
    CHECK(other.find_matching(ridge) != nullptr && other.find_matching(ridge)->name == ridge_entry->name);
    // The same file again: nothing new.
    CHECK(other.import_shc(file, error, warnings).size() == 2 && other.entries().size() == 2U);
  }
  // The folder holds exactly the five files, under their real names.
  std::size_t shc_files = 0;
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    shc_files += entry.path().extension() == ".shc" ? 1U : 0U;
  }
  CHECK(shc_files == patchy::test::kUnicodePathStems.size());

  // Failures: nothing chosen, a missing file, a file that is not a contour file.
  CHECK(!library.export_shc({}, patchy::ui::to_qstring(root / "none.shc"), error));
  CHECK(!error.isEmpty());
  CHECK(!std::filesystem::exists(root / "none.shc"));
  QStringList warnings;
  CHECK(library.import_shc(patchy::ui::to_qstring(root / "missing.shc"), error, warnings).isEmpty());
  CHECK(!error.isEmpty());
  const auto junk = patchy::ui::to_qstring(root / "junk.shc");
  {
    QFile file(junk);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("not a contour file at all");
  }
  CHECK(library.import_shc(junk, error, warnings).isEmpty());
  CHECK(!error.isEmpty());
  CHECK(library.entries().size() == 2U);
}

// GitHub issue 726 follow-up: the name prompt was a stock input dialog about 200 px wide, too
// narrow for the window manager to show "New Contour" in its title bar. It has one size now,
// wide enough for its whole title.
void ui_contour_name_dialog_fits_its_title_at_one_size() {
  const auto title = QStringLiteral("New Contour");
  when_dialog_visible(QStringLiteral("layerStyleContourNameDialog"), [&](QDialog& dialog) {
    auto* input = dialog.findChild<QLineEdit*>(QStringLiteral("contourNameEdit"));
    CHECK(input != nullptr);
    CHECK(dialog.windowTitle() == title);
    // One fixed size: nothing to resize.
    CHECK(dialog.minimumSize() == dialog.maximumSize());
    CHECK(dialog.size() == dialog.minimumSize());
    CHECK(dialog.width() >= 340);
    // The title in the title bar's font, beside the icon and buttons a title bar holds.
    CHECK(dialog.width() >= patchy::ui::progress_dialog_title_text_width(dialog, title) +
                                patchy::ui::progress_dialog_title_reserved_width(dialog));
    // The suggested name is selected, ready to be typed over.
    CHECK(input != nullptr && input->text() == QStringLiteral("Contour 1"));
    CHECK(input != nullptr && input->selectedText() == QStringLiteral("Contour 1"));
    CHECK(input != nullptr && dialog.focusWidget() == input);
    QApplication::processEvents();
    (void)capture_widget_artifact("ui_contour_name_dialog", dialog);
    if (input != nullptr) {
      input->setText(QStringLiteral("  Steep Ridge  "));
    }
    dialog.accept();
  });
  const auto named = patchy::ui::request_contour_name(nullptr, title, QStringLiteral("Contour 1"));
  CHECK(named.has_value() && *named == QStringLiteral("Steep Ridge"));

  // A longer title widens the dialog, up to the cap every fitted title has (three fifths of
  // the screen, 520 px at most).
  const auto long_title = QStringLiteral("Rename the Contour Saved for the Bevel and Emboss Gloss");
  int long_width = 0;
  when_dialog_visible(QStringLiteral("layerStyleContourNameDialog"), [&](QDialog& dialog) {
    long_width = dialog.width();
    CHECK(dialog.minimumSize() == dialog.maximumSize());
    CHECK(dialog.width() >= std::min(patchy::ui::progress_dialog_title_text_width(dialog, long_title) +
                                         patchy::ui::progress_dialog_title_reserved_width(dialog),
                                     patchy::ui::progress_dialog_title_width_cap(dialog)));
    dialog.reject();
  });
  CHECK(!patchy::ui::request_contour_name(nullptr, long_title, QStringLiteral("Ridge")).has_value());
  CHECK(long_width > 340);

  // An empty name is no name.
  when_dialog_visible(QStringLiteral("layerStyleContourNameDialog"), [](QDialog& dialog) {
    auto* input = dialog.findChild<QLineEdit*>(QStringLiteral("contourNameEdit"));
    CHECK(input != nullptr);
    if (input != nullptr) {
      input->setText(QStringLiteral("   "));
    }
    dialog.accept();
  });
  CHECK(!patchy::ui::request_contour_name(nullptr, title, QStringLiteral("Contour 1")).has_value());
}

// GitHub issue 726 follow-up: Save as New Contour in the editor asks for a name, adds the
// curve to the library and goes on from the saved contour.
void ui_contour_editor_save_as_new_contour_names_and_saves_the_curve() {
  QTemporaryDir directory;
  patchy::ui::ContourLibrary library(directory.filePath(QStringLiteral("contours")));
  std::vector<patchy::StyleContour> heard;
  const auto changed = [&heard](const patchy::StyleContour& value) { heard.push_back(value); };

  // Save, then OK: the saved contour comes back under its name.
  QString saved_id;
  when_dialog_visible(QStringLiteral("contourEditorDialog"), [&](QDialog& dialog) {
    auto* save = dialog.findChild<QPushButton*>(QStringLiteral("contourEditorSaveAsNewButton"));
    auto* ok = dialog.findChild<QDialogButtonBox*>();
    CHECK(save != nullptr && save->isVisible() && save->isEnabled());
    CHECK(ok != nullptr);
    if (save == nullptr) {
      dialog.reject();
      return;
    }
    // Left of OK and Cancel, on their row.
    if (ok != nullptr) {
      CHECK(save->geometry().right() < ok->geometry().left());
      CHECK(save->geometry().center().y() >= ok->geometry().top() &&
            save->geometry().center().y() <= ok->geometry().bottom());
    }
    draw_peak_and_dip(dialog);
    CHECK(!heard.empty() && heard.back().name == "Custom");

    // Cancelling the name saves nothing.
    when_dialog_visible(QStringLiteral("layerStyleContourNameDialog"), [](QDialog& prompt) { prompt.reject(); });
    save->click();
    CHECK(library.entries().empty());

    // The prompt suggests "Contour 1"; the typed name is the one saved.
    when_dialog_visible(QStringLiteral("layerStyleContourNameDialog"), [](QDialog& prompt) {
      auto* input = prompt.findChild<QLineEdit*>(QStringLiteral("contourNameEdit"));
      CHECK(input != nullptr && input->text() == QStringLiteral("Contour 1"));
      CHECK(prompt.windowTitle() == QStringLiteral("New Contour"));
      if (input != nullptr) {
        input->setText(QStringLiteral("Peak and Dip"));
      }
      prompt.accept();
    });
    save->click();
    CHECK(library.entries().size() == 1U);
    if (library.entries().size() != 1U) {
      dialog.reject();
      return;
    }
    const auto& entry = library.entries().front();
    CHECK(entry.name == QStringLiteral("Peak and Dip"));
    CHECK(entry.contour.points.size() == 4U);
    CHECK(entry.contour.points[1] == contour_point(64.0F, 220.0F, true));
    CHECK(entry.contour.points[2] == contour_point(150.0F, 40.0F));
    // The caller hears the saved contour, and the graph goes on from it.
    CHECK(!heard.empty() && heard.back() == entry.contour);
    CHECK(heard.back().name == "Peak and Dip");
    auto* graph = editor_graph(dialog);
    CHECK(graph != nullptr && patchy::style_contours_same_shape(graph->contour(), entry.contour));
    QApplication::processEvents();
    (void)capture_widget_artifact("ui_contour_editor_save_as_new", dialog);
    dialog.accept();
  });
  const auto saved = patchy::ui::request_contour_edit(nullptr, patchy::StyleContour{}, changed, &library, &saved_id);
  CHECK(saved.has_value() && library.entries().size() == 1U);
  if (!saved.has_value() || library.entries().size() != 1U) {
    return;
  }
  CHECK(saved->name == "Peak and Dip");
  CHECK(*saved == library.entries().front().contour);
  CHECK(saved_id == library.entries().front().storage_id);

  // Save, edit on, OK: the library keeps what was saved; the result is the newer curve.
  QString second_id = QStringLiteral("stale");
  when_dialog_visible(QStringLiteral("contourEditorDialog"), [&](QDialog& dialog) {
    auto* save = dialog.findChild<QPushButton*>(QStringLiteral("contourEditorSaveAsNewButton"));
    auto* graph = editor_graph(dialog);
    CHECK(save != nullptr && graph != nullptr);
    if (save == nullptr || graph == nullptr) {
      dialog.reject();
      return;
    }
    click_graph(*graph, 128.0, 30.0);
    answer_name(QStringLiteral("Low Middle"));
    save->click();
    CHECK(library.entries().size() == 2U);
    click_graph(*graph, 200.0, 250.0);
    CHECK(!heard.empty() && heard.back().name == "Custom");
    dialog.accept();
  });
  const auto edited_on = patchy::ui::request_contour_edit(nullptr, patchy::StyleContour{}, changed, &library, &second_id);
  CHECK(edited_on.has_value() && edited_on->name == "Custom" && edited_on->points.size() == 4U);
  CHECK(second_id.isEmpty());
  CHECK(library.entries().size() == 2U);
  const auto* low_middle = library.find_matching([] {
    patchy::StyleContour contour;
    contour.points = {contour_point(0.0F, 0.0F), contour_point(128.0F, 30.0F), contour_point(255.0F, 255.0F)};
    return contour;
  }());
  CHECK(low_middle != nullptr && low_middle->name == QStringLiteral("Low Middle"));

  // Save, then Cancel: nothing comes back, the contour stays saved.
  when_dialog_visible(QStringLiteral("contourEditorDialog"), [&](QDialog& dialog) {
    auto* save = dialog.findChild<QPushButton*>(QStringLiteral("contourEditorSaveAsNewButton"));
    auto* graph = editor_graph(dialog);
    CHECK(save != nullptr && graph != nullptr);
    if (save == nullptr || graph == nullptr) {
      dialog.reject();
      return;
    }
    click_graph(*graph, 30.0, 200.0);
    answer_name(QStringLiteral("Kept"));
    save->click();
    dialog.reject();
  });
  QString third_id = QStringLiteral("stale");
  CHECK(!patchy::ui::request_contour_edit(nullptr, patchy::StyleContour{}, changed, &library, &third_id).has_value());
  CHECK(third_id.isEmpty());
  CHECK(library.entries().size() == 3U);
  // A new library on the same folder reads all three back.
  patchy::ui::ContourLibrary reopened(library.storage_dir());
  CHECK(reopened.entries().size() == 3U);
}

// The same button from the Layer Style dialog: the picker the editor came from shows the saved
// contour, and the other pickers list it.
void ui_layer_style_contour_editor_save_as_new_selects_it_in_the_picker() {
  QTemporaryDir directory;
  patchy::ui::ContourLibrary library(directory.filePath(QStringLiteral("contours")));
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  const auto layer = contour_test_layer(document);
  std::vector<patchy::LayerStyle> previews;
  const auto gloss_name = QStringLiteral("layerStyleBevelGlossContourCombo");
  when_dialog_visible(QStringLiteral("patchyLayerStyleDialog"), [&](QDialog& dialog) {
    select_category(dialog, QStringLiteral("Bevel & Emboss"));
    auto* gloss = dialog.findChild<QComboBox*>(gloss_name);
    auto* shadow = dialog.findChild<QComboBox*>(QStringLiteral("layerStyleDropShadowContourCombo"));
    auto* edit = dialog.findChild<QPushButton*>(gloss_name + QStringLiteral("EditButton"));
    CHECK(gloss != nullptr && shadow != nullptr && edit != nullptr);
    if (gloss == nullptr || shadow == nullptr || edit == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(gloss->currentData().toString() == QStringLiteral("contour.linear"));

    // Save as New Contour, then Cancel: the layer's contour goes back, the saved one stays listed.
    when_dialog_visible(QStringLiteral("contourEditorDialog"), [&](QDialog& editor) {
      auto* save = editor.findChild<QPushButton*>(QStringLiteral("contourEditorSaveAsNewButton"));
      auto* graph = editor_graph(editor);
      CHECK(save != nullptr && graph != nullptr);
      if (save == nullptr || graph == nullptr) {
        editor.reject();
        return;
      }
      click_graph(*graph, 30.0, 200.0);
      answer_name(QStringLiteral("Early Rise"));
      save->click();
      CHECK(gloss->currentText() == QStringLiteral("Early Rise"));
      editor.reject();
    });
    edit->click();
    CHECK(library.entries().size() == 1U);
    if (library.entries().size() != 1U) {
      dialog.reject();
      return;
    }
    const auto early_id = QStringLiteral("user:") + library.entries().front().storage_id;
    CHECK(gloss->currentData().toString() == QStringLiteral("contour.linear"));
    CHECK(gloss->findData(early_id) > 0);
    CHECK(gloss->findData(QStringLiteral("custom")) < 0);
    CHECK(shadow->findData(early_id) > 0);
    CHECK(shadow->currentData().toString() == QStringLiteral("contour.linear"));

    // Save as New Contour, then OK: the picker shows the saved contour and the layer gets it.
    QString saved_user_id;
    when_dialog_visible(QStringLiteral("contourEditorDialog"), [&](QDialog& editor) {
      auto* save = editor.findChild<QPushButton*>(QStringLiteral("contourEditorSaveAsNewButton"));
      CHECK(save != nullptr);
      if (save == nullptr) {
        editor.reject();
        return;
      }
      draw_peak_and_dip(editor);
      CHECK(gloss->currentData().toString() == QStringLiteral("custom"));
      answer_name(QStringLiteral("Peak and Dip"));
      save->click();
      CHECK(library.entries().size() == 2U);
      // Shown in the picker at once, behind the open editor, and previewed on the canvas.
      CHECK(gloss->currentText() == QStringLiteral("Peak and Dip"));
      saved_user_id = gloss->currentData().toString();
      CHECK(saved_user_id.startsWith(QStringLiteral("user:")));
      CHECK(gloss->findData(QStringLiteral("custom")) < 0);
      CHECK(process_events_until([&] {
        return !previews.empty() && !previews.back().bevels.empty() &&
               previews.back().bevels.front().gloss_contour.name == "Peak and Dip";
      }));
      editor.accept();
    });
    edit->click();
    CHECK(gloss->currentText() == QStringLiteral("Peak and Dip"));
    CHECK(gloss->currentData().toString() == saved_user_id);
    CHECK(shadow->findData(saved_user_id) > 0);
    CHECK(shadow->currentData().toString() == QStringLiteral("contour.linear"));
    QApplication::processEvents();
    (void)capture_widget_artifact("ui_layer_style_contour_saved_from_editor", dialog);
    dialog.accept();
  });
  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, [&previews](const patchy::ui::LayerStyleSettings& value) { previews.push_back(value.style); },
      nullptr, nullptr, nullptr, {}, nullptr, patchy::RgbColor{0, 0, 0}, patchy::RgbColor{255, 255, 255}, nullptr,
      patchy::BitDepth::UInt8, std::nullopt, 0, true, &library);
  CHECK(settings.has_value());
  if (!settings.has_value() || settings->style.bevels.empty()) {
    CHECK(false);
    return;
  }
  const auto& gloss = settings->style.bevels.front().gloss_contour;
  CHECK(gloss.name == "Peak and Dip");
  CHECK(gloss.points.size() == 4U);
  CHECK(gloss.points[1] == contour_point(64.0F, 220.0F, true));
  CHECK(settings->style.drop_shadows.front().contour.points.empty() ||
        patchy::style_contour_is_linear(settings->style.drop_shadows.front().contour));
}

// GitHub issue 726 follow-up: the Contour Manager (Edit > Presets, Preferences > Presets) lists
// the built-in contours, locked, and the saved ones, which it renames, edits, duplicates,
// deletes, loads and saves; the Layer Style pickers list what it leaves.
void ui_contour_manager_lists_renames_edits_duplicates_and_deletes() {
  QTemporaryDir directory;
  patchy::ui::ContourLibrary library(directory.filePath(QStringLiteral("contours")));
  const auto ridge = ridge_contour("Ridge");
  const auto ridge_id = library.add_contour(QStringLiteral("Ridge"), ridge);
  patchy::StyleContour dip;
  dip.points = {contour_point(0.0F, 0.0F), contour_point(128.0F, 30.0F), contour_point(255.0F, 255.0F)};
  const auto dip_id = library.add_contour(QStringLiteral("Dip"), dip);
  CHECK(!ridge_id.isEmpty() && !dip_id.isEmpty());
  const auto shc_path = directory.filePath(QStringLiteral("managed contours.shc"));
  const auto builtin_count = static_cast<int>(patchy::builtin_contour_presets().size());
  CHECK(builtin_count > 0);

  when_dialog_visible(QStringLiteral("contourManagerDialog"), [&](QDialog& dialog) {
    auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("contourManagerTree"));
    auto* name_edit = dialog.findChild<QLineEdit*>(QStringLiteral("contourManagerNameEdit"));
    auto* note = dialog.findChild<QLabel*>(QStringLiteral("contourManagerBuiltinNote"));
    auto* edit = dialog.findChild<QPushButton*>(QStringLiteral("contourManagerEditButton"));
    auto* duplicate = dialog.findChild<QPushButton*>(QStringLiteral("contourManagerDuplicateButton"));
    auto* remove = dialog.findChild<QPushButton*>(QStringLiteral("contourManagerDeleteButton"));
    auto* load = dialog.findChild<QPushButton*>(QStringLiteral("contourManagerLoadButton"));
    auto* save = dialog.findChild<QPushButton*>(QStringLiteral("contourManagerSaveButton"));
    auto* preview =
        static_cast<patchy::ui::ContourGraphWidget*>(dialog.findChild<QWidget*>(QStringLiteral("contourManagerPreview")));
    CHECK(tree != nullptr && name_edit != nullptr && note != nullptr && edit != nullptr && duplicate != nullptr &&
          remove != nullptr && load != nullptr && save != nullptr && preview != nullptr);
    if (tree == nullptr || name_edit == nullptr || note == nullptr || edit == nullptr || duplicate == nullptr ||
        remove == nullptr || load == nullptr || save == nullptr || preview == nullptr) {
      dialog.reject();
      return;
    }
    const auto selected_name = [&] {
      return tree->currentItem() != nullptr ? tree->currentItem()->text(0) : QString();
    };
    const auto rename_to = [&](const QString& name) {
      name_edit->setText(name);
      name_edit->editingFinished();
    };

    // Built-ins first, marked and locked; then the saved contours by name.
    CHECK(tree->topLevelItemCount() == builtin_count + 2);
    CHECK(tree->topLevelItem(0)->text(0) == QStringLiteral("Linear"));
    CHECK(tree->topLevelItem(0)->text(1) == QStringLiteral("Built-in"));
    CHECK(tree->topLevelItem(builtin_count)->text(0) == QStringLiteral("Dip"));
    CHECK(tree->topLevelItem(builtin_count)->text(1).isEmpty());
    CHECK(tree->topLevelItem(builtin_count + 1)->text(0) == QStringLiteral("Ridge"));
    // It opened on the contour it was asked for, with its curve in the preview.
    CHECK(selected_name() == QStringLiteral("Ridge"));
    CHECK(name_edit->isEnabled() && name_edit->text() == QStringLiteral("Ridge"));
    CHECK(!note->isVisible());
    CHECK(edit->isEnabled() && duplicate->isEnabled() && remove->isEnabled() && save->isEnabled());
    CHECK(preview->read_only());
    CHECK(patchy::style_contours_same_shape(preview->contour(), ridge));
    // The preview only shows the curve: a click adds no point.
    click_graph(*preview, 128.0, 128.0);
    CHECK(preview->contour().points.size() == ridge.points.size());
    QApplication::processEvents();
    (void)capture_widget_artifact("ui_contour_manager", dialog);

    // New Contour: a name, then the editor on a straight line. Cancel saves nothing.
    auto* new_button = dialog.findChild<QPushButton*>(QStringLiteral("contourManagerNewButton"));
    CHECK(new_button != nullptr && new_button->isEnabled());
    if (new_button != nullptr) {
      const auto before_new = library.entries().size();
      answer_new_contour(QStringLiteral("Drawn Later"), [](QDialog& editor) { editor.reject(); });
      new_button->click();
      CHECK(library.entries().size() == before_new);
      answer_new_contour(QStringLiteral("Drawn Here"), [](QDialog& editor) {
        draw_peak_and_dip(editor);
        editor.accept();
      });
      new_button->click();
      CHECK(library.entries().size() == before_new + 1U);
      CHECK(selected_name() == QStringLiteral("Drawn Here"));
      CHECK(patchy::style_contours_same_shape(preview->contour(), peak_and_dip_contour()));
      // Out of the way of the counts below.
      const auto drawn_id = tree->currentItem()->data(0, Qt::UserRole).toString();
      CHECK(library.remove_contour(drawn_id));
      tree->setCurrentItem(manager_row(*tree, QStringLiteral("Ridge")));
    }

    // A built-in can be duplicated, nothing else.
    auto* cone = manager_row(*tree, QStringLiteral("Cone"));
    CHECK(cone != nullptr);
    if (cone == nullptr) {
      dialog.reject();
      return;
    }
    tree->setCurrentItem(cone);
    CHECK(note->isVisible());
    CHECK(!name_edit->isEnabled() && name_edit->text() == QStringLiteral("Cone"));
    CHECK(!edit->isEnabled() && !remove->isEnabled() && duplicate->isEnabled());
    QApplication::processEvents();
    (void)capture_widget_artifact("ui_contour_manager_builtin", dialog);
    duplicate->click();
    CHECK(library.entries().size() == 3U);
    CHECK(selected_name() == QStringLiteral("Cone copy"));
    CHECK(name_edit->isEnabled() && edit->isEnabled() && remove->isEnabled());
    const auto* cone_preset = patchy::find_builtin_contour_preset("contour.cone");
    CHECK(cone_preset != nullptr);
    CHECK(cone_preset != nullptr && patchy::style_contours_same_shape(preview->contour(), cone_preset->contour));
    // A second copy gets its own name (the list was rebuilt, so the row is found again).
    cone = manager_row(*tree, QStringLiteral("Cone"));
    CHECK(cone != nullptr);
    if (cone == nullptr) {
      dialog.reject();
      return;
    }
    tree->setCurrentItem(cone);
    duplicate->click();
    CHECK(selected_name() == QStringLiteral("Cone copy 2"));
    CHECK(library.entries().size() == 4U);

    // Rename: the list re-sorts and the row stays selected. An empty name changes nothing.
    rename_to(QStringLiteral("Zigzag"));
    CHECK(selected_name() == QStringLiteral("Zigzag"));
    CHECK(tree->topLevelItem(tree->topLevelItemCount() - 1)->text(0) == QStringLiteral("Zigzag"));
    rename_to(QStringLiteral("   "));
    CHECK(selected_name() == QStringLiteral("Zigzag") && name_edit->text() == QStringLiteral("Zigzag"));
    {
      const auto* zigzag = library.find_entry(tree->currentItem()->data(0, Qt::UserRole).toString());
      CHECK(zigzag != nullptr && zigzag->name == QStringLiteral("Zigzag") && zigzag->contour.name == "Zigzag");
    }

    // Edit Curve changes the saved curve and keeps the name; Cancel keeps the curve.
    auto* dip_row = manager_row(*tree, QStringLiteral("Dip"));
    CHECK(dip_row != nullptr);
    if (dip_row == nullptr) {
      dialog.reject();
      return;
    }
    tree->setCurrentItem(dip_row);
    when_dialog_visible(QStringLiteral("contourEditorDialog"), [&](QDialog& editor) {
      auto* graph = editor_graph(editor);
      CHECK(graph != nullptr && graph->contour().points.size() == 3U);
      CHECK(editor.findChild<QPushButton*>(QStringLiteral("contourEditorSaveAsNewButton")) != nullptr);
      if (graph != nullptr) {
        click_graph(*graph, 200.0, 250.0);
      }
      editor.reject();
    });
    edit->click();
    CHECK(library.find_entry(dip_id) != nullptr && library.find_entry(dip_id)->contour.points.size() == 3U);
    when_dialog_visible(QStringLiteral("contourEditorDialog"), [&](QDialog& editor) {
      auto* graph = editor_graph(editor);
      CHECK(graph != nullptr);
      if (graph != nullptr) {
        click_graph(*graph, 200.0, 250.0);
      }
      editor.accept();
    });
    edit->click();
    {
      const auto* edited = library.find_entry(dip_id);
      CHECK(edited != nullptr && edited->name == QStringLiteral("Dip") && edited->contour.name == "Dip");
      CHECK(edited != nullptr && edited->contour.points.size() == 4U);
      CHECK(edited != nullptr && edited->contour.points[2] == contour_point(200.0F, 250.0F));
      CHECK(selected_name() == QStringLiteral("Dip"));
      CHECK(preview->contour().points.size() == 4U);
    }

    // Save as New Contour from the editor: the new contour is selected, the edited one untouched.
    const auto entries_before = library.entries().size();
    when_dialog_visible(QStringLiteral("contourEditorDialog"), [&](QDialog& editor) {
      auto* graph = editor_graph(editor);
      auto* save_as_new = editor.findChild<QPushButton*>(QStringLiteral("contourEditorSaveAsNewButton"));
      CHECK(graph != nullptr && save_as_new != nullptr);
      if (graph == nullptr || save_as_new == nullptr) {
        editor.reject();
        return;
      }
      click_graph(*graph, 60.0, 120.0);
      answer_name(QStringLiteral("Dip Variant"));
      save_as_new->click();
      editor.accept();
    });
    edit->click();
    CHECK(library.entries().size() == entries_before + 1U);
    CHECK(selected_name() == QStringLiteral("Dip Variant"));
    CHECK(preview->contour().points.size() == 5U);
    CHECK(library.find_entry(dip_id) != nullptr && library.find_entry(dip_id)->contour.points.size() == 4U);

    // Save Contours writes the ticked contours; Delete asks first.
    when_dialog_visible(QStringLiteral("saveContoursDialog"), [&](QDialog& chooser) {
      auto* list = chooser.findChild<QListWidget*>(QStringLiteral("saveContoursList"));
      CHECK(list != nullptr && list->count() == static_cast<int>(library.entries().size()));
      answer_file_when_visible(QStringLiteral("contourManagerSaveFileDialog"), shc_path);
      chooser.accept();
    });
    save->click();
    CHECK(QFileInfo::exists(shc_path));

    const auto count_before_delete = library.entries().size();
    when_dialog_visible(QStringLiteral("contourManagerDeleteMessageBox"), [](QDialog& box) { box.reject(); });
    remove->click();
    CHECK(library.entries().size() == count_before_delete);
    when_dialog_visible(QStringLiteral("contourManagerDeleteMessageBox"), [](QDialog& box) {
      auto* message = qobject_cast<QMessageBox*>(&box);
      CHECK(message != nullptr && message->button(QMessageBox::Yes) != nullptr);
      if (message != nullptr && message->button(QMessageBox::Yes) != nullptr) {
        message->button(QMessageBox::Yes)->click();
      } else {
        box.reject();
      }
    });
    remove->click();
    CHECK(library.entries().size() == count_before_delete - 1U);
    CHECK(manager_row(*tree, QStringLiteral("Dip Variant")) == nullptr);
    // The selection moved to a neighbouring saved contour.
    CHECK(tree->currentItem() != nullptr && !tree->currentItem()->data(0, Qt::UserRole).toString().isEmpty());

    // Load Contours brings the deleted one back from the file and selects the file's first.
    answer_file_when_visible(QStringLiteral("contourManagerLoadFileDialog"), shc_path);
    load->click();
    CHECK(library.entries().size() == count_before_delete);
    CHECK(manager_row(*tree, QStringLiteral("Dip Variant")) != nullptr);
    CHECK(tree->topLevelItemCount() == builtin_count + static_cast<int>(library.entries().size()));
    dialog.reject();
  });
  patchy::ui::request_contour_manager(nullptr, library, ridge_id);

  // What the manager left is what a Layer Style contour picker lists, after the built-ins.
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  const auto layer = contour_test_layer(document);
  when_dialog_visible(QStringLiteral("patchyLayerStyleDialog"), [&](QDialog& dialog) {
    select_category(dialog, QStringLiteral("Bevel & Emboss"));
    auto* gloss = dialog.findChild<QComboBox*>(QStringLiteral("layerStyleBevelGlossContourCombo"));
    CHECK(gloss != nullptr);
    if (gloss != nullptr) {
      for (const auto& entry : library.entries()) {
        const auto index = gloss->findData(QStringLiteral("user:") + entry.storage_id);
        CHECK(index > builtin_count);
        CHECK(index < 0 || gloss->itemText(index) == entry.name);
      }
      CHECK(gloss->findText(QStringLiteral("Zigzag")) > 0);
      CHECK(gloss->findText(QStringLiteral("Cone copy")) > 0);
      CHECK(gloss->findText(QStringLiteral("Dip Variant")) > 0);
    }
    dialog.reject();
  });
  (void)patchy::ui::request_layer_style_settings(
      nullptr, layer, {}, nullptr, nullptr, nullptr, {}, nullptr, patchy::RgbColor{0, 0, 0},
      patchy::RgbColor{255, 255, 255}, nullptr, patchy::BitDepth::UInt8, std::nullopt, 0, true, &library);

  // The library call the manager's Edit Curve uses: whole levels, name kept, written to disk.
  auto fractional = dip;
  fractional.points[1].y = 30.4F;
  CHECK(library.set_contour_curve(ridge_id, fractional));
  CHECK(!library.set_contour_curve(QStringLiteral("no-such-id"), fractional));
  patchy::ui::ContourLibrary reopened(library.storage_dir());
  const auto* rewritten = reopened.find_entry(ridge_id);
  CHECK(rewritten != nullptr && rewritten->name == QStringLiteral("Ridge") && rewritten->contour.name == "Ridge");
  CHECK(rewritten != nullptr && rewritten->contour.points.size() == 3U);
  CHECK(rewritten != nullptr && rewritten->contour.points[1] == contour_point(128.0F, 30.0F));
}

// GitHub issue 726 follow-up: New Contour asks for a name, then opens the editor on a
// straight line. OK saves the drawn curve under the name and the picker shows it; Cancel
// (of the name or of the editor) saves nothing and puts the picker back.
void ui_layer_style_new_contour_starts_from_a_straight_line() {
  QTemporaryDir directory;
  patchy::ui::ContourLibrary library(directory.filePath(QStringLiteral("contours")));
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  const auto* ring = patchy::find_builtin_contour_preset("contour.ring");
  CHECK(ring != nullptr);
  if (ring == nullptr) {
    return;
  }
  const auto layer = contour_test_layer(document, ring->contour);
  std::vector<patchy::LayerStyle> previews;
  const auto gloss_name = QStringLiteral("layerStyleBevelGlossContourCombo");
  when_dialog_visible(QStringLiteral("patchyLayerStyleDialog"), [&](QDialog& dialog) {
    select_category(dialog, QStringLiteral("Bevel & Emboss"));
    auto* gloss = dialog.findChild<QComboBox*>(gloss_name);
    CHECK(gloss != nullptr);
    if (gloss == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(gloss->currentData().toString() == QStringLiteral("contour.ring"));
    auto* new_action = library_action(dialog, gloss_name, QStringLiteral("contourLibraryNewAction"));

    // Cancel the name: no editor, nothing saved.
    when_dialog_visible(QStringLiteral("layerStyleContourNameDialog"), [](QDialog& prompt) { prompt.reject(); });
    new_action->trigger();
    CHECK(library.entries().empty());
    CHECK(find_top_level_dialog(QStringLiteral("contourEditorDialog")) == nullptr);
    CHECK(gloss->currentData().toString() == QStringLiteral("contour.ring"));

    // Cancel the editor: nothing saved, the picker back on Ring.
    answer_new_contour(QStringLiteral("Not Kept"), [&](QDialog& editor) {
      // While it is open the picker and the canvas show the straight line, then the edit.
      CHECK(gloss->currentData().toString() == QStringLiteral("contour.linear"));
      draw_peak_and_dip(editor);
      CHECK(gloss->currentData().toString() == QStringLiteral("custom"));
      CHECK(process_events_until([&] {
        return !previews.empty() && !previews.back().bevels.empty() &&
               previews.back().bevels.front().gloss_contour.points.size() == 4U;
      }));
      editor.reject();
    });
    new_action->trigger();
    CHECK(library.entries().empty());
    CHECK(gloss->currentData().toString() == QStringLiteral("contour.ring"));
    CHECK(gloss->findData(QStringLiteral("custom")) < 0);
    CHECK(process_events_until([&] {
      return !previews.empty() && !previews.back().bevels.empty() &&
             patchy::style_contours_same_shape(previews.back().bevels.front().gloss_contour, ring->contour);
    }));

    // OK: saved under the name, picked in this picker.
    answer_new_contour(QStringLiteral("Peak and Dip"), [&](QDialog& editor) {
      draw_peak_and_dip(editor);
      QApplication::processEvents();
      (void)capture_widget_artifact("ui_layer_style_new_contour_editor", editor);
      editor.accept();
    });
    new_action->trigger();
    CHECK(library.entries().size() == 1U);
    if (library.entries().size() == 1U) {
      const auto& entry = library.entries().front();
      CHECK(entry.name == QStringLiteral("Peak and Dip"));
      CHECK(patchy::style_contours_same_shape(entry.contour, peak_and_dip_contour()));
      CHECK(gloss->currentData().toString() == QStringLiteral("user:") + entry.storage_id);
      CHECK(gloss->currentText() == QStringLiteral("Peak and Dip"));
    }
    dialog.accept();
  });
  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, [&previews](const patchy::ui::LayerStyleSettings& value) { previews.push_back(value.style); },
      nullptr, nullptr, nullptr, {}, nullptr, patchy::RgbColor{0, 0, 0}, patchy::RgbColor{255, 255, 255}, nullptr,
      patchy::BitDepth::UInt8, std::nullopt, 0, true, &library);
  CHECK(settings.has_value() && !settings->style.bevels.empty());
  if (settings.has_value() && !settings->style.bevels.empty()) {
    CHECK(settings->style.bevels.front().gloss_contour.name == "Peak and Dip");
    CHECK(patchy::style_contours_same_shape(settings->style.bevels.front().gloss_contour, peak_and_dip_contour()));
  }
}

// GitHub issue 726 follow-up: a contour picker's list opens as a grid of curve chips, the
// built-in contours first, then the saved ones under a header; the name is the tooltip, the
// current one is selected, a click or Enter picks, Esc closes.
void ui_layer_style_contour_list_opens_as_a_grid() {
  QTemporaryDir directory;
  patchy::ui::ContourLibrary library(directory.filePath(QStringLiteral("contours")));
  const auto ridge_id = library.add_contour(QStringLiteral("Ridge"), ridge_contour("Ridge"));
  CHECK(!library.add_contour(QStringLiteral("Peak and Dip"), peak_and_dip_contour()).isEmpty());
  CHECK(!ridge_id.isEmpty());
  const auto builtin_count = static_cast<int>(patchy::builtin_contour_presets().size());
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  const auto layer = contour_test_layer(document);
  const auto gloss_name = QStringLiteral("layerStyleBevelGlossContourCombo");
  when_dialog_visible(QStringLiteral("patchyLayerStyleDialog"), [&](QDialog& dialog) {
    select_category(dialog, QStringLiteral("Bevel & Emboss"));
    auto* gloss = dialog.findChild<QComboBox*>(gloss_name);
    CHECK(gloss != nullptr);
    if (gloss == nullptr) {
      dialog.reject();
      return;
    }
    gloss->setCurrentIndex(gloss->findData(QStringLiteral("contour.cone")));
    CHECK(gloss->currentText() == QStringLiteral("Cone"));
    // The app's style sheet, as in the app: it sets the cells' padding and the scroll bar.
    ensure_capture_themed(dialog);

    gloss->showPopup();
    QApplication::processEvents();
    auto* grid = contour_grid(*gloss);
    CHECK(grid != nullptr);
    CHECK(!gloss->view()->isVisible());  // the flat list stays shut
    if (grid == nullptr) {
      dialog.reject();
      return;
    }
    // Every contour once, in the picker's order: the built-ins, a header, the saved ones.
    QStringList names;
    QListWidgetItem* header = nullptr;
    int header_row = -1;
    for (int row = 0; row < grid->count(); ++row) {
      auto* item = grid->item(row);
      if (patchy::ui::PresetGridWidget::is_folder_item(item)) {
        header = item;
        header_row = row;
        continue;
      }
      const auto name = item->data(patchy::ui::PresetGridWidget::kNameRole).toString();
      names << name;
      CHECK(item->toolTip() == name);
      CHECK(!item->isHidden());
    }
    QStringList expected;
    for (int row = 0; row < gloss->count(); ++row) {
      if (!gloss->itemText(row).isEmpty()) {
        expected << gloss->itemText(row);
      }
    }
    CHECK(names == expected);
    CHECK(names.size() == builtin_count + 2);
    CHECK(header != nullptr && header_row == builtin_count);
    CHECK(header != nullptr && header->text() == QStringLiteral("Saved Contours (2)"));
    CHECK(names.mid(builtin_count) == (QStringList{QStringLiteral("Peak and Dip"), QStringLiteral("Ridge")}));
    // The current contour is selected; nothing scrolls; the cells wrap into rows.
    CHECK(grid->currentItem() != nullptr && grid->currentItem()->toolTip() == QStringLiteral("Cone"));
    CHECK(grid->currentItem() != nullptr && grid->currentItem()->isSelected());
    CHECK(grid->verticalScrollBar()->maximum() == 0);
    // kGridColumns cells a row: the last of the first row is level with the first.
    CHECK(grid->visualItemRect(grid->item(patchy::ui::ContourPickerCombo::kGridColumns - 1)).top() ==
          grid->visualItemRect(grid->item(0)).top());
    CHECK(grid->visualItemRect(grid->item(patchy::ui::ContourPickerCombo::kGridColumns)).top() >
          grid->visualItemRect(grid->item(0)).top());
    CHECK(grid->iconSize() == patchy::ui::ContourPickerCombo::kGridIconSize);
    QApplication::processEvents();
    if (auto* popup = grid->window(); popup != nullptr) {
      save_window_popup_artifact("ui_layer_style_contour_grid", dialog, *gloss, *popup);
      (void)capture_widget_artifact("ui_layer_style_contour_grid_popup", *popup);
    }

    // The arrow keys move through the cells and Enter picks: Cone, then Cone - Inverted.
    QTest::keyClick(grid, Qt::Key_Right);
    CHECK(grid->currentItem() != nullptr && grid->currentItem()->toolTip() == QStringLiteral("Cone - Inverted"));
    QTest::keyClick(grid, Qt::Key_Return);
    QApplication::processEvents();
    CHECK(contour_grid(*gloss) == nullptr);
    CHECK(gloss->currentText() == QStringLiteral("Cone - Inverted"));

    // Esc closes it without a change. (A grid that just closed ignores the combo for a
    // moment: the click that closed it lands on the combo too.)
    QTest::qWait(300);
    gloss->showPopup();
    QApplication::processEvents();
    grid = contour_grid(*gloss);
    CHECK(grid != nullptr);
    if (grid != nullptr) {
      QTest::keyClick(grid, Qt::Key_Escape);
      QApplication::processEvents();
    }
    CHECK(contour_grid(*gloss) == nullptr);
    CHECK(gloss->currentText() == QStringLiteral("Cone - Inverted"));

    // A click on a saved contour picks it; the closed box shows its name.
    QTest::qWait(300);  // past the guard against the click that closed the grid
    gloss->showPopup();
    QApplication::processEvents();
    grid = contour_grid(*gloss);
    CHECK(grid != nullptr);
    if (grid != nullptr) {
      QListWidgetItem* ridge = nullptr;
      for (int row = 0; row < grid->count(); ++row) {
        if (grid->item(row)->toolTip() == QStringLiteral("Ridge")) {
          ridge = grid->item(row);
        }
      }
      CHECK(ridge != nullptr);
      if (ridge != nullptr) {
        QTest::mouseClick(grid->viewport(), Qt::LeftButton, Qt::NoModifier, grid->visualItemRect(ridge).center());
        QApplication::processEvents();
      }
    }
    CHECK(contour_grid(*gloss) == nullptr);
    CHECK(gloss->currentText() == QStringLiteral("Ridge"));
    CHECK(gloss->currentData().toString() == QStringLiteral("user:") + ridge_id);

    // The other pickers open the same way.
    for (const auto* other : {"layerStyleBevelContourCombo", "layerStyleDropShadowContourCombo"}) {
      auto* combo = dialog.findChild<QComboBox*>(QLatin1String(other));
      CHECK(dynamic_cast<patchy::ui::ContourPickerCombo*>(combo) != nullptr);
      // Bevel's Contour picker is off until its Contour row is ticked.
      if (combo == nullptr || !combo->isEnabled()) {
        continue;
      }
      QTest::qWait(300);
      combo->showPopup();
      QApplication::processEvents();
      auto* other_grid = contour_grid(*combo);
      CHECK(other_grid != nullptr);
      CHECK(other_grid != nullptr && other_grid->count() == builtin_count + 3);
      combo->hidePopup();
      QApplication::processEvents();
    }
    dialog.accept();
  });
  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, {}, nullptr, nullptr, nullptr, {}, nullptr, patchy::RgbColor{0, 0, 0},
      patchy::RgbColor{255, 255, 255}, nullptr, patchy::BitDepth::UInt8, std::nullopt, 0, true, &library);
  CHECK(settings.has_value() && !settings->style.bevels.empty());
  if (settings.has_value() && !settings->style.bevels.empty()) {
    CHECK(settings->style.bevels.front().gloss_contour.name == "Ridge");
  }
}

void ui_layer_style_swatch_follows_settings() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  const auto layer = contour_test_layer(document);
  QImage opening;
  QImage final_swatch;
  when_dialog_visible(QStringLiteral("patchyLayerStyleDialog"), [&](QDialog& dialog) {
    auto* swatch = dialog.findChild<QLabel*>(QStringLiteral("layerStyleSwatch"));
    auto* timer = dialog.findChild<QTimer*>(QStringLiteral("layerStyleSwatchTimer"));
    CHECK(swatch != nullptr && timer != nullptr);
    CHECK(swatch->isVisible());
    // It shows the layer's style as the dialog opens, at its own fixed size.
    opening = swatch->pixmap().toImage();
    CHECK(opening.size() == QSize(72, 72));
    CHECK(!timer->isActive());

    select_category(dialog, QStringLiteral("Drop Shadow"));
    auto* distance = dialog.findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowDistanceSpin"));
    CHECK(distance != nullptr);
    // A burst of changes (a slider drag) arms one render, not one per change.
    const auto key_before = swatch->pixmap().cacheKey();
    for (int value = 6; value <= 25; ++value) {
      distance->setValue(value);
    }
    CHECK(timer->isActive());
    CHECK(swatch->pixmap().cacheKey() == key_before);  // nothing rendered inside the burst
    CHECK(process_events_until([&] { return !timer->isActive(); }));
    CHECK(swatch->pixmap().cacheKey() != key_before);
    const auto after_distance = swatch->pixmap().toImage();
    CHECK(after_distance != opening);

    // Another page's change shows too.
    select_category(dialog, QStringLiteral("Bevel & Emboss"));
    auto* gloss = dialog.findChild<QComboBox*>(QStringLiteral("layerStyleBevelGlossContourCombo"));
    CHECK(gloss != nullptr);
    gloss->setCurrentIndex(gloss->findData(QStringLiteral("contour.ring")));
    CHECK(process_events_until([&] { return !timer->isActive(); }));
    CHECK(swatch->pixmap().toImage() != after_distance);
    final_swatch = swatch->pixmap().toImage();
    (void)capture_widget_artifact("ui_layer_style_swatch", dialog);
    dialog.accept();
  });
  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(settings.has_value());
  if (!settings.has_value()) {
    return;
  }
  // The swatch is the style preview of the settings the dialog returned, on
  // the square sample a pixel layer gets (GitHub issue 732).
  const auto expected = patchy::ui::render_style_preview(
      settings->style,
      patchy::psd::AslBlendSettings{settings->opacity, settings->blend_mode, settings->blend_if,
                                    settings->fill_opacity},
      {}, 72, patchy::ui::StylePreviewSample::Square);
  CHECK(final_swatch == expected.toImage());
  CHECK(opening == patchy::ui::render_style_preview(
                       layer.layer_style(), patchy::psd::AslBlendSettings{100, layer.blend_mode(), {}, 100}, {}, 72,
                       patchy::ui::StylePreviewSample::Square)
                       .toImage());
}

// GitHub issue 732: the swatch draws the style on "Aa" for a text layer and on
// a plain square for every other layer; style preset thumbnails keep "Aa".
void ui_layer_style_swatch_sample_follows_layer_kind() {
  using patchy::ui::StylePreviewSample;
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  auto pixel_layer = contour_test_layer(document);
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.color = patchy::RgbColor{240, 200, 60};
  stroke.size = 2.0F;
  pixel_layer.layer_style().strokes.push_back(stroke);
  auto text_layer = patchy::Layer(document.allocate_layer_id(), "Text: Title", patchy::LayerKind::Text);
  text_layer.layer_style() = pixel_layer.layer_style();
  const auto& style = std::as_const(pixel_layer).layer_style();
  const auto letters = patchy::ui::render_style_preview(
                           style, patchy::psd::AslBlendSettings{100, pixel_layer.blend_mode(), {}, 100}, {}, 72)
                           .toImage();
  const auto square = patchy::ui::render_style_preview(
                          style, patchy::psd::AslBlendSettings{100, pixel_layer.blend_mode(), {}, 100}, {}, 72,
                          StylePreviewSample::Square)
                          .toImage();
  CHECK(!letters.isNull() && !square.isNull());
  CHECK(letters != square);
  // The square is one solid face: its center pixel and a pixel a quarter in
  // from the center toward each corner match (letters leave gaps there).
  const QPoint center(36, 36);
  const auto face = square.pixel(center);
  for (const QPoint offset : {QPoint(-9, -9), QPoint(9, -9), QPoint(-9, 9), QPoint(9, 9)}) {
    CHECK(qAbs(qGray(square.pixel(center + offset)) - qGray(face)) <= 24);
  }
  // Its corners stay the card, with room for the drop shadow.
  CHECK(qAbs(qGray(square.pixel(3, 3)) - qGray(face)) > 40);

  const auto swatch_for = [](const patchy::Layer& layer) {
    QImage image;
    when_dialog_visible(QStringLiteral("patchyLayerStyleDialog"), [&](QDialog& dialog) {
      auto* swatch = dialog.findChild<QLabel*>(QStringLiteral("layerStyleSwatch"));
      CHECK(swatch != nullptr);
      if (swatch != nullptr) {
        image = swatch->pixmap().toImage();
      }
      (void)capture_widget_artifact(layer.kind() == patchy::LayerKind::Text ? "ui_layer_style_swatch_text_layer"
                                                                             : "ui_layer_style_swatch_pixel_layer",
                                    dialog);
      dialog.reject();
    });
    (void)patchy::ui::request_layer_style_settings(nullptr, layer, {});
    return image;
  };
  CHECK(swatch_for(pixel_layer) == square);
  CHECK(swatch_for(text_layer) == letters);

  // The style presets' thumbnails keep the letters.
  QTemporaryDir directory;
  CHECK(directory.isValid());
  patchy::ui::StyleLibrary library(directory.filePath(QStringLiteral("styles")));
  const auto storage_id = library.add_style(QStringLiteral("Bevel and Shadow"), style, std::nullopt, {});
  CHECK(!storage_id.isEmpty());
  CHECK(library.entries().size() == 1U);
  if (library.entries().size() == 1U) {
    const auto& entry = library.entries().front();
    const auto extent = entry.thumbnail.width();
    CHECK(extent > 0);
    const auto thumbnail = entry.thumbnail.toImage();
    CHECK(thumbnail ==
          patchy::ui::render_style_preview(entry.style, entry.blend_settings, {}, extent).toImage());
    CHECK(thumbnail != patchy::ui::render_style_preview(entry.style, entry.blend_settings, {}, extent,
                                                        StylePreviewSample::Square)
                           .toImage());
  }
}

}  // namespace

std::vector<patchy::test::TestCase> contour_editor_tests() {
  return {
      {"ui_contour_editor_adds_moves_deletes_and_toggles_corners",
       ui_contour_editor_adds_moves_deletes_and_toggles_corners},
      {"ui_contour_editor_cancel_and_untouched_curve_keep_the_original",
       ui_contour_editor_cancel_and_untouched_curve_keep_the_original},
      {"ui_contour_graph_drag_coalesces_queued_moves", ui_contour_graph_drag_coalesces_queued_moves},
      {"ui_layer_style_contour_editor_previews_and_cancel_restores",
       ui_layer_style_contour_editor_previews_and_cancel_restores},
      {"ui_layer_style_contour_library_new_rename_delete_load_save",
       ui_layer_style_contour_library_new_rename_delete_load_save},
      {"ui_contour_library_persists_and_reads_unicode_paths", ui_contour_library_persists_and_reads_unicode_paths},
      {"ui_layer_style_swatch_sample_follows_layer_kind", ui_layer_style_swatch_sample_follows_layer_kind},
      {"ui_layer_style_swatch_follows_settings", ui_layer_style_swatch_follows_settings},
      {"ui_contour_name_dialog_fits_its_title_at_one_size", ui_contour_name_dialog_fits_its_title_at_one_size},
      {"ui_contour_editor_save_as_new_contour_names_and_saves_the_curve",
       ui_contour_editor_save_as_new_contour_names_and_saves_the_curve},
      {"ui_layer_style_contour_editor_save_as_new_selects_it_in_the_picker",
       ui_layer_style_contour_editor_save_as_new_selects_it_in_the_picker},
      {"ui_layer_style_new_contour_starts_from_a_straight_line", ui_layer_style_new_contour_starts_from_a_straight_line},
      {"ui_layer_style_contour_list_opens_as_a_grid", ui_layer_style_contour_list_opens_as_a_grid},
      {"ui_contour_manager_lists_renames_edits_duplicates_and_deletes",
       ui_contour_manager_lists_renames_edits_duplicates_and_deletes},
  };
}
