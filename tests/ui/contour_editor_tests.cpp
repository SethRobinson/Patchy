// The contour editor, the contour library and its Photoshop .shc files, and the
// Layer Style dialog's contour tools and style preview swatch (GitHub issue 726).
#include "core/contour_editing.hpp"
#include "core/contour_presets.hpp"
#include "core/document.hpp"
#include "core/style_contour.hpp"
#include "psd/shc_io.hpp"
#include "ui/contour_editor_dialog.hpp"
#include "ui/contour_library.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/layer_style_dialog.hpp"
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
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScopeGuard>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

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
    auto* input = qobject_cast<QInputDialog*>(&dialog);
    CHECK(input != nullptr);
    input->setTextValue(name);
    dialog.accept();
  });
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

    // New Contour saves the picker's curve under a name; every picker lists it.
    answer_name(QStringLiteral("My Ridge"));
    library_action(dialog, gloss_name, QStringLiteral("contourLibraryNewAction"))->trigger();
    CHECK(library.entries().size() == 1U);
    if (library.entries().size() != 1U) {
      dialog.reject();
      return;
    }
    const auto user_id = QStringLiteral("user:") + library.entries().front().storage_id;
    CHECK(library.entries().front().name == QStringLiteral("My Ridge"));
    CHECK(patchy::style_contours_same_shape(library.entries().front().contour, custom));
    CHECK(gloss->currentData().toString() == user_id);
    CHECK(gloss->currentText() == QStringLiteral("My Ridge"));
    CHECK(gloss->findData(QStringLiteral("custom")) < 0);  // the custom curve is a listed contour now
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
    // The picker's list: the built-in shapes, then the saved contour, each name in full.
    gloss->showPopup();
    QApplication::processEvents();
    auto* list_popup = gloss->view()->window();
    CHECK(list_popup != nullptr && list_popup != &dialog && list_popup->isVisible());
    CHECK(gloss->view()->width() >= gloss->view()->sizeHintForColumn(0));
    save_window_popup_artifact("ui_layer_style_contour_picker_list", dialog, *gloss, *list_popup);
    gloss->hidePopup();
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
      CHECK(parsed.has_value() && patchy::style_contours_same_shape(parsed->contours[0], custom));
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
  CHECK(patchy::style_contours_same_shape(gloss, custom));
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
  // The swatch is the style presets' thumbnail of the settings the dialog returned.
  const auto expected = patchy::ui::render_style_preview(
      settings->style,
      patchy::psd::AslBlendSettings{settings->opacity, settings->blend_mode, settings->blend_if,
                                    settings->fill_opacity},
      {}, 72);
  CHECK(final_swatch == expected.toImage());
  CHECK(opening == patchy::ui::render_style_preview(
                       layer.layer_style(), patchy::psd::AslBlendSettings{100, layer.blend_mode(), {}, 100}, {}, 72)
                       .toImage());
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
      {"ui_layer_style_swatch_follows_settings", ui_layer_style_swatch_follows_settings},
  };
}
