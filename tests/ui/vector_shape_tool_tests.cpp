#include <QJsonObject>
#include <QLayout>
#include <QScrollBar>
#include <QStatusBar>
#include "ui/modifier_names.hpp"
#include "ui/tool_cursors.hpp"
#include "ui/canvas_widget.hpp"
#include "core/pattern_presets.hpp"
#include "ui/theme_manager.hpp"
#include "ui/theme_palette.hpp"
#include "ui/main_window.hpp"
#include "ui/panel_column.hpp"
#include "ui/app_settings.hpp"
#include "synthetic_dng.hpp"
#include "test_harness.hpp"
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QMetaObject>
#include <QIODevice>
#include <QStringList>
#include <QWidget>
#include <atomic>
#include <filesystem>
#include <fstream>
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
// Shape-tool vector mode tests: Shape-mode drags create shape layers (with
// combine ops extending them), Path mode populates the work path, Pixels mode
// keeps the legacy raster commit, and the mode rides new sessions.
#include "ui_test_support.hpp"
#include "ui/color_panel.hpp"
#include "core/document_path.hpp"
#include "core/palette_presets.hpp"
#include "core/pixel_buffer.hpp"
#include "core/vector_shape.hpp"
#include "core/vector_raster.hpp"
#include "ui/default_custom_shapes.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/measurement_units.hpp"
#include "ui/pattern_library.hpp"
#include "ui/shape_appearance_dialog.hpp"
#include "ui/unit_spin_box.hpp"
#include "ui/qt_paths.hpp"
#include "psd/psd_document_io.hpp"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSpinBox>
#include <QImage>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTimer>
#include <QMouseEvent>
#include <QToolBar>
#include <QFontMetrics>
#include <QToolButton>
#include <QPushButton>
#include <QEvent>
#include <QPainter>
#include <QStyleFactory>
#include <QStyleOptionComboBox>
#include <QTest>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <cstdio>
#include <cstring>

using namespace patchy::test::ui;

namespace {

// VectorSettingsGuard moved to ui_test_support.hpp (the readme screenshot
// scenes pin the same vector tool keys).

void shape_drag(patchy::ui::CanvasWidget& canvas, QPoint document_from, QPoint document_to) {
  drag(canvas, canvas.widget_position_for_document_point(document_from),
       canvas.widget_position_for_document_point(document_to));
  QApplication::processEvents();
}

void ui_shape_tool_creates_shape_layer_and_undoes() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  CHECK(canvas->vector_tool_mode() == patchy::ui::VectorToolMode::Shape);
  // A leaked corner radius would turn the drag into an 8-anchor rounded rect.
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(120, 140), QPoint(320, 260));

  CHECK(document.layers().size() == initial_layers + 1);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Rectangle 1");
  CHECK(patchy::layer_is_vector_shape(*layer));
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  CHECK(content->fill.kind == patchy::VectorFillKind::Solid);
  CHECK(!content->stroke.enabled);
  CHECK(content->path.subpaths.size() == 1);
  CHECK(content->path.subpaths[0].anchors.size() == 4);
  CHECK(content->origination.size() == 1);
  CHECK(content->origination[0].kind == patchy::LiveShapeKind::Rectangle);
  CHECK(std::abs(content->origination[0].left - 120.0) < 0.5);
  CHECK(std::abs(content->origination[0].bottom - 260.0) < 0.5);
  CHECK(layer->bounds().width == 200);
  CHECK(layer->bounds().height == 120);
  // Default appearance: black fill baked into the pixel cache.
  const auto center = canvas_pixel(*canvas, QPoint(220, 200));
  CHECK(color_close(center, Qt::black, 8));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers);
  const auto after_undo = canvas_pixel(*canvas, QPoint(220, 200));
  CHECK(color_close(after_undo, Qt::white, 8));
}

// A shape or text drag may begin on the pasteboard, as in Photoshop: the press
// used to be refused outside the document, while marquee drags were not.
void ui_shape_and_text_drags_start_off_canvas() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();
  // Zoom out so the pasteboard around the document is inside the widget.
  canvas->set_zoom(0.5);
  canvas->set_snap_enabled(false);
  QApplication::processEvents();

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  CHECK(canvas->vector_tool_mode() == patchy::ui::VectorToolMode::Shape);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(0);
  CHECK(canvas->rect().contains(canvas->widget_position_for_document_point(QPoint(-60, 40))));
  shape_drag(*canvas, QPoint(-60, 40), QPoint(150, 200));

  CHECK(document.layers().size() == initial_layers + 1);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = active.has_value() ? document.find_layer(*active) : nullptr;
  CHECK(layer != nullptr);
  if (layer != nullptr) {
    CHECK(patchy::layer_is_vector_shape(*layer));
    const auto* content = layer->vector_shape();
    CHECK(content != nullptr && content->origination.size() == 1);
    if (content != nullptr && content->origination.size() == 1) {
      CHECK(std::abs(content->origination[0].left - (-60.0)) < 0.5);
      CHECK(std::abs(content->origination[0].right - 150.0) < 0.5);
    }
    CHECK(layer->bounds().x == -60);
  }

  // The Text tool's box drag, from the pasteboard into the document.
  patchy::Document text_document(96, 64, patchy::PixelFormat::rgba8());
  patchy::ui::CanvasWidget text_canvas;
  text_canvas.resize(420, 300);
  text_canvas.set_document(&text_document);
  text_canvas.set_zoom(2.0);
  text_canvas.set_snap_enabled(false);
  QPoint requested_point;
  QRect requested_box;
  int requests = 0;
  text_canvas.set_text_requested_callback([&](QPoint point, QRect box) {
    requested_point = point;
    requested_box = box;
    ++requests;
  });
  text_canvas.set_tool(patchy::ui::CanvasTool::Text);
  text_canvas.show();
  QApplication::processEvents();
  drag(text_canvas, text_canvas.widget_position_for_document_point(QPoint(-12, -8)),
       text_canvas.widget_position_for_document_point(QPoint(40, 30)));
  CHECK(requests == 1);
  CHECK(requested_box.isValid());
  CHECK(requested_box.left() == -12);
  CHECK(requested_box.top() == -8);
  CHECK(requested_point == QPoint(-12, -8));
  // A plain click on the pasteboard places point text there too.
  click_widget_like_a_user(text_canvas, text_canvas.widget_position_for_document_point(QPoint(-20, 10)));
  CHECK(requests == 2);
  CHECK(requested_point == QPoint(-20, 10));
  CHECK(!requested_box.isValid() || requested_box.isEmpty());
}

void ui_shape_tool_combine_extends_active_shape_layer() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  shape_drag(*canvas, QPoint(100, 100), QPoint(400, 300));
  const auto layers_after_first = document.layers().size();

  auto* combine_combo = window.findChild<QComboBox*>(QStringLiteral("vectorCombineCombo"));
  CHECK(combine_combo != nullptr);
  combine_combo->setCurrentIndex(2);  // Subtract
  shape_drag(*canvas, QPoint(200, 160), QPoint(300, 240));

  CHECK(document.layers().size() == layers_after_first);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  CHECK(content->path.subpaths.size() == 2);
  CHECK(content->path.subpaths[1].op == patchy::PathCombineOp::Subtract);
  CHECK(content->path.subpaths[1].shape_group == 1);
  CHECK(content->origination.size() == 2);
  CHECK(patchy::layer_vector_block_dirty(*layer));
  // The subtracted interior shows the white background again.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(250, 200)), Qt::white, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(150, 130)), Qt::black, 8));
}

void ui_shape_tool_path_mode_populates_work_path() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path
  CHECK(canvas->vector_tool_mode() == patchy::ui::VectorToolMode::Path);

  shape_drag(*canvas, QPoint(150, 150), QPoint(350, 280));
  CHECK(document.layers().size() == initial_layers);
  const auto* work = document.work_path();
  CHECK(work != nullptr);
  CHECK(work->path().subpaths.size() == 1);
  CHECK(work->path().subpaths[0].anchors.size() == 4);
  CHECK(work->dirty());

  // A second drag extends the same work path in a new shape group.
  shape_drag(*canvas, QPoint(400, 150), QPoint(500, 250));
  CHECK(document.work_path()->path().subpaths.size() == 2);
  CHECK(document.work_path()->path().subpaths[1].shape_group == 1);

  // The persisted mode rides new document sessions.
  patchy::Document extra(320, 240, patchy::PixelFormat::rgb8());
  extra.add_pixel_layer("Base", solid_pixels(320, 240, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
  window.add_document_session(std::move(extra), QStringLiteral("Second"));
  QApplication::processEvents();
  auto* second_canvas = require_canvas(window);
  CHECK(second_canvas->vector_tool_mode() == patchy::ui::VectorToolMode::Path);
}

// A Palette swatch click recolors the options-bar solid Fill and the selected shape layer
// (GitHub issue 61); gradient paint is left alone.
void ui_palette_swatch_click_recolors_selected_shape() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  const auto* preset = patchy::find_builtin_palette_preset("pico8");
  CHECK(preset != nullptr);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  editing.palette_revision = 1;
  document.palette_editing() = editing;
  patchy::ui::MainWindowTestAccess::refresh_document_info(window);
  QApplication::processEvents();
  auto* grid = window.findChild<QWidget*>(QStringLiteral("paletteSwatchGrid"));
  CHECK(grid != nullptr);
  const auto click_swatch = [grid](int index) {
    const QPoint center((index % 12) * 20 + 9, (index / 12) * 20 + 9);
    send_mouse(*grid, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*grid, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };
  const auto swatch = [preset](int index) { return preset->colors[static_cast<std::size_t>(index)]; };

  require_action_by_text(window, QStringLiteral("Ellipse"))->trigger();
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(0);  // Shape
  auto& fill = patchy::ui::MainWindowTestAccess::current_vector_fill(window);
  fill = {};
  fill.kind = patchy::VectorFillKind::Solid;
  fill.color = {10, 20, 30};
  shape_drag(*canvas, QPoint(150, 150), QPoint(350, 280));
  const auto shape_id = std::as_const(document).active_layer_id();
  CHECK(shape_id.has_value());
  const auto shape = [&]() -> const patchy::VectorShapeContent* {
    const auto* layer = std::as_const(document).find_layer(shape_id.value_or(0));
    return layer != nullptr ? layer->vector_shape() : nullptr;
  };
  CHECK(shape() != nullptr);
  if (shape() == nullptr) {
    return;
  }
  CHECK(shape()->fill.color == (patchy::RgbColor{10, 20, 30}));

  // Solid fill: the box and the layer take the swatch, as one undo step.
  const auto depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  click_swatch(8);
  CHECK(fill.kind == patchy::VectorFillKind::Solid);
  CHECK(fill.color == swatch(8));
  CHECK(shape()->fill.color == swatch(8));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
  CHECK(canvas->primary_color() == QColor(swatch(8).red, swatch(8).green, swatch(8).blue));

  // Another tool: the swatch only sets the foreground.
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  click_swatch(9);
  CHECK(shape()->fill.color == swatch(8));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);

  // Gradient fill: left alone.
  require_action_by_text(window, QStringLiteral("Ellipse"))->trigger();
  QApplication::processEvents();
  fill.kind = patchy::VectorFillKind::Gradient;
  CHECK(patchy::ui::MainWindowTestAccess::apply_options_bar_appearance(window));
  const auto gradient_depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  click_swatch(10);
  CHECK(shape()->fill.kind == patchy::VectorFillKind::Gradient);
  CHECK(shape()->fill.color == swatch(8));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == gradient_depth);
}

// GitHub issue 67: an Eyedropper pick recolors the selected shape layer and the options-bar
// solid Fill the way a swatch click does, as one undo step. An Alt-pick from a painting tool
// only sets the foreground.
void ui_eyedropper_pick_recolors_selected_shape() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // Two sample patches on the bottom pixel layer.
  patchy::Layer* background = nullptr;
  for (auto& layer : document.layers()) {
    if (layer.kind() == patchy::LayerKind::Pixel && !layer.pixels().empty()) {
      background = &layer;
      break;
    }
  }
  CHECK(background != nullptr);
  if (background == nullptr) {
    return;
  }
  fill_pixel_rect(background->pixels(), QRect(0, 0, 60, 60), QColor(200, 100, 50));
  fill_pixel_rect(background->pixels(), QRect(0, 70, 60, 60), QColor(20, 160, 90));
  canvas->document_changed();
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Ellipse"))->trigger();
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(0);  // Shape
  auto& fill = patchy::ui::MainWindowTestAccess::current_vector_fill(window);
  fill = {};
  fill.kind = patchy::VectorFillKind::Solid;
  fill.color = {10, 20, 30};
  shape_drag(*canvas, QPoint(150, 150), QPoint(350, 280));
  const auto shape_id = std::as_const(document).active_layer_id();
  CHECK(shape_id.has_value());
  const auto shape = [&]() -> const patchy::VectorShapeContent* {
    const auto* layer = std::as_const(document).find_layer(shape_id.value_or(0));
    return layer != nullptr ? layer->vector_shape() : nullptr;
  };
  CHECK(shape() != nullptr);
  if (shape() == nullptr) {
    return;
  }

  const auto pick = [&](QPoint document_point, Qt::KeyboardModifiers modifiers) {
    const auto widget_point = canvas->widget_position_for_document_point(document_point);
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point, Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::processEvents();
  };

  // Eyedropper tool: the shape and the Fill box take the picked color.
  const auto depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  require_action_by_text(window, QStringLiteral("Pick"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Eyedropper);
  pick(QPoint(30, 30), Qt::NoModifier);
  CHECK(canvas->primary_color() == QColor(200, 100, 50));
  CHECK(fill.color == (patchy::RgbColor{200, 100, 50}));
  CHECK(shape()->fill.color == (patchy::RgbColor{200, 100, 50}));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);

  // Alt-pick from the Brush: foreground only.
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  pick(QPoint(30, 100), Qt::AltModifier);
  CHECK(canvas->primary_color() == QColor(20, 160, 90));
  CHECK(shape()->fill.color == (patchy::RgbColor{200, 100, 50}));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);

  // Undo takes the shape back to its drawn color.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(shape() != nullptr && shape()->fill.color == (patchy::RgbColor{10, 20, 30}));
}

// GitHub issue 67: a color chosen in the Foreground color panel while a shape
// tool is active recolors the options-bar Fill and the selected shape, debounced
// so a drag through the picker is one undo step; gradient fills are left alone.
void ui_foreground_panel_color_recolors_selected_shape() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  require_action_by_text(window, QStringLiteral("Ellipse"))->trigger();
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(0);  // Shape
  auto& fill = patchy::ui::MainWindowTestAccess::current_vector_fill(window);
  fill = {};
  fill.kind = patchy::VectorFillKind::Solid;
  fill.color = {10, 20, 30};
  shape_drag(*canvas, QPoint(150, 150), QPoint(350, 280));
  const auto shape_id = std::as_const(document).active_layer_id();
  CHECK(shape_id.has_value());
  const auto shape = [&]() -> const patchy::VectorShapeContent* {
    const auto* layer = std::as_const(document).find_layer(shape_id.value_or(0));
    return layer != nullptr ? layer->vector_shape() : nullptr;
  };
  CHECK(shape() != nullptr);
  if (shape() == nullptr) {
    return;
  }

  auto* foreground_button = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(foreground_button != nullptr);
  foreground_button->click();
  QApplication::processEvents();
  auto* dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
  CHECK(dialog != nullptr);
  auto* picker = dialog != nullptr
                     ? dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"))
                     : nullptr;
  CHECK(picker != nullptr);
  if (picker == nullptr) {
    return;
  }

  // The Fill box follows at once; the layer follows after the debounce, as one step.
  const auto depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  picker->setCurrentColor(QColor(200, 100, 50));
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(200, 100, 50));
  CHECK(fill.color == (patchy::RgbColor{200, 100, 50}));
  process_events_for(400);
  CHECK(shape()->fill.color == (patchy::RgbColor{200, 100, 50}));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);

  // Two quick changes (a drag through the picker) coalesce into one undo step.
  picker->setCurrentColor(QColor(10, 200, 10));
  picker->setCurrentColor(QColor(20, 160, 90));
  process_events_for(400);
  CHECK(shape()->fill.color == (patchy::RgbColor{20, 160, 90}));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth + 2);

  // Gradient fill: the paint kind is an explicit choice, so the color is ignored.
  fill.kind = patchy::VectorFillKind::Gradient;
  CHECK(patchy::ui::MainWindowTestAccess::apply_options_bar_appearance(window));
  const auto gradient_depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  picker->setCurrentColor(QColor(220, 30, 30));
  process_events_for(400);
  CHECK(shape()->fill.kind == patchy::VectorFillKind::Gradient);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == gradient_depth);
  dialog->close();
  QApplication::processEvents();
}

void ui_shape_tool_pixels_mode_keeps_raster_commit() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(2);  // Pixels
  CHECK(canvas->vector_tool_mode() == patchy::ui::VectorToolMode::Pixels);
  canvas->set_primary_color(Qt::black);
  canvas->set_fill_shapes(true);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);

  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));
  CHECK(document.layers().size() == initial_layers);
  CHECK(document.work_path() == nullptr);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  CHECK(!patchy::layer_is_vector_shape(*document.find_layer(*active)));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 160)), Qt::black, 8));
}

void ui_line_shape_layer_uses_weight_and_stroke_settings() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Line);
  auto* weight_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorLineWeightSpin"));
  CHECK(weight_spin != nullptr);
  weight_spin->setValue(10);

  shape_drag(*canvas, QPoint(100, 200), QPoint(300, 200));
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Line 1");
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  CHECK(content->origination.size() == 1);
  CHECK(content->origination[0].kind == patchy::LiveShapeKind::Line);
  CHECK(std::abs(content->origination[0].line_weight - 10.0) < 1e-9);
  // A 10 px weight centered on y=200 rasterizes a band 195..205.
  CHECK(layer->bounds().height == 10);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 200)), Qt::black, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 212)), Qt::white, 8));
}

void pen_click(patchy::ui::CanvasWidget& canvas, QPoint document_point) {
  const auto widget_point = canvas.widget_position_for_document_point(document_point);
  drag(canvas, widget_point, widget_point);
  QApplication::processEvents();
}

void ui_pen_tool_click_and_close_creates_shape_layer() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  CHECK(canvas->vector_tool_mode() == patchy::ui::VectorToolMode::Shape);
  pen_click(*canvas, QPoint(100, 100));
  CHECK(canvas->pen_session_active());
  pen_click(*canvas, QPoint(300, 100));
  pen_click(*canvas, QPoint(200, 250));
  // Clicking the first anchor closes and commits the shape.
  pen_click(*canvas, QPoint(100, 100));
  CHECK(!canvas->pen_session_active());

  CHECK(document.layers().size() == initial_layers + 1);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Shape 1");
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  CHECK(content->path.subpaths.size() == 1);
  CHECK(content->path.subpaths[0].closed);
  CHECK(content->path.subpaths[0].anchors.size() == 3);
  CHECK(content->origination.empty());
  // Default black fill inside the triangle; white outside.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 140)), Qt::black, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(110, 240)), Qt::white, 8));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers);
}

void ui_pen_tool_path_mode_keys_and_handles() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  const auto initial_layers = document.layers().size();
  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path

  // Click, click, drag-for-smooth-handles, Backspace pops it, Enter commits.
  pen_click(*canvas, QPoint(100, 100));
  pen_click(*canvas, QPoint(260, 120));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(200, 240)),
       canvas->widget_position_for_document_point(QPoint(260, 280)));
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Backspace);
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->pen_session_active());
  const auto* work = document.work_path();
  CHECK(work != nullptr);
  CHECK(work->path().subpaths.size() == 1);
  CHECK(!work->path().subpaths[0].closed);
  CHECK(work->path().subpaths[0].anchors.size() == 2);
  CHECK(document.layers().size() == initial_layers);  // no shape layer in Path mode

  // A drag places a smooth anchor with mirrored handles.
  pen_click(*canvas, QPoint(400, 100));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(500, 200)),
       canvas->widget_position_for_document_point(QPoint(540, 240)));
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(document.work_path()->path().subpaths.size() == 2);
  const auto& smooth_subpath = document.work_path()->path().subpaths[1];
  CHECK(smooth_subpath.anchors.size() == 2);
  CHECK(smooth_subpath.anchors[1].smooth);
  CHECK(std::abs(smooth_subpath.anchors[1].out_x - 540.0) < 1.5);
  CHECK(std::abs(smooth_subpath.anchors[1].in_x - 460.0) < 1.5);

  // Escape cancels a session without touching the work path.
  pen_click(*canvas, QPoint(600, 100));
  pen_click(*canvas, QPoint(700, 100));
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->pen_session_active());
  CHECK(document.work_path()->path().subpaths.size() == 2);
}

// Creates a 200x120 rectangle shape layer at (100,100)-(300,220) and returns
// its layer id (Shape mode, zero corner radius).
patchy::LayerId make_rect_shape_layer(patchy::ui::MainWindow& window,
                                      patchy::ui::CanvasWidget& canvas) {
  canvas.set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(0);
  shape_drag(canvas, QPoint(100, 100), QPoint(300, 220));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  return *active;
}

void ui_direct_select_drags_anchor_with_single_undo() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);

  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  // Drag the top-left anchor from (100,100) to (60,70).
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(60, 70)));
  QApplication::processEvents();

  auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  CHECK(std::abs(content->path.subpaths[0].anchors[0].anchor_x - 60.0) < 1.0);
  CHECK(std::abs(content->path.subpaths[0].anchors[0].anchor_y - 70.0) < 1.0);
  // Editing the rectangle's anchors drops its live-shape annotation.
  CHECK(content->origination.empty());
  CHECK(patchy::layer_vector_block_dirty(*layer));
  CHECK(canvas->path_edit_has_selection());

  // The whole drag is one history entry: a single undo restores everything.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  layer = document.find_layer(layer_id);
  const auto* restored = layer->vector_shape();
  CHECK(std::abs(restored->path.subpaths[0].anchors[0].anchor_x - 100.0) < 0.5);
  CHECK(restored->origination.size() == 1);
}

void ui_path_select_drags_whole_shape_group() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);

  canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  // Press on the top-left anchor: PathSelect grabs the whole group; drag by
  // (50, 30).
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(150, 130)));
  QApplication::processEvents();

  auto* layer = document.find_layer(layer_id);
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  const auto& anchors = content->path.subpaths[0].anchors;
  CHECK(anchors.size() == 4);
  CHECK(std::abs(anchors[0].anchor_x - 150.0) < 1.0);
  CHECK(std::abs(anchors[2].anchor_x - 350.0) < 1.0);
  CHECK(std::abs(anchors[2].anchor_y - 250.0) < 1.0);
  CHECK(layer->bounds().x == 150);
  CHECK(layer->bounds().y == 130);
}

void ui_pen_adds_deletes_and_converts_anchors() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);

  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  // Click the middle of the top edge: adds an anchor (no new session).
  pen_click(*canvas, QPoint(200, 100));
  CHECK(!canvas->pen_session_active());
  auto* layer = document.find_layer(layer_id);
  CHECK(layer->vector_shape()->path.subpaths[0].anchors.size() == 5);
  CHECK(layer->vector_shape()->origination.empty());
  // The insertion t comes from 24-step sampling, so the anchor lands within
  // half a sample step of the click.
  const auto inserted_x = layer->vector_shape()->path.subpaths[0].anchors[1].anchor_x;
  CHECK(std::abs(inserted_x - 200.0) < 12.0);

  // Clicking an anchor deletes it.
  const auto inserted_y = layer->vector_shape()->path.subpaths[0].anchors[1].anchor_y;
  pen_click(*canvas, QPoint(static_cast<int>(inserted_x), static_cast<int>(inserted_y)));
  CHECK(!canvas->pen_session_active());
  layer = document.find_layer(layer_id);
  CHECK(layer->vector_shape()->path.subpaths[0].anchors.size() == 4);

  // Alt+click converts a corner anchor to smooth (handles appear).
  const auto corner = canvas->widget_position_for_document_point(QPoint(300, 100));
  send_mouse(*canvas, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton,
             Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, corner, Qt::LeftButton, Qt::NoButton,
             Qt::AltModifier);
  QApplication::processEvents();
  layer = document.find_layer(layer_id);
  const auto& converted = layer->vector_shape()->path.subpaths[0].anchors[1];
  CHECK(converted.smooth);
  CHECK(std::abs(converted.out_x - converted.anchor_x) > 1.0);
}

void ui_path_select_combine_op_edit_applies() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);

  // Add an inner rectangle with Subtract (combine index 2).
  auto* combine_combo = window.findChild<QComboBox*>(QStringLiteral("vectorCombineCombo"));
  CHECK(combine_combo != nullptr);
  combine_combo->setCurrentIndex(2);
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  shape_drag(*canvas, QPoint(160, 140), QPoint(240, 190));
  auto* layer = document.find_layer(layer_id);
  CHECK(layer->vector_shape()->path.subpaths.size() == 2);
  CHECK(layer->vector_shape()->path.subpaths[1].op == patchy::PathCombineOp::Subtract);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 160)), Qt::white, 8));

  // Select the inner shape with PathSelect and flip it to Add via the combo.
  canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  const auto inner_anchor = canvas->widget_position_for_document_point(QPoint(160, 140));
  drag(*canvas, inner_anchor, inner_anchor);
  QApplication::processEvents();
  CHECK(canvas->path_edit_has_selection());
  combine_combo->setCurrentIndex(1);  // Add
  QApplication::processEvents();
  layer = document.find_layer(layer_id);
  CHECK(layer->vector_shape()->path.subpaths[1].op == patchy::PathCombineOp::Add);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 160)), Qt::black, 8));
}

void ui_vector_mask_from_current_path_masks_layer() {
  VectorSettingsGuard settings_guard;
  patchy::Document base(400, 300, patchy::PixelFormat::rgb8());
  base.add_pixel_layer("Red", solid_pixels(400, 300, patchy::PixelFormat::rgb8(), QColor(200, 30, 30)));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(base), QStringLiteral("Vector Mask"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // Build a work path over the left half, then convert it to a vector mask.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(0, 0), QPoint(200, 300));
  CHECK(document.work_path() != nullptr);

  auto* action = window.findChild<QAction*>(QStringLiteral("layerVectorMaskCurrentPathAction"));
  CHECK(action != nullptr);
  action->trigger();
  QApplication::processEvents();

  const auto active = document.active_layer_id();
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->vector_mask() != nullptr);
  CHECK(layer->vector_mask()->path.subpaths.size() == 1);
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::VectorMask);
  // Left half stays red, right half is masked to the checkerboard/white.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(100, 150)), QColor(200, 30, 30), 8));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(300, 150)), QColor(200, 30, 30), 8));

  // The row grew a vector-mask thumbnail.
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* item = require_layer_item(*layer_list, QStringLiteral("Red"));
  auto* row = layer_list->itemWidget(item);
  CHECK(row != nullptr);
  CHECK(row->findChild<QLabel*>(QStringLiteral("layerVectorMaskThumbnail")) != nullptr);

  // The pen extends the mask path while the vector-mask target is active.
  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  pen_click(*canvas, QPoint(250, 50));
  pen_click(*canvas, QPoint(380, 50));
  pen_click(*canvas, QPoint(320, 250));
  pen_click(*canvas, QPoint(250, 50));  // close
  QApplication::processEvents();
  layer = document.find_layer(*active);
  CHECK(layer->vector_mask()->path.subpaths.size() == 2);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(315, 100)), QColor(200, 30, 30), 8));
}

void ui_vector_mask_shift_click_disable_and_rasterize() {
  VectorSettingsGuard settings_guard;
  patchy::Document base(400, 300, patchy::PixelFormat::rgb8());
  base.add_pixel_layer("Red", solid_pixels(400, 300, patchy::PixelFormat::rgb8(), QColor(200, 30, 30)));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(base), QStringLiteral("Vector Mask 2"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(0, 0), QPoint(200, 300));
  window.findChild<QAction*>(QStringLiteral("layerVectorMaskCurrentPathAction"))->trigger();
  QApplication::processEvents();
  const auto active = document.active_layer_id();

  // Shift-click the vector-mask thumbnail disables the mask (full red again).
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  click_layer_row_thumbnail(*layer_list, QStringLiteral("Red"),
                            QStringLiteral("layerVectorMaskThumbnail"), Qt::ShiftModifier);
  QApplication::processEvents();
  auto* layer = document.find_layer(*active);
  CHECK(layer->vector_mask() != nullptr);
  CHECK(layer->vector_mask()->disabled);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(300, 150)), QColor(200, 30, 30), 8));
  click_layer_row_thumbnail(*layer_list, QStringLiteral("Red"),
                            QStringLiteral("layerVectorMaskThumbnail"), Qt::ShiftModifier);
  QApplication::processEvents();
  layer = document.find_layer(*active);
  CHECK(!layer->vector_mask()->disabled);

  // Rasterize converts the coverage into the raster layer mask.
  window.findChild<QAction*>(QStringLiteral("layerVectorMaskRasterizeAction"))->trigger();
  QApplication::processEvents();
  layer = document.find_layer(*active);
  CHECK(layer->vector_mask() == nullptr);
  CHECK(layer->mask().has_value());
  CHECK(*layer->mask()->pixels.pixel(100, 150) == 255);
  CHECK(*layer->mask()->pixels.pixel(300, 150) == 0);
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(300, 150)), QColor(200, 30, 30), 8));

  // Delete the raster mask path: Vector Mask > Delete now errors politely
  // (no vector mask) without crashing.
  window.findChild<QAction*>(QStringLiteral("layerVectorMaskDeleteAction"))->trigger();
  QApplication::processEvents();
  CHECK(document.find_layer(*active)->mask().has_value());
}

QDialog* find_top_level_dialog(const QString& object_name);

void ui_free_transform_scales_shape_layer_crisply() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);

  auto* free_transform_action = window.findChild<QAction*>(QStringLiteral("editFreeTransformAction"));
  CHECK(free_transform_action != nullptr);
  free_transform_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  // Drag the bottom-right handle outward: (300,220) -> (500,340).
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(300, 220)),
       canvas->widget_position_for_document_point(QPoint(500, 340)));
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  const auto& anchors = content->path.subpaths[0].anchors;
  CHECK(std::abs(anchors[2].anchor_x - 500.0) < 2.0);
  CHECK(std::abs(anchors[2].anchor_y - 340.0) < 2.0);
  // A pure axis-aligned scale keeps the live-rect annotation, scaled.
  CHECK(content->origination.size() == 1);
  CHECK(std::abs(content->origination[0].right - 500.0) < 2.0);
  CHECK(patchy::layer_vector_block_dirty(*layer));
  // The re-raster is crisp: bounds match the scaled path, and the pixels come
  // from the rasterizer (fill reaches exactly to the new edges).
  CHECK(std::abs(layer->bounds().x - 100) <= 1);
  CHECK(std::abs(layer->bounds().x + layer->bounds().width - 500) <= 1);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(490, 330)), Qt::black, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(510, 330)), Qt::white, 8));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  layer = document.find_layer(layer_id);
  CHECK(std::abs(layer->vector_shape()->path.subpaths[0].anchors[2].anchor_x - 300.0) < 0.5);
}

