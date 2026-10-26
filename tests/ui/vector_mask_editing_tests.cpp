// Vector mask editing after the mask exists (Seth on #447, October 2026): the
// row thumbnail targets the mask and shows the target border, the path tools
// and the pen edit the mask path with one undo step each, a double-click opens
// Mask Properties (Density, Feather), and the link button between the
// thumbnails decides whether the mask follows Move and Free Transform.
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

#include "core/layer_render_utils.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "test_harness.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/paths_panel.hpp"
#include "render/compositor.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

using namespace patchy::test::ui;

namespace {

using Access = patchy::ui::MainWindowTestAccess;
using Target = patchy::ui::CanvasWidget::LayerEditTarget;

const QColor kRed(200, 30, 30);

void pen_click_at(patchy::ui::CanvasWidget& canvas, QPoint document_point) {
  const auto widget_point = canvas.widget_position_for_document_point(document_point);
  drag(canvas, widget_point, widget_point);
  QApplication::processEvents();
}

void document_drag(patchy::ui::CanvasWidget& canvas, QPoint from, QPoint to) {
  drag(canvas, canvas.widget_position_for_document_point(from), canvas.widget_position_for_document_point(to));
  QApplication::processEvents();
}

QWidget* row_child(QListWidget& layers, const QString& layer_name, const QString& child) {
  auto* item = require_layer_item(layers, layer_name);
  auto* row = layers.itemWidget(item);
  CHECK(row != nullptr);
  return row != nullptr ? row->findChild<QWidget*>(child) : nullptr;
}

bool target_active(QListWidget& layers, const QString& layer_name, const QString& thumbnail) {
  auto* widget = row_child(layers, layer_name, thumbnail);
  return widget != nullptr && widget->property("layerTargetActive").toBool();
}

// The rendered target border: the thumbnail's left column at mid height.
QColor thumbnail_edge_color(QListWidget& layers, const QString& layer_name, const QString& thumbnail) {
  auto* widget = row_child(layers, layer_name, thumbnail);
  CHECK(widget != nullptr);
  if (widget == nullptr) {
    return {};
  }
  const auto image = widget->grab().toImage();
  return image.pixelColor(0, image.height() / 2);
}

struct MaskWindow {
  patchy::ui::MainWindow window;
  patchy::ui::CanvasWidget* canvas{nullptr};
  QListWidget* layers{nullptr};
  patchy::LayerId red{0};
  patchy::LayerId blue{0};

  // `paint_layer`: Seth's #447 setup instead, a light Background under an
  // RGBA "Paint Layer" filled red.
  explicit MaskWindow(bool paint_layer = false, QSize size = QSize(400, 300)) {
    const int width = size.width();
    const int height = size.height();
    patchy::Document base(width, height, patchy::PixelFormat::rgb8());
    blue = base.add_pixel_layer(paint_layer ? "Background" : "Blue",
                                solid_pixels(width, height, patchy::PixelFormat::rgb8(),
                                             paint_layer ? QColor(225, 225, 225) : QColor(30, 30, 200)))
               .id();
    red = base.add_pixel_layer(paint_layer ? "Paint Layer" : "Red",
                               solid_pixels(width, height,
                                            paint_layer ? patchy::PixelFormat::rgba8() : patchy::PixelFormat::rgb8(),
                                            kRed))
              .id();
    base.set_active_layer(red);
    window.add_document_session(std::move(base), QStringLiteral("Vector Mask Editing"));
    show_window(window);
    canvas = require_canvas(window);
    layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(layers != nullptr);
  }