// Free Transform's box on a shape hugs the ink, stroke included, but the commit
// transforms only the path and keeps the stroke width. The path must map so the
// redrawn ink fills the dragged box: before, a 20 px outside stroke made the
// fixed corner creep 3 px per transform and the dragged corner miss the box.
void ui_free_transform_shape_with_outside_stroke_lands_on_box() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);
  {
    auto* layer = document.find_layer(layer_id);
    CHECK(layer != nullptr && layer->vector_shape() != nullptr);
    auto content = *layer->vector_shape();
    content.stroke.enabled = true;
    content.stroke.width = 20.0;
    content.stroke.alignment = patchy::VectorStrokeAlignment::Outside;
    content.stroke.content.kind = patchy::VectorFillKind::Solid;
    content.stroke.content.color = patchy::RgbColor{200, 30, 30};
    layer->set_vector_shape(std::move(content));
    patchy::update_vector_shape_raster(*layer, patchy::Rect::from_size(document.width(), document.height()),
                                       &document.metadata().patterns);
    canvas->document_changed();
    QApplication::processEvents();
  }
  const auto ink = [&] { return document.find_layer(layer_id)->bounds(); };
  CHECK(ink().x == 80 && ink().y == 80 && ink().width == 240 && ink().height == 160);

  // Shift frees the aspect ratio (the Photoshop CC default pairing).
  const QPoint targets[] = {QPoint(400, 300), QPoint(360, 330)};
  for (const auto target : targets) {
    const auto before = ink();
    require_action(window, "editFreeTransformAction")->trigger();
    QApplication::processEvents();
    CHECK(canvas->free_transform_active());
    drag(*canvas, canvas->widget_position_for_document_point(QPoint(before.x + before.width, before.y + before.height)),
         canvas->widget_position_for_document_point(target), Qt::ShiftModifier);
    QApplication::processEvents();
    send_key(*canvas, Qt::Key_Return);
    QApplication::processEvents();
    CHECK(!canvas->free_transform_active());
    const auto after = ink();
    CHECK(std::abs(after.x - 80) <= 1 && std::abs(after.y - 80) <= 1);
    CHECK(std::abs(after.x + after.width - target.x()) <= 1);
    CHECK(std::abs(after.y + after.height - target.y()) <= 1);
    const auto* content = document.find_layer(layer_id)->vector_shape();
    CHECK(content != nullptr && content->stroke.width == 20.0);
    const auto path = content->path.bounds();
    CHECK(path.has_value() && std::abs(path->left - 100.0) < 1.0 && std::abs(path->right - (target.x() - 20.0)) < 1.0);
  }
}

// A Free Transform that leaves a shape wholly on the pasteboard used to bake
// it clipped to the canvas, i.e. to nothing: the Move tool reported "Click an
// editable layer to move" and the shape could not be brought back. The bake
// now covers the shape wherever it sits, so the plain Move drag returns it.
void ui_shape_moved_off_canvas_by_free_transform_moves_back() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  canvas->set_auto_select_layer(false);
  canvas->set_zoom(0.25);
  QApplication::processEvents();
  const auto on_canvas = canvas->widget_position_for_document_point(QPoint(200, 160));
  const auto below_canvas = canvas->widget_position_for_document_point(QPoint(200, 1300));

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  drag(*canvas, on_canvas, below_canvas);
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  const auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr && !layer->pixels().empty());
  CHECK(layer->bounds().y > document.height() && layer->bounds().width == 200 && layer->bounds().height == 120);

  drag(*canvas, below_canvas, on_canvas);
  QApplication::processEvents();
  layer = document.find_layer(layer_id);
  CHECK(layer->bounds().x == 100 && layer->bounds().y == 100);
  CHECK(layer->bounds().width == 200 && layer->bounds().height == 120);
  const auto path = layer->vector_shape()->path.bounds();
  CHECK(path.has_value() && std::abs(path->top - 100.0) < 0.5 && std::abs(path->bottom - 220.0) < 0.5);
}

void ui_polygon_tool_creates_polygons_and_stars() {
  VectorSettingsGuard settings_guard;
  SettingsValueRestorer saved_sides(QStringLiteral("tools/polygonSides"));
  SettingsValueRestorer saved_inset(QStringLiteral("tools/polygonStarInset"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Polygon);
  auto* sides = window.findChild<QSpinBox*>(QStringLiteral("polygonSidesSpin"));
  auto* inset = window.findChild<QSpinBox*>(QStringLiteral("polygonStarInsetSpin"));
  CHECK(sides != nullptr);
  CHECK(inset != nullptr);
  sides->setValue(6);
  inset->setValue(0);

  // Center-out drag: center (300,300), first vertex at the cursor (300,200).
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(300, 300)),
       canvas->widget_position_for_document_point(QPoint(300, 200)));
  QApplication::processEvents();
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Polygon 1");
  const auto& hexagon = layer->vector_shape()->path.subpaths[0];
  CHECK(hexagon.anchors.size() == 6);
  CHECK(std::abs(hexagon.anchors[0].anchor_x - 300.0) < 1.0);
  CHECK(std::abs(hexagon.anchors[0].anchor_y - 200.0) < 1.0);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(300, 300)), Qt::black, 8));

  // Star inset doubles the point count.
  inset->setValue(50);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(600, 300)),
       canvas->widget_position_for_document_point(QPoint(600, 220)));
  QApplication::processEvents();
  layer = document.find_layer(*document.active_layer_id());
  CHECK(layer->name() == "Polygon 2");
  CHECK(layer->vector_shape()->path.subpaths[0].anchors.size() == 12);
}

void ui_custom_shape_stamps_and_defines() {
  VectorSettingsGuard settings_guard;
  SettingsValueRestorer saved_shape(QStringLiteral("tools/customShapeId"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::CustomShape);
  auto* combo = window.findChild<QComboBox*>(QStringLiteral("customShapeCombo"));
  CHECK(combo != nullptr);
  CHECK(combo->count() >= 17);  // the code-generated builtins
  const auto heart_index = combo->findData(QStringLiteral("shape.builtin.heart"));
  CHECK(heart_index >= 0);
  combo->setCurrentIndex(heart_index);
  QApplication::processEvents();
  CHECK(canvas->custom_shape_path() != nullptr);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(300, 300)));
  QApplication::processEvents();
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Custom Shape 1");
  CHECK(layer->vector_shape()->path.subpaths.size() == 1);
  CHECK(layer->vector_shape()->path.subpaths[0].anchors.size() == 6);
  // The heart lobes cover the upper half's center.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 180)), Qt::black, 8));

  // Define Custom Shape from the active shape layer's path, then stamp it.
  auto& library = patchy::ui::MainWindowTestAccess::custom_shape_library(window);
  QStringList user_shapes_before;
  for (const auto& entry : library.entries()) {
    user_shapes_before.append(entry.storage_id);
  }
  const auto entries_before = combo->count();
  // The action prompts for a name, prefilled with the generated fallback.
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("defineCustomShapeDialog"));
    CHECK(dialog != nullptr);
    auto* name_edit = dialog->findChild<QLineEdit*>(QStringLiteral("defineCustomShapeNameEdit"));
    CHECK(name_edit != nullptr);
    CHECK(!name_edit->text().isEmpty());
    name_edit->setText(QStringLiteral("Test Heart Copy"));
    dialog->accept();
  });
  window.findChild<QAction*>(QStringLiteral("editDefineCustomShapeAction"))->trigger();
  QApplication::processEvents();
  CHECK(combo->count() == entries_before + 1);
  CHECK(combo->currentText() == QStringLiteral("Test Heart Copy"));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(500, 100)),
       canvas->widget_position_for_document_point(QPoint(600, 200)));
  QApplication::processEvents();
  layer = document.find_layer(*document.active_layer_id());
  CHECK(layer->name() == "Custom Shape 2");
  CHECK(layer->vector_shape()->path.subpaths[0].anchors.size() == 6);

  // Remove the shape the test defined so runs never accumulate user entries.
  QStringList added;
  for (const auto& entry : library.entries()) {
    if (!user_shapes_before.contains(entry.storage_id)) {
      added.append(entry.storage_id);
    }
  }
  for (const auto& storage_id : added) {
    CHECK(library.remove_shape(storage_id));
  }
}

// Shift, and the options-bar W/H link, keep a custom shape in its own proportions while it
// is drawn; Alt draws it from the center (a Discord report from Kubuntu: neither kept the
// heart in proportion). The link leaves the Rectangle tool free.
void ui_custom_shape_shift_and_link_keep_proportions() {
  VectorSettingsGuard settings_guard;
  SettingsValueRestorer saved_shape(QStringLiteral("tools/customShapeId"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* link_button = window.findChild<QPushButton*>(QStringLiteral("vectorShapeLinkSizeButton"));
  CHECK(link_button != nullptr);

  require_action(window, "toolCustomShapeAction")->trigger();
  QApplication::processEvents();
  auto* combo = window.findChild<QComboBox*>(QStringLiteral("customShapeCombo"));
  CHECK(combo != nullptr);
  const auto select_shape = [&](const char* id) {
    const auto index = combo->findData(QString::fromLatin1(id));
    CHECK(index >= 0);
    combo->setCurrentIndex(index);
    QApplication::processEvents();
  };
  // The bounds of the shape the last drag drew (Shift at the press adds it to the active
  // shape layer, so read the newest subpath).
  const auto last_shape_bounds = [&]() -> std::optional<patchy::VectorPathBounds> {
    const auto* layer = document.find_layer(*document.active_layer_id());
    if (layer == nullptr || layer->vector_shape() == nullptr || layer->vector_shape()->path.subpaths.empty()) {
      return std::nullopt;
    }
    patchy::VectorPath last;
    last.subpaths.push_back(layer->vector_shape()->path.subpaths.back());
    return last.bounds();
  };
  const auto edge_near = [](double a, double b) { return std::abs(a - b) < 0.75; };

  // The link stays usable for Custom Shape before any shape layer exists.
  CHECK(link_button->isVisible());
  CHECK(link_button->isEnabled());
  CHECK(!link_button->isChecked());

  select_shape("shape.builtin.heart");
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 200));
  auto bounds = last_shape_bounds();
  CHECK(bounds.has_value() && edge_near(bounds->right - bounds->left, 200.0) &&
        edge_near(bounds->bottom - bounds->top, 100.0));

  // Shift: the heart is authored 1:1, so it comes out square, sized to the smaller axis.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(400, 100)),
       canvas->widget_position_for_document_point(QPoint(600, 180)), Qt::ShiftModifier);
  QApplication::processEvents();
  bounds = last_shape_bounds();
  CHECK(bounds.has_value() && edge_near(bounds->left, 400.0) && edge_near(bounds->top, 100.0) &&
        edge_near(bounds->right - bounds->left, 80.0) && edge_near(bounds->bottom - bounds->top, 80.0));

  // Shift with a narrow shape keeps its own proportions (the bolt is 0.6:1).
  select_shape("shape.builtin.lightning");
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 300)),
       canvas->widget_position_for_document_point(QPoint(300, 400)), Qt::ShiftModifier);
  QApplication::processEvents();
  bounds = last_shape_bounds();
  CHECK(bounds.has_value() && edge_near(bounds->right - bounds->left, 60.0) &&
        edge_near(bounds->bottom - bounds->top, 100.0));

  // Alt draws from the center (the from-center rect is an inclusive pixel rect shared with
  // Rectangle and Ellipse, so its far edges may land one pixel in).
  select_shape("shape.builtin.heart");
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(500, 400)),
       canvas->widget_position_for_document_point(QPoint(540, 420)), Qt::AltModifier);
  QApplication::processEvents();
  bounds = last_shape_bounds();
  CHECK(bounds.has_value() && edge_near(bounds->left, 460.0) && edge_near(bounds->top, 380.0) &&
        bounds->right > 538.5 && bounds->right < 540.5 && bounds->bottom > 418.5 && bounds->bottom < 420.5);

  // The link on: a plain drag keeps the proportions too, and a new layer is made.
  const auto layers_before = document.layers().size();
  link_button->setChecked(true);
  QApplication::processEvents();
  CHECK(canvas->custom_shape_keep_proportions());
  shape_drag(*canvas, QPoint(550, 200), QPoint(750, 260));
  CHECK(document.layers().size() == layers_before + 1);
  bounds = last_shape_bounds();
  CHECK(bounds.has_value() && edge_near(bounds->left, 550.0) && edge_near(bounds->top, 200.0) &&
        edge_near(bounds->right - bounds->left, 60.0) && edge_near(bounds->bottom - bounds->top, 60.0));
  // Dragging up and left keeps it square at the press corner (QRect::normalized() moves an
  // up-left drag's edges one pixel in, as for every shape tool).
  shape_drag(*canvas, QPoint(700, 560), QPoint(500, 510));
  bounds = last_shape_bounds();
  CHECK(bounds.has_value() && bounds->right > 698.5 && bounds->right < 700.5 && bounds->bottom > 558.5 &&
        bounds->bottom < 560.5 && bounds->right - bounds->left > 47.5 &&
        edge_near(bounds->right - bounds->left, bounds->bottom - bounds->top));

  // The link leaves the Rectangle tool's drags free.
  require_action(window, "toolRectAction")->trigger();
  QApplication::processEvents();
  shape_drag(*canvas, QPoint(200, 450), QPoint(400, 510));
  bounds = last_shape_bounds();
  // Within a pixel: the drag end snaps to the up-left heart's top edge (y 511).
  CHECK(bounds.has_value() && edge_near(bounds->right - bounds->left, 200.0) &&
        bounds->bottom - bounds->top > 59.5 && bounds->bottom - bounds->top < 61.5);
  link_button->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->custom_shape_keep_proportions());
}

void ui_custom_shape_builtin_geometry_refreshes() {
  // Builtin shape geometry is code-authoritative: a sidecar materialized by
  // an older build (simulated by tampering the stored path into a triangle)
  // is rewritten to the current code geometry by restore_default_shapes(),
  // while a user rename survives and refreshes never count as adds.
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto storage = directory.filePath(QStringLiteral("shapes"));
  const auto heart_id = QStringLiteral("shape.builtin.heart");
  const patchy::ui::BuiltinCustomShape* builtin_heart = nullptr;
  for (const auto& builtin : patchy::ui::builtin_custom_shapes()) {
    if (heart_id == QLatin1String(builtin.id)) {
      builtin_heart = &builtin;
    }
  }
  CHECK(builtin_heart != nullptr);

  QString sidecar_file;
  {
    patchy::ui::CustomShapeLibrary library(storage);
    CHECK(library.restore_default_shapes() ==
          static_cast<int>(patchy::ui::builtin_custom_shapes().size()));
    const auto* heart = library.find_entry_by_shape_id(heart_id);
    CHECK(heart != nullptr);
    // rename_shape re-sorts the entry vector, so `heart` dangles afterwards;
    // capture the storage id first.
    const auto heart_storage_id = heart->storage_id;
    CHECK(library.rename_shape(heart_storage_id, QStringLiteral("My Heart")));
    sidecar_file = storage + QStringLiteral("/") + heart_storage_id + QStringLiteral(".json");
  }

  patchy::VectorPath triangle;
  patchy::PathSubpath triangle_subpath;
  for (const auto& [x, y] : {std::pair{0.5, 0.0}, {1.0, 1.0}, {0.0, 1.0}}) {
    patchy::PathAnchor anchor;
    anchor.anchor_x = anchor.in_x = anchor.out_x = x;
    anchor.anchor_y = anchor.in_y = anchor.out_y = y;
    triangle_subpath.anchors.push_back(anchor);
  }
  triangle.subpaths.push_back(triangle_subpath);
  {
    QFile file(sidecar_file);
    CHECK(file.open(QIODevice::ReadOnly));
    auto object = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    object.insert(QStringLiteral("path"),
                  QString::fromStdString(patchy::serialize_vector_path(triangle)));
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    file.close();
  }

  {
    patchy::ui::CustomShapeLibrary library(storage);
    const auto* heart = library.find_entry_by_shape_id(heart_id);
    CHECK(heart != nullptr);
    CHECK(heart->path == triangle);  // the tampered sidecar loaded as written
    CHECK(library.restore_default_shapes() == 0);
    heart = library.find_entry_by_shape_id(heart_id);
    CHECK(heart != nullptr);
    CHECK(heart->path == builtin_heart->path);
    CHECK(heart->name == QStringLiteral("My Heart"));
  }
  {
    // The refresh reached the sidecar, not just the in-memory entry.
    patchy::ui::CustomShapeLibrary library(storage);
    const auto* heart = library.find_entry_by_shape_id(heart_id);
    CHECK(heart != nullptr);
    CHECK(heart->path == builtin_heart->path);
    CHECK(heart->name == QStringLiteral("My Heart"));
  }
}

void ui_line_arrowheads_extend_the_shape() {
  VectorSettingsGuard settings_guard;
  SettingsValueRestorer saved_start(QStringLiteral("tools/lineArrowStart"));
  SettingsValueRestorer saved_end(QStringLiteral("tools/lineArrowEnd"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Line);
  auto* weight_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorLineWeightSpin"));
  CHECK(weight_spin != nullptr);
  weight_spin->setValue(6);
  // A glyph toggle since GitHub issue 174 (the checkboxes made the row wrap).
  auto* arrow_end = window.findChild<QPushButton*>(QStringLiteral("lineArrowEndButton"));
  CHECK(arrow_end != nullptr && arrow_end->isCheckable());
  arrow_end->setChecked(true);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 200)),
       canvas->widget_position_for_document_point(QPoint(400, 200)));
  QApplication::processEvents();
  auto* layer = document.find_layer(*document.active_layer_id());
  CHECK(layer != nullptr);
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  CHECK(content->path.subpaths.size() == 2);  // body quad + arrowhead
  CHECK(content->origination.size() == 1);
  CHECK(content->origination[0].arrow_end);
  // The head is wider than the 6 px body: bounds reach ~15 px from the axis.
  CHECK(layer->bounds().height > 20);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 200)), Qt::black, 8));
}

void ui_paths_panel_lists_saves_and_targets_paths() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // A Path-mode drag creates the work path; the panel lists it.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));
  CHECK(window.findChild<QWidget*>(QStringLiteral("pathsDock")) != nullptr);
  patchy::ui::MainWindowTestAccess::show_panel(window, QStringLiteral("pathsDock"));
  QApplication::processEvents();
  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  CHECK(paths_list->count() == 1);
  CHECK(paths_list->item(0)->text() == QStringLiteral("Work Path"));
  CHECK(paths_list->item(0)->font().italic());

  // Double-click saves the work path under a generated name. (Emitted via the
  // signal: the tabified dock has no reliable item geometry offscreen.)
  QMetaObject::invokeMethod(paths_list, "itemDoubleClicked", Qt::DirectConnection,
                            Q_ARG(QListWidgetItem*, paths_list->item(0)));
  QApplication::processEvents();
  CHECK(document.work_path() == nullptr);
  CHECK(document.paths().size() == 1);
  CHECK(document.paths().front().name() == "Path 1");
  CHECK(paths_list->count() == 1);
  CHECK(!paths_list->item(0)->font().italic());

  // New Path creates an empty saved path and targets it: the next Path-mode
  // drag lands there instead of a fresh work path.
  window.findChild<QAction*>(QStringLiteral("pathNewAction"))->trigger();
  QApplication::processEvents();
  CHECK(document.paths().size() == 2);
  CHECK(canvas->active_document_path().has_value());
  canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
  shape_drag(*canvas, QPoint(400, 100), QPoint(500, 200));
  const auto* path2 = document.find_path(*canvas->active_document_path());
  CHECK(path2 != nullptr);
  CHECK(path2->path().subpaths.size() == 1);
  CHECK(document.work_path() == nullptr);

  // Clicking empty panel space deselects (back to the work-path routing).
  send_mouse(*paths_list->viewport(), QEvent::MouseButtonPress,
             QPoint(paths_list->viewport()->width() / 2, paths_list->viewport()->height() - 4),
             Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(!canvas->active_document_path().has_value());
}

void ui_paths_panel_fill_stroke_and_make_selection() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(200, 200), QPoint(400, 320));
  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  paths_list->setCurrentRow(0);
  QApplication::processEvents();

  // Fill Path paints the foreground color into the active raster layer (the
  // dialog's default contents).
  canvas->set_primary_color(QColor(30, 160, 40));
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("fillPathDialog"));
    CHECK(dialog != nullptr);
    dialog->findChild<QComboBox*>(QStringLiteral("fillPathContentsCombo"))->setCurrentIndex(0);
    dialog->findChild<QSpinBox*>(QStringLiteral("fillPathOpacitySpin"))->setValue(100);
    dialog->accept();
  });
  window.findChild<QAction*>(QStringLiteral("pathFillAction"))->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(300, 260)), QColor(30, 160, 40), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(150, 260)), Qt::white, 8));

  // Stroke Path replays the path through the BRUSH ENGINE (one undo entry).
  // The targeted path's outline overlay draws with any tool and would cover
  // the stroke's center line in a widget grab, so deselect (empty-space
  // click) before sampling: the overlay must disappear and reveal the paint.
  canvas->set_primary_color(QColor(200, 40, 160));
  canvas->set_brush_size(8);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  const auto undo_depth_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("strokePathDialog"));
    CHECK(dialog != nullptr);
    auto* simulate =
        dialog->findChild<QCheckBox*>(QStringLiteral("strokePathSimulatePressureCheck"));
    CHECK(simulate != nullptr);
    simulate->setChecked(false);
    dialog->accept();
  });
  window.findChild<QAction*>(QStringLiteral("pathStrokeAction"))->trigger();
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_depth_before + 1);
  send_mouse(*paths_list->viewport(), QEvent::MouseButtonPress,
             QPoint(paths_list->viewport()->width() / 2, paths_list->viewport()->height() - 4),
             Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 260)), QColor(200, 40, 160), 12));

  // Make Selection converts the path (re-select the row first).
  paths_list->setCurrentRow(0);
  QApplication::processEvents();
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("makeSelectionDialog"));
    CHECK(dialog != nullptr);
    dialog->accept();
  });
  window.findChild<QAction*>(QStringLiteral("pathMakeSelectionAction"))->trigger();
  QApplication::processEvents();
  const auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(std::abs(selection->x() - 200) <= 1);
  CHECK(std::abs(selection->width() - 200) <= 2);

  // Delete Path removes it and clears the panel targeting.
  window.findChild<QAction*>(QStringLiteral("pathDeleteAction"))->trigger();
  QApplication::processEvents();
  CHECK(document.paths().empty());
  CHECK(paths_list->count() == 0);
}

QDialog* find_top_level_dialog(const QString& object_name) {
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == object_name) {
      return qobject_cast<QDialog*>(widget);
    }
  }
  return nullptr;
}

void ui_shape_appearance_dialog_commits_and_cancels() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));
  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());

  // Commit: enable a 6 px centered stroke and check the live preview fired.
  bool saw_live_preview = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    auto* stroke_width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeStrokeWidthSpin"));
    auto* stroke_align = dialog->findChild<QComboBox*>(QStringLiteral("shapeStrokeAlignCombo"));
    // No Stroke checkbox (GitHub issue 311): a disabled stroke reads Width 0, and the
    // rows stay enabled; a width above 0 turns the stroke on.
    CHECK(dialog->findChild<QCheckBox*>(QStringLiteral("shapeStrokeCheck")) == nullptr);
    CHECK(stroke_width != nullptr);
    CHECK(stroke_align != nullptr);
    CHECK(stroke_width->value() == 0.0);
    CHECK(stroke_width->isEnabled());
    CHECK(stroke_align->isEnabled());
    stroke_width->setValue(6.0);
    stroke_align->setCurrentIndex(1);  // Center
    QApplication::processEvents();
    // Preview applies to the layer immediately: the stroke color defaults to
    // black over the black fill, so check the model rather than pixels.
    const auto* preview_layer = document.find_layer(*layer_id);
    saw_live_preview = preview_layer != nullptr && preview_layer->vector_shape() != nullptr &&
                       preview_layer->vector_shape()->stroke.enabled;
    dialog->accept();
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();
  CHECK(saw_live_preview);
  auto* layer = document.find_layer(*layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->vector_shape()->stroke.enabled);
  CHECK(std::abs(layer->vector_shape()->stroke.width - 6.0) < 1e-9);
  CHECK(layer->vector_shape()->stroke.alignment == patchy::VectorStrokeAlignment::Center);
  CHECK(patchy::layer_vector_block_dirty(*layer));

  // Cancel: change the width, reject, and expect no change to the model.
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    auto* stroke_width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeStrokeWidthSpin"));
    CHECK(stroke_width != nullptr);
    stroke_width->setValue(20.0);
    QApplication::processEvents();
    dialog->reject();
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();
  layer = document.find_layer(*layer_id);
  CHECK(layer != nullptr);
  CHECK(std::abs(layer->vector_shape()->stroke.width - 6.0) < 1e-9);
}

// Issues 327 and 329: the px fields (UnitSpinBox, a QDoubleSpinBox) drew the
// platform style's dark frame because the application sheet gave its field
// border to QSpinBox only, and the stroke Width field lost its top and bottom
// edge once it went from disabled to enabled. Every field in the dialog draws the sheet's
// field border, in both schemes, disabled and enabled.
void ui_shape_appearance_unit_fields_share_spin_box_border() {
  VectorSettingsGuard settings_guard;
  ColorSchemeRestorer restore_scheme;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));

  // The middle of each edge: corners are left to the style.
  const auto edge_colors = [](QWidget& field) {
    const auto shot = field.grab().toImage();
    const int w = shot.width();
    const int h = shot.height();
    return std::array<QColor, 4>{shot.pixelColor(w / 2, 0), shot.pixelColor(w / 2, h - 1),
                                 shot.pixelColor(0, h / 2), shot.pixelColor(w - 1, h / 2)};
  };
  const auto same_color = [](const QColor& a, const QColor& b) {
    return std::abs(a.red() - b.red()) <= 2 && std::abs(a.green() - b.green()) <= 2 &&
           std::abs(a.blue() - b.blue()) <= 2;
  };
  // Reports every mismatch instead of stopping at the first, and after the
  // captures, so a failing run still leaves the pictures.
  bool borders_match = true;
  const auto check_border = [&](const std::string& label, QWidget& unit_field, QWidget& plain_field,
                                const QColor& expected) {
    const auto unit_edges = edge_colors(unit_field);
    const auto plain_edges = edge_colors(plain_field);
    static constexpr std::array<const char*, 4> kEdges{"top", "bottom", "left", "right"};
    for (std::size_t edge = 0; edge < kEdges.size(); ++edge) {
      const bool unit_ok = same_color(unit_edges[edge], expected);
      const bool plain_ok = same_color(plain_edges[edge], expected);
      if (!unit_ok || !plain_ok) {
        fprintf(stderr, "  %s %s edge: unit field %s, plain field %s, want %s\n", label.c_str(), kEdges[edge],
                qPrintable(unit_edges[edge].name()), qPrintable(plain_edges[edge].name()),
                qPrintable(expected.name()));
        borders_match = false;
      }
    }
  };

  for (const auto preference : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer::apply(preference);
    const std::string scheme = preference == patchy::ui::ColorSchemePreference::Light ? "light" : "dark";
    bool sampled = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      auto* stroke_width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeStrokeWidthSpin"));
      auto* stroke_opacity = dialog->findChild<QSpinBox*>(QStringLiteral("shapeStrokeOpacitySpin"));
      auto* feather = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeFeatherSpin"));
      auto* density = dialog->findChild<QSpinBox*>(QStringLiteral("shapeDensitySpin"));
      CHECK(stroke_width != nullptr && stroke_opacity != nullptr);
      CHECK(feather != nullptr && density != nullptr);
      if (stroke_width == nullptr || stroke_opacity == nullptr || feather == nullptr || density == nullptr) {
        dialog->reject();
        return;
      }
      const auto& palette = patchy::ui::theme();
      // The stroke rows no longer grey out (width 0 is the off switch, #311),
      // but the enabled-after-disabled path is the #329 one, so drive it directly.
      stroke_width->setValue(3.0);
      stroke_width->setEnabled(false);
      stroke_opacity->setEnabled(false);
      QApplication::processEvents();
      check_border(scheme + " stroke (disabled)", *stroke_width, *stroke_opacity, palette.field_border_disabled);
      stroke_width->setEnabled(true);
      stroke_opacity->setEnabled(true);
      QApplication::processEvents();
      if (auto* page = dialog->findChild<QWidget*>(QStringLiteral("shapeAppearancePage")); page != nullptr) {
        capture_widget_artifact("shape_appearance_fields_" + scheme, *page);
      }
      check_border(scheme + " stroke (enabled)", *stroke_width, *stroke_opacity, palette.field_border);
      check_border(scheme + " feather", *feather, *density, palette.field_border);
      sampled = true;
      dialog->reject();
    });
    patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
    QApplication::processEvents();
    CHECK(sampled);
  }
  CHECK(borders_match);
}

void ui_new_solid_fill_layer_uses_selection_mask() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  canvas->set_primary_color(QColor(200, 40, 40));
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(300, 220)));
  QApplication::processEvents();
  const auto selection_rect = canvas->selected_document_rect();
  CHECK(selection_rect.has_value());

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
    CHECK(dialog != nullptr);
    dialog->accept();
  });
  auto* action = window.findChild<QAction*>(QStringLiteral("layerNewSolidColorFillAction"));
  CHECK(action != nullptr);
  action->trigger();
  QApplication::processEvents();

  CHECK(document.layers().size() == initial_layers + 1);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Color Fill 1");
  CHECK(patchy::layer_is_vector_shape(*layer));
  CHECK(layer->vector_shape()->path.empty());
  CHECK(layer->vector_shape()->fill.color == (patchy::RgbColor{200, 40, 40}));
  CHECK(layer->mask().has_value());
  CHECK(layer->mask()->bounds.x == selection_rect->x());
  CHECK(layer->mask()->bounds.width == selection_rect->width());
  CHECK(layer->mask()->default_color == 0);
  // Inside the selection the fill shows; outside the mask hides it.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 160)), QColor(200, 40, 40), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(50, 50)), Qt::white, 8));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers);
}

void ui_shape_geometry_edits_live_shape() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(120, 140), QPoint(320, 260));
  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());

  // Edit the live rect's bounds and one corner radius through the dialog.
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    auto* x_spin = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeGeometryXSpin"));
    auto* y_spin = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeGeometryYSpin"));
    auto* width_spin =
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeGeometryWidthSpin"));
    auto* height_spin =
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeGeometryHeightSpin"));
    auto* radius_tl =
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeGeometryRadiusTopLeftSpin"));
    CHECK(x_spin != nullptr && y_spin != nullptr && width_spin != nullptr &&
          height_spin != nullptr && radius_tl != nullptr);
    CHECK(std::abs(x_spin->value() - 120.0) < 0.6);
    CHECK(std::abs(width_spin->value() - 200.0) < 0.6);
    x_spin->setValue(100.0);
    y_spin->setValue(100.0);
    width_spin->setValue(400.0);
    height_spin->setValue(200.0);
    radius_tl->setValue(30.0);
    QApplication::processEvents();
    dialog->accept();
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();

  const auto* layer = std::as_const(document).find_layer(*layer_id);
  CHECK(layer != nullptr);
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  // The shape stays LIVE with the edited parameters.
  CHECK(content->origination.size() == 1);
  CHECK(content->origination[0].kind == patchy::LiveShapeKind::RoundedRectangle);
  CHECK(std::abs(content->origination[0].left - 100.0) < 1e-6);
  CHECK(std::abs(content->origination[0].right - 500.0) < 1e-6);
  CHECK(std::abs(content->origination[0].bottom - 300.0) < 1e-6);
  CHECK(std::abs(content->origination[0].corner_radii[0] - 30.0) < 1e-6);
  // The raster followed: inside is the black fill, the rounded top-left
  // corner is cut, and the old area outside the new bounds stays white.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(300, 200)), Qt::black, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(103, 103)), Qt::white, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(95, 95)), Qt::white, 8));

  // Undo restores the original drag geometry in one step.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(*layer_id);
  CHECK(restored != nullptr);
  CHECK(std::abs(restored->vector_shape()->origination[0].left - 120.0) < 1e-6);
}

void ui_shape_mode_drag_previews_fill_appearance() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);  // Shape mode default
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);

  // Mid-drag the preview shows the ACTUAL fill (default black, full
  // opacity), not the translucent raster-paint preview.
  send_mouse(*canvas, QEvent::MouseButtonPress,
             canvas->widget_position_for_document_point(QPoint(150, 150)), Qt::LeftButton,
             Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove,
             canvas->widget_position_for_document_point(QPoint(400, 300)), Qt::NoButton,
             Qt::LeftButton);
  const auto center = canvas_pixel(*canvas, QPoint(275, 225));
  CHECK(color_close(center, Qt::black, 30));
  send_mouse(*canvas, QEvent::MouseButtonRelease,
             canvas->widget_position_for_document_point(QPoint(400, 300)), Qt::LeftButton,
             Qt::NoButton);
  QApplication::processEvents();
}

// GitHub issue 125: the live preview once drew the stroke as a centered band
// with Qt's default bevel join, so a wide stroke jumped inward and its corners
// changed shape on release. The preview now follows the committed inside
// alignment: stroke pixels sit inside the dragged rectangle, none outside it,
// and the corner is filled to the tip.
void ui_shape_mode_drag_previews_stroke_inside_the_shape() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);  // Shape mode default
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  auto* stroke_width = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorStrokeWidthSpin"));
  CHECK(radius_spin != nullptr && stroke_width != nullptr);
  if (radius_spin == nullptr || stroke_width == nullptr) {
    return;
  }
  radius_spin->setValue(0);
  stroke_width->setValue(16);
  auto& fill = patchy::ui::MainWindowTestAccess::current_vector_fill(window);
  fill = patchy::VectorFill{};
  fill.color = patchy::RgbColor{255, 255, 255};
  auto& stroke = patchy::ui::MainWindowTestAccess::current_vector_stroke_paint(window);
  stroke = patchy::VectorFill{};
  stroke.color = patchy::RgbColor{220, 30, 30};
  QApplication::processEvents();

  const QPoint from(300, 250);
  const QPoint to(600, 450);
  send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(from),
             Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(to),
             Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  const QColor stroke_color(220, 30, 30);
  // Inside the left edge: stroke. Outside it: the untouched white canvas.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(305, 350)), stroke_color, 30));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(295, 350)), Qt::white, 30));
  // The corner is filled right to the tip (a centered bevel left it hollow).
  CHECK(color_close(canvas_pixel(*canvas, QPoint(303, 253)), stroke_color, 30));
  // Past the stroke depth the fill shows.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(330, 350)), Qt::white, 30));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(450, 350)), Qt::white, 30));
  send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(to),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  // The committed shape agrees with the preview at the same points.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(305, 350)), stroke_color, 30));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(295, 350)), Qt::white, 30));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(303, 253)), stroke_color, 30));
}

void ui_paths_panel_clipping_path_toggle() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // Two saved paths.
  canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  mode_combo->setCurrentIndex(1);  // Path
  window.findChild<QAction*>(QStringLiteral("pathNewAction"))->trigger();
  QApplication::processEvents();
  shape_drag(*canvas, QPoint(100, 100), QPoint(200, 200));
  window.findChild<QAction*>(QStringLiteral("pathNewAction"))->trigger();
  QApplication::processEvents();
  shape_drag(*canvas, QPoint(300, 100), QPoint(400, 200));

  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  auto* clipping = window.findChild<QAction*>(QStringLiteral("pathClippingAction"));
  CHECK(paths_list != nullptr && clipping != nullptr);
  CHECK(paths_list->count() == 2);

  paths_list->setCurrentRow(0);
  QApplication::processEvents();
  CHECK(clipping->isEnabled());
  CHECK(!clipping->isChecked());
  clipping->trigger();
  QApplication::processEvents();
  CHECK(document.paths()[0].is_clipping_path());
  CHECK(paths_list->item(0)->font().underline());
  CHECK(!paths_list->item(1)->font().underline());

  // Exclusivity: designating the second path clears the first.
  paths_list->setCurrentRow(1);
  QApplication::processEvents();
  CHECK(!clipping->isChecked());
  clipping->trigger();
  QApplication::processEvents();
  CHECK(!document.paths()[0].is_clipping_path());
  CHECK(document.paths()[1].is_clipping_path());
  CHECK(clipping->isChecked());

  // Toggling again clears it entirely.
  clipping->trigger();
  QApplication::processEvents();
  CHECK(!document.paths()[1].is_clipping_path());
  CHECK(!paths_list->item(1)->font().underline());
}

void ui_path_edits_refresh_panel_thumbnails() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));

  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  CHECK(paths_list->count() == 1);
  const auto before = paths_list->item(0)->icon().pixmap(QSize(42, 30)).toImage();

  // A Direct Select anchor drag mutates the path canvas-side; the panel row
  // thumbnail must follow without any other UI action.
  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(40, 60)));
  QApplication::processEvents();
  CHECK(paths_list->count() == 1);
  const auto after = paths_list->item(0)->icon().pixmap(QSize(42, 30)).toImage();
  CHECK(before != after);
}

void ui_shape_pattern_dropdowns_show_embedded_thumbnails() {
  QTemporaryDir library_dir;
  CHECK(library_dir.isValid());
  patchy::ui::PatternLibrary library(library_dir.path());
  const std::array<QString, 3> ids{QStringLiteral("embedded-only"),
                                 QStringLiteral("shared-pattern-id"),
                                 QStringLiteral("library-only")};
  const std::array<QColor, 3> colors{QColor(220, 40, 60), QColor(30, 180, 70),
                                   QColor(40, 80, 220)};
  const auto tile = [](QColor color) {
    return solid_pixels(8, 8, patchy::PixelFormat::rgba8(), color);
  };
  patchy::PatternStore patterns;
  patterns.adopt({ids[0].toStdString(), "Embedded pattern", tile(colors[0])});
  patterns.adopt({ids[1].toStdString(), "Document version", tile(colors[1])});
  CHECK(!library.add_pattern(QStringLiteral("Library version"), tile(QColor(200, 90, 210)),
                             {}, ids[1]).isEmpty());
  CHECK(!library.add_pattern(QStringLiteral("Library only"), tile(colors[2]), {}, ids[2]).isEmpty());

  patchy::ui::ShapeAppearanceSettings initial;
  initial.fill.kind = patchy::VectorFillKind::Pattern;
  initial.fill.pattern_id = ids[0].toStdString();
  initial.fill.pattern_name = "Embedded pattern";
  initial.stroke.enabled = true;
  initial.stroke.width = 4;
  initial.stroke.content.kind = patchy::VectorFillKind::Pattern;
  initial.stroke.content.pattern_id = ids[1].toStdString();
  initial.stroke.content.pattern_name = "Document version";
  QTimer::singleShot(0, [&] {
    try {
      auto* dialog = find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
      CHECK(dialog != nullptr);
      const std::array<QString, 2> combo_names{QStringLiteral("shapeFillPatternCombo"),
                                             QStringLiteral("shapeStrokePatternCombo")};
      for (std::size_t paint = 0; paint < combo_names.size(); ++paint) {
        auto* combo = dialog->findChild<QComboBox*>(combo_names[paint]);
        CHECK(combo != nullptr);
        CHECK(combo->currentData().toString() == ids[paint]);
        // A shared id appears once, with the document's pixels, even when
        // the library has a different tile under that same id.
        CHECK(combo->count() == 3);
        for (std::size_t pattern = 0; pattern < ids.size(); ++pattern) {
          const auto index = combo->findData(ids[pattern]);
          CHECK(index >= 0);
          const auto icon = combo->itemIcon(index);
          CHECK(!icon.isNull());
          const auto preview = icon.pixmap(combo->iconSize()).toImage();
          CHECK(!preview.isNull());
          CHECK(color_close(preview.pixelColor(preview.width() / 2, preview.height() / 2),
                            colors[pattern], 1));
        }
        combo->showPopup();
        QApplication::processEvents();
        save_widget_artifact(paint == 0 ? "ui_shape_fill_pattern_thumbnails"
                                       : "ui_shape_stroke_pattern_thumbnails",
                             *combo->view()->window());
        combo->hidePopup();
      }
      dialog->accept();
    } catch (...) {
      patchy::ui::unwind_non_modal_dialog_loop(std::current_exception());
    }
  });
  const auto edited = patchy::ui::request_shape_appearance_settings(
      nullptr, {}, initial, {}, nullptr, &library, &patterns, {}, {});
  CHECK(edited.has_value());
  CHECK(edited->fill == initial.fill);
  CHECK(edited->stroke == initial.stroke);
}

// The whole available screen with the dialog and the open list where they sit, so the
// shot shows whether the list fits.
void save_screen_popup_artifact(const std::string& name, QWidget& window, QWidget& popup) {
  ensure_artifact_dir();
  ensure_capture_themed(window);
  ensure_capture_themed(popup);
  const auto screen_area = window.screen()->availableGeometry();
  QImage shot(screen_area.size(), QImage::Format_ARGB32_Premultiplied);
  shot.fill(QColor(96, 96, 104));
  QPainter painter(&shot);
  painter.drawPixmap(window.mapToGlobal(QPoint(0, 0)) - screen_area.topLeft(), window.grab());
  painter.drawPixmap(popup.mapToGlobal(QPoint(0, 0)) - screen_area.topLeft(), popup.grab());
  painter.end();
  CHECK(shot.save(
      QString::fromStdString((std::filesystem::path("test-artifacts") / (name + ".png")).string())));
}

// The checks a long pattern or gradient list must pass once its popup is open (GitHub
// issue 271): the scrolling list form on every style, at most kLongListPopupRows rows,
// inside the screen's available area, the wheel reaching the top and the keyboard
// reaching and picking the first row. `combo` must be scrolled away from row 0 (its
// current row is near the end).
void check_long_list_popup_reaches_first_row(QComboBox& combo, const std::string& artifact,
                                             QWidget& window) {
  combo.showPopup();
  QApplication::processEvents();
  auto* view = combo.view();
  auto* popup = view->window();
  CHECK(popup->isVisible());
  save_screen_popup_artifact(artifact, window, *popup);
  QStyleOptionComboBox option;
  option.initFrom(&combo);
  option.editable = false;
  CHECK(combo.style()->styleHint(QStyle::SH_ComboBox_Popup, &option, &combo) == 0);
  const auto available = combo.screen()->availableGeometry();
  CHECK(available.contains(popup->frameGeometry()));
  const auto row_height = view->sizeHintForRow(0);
  CHECK(row_height > 0);
  CHECK(view->viewport()->height() <= row_height * patchy::ui::kLongListPopupRows);
  auto* scroll_bar = view->verticalScrollBar();
  CHECK(scroll_bar->isVisible());
  CHECK(scroll_bar->value() > 0);
  for (int step = 0; step < 200 && scroll_bar->value() > 0; ++step) {
    const QPointF position(view->viewport()->width() / 2.0, view->viewport()->height() / 2.0);
    QWheelEvent wheel(position, view->viewport()->mapToGlobal(position), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view->viewport(), &wheel);
  }
  CHECK(scroll_bar->value() == 0);
  QTest::keyClick(view, Qt::Key_End);
  QTest::keyClick(view, Qt::Key_Home);
  const auto first = view->model()->index(0, combo.modelColumn(), view->rootIndex());
  CHECK(view->currentIndex() == first);
  CHECK(view->viewport()->rect().contains(view->visualRect(first)));
  save_screen_popup_artifact(artifact + "_top", window, *popup);
  QTest::keyClick(view, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!popup->isVisible());
  CHECK(combo.currentIndex() == 0);
}

// GitHub issue 271: with a document's embedded patterns ahead of the library, the Shape
// Appearance pattern lists ran taller than the screen. Fusion (the Linux desktop style)
// shows a combo's list as a menu sized to every row and placed so the current row covers
// the box; on Wayland it ran off the top and the first rows could not be reached. The
// first embedded pattern also showed its raw Photoshop localization key.
void ui_shape_pattern_list_popup_fits_the_screen() {
  ColorSchemeRestorer restore_scheme;
  QTemporaryDir library_dir;
  CHECK(library_dir.isValid());
  patchy::ui::PatternLibrary library(library_dir.path());
  const std::string tie_dye_key = "$$$/Presets/Patterns/Patterns_pat/TieDye=Tie Dye";
  const std::string tie_dye_id = "b4b2bb21-0b6a-11d7-9c2f-271issue0001";
  patchy::PatternStore patterns;
  patterns.adopt({tie_dye_id, tie_dye_key,
                  solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(200, 60, 160))});
  // Well over a 1080 px screen of rows ahead of the library's own patterns.
  constexpr int kEmbeddedCount = 80;
  for (int index = 1; index < kEmbeddedCount; ++index) {
    const auto shade = 40 + (index * 2);
    patterns.adopt({"embedded-271-" + std::to_string(index), "Embedded " + std::to_string(index),
                    solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(shade, 120, 255 - shade))});
  }
  const auto last_id = patterns.patterns.back().id;
  std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("Fusion")));
  CHECK(fusion != nullptr);

  for (const auto preference :
       {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer::apply(preference);
    const std::string scheme =
        preference == patchy::ui::ColorSchemePreference::Light ? "light" : "dark";
    patchy::ui::ShapeAppearanceSettings initial;
    initial.fill.kind = patchy::VectorFillKind::Pattern;
    initial.fill.pattern_id = last_id;
    initial.fill.pattern_name = patterns.patterns.back().name;
    initial.stroke.enabled = true;
    initial.stroke.width = 4;
    initial.stroke.content.kind = patchy::VectorFillKind::Pattern;
    initial.stroke.content.pattern_id = last_id;
    initial.stroke.content.pattern_name = patterns.patterns.back().name;
    QTimer::singleShot(0, [&] {
      try {
        auto* dialog = find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
        CHECK(dialog != nullptr);
        const std::array<QString, 2> combo_names{QStringLiteral("shapeFillPatternCombo"),
                                                 QStringLiteral("shapeStrokePatternCombo")};
        for (const auto& combo_name : combo_names) {
          auto* combo = dialog->findChild<QComboBox*>(combo_name);
          CHECK(combo != nullptr);
          CHECK(combo->count() >= kEmbeddedCount);
          CHECK(combo->currentData().toString() == QString::fromStdString(last_id));
          // Every desktop style: the Linux one (Fusion) here, the platform's own elsewhere.
          for (auto* style : {fusion.get(), static_cast<QStyle*>(nullptr)}) {
            combo->setStyle(style);
            combo->setCurrentIndex(combo->count() - 1);
            check_long_list_popup_reaches_first_row(
                *combo,
                "ui_issue271_" + combo_name.toStdString() + (style != nullptr ? "_fusion_" : "_native_") +
                    scheme,
                *dialog);
          }
          combo->setStyle(nullptr);
          // The Photoshop preset name shows its English default text, not the key.
          CHECK(combo->itemText(0) == QStringLiteral("Tie Dye"));
        }
        dialog->accept();
      } catch (...) {
        patchy::ui::unwind_non_modal_dialog_loop(std::current_exception());
      }
    });
    const auto edited = patchy::ui::request_shape_appearance_settings(
        nullptr, {}, initial, {}, nullptr, &library, &patterns, {}, {});
    CHECK(edited.has_value());
    // Picking the preset keeps Photoshop's own name for the saved file.
    CHECK(edited->fill.pattern_id == tie_dye_id);
    CHECK(edited->fill.pattern_name == tie_dye_key);
    CHECK(edited->stroke.content.pattern_id == tie_dye_id);
    CHECK(edited->stroke.content.pattern_name == tie_dye_key);
  }
}

void ui_shape_pattern_fill_uses_custom_library_pattern() {
  // Regression: choosing a CUSTOM library pattern (imported image, auto
  // generated id) in the Shape Appearance dialog rendered an empty fill.
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // A shape layer to restyle.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(120, 140), QPoint(320, 260));
  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());

  // A custom pattern exactly like the Pattern Manager's image import: the
  // pattern id is auto-generated by the library.
  patchy::PixelBuffer tile(8, 8, patchy::PixelFormat::rgba8());
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      auto* px = tile.pixel(x, y);
      const bool first = ((x / 4) + (y / 4)) % 2 == 0;
      px[0] = first ? 30 : 220;
      px[1] = first ? 180 : 60;
      px[2] = first ? 60 : 200;
      px[3] = 255;
    }
  }
  // A poisoned document-store entry from an earlier failed attempt: same id,
  // EMPTY tile. Adoption must heal it rather than skip (the empty tile would
  // render the fill transparent forever).
  patchy::PatternResource poisoned;
  poisoned.name = "poisoned";
  const auto storage_id = window.pattern_library().add_pattern(
      QStringLiteral("Custom Import Test"), tile, QStringLiteral("Tests"), QString());
  CHECK(!storage_id.isEmpty());
  const auto* entry = window.pattern_library().find_entry(storage_id);
  CHECK(entry != nullptr);
  const auto pattern_id = entry->id;
  CHECK(!pattern_id.isEmpty());
  poisoned.id = pattern_id.toStdString();
  document.metadata().patterns.patterns.push_back(poisoned);

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    auto* type_combo = dialog->findChild<QComboBox*>(QStringLiteral("shapeFillKindCombo"));
    auto* pattern_combo = dialog->findChild<QComboBox*>(QStringLiteral("shapeFillPatternCombo"));
    CHECK(type_combo != nullptr && pattern_combo != nullptr);
    const auto pattern_type_index = type_combo->findText(QObject::tr("Pattern"));
    CHECK(pattern_type_index >= 0);
    type_combo->setCurrentIndex(pattern_type_index);
    const auto pattern_index = pattern_combo->findData(pattern_id);
    CHECK(pattern_index >= 0);
    pattern_combo->setCurrentIndex(pattern_index);
    // Placement params: angle, offsets, and the align-with-layer anchor.
    auto* angle_spin = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapePatternAngleSpin"));
    auto* offset_x_spin =
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapePatternOffsetXSpin"));
    auto* offset_y_spin =
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapePatternOffsetYSpin"));
    auto* align_check = dialog->findChild<QCheckBox*>(QStringLiteral("shapePatternAlignCheck"));
    CHECK(angle_spin != nullptr && offset_x_spin != nullptr && offset_y_spin != nullptr &&
          align_check != nullptr);
    CHECK(align_check->isChecked());  // pattern_linked defaults on
    angle_spin->setValue(30.0);
    offset_x_spin->setValue(5.0);
    offset_y_spin->setValue(-2.0);
    align_check->setChecked(false);
    // Pattern STROKE paint: enable the stroke, switch its paint to Pattern.
    auto* paint_combo = dialog->findChild<QComboBox*>(QStringLiteral("shapeStrokePaintCombo"));
    auto* stroke_pattern_combo =
        dialog->findChild<QComboBox*>(QStringLiteral("shapeStrokePatternCombo"));
    auto* stroke_color_button = dialog->findChild<QWidget*>(QStringLiteral("shapeStrokeColorButton"));
    CHECK(paint_combo != nullptr && stroke_pattern_combo != nullptr && stroke_color_button != nullptr);
    CHECK(stroke_color_button->isVisible());  // solid paint shows the color row
    const auto pattern_paint_index =
        paint_combo->findData(static_cast<int>(patchy::VectorFillKind::Pattern));
    CHECK(pattern_paint_index >= 0);
    paint_combo->setCurrentIndex(pattern_paint_index);
    CHECK(!stroke_color_button->isVisible());  // pattern paint swaps the rows
    CHECK(stroke_pattern_combo->isVisible());
    const auto stroke_pattern_index = stroke_pattern_combo->findData(pattern_id);
    CHECK(stroke_pattern_index >= 0);
    stroke_pattern_combo->setCurrentIndex(stroke_pattern_index);
    auto* stroke_width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeStrokeWidthSpin"));
    CHECK(stroke_width != nullptr);
    stroke_width->setValue(6.0);
    QApplication::processEvents();
    dialog->accept();
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();

  const auto* layer = std::as_const(document).find_layer(*layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->vector_shape()->fill.kind == patchy::VectorFillKind::Pattern);
  CHECK(layer->vector_shape()->fill.pattern_id == pattern_id.toStdString());
  CHECK(std::abs(layer->vector_shape()->fill.pattern_angle_degrees - 30.0) < 1e-9);
  CHECK(std::abs(layer->vector_shape()->fill.pattern_phase_x - 5.0) < 1e-9);
  CHECK(std::abs(layer->vector_shape()->fill.pattern_phase_y + 2.0) < 1e-9);
  CHECK(!layer->vector_shape()->fill.pattern_linked);
  CHECK(layer->vector_shape()->stroke.enabled);
  CHECK(layer->vector_shape()->stroke.content.kind == patchy::VectorFillKind::Pattern);
  CHECK(layer->vector_shape()->stroke.content.pattern_id == pattern_id.toStdString());
  // The healthy tile healed the poisoned store entry and the raster shows the
  // checker colors inside the shape.
  const auto* adopted = document.metadata().patterns.find(pattern_id.toStdString());
  CHECK(adopted != nullptr);
  CHECK(!adopted->tile.empty());
  const auto inside = canvas_pixel(*canvas, QPoint(220, 200));
  const bool matches_checker = color_close(inside, QColor(30, 180, 60), 12) ||
                               color_close(inside, QColor(220, 60, 200), 12);
  CHECK(matches_checker);

  CHECK(window.pattern_library().remove_pattern(storage_id));
}

void ui_options_bar_pattern_fill_creates_pattern_shape() {
  // The options-bar Fill picker's backing mirror can hold a pattern; a drawn
  // shape then carries it, the tile is adopted into the document store (the
  // Patt-block hard-refusal rule), and the mirror seeds later documents too.
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  patchy::PixelBuffer tile(8, 8, patchy::PixelFormat::rgba8());
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      auto* px = tile.pixel(x, y);
      const bool first = ((x / 4) + (y / 4)) % 2 == 0;
      px[0] = first ? 20 : 230;
      px[1] = first ? 160 : 80;
      px[2] = first ? 80 : 190;
      px[3] = 255;
    }
  }
  const auto pattern_id = QStringLiteral("patchy-test-options-bar-pattern");
  if (const auto* stale = window.pattern_library().find_entry_by_pattern_id(pattern_id);
      stale != nullptr) {
    CHECK(window.pattern_library().remove_pattern(stale->storage_id));
  }
  const auto storage_id = window.pattern_library().add_pattern(
      QStringLiteral("Options Bar Pattern"), tile, QStringLiteral("Tests"), pattern_id);
  CHECK(!storage_id.isEmpty());

  // The tool ACTION (not canvas->set_tool) carries the application-level tool
  // state that later documents inherit.
  require_action_by_text(window, QStringLiteral("Rect"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  auto& fill = patchy::ui::MainWindowTestAccess::current_vector_fill(window);
  fill.kind = patchy::VectorFillKind::Pattern;
  fill.pattern_id = pattern_id.toStdString();
  fill.pattern_name = "Options Bar Pattern";
  patchy::ui::MainWindowTestAccess::update_vector_swatch_icons(window);

  shape_drag(*canvas, QPoint(120, 140), QPoint(320, 260));
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  const auto* layer = std::as_const(document).find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->vector_shape() != nullptr);
  CHECK(layer->vector_shape()->fill.kind == patchy::VectorFillKind::Pattern);
  CHECK(layer->vector_shape()->fill.pattern_id == pattern_id.toStdString());
  const auto* adopted = document.metadata().patterns.find(pattern_id.toStdString());
  CHECK(adopted != nullptr);
  CHECK(!adopted->tile.empty());
  const auto inside = canvas_pixel(*canvas, QPoint(220, 200));
  CHECK(color_close(inside, QColor(20, 160, 80), 12) || color_close(inside, QColor(230, 80, 190), 12));

  // The mirror is application-wide: a brand-new document's first drag uses it
  // (the current_* mirror rule; the commit pulls, never a per-canvas copy).
  accept_new_document_dialog(320, 240);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  auto* new_canvas = require_canvas(window);
  CHECK(new_canvas != canvas);
  CHECK(new_canvas->tool() == patchy::ui::CanvasTool::Rectangle);
  shape_drag(*new_canvas, QPoint(40, 40), QPoint(200, 160));
  auto* new_document = patchy::ui::MainWindowTestAccess::document_for_canvas(window, new_canvas);
  CHECK(new_document != nullptr);
  const auto new_active = new_document->active_layer_id();
  CHECK(new_active.has_value());
  const auto* new_layer = std::as_const(*new_document).find_layer(*new_active);
  CHECK(new_layer != nullptr && new_layer->vector_shape() != nullptr);
  CHECK(new_layer->vector_shape()->fill.kind == patchy::VectorFillKind::Pattern);
  CHECK(new_document->metadata().patterns.find(pattern_id.toStdString()) != nullptr);

  CHECK(window.pattern_library().remove_pattern(storage_id));
}

void ui_options_bar_edits_selected_shape_appearance() {
  // Photoshop's live options bar: with a shape layer selected, the appearance
  // controls show for the path-select tools, edit the LAYER (one undo entry
  // per gesture), and stick as the next-shape defaults.
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  require_action_by_text(window, QStringLiteral("Rect"))->trigger();
  QApplication::processEvents();
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));
  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());
  const auto base_depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  // The tool ACTION updates the application-level tool the options bar keys
  // its live-edit gating on.
  require_action_by_text(window, QStringLiteral("Path Select"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::PathSelect);
  auto* stroke_width = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorStrokeWidthSpin"));
  auto* stroke_swatch = window.findChild<QToolButton*>(QStringLiteral("vectorStrokeSwatchButton"));
  auto* stroke_label = window.findChild<QLabel*>(QStringLiteral("vectorStrokeWidthLabel"));
  CHECK(stroke_width != nullptr);
  CHECK(stroke_swatch != nullptr && stroke_label != nullptr);
  // The appearance controls show for the select tool because the active layer
  // is an editable shape. No Stroke checkbox (GitHub issue 311): the strokeless
  // layer reads 0 and the controls stay enabled.
  CHECK(window.findChild<QCheckBox*>(QStringLiteral("vectorStrokeCheck")) == nullptr);
  CHECK(stroke_width->isVisible());
  CHECK(stroke_width->value() == 0.0);  // synced from the strokeless layer
  CHECK(stroke_width->isEnabled() && stroke_swatch->isEnabled() && stroke_label->isEnabled());

  // A width turns the stroke on and applies to the selected shape (debounced,
  // one undo step); wait for it so no queued edit outranks later syncs.
  stroke_width->setValue(8.0);
  CHECK(process_events_until([&] {
    const auto* layer = std::as_const(document).find_layer(*layer_id);
    return layer != nullptr && layer->vector_shape() != nullptr && layer->vector_shape()->stroke.enabled &&
           std::abs(layer->vector_shape()->stroke.width - 8.0) < 1e-9;
  }));
  {
    const auto* layer = std::as_const(document).find_layer(*layer_id);
    CHECK(layer->vector_shape()->stroke.enabled);
    CHECK(std::abs(layer->vector_shape()->stroke.width - 8.0) < 1e-9);
    CHECK(patchy::layer_vector_block_dirty(*layer));
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == base_depth + 1);

  // A fill edit through the mirror (the popup pickers' backing state).
  auto& fill = patchy::ui::MainWindowTestAccess::current_vector_fill(window);
  fill.kind = patchy::VectorFillKind::Solid;
  fill.color = patchy::RgbColor{200, 30, 30};
  CHECK(patchy::ui::MainWindowTestAccess::apply_options_bar_appearance(window));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == base_depth + 2);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 160)), QColor(200, 30, 30), 8));
  // Re-applying identical values is a no-op and pushes nothing.
  CHECK(!patchy::ui::MainWindowTestAccess::apply_options_bar_appearance(window));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == base_depth + 2);

  // Sticky: the next drawn shape inherits the edited appearance.
  require_action_by_text(window, QStringLiteral("Rect"))->trigger();
  QApplication::processEvents();
  shape_drag(*canvas, QPoint(400, 300), QPoint(500, 400));
  const auto second_id = document.active_layer_id();
  CHECK(second_id.has_value() && *second_id != *layer_id);
  const auto* second = std::as_const(document).find_layer(*second_id);
  CHECK(second != nullptr && second->vector_shape() != nullptr);
  CHECK(second->vector_shape()->stroke.enabled);
  CHECK(std::abs(second->vector_shape()->stroke.width - 8.0) < 1e-9);
  CHECK(second->vector_shape()->fill.color == (patchy::RgbColor{200, 30, 30}));

  // Undo unwinds the appearance edits one gesture at a time.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();  // second shape
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();  // fill color
  QApplication::processEvents();
  {
    const auto* layer = std::as_const(document).find_layer(*layer_id);
    CHECK(layer != nullptr && layer->vector_shape() != nullptr);
    CHECK(layer->vector_shape()->fill.color == (patchy::RgbColor{0, 0, 0}));
    CHECK(layer->vector_shape()->stroke.enabled);  // stroke gesture still applied
  }
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();  // the width that turned it on
  QApplication::processEvents();
  CHECK(stroke_width->value() == 0.0);
  CHECK(stroke_width->isEnabled() && stroke_swatch->isEnabled() && stroke_label->isEnabled());
  require_action_by_text(window, QStringLiteral("Redo"))->trigger();
  QApplication::processEvents();
  CHECK(std::abs(stroke_width->value() - 8.0) < 1e-9);
}