  patchy::Document& document() { return Access::document(window); }
  const patchy::LayerVectorMask* mask(patchy::LayerId id) {
    const auto* layer = std::as_const(document()).find_layer(id);
    return layer != nullptr ? layer->vector_mask() : nullptr;
  }
  void trigger(const char* name) {
    auto* action = window.findChild<QAction*>(QLatin1String(name));
    CHECK(action != nullptr);
    if (action != nullptr) {
      action->trigger();
    }
    QApplication::processEvents();
  }
  // A closed square drawn with the Pen into the targeted mask.
  void pen_square(QPoint top_left, int size) {
    canvas->set_tool(patchy::ui::CanvasTool::Pen);
    pen_click_at(*canvas, top_left);
    pen_click_at(*canvas, top_left + QPoint(size, 0));
    pen_click_at(*canvas, top_left + QPoint(size, size));
    pen_click_at(*canvas, top_left + QPoint(0, size));
    pen_click_at(*canvas, top_left);
  }
};

// Runs `drive` once the named dialog is up. The thumbnail opens Mask Properties from
// a deferred call, so poll for it rather than assume an order between zero timers;
// give up (and fail) after two seconds instead of hanging the suite.
void when_dialog_shown(const QString& object_name, std::function<void(QDialog&)> drive) {
  auto* timer = new QTimer();
  auto attempts = std::make_shared<int>(0);
  QObject::connect(timer, &QTimer::timeout, [timer, attempts, object_name, drive = std::move(drive)] {
    if (auto* dialog = find_top_level_dialog(object_name); dialog != nullptr && dialog->isVisible()) {
      timer->stop();
      timer->deleteLater();
      drive(*dialog);
      return;
    }
    if (++*attempts > 200) {
      timer->stop();
      timer->deleteLater();
      CHECK(false);
      // Whatever opened instead (a modal editor) must not hang the suite.
      for (auto* widget : QApplication::topLevelWidgets()) {
        if (auto* other = qobject_cast<QDialog*>(widget); other != nullptr && other->isVisible()) {
          std::printf("[vector-mask] unexpected dialog %s\n", qPrintable(other->objectName()));
          other->reject();
        }
      }
    }
  });
  timer->start(10);
}

void pump_until(const std::function<bool()>& done) {
  QElapsedTimer clock;
  clock.start();
  while (!done() && clock.elapsed() < 4000) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  CHECK(done());
}

bool has_anchor(const patchy::LayerVectorMask& mask, double x, double y, double tolerance = 1.5) {
  for (const auto& subpath : mask.path.subpaths) {
    for (const auto& anchor : subpath.anchors) {
      if (std::abs(anchor.anchor_x - x) < tolerance && std::abs(anchor.anchor_y - y) < tolerance) {
        return true;
      }
    }
  }
  return false;
}

// The row's vector mask thumbnail shows the target border, the click targets it
// (and never toggles it off again), and every way of adding a mask leaves it
// targeted and bordered.
void ui_vector_mask_thumbnail_click_targets_and_highlights() {
  VectorSettingsGuard settings_guard;
  MaskWindow fixture;
  auto& layers = *fixture.layers;
  const auto red = QStringLiteral("Red");
  const auto vm = QStringLiteral("layerVectorMaskThumbnail");
  const auto content = QStringLiteral("layerContentThumbnail");

  // The content thumbnail's own border is the reference accent.
  const auto accent = thumbnail_edge_color(layers, red, content);
  fixture.trigger("layerVectorMaskRevealAllAction");
  CHECK(fixture.canvas->layer_edit_target() == Target::VectorMask);
  CHECK(target_active(layers, red, vm));
  CHECK(!target_active(layers, red, content));
  CHECK(color_close(thumbnail_edge_color(layers, red, vm), accent, 8));
  CHECK(!color_close(thumbnail_edge_color(layers, red, content), accent, 8));
  // An empty Reveal All mask says how to fill it.
  CHECK(fixture.window.statusBar()->currentMessage().contains(QStringLiteral("Pen")));

  // The first click on the already targeted thumbnail keeps it targeted.
  click_layer_row_thumbnail(layers, red, vm);
  CHECK(fixture.canvas->layer_edit_target() == Target::VectorMask);
  CHECK(color_close(thumbnail_edge_color(layers, red, vm), accent, 8));
  click_layer_row_thumbnail(layers, red, content);
  CHECK(fixture.canvas->layer_edit_target() == Target::Content);
  CHECK(!color_close(thumbnail_edge_color(layers, red, vm), accent, 8));
  CHECK(color_close(thumbnail_edge_color(layers, red, content), accent, 8));
  click_layer_row_thumbnail(layers, red, vm);
  CHECK(fixture.canvas->layer_edit_target() == Target::VectorMask);
  CHECK(color_close(thumbnail_edge_color(layers, red, vm), accent, 8));

  // The Paths panel lists the mask path, selected.
  auto* paths = fixture.window.findChild<patchy::ui::PathsPanel*>();
  CHECK(paths != nullptr);
  if (paths != nullptr) {
    const auto row = paths->selected_row();
    CHECK(row.has_value() && row->kind == patchy::ui::PathsPanel::RowKind::LayerPath &&
          row->name == QStringLiteral("Red Vector Mask"));
  }
  // With the pixels targeted, a click on that row targets the mask again.
  click_layer_row_thumbnail(layers, red, content);
  CHECK(fixture.canvas->layer_edit_target() == Target::Content);
  auto* paths_list = fixture.window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr && paths_list->count() > 0);
  if (paths_list != nullptr && paths_list->count() > 0) {
    Q_EMIT paths_list->itemClicked(paths_list->item(0));
    QApplication::processEvents();
  }
  CHECK(fixture.canvas->layer_edit_target() == Target::VectorMask);
  CHECK(color_close(thumbnail_edge_color(layers, red, vm), accent, 8));