void ui_shape_options_group_stroke_controls_and_end_with_appearance() {
  VectorSettingsGuard settings_guard;
  SettingsValueRestorer restore_units(QStringLiteral("view/rulerUnits"));
  patchy::ui::MainWindow window;
  show_window(window);
  window.resize(1800, 800);
  patchy::ui::MainWindowTestAccess::set_ruler_unit(window, patchy::ui::MeasurementUnit::Pixels);
  auto* stroke_width = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorStrokeWidthSpin"));
  auto* stroke_swatch = window.findChild<QToolButton*>(QStringLiteral("vectorStrokeSwatchButton"));
  auto* stroke_label = window.findChild<QLabel*>(QStringLiteral("vectorStrokeWidthLabel"));
  auto* appearance = window.findChild<QPushButton*>(QStringLiteral("vectorAppearanceButton"));
  auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
  CHECK(stroke_width && stroke_swatch && stroke_label && appearance && options);
  CHECK(window.findChild<QCheckBox*>(QStringLiteral("vectorStrokeCheck")) == nullptr);  // issue 311
  CHECK(stroke_label->text() == QStringLiteral("Stroke"));  // no colon on the bar (issue 127)
  CHECK(stroke_width->property(patchy::ui::kScrubHandleInstalledProperty).toBool());
  stroke_width->setValue(3.0);

  for (const char* tool : {"Rect", "Ellipse", "Line", "Polygon", "Custom Shape", "Pen"}) {
    require_action_by_text(window, QString::fromLatin1(tool))->trigger();
    QApplication::processEvents();
    CHECK(appearance->isVisible());
    CHECK(stroke_width->isVisible() && stroke_swatch->isVisible() && stroke_label->isVisible());
    const auto appearance_position = appearance->mapTo(options, QPoint());
    for (int i = 0; i < options->layout()->count(); ++i) {
      auto* widget = options->layout()->itemAt(i)->widget();
      if (widget == nullptr || widget == appearance || !widget->isVisible()) {
        continue;
      }
      const auto position = widget->mapTo(options, QPoint());
      CHECK(position.y() + widget->height() <= appearance_position.y() ||
            (position.y() <= appearance_position.y() + appearance->height() &&
             position.x() + widget->width() <= appearance_position.x()));
    }
    // 0 is no stroke and the last real width is kept for when the stroke comes back.
    stroke_width->setValue(0.0);
    CHECK(!patchy::ui::MainWindowTestAccess::current_shape_appearance(window).stroke.enabled);
    CHECK(stroke_width->isEnabled() && stroke_swatch->isEnabled() && stroke_label->isEnabled());
    stroke_width->setValue(3.0);
    CHECK(patchy::ui::MainWindowTestAccess::current_shape_appearance(window).stroke.enabled);
    CHECK(stroke_width->value() == 3.0);
  }
  require_action_by_text(window, QStringLiteral("Rect"))->trigger();
  QApplication::processEvents();
  stroke_width->setValue(0.0);
  save_widget_artifact("shape-options-stroke-disabled", *options);
  stroke_width->setValue(3.0);
  const auto origin = stroke_label->rect().center();
  const auto destination = origin + QPoint(QApplication::startDragDistance() + 5, 0);
  send_mouse(*stroke_label, QEvent::MouseButtonPress, origin, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*stroke_label, QEvent::MouseMove, destination, Qt::NoButton, Qt::LeftButton);
  send_mouse(*stroke_label, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(stroke_width->value() == 3.0 + (QApplication::startDragDistance() + 5) * stroke_width->singleStep());
  save_widget_artifact("shape-options-stroke-enabled", *options);
}

[[nodiscard]] double swatch_relative_luminance(const QColor& color) {
  const auto channel = [](double value) {
    return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) +
         0.0722 * channel(color.blueF());
}

[[nodiscard]] double swatch_contrast_ratio(const QColor& first, const QColor& second) {
  const double a = swatch_relative_luminance(first);
  const double b = swatch_relative_luminance(second);
  return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

// Renders an options-bar swatch button over the bar color and checks that the
// pixels which read against the bar form a closed box: a chip the same color as
// the bar is found by its outline alone.
void check_swatch_outline_reads_on_bar(QToolButton& button, const char* label) {
  constexpr double kMinimumContrast = 3.0;
  const QColor bar = patchy::ui::theme().toolbar_bg;
  QImage shot(button.size(), QImage::Format_ARGB32_Premultiplied);
  shot.fill(bar);
  button.render(&shot, QPoint(), QRegion(), QWidget::DrawChildren);
  const auto reads = [&](int x, int y) {
    return swatch_contrast_ratio(shot.pixelColor(x, y), bar) >= kMinimumContrast;
  };
  QRect box;
  for (int y = 0; y < shot.height(); ++y) {
    for (int x = 0; x < shot.width(); ++x) {
      if (reads(x, y)) {
        box = box.united(QRect(x, y, 1, 1));
      }
    }
  }
  int top = 0;
  int bottom = 0;
  int left = 0;
  int right = 0;
  for (int x = box.left(); !box.isNull() && x <= box.right(); ++x) {
    top += reads(x, box.top()) ? 1 : 0;
    bottom += reads(x, box.bottom()) ? 1 : 0;
  }
  for (int y = box.top(); !box.isNull() && y <= box.bottom(); ++y) {
    left += reads(box.left(), y) ? 1 : 0;
    right += reads(box.right(), y) ? 1 : 0;
  }
  // The rounded corners are antialiased below the floor, so each side is asked
  // for its straight run only.
  const bool closed = box.width() >= 14 && box.height() >= 9 && top * 2 >= box.width() &&
                      bottom * 2 >= box.width() && left * 5 >= box.height() * 2 &&
                      right * 5 >= box.height() * 2;
  if (!closed) {
    double strongest = 1.0;
    for (int y = 0; y < shot.height(); ++y) {
      for (int x = 0; x < shot.width(); ++x) {
        strongest = std::max(strongest, swatch_contrast_ratio(shot.pixelColor(x, y), bar));
      }
    }
    std::fprintf(stderr,
                 "  %s: outline box %dx%d, sides top %d bottom %d left %d right %d, strongest contrast %.2f\n",
                 label, box.width(), box.height(), top, bottom, left, right, strongest);
    shot.save(QStringLiteral("test-artifacts/shape-swatch-outline-failure.png"));
  }
  CHECK(closed);
}

// GitHub issue 89: a black stroke swatch had no edge on Dark's options bar, and
// a white one barely had one on Light's.
// GitHub issue 311 follow-up: there is no Stroke checkbox, the width is the switch. 0 draws no
// stroke and keeps the last real width in the model, so no PSD ever stores a zero width; a PSD
// layer whose stroke is off reads 0 in the bar; typing a width again brings the stroke back in
// the stroke paint it had.
void ui_shape_stroke_width_zero_means_no_stroke() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  require_action_by_text(window, QStringLiteral("Rect"))->trigger();
  QApplication::processEvents();
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  auto* stroke_width = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorStrokeWidthSpin"));
  CHECK(radius_spin != nullptr && stroke_width != nullptr);
  if (radius_spin == nullptr || stroke_width == nullptr) {
    return;
  }
  CHECK(stroke_width->minimum() == 0.0);
  radius_spin->setValue(0);
  auto& fill = patchy::ui::MainWindowTestAccess::current_vector_fill(window);
  fill = patchy::VectorFill{};
  fill.color = patchy::RgbColor{255, 255, 255};
  auto& stroke = patchy::ui::MainWindowTestAccess::current_vector_stroke_paint(window);
  stroke = patchy::VectorFill{};
  stroke.color = patchy::RgbColor{220, 30, 30};

  // 10 px: an inside stroke along the left edge.
  stroke_width->setValue(10.0);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));
  const auto stroked_id = document.active_layer_id();
  CHECK(stroked_id.has_value());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(103, 160)), QColor(220, 30, 30), 12));

  // 0: no stroke; the fill reaches the edge, the model keeps the 10 px width. The Background is
  // made active first, or the bar would live-edit the stroked shape (Photoshop's rule).
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list == nullptr) {
    return;
  }
  layer_list->setCurrentItem(require_layer_item(*layer_list, QStringLiteral("Background")));
  QApplication::processEvents();
  stroke_width->setValue(0.0);
  QApplication::processEvents();
  shape_drag(*canvas, QPoint(400, 100), QPoint(600, 220));
  const auto plain_id = document.active_layer_id();
  CHECK(plain_id.has_value() && plain_id != stroked_id);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(403, 160)), QColor(255, 255, 255), 12));
  if (!stroked_id || !plain_id) {
    return;
  }
  {
    const auto* plain = std::as_const(document).find_layer(*plain_id);
    CHECK(plain != nullptr && plain->vector_shape() != nullptr);
    CHECK(!plain->vector_shape()->stroke.enabled);
    CHECK(std::abs(plain->vector_shape()->stroke.width - 10.0) < 1e-9);
  }

  // The strokeless layer reads 0 in the bar; a width brings its stroke back in the paint it had.
  require_action_by_text(window, QStringLiteral("Path Select"))->trigger();
  QApplication::processEvents();
  CHECK(stroke_width->value() == 0.0);
  CHECK(stroke_width->isEnabled());
  stroke_width->setValue(6.0);
  CHECK(process_events_until([&] {
    const auto* layer = std::as_const(document).find_layer(*plain_id);
    return layer != nullptr && layer->vector_shape() != nullptr && layer->vector_shape()->stroke.enabled &&
           std::abs(layer->vector_shape()->stroke.width - 6.0) < 1e-9;
  }));
  {
    const auto* restroked = std::as_const(document).find_layer(*plain_id);
    CHECK(restroked != nullptr && restroked->vector_shape() != nullptr &&
          restroked->vector_shape()->stroke.content.color == (patchy::RgbColor{220, 30, 30}));
  }
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(!std::as_const(document).find_layer(*plain_id)->vector_shape()->stroke.enabled);
  CHECK(stroke_width->value() == 0.0);

  // PSD round trip: the stroked layer keeps its 10 px stroke; the strokeless one writes no
  // vstk (as Photoshop does for a filled shape without a stroke), so no zero width is ever
  // stored, and it reads back off with a real default width.
  ensure_artifact_dir();
  const auto path = QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(
                                  QStringLiteral("issue_311_zero_width_stroke.psd")))
                        .absoluteFilePath();
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, patchy::ui::to_filesystem_path(path));
  const auto reread = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(path));
  int disabled = 0;
  int enabled = 0;
  for (const auto& layer : reread.layers()) {
    const auto* shape = std::as_const(layer).vector_shape();
    if (shape == nullptr) continue;
    std::printf("  reread %s: stroke %s, width %.2f\n", layer.name().c_str(), shape->stroke.enabled ? "on" : "off",
                shape->stroke.width);
    CHECK(shape->stroke.width > 0.0);
    if (shape->stroke.enabled) {
      CHECK(std::abs(shape->stroke.width - 10.0) < 1e-6);
      ++enabled;
    } else {
      ++disabled;
    }
  }
  CHECK(disabled == 1 && enabled == 1);

  // Opened, the strokeless layer reads 0 in the bar and the stroked one its width.
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Rect"))->trigger();
  require_action_by_text(window, QStringLiteral("Path Select"))->trigger();
  QApplication::processEvents();
  layer_list->setCurrentItem(require_layer_item(*layer_list, QStringLiteral("Rectangle 1")));
  QApplication::processEvents();
  CHECK(std::abs(stroke_width->value() - 10.0) < 1e-9);
  layer_list->setCurrentItem(require_layer_item(*layer_list, QStringLiteral("Rectangle 2")));
  QApplication::processEvents();
  auto& opened = patchy::ui::MainWindowTestAccess::document(window);
  const auto active = opened.active_layer_id();
  CHECK(active.has_value());
  if (!active) {
    return;
  }
  const auto* active_layer = std::as_const(opened).find_layer(*active);
  CHECK(active_layer != nullptr && active_layer->vector_shape() != nullptr &&
        !active_layer->vector_shape()->stroke.enabled);
  CHECK(stroke_width->value() == 0.0);
}

void ui_shape_swatches_outline_contrasts_with_options_bar() {
  VectorSettingsGuard settings_guard;
  ColorSchemeRestorer restore_scheme(patchy::ui::ColorSchemePreference::Dark);
  patchy::ui::MainWindow window;
  show_window(window);
  window.resize(1800, 800);
  require_action_by_text(window, QStringLiteral("Rect"))->trigger();
  QApplication::processEvents();
  auto* stroke_width = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorStrokeWidthSpin"));
  auto* fill_swatch = window.findChild<QToolButton*>(QStringLiteral("vectorFillSwatchButton"));
  auto* stroke_swatch = window.findChild<QToolButton*>(QStringLiteral("vectorStrokeSwatchButton"));
  CHECK(stroke_width != nullptr && fill_swatch != nullptr && stroke_swatch != nullptr);
  if (stroke_width == nullptr || fill_swatch == nullptr || stroke_swatch == nullptr) {
    return;
  }
  stroke_width->setValue(3.0);
  QApplication::processEvents();
  CHECK(fill_swatch->isVisible() && stroke_swatch->isVisible() && stroke_swatch->isEnabled());

  auto& fill = patchy::ui::MainWindowTestAccess::current_vector_fill(window);
  auto& stroke = patchy::ui::MainWindowTestAccess::current_vector_stroke_paint(window);
  const auto set_paints = [&](std::uint8_t value) {
    fill = patchy::VectorFill{};
    fill.kind = patchy::VectorFillKind::Solid;
    fill.color = patchy::RgbColor{value, value, value};
    stroke = fill;
  };

  set_paints(0);
  patchy::ui::MainWindowTestAccess::update_vector_swatch_icons(window);
  QApplication::processEvents();
  const auto fill_size = fill_swatch->size();
  const auto stroke_size = stroke_swatch->size();
  check_swatch_outline_reads_on_bar(*fill_swatch, "dark, black fill");
  check_swatch_outline_reads_on_bar(*stroke_swatch, "dark, black stroke");
  save_widget_artifact("shape-swatch-black-on-dark", *stroke_swatch->parentWidget());

  // No refresh call here: the scheme change itself has to redraw the outline in
  // the new scheme's color.
  set_paints(255);
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Light);
  check_swatch_outline_reads_on_bar(*fill_swatch, "light, white fill");
  check_swatch_outline_reads_on_bar(*stroke_swatch, "light, white stroke");
  save_widget_artifact("shape-swatch-white-on-light", *stroke_swatch->parentWidget());
  CHECK(fill_swatch->size() == fill_size);
  CHECK(stroke_swatch->size() == stroke_size);
}

void ui_new_fill_layer_clips_to_targeted_path() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // A Path-mode rect drag creates AND targets the work path.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(200, 200), QPoint(400, 320));
  CHECK(canvas->panel_path_targeted());

  canvas->set_primary_color(QColor(40, 90, 200));
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
    CHECK(dialog != nullptr);
    dialog->accept();
  });
  window.findChild<QAction*>(QStringLiteral("layerNewSolidColorFillAction"))->trigger();
  QApplication::processEvents();

  // Photoshop's "current path" rule: the new fill layer's shape path IS the
  // targeted path, so the fill clips to it.
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  const auto* layer = std::as_const(document).find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(patchy::layer_is_vector_shape(*layer));
  CHECK(layer->vector_shape()->path.subpaths.size() == 1);
  CHECK(layer->vector_shape()->path.subpaths[0].anchors.size() == 4);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(300, 260)), QColor(40, 90, 200), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(100, 100)), Qt::white, 8));
}

void ui_new_gradient_fill_layer_spans_canvas() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_primary_color(Qt::black);
  canvas->set_secondary_color(Qt::white);
  // The gradient flow opens the appearance dialog right after creating the
  // layer; accept it unchanged.
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    dialog->accept();
  });
  auto* action = window.findChild<QAction*>(QStringLiteral("layerNewGradientFillAction"));
  CHECK(action != nullptr);
  action->trigger();
  QApplication::processEvents();

  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Gradient Fill 1");
  CHECK(layer->vector_shape()->fill.kind == patchy::VectorFillKind::Gradient);
  // Photoshop's 90-degree linear gradient points up: stop 0 (foreground
  // black) fills the bottom, the background white the top.
  const auto top = canvas_pixel(*canvas, QPoint(512, 8));
  const auto bottom = canvas_pixel(*canvas, QPoint(512, 760));
  CHECK(bottom.red() + 60 < top.red());
}

void ui_pen_hover_shows_context_cursor_badges() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);

  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  canvas->setFocus();
  // Cursor state is driven by hover moves carrying the event's own modifiers
  // (the offscreen platform never clears the global keyboard state).
  const auto hover = [&](QPoint document_point, Qt::KeyboardModifiers modifiers) {
    send_mouse(*canvas, QEvent::MouseMove,
               canvas->widget_position_for_document_point(document_point), Qt::NoButton,
               Qt::NoButton, modifiers);
    return canvas->cursor();
  };

  const auto plain = hover(QPoint(500, 400), Qt::NoModifier);
  CHECK(plain.shape() == Qt::BitmapCursor);
  CHECK(plain.hotSpot() == QPoint(10, 10));
  const auto plain_image = plain.pixmap().toImage();

  const auto add = hover(QPoint(200, 100), Qt::NoModifier);  // top-edge segment
  CHECK(add.shape() == Qt::BitmapCursor);
  const auto add_image = add.pixmap().toImage();
  CHECK(add_image != plain_image);

  const auto del = hover(QPoint(100, 100), Qt::NoModifier);  // anchor
  const auto delete_image = del.pixmap().toImage();
  CHECK(delete_image != plain_image);
  CHECK(delete_image != add_image);

  const auto convert = hover(QPoint(100, 100), Qt::AltModifier);  // Alt over anchor
  const auto convert_image = convert.pixmap().toImage();
  CHECK(convert_image != delete_image);
  CHECK(convert_image != plain_image);

  // Ctrl with a stationary pointer flips to the temporary Direct Select arrow
  // and back on release (the folded-modifier filter path).
  hover(QPoint(500, 400), Qt::NoModifier);
  send_key_press(*canvas, Qt::Key_Control, Qt::NoModifier);
  CHECK(canvas->cursor().pixmap().toImage() == patchy::ui::direct_select_cursor().pixmap().toImage());
  send_key_release(*canvas, Qt::Key_Control, Qt::ControlModifier);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);

  // Mid-session, hovering the first anchor advertises the close action.
  pen_click(*canvas, QPoint(500, 300));
  pen_click(*canvas, QPoint(650, 300));
  pen_click(*canvas, QPoint(575, 420));
  CHECK(canvas->pen_session_active());
  const auto close = hover(QPoint(500, 300), Qt::NoModifier);
  const auto close_image = close.pixmap().toImage();
  CHECK(close_image != plain_image);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_pen_ctrl_drag_direct_selects_committed_anchor_then_draws() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);

  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  // Ctrl+click on an anchor selects it - it must never delete (the plain-click
  // pen behavior) or start a session.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(100, 100)), Qt::ControlModifier);
  QApplication::processEvents();
  auto* layer = document.find_layer(layer_id);
  CHECK(layer->vector_shape()->path.subpaths[0].anchors.size() == 4);
  CHECK(canvas->path_edit_has_selection());
  CHECK(!canvas->pen_session_active());

  // Ctrl+drag moves the anchor with Direct Select semantics.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(60, 70)), Qt::ControlModifier);
  QApplication::processEvents();
  layer = document.find_layer(layer_id);
  const auto* content = layer->vector_shape();
  CHECK(content->path.subpaths[0].anchors.size() == 4);
  CHECK(std::abs(content->path.subpaths[0].anchors[0].anchor_x - 60.0) < 1.0);
  CHECK(std::abs(content->path.subpaths[0].anchors[0].anchor_y - 70.0) < 1.0);
  CHECK(content->origination.empty());
  CHECK(!canvas->pen_session_active());

  // Releasing Ctrl leaves the Pen drawing as usual.
  pen_click(*canvas, QPoint(600, 80));
  CHECK(canvas->pen_session_active());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();

  // The whole Ctrl-drag is one history entry.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  layer = document.find_layer(layer_id);
  CHECK(std::abs(layer->vector_shape()->path.subpaths[0].anchors[0].anchor_x - 100.0) < 0.5);
  CHECK(layer->vector_shape()->origination.size() == 1);
}

void ui_pen_ctrl_drag_moves_in_progress_session_anchor() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path

  pen_click(*canvas, QPoint(100, 100));
  pen_click(*canvas, QPoint(300, 100));
  pen_click(*canvas, QPoint(200, 250));
  CHECK(canvas->pen_session_active());

  // Ctrl+drag the middle anchor of the in-progress path; the session survives
  // and no anchor is added.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(300, 100)),
       canvas->widget_position_for_document_point(QPoint(340, 140)), Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->pen_session_active());

  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->pen_session_active());
  const auto* work = document.work_path();
  CHECK(work != nullptr);
  CHECK(work->path().subpaths.size() == 1);
  const auto& anchors = work->path().subpaths[0].anchors;
  CHECK(anchors.size() == 3);
  CHECK(std::abs(anchors[1].anchor_x - 340.0) < 1.5);
  CHECK(std::abs(anchors[1].anchor_y - 140.0) < 1.5);
  // The handles rode along with the anchor.
  CHECK(std::abs(anchors[1].in_x - 340.0) < 1.5);
  CHECK(std::abs(anchors[1].out_x - 340.0) < 1.5);
}

// Ctrl with a shape tool is a temporary Path Select (GitHub issue 312): the arrow
// cursor, click or marquee selects and deselects whole shapes, a drag moves the
// selected shape as one undo step, and the next plain drag draws again.
void ui_shape_tool_ctrl_acts_as_path_select() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto first_id = make_rect_shape_layer(window, *canvas);  // 100,100 - 300,220
  shape_drag(*canvas, QPoint(400, 300), QPoint(500, 380));
  const auto second_id = document.active_layer_id();
  CHECK(second_id.has_value() && *second_id != first_id);
  const auto layer_count = document.layers().size();
  canvas->setFocus();

  const auto widget = [&](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  const auto ctrl_drag = [&](QPoint from, QPoint to) {
    drag(*canvas, widget(from), widget(to), Qt::ControlModifier);
    QApplication::processEvents();
  };
  const auto path_select_image = patchy::ui::path_select_cursor().pixmap().toImage();

  // Ctrl with a stationary pointer shows the Path Select arrow, and releasing it
  // gives the drawing cursor back.
  send_mouse(*canvas, QEvent::MouseMove, widget(QPoint(600, 450)), Qt::NoButton, Qt::NoButton,
             Qt::NoModifier);
  const auto drawing_image = canvas->cursor().pixmap().toImage();
  CHECK(drawing_image != path_select_image);
  send_key_press(*canvas, Qt::Key_Control, Qt::NoModifier);
  CHECK(canvas->cursor().pixmap().toImage() == path_select_image);
  send_key_release(*canvas, Qt::Key_Control, Qt::ControlModifier);
  CHECK(canvas->cursor().pixmap().toImage() == drawing_image);
  // A hover move carrying Ctrl shows it too.
  send_mouse(*canvas, QEvent::MouseMove, widget(QPoint(610, 450)), Qt::NoButton, Qt::NoButton,
             Qt::ControlModifier);
  CHECK(canvas->cursor().pixmap().toImage() == path_select_image);
  send_mouse(*canvas, QEvent::MouseMove, widget(QPoint(600, 450)), Qt::NoButton, Qt::NoButton,
             Qt::NoModifier);

  // Ctrl+click inside the other shape picks its layer and selects the shape; nothing is drawn.
  ctrl_drag(QPoint(200, 160), QPoint(200, 160));
  CHECK(document.active_layer_id() == first_id);
  CHECK(canvas->path_edit_has_selection());
  CHECK(document.layers().size() == layer_count);
  CHECK(document.find_layer(first_id)->vector_shape()->path.subpaths.size() == 1);
  save_widget_artifact("ui_shape_tool_ctrl_acts_as_path_select", window);

  // Ctrl+click on empty canvas deselects.
  ctrl_drag(QPoint(650, 80), QPoint(650, 80));
  CHECK(!canvas->path_edit_has_selection());

  // A Ctrl marquee touching the shape selects it.
  ctrl_drag(QPoint(50, 50), QPoint(150, 150));
  CHECK(canvas->path_edit_has_selection());
  ctrl_drag(QPoint(650, 80), QPoint(650, 80));
  CHECK(!canvas->path_edit_has_selection());

  // Ctrl+drag on the shape moves it by (50, 30).
  ctrl_drag(QPoint(200, 160), QPoint(250, 190));
  const auto* content = document.find_layer(first_id)->vector_shape();
  CHECK(content->path.subpaths.size() == 1);
  CHECK(std::abs(content->path.subpaths[0].anchors[0].anchor_x - 150.0) < 1.0);
  CHECK(std::abs(content->path.subpaths[0].anchors[0].anchor_y - 130.0) < 1.0);
  CHECK(document.layers().size() == layer_count);

  // The move is one history entry.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  content = document.find_layer(first_id)->vector_shape();
  CHECK(std::abs(content->path.subpaths[0].anchors[0].anchor_x - 100.0) < 0.5);
  CHECK(document.layers().size() == layer_count);

  // Without Ctrl the tool draws a new shape layer again.
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Rectangle);
  shape_drag(*canvas, QPoint(420, 60), QPoint(520, 140));
  CHECK(document.layers().size() == layer_count + 1);
}

// Ctrl+Alt-drag with a shape tool duplicates the shape like Path Select's Alt-drag
// (GitHub issue 312): the copy joins the same shape layer, paints with its fill and
// stroke like the original, and one Undo removes it.
void ui_shape_tool_ctrl_alt_drag_duplicates_a_filled_shape() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  canvas->set_view_zoom_centered(1.0);
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));
  shape_drag(*canvas, QPoint(420, 300), QPoint(520, 380));  // another layer is active
  const auto second_id = document.active_layer_id();
  CHECK(second_id.has_value());
  const auto layer_count = document.layers().size();
  patchy::LayerId first_id{};
  for (const auto& layer : std::as_const(document).layers()) {
    if (layer.vector_shape() != nullptr && layer.id() != *second_id) {
      first_id = layer.id();
    }
  }
  const auto alpha_at = [&](QPoint point) {
    const auto* layer = std::as_const(document).find_layer(first_id);
    CHECK(layer != nullptr);
    const auto bounds = layer->bounds();
    const QPoint local(point.x() - bounds.x, point.y() - bounds.y);
    const auto& pixels = layer->pixels();
    if (local.x() < 0 || local.y() < 0 || local.x() >= pixels.width() || local.y() >= pixels.height() ||
        pixels.format().channels < 4) {
      return 0;
    }
    return static_cast<int>(pixels.pixel(local.x(), local.y())[3]);
  };
  const auto canvas_color_at = [&](QPoint point) {
    return canvas->grab().toImage().pixelColor(canvas->widget_position_for_document_point(point));
  };
  const auto original_color = canvas_color_at(QPoint(200, 160));
  const auto background_color = canvas_color_at(QPoint(340, 290));
  CHECK(original_color != background_color);
  canvas->setFocus();

  // Ctrl+Alt-drag from inside the first rectangle by (60, 80).
  const auto start = canvas->widget_position_for_document_point(QPoint(200, 160));
  const auto end = canvas->widget_position_for_document_point(QPoint(260, 240));
  const auto modifiers = Qt::ControlModifier | Qt::AltModifier;
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, modifiers);
  send_mouse(*canvas, QEvent::MouseMove, (start + end) / 2, Qt::NoButton, Qt::LeftButton, modifiers);
  send_mouse(*canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, modifiers);
  send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, modifiers);
  QApplication::processEvents();
  save_widget_artifact("ui_shape_tool_ctrl_alt_drag_duplicates_a_filled_shape", window);

  CHECK(document.layers().size() == layer_count);
  CHECK(document.active_layer_id() == first_id);
  const auto* content = std::as_const(document).find_layer(first_id)->vector_shape();
  CHECK(content != nullptr && content->path.subpaths.size() == 2);
  if (content == nullptr || content->path.subpaths.size() != 2) {
    return;
  }
  CHECK(content->path.subpaths[0].shape_group != content->path.subpaths[1].shape_group);
  CHECK(content->path.subpaths[1].op == content->path.subpaths[0].op);
  // The copy sits the drag (60, 80) away from the original. Measured from the original, not from fixed
  // coordinates: after earlier tests in the full suite the drawn rectangle can land a pixel or two off (100,100).
  const auto& original_anchor = content->path.subpaths[0].anchors[0];
  CHECK(std::abs(content->path.subpaths[1].anchors[0].anchor_x - (original_anchor.anchor_x + 60.0)) < 1.0);
  CHECK(std::abs(content->path.subpaths[1].anchors[0].anchor_y - (original_anchor.anchor_y + 80.0)) < 1.0);
  CHECK(document.work_path() == nullptr || document.work_path()->path().subpaths.empty());
  // The copy (160,180)-(360,300) paints like the original, in the layer and on the canvas.
  CHECK(alpha_at(QPoint(340, 290)) > 200);
  CHECK(alpha_at(QPoint(200, 160)) > 200);
  CHECK(canvas_color_at(QPoint(340, 290)) == original_color);

  // One Undo removes the copy.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(std::as_const(document).find_layer(first_id)->vector_shape()->path.subpaths.size() == 1);
  CHECK(alpha_at(QPoint(340, 290)) == 0);
  CHECK(canvas_color_at(QPoint(340, 290)) == background_color);
  CHECK(document.layers().size() == layer_count);
}

// Shift held at a shape tool's press adds that one shape to the targeted shape layer, even
// a different kind of shape (GitHub issue 313); the drag still constrains, the options-bar
// Combine setting stays New Layer, and the add is one undo step.
void ui_shape_tool_shift_press_adds_to_shape_layer() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* combine_combo = window.findChild<QComboBox*>(QStringLiteral("vectorCombineCombo"));
  CHECK(combine_combo != nullptr);
  const auto layers_at_start = document.layers().size();
  const auto bounds_of = [](const patchy::PathSubpath& subpath) {
    double left = 1e9;
    double top = 1e9;
    double right = -1e9;
    double bottom = -1e9;
    for (const auto& anchor : subpath.anchors) {
      left = std::min(left, anchor.anchor_x);
      top = std::min(top, anchor.anchor_y);
      right = std::max(right, anchor.anchor_x);
      bottom = std::max(bottom, anchor.anchor_y);
    }
    return QRectF(QPointF(left, top), QPointF(right, bottom));
  };

  // No shape layer targeted: Shift at the press makes a new layer as before, constrained.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(0);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(300, 220)), Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_at_start + 1);
  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());
  {
    const auto& path = document.find_layer(*layer_id)->vector_shape()->path;
    CHECK(path.subpaths.size() == 1);
    const auto square = bounds_of(path.subpaths[0]);
    CHECK(square.width() > 50.0);
    CHECK(std::abs(square.width() - square.height()) < 1.0);
  }

  // The shape layer is targeted: Shift at an Ellipse press adds a circle to it.
  canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
  CHECK(combine_combo->currentIndex() == 0);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(400, 250)),
       canvas->widget_position_for_document_point(QPoint(520, 320)), Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_at_start + 1);
  CHECK(document.active_layer_id() == layer_id);
  {
    const auto* content = document.find_layer(*layer_id)->vector_shape();
    CHECK(content->path.subpaths.size() == 2);
    CHECK(content->path.subpaths[1].op == patchy::PathCombineOp::Add);
    CHECK(content->path.subpaths[1].shape_group != content->path.subpaths[0].shape_group);
    const auto circle = bounds_of(content->path.subpaths[1]);
    CHECK(circle.left() >= 399.0);
    CHECK(std::abs(circle.width() - circle.height()) < 1.0);
    CHECK(content->origination.size() == 2);
    CHECK(content->origination[1].kind == patchy::LiveShapeKind::Ellipse);
  }
  CHECK(combine_combo->currentIndex() == 0);
  save_widget_artifact("ui_shape_tool_shift_press_adds_to_shape_layer", window);

  // One undo step takes the added shape back off the layer.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_at_start + 1);
  CHECK(document.find_layer(*layer_id)->vector_shape()->path.subpaths.size() == 1);

  // The Combine setting never changed: a plain drag still makes a new layer.
  shape_drag(*canvas, QPoint(400, 250), QPoint(520, 320));
  CHECK(document.layers().size() == layers_at_start + 2);
  CHECK(document.find_layer(*layer_id)->vector_shape()->path.subpaths.size() == 1);
}