  // Ctrl-click on Add Layer Mask (another layer): targeted and bordered too.
  Access::select_layers(fixture.window, {fixture.blue}, fixture.blue);
  QApplication::processEvents();
  auto* mask_button = fixture.window.findChild<QPushButton*>(QStringLiteral("layerAddMaskButton"));
  CHECK(mask_button != nullptr);
  if (mask_button != nullptr) {
    QTest::mouseClick(mask_button, Qt::LeftButton, Qt::ControlModifier);
    QApplication::processEvents();
  }
  CHECK(fixture.mask(fixture.blue) != nullptr);
  CHECK(fixture.canvas->layer_edit_target() == Target::VectorMask);
  CHECK(color_close(thumbnail_edge_color(layers, QStringLiteral("Blue"), vm), accent, 8));
  CHECK(!color_close(thumbnail_edge_color(layers, red, vm), accent, 8));
}

// With the mask targeted the Pen draws into it and Direct Select edits its
// anchors live; each gesture is one undo step and the row preview follows.
void ui_vector_mask_pen_and_direct_select_edit_with_one_undo() {
  VectorSettingsGuard settings_guard;
  MaskWindow fixture;
  auto& document = fixture.document();
  fixture.trigger("layerVectorMaskRevealAllAction");
  auto* thumbnail = qobject_cast<QLabel*>(row_child(*fixture.layers, QStringLiteral("Red"),
                                                    QStringLiteral("layerVectorMaskThumbnail")));
  CHECK(thumbnail != nullptr);
  const auto preview_before = thumbnail != nullptr ? thumbnail->pixmap().toImage() : QImage();
  const auto history = Access::active_session_undo_depth(fixture.window);

  fixture.pen_square(QPoint(50, 50), 150);
  const auto* mask = fixture.mask(fixture.red);
  CHECK(mask != nullptr && mask->path.subpaths.size() == 1);
  CHECK(document.work_path() == nullptr || document.work_path()->path().empty());
  CHECK(color_close(canvas_pixel(*fixture.canvas, QPoint(100, 100)), kRed, 8));
  CHECK(!color_close(canvas_pixel(*fixture.canvas, QPoint(300, 250)), kRed, 8));
  CHECK(Access::active_session_undo_depth(fixture.window) == history + 1);
  thumbnail = qobject_cast<QLabel*>(row_child(*fixture.layers, QStringLiteral("Red"),
                                              QStringLiteral("layerVectorMaskThumbnail")));
  CHECK(thumbnail != nullptr && thumbnail->pixmap().toImage() != preview_before);

  fixture.trigger("toolDirectSelectAction");
  CHECK(fixture.canvas->tool() == patchy::ui::CanvasTool::DirectSelect);
  document_drag(*fixture.canvas, QPoint(200, 200), QPoint(260, 240));
  mask = fixture.mask(fixture.red);
  CHECK(mask != nullptr && has_anchor(*mask, 260.0, 240.0));
  CHECK(color_close(canvas_pixel(*fixture.canvas, QPoint(230, 205)), kRed, 8));
  CHECK(Access::active_session_undo_depth(fixture.window) == history + 2);
  save_widget_artifact("vector_mask_direct_select_edit", fixture.window);

  Access::undo(fixture.window);
  QApplication::processEvents();
  mask = fixture.mask(fixture.red);
  CHECK(mask != nullptr && has_anchor(*mask, 200.0, 200.0, 0.5) && !has_anchor(*mask, 260.0, 240.0));
  CHECK(!color_close(canvas_pixel(*fixture.canvas, QPoint(230, 205)), kRed, 8));

  // Ctrl+T transforms the mask path, not the layer.
  fixture.canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  fixture.trigger("editFreeTransformAction");
  CHECK(fixture.canvas->path_transform_active());
  CHECK(!fixture.canvas->free_transform_active());
  send_key(*fixture.canvas, Qt::Key_Escape);
  QApplication::processEvents();
}