void ui_pen_session_start_shows_hint_message() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  const auto hint = patchy::ui::resolve_modifier_names(QStringLiteral(
      "Click to add points, drag for curves; Shift keeps 45 degrees. Click the first point to close; "
      "Enter or %CTRL%+click away ends an open path; Esc cancels."));

  make_rect_shape_layer(window, *canvas);
  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  // Editing the existing path (add anchor on the top edge) is not a session:
  // no hint appears.
  pen_click(*canvas, QPoint(200, 100));
  CHECK(!canvas->pen_session_active());
  CHECK(window.statusBar()->currentMessage() != hint);

  // The first anchor of a new path shows the hint once.
  pen_click(*canvas, QPoint(600, 400));
  CHECK(canvas->pen_session_active());
  CHECK(window.statusBar()->currentMessage() == hint);
  pen_click(*canvas, QPoint(700, 400));
  CHECK(window.statusBar()->currentMessage() == hint);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_vector_mode_combo_disables_pixels_for_vector_only_tools() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  auto* model = qobject_cast<QStandardItemModel*>(mode_combo->model());
  CHECK(model != nullptr);

  require_action(window, "toolRectAction")->trigger();
  QApplication::processEvents();
  mode_combo->setCurrentIndex(2);  // Pixels, the app-wide persisted mode
  QApplication::processEvents();
  CHECK(canvas->vector_tool_mode() == patchy::ui::VectorToolMode::Pixels);

  // The Pen never rasterizes: Pixels greys out and the combo displays the
  // effective mode (Path) without rewriting the setting.
  require_action(window, "toolPenAction")->trigger();
  QApplication::processEvents();
  CHECK(mode_combo->currentIndex() == 1);
  CHECK(!model->item(2)->isEnabled());
  CHECK(canvas->vector_tool_mode() == patchy::ui::VectorToolMode::Pixels);

  // Pen commits land on the work path, matching the displayed mode.
  pen_click(*canvas, QPoint(100, 100));
  pen_click(*canvas, QPoint(300, 100));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  const auto* work = document.work_path();
  CHECK(work != nullptr);
  CHECK(work->path().subpaths.size() == 1);

  // A raster-capable shape tool shows the real setting again.
  require_action(window, "toolRectAction")->trigger();
  QApplication::processEvents();
  CHECK(mode_combo->currentIndex() == 2);
  CHECK(model->item(2)->isEnabled());
}

void ui_layer_context_menu_offers_shape_appearance_for_shape_layers() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr && layer_list->count() > 0);

  // Open the context menu on the active (shape) layer and record its actions.
  const auto collect_menu_actions = [&](QStringList& names) {
    bool saw_menu = false;
    int poll_attempts = 0;
    QTimer poller;
    QObject::connect(&poller, &QTimer::timeout, [&] {
      if (++poll_attempts > 500) {
        poller.stop();
        return;
      }
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* menu = qobject_cast<QMenu*>(widget);
        if (menu != nullptr && menu->objectName() == QStringLiteral("layerContextMenu") &&
            menu->isVisible()) {
          saw_menu = true;
          for (auto* action : menu->actions()) {
            names << action->objectName();
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
    return saw_menu;
  };

  QStringList shape_layer_actions;
  CHECK(collect_menu_actions(shape_layer_actions));
  // Edit Layer Styles... stays first; the appearance editor rides second.
  CHECK(shape_layer_actions.indexOf(QStringLiteral("layerContextEditShapeAppearanceAction")) == 1);

  // A plain pixel layer (after undoing the shape) offers no appearance entry.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  QStringList pixel_layer_actions;
  CHECK(collect_menu_actions(pixel_layer_actions));
  CHECK(!pixel_layer_actions.contains(QStringLiteral("layerContextEditShapeAppearanceAction")));
}

void ui_paths_panel_actions_follow_row_selection() {
  // A real mouse click updates the list's CURRENT item before committing the
  // selection, so the panel must refresh its action states on selectionChanged
  // too; clicking a row used to leave Fill/Stroke/Make Selection/Delete stuck
  // disabled (and the post-open blanket enable left them wrongly enabled).
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));

  auto* list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  auto* new_action = window.findChild<QAction*>(QStringLiteral("pathNewAction"));
  auto* fill = window.findChild<QAction*>(QStringLiteral("pathFillAction"));
  auto* stroke = window.findChild<QAction*>(QStringLiteral("pathStrokeAction"));
  auto* make_selection = window.findChild<QAction*>(QStringLiteral("pathMakeSelectionAction"));
  auto* delete_action = window.findChild<QAction*>(QStringLiteral("pathDeleteAction"));
  CHECK(list != nullptr && new_action != nullptr && fill != nullptr && stroke != nullptr &&
        make_selection != nullptr && delete_action != nullptr);
  CHECK(list->count() == 1);  // the work path

  // Drawing auto-targets the work path row (Photoshop highlights it as soon
  // as you draw), so the row commands start enabled.
  CHECK(canvas->active_document_path().has_value());
  CHECK(new_action->isEnabled());
  CHECK(fill->isEnabled());
  CHECK(stroke->isEnabled());
  CHECK(make_selection->isEnabled());
  CHECK(delete_action->isEnabled());

  // Clicking the empty area below the rows deselects and disables them.
  send_mouse(*list->viewport(), QEvent::MouseButtonPress,
             QPoint(list->viewport()->width() / 2, list->viewport()->height() - 4), Qt::LeftButton,
             Qt::LeftButton);
  QApplication::processEvents();
  CHECK(!canvas->active_document_path().has_value());
  CHECK(new_action->isEnabled());
  CHECK(!fill->isEnabled());
  CHECK(!stroke->isEnabled());
  CHECK(!make_selection->isEnabled());
  CHECK(!delete_action->isEnabled());

  // A real mouse click on the row (press + release) re-enables the row
  // commands: the panel must refresh action states on selectionChanged, not
  // just currentItemChanged (the click updates the current item first).
  const auto row_rect = list->visualItemRect(list->item(0));
  send_mouse(*list->viewport(), QEvent::MouseButtonPress, row_rect.center(), Qt::LeftButton,
             Qt::LeftButton);
  send_mouse(*list->viewport(), QEvent::MouseButtonRelease, row_rect.center(), Qt::LeftButton,
             Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->active_document_path().has_value());
  CHECK(fill->isEnabled());
  CHECK(stroke->isEnabled());
  CHECK(make_selection->isEnabled());
  CHECK(delete_action->isEnabled());
}

// True when a pixel near the document point reads as the path-overlay accent
// (116, 192, 255) in a canvas grab: distinctly blue-leaning against the
// white/black canvas content these tests use.
bool accent_overlay_near(patchy::ui::CanvasWidget& canvas, QPoint document_point) {
  const auto image = canvas.grab().toImage();
  const auto center = canvas.widget_position_for_document_point(document_point);
  for (int dy = -3; dy <= 3; ++dy) {
    for (int dx = -3; dx <= 3; ++dx) {
      const QPoint probe(center.x() + dx, center.y() + dy);
      if (!image.rect().contains(probe)) {
        continue;
      }
      const auto color = image.pixelColor(probe);
      if (color.blue() >= 200 && color.blue() - color.red() >= 60 &&
          color.green() > color.red()) {
        return true;
      }
    }
  }
  return false;
}

void ui_paths_panel_target_shows_overlay_with_any_tool() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  // A Path-mode drag creates the work path AND targets its row (Photoshop
  // highlights Work Path as soon as you draw).
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));
  CHECK(canvas->active_document_path().has_value());
  CHECK(canvas->panel_path_targeted());

  // The outline stays visible with ANY tool (the Photoshop target-path rule).
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  QApplication::processEvents();
  CHECK(accent_overlay_near(*canvas, QPoint(100, 160)));  // left edge midpoint

  // Clicking empty panel space hides it.
  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  send_mouse(*paths_list->viewport(), QEvent::MouseButtonPress,
             QPoint(paths_list->viewport()->width() / 2, paths_list->viewport()->height() - 4),
             Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(!canvas->panel_path_targeted());
  CHECK(!accent_overlay_near(*canvas, QPoint(100, 160)));

  // Re-selecting the row shows it again; Escape under a path tool (with no
  // anchor selection to clear first) dismisses the panel targeting. A path
  // tool still displays its edit-target fallback (it would edit the work
  // path), so switch to Move to see the outline actually gone.
  paths_list->setCurrentRow(0);
  QApplication::processEvents();
  CHECK(canvas->panel_path_targeted());
  CHECK(accent_overlay_near(*canvas, QPoint(100, 160)));
  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->panel_path_targeted());
  CHECK(paths_list->selectedItems().isEmpty());
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  QApplication::processEvents();
  CHECK(!accent_overlay_near(*canvas, QPoint(100, 160)));

  // View > Show Target Path (Ctrl+Shift+H) hides the overlay without
  // touching the targeting, and re-shows it on toggle-back.
  paths_list->setCurrentRow(0);
  QApplication::processEvents();
  CHECK(accent_overlay_near(*canvas, QPoint(100, 160)));
  auto* toggle = window.findChild<QAction*>(QStringLiteral("viewToggleTargetPathAction"));
  CHECK(toggle != nullptr);
  toggle->trigger();
  QApplication::processEvents();
  CHECK(!accent_overlay_near(*canvas, QPoint(100, 160)));
  CHECK(canvas->panel_path_targeted());
  toggle->trigger();
  QApplication::processEvents();
  CHECK(accent_overlay_near(*canvas, QPoint(100, 160)));
}

void ui_shape_layer_auto_targets_path_row_until_dismissed() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // A Shape-mode drag creates a shape layer; its transient row auto-targets
  // so the outline shows on canvas under the path and shape tools.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(120, 140), QPoint(320, 260));
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  CHECK(patchy::layer_is_vector_shape(*document.find_layer(*active)));
  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  CHECK(paths_list->count() == 1);  // the transient layer row
  CHECK(!paths_list->selectedItems().isEmpty());
  CHECK(canvas->panel_path_targeted());
  QApplication::processEvents();
  CHECK(accent_overlay_near(*canvas, QPoint(120, 200)));  // shape left edge

  // Dismissing hides it, and a panel refresh must NOT resurrect it while the
  // same layer stays active.
  send_mouse(*paths_list->viewport(), QEvent::MouseButtonPress,
             QPoint(paths_list->viewport()->width() / 2, paths_list->viewport()->height() - 4),
             Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(!canvas->panel_path_targeted());
  patchy::ui::MainWindowTestAccess::refresh_paths_panel(window);
  QApplication::processEvents();
  CHECK(!canvas->panel_path_targeted());
  CHECK(paths_list->selectedItems().isEmpty());

  // An explicit row click re-shows it (and clears the dismissal).
  paths_list->setCurrentRow(0);
  QApplication::processEvents();
  CHECK(canvas->panel_path_targeted());
  patchy::ui::MainWindowTestAccess::refresh_paths_panel(window);
  QApplication::processEvents();
  CHECK(canvas->panel_path_targeted());
  CHECK(!paths_list->selectedItems().isEmpty());
}

void ui_shape_layer_row_shows_vector_badge() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(120, 140), QPoint(320, 260));

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* shape_item = require_layer_item(*layer_list, QStringLiteral("Rectangle 1"));
  auto* shape_row = layer_list->itemWidget(shape_item);
  CHECK(shape_row != nullptr);
  auto* badge = shape_row->findChild<QToolButton*>(QStringLiteral("layerVectorBadgeButton"));
  CHECK(badge != nullptr);
  CHECK(!badge->icon().isNull());

  // Plain raster layers carry no vector badge.
  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  auto* background_row = layer_list->itemWidget(background_item);
  CHECK(background_row != nullptr);
  CHECK(background_row->findChild<QToolButton*>(QStringLiteral("layerVectorBadgeButton")) ==
        nullptr);

  // Clicking the badge opens the Shape Appearance dialog. The badge defers
  // its open by one timer tick, so queue the reject AFTER the click: the
  // open fires first and blocks in the dialog loop, then the reject timer
  // fires inside that loop.
  bool dialog_seen = false;
  badge->click();
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    dialog_seen = dialog != nullptr;
    if (dialog != nullptr) {
      dialog->reject();
    }
  });
  QApplication::processEvents();
  CHECK(dialog_seen);
}

void ui_paths_panel_ctrl_click_loads_selection() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(200, 200), QPoint(400, 320));
  CHECK(!canvas->has_selection());

  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  CHECK(paths_list->count() == 1);
  const auto row_rect = paths_list->visualItemRect(paths_list->item(0));
  send_mouse(*paths_list->viewport(), QEvent::MouseButtonPress, row_rect.center(), Qt::LeftButton,
             Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*paths_list->viewport(), QEvent::MouseButtonRelease, row_rect.center(), Qt::LeftButton,
             Qt::NoButton, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  const auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(std::abs(selection->x() - 200) <= 1);
  CHECK(std::abs(selection->y() - 200) <= 1);
  CHECK(std::abs(selection->width() - 200) <= 2);
  CHECK(std::abs(selection->height() - 120) <= 2);

  // Ctrl+Enter on the canvas loads the targeted row too (the PS staple).
  canvas->clear_selection();
  CHECK(!canvas->has_selection());
  paths_list->setCurrentRow(0);
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->has_selection());
}

void ui_paths_panel_duplicate_and_reorder() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // Two saved paths via New Path + Path-mode drags (each drag lands in the
  // targeted saved path).
  canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  mode_combo->setCurrentIndex(1);  // Path
  window.findChild<QAction*>(QStringLiteral("pathNewAction"))->trigger();
  QApplication::processEvents();
  shape_drag(*canvas, QPoint(100, 100), QPoint(200, 200));
  window.findChild<QAction*>(QStringLiteral("pathNewAction"))->trigger();
  QApplication::processEvents();
  shape_drag(*canvas, QPoint(300, 100), QPoint(400, 200));
  CHECK(document.paths().size() == 2);

  // Duplicate Path copies the selected row under "<name> copy" and targets it.
  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  CHECK(paths_list->count() == 2);
  paths_list->setCurrentRow(0);  // "Path 1"
  QApplication::processEvents();
  window.findChild<QAction*>(QStringLiteral("pathDuplicateAction"))->trigger();
  QApplication::processEvents();
  CHECK(document.paths().size() == 3);
  CHECK(document.paths().back().name() == "Path 1 copy");
  CHECK(document.paths().back().path().subpaths.size() == 1);
  CHECK(canvas->active_document_path().has_value());
  CHECK(*canvas->active_document_path() == document.paths().back().id());

  // Undo removes the copy.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.paths().size() == 2);

  // Drag-reorder (via the model, like the channel panel test): move "Path 2"
  // above "Path 1" and check the document order followed.
  CHECK(paths_list->model()->moveRow(QModelIndex(), 1, QModelIndex(), 0));
  QApplication::processEvents();
  QApplication::processEvents();  // the reorder commit is deferred one tick
  CHECK(document.paths().size() == 2);
  CHECK(document.paths()[0].name() == "Path 2");
  CHECK(document.paths()[1].name() == "Path 1");

  // Undo restores the original order.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.paths()[0].name() == "Path 1");
  CHECK(document.paths()[1].name() == "Path 2");
}

void ui_fill_path_supports_patterns() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(200, 200), QPoint(400, 320));

  // Install a deterministic two-color checker pattern (the test settings
  // sandbox leaves the library unpopulated, so bundled entries are not
  // guaranteed here).
  patchy::PixelBuffer tile(8, 8, patchy::PixelFormat::rgba8());
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      auto* px = tile.pixel(x, y);
      const bool first = ((x / 4) + (y / 4)) % 2 == 0;
      px[0] = first ? 10 : 240;
      px[1] = first ? 200 : 40;
      px[2] = first ? 30 : 220;
      px[3] = 255;
    }
  }
  const auto pattern_id = QStringLiteral("patchy-test-fill-path-pattern");
  if (const auto* stale = window.pattern_library().find_entry_by_pattern_id(pattern_id);
      stale != nullptr) {
    CHECK(window.pattern_library().remove_pattern(stale->storage_id));
  }
  const auto storage_id = window.pattern_library().add_pattern(
      QStringLiteral("Fill Path Test"), tile, QStringLiteral("Tests"), pattern_id);
  CHECK(!storage_id.isEmpty());

  // A deliberately absurd foreground proves the pattern pixels were used.
  canvas->set_primary_color(QColor(255, 0, 255));
  bool pattern_selected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("fillPathDialog"));
    CHECK(dialog != nullptr);
    auto* contents = dialog->findChild<QComboBox*>(QStringLiteral("fillPathContentsCombo"));
    auto* pattern = dialog->findChild<QComboBox*>(QStringLiteral("fillPathPatternCombo"));
    CHECK(contents != nullptr && pattern != nullptr);
    // The pattern row greys out unless Pattern contents are selected.
    contents->setCurrentIndex(0);
    CHECK(!pattern->isEnabled());
    contents->setCurrentIndex(2);  // Pattern
    CHECK(pattern->isEnabled());
    const auto index = pattern->findData(pattern_id);
    pattern_selected = index >= 0;
    if (pattern_selected) {
      pattern->setCurrentIndex(index);
    }
    dialog->findChild<QSpinBox*>(QStringLiteral("fillPathOpacitySpin"))->setValue(100);
    dialog->accept();
  });
  window.findChild<QAction*>(QStringLiteral("pathFillAction"))->trigger();
  QApplication::processEvents();
  CHECK(pattern_selected);

  // The active Paint Layer gained checker pixels inside the path (never the
  // magenta foreground); outside stays untouched. Document-origin tiling makes
  // the expected color at (304, 260) exact: tile (0, 4) = the second color.
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  const auto* paint_layer = std::as_const(document).find_layer(*active);
  CHECK(paint_layer != nullptr);
  {
    const auto& pixels = paint_layer->pixels();
    CHECK(pixels.format().channels == 4);
    const auto* inside = pixels.pixel(304, 260);
    CHECK(inside[3] == 255U);
    CHECK(inside[0] == 240U && inside[1] == 40U && inside[2] == 220U);
    const auto* outside = pixels.pixel(150, 260);
    CHECK(outside[3] == 0U);
  }

  // Undo the fill, then fill again with a 4 px X offset: the tile grid
  // shifts so the same document pixel lands on the OTHER checker cell.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  if (auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
      paths_list != nullptr && paths_list->count() > 0) {
    paths_list->setCurrentRow(0);  // keep the work path targeted for the refill
    QApplication::processEvents();
  }
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("fillPathDialog"));
    CHECK(dialog != nullptr);
    auto* contents = dialog->findChild<QComboBox*>(QStringLiteral("fillPathContentsCombo"));
    auto* offset_x = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("fillPathPatternOffsetXSpin"));
    auto* angle = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("fillPathPatternAngleSpin"));
    CHECK(contents != nullptr && offset_x != nullptr && angle != nullptr);
    // The placement rows grey out with non-pattern contents, like the combo.
    contents->setCurrentIndex(0);
    CHECK(!offset_x->isEnabled());
    contents->setCurrentIndex(2);
    CHECK(offset_x->isEnabled());
    CHECK(angle->isEnabled());
    offset_x->setValue(4.0);
    dialog->accept();
  });
  window.findChild<QAction*>(QStringLiteral("pathFillAction"))->trigger();
  QApplication::processEvents();
  {
    const auto* refetched = std::as_const(document).find_layer(*active);
    CHECK(refetched != nullptr);
    const auto& pixels = refetched->pixels();
    const auto* inside = pixels.pixel(304, 260);
    CHECK(inside[3] == 255U);
    CHECK(inside[0] == 10U && inside[1] == 200U && inside[2] == 30U);
  }

  CHECK(window.pattern_library().remove_pattern(storage_id));
}

void ui_stroke_path_simulate_pressure_tapers() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // Pin the pen-input mapping the taper rides (the user's preferences could
  // disable pressure->size).
  auto pen_settings = canvas->pen_input_settings();
  pen_settings.pressure_size = true;
  pen_settings.pressure_size_min_percent = 20;
  pen_settings.pressure_opacity = false;
  canvas->set_pen_input_settings(pen_settings);

  // An open horizontal line as the work path (document API: no implied chord).
  patchy::PathAnchor start;
  start.anchor_x = start.in_x = start.out_x = 150.0;
  start.anchor_y = start.in_y = start.out_y = 300.0;
  auto end = start;
  end.anchor_x = end.in_x = end.out_x = 450.0;
  patchy::PathSubpath line;
  line.closed = false;
  line.anchors = {start, end};
  patchy::VectorPath path;
  path.subpaths.push_back(line);
  patchy::DocumentPath work(document.allocate_path_id(), "Work Path",
                            patchy::DocumentPathKind::Work, std::move(path));
  work.mark_dirty();
  document.add_path(std::move(work));
  patchy::ui::MainWindowTestAccess::refresh_paths_panel(window);
  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  paths_list->setCurrentRow(0);
  QApplication::processEvents();

  canvas->set_primary_color(QColor(20, 20, 20));
  canvas->set_brush_size(20);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("strokePathDialog"));
    CHECK(dialog != nullptr);
    dialog->findChild<QCheckBox*>(QStringLiteral("strokePathSimulatePressureCheck"))
        ->setChecked(true);
    dialog->accept();
  });
  window.findChild<QAction*>(QStringLiteral("pathStrokeAction"))->trigger();
  QApplication::processEvents();

  // The taper: thin near the ends, full brush width in the middle. The
  // startup document paints onto the transparent "Paint Layer" (the active
  // layer), so measure painted alpha there, not on the Background.
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  const auto* paint_layer = std::as_const(document).find_layer(*active);
  CHECK(paint_layer != nullptr);
  const auto& pixels = paint_layer->pixels();
  CHECK(pixels.format().channels == 4);
  const auto thickness_at = [&pixels](int x) {
    int count = 0;
    for (int y = 250; y <= 350; ++y) {
      if (pixels.pixel(x, y)[3] > 0U) {
        ++count;
      }
    }
    return count;
  };
  const int start_thickness = thickness_at(160);
  const int mid_thickness = thickness_at(300);
  CHECK(mid_thickness >= 15);
  CHECK(start_thickness >= 1);
  CHECK(start_thickness <= mid_thickness / 2);
}

void ui_path_free_transform_moves_scales_and_undoes() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // A rect work path, auto-targeted by the drag.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));

  // Ctrl+T under a path tool starts the PATH session, not the layer one.
  canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->path_transform_active());
  CHECK(!canvas->free_transform_active());

  // Arrows nudge the pending box; Enter commits ONE undo entry.
  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Down, Qt::ShiftModifier);  // +10
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->path_transform_active());
  const auto* work = document.work_path();
  CHECK(work != nullptr);
  const auto bounds_of_work = [&document] {
    std::array<double, 4> bounds{1e9, 1e9, -1e9, -1e9};
    for (const auto& anchor : document.work_path()->path().subpaths[0].anchors) {
      bounds[0] = std::min(bounds[0], anchor.anchor_x);
      bounds[1] = std::min(bounds[1], anchor.anchor_y);
      bounds[2] = std::max(bounds[2], anchor.anchor_x);
      bounds[3] = std::max(bounds[3], anchor.anchor_y);
    }
    return bounds;
  };
  auto bounds = bounds_of_work();
  CHECK(std::abs(bounds[0] - 102.0) < 1e-6);
  CHECK(std::abs(bounds[1] - 110.0) < 1e-6);

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  bounds = bounds_of_work();
  CHECK(std::abs(bounds[0] - 100.0) < 1e-6);

  // A corner-handle drag scales; the bottom-right anchor follows the handle.
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->path_transform_active());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(300, 220)),
       canvas->widget_position_for_document_point(QPoint(400, 280)));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  bounds = bounds_of_work();
  CHECK(std::abs(bounds[2] - 400.0) < 1.5);
  CHECK(std::abs(bounds[3] - 280.0) < 1.5);

  // Escape cancels a pending transform without touching the path.
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->path_transform_active());
  bounds = bounds_of_work();
  CHECK(std::abs(bounds[2] - 400.0) < 1.5);
}

void ui_make_work_path_from_selection_traces_selection() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // A donut selection: outer rect with a rectangular hole.
  patchy::PixelBuffer coverage(document.width(), document.height(),
                               patchy::PixelFormat::gray8());
  for (int y = 0; y < coverage.height(); ++y) {
    for (int x = 0; x < coverage.width(); ++x) {
      const bool outer = x >= 100 && x < 300 && y >= 100 && y < 260;
      const bool hole = x >= 160 && x < 240 && y >= 140 && y < 220;
      *coverage.pixel(x, y) = outer && !hole ? 255 : 0;
    }
  }
  canvas->replace_selection_from_grayscale(coverage, QStringLiteral("test selection"));
  CHECK(canvas->has_selection());

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("makeWorkPathDialog"));
    CHECK(dialog != nullptr);
    dialog->accept();
  });
  window.findChild<QAction*>(QStringLiteral("pathFromSelectionAction"))->trigger();
  QApplication::processEvents();

  const auto* work = document.work_path();
  CHECK(work != nullptr);
  CHECK(work->path().subpaths.size() == 2);
  const auto& outer_subpath = work->path().subpaths[0];
  const auto& hole_subpath = work->path().subpaths[1];
  CHECK(outer_subpath.op == patchy::PathCombineOp::Add);
  CHECK(hole_subpath.op == patchy::PathCombineOp::Subtract);
  CHECK(outer_subpath.anchors.size() == 4);
  CHECK(hole_subpath.anchors.size() == 4);
  const auto bounds_of = [](const patchy::PathSubpath& subpath) {
    double min_x = 1e9;
    double min_y = 1e9;
    double max_x = -1e9;
    double max_y = -1e9;
    for (const auto& anchor : subpath.anchors) {
      min_x = std::min(min_x, anchor.anchor_x);
      min_y = std::min(min_y, anchor.anchor_y);
      max_x = std::max(max_x, anchor.anchor_x);
      max_y = std::max(max_y, anchor.anchor_y);
    }
    return std::array<double, 4>{min_x, min_y, max_x, max_y};
  };
  const auto outer_bounds = bounds_of(outer_subpath);
  CHECK(std::abs(outer_bounds[0] - 100.0) <= 0.5);
  CHECK(std::abs(outer_bounds[1] - 100.0) <= 0.5);
  CHECK(std::abs(outer_bounds[2] - 300.0) <= 0.5);
  CHECK(std::abs(outer_bounds[3] - 260.0) <= 0.5);
  const auto hole_bounds = bounds_of(hole_subpath);
  CHECK(std::abs(hole_bounds[0] - 160.0) <= 0.5);
  CHECK(std::abs(hole_bounds[1] - 140.0) <= 0.5);
  CHECK(std::abs(hole_bounds[2] - 240.0) <= 0.5);
  CHECK(std::abs(hole_bounds[3] - 220.0) <= 0.5);

  // The work path row is targeted (its outline shows immediately).
  CHECK(canvas->active_document_path().has_value());
  CHECK(*canvas->active_document_path() == work->id());
  CHECK(canvas->panel_path_targeted());
}

}  // namespace


void ui_new_fill_cancel_preserves_document_and_history() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  // Earlier ordered tests can empty the user's pattern library. Supply this
  // document's own pattern so the Pattern command can open its dialog.
  document.metadata().patterns.adopt(
      patchy::builtin_pattern_resource(patchy::builtin_pattern_presets().front().id));
  const auto layers = std::as_const(document).layers().size();
  const auto active = document.active_layer_id();
  const auto undo = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  for (const auto* name : {"layerNewGradientFillAction", "layerNewPatternFillAction"}) {
    bool cancelled = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
      CHECK(dialog != nullptr);
      cancelled = true;
      dialog->reject();
    });
    auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
    CHECK(action != nullptr);
    action->trigger();
    QApplication::processEvents();
    CHECK(cancelled);
    CHECK(std::as_const(document).layers().size() == layers);
    CHECK(document.active_layer_id() == active);
    CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo);
    CHECK(patchy::ui::MainWindowTestAccess::active_session_redo_depth(window) == 0);
    CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  }
}


void ui_path_transform_is_cancelled_on_layer_target_change() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto first = *document.active_layer_id();
  canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  CHECK(canvas->begin_path_transform());
  CHECK(canvas->path_transform_active());
  auto& other = document.add_layer(patchy::Layer(document.allocate_layer_id(), "Other",
                                               patchy::PixelBuffer(2,2,patchy::PixelFormat::rgba8())));
  const auto id = other.id();
  canvas->set_selected_layer_ids({id});
  CHECK(!canvas->path_transform_active());
  canvas->commit_path_transform();
  CHECK(std::as_const(document).find_layer(first)->vector_shape() != nullptr);
  CHECK(std::as_const(document).find_layer(id)->vector_shape() == nullptr);
}


// A tap (press + release without dragging) at a document point. on_release
// is armed as a 0 ms timer right before the release goes out: send_mouse
// pumps the event loop after every event, so a timer armed before the press
// would fire before the modal Create dialog exists.
void shape_tap(patchy::ui::CanvasWidget& canvas, QPoint document_point,
               std::function<void()> on_release = {}) {
  const auto widget_point = canvas.widget_position_for_document_point(document_point);
  send_mouse(canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, widget_point, Qt::NoButton, Qt::LeftButton);
  if (on_release) {
    QTimer::singleShot(0, std::move(on_release));
  }
  QMouseEvent release(QEvent::MouseButtonRelease, widget_point, canvas.mapToGlobal(widget_point),
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(&canvas, &release);
  QApplication::processEvents();
}

// Answers the Create <Shape> dialog (pass to shape_tap); from_center < 0
// leaves the checkbox alone, radius < 0 leaves the rect radii alone.
std::function<void()> shape_create_dialog_answer(bool& seen, double width, double height,
                                                 int from_center, double radius, bool accept) {
  return [&seen, width, height, from_center, radius, accept] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeCreateDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    seen = true;
    auto* width_spin = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeCreateWidthSpin"));
    auto* height_spin = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeCreateHeightSpin"));
    auto* center_check = dialog->findChild<QCheckBox*>(QStringLiteral("shapeCreateFromCenterCheck"));
    CHECK(width_spin != nullptr && height_spin != nullptr && center_check != nullptr);
    width_spin->setValue(width);
    height_spin->setValue(height);
    if (from_center >= 0) {
      center_check->setChecked(from_center != 0);
    }
    if (radius >= 0.0) {
      for (const auto* name : {"shapeCreateRadiusSpin", "shapeCreateRadiusTopLeftSpin",
                               "shapeCreateRadiusTopRightSpin",
                               "shapeCreateRadiusBottomRightSpin", "shapeCreateRadiusBottomLeftSpin"}) {
        if (auto* spin = dialog->findChild<QDoubleSpinBox*>(QLatin1String(name)); spin != nullptr) {
          spin->setValue(radius);
        }
      }
    }
    if (accept) {
      dialog->accept();
    } else {
      dialog->reject();
    }
  };
}

void ui_shape_tap_opens_create_dialog_and_places_rectangle() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  CHECK(canvas->vector_tool_mode() == patchy::ui::VectorToolMode::Shape);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(0);

  // Top-left at the click, with one rounded corner from the dialog.
  bool seen = false;
  shape_tap(*canvas, QPoint(120, 140), [&seen] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeCreateDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    seen = true;
    auto* width_spin = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeCreateWidthSpin"));
    auto* height_spin = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeCreateHeightSpin"));
    auto* center_check = dialog->findChild<QCheckBox*>(QStringLiteral("shapeCreateFromCenterCheck"));
    auto* top_left = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeCreateRadiusTopLeftSpin"));
    auto* link = dialog->findChild<QToolButton*>(QStringLiteral("shapeCreateRadiusLinkButton"));
    CHECK(width_spin != nullptr && height_spin != nullptr && center_check != nullptr &&
          top_left != nullptr && link != nullptr);
    CHECK(!center_check->isChecked());
    // The radii prefill from the options-bar Radius (0 here), linked.
    CHECK(std::abs(top_left->value()) < 1e-9);
    CHECK(link->isChecked());
    link->click();  // unlink to round one corner only
    width_spin->setValue(150.0);
    height_spin->setValue(80.0);
    top_left->setValue(10.0);
    dialog->accept();
  });
  CHECK(seen);
  CHECK(document.layers().size() == initial_layers + 1);
  auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Rectangle 1");
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  CHECK(content->origination.size() == 1);
  CHECK(content->origination[0].kind == patchy::LiveShapeKind::RoundedRectangle);
  CHECK(std::abs(content->origination[0].left - 120.0) < 1e-6);
  CHECK(std::abs(content->origination[0].top - 140.0) < 1e-6);
  CHECK(std::abs(content->origination[0].right - 270.0) < 1e-6);
  CHECK(std::abs(content->origination[0].bottom - 220.0) < 1e-6);
  CHECK(std::abs(content->origination[0].corner_radii[0] - 10.0) < 1e-9);
  CHECK(std::abs(content->origination[0].corner_radii[1]) < 1e-9);
  CHECK(layer->bounds().width == 150);
  CHECK(layer->bounds().height == 80);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(195, 180)), Qt::black, 8));

  // From Center centers the shape on the click.
  seen = false;
  shape_tap(*canvas, QPoint(400, 300), shape_create_dialog_answer(seen, 100.0, 60.0, 1, 0.0, true));
  CHECK(seen);
  CHECK(document.layers().size() == initial_layers + 2);
  active = document.active_layer_id();
  layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Rectangle 2");
  content = layer->vector_shape();
  CHECK(content != nullptr && content->origination.size() == 1);
  CHECK(content->origination[0].kind == patchy::LiveShapeKind::Rectangle);
  CHECK(std::abs(content->origination[0].left - 350.0) < 1e-6);
  CHECK(std::abs(content->origination[0].top - 270.0) < 1e-6);
  CHECK(std::abs(content->origination[0].right - 450.0) < 1e-6);
  CHECK(std::abs(content->origination[0].bottom - 330.0) < 1e-6);

  // The dialog remembers the last accepted values; Cancel creates nothing.
  bool remembered = false;
  shape_tap(*canvas, QPoint(500, 500), [&remembered] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeCreateDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* width_spin = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeCreateWidthSpin"));
    auto* height_spin = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeCreateHeightSpin"));
    auto* center_check = dialog->findChild<QCheckBox*>(QStringLiteral("shapeCreateFromCenterCheck"));
    CHECK(width_spin != nullptr && height_spin != nullptr && center_check != nullptr);
    remembered = std::abs(width_spin->value() - 100.0) < 1e-9 &&
                 std::abs(height_spin->value() - 60.0) < 1e-9 && center_check->isChecked();
    dialog->reject();
  });
  CHECK(remembered);
  CHECK(document.layers().size() == initial_layers + 2);
  CHECK(document.work_path() == nullptr);

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers + 1);
}

// Only a release on the press's own document pixel is a click: a drag that is
// tiny on screen (under the platform drag distance) still commits its shape
// and never opens the Create <Shape> dialog.
void ui_shape_tiny_drag_commits_without_create_dialog() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
  const QPoint from(300, 300);
  const QPoint to(304, 303);
  // The drag must sit under the drag distance for this to pin the rule.
  CHECK(4.0 * canvas->zoom() < static_cast<double>(QApplication::startDragDistance()));

  const auto press_point = canvas->widget_position_for_document_point(from);
  const auto release_point = canvas->widget_position_for_document_point(to);
  send_mouse(*canvas, QEvent::MouseButtonPress, press_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, release_point, Qt::NoButton, Qt::LeftButton);
  // Hang guard: a regression that opens the modal dialog gets it dismissed.
  bool dialog_opened = false;
  QTimer::singleShot(0, [&dialog_opened] {
    if (auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeCreateDialog"));
        dialog != nullptr) {
      dialog_opened = true;
      dialog->reject();
    }
  });
  QMouseEvent release(QEvent::MouseButtonRelease, release_point, canvas->mapToGlobal(release_point),
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &release);
  QApplication::processEvents();

  CHECK(!dialog_opened);
  CHECK(document.layers().size() == initial_layers + 1);
  auto* layer = document.find_layer(*document.active_layer_id());
  CHECK(layer != nullptr);
  const auto* content = layer != nullptr ? layer->vector_shape() : nullptr;
  CHECK(content != nullptr && content->origination.size() == 1);
  if (content != nullptr && content->origination.size() == 1) {
    CHECK(content->origination[0].kind == patchy::LiveShapeKind::Ellipse);
    const auto width = content->origination[0].right - content->origination[0].left;
    const auto height = content->origination[0].bottom - content->origination[0].top;
    CHECK(width >= 3.0 && width <= 5.0);
    CHECK(height >= 2.0 && height <= 4.0);
  }
}

void ui_shape_tap_creates_ellipse_polygon_and_custom_shape() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
  bool seen = false;
  shape_tap(*canvas, QPoint(200, 200), shape_create_dialog_answer(seen, 100.0, 50.0, 0, -1.0, true));
  CHECK(seen);
  CHECK(document.layers().size() == initial_layers + 1);
  auto* layer = document.find_layer(*document.active_layer_id());
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Ellipse 1");
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr && content->origination.size() == 1);
  CHECK(content->origination[0].kind == patchy::LiveShapeKind::Ellipse);
  CHECK(std::abs(content->origination[0].right - 300.0) < 1e-6);
  CHECK(std::abs(content->origination[0].bottom - 250.0) < 1e-6);

  // Polygon: a unit pentagon (first vertex up) scaled into the W x H box.
  canvas->set_tool(patchy::ui::CanvasTool::Polygon);
  auto* sides_spin = window.findChild<QSpinBox*>(QStringLiteral("polygonSidesSpin"));
  CHECK(sides_spin != nullptr);
  sides_spin->setValue(5);
  auto* inset_spin = window.findChild<QSpinBox*>(QStringLiteral("polygonStarInsetSpin"));
  CHECK(inset_spin != nullptr);
  inset_spin->setValue(0);
  seen = false;
  shape_tap(*canvas, QPoint(400, 400), shape_create_dialog_answer(seen, 100.0, 100.0, 0, -1.0, true));
  CHECK(seen);
  CHECK(document.layers().size() == initial_layers + 2);
  layer = document.find_layer(*document.active_layer_id());
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Polygon 1");
  content = layer->vector_shape();
  CHECK(content != nullptr);
  CHECK(content->path.subpaths.size() == 1);
  CHECK(content->path.subpaths[0].anchors.size() == 5);
  const auto polygon_layer_id = *document.active_layer_id();
  const auto polygon_bounds = content->path.bounds();
  CHECK(polygon_bounds.has_value());
  CHECK(std::abs(polygon_bounds->top - 400.0) < 0.5);           // apex touches the box top
  CHECK(polygon_bounds->left > 400.0 && polygon_bounds->left < 405.0);
  CHECK(polygon_bounds->right > 495.0 && polygon_bounds->right < 500.0);
  CHECK(polygon_bounds->bottom > 488.0 && polygon_bounds->bottom < 493.0);

  // The dialog's Sides field starts at the bar's count; a new count makes the
  // polygon and becomes the tool's (GitHub issue 309).
  seen = false;
  // Placed clear of the custom shape below: a click snaps to nearby shape edges.
  shape_tap(*canvas, QPoint(600, 250), [&seen] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeCreateDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* sides = dialog->findChild<QSpinBox*>(QStringLiteral("shapeCreateSidesSpin"));
    CHECK(sides != nullptr);
    if (sides == nullptr) {
      dialog->reject();
      return;
    }
    seen = true;
    CHECK(sides->value() == 5);
    sides->setValue(7);
    dialog->accept();
  });
  CHECK(seen);
  CHECK(document.layers().size() == initial_layers + 3);
  CHECK(*document.active_layer_id() != polygon_layer_id);
  layer = document.find_layer(*document.active_layer_id());
  content = layer != nullptr ? layer->vector_shape() : nullptr;
  CHECK(content != nullptr && content->path.subpaths.size() == 1);
  if (content != nullptr && content->path.subpaths.size() == 1) {
    CHECK(content->path.subpaths[0].anchors.size() == 7);
  }
  CHECK(sides_spin->value() == 7);
  CHECK(canvas->polygon_sides() == 7);

  // Custom Shape: the library shape stamped into the box.
  canvas->set_tool(patchy::ui::CanvasTool::CustomShape);
  seen = false;
  shape_tap(*canvas, QPoint(100, 500), shape_create_dialog_answer(seen, 80.0, 40.0, 0, -1.0, true));
  CHECK(seen);
  CHECK(document.layers().size() == initial_layers + 4);
  layer = document.find_layer(*document.active_layer_id());
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Custom Shape 1");
  content = layer->vector_shape();
  CHECK(content != nullptr);
  const auto custom_bounds = content->path.bounds();
  CHECK(custom_bounds.has_value());
  CHECK(custom_bounds->left >= 99.5 && custom_bounds->right <= 180.5);
  CHECK(custom_bounds->top >= 499.5 && custom_bounds->bottom <= 540.5);
  CHECK(custom_bounds->right - custom_bounds->left > 40.0);

  // The dialog's Shape picker lists the bar's shapes, starts on the bar's
  // shape, and the one picked is created and becomes the tool's (issue 309).
  auto* bar_combo = window.findChild<QComboBox*>(QStringLiteral("customShapeCombo"));
  CHECK(bar_combo != nullptr && bar_combo->count() > 1);
  if (bar_combo == nullptr || bar_combo->count() < 2) {
    return;
  }
  const int picked_index = bar_combo->currentIndex() == 0 ? 1 : 0;
  const auto picked_id = bar_combo->itemData(picked_index).toString();
  seen = false;
  shape_tap(*canvas, QPoint(300, 500), [&seen, bar_combo, picked_index] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeCreateDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("shapeCreateCustomShapeCombo"));
    CHECK(combo != nullptr);
    if (combo == nullptr) {
      dialog->reject();
      return;
    }
    seen = true;
    CHECK(combo->count() == bar_combo->count());
    CHECK(combo->currentData() == bar_combo->currentData());
    CHECK(combo->itemText(picked_index) == bar_combo->itemText(picked_index));
    combo->setCurrentIndex(picked_index);
    dialog->accept();
  });
  CHECK(seen);
  CHECK(document.layers().size() == initial_layers + 5);
  CHECK(bar_combo->currentData().toString() == picked_id);
  const auto* tool_shape = canvas->custom_shape_path();
  layer = document.find_layer(*document.active_layer_id());
  content = layer != nullptr ? layer->vector_shape() : nullptr;
  CHECK(tool_shape != nullptr && content != nullptr);
  if (tool_shape != nullptr && content != nullptr) {
    CHECK(content->path.subpaths.size() == tool_shape->subpaths.size());
    for (std::size_t i = 0; i < std::min(content->path.subpaths.size(), tool_shape->subpaths.size()); ++i) {
      CHECK(content->path.subpaths[i].anchors.size() == tool_shape->subpaths[i].anchors.size());
    }
  }
}

// Layout of the four Create <Shape> dialogs (GitHub issue 309): wide enough
// for the title, Width and Height on one row with From Center below, and per
// tool only its own extra field.
void ui_shape_create_dialog_layout() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  const auto visible_child = [](QDialog* dialog, const char* name) {
    auto* widget = dialog->findChild<QWidget*>(QLatin1String(name));
    return widget != nullptr && widget->isVisible();
  };
  const std::pair<patchy::ui::CanvasTool, const char*> tools[] = {
      {patchy::ui::CanvasTool::Rectangle, "shapeCreateRadiusLinkButton"},
      {patchy::ui::CanvasTool::Ellipse, nullptr},
      {patchy::ui::CanvasTool::Polygon, "shapeCreateSidesSpin"},
      {patchy::ui::CanvasTool::CustomShape, "shapeCreateCustomShapeCombo"},
  };
  for (const auto& [tool, own_field] : tools) {
    canvas->set_tool(tool);
    bool seen = false;
    shape_tap(*canvas, QPoint(200, 200), [&seen, &visible_child, own = own_field] {
      auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeCreateDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      QApplication::processEvents();
      auto* width_spin = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeCreateWidthSpin"));
      auto* height_spin = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeCreateHeightSpin"));
      auto* center = dialog->findChild<QCheckBox*>(QStringLiteral("shapeCreateFromCenterCheck"));
      CHECK(width_spin != nullptr && height_spin != nullptr && center != nullptr);
      if (width_spin == nullptr || height_spin == nullptr || center == nullptr) {
        dialog->reject();
        return;
      }
      seen = true;
      const auto width_box = QRect(width_spin->mapTo(dialog, QPoint()), width_spin->size());
      const auto height_box = QRect(height_spin->mapTo(dialog, QPoint()), height_spin->size());
      const auto center_box = QRect(center->mapTo(dialog, QPoint()), center->size());
      CHECK(std::abs(width_box.center().y() - height_box.center().y()) <= 1);
      CHECK(height_box.left() > width_box.right());
      CHECK(center_box.top() > width_box.bottom());
      QFont title_font = dialog->font();
      title_font.setBold(true);
      CHECK(dialog->width() >=
            QFontMetrics(title_font).horizontalAdvance(dialog->windowTitle()) + 200);
      for (const auto* field : {"shapeCreateRadiusLinkButton", "shapeCreateSidesSpin",
                                "shapeCreateCustomShapeCombo"}) {
        const bool expected = own != nullptr && std::string_view(own) == field;
        CHECK(visible_child(dialog, field) == expected);
      }
      dialog->reject();
    });
    CHECK(seen);
  }
}

// Create Rectangle's Radius (GitHub issue 310): linked by default, one value
// sets all four corners; unlinking shows the four, unequal radii reopen
// unlinked, and a new options-bar Radius replaces the remembered corners.
void ui_shape_create_rectangle_radius_link() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* bar_radius = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(bar_radius != nullptr);
  if (bar_radius == nullptr) {
    return;
  }
  const auto visible_child = [](QDialog* dialog, const char* name) {
    auto* widget = dialog->findChild<QWidget*>(QLatin1String(name));
    return widget != nullptr && widget->isVisible();
  };

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  bar_radius->setValue(6);
  const auto initial_layers = document.layers().size();
  bool seen = false;
  shape_tap(*canvas, QPoint(100, 100), [&seen, &visible_child] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeCreateDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    QApplication::processEvents();
    auto* link = dialog->findChild<QToolButton*>(QStringLiteral("shapeCreateRadiusLinkButton"));
    auto* radius = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeCreateRadiusSpin"));
    auto* top_left = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeCreateRadiusTopLeftSpin"));
    auto* top_right = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeCreateRadiusTopRightSpin"));
    auto* bottom_right =
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeCreateRadiusBottomRightSpin"));
    CHECK(link != nullptr && radius != nullptr && top_left != nullptr && top_right != nullptr &&
          bottom_right != nullptr);
    if (link == nullptr || radius == nullptr || top_left == nullptr || top_right == nullptr ||
        bottom_right == nullptr) {
      dialog->reject();
      return;
    }
    seen = true;
    CHECK(link->isChecked());
    CHECK(radius->isVisible());
    CHECK(!visible_child(dialog, "shapeCreateRadiusTopLeftSpin"));
    CHECK(std::abs(radius->value() - 6.0) < 1e-9);
    radius->setValue(9.0);
    CHECK(std::abs(bottom_right->value() - 9.0) < 1e-9);
    // Unlinking shows the four corners at the shared radius.
    link->click();
    QApplication::processEvents();
    CHECK(!link->isChecked());
    CHECK(!radius->isVisible());
    CHECK(top_left->isVisible() && bottom_right->isVisible());
    CHECK(std::abs(top_left->value() - 9.0) < 1e-9);
    top_left->setValue(12.0);
    top_right->setValue(4.0);
    dialog->accept();
  });
  CHECK(seen);
  CHECK(document.layers().size() == initial_layers + 1);
  auto* layer = document.find_layer(*document.active_layer_id());
  const auto* content = layer != nullptr ? layer->vector_shape() : nullptr;
  CHECK(content != nullptr && content->origination.size() == 1);
  if (content != nullptr && content->origination.size() == 1) {
    const auto& radii = content->origination[0].corner_radii;
    CHECK(std::abs(radii[0] - 12.0) < 1e-9);
    CHECK(std::abs(radii[1] - 4.0) < 1e-9);
    CHECK(std::abs(radii[2] - 9.0) < 1e-9);
    CHECK(std::abs(radii[3] - 9.0) < 1e-9);
  }

  // Unequal radii reopen unlinked with every corner kept.
  seen = false;
  shape_tap(*canvas, QPoint(300, 100), [&seen] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeCreateDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    QApplication::processEvents();
    auto* link = dialog->findChild<QToolButton*>(QStringLiteral("shapeCreateRadiusLinkButton"));
    auto* top_left = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeCreateRadiusTopLeftSpin"));
    auto* top_right = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeCreateRadiusTopRightSpin"));
    CHECK(link != nullptr && top_left != nullptr && top_right != nullptr);
    if (link == nullptr || top_left == nullptr || top_right == nullptr) {
      dialog->reject();
      return;
    }
    seen = true;
    CHECK(!link->isChecked());
    CHECK(top_left->isVisible());
    CHECK(std::abs(top_left->value() - 12.0) < 1e-9);
    CHECK(std::abs(top_right->value() - 4.0) < 1e-9);
    // Relinking takes the top-left radius for all four.
    link->click();
    QApplication::processEvents();
    auto* radius = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeCreateRadiusSpin"));
    CHECK(radius != nullptr && radius->isVisible() && std::abs(radius->value() - 12.0) < 1e-9);
    CHECK(std::abs(top_right->value() - 12.0) < 1e-9);
    dialog->reject();
  });
  CHECK(seen);

  // A new options-bar Radius replaces the remembered corners.
  bar_radius->setValue(3);
  seen = false;
  shape_tap(*canvas, QPoint(300, 100), [&seen] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeCreateDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* link = dialog->findChild<QToolButton*>(QStringLiteral("shapeCreateRadiusLinkButton"));
    auto* radius = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeCreateRadiusSpin"));
    CHECK(link != nullptr && radius != nullptr);
    if (link != nullptr && radius != nullptr) {
      seen = true;
      CHECK(link->isChecked());
      CHECK(std::abs(radius->value() - 3.0) < 1e-9);
    }
    dialog->reject();
  });
  CHECK(seen);
  CHECK(document.layers().size() == initial_layers + 1);
}

void ui_shape_tap_line_fixed_size_and_path_mode() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  // Line has no Create dialog (Photoshop); a tap does nothing.
  canvas->set_tool(patchy::ui::CanvasTool::Line);
  bool dialog_seen = false;
  shape_tap(*canvas, QPoint(200, 200), [&dialog_seen] {
    dialog_seen = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeCreateDialog")) != nullptr;
  });
  QApplication::processEvents();
  CHECK(!dialog_seen);
  CHECK(document.layers().size() == initial_layers);

  // Fixed Size keeps its click behavior: the exact W x H lands without a dialog.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  auto* style_combo = window.findChild<QComboBox*>(QStringLiteral("shapeStyleCombo"));
  auto* fixed_width = window.findChild<QSpinBox*>(QStringLiteral("shapeFixedWidthSpin"));
  auto* fixed_height = window.findChild<QSpinBox*>(QStringLiteral("shapeFixedHeightSpin"));
  CHECK(style_combo != nullptr && fixed_width != nullptr && fixed_height != nullptr);
  style_combo->setCurrentIndex(2);  // Fixed Size
  fixed_width->setValue(120);
  fixed_height->setValue(90);
  dialog_seen = false;
  shape_tap(*canvas, QPoint(50, 50), [&dialog_seen] {
    dialog_seen = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeCreateDialog")) != nullptr;
  });
  QApplication::processEvents();
  CHECK(!dialog_seen);
  CHECK(document.layers().size() == initial_layers + 1);
  auto* layer = document.find_layer(*document.active_layer_id());
  CHECK(layer != nullptr);
  CHECK(std::abs(layer->bounds().width - 120) <= 1);
  CHECK(std::abs(layer->bounds().height - 90) <= 1);
  style_combo->setCurrentIndex(0);

  // Path mode: the accepted shape lands on the work path, not a layer.
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path
  bool seen = false;
  shape_tap(*canvas, QPoint(300, 300), shape_create_dialog_answer(seen, 100.0, 100.0, 0, 0.0, true));
  CHECK(seen);
  CHECK(document.layers().size() == initial_layers + 1);
  CHECK(document.work_path() != nullptr);
  CHECK(document.work_path()->path().subpaths.size() == 1);
  const auto work_bounds = document.work_path()->path().bounds();
  CHECK(work_bounds.has_value());
  CHECK(std::abs(work_bounds->left - 300.0) < 1e-6);
  CHECK(std::abs(work_bounds->right - 400.0) < 1e-6);
}

// Captures each Create <Shape> dialog as opened by a tap, Rectangle also
// unlinked (issue shots for GitHub issues 309 and 310); nothing is created.
void ui_shape_create_dialog_captures() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  const std::pair<patchy::ui::CanvasTool, const char*> tools[] = {
      {patchy::ui::CanvasTool::Rectangle, "shape_create_dialog_rectangle"},
      {patchy::ui::CanvasTool::Ellipse, "shape_create_dialog_ellipse"},
      {patchy::ui::CanvasTool::Polygon, "shape_create_dialog_polygon"},
      {patchy::ui::CanvasTool::CustomShape, "shape_create_dialog_custom_shape"},
  };
  for (const auto& [tool, artifact] : tools) {
    canvas->set_tool(tool);
    bool seen = false;
    const std::string name = artifact;
    shape_tap(*canvas, QPoint(200, 200), [&seen, name] {
      auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeCreateDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      seen = true;
      QApplication::processEvents();
      save_widget_artifact(name, *dialog);
      // Rectangle again with the corners unlinked.
      if (auto* link = dialog->findChild<QToolButton*>(QStringLiteral("shapeCreateRadiusLinkButton"));
          link != nullptr) {
        link->click();
        QApplication::processEvents();
        save_widget_artifact(name + "_unlinked", *dialog);
      }
      dialog->reject();
    });
    CHECK(seen);
  }
}

void ui_shape_size_spins_reflect_and_resize_active_shape() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  auto* width_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorShapeWidthSpin"));
  auto* height_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorShapeHeightSpin"));
  auto* link_button = window.findChild<QPushButton*>(QStringLiteral("vectorShapeLinkSizeButton"));
  CHECK(width_spin != nullptr && height_spin != nullptr && link_button != nullptr);

  require_action(window, "toolEllipseAction")->trigger();
  QApplication::processEvents();
  CHECK(width_spin->isVisible());
  CHECK(!width_spin->isEnabled());  // no shape layer yet
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));
  const auto layer_id = *document.active_layer_id();
  CHECK(width_spin->isEnabled());
  CHECK(std::abs(width_spin->value() - 200.0) < 0.5);
  CHECK(std::abs(height_spin->value() - 120.0) < 0.5);

  // Width edit: top-left anchored, the ellipse stays a live shape.
  width_spin->setValue(400.0);
  process_events_for(450);
  auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  CHECK(content->origination.size() == 1);
  CHECK(content->origination[0].kind == patchy::LiveShapeKind::Ellipse);
  CHECK(std::abs(content->origination[0].left - 100.0) < 0.5);
  CHECK(std::abs(content->origination[0].right - 500.0) < 0.5);
  CHECK(std::abs(content->origination[0].bottom - 220.0) < 0.5);
  CHECK(std::abs(width_spin->value() - 400.0) < 0.5);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(480, 160)), Qt::black, 8));

  // Linked: a height edit scales the width in proportion (400:120).
  link_button->setChecked(true);
  height_spin->setValue(60.0);
  CHECK(std::abs(width_spin->value() - 200.0) < 0.5);
  process_events_for(450);
  content = document.find_layer(layer_id)->vector_shape();
  CHECK(std::abs(content->origination[0].right - 300.0) < 0.5);
  CHECK(std::abs(content->origination[0].bottom - 160.0) < 0.5);
  link_button->setChecked(false);

  // One undo entry per edit.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  content = document.find_layer(layer_id)->vector_shape();
  CHECK(std::abs(content->origination[0].right - 500.0) < 0.5);
  CHECK(std::abs(content->origination[0].bottom - 220.0) < 0.5);
  CHECK(std::abs(width_spin->value() - 400.0) < 0.5);
  CHECK(std::abs(height_spin->value() - 120.0) < 0.5);

  // A non-live shape (custom stamp) scales its path instead.
  require_action(window, "toolCustomShapeAction")->trigger();
  QApplication::processEvents();
  shape_drag(*canvas, QPoint(400, 400), QPoint(500, 500));
  const auto custom_id = *document.active_layer_id();
  CHECK(custom_id != layer_id);
  const auto before = document.find_layer(custom_id)->vector_shape()->path.bounds();
  CHECK(before.has_value());
  const auto before_width = before->right - before->left;
  CHECK(std::abs(width_spin->value() - before_width) < 0.5);
  width_spin->setValue(before_width * 2.0);
  process_events_for(450);
  const auto after = document.find_layer(custom_id)->vector_shape()->path.bounds();
  CHECK(after.has_value());
  CHECK(std::abs((after->right - after->left) - before_width * 2.0) < 0.5);
  CHECK(std::abs(after->left - before->left) < 0.5);
  CHECK(std::abs((after->bottom - after->top) - (before->bottom - before->top)) < 0.5);

  // Path Select shows the readouts for the active shape; without one they
  // disable (Undo drops the custom shape layer and reactivates the ellipse
  // first, then the ellipse layer itself).
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  CHECK(width_spin->isVisible());
  CHECK(width_spin->isEnabled());
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();  // shape size
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();  // custom shape layer
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();  // shape size
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();  // ellipse layer
  QApplication::processEvents();
  CHECK(document.find_layer(layer_id) == nullptr);
  CHECK(!width_spin->isEnabled());
}

void ui_shape_size_controls_follow_move_and_properties_selection() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  require_action(window, "toolRectAction")->trigger();
  QApplication::processEvents();
  canvas->setFocus(Qt::MouseFocusReason);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));
  const auto layer_id = *document.active_layer_id();

  require_action(window, "toolMoveAction")->trigger();
  QApplication::processEvents();
  auto* options_width = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorShapeWidthSpin"));
  auto* options_height = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorShapeHeightSpin"));
  auto* properties_panel = window.findChild<QWidget*>(QStringLiteral("propertiesShapeSizePanel"));
  auto* properties_width = window.findChild<QDoubleSpinBox*>(QStringLiteral("propertiesShapeWidthSpin"));
  auto* properties_height = window.findChild<QDoubleSpinBox*>(QStringLiteral("propertiesShapeHeightSpin"));
  CHECK(options_width != nullptr && options_height != nullptr);
  CHECK(properties_panel != nullptr && properties_width != nullptr && properties_height != nullptr);
  CHECK(options_width->isVisible() && options_width->isEnabled());
  CHECK(!properties_panel->isHidden() && properties_panel->isEnabled());
  CHECK(std::abs(options_width->value() - 200.0) < 0.5);
  CHECK(std::abs(properties_width->value() - 200.0) < 0.5);
  CHECK(std::abs(properties_height->value() - 120.0) < 0.5);
  // The row must read at the dock's default width: the spins are fixed-width,
  // so the values stay inside the visible panel (the first draft stretched
  // them past the dock edge). Artifact: properties-shape-size-row.png.
  if (auto* toggle = window.findChild<QToolButton*>(QStringLiteral("propertiesDockCollapseButton"));
      toggle != nullptr && !toggle->isChecked()) {
    toggle->click();
  }
  process_events_for(200);
  CHECK(properties_width->width() <= 110);
  if (auto* panel = window.findChild<QWidget*>(QStringLiteral("propertiesPanel"))) {
    CHECK(properties_width->mapTo(panel, properties_width->rect().bottomRight()).x() <= panel->width());
    save_widget_artifact("properties-shape-size-row", *panel);
  }

  properties_width->setValue(360.0);
  process_events_for(450);
  auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(std::abs(layer->bounds().width - 360.0) <= 1.0);
  CHECK(std::abs(options_width->value() - 360.0) < 0.5);
  CHECK(std::abs(properties_width->value() - 360.0) < 0.5);

  options_height->setValue(60.0);
  process_events_for(450);
  layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(std::abs(layer->bounds().height - 60.0) <= 1.0);
  CHECK(std::abs(properties_height->value() - 60.0) < 0.5);
}

void ui_shape_style_row_is_pixel_only_and_greys_size_at_normal() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* style_combo = window.findChild<QComboBox*>(QStringLiteral("shapeStyleCombo"));
  auto* fixed_width = window.findChild<QSpinBox*>(QStringLiteral("shapeFixedWidthSpin"));
  auto* fixed_height = window.findChild<QSpinBox*>(QStringLiteral("shapeFixedHeightSpin"));
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(style_combo != nullptr && fixed_width != nullptr && fixed_height != nullptr &&
        mode_combo != nullptr);

  require_action(window, "toolRectAction")->trigger();
  QApplication::processEvents();
  CHECK(mode_combo->currentIndex() == 0);  // Shape
  CHECK(!style_combo->isVisible());
  CHECK(!fixed_width->isVisible());
  mode_combo->setCurrentIndex(1);  // Path
  QApplication::processEvents();
  CHECK(!style_combo->isVisible());
  mode_combo->setCurrentIndex(2);  // Pixels
  QApplication::processEvents();
  CHECK(style_combo->isVisible());
  CHECK(fixed_width->isVisible());
  CHECK(style_combo->currentIndex() == 0);  // Normal
  CHECK(!fixed_width->isEnabled());
  CHECK(!fixed_height->isEnabled());
  style_combo->setCurrentIndex(1);  // Fixed Ratio
  CHECK(fixed_width->isEnabled());
  CHECK(fixed_height->isEnabled());
  style_combo->setCurrentIndex(0);
  CHECK(!fixed_width->isEnabled());
  mode_combo->setCurrentIndex(0);

  // The marquee's twin row greys its size fields the same way.
  auto* marquee_style = window.findChild<QComboBox*>(QStringLiteral("selectionStyleCombo"));
  auto* marquee_width = window.findChild<QSpinBox*>(QStringLiteral("selectionFixedWidthSpin"));
  CHECK(marquee_style != nullptr && marquee_width != nullptr);
  require_action(window, "toolMarqueeAction")->trigger();
  QApplication::processEvents();
  CHECK(marquee_width->isVisible());
  CHECK(!marquee_width->isEnabled());
  marquee_style->setCurrentIndex(2);
  CHECK(marquee_width->isEnabled());
  marquee_style->setCurrentIndex(0);
  CHECK(!marquee_width->isEnabled());
}

class ShowToParentCounter final : public QObject {
public:
  int shows{0};

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::ShowToParent) {
      ++shows;
    }
    return QObject::eventFilter(watched, event);
  }
};

class HeightRecorder final : public QObject {
public:
  int max_height{0};

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::Resize) {
      if (auto* widget = qobject_cast<QWidget*>(watched); widget != nullptr) {
        max_height = std::max(max_height, widget->height());
      }
    }
    return QObject::eventFilter(watched, event);
  }
};