// A double-click on the vector mask thumbnail opens Mask Properties: Density
// and Feather preview live, OK is one undo step, Cancel restores. The layer
// mask thumbnail opens the same dialog for the raster mask.
void ui_vector_mask_double_click_opens_mask_properties() {
  VectorSettingsGuard settings_guard;
  MaskWindow fixture;
  fixture.trigger("layerVectorMaskRevealAllAction");
  fixture.pen_square(QPoint(50, 50), 150);
  const auto history = Access::active_session_undo_depth(fixture.window);

  const auto double_click = [&fixture](const QString& thumbnail_name) {
    auto* thumbnail = row_child(*fixture.layers, QStringLiteral("Red"), thumbnail_name);
    CHECK(thumbnail != nullptr);
    if (thumbnail != nullptr) {
      send_double_click(*thumbnail, thumbnail->rect().center());
    }
  };
  bool previewed = false;
  bool handled = false;
  when_dialog_shown(QStringLiteral("maskPropertiesDialog"), [&](QDialog& shown) {
    auto* dialog = &shown;
    CHECK(dialog->windowTitle() == QStringLiteral("Vector Mask Properties"));
    auto* density = dialog->findChild<QSpinBox*>(QStringLiteral("maskPropertiesDensitySpin"));
    auto* feather = dialog->findChild<QSpinBox*>(QStringLiteral("maskPropertiesFeatherSpin"));
    CHECK(density != nullptr && feather != nullptr);
    if (density == nullptr || feather == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(density->value() == 100 && feather->value() == 0);
    density->setValue(50);
    feather->setValue(6);
    QApplication::processEvents();
    const auto* live = fixture.mask(fixture.red);
    previewed = live != nullptr && live->density == 128 && std::abs(live->feather - 6.0) < 1e-9;
    // Density 50%: the masked-out corner shows the layer half through.
    const auto corner = canvas_pixel(*fixture.canvas, QPoint(350, 280));
    previewed = previewed && !color_close(corner, kRed, 8) && corner.red() > 80;
    save_widget_artifact("vector_mask_properties_dialog", *dialog);
    handled = true;
    dialog->accept();
  });
  double_click(QStringLiteral("layerVectorMaskThumbnail"));
  pump_until([&] { return handled; });
  QApplication::processEvents();
  CHECK(previewed);
  const auto* mask = fixture.mask(fixture.red);
  CHECK(mask != nullptr && mask->density == 128 && std::abs(mask->feather - 6.0) < 1e-9);
  CHECK(fixture.canvas->layer_edit_target() == Target::VectorMask);
  CHECK(Access::active_session_undo_depth(fixture.window) == history + 1);
  Access::undo(fixture.window);
  QApplication::processEvents();
  mask = fixture.mask(fixture.red);
  CHECK(mask != nullptr && mask->density == 255 && mask->feather == 0.0);

  // Cancel leaves no trace.
  handled = false;
  when_dialog_shown(QStringLiteral("maskPropertiesDialog"), [&](QDialog& shown) {
    auto* dialog = &shown;
    if (auto* density = dialog->findChild<QSpinBox*>(QStringLiteral("maskPropertiesDensitySpin")); density) {
      density->setValue(10);
    }
    QApplication::processEvents();
    handled = true;
    dialog->reject();
  });
  double_click(QStringLiteral("layerVectorMaskThumbnail"));
  pump_until([&] { return handled; });
  QApplication::processEvents();
  mask = fixture.mask(fixture.red);
  CHECK(mask != nullptr && mask->density == 255);
  CHECK(Access::active_session_undo_depth(fixture.window) == history);

  // The raster mask's thumbnail opens Layer Mask Properties.
  fixture.trigger("layerAddMaskAction");
  const auto* layer = std::as_const(fixture.document()).find_layer(fixture.red);
  CHECK(layer != nullptr && layer->mask().has_value());
  handled = false;
  when_dialog_shown(QStringLiteral("maskPropertiesDialog"), [&](QDialog& shown) {
    auto* dialog = &shown;
    CHECK(dialog->windowTitle() == QStringLiteral("Layer Mask Properties"));
    if (auto* feather = dialog->findChild<QSpinBox*>(QStringLiteral("maskPropertiesFeatherSpin")); feather) {
      feather->setValue(3);
    }
    QApplication::processEvents();
    handled = true;
    dialog->accept();
  });
  double_click(QStringLiteral("layerMaskThumbnail"));
  pump_until([&] { return handled; });
  QApplication::processEvents();
  layer = std::as_const(fixture.document()).find_layer(fixture.red);
  CHECK(layer != nullptr && layer->mask().has_value() && std::abs(layer->mask()->feather - 3.0) < 1e-9);
  CHECK(fixture.canvas->layer_edit_target() == Target::Mask);
}

// The chain before the vector mask thumbnail links and unlinks it (one undo
// step each). A linked mask follows Move; an unlinked one stays, and with the
// unlinked mask targeted Move drags only the mask path.
void ui_vector_mask_link_button_controls_move() {
  VectorSettingsGuard settings_guard;
  MaskWindow fixture;
  auto& document = fixture.document();
  fixture.trigger("layerVectorMaskRevealAllAction");
  fixture.pen_square(QPoint(50, 50), 100);
  const auto red_x = [&] { return std::as_const(document).find_layer(fixture.red)->bounds().x; };

  auto* link = qobject_cast<QToolButton*>(
      row_child(*fixture.layers, QStringLiteral("Red"), QStringLiteral("layerVectorMaskLinkButton")));
  CHECK(link != nullptr && link->isChecked());
  // Linked: Move with the pixels targeted carries the mask.
  click_layer_row_thumbnail(*fixture.layers, QStringLiteral("Red"), QStringLiteral("layerContentThumbnail"));
  fixture.canvas->set_tool(patchy::ui::CanvasTool::Move);
  const auto x_before = red_x();
  document_drag(*fixture.canvas, QPoint(100, 100), QPoint(120, 100));
  CHECK(red_x() == x_before + 20);
  CHECK(has_anchor(*fixture.mask(fixture.red), 70.0, 50.0));
  Access::undo(fixture.window);
  QApplication::processEvents();

  const auto history = Access::active_session_undo_depth(fixture.window);
  link = qobject_cast<QToolButton*>(
      row_child(*fixture.layers, QStringLiteral("Red"), QStringLiteral("layerVectorMaskLinkButton")));
  CHECK(link != nullptr);
  if (link != nullptr) {
    QTest::mouseClick(link, Qt::LeftButton);
  }
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(fixture.mask(fixture.red)->unlinked);
  CHECK(Access::active_session_undo_depth(fixture.window) == history + 1);
  link = qobject_cast<QToolButton*>(
      row_child(*fixture.layers, QStringLiteral("Red"), QStringLiteral("layerVectorMaskLinkButton")));
  CHECK(link != nullptr && !link->isChecked());

  // Unlinked, pixels targeted: the layer moves, the mask stays.
  click_layer_row_thumbnail(*fixture.layers, QStringLiteral("Red"), QStringLiteral("layerContentThumbnail"));
  document_drag(*fixture.canvas, QPoint(100, 100), QPoint(120, 100));
  CHECK(red_x() == x_before + 20);
  CHECK(has_anchor(*fixture.mask(fixture.red), 50.0, 50.0));
  Access::undo(fixture.window);
  QApplication::processEvents();

  // Unlinked, mask targeted: only the mask path moves, one undo step.
  click_layer_row_thumbnail(*fixture.layers, QStringLiteral("Red"), QStringLiteral("layerVectorMaskThumbnail"));
  const auto before_mask_move = Access::active_session_undo_depth(fixture.window);
  document_drag(*fixture.canvas, QPoint(100, 100), QPoint(130, 110));
  CHECK(red_x() == x_before);
  CHECK(has_anchor(*fixture.mask(fixture.red), 80.0, 60.0));
  CHECK(Access::active_session_undo_depth(fixture.window) == before_mask_move + 1);
  CHECK(color_close(canvas_pixel(*fixture.canvas, QPoint(170, 150)), kRed, 8));
  Access::undo(fixture.window);
  QApplication::processEvents();
  CHECK(has_anchor(*fixture.mask(fixture.red), 50.0, 50.0));

  // Undo relinks.
  Access::undo(fixture.window);
  QApplication::processEvents();
  CHECK(!fixture.mask(fixture.red)->unlinked);
}

// Drawing into a Hide All mask reveals what is drawn (an inverted empty path would
// hide the shape and show the rest).
void ui_vector_mask_hide_all_reveals_drawn_shapes() {
  VectorSettingsGuard settings_guard;
  MaskWindow fixture;
  fixture.trigger("layerVectorMaskHideAllAction");
  CHECK(!color_close(canvas_pixel(*fixture.canvas, QPoint(100, 100)), kRed, 8));
  fixture.pen_square(QPoint(50, 50), 150);
  CHECK(color_close(canvas_pixel(*fixture.canvas, QPoint(100, 100)), kRed, 8));
  CHECK(!color_close(canvas_pixel(*fixture.canvas, QPoint(300, 250)), kRed, 8));
  CHECK(!fixture.mask(fixture.red)->inverted);
  // A second shape reveals more.
  fixture.pen_square(QPoint(250, 150), 100);
  CHECK(color_close(canvas_pixel(*fixture.canvas, QPoint(300, 200)), kRed, 8));
  CHECK(!color_close(canvas_pixel(*fixture.canvas, QPoint(375, 50)), kRed, 8));
  // One undo per shape: back to the hidden layer.
  Access::undo(fixture.window);
  Access::undo(fixture.window);
  QApplication::processEvents();
  CHECK(fixture.mask(fixture.red)->inverted && fixture.mask(fixture.red)->path.subpaths.empty());
  CHECK(!color_close(canvas_pixel(*fixture.canvas, QPoint(100, 100)), kRed, 8));
}


// #447 follow-up: Drop Shadow, Inner Shadow and Bevel added through the Layer
// Style dialog to a canvas-filled layer whose vector mask cuts a triangle out
// draw from the triangle: in the reference composite, on the canvas zoomed in,
// and at 15% (the default 5 px effects are about one screen pixel there).
// Small page and a 3600x2700 one (past the 8 Mpx line where full refreshes go
// async).
void ui_vector_mask_layer_effects_render_on_canvas() {
  for (const int scale : {1, 9}) {
    VectorSettingsGuard settings_guard;
    MaskWindow fixture(true, QSize(400 * scale, 300 * scale));
    const auto at = [scale](int x, int y) { return QPoint(x * scale, y * scale); };
    fixture.trigger("layerVectorMaskRevealAllAction");
    fixture.canvas->set_tool(patchy::ui::CanvasTool::Pen);
    pen_click_at(*fixture.canvas, at(60, 60));
    pen_click_at(*fixture.canvas, at(300, 80));
    pen_click_at(*fixture.canvas, at(120, 240));
    pen_click_at(*fixture.canvas, at(60, 60));
    CHECK(fixture.mask(fixture.red) != nullptr && fixture.mask(fixture.red)->path.subpaths.size() == 1);
    const auto below_edge = at(210, 160) + QPoint(2, 3);  // just outside the lower right edge
    bool handled = false;
    when_dialog_shown(QStringLiteral("patchyLayerStyleDialog"), [&](QDialog& dialog) {
      for (const auto* name : {"layerStyleDropShadowCategoryCheck", "layerStyleInnerShadowCategoryCheck",
                               "layerStyleBevelEmbossCategoryCheck"}) {
        auto* check = dialog.findChild<QCheckBox*>(QLatin1String(name));
        CHECK(check != nullptr);
        if (check != nullptr) {
          check->setChecked(true);
        }
      }
      QApplication::processEvents();
      handled = true;
      dialog.accept();
    });
    fixture.trigger("layerBlendingOptionsAction");
    pump_until([&] { return handled; });
    const auto settle = [&] {
      QElapsedTimer clock;
      clock.start();
      while (clock.elapsed() < 1500) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
      }
    };
    settle();
    const auto* styled = std::as_const(fixture.document()).find_layer(fixture.red);
    CHECK(styled->layer_style().drop_shadows.size() == 1 && styled->layer_style().inner_shadows.size() == 1 &&
          styled->layer_style().bevels.size() == 1 && !styled->layer_style().vector_mask_hides_effects);
    const auto flattened = patchy::Compositor{}.flatten_rgb8(std::as_const(fixture.document()));
    CHECK(flattened.pixel(below_edge.x(), below_edge.y())[0] < 200);  // the shadow on the 225 Background
    save_widget_artifact(scale == 1 ? "vector_mask_layer_effects" : "vector_mask_layer_effects_large_fit", fixture.window);

    fixture.canvas->zoom_to_document_rect(QRect(below_edge - QPoint(100, 100), QSize(200, 200)));
    settle();
    CHECK(canvas_pixel(*fixture.canvas, below_edge).red() < 200);
    save_widget_artifact(scale == 1 ? "vector_mask_layer_effects_close" : "vector_mask_layer_effects_large_close",
                         fixture.window);

    // Zoomed out, with the pixels targeted and the Move tool (no path outline).
    fixture.canvas->set_tool(patchy::ui::CanvasTool::Move);
    click_layer_row_thumbnail(*fixture.layers, QStringLiteral("Paint Layer"), QStringLiteral("layerContentThumbnail"));
    fixture.canvas->set_zoom_centered(0.15);
    settle();
    CHECK(fixture.canvas->rect().contains(fixture.canvas->widget_position_for_document_point(below_edge)));
    const auto with_effects = fixture.canvas->grab().toImage();
    fixture.document().find_layer(fixture.red)->layer_style().effects_visible = false;
    fixture.canvas->force_refresh();
    const auto without_effects = fixture.canvas->grab().toImage();
    fixture.document().find_layer(fixture.red)->layer_style().effects_visible = true;
    fixture.canvas->force_refresh();
    int changed = 0;
    for (int y = 0; y < with_effects.height(); ++y) {
      for (int x = 0; x < with_effects.width(); ++x) {
        const auto a = with_effects.pixelColor(x, y);
        const auto b = without_effects.pixelColor(x, y);
        changed +=
            std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue()) > 24 ? 1 : 0;
      }
    }
    std::printf("[vector-mask] scale %d: effects change %d screen pixels at 15%%\n", scale, changed);
    CHECK(changed > 0);
  }
}