void ui_options_bar_never_shows_pixel_widgets_in_shape_mode() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  auto* fill_check = window.findChild<QCheckBox*>(QStringLiteral("shapeFillCheck"));
  auto* style_combo = window.findChild<QComboBox*>(QStringLiteral("shapeStyleCombo"));
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(options_bar != nullptr && fill_check != nullptr && style_combo != nullptr &&
        mode_combo != nullptr);
  QApplication::processEvents();

  // Brush -> Rectangle in Shape mode: the raster-only controls must never
  // be shown and then hidden (that show activates the layouts synchronously
  // and painted a two-row bar for one frame).
  ShowToParentCounter counter;
  fill_check->installEventFilter(&counter);
  style_combo->installEventFilter(&counter);
  HeightRecorder recorder;
  options_bar->installEventFilter(&recorder);
  require_action(window, "toolRectAction")->trigger();
  QApplication::processEvents();
  CHECK(mode_combo->isVisible());
  CHECK(!fill_check->isVisible());
  CHECK(!style_combo->isVisible());
  CHECK(counter.shows == 0);
  CHECK(recorder.max_height <= options_bar->height());

  // Switching to Pixels shows them (the row is allowed to grow then).
  mode_combo->setCurrentIndex(2);
  QApplication::processEvents();
  CHECK(fill_check->isVisible());
  CHECK(style_combo->isVisible());
  fill_check->removeEventFilter(&counter);
  style_combo->removeEventFilter(&counter);
  options_bar->removeEventFilter(&recorder);
}

// Presses at `from` and drags to `to` (document points) with the left button held.
void drag_held(patchy::ui::CanvasWidget& canvas, QPoint from, QPoint to) {
  const auto press = canvas.widget_position_for_document_point(from);
  const auto end = canvas.widget_position_for_document_point(to);
  send_mouse(canvas, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, (press + end) / 2, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
}

void ui_path_overlay_follows_move_drag() {
  VectorSettingsGuard settings_guard;
  {
    // A shape layer shows no path outline under Move (GitHub issue 238), before,
    // during or after a drag. An ellipse, probed where its curve runs inside the
    // bounds: the Move tool's own layer box draws on the bounds edges.
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
    shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));  // center (200,160), radii 100 x 60
    CHECK(canvas->panel_path_targeted());
    const QPoint on_curve(271, 118);  // 45 degrees up and right
    CHECK(accent_overlay_near(*canvas, on_curve));  // drawn under the Ellipse tool
    canvas->set_tool(patchy::ui::CanvasTool::Move);
    QApplication::processEvents();
    CHECK(!accent_overlay_near(*canvas, on_curve));
    drag_held(*canvas, QPoint(200, 160), QPoint(300, 160));
    CHECK(!accent_overlay_near(*canvas, on_curve + QPoint(100, 0)));
    send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(QPoint(300, 160)),
               Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(!accent_overlay_near(*canvas, on_curve + QPoint(100, 0)));
  }

  // A vector mask's outline draws with any tool and rides its layer's Move drag.
  patchy::Document base(400, 300, patchy::PixelFormat::rgb8());
  base.add_pixel_layer("Red", solid_pixels(400, 300, patchy::PixelFormat::rgb8(), QColor(200, 30, 30)));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(base), QStringLiteral("Masked Move"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"))->setCurrentIndex(1);  // Path
  window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"))->setValue(0);
  shape_drag(*canvas, QPoint(40, 60), QPoint(200, 240));
  window.findChild<QAction*>(QStringLiteral("layerVectorMaskCurrentPathAction"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::VectorMask);
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  QApplication::processEvents();
  CHECK(accent_overlay_near(*canvas, QPoint(200, 150)));  // the mask's right edge
  // Drag 100 px right without releasing: the right edge previews at x=300, and
  // the old edge lies inside the moved red area (no accent there).
  drag_held(*canvas, QPoint(120, 150), QPoint(220, 150));
  CHECK(accent_overlay_near(*canvas, QPoint(300, 150)));
  CHECK(!accent_overlay_near(*canvas, QPoint(200, 150)));
  send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(QPoint(220, 150)),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(accent_overlay_near(*canvas, QPoint(300, 150)));
}

// GitHub issue 238: a selected shape layer's path outline draws only under the
// path and shape tools, as in Photoshop; under Brush or Move it read as part of
// the artwork. View > Show Target Path still hides it under a path tool.
void ui_shape_layer_outline_shows_only_under_path_and_shape_tools() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220); its path row auto-targets
  CHECK(canvas->panel_path_targeted());
  const auto outline_shown = [&](patchy::ui::CanvasTool tool) {
    canvas->set_tool(tool);
    QApplication::processEvents();
    // Left and top edge midpoints, away from the corner anchors.
    return accent_overlay_near(*canvas, QPoint(100, 160)) && accent_overlay_near(*canvas, QPoint(200, 100));
  };
  using patchy::ui::CanvasTool;
  for (const auto tool : {CanvasTool::Brush, CanvasTool::Move, CanvasTool::Eraser, CanvasTool::Marquee,
                          CanvasTool::Text, CanvasTool::Eyedropper}) {
    CHECK(!outline_shown(tool));
  }
  for (const auto tool : {CanvasTool::PathSelect, CanvasTool::DirectSelect, CanvasTool::Pen,
                          CanvasTool::ConvertPoint, CanvasTool::Rectangle, CanvasTool::Ellipse,
                          CanvasTool::Polygon, CanvasTool::Line, CanvasTool::CustomShape}) {
    CHECK(outline_shown(tool));
  }
  CHECK(canvas->panel_path_targeted());  // switching tools never touched the targeting

  auto* toggle = window.findChild<QAction*>(QStringLiteral("viewToggleTargetPathAction"));
  CHECK(toggle != nullptr);
  CHECK(outline_shown(CanvasTool::PathSelect));
  toggle->trigger();
  QApplication::processEvents();
  CHECK(!accent_overlay_near(*canvas, QPoint(100, 160)));
  toggle->trigger();
  QApplication::processEvents();
  CHECK(accent_overlay_near(*canvas, QPoint(100, 160)));
}

void ui_path_overlay_follows_free_transform() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  CHECK(canvas->panel_path_targeted());
  // The Rectangle tool stays active, so the shape layer's outline draws (GitHub issue 238).
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Rectangle);

  auto* free_transform_action = window.findChild<QAction*>(QStringLiteral("editFreeTransformAction"));
  CHECK(free_transform_action != nullptr);
  free_transform_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  // Drag the bottom-right handle to (500,340) and hold: the outline's right
  // edge previews at x=500 while the old edge is inside the scaled fill.
  const auto press = canvas->widget_position_for_document_point(QPoint(300, 220));
  const auto to = canvas->widget_position_for_document_point(QPoint(500, 340));
  send_mouse(*canvas, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, (press + to) / 2, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(accent_overlay_near(*canvas, QPoint(500, 220)));
  CHECK(!accent_overlay_near(*canvas, QPoint(300, 160)));
  send_mouse(*canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(accent_overlay_near(*canvas, QPoint(500, 220)));
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(accent_overlay_near(*canvas, QPoint(300, 160)));
}

void ui_layer_dialogs_refuse_during_transform() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);

  auto* free_transform_action = window.findChild<QAction*>(QStringLiteral("editFreeTransformAction"));
  CHECK(free_transform_action != nullptr);
  free_transform_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  // Shape Appearance refuses with a status hint and opens nothing; the layer
  // is untouched and the session survives.
  bool appearance_seen = false;
  QTimer::singleShot(0, [&appearance_seen] {
    appearance_seen =
        patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeAppearanceDialog")) != nullptr;
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();
  CHECK(!appearance_seen);
  CHECK(canvas->free_transform_active());
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("transform")));
  CHECK(!document.find_layer(layer_id)->vector_shape()->stroke.enabled);

  // Layer Style (Blending Options) refuses the same way.
  bool style_seen = false;
  QTimer::singleShot(0, [&style_seen] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (qobject_cast<QDialog*>(widget) != nullptr && widget->isVisible() &&
          widget->windowTitle().contains(QStringLiteral("Layer Style"))) {
        style_seen = true;
      }
    }
  });
  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(!style_seen);
  CHECK(canvas->free_transform_active());

  // After the session ends, the dialog opens again (and cancels cleanly).
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  appearance_seen = false;
  QTimer::singleShot(0, [&appearance_seen] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    appearance_seen = dialog != nullptr;
    if (dialog != nullptr) {
      dialog->reject();
    }
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();
  CHECK(appearance_seen);
}

void ui_shape_appearance_dialog_edits_opacity_feather_and_stroke_opacity() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  const auto history_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  QTimer::singleShot(0, [&] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("shapeLayerOpacitySpin"));
    auto* fill_opacity = dialog->findChild<QSpinBox*>(QStringLiteral("shapeLayerFillOpacitySpin"));
    auto* stroke_width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeStrokeWidthSpin"));
    auto* stroke_opacity = dialog->findChild<QSpinBox*>(QStringLiteral("shapeStrokeOpacitySpin"));
    auto* feather = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeFeatherSpin"));
    auto* density = dialog->findChild<QSpinBox*>(QStringLiteral("shapeDensitySpin"));
    CHECK(opacity != nullptr && fill_opacity != nullptr && stroke_width != nullptr &&
          stroke_opacity != nullptr && feather != nullptr && density != nullptr);
    CHECK(opacity->value() == 100 && fill_opacity->value() == 100 && density->value() == 100);
    CHECK(stroke_opacity->isEnabled());  // the stroke rows stay enabled (issue 311)
    opacity->setValue(50);
    fill_opacity->setValue(40);
    stroke_width->setValue(3.0);
    stroke_opacity->setValue(30);
    feather->setValue(4.0);
    density->setValue(60);
    QApplication::processEvents();
    dialog->accept();
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();
  auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(std::abs(layer->opacity() - 0.5F) < 0.01F);
  CHECK(std::abs(layer->fill_opacity() - 0.4F) < 0.01F);
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  CHECK(content->stroke.enabled);
  CHECK(std::abs(content->stroke.opacity - 0.3) < 1e-6);
  CHECK(std::abs(content->feather - 4.0) < 1e-9);
  CHECK(content->density == 153);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == history_before + 1);
  // The Layers panel mirrors the new opacity, and the edge is soft: partial
  // alpha on the path edge, and the density floor far from the shape.
  auto* panel_opacity = window.findChild<QSpinBox*>(QStringLiteral("layerOpacitySpin"));
  CHECK(panel_opacity != nullptr && panel_opacity->value() == 50);
  const auto bounds = layer->bounds();
  CHECK(bounds.x == 0 && bounds.width == document.width());  // density floors the whole canvas
  const auto edge_alpha = static_cast<int>(layer->pixels().pixel(100 - bounds.x, 160 - bounds.y)[3]);
  CHECK(edge_alpha > 130 && edge_alpha < 230);  // half coverage over the 40% floor
  const auto far_alpha = static_cast<int>(layer->pixels().pixel(20 - bounds.x, 20 - bounds.y)[3]);
  CHECK(far_alpha >= 95 && far_alpha <= 110);

  // Cancel restores everything, including the layer opacity.
  QTimer::singleShot(0, [&] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("shapeLayerOpacitySpin"));
    CHECK(opacity != nullptr && opacity->value() == 50);
    opacity->setValue(10);
    dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeFeatherSpin"))->setValue(20.0);
    QApplication::processEvents();
    dialog->reject();
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();
  layer = document.find_layer(layer_id);
  CHECK(std::abs(layer->opacity() - 0.5F) < 0.01F);
  CHECK(std::abs(layer->vector_shape()->feather - 4.0) < 1e-9);

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  layer = document.find_layer(layer_id);
  CHECK(std::abs(layer->opacity() - 1.0F) < 0.01F);
  CHECK(std::abs(layer->vector_shape()->feather) < 1e-9);
  CHECK(layer->vector_shape()->density == 255);
}

void ui_shape_geometry_link_keeps_aspect() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);  // 200 x 120

  QTimer::singleShot(0, [&] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* link = dialog->findChild<QToolButton*>(QStringLiteral("shapeGeometryLinkButton"));
    auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeGeometryWidthSpin"));
    auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeGeometryHeightSpin"));
    CHECK(link != nullptr && width != nullptr && height != nullptr);
    CHECK(!link->isChecked());
    // Unlinked: height stays put.
    width->setValue(300.0);
    CHECK(std::abs(height->value() - 120.0) < 1e-9);
    width->setValue(200.0);
    link->setChecked(true);  // ratio 200:120 captured here
    width->setValue(400.0);
    CHECK(std::abs(height->value() - 240.0) < 0.05);
    height->setValue(60.0);
    CHECK(std::abs(width->value() - 100.0) < 0.05);
    width->setValue(400.0);
    QApplication::processEvents();
    dialog->accept();
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();
  const auto* content = document.find_layer(layer_id)->vector_shape();
  CHECK(content != nullptr && content->origination.size() == 1);
  CHECK(std::abs(content->origination[0].right - 500.0) < 0.5);
  CHECK(std::abs(content->origination[0].bottom - 340.0) < 0.5);
}

void ui_shape_geometry_radius_link_edits_all_corners() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* tool_radius = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(tool_radius != nullptr);
  tool_radius->setValue(20);  // every corner 20: the dialog opens linked
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));
  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());

  const auto find_corners = [](QDialog& dialog) {
    return std::array<QDoubleSpinBox*, 4>{
        dialog.findChild<QDoubleSpinBox*>(QStringLiteral("shapeGeometryRadiusTopLeftSpin")),
        dialog.findChild<QDoubleSpinBox*>(QStringLiteral("shapeGeometryRadiusTopRightSpin")),
        dialog.findChild<QDoubleSpinBox*>(QStringLiteral("shapeGeometryRadiusBottomRightSpin")),
        dialog.findChild<QDoubleSpinBox*>(QStringLiteral("shapeGeometryRadiusBottomLeftSpin"))};
  };
  bool first_open_checked = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* radius_link = dialog->findChild<QToolButton*>(QStringLiteral("shapeGeometryRadiusLinkButton"));
    const auto corners = find_corners(*dialog);
    CHECK(radius_link != nullptr && corners[0] != nullptr && corners[1] != nullptr &&
          corners[2] != nullptr && corners[3] != nullptr);
    if (radius_link == nullptr || corners[3] == nullptr) {
      dialog->reject();
      return;
    }
    // Equal corners at open: linked by default, and one edit moves all four.
    CHECK(radius_link->isChecked());
    corners[0]->setValue(30.0);
    for (const auto* corner : corners) {
      CHECK(std::abs(corner->value() - 30.0) < 1e-9);
    }
    // Unlinked: a corner edits alone.
    radius_link->setChecked(false);
    corners[1]->setValue(5.0);
    CHECK(std::abs(corners[0]->value() - 30.0) < 1e-9);
    CHECK(std::abs(corners[2]->value() - 30.0) < 1e-9);
    CHECK(std::abs(corners[3]->value() - 30.0) < 1e-9);
    // Relinking copies nothing by itself; the next edit brings them together.
    radius_link->setChecked(true);
    CHECK(std::abs(corners[1]->value() - 5.0) < 1e-9);
    corners[3]->setValue(12.0);
    for (const auto* corner : corners) {
      CHECK(std::abs(corner->value() - 12.0) < 1e-9);
    }
    // Captured linked: the radius chain lit, the W / H chain off.
    save_widget_artifact("shape-appearance-dialog-geometry-links", *dialog);
    radius_link->setChecked(false);
    corners[1]->setValue(5.0);
    // Layout: each link is a normal small button centered between the rows
    // it ties, sitting on a bracket widget that spans those rows, between the
    // label column and the fields (the Image Size dialog's Width / Height link).
    auto* size_link = dialog->findChild<QToolButton*>(QStringLiteral("shapeGeometryLinkButton"));
    auto* size_bracket = dialog->findChild<QWidget*>(QStringLiteral("shapeGeometryLinkBracket"));
    auto* radius_bracket = dialog->findChild<QWidget*>(QStringLiteral("shapeGeometryRadiusLinkBracket"));
    auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeGeometryWidthSpin"));
    auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeGeometryHeightSpin"));
    CHECK(size_link != nullptr && size_bracket != nullptr && radius_bracket != nullptr &&
          width != nullptr && height != nullptr);
    if (size_link != nullptr && size_bracket != nullptr && radius_bracket != nullptr &&
        width != nullptr && height != nullptr) {
      const auto rect_in_dialog = [dialog](const QWidget& widget) {
        return QRect(widget.mapTo(dialog, QPoint(0, 0)), widget.size());
      };
      const auto link_rect = rect_in_dialog(*size_link);
      const auto bracket_rect = rect_in_dialog(*size_bracket);
      const int width_top = width->mapTo(dialog, QPoint(0, 0)).y();
      const int height_bottom = height->mapTo(dialog, QPoint(0, height->height())).y();
      CHECK(link_rect.width() <= 26 && link_rect.height() <= 26);
      CHECK(bracket_rect.top() <= width_top && bracket_rect.bottom() + 1 >= height_bottom);
      CHECK(link_rect.top() > width_top && link_rect.bottom() < height_bottom);
      CHECK(link_rect.right() < width->mapTo(dialog, QPoint(0, 0)).x());
      const auto radius_rect = rect_in_dialog(*radius_link);
      const auto radius_bracket_rect = rect_in_dialog(*radius_bracket);
      const int corners_top = corners[0]->mapTo(dialog, QPoint(0, 0)).y();
      const int corners_bottom = corners[3]->mapTo(dialog, QPoint(0, corners[3]->height())).y();
      CHECK(radius_rect.width() <= 26 && radius_rect.height() <= 26);
      CHECK(radius_bracket_rect.top() <= corners_top && radius_bracket_rect.bottom() + 1 >= corners_bottom);
      CHECK(radius_rect.top() > corners_top && radius_rect.bottom() < corners_bottom);
      CHECK(radius_rect.right() < corners[0]->mapTo(dialog, QPoint(0, 0)).x());
      CHECK(radius_rect.left() == link_rect.left());
      CHECK(radius_rect.top() > link_rect.bottom());
    }
    first_open_checked = true;
    dialog->accept();
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();
  CHECK(first_open_checked);
  const auto* content = document.find_layer(*layer_id)->vector_shape();
  CHECK(content != nullptr && content->origination.size() == 1);
  if (content != nullptr && content->origination.size() == 1) {
    // Three corners from the linked edit, one from the lone edit.
    const auto& radii = content->origination[0].corner_radii;
    CHECK(std::abs(radii[0] - 12.0) < 1e-6);
    CHECK(std::abs(radii[1] - 5.0) < 1e-6);
    CHECK(std::abs(radii[2] - 12.0) < 1e-6);
    CHECK(std::abs(radii[3] - 12.0) < 1e-6);
  }

  // Distinct corners: the dialog reopens unlinked, so one edit cannot flatten
  // them by accident.
  bool reopened = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* radius_link = dialog->findChild<QToolButton*>(QStringLiteral("shapeGeometryRadiusLinkButton"));
    const auto corners = find_corners(*dialog);
    CHECK(radius_link != nullptr && !radius_link->isChecked());
    CHECK(corners[1] != nullptr && std::abs(corners[1]->value() - 5.0) < 1e-6);
    reopened = true;
    dialog->reject();
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();
  CHECK(reopened);
}

void ui_shape_appearance_dialog_fits_1080p_and_has_two_columns() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(12);  // rounded rect: the tallest Geometry group
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));

  bool checked = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    // At open (solid fill, stroke off) the dialog hugs its visible rows.
    {
      auto* opened_page = dialog->findChild<QWidget*>(QStringLiteral("shapeAppearancePage"));
      CHECK(opened_page != nullptr);
      if (opened_page != nullptr) {
        CHECK(dialog->height() <= opened_page->sizeHint().height() + 80);
      }
      // Solid fill + stroke off: the left column's 11 rows set the height,
      // well under the all-rows-visible measurement (about 900 px).
      CHECK(dialog->height() <= 720);
      // ...and not one row short: unless the screen cap applied, the whole
      // page is in view at open, no scrollbar (the Preview row under the
      // scroll area used to go uncounted; Seth, October 2026).
      auto* opened_scroll = dialog->findChild<QScrollArea*>(QStringLiteral("shapeAppearanceScroll"));
      CHECK(opened_scroll != nullptr);
      if (opened_page != nullptr && opened_scroll != nullptr && dialog->screen() != nullptr &&
          dialog->height() < dialog->screen()->availableGeometry().height() - 40) {
        CHECK(opened_scroll->viewport()->height() >= opened_page->sizeHint().height());
        // (The bar's visibility lags a layout pass; its range is current.)
        CHECK(opened_scroll->verticalScrollBar()->maximum() == 0);
      }
    }
    // Worst case: pattern fill rows plus a gradient stroke.
    auto* fill_kind = dialog->findChild<QComboBox*>(QStringLiteral("shapeFillKindCombo"));
    auto* stroke_paint = dialog->findChild<QComboBox*>(QStringLiteral("shapeStrokePaintCombo"));
    CHECK(fill_kind != nullptr && stroke_paint != nullptr);
    fill_kind->setCurrentIndex(fill_kind->findData(static_cast<int>(patchy::VectorFillKind::Pattern)));
    stroke_paint->setCurrentIndex(stroke_paint->findData(static_cast<int>(patchy::VectorFillKind::Gradient)));
    QApplication::processEvents();
    // Fits a 1080p screen (about 1040 px usable) even in the worst case, and
    // never grows past the screen it is on.
    CHECK(dialog->height() <= 1000);
    CHECK(dialog->width() <= 900);
    // Sized to the VISIBLE rows at open (solid fill, stroke off): no empty band
    // from rows hidden after the width measurement.
    auto* page = dialog->findChild<QWidget*>(QStringLiteral("shapeAppearancePage"));
    CHECK(page != nullptr);
    if (const auto* screen = dialog->screen(); screen != nullptr) {
      CHECK(dialog->height() <= screen->availableGeometry().height());
    }
    CHECK(dialog->findChild<QScrollArea*>(QStringLiteral("shapeAppearanceScroll")) != nullptr);
    // Two columns: Layer / Geometry / Edge left, Fill / Stroke right.
    auto* feather = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeFeatherSpin"));
    auto* stroke_width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeStrokeWidthSpin"));
    auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("shapeLayerOpacitySpin"));
    CHECK(feather != nullptr && stroke_width != nullptr && opacity != nullptr);
    CHECK(feather->mapTo(dialog, QPoint(0, 0)).x() < stroke_width->mapTo(dialog, QPoint(0, 0)).x());
    CHECK(opacity->mapTo(dialog, QPoint(0, 0)).x() < fill_kind->mapTo(dialog, QPoint(0, 0)).x());
    // The right column's steppers stay inside the viewport after the paint
    // kinds switched (the width was measured with every row visible).
    auto* scroll = dialog->findChild<QScrollArea*>(QStringLiteral("shapeAppearanceScroll"));
    auto* angle_increase =
        dialog->findChild<QPushButton*>(QStringLiteral("shapeStrokeGradientAngleSpinIncreaseButton"));
    CHECK(scroll != nullptr && angle_increase != nullptr && angle_increase->isVisible());
    const auto right_edge = angle_increase->mapTo(scroll->viewport(), QPoint(angle_increase->width(), 0)).x();
    CHECK(right_edge <= scroll->viewport()->width());
    // The buttons sit under the scroll area and stay visible.
    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    CHECK(buttons != nullptr && buttons->isVisible());
    CHECK(buttons->mapTo(dialog, QPoint(0, 0)).y() + buttons->height() <= dialog->height());
    checked = true;
    dialog->reject();
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();
  CHECK(checked);
}

void ui_shape_appearance_spins_have_step_buttons() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)

  int spins_checked = 0;
  QTimer::singleShot(0, [&] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    const auto check_buttons = [&](const QString& name) {
      auto* decrease = dialog->findChild<QPushButton*>(name + QStringLiteral("DecreaseButton"));
      auto* increase = dialog->findChild<QPushButton*>(name + QStringLiteral("IncreaseButton"));
      CHECK(decrease != nullptr && increase != nullptr);
      if (decrease == nullptr || increase == nullptr) {
        return;
      }
      CHECK(!decrease->icon().isNull() && !increase->icon().isNull());
      CHECK(decrease->autoRepeat() && increase->autoRepeat());
      ++spins_checked;
    };
    for (const auto* spin : dialog->findChildren<QSpinBox*>()) {
      check_buttons(spin->objectName());
    }
    for (const auto* spin : dialog->findChildren<QDoubleSpinBox*>()) {
      check_buttons(spin->objectName());
    }
    // A hidden pattern row hides its buttons with it (fill is Solid here).
    auto* pattern_scale = dialog->findChild<QSpinBox*>(QStringLiteral("shapePatternScaleSpin"));
    auto* pattern_scale_increase =
        dialog->findChild<QPushButton*>(QStringLiteral("shapePatternScaleSpinIncreaseButton"));
    CHECK(pattern_scale != nullptr && !pattern_scale->isVisible());
    CHECK(pattern_scale_increase != nullptr && !pattern_scale_increase->isVisible());
    // The stroke width stays usable with the stroke off: it is the switch (issue 311).
    auto* stroke_width_increase =
        dialog->findChild<QPushButton*>(QStringLiteral("shapeStrokeWidthSpinIncreaseButton"));
    CHECK(stroke_width_increase != nullptr && stroke_width_increase->isEnabled());
    // Stepping the width by one grows the live rect by one pixel.
    auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeGeometryWidthSpin"));
    auto* width_increase = dialog->findChild<QPushButton*>(QStringLiteral("shapeGeometryWidthSpinIncreaseButton"));
    CHECK(width != nullptr && width_increase != nullptr);
    const auto before = width->value();
    width_increase->click();
    CHECK(std::abs(width->value() - (before + 1.0)) < 1e-9);
    QApplication::processEvents();
    dialog->accept();
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();
  CHECK(spins_checked >= 17);
  const auto* content = document.find_layer(layer_id)->vector_shape();
  CHECK(content != nullptr && content->origination.size() == 1);
  CHECK(std::abs(content->origination[0].right - 301.0) < 0.5);
}

void ui_shape_appearance_reset_restores_factory_defaults() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  require_action(window, "toolRectAction")->trigger();  // the options-bar mirrors need the real tool
  QApplication::processEvents();
  const auto layer_id = make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)
  canvas->set_primary_color(Qt::red);

  bool reset_seen = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* fill_kind = dialog->findChild<QComboBox*>(QStringLiteral("shapeFillKindCombo"));
    auto* stroke_paint = dialog->findChild<QComboBox*>(QStringLiteral("shapeStrokePaintCombo"));
    auto* stroke_width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeStrokeWidthSpin"));
    auto* stroke_opacity = dialog->findChild<QSpinBox*>(QStringLiteral("shapeStrokeOpacitySpin"));
    auto* feather = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeFeatherSpin"));
    auto* density = dialog->findChild<QSpinBox*>(QStringLiteral("shapeDensitySpin"));
    auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("shapeLayerOpacitySpin"));
    auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeGeometryWidthSpin"));
    auto* reset = dialog->findChild<QPushButton*>(QStringLiteral("shapeAppearanceResetButton"));
    CHECK(fill_kind != nullptr && stroke_paint != nullptr &&
          stroke_width != nullptr && stroke_opacity != nullptr && feather != nullptr &&
          density != nullptr && opacity != nullptr && width != nullptr && reset != nullptr);
    fill_kind->setCurrentIndex(fill_kind->findData(static_cast<int>(patchy::VectorFillKind::Pattern)));
    stroke_paint->setCurrentIndex(stroke_paint->findData(static_cast<int>(patchy::VectorFillKind::Gradient)));
    stroke_width->setValue(12.0);
    stroke_opacity->setValue(40);
    feather->setValue(4.0);
    density->setValue(60);
    opacity->setValue(50);
    width->setValue(400.0);
    QApplication::processEvents();
    reset->click();
    QApplication::processEvents();
    reset_seen = true;
    CHECK(fill_kind->currentData().toInt() == static_cast<int>(patchy::VectorFillKind::Solid));
    CHECK(stroke_paint->currentData().toInt() == static_cast<int>(patchy::VectorFillKind::Solid));
    CHECK(stroke_width->value() == 0.0);  // the default stroke is off, which reads 0 (issue 311)
    CHECK(stroke_opacity->value() == 100);
    CHECK(std::abs(feather->value()) < 1e-9);
    CHECK(density->value() == 100);
    CHECK(opacity->value() == 100);
    CHECK(std::abs(width->value() - 400.0) < 1e-9);  // geometry survives the reset
    dialog->accept();
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();
  CHECK(reset_seen);
  auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  CHECK(content->fill.kind == patchy::VectorFillKind::Solid);
  CHECK(content->fill.color.red == 255 && content->fill.color.green == 0 && content->fill.color.blue == 0);
  CHECK(!content->stroke.enabled);
  CHECK(std::abs(content->stroke.width - 3.0) < 1e-9);
  CHECK(content->stroke.alignment == patchy::VectorStrokeAlignment::Inside);
  CHECK(std::abs(content->feather) < 1e-9);
  CHECK(content->density == 255);
  CHECK(std::abs(layer->opacity() - 1.0F) < 1e-6F);
  CHECK(content->origination.size() == 1 && std::abs(content->origination[0].right - 500.0) < 0.5);
  // The options-bar defaults synced from the reset layer: the next shape is red.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  shape_drag(*canvas, QPoint(600, 500), QPoint(700, 600));
  const auto* next = document.find_layer(*document.active_layer_id())->vector_shape();
  CHECK(next != nullptr && next->fill.kind == patchy::VectorFillKind::Solid && next->fill.color.red == 255 &&
        next->fill.color.green == 0);
}

void ui_options_bar_mode_combo_stays_first_for_shape_tools() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  require_action(window, "toolRectAction")->trigger();
  QApplication::processEvents();
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  auto* brush_opacity = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
  auto* fill_swatch = window.findChild<QToolButton*>(QStringLiteral("vectorFillSwatchButton"));
  CHECK(mode_combo != nullptr && brush_opacity != nullptr && fill_swatch != nullptr);
  const auto shape_position = mode_combo->mapTo(&window, QPoint(0, 0));
  CHECK(!brush_opacity->isVisible());
  CHECK(shape_position.x() < fill_swatch->mapTo(&window, QPoint(0, 0)).x());

  mode_combo->setCurrentIndex(2);  // Pixels: the brush trio appears AFTER Mode
  QApplication::processEvents();
  CHECK(brush_opacity->isVisible());
  CHECK(mode_combo->mapTo(&window, QPoint(0, 0)) == shape_position);
  CHECK(shape_position.x() < brush_opacity->mapTo(&window, QPoint(0, 0)).x());

  mode_combo->setCurrentIndex(0);
  QApplication::processEvents();
  CHECK(mode_combo->mapTo(&window, QPoint(0, 0)) == shape_position);
}

void ui_shape_appearance_entry_points_open_the_dialog() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);  // (100,100)-(300,220)

  const auto opens_dialog = [&](const std::function<void()>& trigger) {
    bool seen = false;
    QTimer::singleShot(0, [&seen] {
      auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
      seen = dialog != nullptr;
      if (dialog != nullptr) {
        dialog->reject();
      }
    });
    trigger();
    QApplication::processEvents();
    return seen;
  };

  // (a) Options-bar button in Shape mode.
  require_action(window, "toolRectAction")->trigger();
  QApplication::processEvents();
  auto* appearance_button = window.findChild<QPushButton*>(QStringLiteral("vectorAppearanceButton"));
  CHECK(appearance_button != nullptr && appearance_button->isVisible() && appearance_button->isEnabled());
  CHECK(opens_dialog([&] { appearance_button->click(); }));

  // (b) Layer > Shape > Shape Appearance...
  auto* menu_action = require_action(window, "layerShapeAppearanceAction");
  CHECK(menu_action->isEnabled());
  CHECK(opens_dialog([&] { menu_action->trigger(); }));

  // (c) Properties panel button.
  auto* properties_button = window.findChild<QPushButton*>(QStringLiteral("propertiesEditAppearanceButton"));
  CHECK(properties_button != nullptr && !properties_button->isHidden());
  auto* shape_label = window.findChild<QLabel*>(QStringLiteral("activeLayerShapeLabel"));
  CHECK(shape_label != nullptr && shape_label->text().contains(QStringLiteral("Stroke off")));
  CHECK(opens_dialog([&] { properties_button->click(); }));

  // (d) Path Select double-click on the shape; empty canvas does nothing.
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  CHECK(appearance_button->isVisible() && appearance_button->isEnabled());
  CHECK(opens_dialog([&] {
    send_double_click(*canvas, canvas->widget_position_for_document_point(QPoint(200, 160)));
  }));
  CHECK(!opens_dialog([&] {
    send_double_click(*canvas, canvas->widget_position_for_document_point(QPoint(700, 600)));
  }));

  // Without a shape layer the entry points go quiet (a fresh pixel layer
  // becomes active).
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  require_action(window, "toolRectAction")->trigger();
  QApplication::processEvents();
  CHECK(!appearance_button->isEnabled());
  CHECK(!menu_action->isEnabled());
  CHECK(properties_button->isHidden());
}

// GitHub issue 273: the Properties panel's Edit Appearance button stayed up after the shape
// layer was deselected (a blank click in the Layers panel, Select > Deselect Layers).
void ui_properties_edit_appearance_hides_when_layers_are_deselected() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);
  patchy::ui::MainWindowTestAccess::show_panel(window, QStringLiteral("propertiesDock"));
  QApplication::processEvents();
  auto* properties_button = window.findChild<QPushButton*>(QStringLiteral("propertiesEditAppearanceButton"));
  auto* properties = window.findChild<QWidget*>(QStringLiteral("propertiesScrollArea"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(properties_button != nullptr && properties != nullptr && layer_list != nullptr);
  CHECK(!properties_button->isHidden());
  save_widget_artifact("issue273_properties_shape_selected", *properties);

  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto shape_id = doc.active_layer_id();
  CHECK(shape_id.has_value());
  require_action(window, "selectDeselectLayersAction")->trigger();
  QApplication::processEvents();
  CHECK(!doc.active_layer_id().has_value());
  save_widget_artifact("issue273_properties_after_deselect", *properties);
  CHECK(properties_button->isHidden());

  // Selecting the shape again brings the button back; a click on the Layers panel's blank
  // area (Properties collapsed, so the list has room below its rows) hides it again.
  patchy::ui::MainWindowTestAccess::panel_column(window)->set_panel_expanded(QStringLiteral("propertiesDock"), false);
  QApplication::processEvents();
  for (int row = 0; row < layer_list->count(); ++row) {
    if (static_cast<patchy::LayerId>(layer_list->item(row)->data(Qt::UserRole).toULongLong()) == *shape_id) {
      layer_list->setCurrentRow(row);
    }
  }
  QApplication::processEvents();
  CHECK(doc.active_layer_id() == shape_id);
  CHECK(!properties_button->isHidden());
  auto* viewport = layer_list->viewport();
  const auto last_row = layer_list->visualItemRect(layer_list->item(layer_list->count() - 1));
  const QPoint blank(viewport->width() / 2, std::min(viewport->height() - 4, last_row.bottom() + 30));
  CHECK(layer_list->itemAt(blank) == nullptr);
  send_mouse(*viewport, QEvent::MouseButtonPress, blank, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*viewport, QEvent::MouseButtonRelease, blank, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!doc.active_layer_id().has_value());
  CHECK(properties_button->isHidden());
}


// GitHub issue 273 follow-up (the reporter): a click on Edit Appearance with no shape layer
// selected crashed. The entry is safe on its own: forced visible and clicked with a pixel
// layer active, with no active layer and with no document, it reports the usual status
// message, opens nothing and changes nothing; the Properties W and H fields stay inert too.
void ui_properties_shape_controls_are_safe_without_a_shape() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::show_panel(window, QStringLiteral("propertiesDock"));
  QApplication::processEvents();
  auto* button = window.findChild<QPushButton*>(QStringLiteral("propertiesEditAppearanceButton"));
  auto* width = window.findChild<QDoubleSpinBox*>(QStringLiteral("propertiesShapeWidthSpin"));
  auto* height = window.findChild<QDoubleSpinBox*>(QStringLiteral("propertiesShapeHeightSpin"));
  CHECK(button != nullptr && width != nullptr && height != nullptr);
  if (button == nullptr || width == nullptr || height == nullptr) {
    return;
  }
  const auto expected = QStringLiteral("Select an unlocked, editable shape layer");
  const auto click_forced = [&] {
    bool dialog_opened = false;
    QTimer::singleShot(0, [&dialog_opened] {
      if (auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
          dialog != nullptr) {
        dialog_opened = true;
        dialog->reject();
      }
    });
    window.statusBar()->clearMessage();
    button->setVisible(true);
    button->setEnabled(true);
    button->click();
    QApplication::processEvents();
    CHECK(!dialog_opened);
    return window.statusBar()->currentMessage();
  };
  const auto type_sizes = [&] {
    for (auto* spin : {width, height}) {
      spin->setEnabled(true);
      spin->setValue(spin->value() + 17.0);
    }
    QApplication::processEvents();
  };

  // The default document's Paint Layer: a pixel layer.
  const auto undo_depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  CHECK(click_forced().contains(expected));
  type_sizes();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth);

  // No active layer.
  require_action(window, "selectDeselectLayersAction")->trigger();
  QApplication::processEvents();
  CHECK(!patchy::ui::MainWindowTestAccess::document(window).active_layer_id().has_value());
  CHECK(click_forced().contains(expected));
  type_sizes();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth);

  // No document at all.
  require_action(window, "fileCloseAllAction")->trigger();
  QApplication::processEvents();
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr && tabs->count() == 0);
  // Before the fix the size fields reached document() here, which throws with no document
  // (a crash from inside a Qt slot), and the button returned without a word.
  type_sizes();
  CHECK(click_forced().contains(expected));
}

// The active-shape W / H readouts belong to Shape mode: Path and Pixels have
// no shape layer to size, and Pixels already carries the fixed-size Width /
// Height row (two width/height pairs on one bar was the September 2026 bug).
void ui_shape_size_row_shows_in_shape_mode_only() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  auto* width_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorShapeWidthSpin"));
  auto* link_button = window.findChild<QPushButton*>(QStringLiteral("vectorShapeLinkSizeButton"));
  CHECK(mode_combo != nullptr && width_spin != nullptr && link_button != nullptr);
  require_action(window, "toolRectAction")->trigger();
  QApplication::processEvents();
  mode_combo->setCurrentIndex(0);  // Shape
  QApplication::processEvents();
  CHECK(width_spin->isVisible());
  CHECK(!width_spin->isEnabled());  // no shape layer yet: a disabled readout
  mode_combo->setCurrentIndex(1);  // Path
  QApplication::processEvents();
  CHECK(!width_spin->isVisible());
  CHECK(!link_button->isVisible());
  mode_combo->setCurrentIndex(2);  // Pixels
  QApplication::processEvents();
  CHECK(!width_spin->isVisible());
  mode_combo->setCurrentIndex(0);  // Shape
  QApplication::processEvents();
  CHECK(width_spin->isVisible());
  // The path selection tools and Move show the row only with an editable
  // shape layer.
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  CHECK(!width_spin->isVisible());
  require_action(window, "toolMoveAction")->trigger();
  QApplication::processEvents();
  CHECK(!width_spin->isVisible());
}

// A right-click on the active shape layer adds the shape commands to the
// canvas menu (docs/tools.md, "Canvas right-click menu").
void ui_shape_context_menu_offers_shape_commands() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action(window, "toolRectAction")->trigger();
  QApplication::processEvents();
  canvas->setFocus(Qt::MouseFocusReason);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));
  require_action(window, "toolMoveAction")->trigger();
  QApplication::processEvents();

  const auto visible_menu = [canvas]() -> QMenu* {
    for (auto* menu : canvas->findChildren<QMenu*>(QStringLiteral("canvasContextMenu"))) {
      if (menu->isVisible()) {
        return menu;
      }
    }
    return nullptr;
  };
  const auto right_click = [&](QPoint document_point) {
    const auto point = canvas->widget_position_for_document_point(document_point);
    send_mouse(*canvas, QEvent::MouseButtonPress, point, Qt::RightButton, Qt::RightButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, point, Qt::RightButton, Qt::NoButton);
    QApplication::processEvents();
    return visible_menu();
  };
  const auto find_action = [](QMenu& menu, const char* object_name) -> QAction* {
    for (auto* action : menu.actions()) {
      if (action->objectName() == QLatin1String(object_name)) {
        return action;
      }
    }
    return nullptr;
  };

  auto* menu = right_click(QPoint(200, 160));
  CHECK(menu != nullptr);
  if (menu != nullptr) {
    auto* appearance = find_action(*menu, "layerShapeAppearanceAction");
    CHECK(appearance != nullptr);
    CHECK(appearance != nullptr && appearance->isEnabled());
    CHECK(find_action(*menu, "editFreeTransformAction") != nullptr);
    CHECK(find_action(*menu, "pathSimplifyAction") != nullptr);
    CHECK(find_action(*menu, "editDefineCustomShapeAction") != nullptr);
    save_widget_artifact("shape-context-menu", *menu);
    menu->close();
    QApplication::processEvents();
  }

  // Off the shape: no shape section.
  menu = right_click(QPoint(600, 500));
  CHECK(menu == nullptr || find_action(*menu, "layerShapeAppearanceAction") == nullptr);
  if (menu != nullptr) {
    menu->close();
    QApplication::processEvents();
  }
}

// Issue 53: the shape W/H readouts (options bar and Properties panel), the
// transform X/Y fields, stroke width and line weight present themselves in the
// ruler unit through the document PPI. value() stays document pixels, a typed
// plain number is read in the shown unit, arrow steps move one shown unit, and
// a ruler unit change re-renders every field.
void ui_shape_size_fields_follow_ruler_unit() {
  VectorSettingsGuard settings_guard;
  SettingsValueRestorer restore_units(QStringLiteral("view/rulerUnits"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  document.print_settings().horizontal_ppi = 300.0;
  document.print_settings().vertical_ppi = 300.0;
  patchy::ui::MainWindowTestAccess::set_ruler_unit(window, patchy::ui::MeasurementUnit::Pixels);

  auto* width_spin = window.findChild<patchy::ui::UnitSpinBox*>(QStringLiteral("vectorShapeWidthSpin"));
  auto* height_spin = window.findChild<patchy::ui::UnitSpinBox*>(QStringLiteral("vectorShapeHeightSpin"));
  auto* properties_width =
      window.findChild<patchy::ui::UnitSpinBox*>(QStringLiteral("propertiesShapeWidthSpin"));
  auto* stroke_width = window.findChild<patchy::ui::UnitSpinBox*>(QStringLiteral("vectorStrokeWidthSpin"));
  auto* line_weight = window.findChild<patchy::ui::UnitSpinBox*>(QStringLiteral("vectorLineWeightSpin"));
  auto* transform_x = window.findChild<patchy::ui::UnitSpinBox*>(QStringLiteral("freeTransformXSpin"));
  const std::array<patchy::ui::UnitSpinBox*, 6> fields{width_spin,   height_spin, properties_width,
                                                        stroke_width, line_weight, transform_x};
  for (auto* spin : fields) {
    CHECK(spin != nullptr);
    CHECK(spin->display_unit() == patchy::ui::SpinUnit::Pixels);
    CHECK(spin->display_unit_switchable());
  }
  const auto mm_suffix =
      QStringLiteral(" ") + patchy::ui::measurement_unit_suffix(patchy::ui::MeasurementUnit::Millimeters);
  const auto commit_text = [](QDoubleSpinBox& spin, const QString& text) {
    auto* editor = spin.findChild<QLineEdit*>();
    CHECK(editor != nullptr);
    editor->setText(text);
    send_key(spin, Qt::Key_Return);
    QApplication::processEvents();
  };

  require_action(window, "toolEllipseAction")->trigger();
  QApplication::processEvents();
  shape_drag(*canvas, QPoint(100, 100), QPoint(400, 250));  // 300 x 150 px
  const auto layer_id = *document.active_layer_id();
  CHECK(std::abs(width_spin->value() - 300.0) < 0.5);
  stroke_width->setValue(6.0);
  line_weight->setValue(3.0);

  // Millimeters: every field re-renders; the values stay pixels.
  patchy::ui::MainWindowTestAccess::set_ruler_unit(window, patchy::ui::MeasurementUnit::Millimeters);
  for (auto* spin : fields) {
    CHECK(spin->display_unit() == patchy::ui::SpinUnit::Millimeters);
    CHECK(spin->suffix() == mm_suffix);
  }
  CHECK(std::abs(width_spin->value() - 300.0) < 0.5);
  CHECK(width_spin->text() == QStringLiteral("25.4") + mm_suffix);
  CHECK(properties_width->text() == QStringLiteral("25.4") + mm_suffix);
  CHECK(height_spin->text() == QStringLiteral("12.7") + mm_suffix);
  CHECK(std::abs(stroke_width->value() - 6.0) < 1e-9);
  CHECK(stroke_width->text() == QStringLiteral("0.5") + mm_suffix);
  CHECK(std::abs(width_spin->singleStep() - 300.0 / 25.4) < 1e-6);  // one arrow press = 1 mm
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("view/rulerUnits")).toString() == QStringLiteral("mm"));
  }

  // A plain number typed now means millimeters: 50.8 mm is 600 px, and the
  // shape resizes to it.
  commit_text(*width_spin, QStringLiteral("50.8"));
  CHECK(std::abs(width_spin->value() - 600.0) < 0.5);
  process_events_for(450);
  const auto* content = document.find_layer(layer_id)->vector_shape();
  CHECK(content != nullptr);
  CHECK(content->origination.size() == 1);
  CHECK(std::abs(content->origination[0].right - 700.0) < 0.5);
  CHECK(properties_width->text() == QStringLiteral("50.8") + mm_suffix);

  // A unit picked from a field's menu is Photoshop's Units & Rulers change: the
  // preference, the rulers and every enrolled field follow.
  height_spin->pick_display_unit(patchy::ui::SpinUnit::Centimeters);
  CHECK(patchy::ui::MainWindowTestAccess::ruler_unit(window) == patchy::ui::MeasurementUnit::Centimeters);
  CHECK(canvas->ruler_unit() == patchy::ui::MeasurementUnit::Centimeters);
  for (auto* spin : fields) {
    CHECK(spin->display_unit() == patchy::ui::SpinUnit::Centimeters);
  }
  const auto cm_suffix =
      QStringLiteral(" ") + patchy::ui::measurement_unit_suffix(patchy::ui::MeasurementUnit::Centimeters);
  CHECK(height_spin->text() == QStringLiteral("1.27") + cm_suffix);
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("view/rulerUnits")).toString() == QStringLiteral("cm"));
  }

  // A typed unit token is the field's own choice: the preference and the other
  // fields stay in centimeters.
  commit_text(*height_spin, QStringLiteral("0.5 in"));
  CHECK(std::abs(height_spin->value() - 150.0) < 0.5);
  CHECK(height_spin->display_unit() == patchy::ui::SpinUnit::Inches);
  CHECK(height_spin->text() == QStringLiteral("0.500") + patchy::ui::inch_suffix());
  CHECK(patchy::ui::MainWindowTestAccess::ruler_unit(window) == patchy::ui::MeasurementUnit::Centimeters);
  CHECK(width_spin->display_unit() == patchy::ui::SpinUnit::Centimeters);

  // A new preference overrides it and lands everywhere.
  patchy::ui::MainWindowTestAccess::set_ruler_unit(window, patchy::ui::MeasurementUnit::Inches);
  for (auto* spin : fields) {
    CHECK(spin->display_unit() == patchy::ui::SpinUnit::Inches);
  }
  CHECK(width_spin->text() == QStringLiteral("2.000") + patchy::ui::inch_suffix());
  CHECK(line_weight->text() == QStringLiteral("0.010") + patchy::ui::inch_suffix());

  // Back to pixels restores the construction presentation.
  patchy::ui::MainWindowTestAccess::set_ruler_unit(window, patchy::ui::MeasurementUnit::Pixels);
  for (auto* spin : fields) {
    CHECK(spin->display_unit() == patchy::ui::SpinUnit::Pixels);
  }
  CHECK(width_spin->text() == QStringLiteral("600.0") + patchy::ui::pixel_suffix());
  CHECK(std::abs(width_spin->singleStep() - 1.0) < 1e-9);
  CHECK(width_spin->decimals() == 1);
  CHECK(line_weight->text() == QStringLiteral("3.0") + patchy::ui::pixel_suffix());
}

// A unit picked on a modal dialog's field is the same Units & Rulers change as on a
// live field: the preference, the options-bar readouts and the dialog's other
// dimension fields all follow while the dialog is still open.
void ui_shape_appearance_unit_pick_sets_ruler_unit() {
  VectorSettingsGuard settings_guard;
  SettingsValueRestorer restore_units(QStringLiteral("view/rulerUnits"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  document.print_settings().horizontal_ppi = 300.0;
  document.print_settings().vertical_ppi = 300.0;
  patchy::ui::MainWindowTestAccess::set_ruler_unit(window, patchy::ui::MeasurementUnit::Pixels);
  make_rect_shape_layer(window, *canvas);  // 200 x 120 px
  auto* options_width = window.findChild<patchy::ui::UnitSpinBox*>(QStringLiteral("vectorShapeWidthSpin"));
  CHECK(options_width != nullptr);

  bool drove_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* width = dialog->findChild<patchy::ui::UnitSpinBox*>(QStringLiteral("shapeGeometryWidthSpin"));
    auto* height = dialog->findChild<patchy::ui::UnitSpinBox*>(QStringLiteral("shapeGeometryHeightSpin"));
    auto* stroke = dialog->findChild<patchy::ui::UnitSpinBox*>(QStringLiteral("shapeStrokeWidthSpin"));
    CHECK(width != nullptr && height != nullptr && stroke != nullptr);
    CHECK(width->display_unit() == patchy::ui::SpinUnit::Pixels);
    height->pick_display_unit(patchy::ui::SpinUnit::Inches);
    QApplication::processEvents();
    CHECK(width->display_unit() == patchy::ui::SpinUnit::Inches);
    CHECK(stroke->display_unit() == patchy::ui::SpinUnit::Inches);
    CHECK(std::abs(width->value() - 200.0) < 1e-6);  // the value stays pixels
    CHECK(width->text() == QStringLiteral("0.667") + patchy::ui::inch_suffix());
    CHECK(patchy::ui::MainWindowTestAccess::ruler_unit(window) == patchy::ui::MeasurementUnit::Inches);
    CHECK(options_width->display_unit() == patchy::ui::SpinUnit::Inches);
    drove_dialog = true;
    dialog->reject();
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();
  CHECK(drove_dialog);
  CHECK(canvas->ruler_unit() == patchy::ui::MeasurementUnit::Inches);
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("view/rulerUnits")).toString() == QStringLiteral("in"));
  }
}

std::vector<patchy::test::TestCase> vector_shape_tool_tests() {
  return {
      {"ui_shape_and_text_drags_start_off_canvas", ui_shape_and_text_drags_start_off_canvas},
      {"ui_shape_size_fields_follow_ruler_unit", ui_shape_size_fields_follow_ruler_unit},
      {"ui_shape_appearance_unit_pick_sets_ruler_unit", ui_shape_appearance_unit_pick_sets_ruler_unit},
      {"ui_shape_tool_creates_shape_layer_and_undoes", ui_shape_tool_creates_shape_layer_and_undoes},
      {"ui_shape_tool_combine_extends_active_shape_layer",
       ui_shape_tool_combine_extends_active_shape_layer},
      {"ui_shape_tool_path_mode_populates_work_path", ui_shape_tool_path_mode_populates_work_path},
      {"ui_palette_swatch_click_recolors_selected_shape", ui_palette_swatch_click_recolors_selected_shape},
      {"ui_eyedropper_pick_recolors_selected_shape", ui_eyedropper_pick_recolors_selected_shape},
      {"ui_foreground_panel_color_recolors_selected_shape", ui_foreground_panel_color_recolors_selected_shape},
      {"ui_shape_tool_pixels_mode_keeps_raster_commit", ui_shape_tool_pixels_mode_keeps_raster_commit},
      {"ui_line_shape_layer_uses_weight_and_stroke_settings",
       ui_line_shape_layer_uses_weight_and_stroke_settings},
      {"ui_pen_tool_click_and_close_creates_shape_layer",
       ui_pen_tool_click_and_close_creates_shape_layer},
      {"ui_pen_tool_path_mode_keys_and_handles", ui_pen_tool_path_mode_keys_and_handles},
      {"ui_direct_select_drags_anchor_with_single_undo",
       ui_direct_select_drags_anchor_with_single_undo},
      {"ui_path_select_drags_whole_shape_group", ui_path_select_drags_whole_shape_group},
      {"ui_pen_adds_deletes_and_converts_anchors", ui_pen_adds_deletes_and_converts_anchors},
      {"ui_pen_hover_shows_context_cursor_badges", ui_pen_hover_shows_context_cursor_badges},
      {"ui_pen_ctrl_drag_direct_selects_committed_anchor_then_draws",
       ui_pen_ctrl_drag_direct_selects_committed_anchor_then_draws},
      {"ui_pen_ctrl_drag_moves_in_progress_session_anchor",
       ui_pen_ctrl_drag_moves_in_progress_session_anchor},
      {"ui_shape_tool_ctrl_acts_as_path_select", ui_shape_tool_ctrl_acts_as_path_select},
      {"ui_shape_tool_ctrl_alt_drag_duplicates_a_filled_shape", ui_shape_tool_ctrl_alt_drag_duplicates_a_filled_shape},
      {"ui_shape_tool_shift_press_adds_to_shape_layer",
       ui_shape_tool_shift_press_adds_to_shape_layer},
      {"ui_pen_session_start_shows_hint_message", ui_pen_session_start_shows_hint_message},
      {"ui_vector_mode_combo_disables_pixels_for_vector_only_tools",
       ui_vector_mode_combo_disables_pixels_for_vector_only_tools},
      {"ui_layer_context_menu_offers_shape_appearance_for_shape_layers",
       ui_layer_context_menu_offers_shape_appearance_for_shape_layers},
      {"ui_path_select_combine_op_edit_applies", ui_path_select_combine_op_edit_applies},
      {"ui_vector_mask_from_current_path_masks_layer", ui_vector_mask_from_current_path_masks_layer},
      {"ui_vector_mask_shift_click_disable_and_rasterize",
       ui_vector_mask_shift_click_disable_and_rasterize},
      {"ui_paths_panel_lists_saves_and_targets_paths", ui_paths_panel_lists_saves_and_targets_paths},
      {"ui_paths_panel_fill_stroke_and_make_selection",
       ui_paths_panel_fill_stroke_and_make_selection},
      {"ui_free_transform_scales_shape_layer_crisply",
       ui_free_transform_scales_shape_layer_crisply},
      {"ui_shape_moved_off_canvas_by_free_transform_moves_back",
       ui_shape_moved_off_canvas_by_free_transform_moves_back},
      {"ui_free_transform_shape_with_outside_stroke_lands_on_box",
       ui_free_transform_shape_with_outside_stroke_lands_on_box},
      {"ui_polygon_tool_creates_polygons_and_stars", ui_polygon_tool_creates_polygons_and_stars},
      {"ui_custom_shape_stamps_and_defines", ui_custom_shape_stamps_and_defines},
      {"ui_custom_shape_shift_and_link_keep_proportions", ui_custom_shape_shift_and_link_keep_proportions},
      {"ui_custom_shape_builtin_geometry_refreshes", ui_custom_shape_builtin_geometry_refreshes},
      {"ui_line_arrowheads_extend_the_shape", ui_line_arrowheads_extend_the_shape},
      {"ui_shape_appearance_dialog_commits_and_cancels",
       ui_shape_appearance_dialog_commits_and_cancels},
      {"ui_shape_appearance_unit_fields_share_spin_box_border",
       ui_shape_appearance_unit_fields_share_spin_box_border},
      {"ui_new_solid_fill_layer_uses_selection_mask", ui_new_solid_fill_layer_uses_selection_mask},
      {"ui_new_fill_layer_clips_to_targeted_path", ui_new_fill_layer_clips_to_targeted_path},
      {"ui_shape_pattern_fill_uses_custom_library_pattern",
       ui_shape_pattern_fill_uses_custom_library_pattern},
      {"ui_shape_pattern_dropdowns_show_embedded_thumbnails",
       ui_shape_pattern_dropdowns_show_embedded_thumbnails},
      {"ui_shape_pattern_list_popup_fits_the_screen", ui_shape_pattern_list_popup_fits_the_screen},
      {"ui_options_bar_pattern_fill_creates_pattern_shape",
       ui_options_bar_pattern_fill_creates_pattern_shape},
      {"ui_options_bar_edits_selected_shape_appearance",
       ui_options_bar_edits_selected_shape_appearance},
      {"ui_shape_options_group_stroke_controls_and_end_with_appearance",
       ui_shape_options_group_stroke_controls_and_end_with_appearance},
      {"ui_shape_stroke_width_zero_means_no_stroke", ui_shape_stroke_width_zero_means_no_stroke},
      {"ui_shape_swatches_outline_contrasts_with_options_bar",
       ui_shape_swatches_outline_contrasts_with_options_bar},
      {"ui_path_edits_refresh_panel_thumbnails", ui_path_edits_refresh_panel_thumbnails},
      {"ui_paths_panel_clipping_path_toggle", ui_paths_panel_clipping_path_toggle},
      {"ui_shape_mode_drag_previews_fill_appearance",
       ui_shape_mode_drag_previews_fill_appearance},
      {"ui_shape_mode_drag_previews_stroke_inside_the_shape",
       ui_shape_mode_drag_previews_stroke_inside_the_shape},
      {"ui_shape_geometry_edits_live_shape", ui_shape_geometry_edits_live_shape},
      {"ui_new_gradient_fill_layer_spans_canvas", ui_new_gradient_fill_layer_spans_canvas},
      {"ui_paths_panel_actions_follow_row_selection", ui_paths_panel_actions_follow_row_selection},
      {"ui_paths_panel_target_shows_overlay_with_any_tool",
       ui_paths_panel_target_shows_overlay_with_any_tool},
      {"ui_shape_layer_auto_targets_path_row_until_dismissed",
       ui_shape_layer_auto_targets_path_row_until_dismissed},
      {"ui_shape_layer_row_shows_vector_badge", ui_shape_layer_row_shows_vector_badge},
      {"ui_paths_panel_ctrl_click_loads_selection", ui_paths_panel_ctrl_click_loads_selection},
      {"ui_paths_panel_duplicate_and_reorder", ui_paths_panel_duplicate_and_reorder},
      {"ui_path_free_transform_moves_scales_and_undoes",
       ui_path_free_transform_moves_scales_and_undoes},
      {"ui_stroke_path_simulate_pressure_tapers", ui_stroke_path_simulate_pressure_tapers},
      {"ui_fill_path_supports_patterns", ui_fill_path_supports_patterns},
      {"ui_make_work_path_from_selection_traces_selection",
       ui_make_work_path_from_selection_traces_selection},
      {"ui_new_fill_cancel_preserves_document_and_history", ui_new_fill_cancel_preserves_document_and_history},
      {"ui_path_transform_is_cancelled_on_layer_target_change", ui_path_transform_is_cancelled_on_layer_target_change},
      {"ui_shape_tap_opens_create_dialog_and_places_rectangle",
       ui_shape_tap_opens_create_dialog_and_places_rectangle},
      {"ui_shape_tiny_drag_commits_without_create_dialog",
       ui_shape_tiny_drag_commits_without_create_dialog},
      {"ui_shape_tap_creates_ellipse_polygon_and_custom_shape",
       ui_shape_tap_creates_ellipse_polygon_and_custom_shape},
      {"ui_shape_tap_line_fixed_size_and_path_mode", ui_shape_tap_line_fixed_size_and_path_mode},
      {"ui_shape_create_dialog_captures", ui_shape_create_dialog_captures},
      {"ui_shape_create_dialog_layout", ui_shape_create_dialog_layout},
      {"ui_shape_create_rectangle_radius_link", ui_shape_create_rectangle_radius_link},
      {"ui_shape_size_spins_reflect_and_resize_active_shape",
       ui_shape_size_spins_reflect_and_resize_active_shape},
      {"ui_shape_size_controls_follow_move_and_properties_selection",
       ui_shape_size_controls_follow_move_and_properties_selection},
      {"ui_shape_size_row_shows_in_shape_mode_only", ui_shape_size_row_shows_in_shape_mode_only},
      {"ui_shape_context_menu_offers_shape_commands", ui_shape_context_menu_offers_shape_commands},
      {"ui_shape_style_row_is_pixel_only_and_greys_size_at_normal",
       ui_shape_style_row_is_pixel_only_and_greys_size_at_normal},
      {"ui_options_bar_never_shows_pixel_widgets_in_shape_mode",
       ui_options_bar_never_shows_pixel_widgets_in_shape_mode},
      {"ui_path_overlay_follows_move_drag", ui_path_overlay_follows_move_drag},
      {"ui_shape_layer_outline_shows_only_under_path_and_shape_tools",
       ui_shape_layer_outline_shows_only_under_path_and_shape_tools},
      {"ui_path_overlay_follows_free_transform", ui_path_overlay_follows_free_transform},
      {"ui_layer_dialogs_refuse_during_transform", ui_layer_dialogs_refuse_during_transform},
      {"ui_shape_appearance_dialog_edits_opacity_feather_and_stroke_opacity",
       ui_shape_appearance_dialog_edits_opacity_feather_and_stroke_opacity},
      {"ui_shape_geometry_link_keeps_aspect", ui_shape_geometry_link_keeps_aspect},
      {"ui_shape_geometry_radius_link_edits_all_corners",
       ui_shape_geometry_radius_link_edits_all_corners},
      {"ui_shape_appearance_dialog_fits_1080p_and_has_two_columns",
       ui_shape_appearance_dialog_fits_1080p_and_has_two_columns},
      {"ui_shape_appearance_spins_have_step_buttons", ui_shape_appearance_spins_have_step_buttons},
      {"ui_shape_appearance_reset_restores_factory_defaults",
       ui_shape_appearance_reset_restores_factory_defaults},
      {"ui_options_bar_mode_combo_stays_first_for_shape_tools",
       ui_options_bar_mode_combo_stays_first_for_shape_tools},
      {"ui_shape_appearance_entry_points_open_the_dialog",
       ui_shape_appearance_entry_points_open_the_dialog},
      {"ui_properties_edit_appearance_hides_when_layers_are_deselected",
       ui_properties_edit_appearance_hides_when_layers_are_deselected},
      {"ui_properties_shape_controls_are_safe_without_a_shape", ui_properties_shape_controls_are_safe_without_a_shape},
  };
}