// #447 follow-up: dragging a vector mask anchor re-renders only the area the
// moved segments sweep, never the whole canvas-sized layer, and a layer with
// expensive effects drafts them (hidden) during the drag and shows them again
// on release. The drag stays one undo step.
void ui_vector_mask_anchor_drag_patches_only_the_moved_area() {
  VectorSettingsGuard settings_guard;
  MaskWindow fixture(true, QSize(1200, 900));
  auto& document = fixture.document();
  fixture.trigger("layerVectorMaskRevealAllAction");
  fixture.pen_square(QPoint(100, 100), 100);
  {
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.distance = 8.0F;
    shadow.size = 8.0F;
    document.find_layer(fixture.red)->layer_style().drop_shadows.push_back(shadow);
    fixture.canvas->document_changed();
  }
  fixture.canvas->set_zoom(0.5);
  fixture.canvas->repaint();
  QApplication::processEvents();
  fixture.trigger("toolDirectSelectAction");
  const auto history = Access::active_session_undo_depth(fixture.window);
  const auto start = fixture.canvas->widget_position_for_document_point(QPoint(200, 200));
  send_mouse(*fixture.canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  bool hidden_during_drag = true;
  // The first move hides the effects (one canvas-wide refresh); count from there.
  patchy::ui::CanvasWidget::RenderCacheDiagnostics before{};
  for (int step = 1; step <= 6; ++step) {
    if (step == 2) {
      before = fixture.canvas->render_cache_diagnostics();
    }
    send_mouse(*fixture.canvas, QEvent::MouseMove, start + QPoint(step * 4, step * 3), Qt::NoButton, Qt::LeftButton);
    QApplication::processEvents();
    fixture.canvas->repaint();
    hidden_during_drag =
        hidden_during_drag && !std::as_const(document).find_layer(fixture.red)->layer_style().effects_visible;
  }
  const auto during = fixture.canvas->render_cache_diagnostics();
  send_mouse(*fixture.canvas, QEvent::MouseButtonRelease, start + QPoint(24, 18), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto* layer = std::as_const(document).find_layer(fixture.red);
  CHECK(has_anchor(*layer->vector_mask(), 248.0, 236.0, 2.5));
  CHECK(hidden_during_drag);
  CHECK(layer->layer_style().effects_visible);
  CHECK(Access::active_session_undo_depth(fixture.window) == history + 1);
  // The per-move patches add up to exactly a fresh full rasterization.
  {
    auto fresh = *layer;
    patchy::update_vector_mask_raster(fresh, patchy::Rect::from_size(1200, 900));
    int differences = 0;
    for (std::int32_t y = 0; y < 400; ++y) {
      for (std::int32_t x = 0; x < 400; ++x) {
        differences += patchy::vector_mask_alpha_at(*layer, x, y) != patchy::vector_mask_alpha_at(fresh, x, y) ? 1 : 0;
      }
    }
    CHECK(differences == 0);
  }
  // No full refresh after the first move; each later move patches a small area.
  const auto moves_pixels = during.dirty_region_pixels - before.dirty_region_pixels;
  std::printf("[vector-mask] drag: full refreshes %d, patched pixels %llu\n",
              during.full_refreshes - before.full_refreshes, static_cast<unsigned long long>(moves_pixels));
  CHECK(during.full_refreshes == before.full_refreshes);
  CHECK(moves_pixels < static_cast<std::uint64_t>(1200 * 900));
  Access::undo(fixture.window);
  QApplication::processEvents();
  layer = std::as_const(document).find_layer(fixture.red);
  CHECK(has_anchor(*layer->vector_mask(), 200.0, 200.0, 0.5));
  CHECK(layer->layer_style().effects_visible);
}

}  // namespace

std::vector<patchy::test::TestCase> vector_mask_editing_tests() {
  return {
      {"ui_vector_mask_thumbnail_click_targets_and_highlights", ui_vector_mask_thumbnail_click_targets_and_highlights},
      {"ui_vector_mask_pen_and_direct_select_edit_with_one_undo", ui_vector_mask_pen_and_direct_select_edit_with_one_undo},
      {"ui_vector_mask_double_click_opens_mask_properties", ui_vector_mask_double_click_opens_mask_properties},
      {"ui_vector_mask_link_button_controls_move", ui_vector_mask_link_button_controls_move},
      {"ui_vector_mask_hide_all_reveals_drawn_shapes", ui_vector_mask_hide_all_reveals_drawn_shapes},
      {"ui_vector_mask_layer_effects_render_on_canvas", ui_vector_mask_layer_effects_render_on_canvas},
      {"ui_vector_mask_anchor_drag_patches_only_the_moved_area", ui_vector_mask_anchor_drag_patches_only_the_moved_area},
  };
}
