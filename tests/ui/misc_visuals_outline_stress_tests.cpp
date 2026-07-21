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

void ui_transparency_checkerboard_and_copy_paste_preserve_alpha() {
#ifndef Q_OS_WIN
  // Builds its alpha content through the bundled "White to Transparent" legacy shim,
  // which is Windows-only (see the probe). Portable clipboard-alpha coverage is the
  // ui_copy_* tests; revisit if a cross-platform variant of this scene is wanted.
  std::cout << "[SKIP] uses the Windows-only bundled legacy 8BF shim\n";
  return;
#endif
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  canvas->set_primary_color(Qt::white);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_legacy_plugin_action(window, QStringLiteral("White to Transparent"))->trigger();
  QApplication::processEvents();

  require_layer_item(*layer_list, QStringLiteral("Background"))->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  auto transparent_preview = canvas_pixel(*canvas, QPoint(40, 40));
  CHECK(transparent_preview.alpha() == 255);
  CHECK(transparent_preview.red() >= 170);
  CHECK(transparent_preview.red() <= 245);
  CHECK(std::abs(transparent_preview.red() - transparent_preview.green()) <= 4);
  CHECK(std::abs(transparent_preview.green() - transparent_preview.blue()) <= 4);
  save_widget_artifact("ui_transparency_checkerboard", window);

  require_action(window, "editSelectAllAction")->trigger();
  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();

  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  accept_new_document_dialog(220, 180);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();

  canvas = require_canvas(window);
  layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  require_layer_item(*layer_list, QStringLiteral("Background"))->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  const auto pasted_preview = canvas_pixel(*canvas, QPoint(24, 24));
  CHECK(pasted_preview.red() >= 170);
  CHECK(pasted_preview.red() <= 245);
  CHECK(std::abs(pasted_preview.red() - pasted_preview.green()) <= 4);
  CHECK(std::abs(pasted_preview.green() - pasted_preview.blue()) <= 4);
  save_widget_artifact("ui_transparent_copy_paste", window);
}

void ui_crop_rotate_stroke_merge_and_filter_render_visually() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_snap_enabled(false);

  canvas->set_primary_color(QColor(0, 130, 255));
  canvas->set_tool(patchy::ui::CanvasTool::Brush);
  drag(*canvas, QPoint(72, 72), QPoint(190, 140));
  save_widget_artifact("ui_brush_before_crop", *canvas);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, QPoint(60, 60), QPoint(200, 160));
  require_action(window, "imageCropToSelectionAction")->trigger();
  QApplication::processEvents();
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(info != nullptr);
  CHECK(info->text().contains(QStringLiteral("141 x 101 px")));
  save_widget_artifact("ui_crop_to_selection", window);

  require_action(window, "imageRotateClockwiseAction")->trigger();
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("101 x 141 px")));
  save_widget_artifact("ui_rotate_canvas", window);

  canvas->set_primary_color(QColor(255, 50, 50));
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  // Crop recenters the view, so anchor the stroke selection to document
  // coordinates instead of assuming the pre-crop pan.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(12, 12)),
       canvas->widget_position_for_document_point(QPoint(60, 50)));
  require_action(window, "editStrokeSelectionAction")->trigger();
  QApplication::processEvents();
  save_widget_artifact("ui_stroke_selection", *canvas);

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  const auto layers_before = layer_list->count();
  require_action(window, "layerMergeVisibleAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);

  auto* sepia = find_action_by_text(window, QStringLiteral("Vintage Sepia"));
  CHECK(sepia != nullptr);
  accept_filter_dialog();
  sepia->trigger();
  QApplication::processEvents();
  save_widget_artifact("ui_merge_visible_and_filter", window);
}

void ui_crop_to_selection_centers_cropped_image() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_snap_enabled(false);

  // Select a square near the top-left corner of the default 1024x768 document.
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, QPoint(60, 60), QPoint(140, 140));
  const auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  const auto selection_size = selection->size();

  // Pan hard toward the opposite corner, as when inspecting one part of a big
  // image; the selected area ends up entirely off screen.
  CHECK(canvas->begin_pan_at_global_position(canvas->mapToGlobal(QPoint(400, 300))));
  CHECK(canvas->pan_to_global_position(canvas->mapToGlobal(QPoint(-2600, -2600))));
  CHECK(canvas->end_pan());
  CHECK(!canvas->rect().contains(canvas->widget_position_for_document_point(selection->center())));

  require_action(window, "imageCropToSelectionAction")->trigger();
  QApplication::processEvents();

  // The cropped document must come back centered in the viewport, not parked
  // mostly off screen at the stale pan.
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(QSize(document.width(), document.height()) == selection_size);
  const auto center_widget =
      canvas->widget_position_for_document_point(QPoint(document.width() / 2, document.height() / 2));
  const QPoint viewport_center(canvas->width() / 2, canvas->height() / 2);
  CHECK(std::abs(center_widget.x() - viewport_center.x()) <= 4);
  CHECK(std::abs(center_widget.y() - viewport_center.y()) <= 4);
  save_widget_artifact("ui_crop_to_selection_centered", *canvas);
}

// ---- Marching ants selection outline ----

double outline_loop_perimeter(const patchy::ui::OutlineLoop& loop) {
  double total = 0.0;
  for (qsizetype index = 0; index < loop.points.size(); ++index) {
    const auto& from = loop.points[index];
    const auto& to = loop.points[(index + 1) % loop.points.size()];
    total += std::abs(to.x() - from.x()) + std::abs(to.y() - from.y());
  }
  return total;
}

// Positive for clockwise loops in Qt's y-down coordinates (outer contours),
// negative for counterclockwise ones (holes).
double outline_loop_signed_area(const patchy::ui::OutlineLoop& loop) {
  double sum = 0.0;
  for (qsizetype index = 0; index < loop.points.size(); ++index) {
    const auto& from = loop.points[index];
    const auto& to = loop.points[(index + 1) % loop.points.size()];
    sum += from.x() * to.y() - to.x() * from.y();
  }
  return sum / 2.0;
}

QRegion region_from_predicate(QRect area, const std::function<bool(int, int)>& contains) {
  QVector<QRect> runs;
  for (int y = area.top(); y <= area.bottom(); ++y) {
    int run_start = std::numeric_limits<int>::min();
    for (int x = area.left(); x <= area.right() + 1; ++x) {
      const bool inside = x <= area.right() && contains(x, y);
      if (inside && run_start == std::numeric_limits<int>::min()) {
        run_start = x;
      } else if (!inside && run_start != std::numeric_limits<int>::min()) {
        runs.append(QRect(run_start, y, x - run_start, 1));
        run_start = std::numeric_limits<int>::min();
      }
    }
  }
  QRegion region;
  region.setRects(runs.constData(), runs.size());
  return region;
}

qint64 region_pixel_area(const QRegion& region) {
  qint64 area = 0;
  for (const auto& rect : region) {
    area += static_cast<qint64>(rect.width()) * rect.height();
  }
  return area;
}

// The pre-rewrite renderer derived the outline from these four subtractions;
// their total pixel count equals the boundary length, which the traced loops
// must reproduce exactly.
qint64 edge_subtraction_perimeter(const QRegion& region) {
  return region_pixel_area(region.subtracted(region.translated(1, 0))) +
         region_pixel_area(region.subtracted(region.translated(-1, 0))) +
         region_pixel_area(region.subtracted(region.translated(0, 1))) +
         region_pixel_area(region.subtracted(region.translated(0, -1)));
}

// Every integer lattice point along the loops' axis-aligned segments; these
// are exact document-space positions the ants must pass through.
std::vector<QPoint> outline_lattice_points(const std::vector<patchy::ui::OutlineLoop>& loops) {
  std::vector<QPoint> points;
  for (const auto& loop : loops) {
    for (qsizetype index = 0; index < loop.points.size(); ++index) {
      const auto from = loop.points[index].toPoint();
      const auto to = loop.points[(index + 1) % loop.points.size()].toPoint();
      const QPoint step((to.x() > from.x()) - (to.x() < from.x()), (to.y() > from.y()) - (to.y() < from.y()));
      for (QPoint at = from; at != to; at += step) {
        points.push_back(at);
      }
    }
  }
  return points;
}

void selection_outline_traces_rect_as_single_loop() {
  const auto loops = patchy::ui::trace_selection_outlines(QRegion(QRect(3, 4, 5, 6)));
  CHECK(loops.size() == 1);
  const QPolygonF expected{QPointF(3, 4), QPointF(8, 4), QPointF(8, 10), QPointF(3, 10)};
  CHECK(loops[0].points == expected);
  CHECK(loops[0].bounds == QRect(3, 4, 5, 6));
  CHECK(outline_loop_signed_area(loops[0]) > 0.0);

  // An L-shape goes through the raster tracer (two bands) and must produce the
  // six hand-computed corners, clockwise from the topmost-leftmost.
  const auto l_shape = QRegion(QRect(0, 0, 4, 2)).united(QRect(0, 2, 2, 2));
  CHECK(l_shape.rectCount() == 2);
  const auto l_loops = patchy::ui::trace_selection_outlines(l_shape);
  CHECK(l_loops.size() == 1);
  const QPolygonF l_expected{QPointF(0, 0), QPointF(4, 0), QPointF(4, 2),
                             QPointF(2, 2), QPointF(2, 4), QPointF(0, 4)};
  CHECK(l_loops[0].points == l_expected);
  CHECK(l_loops[0].bounds == QRect(0, 0, 4, 4));
}

void selection_outline_traces_rect_with_hole_as_two_loops() {
  const auto region = QRegion(QRect(0, 0, 5, 5)).subtracted(QRegion(QRect(2, 2, 1, 1)));
  const auto loops = patchy::ui::trace_selection_outlines(region);
  CHECK(loops.size() == 2);
  const QPolygonF outer_expected{QPointF(0, 0), QPointF(5, 0), QPointF(5, 5), QPointF(0, 5)};
  CHECK(loops[0].points == outer_expected);
  CHECK(outline_loop_signed_area(loops[0]) > 0.0);
  const QPolygonF hole_expected{QPointF(2, 2), QPointF(2, 3), QPointF(3, 3), QPointF(3, 2)};
  CHECK(loops[1].points == hole_expected);
  CHECK(outline_loop_signed_area(loops[1]) < 0.0);
  CHECK(loops[1].bounds == QRect(2, 2, 1, 1));
}

void selection_outline_traces_single_pixels() {
  const auto single = patchy::ui::trace_selection_outlines(QRegion(QRect(7, 9, 1, 1)));
  CHECK(single.size() == 1);
  const QPolygonF expected{QPointF(7, 9), QPointF(8, 9), QPointF(8, 10), QPointF(7, 10)};
  CHECK(single[0].points == expected);
  CHECK(outline_loop_perimeter(single[0]) == 4.0);

  // Two far-apart pixels exercise the raster path and loop discovery order.
  const auto pair = patchy::ui::trace_selection_outlines(QRegion(QRect(0, 0, 1, 1)).united(QRect(5, 5, 1, 1)));
  CHECK(pair.size() == 2);
  CHECK(pair[0].points.first() == QPointF(0, 0));
  CHECK(pair[1].points.first() == QPointF(5, 5));
  CHECK(pair[0].points.size() == 4);
  CHECK(pair[1].points.size() == 4);
}

void selection_outline_saddle_pixels_trace_as_two_loops() {
  // Diagonally-touching pixels in both orientations must come out as two
  // separate 4-corner loops that share the saddle corner without merging into
  // one crossing 8-vertex loop.
  const auto backslash = patchy::ui::trace_selection_outlines(QRegion(QRect(0, 0, 1, 1)).united(QRect(1, 1, 1, 1)));
  CHECK(backslash.size() == 2);
  const QPolygonF backslash_first{QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)};
  const QPolygonF backslash_second{QPointF(1, 1), QPointF(2, 1), QPointF(2, 2), QPointF(1, 2)};
  CHECK(backslash[0].points == backslash_first);
  CHECK(backslash[1].points == backslash_second);

  const auto slash = patchy::ui::trace_selection_outlines(QRegion(QRect(1, 0, 1, 1)).united(QRect(0, 1, 1, 1)));
  CHECK(slash.size() == 2);
  const QPolygonF slash_first{QPointF(1, 0), QPointF(2, 0), QPointF(2, 1), QPointF(1, 1)};
  const QPolygonF slash_second{QPointF(0, 1), QPointF(1, 1), QPointF(1, 2), QPointF(0, 2)};
  CHECK(slash[0].points == slash_first);
  CHECK(slash[1].points == slash_second);
}

void selection_outline_perimeter_matches_edge_subtraction() {
  // Checkerboard: every pixel is an isolated 4-corner loop.
  const auto checkerboard = region_from_predicate(QRect(5, 3, 12, 10), [](int x, int y) { return (x + y) % 2 == 0; });
  const auto checker_loops = patchy::ui::trace_selection_outlines(checkerboard);
  CHECK(checker_loops.size() == static_cast<std::size_t>(region_pixel_area(checkerboard)));

  // Seeded pseudo-random blob (with whatever holes/saddles the noise makes):
  // total traced length must equal the edge-subtraction boundary length, and
  // repeat runs must be identical.
  std::uint32_t state = 12345U;
  const auto noise = [&state]() {
    state = state * 1664525U + 1013904223U;
    return (state >> 16U) & 0x7FFFU;
  };
  std::vector<std::uint8_t> cells(40U * 30U, 0U);
  for (auto& cell : cells) {
    cell = noise() % 100U < 55U ? 1U : 0U;
  }
  const auto blob = region_from_predicate(
      QRect(2, 2, 40, 30), [&cells](int x, int y) { return cells[(y - 2) * 40 + (x - 2)] != 0U; });
  CHECK(!blob.isEmpty());
  CHECK(blob.rectCount() > 1);

  for (const auto& region : {checkerboard, blob}) {
    const auto loops = patchy::ui::trace_selection_outlines(region);
    double traced = 0.0;
    for (const auto& loop : loops) {
      CHECK(loop.points.size() >= 4);
      CHECK(loop.points.size() % 2 == 0);  // rectilinear closed loops have even corner counts
      traced += outline_loop_perimeter(loop);
    }
    CHECK(traced == static_cast<double>(edge_subtraction_perimeter(region)));
  }

  const auto first = patchy::ui::trace_selection_outlines(blob);
  const auto second = patchy::ui::trace_selection_outlines(blob);
  CHECK(first.size() == second.size());
  for (std::size_t index = 0; index < first.size(); ++index) {
    CHECK(first[index].points == second[index].points);
    CHECK(first[index].bounds == second[index].bounds);
  }
}

void selection_outline_device_trace_simplifies_at_low_zoom() {
  // A diagonal band whose boundary is a long staircase of unit steps: at 25%
  // zoom the device-resolution trace must resolve it to far fewer corners than
  // the document-space trace has.
  const auto staircase = region_from_predicate(QRect(0, 0, 70, 40), [](int x, int y) { return x >= y && x < y + 30; });
  const auto doc_loops = patchy::ui::trace_selection_outlines(staircase);
  CHECK(doc_loops.size() == 1);

  const QRectF viewport(0.0, 0.0, 4096.0, 4096.0);
  const auto device_loops =
      patchy::ui::trace_device_selection_outlines(staircase, 0.25, QPointF(0.0, 0.0), viewport);
  CHECK(!device_loops.empty());
  qsizetype doc_corners = 0;
  for (const auto& loop : doc_loops) {
    doc_corners += loop.points.size();
  }
  qsizetype device_corners = 0;
  for (const auto& loop : device_loops) {
    device_corners += loop.points.size();
  }
  CHECK(device_corners * 2 < doc_corners);

  const auto paths =
      patchy::ui::build_selection_outline_screen_paths(device_loops, 1.0, QPointF(0.0, 0.0), viewport);
  CHECK(!paths.marching.isEmpty());
  for (const auto& polygon : paths.marching.toSubpathPolygons()) {
    CHECK(polygon.size() >= 4);
    CHECK(polygon.first() == polygon.last());  // loops stay closed
    for (qsizetype index = 1; index < polygon.size(); ++index) {
      const auto delta = polygon[index] - polygon[index - 1];
      CHECK((delta.x() == 0.0) != (delta.y() == 0.0));  // axis-aligned, no zero-length segments
    }
  }

  // Sub-device-pixel holes and islands resolve away below 100% like the
  // artwork does, instead of strobing as hundreds of tiny loops.
  std::uint32_t state = 4242U;
  const auto noise = [&state]() {
    state = state * 1664525U + 1013904223U;
    return (state >> 16U) & 0x7FFFU;
  };
  std::vector<std::uint8_t> cells(50U * 40U, 0U);
  for (auto& cell : cells) {
    cell = noise() % 100U < 60U ? 1U : 0U;
  }
  const auto noisy = region_from_predicate(
      QRect(0, 0, 50, 40), [&cells](int x, int y) { return cells[y * 50 + x] != 0U; });
  const auto noisy_doc_loops = patchy::ui::trace_selection_outlines(noisy);
  const auto noisy_device_loops =
      patchy::ui::trace_device_selection_outlines(noisy, 0.1, QPointF(0.0, 0.0), viewport);
  CHECK(noisy_doc_loops.size() > 20);
  CHECK(!noisy_device_loops.empty());
  CHECK(noisy_device_loops.size() * 4 < noisy_doc_loops.size());

  // A selection that resolves below the coverage threshold on screen still
  // emits at least a 1x1 device rect, routed to the pinpoint path so the finer
  // dash pattern keeps it permanently visible.
  const auto tiny_loops =
      patchy::ui::trace_device_selection_outlines(QRegion(QRect(10, 10, 2, 2)), 0.05, QPointF(0.0, 0.0), viewport);
  CHECK(tiny_loops.size() == 1);
  const auto tiny_paths =
      patchy::ui::build_selection_outline_screen_paths(tiny_loops, 1.0, QPointF(0.0, 0.0), viewport);
  CHECK(tiny_paths.marching.isEmpty());
  CHECK(!tiny_paths.pinpoint.isEmpty());
  CHECK(tiny_paths.pinpoint.boundingRect().width() >= 1.0);
  CHECK(tiny_paths.pinpoint.boundingRect().height() >= 1.0);

  // A single-pixel loop at 100% is a 4 px perimeter: pinpoint. At 400% the
  // same loop is long enough for the standard pattern.
  const auto pixel_loops = patchy::ui::trace_selection_outlines(QRegion(QRect(3, 3, 1, 1)));
  const auto pixel_at_100 =
      patchy::ui::build_selection_outline_screen_paths(pixel_loops, 1.0, QPointF(0.0, 0.0), viewport);
  CHECK(pixel_at_100.marching.isEmpty());
  CHECK(!pixel_at_100.pinpoint.isEmpty());
  const auto pixel_at_400 =
      patchy::ui::build_selection_outline_screen_paths(pixel_loops, 4.0, QPointF(0.0, 0.0), viewport);
  CHECK(!pixel_at_400.marching.isEmpty());
  CHECK(pixel_at_400.pinpoint.isEmpty());

  // Content entirely outside the viewport is culled in both stages.
  const auto culled_device = patchy::ui::trace_device_selection_outlines(staircase, 0.5, QPointF(-500.0, -500.0),
                                                                         QRectF(0.0, 0.0, 100.0, 100.0));
  CHECK(culled_device.empty());
  const auto culled_paths = patchy::ui::build_selection_outline_screen_paths(
      doc_loops, 1.0, QPointF(-500.0, -500.0), QRectF(0.0, 0.0, 100.0, 100.0));
  CHECK(culled_paths.marching.isEmpty());
  CHECK(culled_paths.pinpoint.isEmpty());
}

// The selection a magic wand tends to produce: a ragged seeded blob with a
// hole, isolated single pixels, and a diagonal saddle pair.
QRegion ragged_test_selection() {
  std::uint32_t state = 777U;
  const auto noise = [&state]() {
    state = state * 1664525U + 1013904223U;
    return (state >> 16U) & 0x7FFFU;
  };
  std::vector<std::uint8_t> cells(50U * 40U, 0U);
  for (auto& cell : cells) {
    cell = noise() % 100U < 62U ? 1U : 0U;
  }
  const auto blob = region_from_predicate(
      QRect(20, 20, 50, 40), [&cells](int x, int y) { return cells[(y - 20) * 50 + (x - 20)] != 0U; });
  return blob.subtracted(QRegion(QRect(30, 30, 6, 5)))
      .united(QRect(110, 30, 1, 1))
      .united(QRect(112, 50, 1, 1))
      .united(QRect(105, 60, 1, 1))
      .united(QRect(106, 61, 1, 1));
}

bool frame_has_black_near(const QImage& frame, QPoint center, int radius) {
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      const QPoint at = center + QPoint(dx, dy);
      if (!frame.rect().contains(at)) {
        continue;
      }
      const QColor color(frame.pixel(at));
      if (color.red() < 70 && color.green() < 70 && color.blue() < 70) {
        return true;
      }
    }
  }
  return false;
}

bool frame_has_white_near(const QImage& frame, QPoint center, int radius) {
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      const QPoint at = center + QPoint(dx, dy);
      if (!frame.rect().contains(at)) {
        continue;
      }
      const QColor color(frame.pixel(at));
      if (color.red() > 235 && color.green() > 235 && color.blue() > 235) {
        return true;
      }
    }
  }
  return false;
}

void ui_marching_ants_visible_at_every_offset_and_zoom() {
  patchy::Document document(140, 100, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(140, 100, patchy::PixelFormat::rgba8(), QColor(150, 150, 150)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(640, 480);
  canvas.set_document(&document);
  canvas.show();
  QApplication::processEvents();

  const auto region = ragged_test_selection();
  patchy::ui::CanvasWidget::SelectionSnapshot snapshot;
  snapshot.selection = region;
  snapshot.display_region = region;
  canvas.apply_selection_snapshot(snapshot);

  auto samples = outline_lattice_points(patchy::ui::trace_selection_outlines(region));
  CHECK(samples.size() > 200);
  // Thin to keep the pixel scans fast; keep a deterministic spread.
  const auto stride = std::max<std::size_t>(1, samples.size() / 120);
  std::vector<QPoint> thinned;
  for (std::size_t index = 0; index < samples.size(); index += stride) {
    thinned.push_back(samples[index]);
  }

  // At and above 100% zoom every document-space boundary point must show a
  // dark outline pixel at EVERY dash phase (the old per-edge renderer failed
  // this: short disconnected segments — including the isolated pixels and the
  // saddle pair in this selection — spent whole phases entirely white and
  // blinked on light artwork), and the white dash must sweep every part.
  const std::array<std::pair<double, const char*>, 2> document_space_zooms = {
      std::pair<double, const char*>{1.0, "ui_marching_ants_zoom_100"},
      std::pair<double, const char*>{4.0, "ui_marching_ants_zoom_400"},
  };
  for (const auto& [zoom, artifact_name] : document_space_zooms) {
    canvas.set_zoom(zoom);
    QApplication::processEvents();

    std::vector<QPoint> widget_samples;
    const auto visible = canvas.rect().adjusted(8, 8, -8, -8);
    for (const auto& doc_point : thinned) {
      const auto widget_point = canvas.widget_position_for_document_point(doc_point);
      if (visible.contains(widget_point)) {
        widget_samples.push_back(widget_point);
      }
    }
    CHECK(widget_samples.size() >= 20);

    std::vector<bool> saw_white(widget_samples.size(), false);
    for (int offset = 0; offset < 8; ++offset) {
      canvas.set_selection_dash_offset_for_testing(offset);
      const auto frame = canvas.grab().toImage().convertToFormat(QImage::Format_RGB32);
      for (std::size_t index = 0; index < widget_samples.size(); ++index) {
        CHECK(frame_has_black_near(frame, widget_samples[index], 4));
        if (frame_has_white_near(frame, widget_samples[index], 4)) {
          saw_white[index] = true;
        }
      }
      if (offset == 0) {
        save_widget_artifact(artifact_name, canvas);
      }
    }
    for (const auto seen : saw_white) {
      CHECK(seen);
    }
  }

  // Below 100% the outline is resolved at device resolution like the artwork:
  // the aggregate shape keeps crisp always-visible ants while sub-pixel noise
  // resolves away instead of strobing.
  canvas.set_zoom(0.25);
  QApplication::processEvents();
  const auto bounds_top_left = canvas.widget_position_for_document_point(QPoint(20, 20));
  const auto bounds_bottom_right = canvas.widget_position_for_document_point(QPoint(70, 60));
  const QRect device_bounds(bounds_top_left, bounds_bottom_right);
  const auto island_center = canvas.widget_position_for_document_point(QPoint(110, 30));
  for (int offset = 0; offset < 8; ++offset) {
    canvas.set_selection_dash_offset_for_testing(offset);
    const auto frame = canvas.grab().toImage().convertToFormat(QImage::Format_RGB32);

    // Ants ink exists around the blob...
    int black_pixels = 0;
    int white_pixels = 0;
    const auto probe = device_bounds.adjusted(-3, -3, 3, 3);
    for (int y = probe.top(); y <= probe.bottom(); ++y) {
      for (int x = probe.left(); x <= probe.right(); ++x) {
        const QColor color(frame.pixel(x, y));
        black_pixels += color.red() < 70 && color.green() < 70 && color.blue() < 70 ? 1 : 0;
        white_pixels += color.red() > 235 && color.green() > 235 && color.blue() > 235 ? 1 : 0;
      }
    }
    CHECK(black_pixels > 10);
    // ...and black/white stay balanced. Tiling the interior with sub-pixel
    // loops whose white dash phase covers them whole (the failure mode this
    // rewrite replaces) turns the area solid white: lots of white, no black.
    CHECK(white_pixels <= black_pixels * 2);

    // A lone sub-device-pixel island resolves away entirely, like the artwork.
    CHECK(!frame_has_black_near(frame, island_center, 2));
    CHECK(!frame_has_white_near(frame, island_center, 2));
  }
  save_widget_artifact("ui_marching_ants_zoom_025", canvas);

  // A clean selection at 25%: its device-resolution boundary must be marked
  // dark at every phase, and swept by the white dash, all along.
  patchy::ui::CanvasWidget::SelectionSnapshot clean;
  clean.selection = QRegion(QRect(24, 24, 72, 56));
  clean.display_region = clean.selection;
  canvas.apply_selection_snapshot(clean);
  auto clean_samples = outline_lattice_points(patchy::ui::trace_selection_outlines(clean.selection));
  const auto clean_stride = std::max<std::size_t>(1, clean_samples.size() / 60);
  std::vector<QPoint> clean_widget_samples;
  for (std::size_t index = 0; index < clean_samples.size(); index += clean_stride) {
    clean_widget_samples.push_back(canvas.widget_position_for_document_point(clean_samples[index]));
  }
  CHECK(clean_widget_samples.size() >= 20);
  std::vector<bool> clean_saw_white(clean_widget_samples.size(), false);
  for (int offset = 0; offset < 8; ++offset) {
    canvas.set_selection_dash_offset_for_testing(offset);
    const auto frame = canvas.grab().toImage().convertToFormat(QImage::Format_RGB32);
    for (std::size_t index = 0; index < clean_widget_samples.size(); ++index) {
      CHECK(frame_has_black_near(frame, clean_widget_samples[index], 3));
      if (frame_has_white_near(frame, clean_widget_samples[index], 3)) {
        clean_saw_white[index] = true;
      }
    }
  }
  for (const auto seen : clean_saw_white) {
    CHECK(seen);
  }
}

void ui_marching_ants_deep_zoom_follows_feathered_display_region() {
  patchy::Document document(80, 60, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(150, 150, 150)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(640, 480);
  canvas.set_document(&document);
  canvas.show();
  QApplication::processEvents();

  // Feathered selection: the mask keeps a sub-threshold (alpha 60) ring around
  // a solid core, so the ants must trace the display region (the >= 50%
  // contour), not the outer extent of the selection.
  const QRect core(30, 22, 12, 10);
  const QRect extent = core.adjusted(-3, -3, 3, 3);
  QImage mask(extent.size(), QImage::Format_Grayscale8);
  mask.fill(60);
  for (int y = core.top(); y <= core.bottom(); ++y) {
    auto* row = mask.scanLine(y - extent.top());
    std::fill_n(row + (core.left() - extent.left()), core.width(), std::uint8_t{255});
  }
  patchy::ui::CanvasWidget::SelectionSnapshot snapshot;
  snapshot.selection = QRegion(extent);
  snapshot.display_region = QRegion(core);
  snapshot.mask_bounds = extent;
  snapshot.mask_alpha = mask;
  canvas.apply_selection_snapshot(snapshot);

  canvas.set_zoom(8.0);  // deep-zoom pixel renderer territory
  QApplication::processEvents();

  const auto core_samples = outline_lattice_points(patchy::ui::trace_selection_outlines(QRegion(core)));
  const auto extent_samples = outline_lattice_points(patchy::ui::trace_selection_outlines(QRegion(extent)));
  const auto visible = canvas.rect().adjusted(8, 8, -8, -8);
  for (int offset = 0; offset < 8; ++offset) {
    canvas.set_selection_dash_offset_for_testing(offset);
    const auto frame = canvas.grab().toImage().convertToFormat(QImage::Format_RGB32);

    int core_checked = 0;
    for (const auto& doc_point : core_samples) {
      const auto center = canvas.widget_position_for_document_point(doc_point);
      if (!visible.contains(center)) {
        continue;
      }
      ++core_checked;
      bool has_black = false;
      for (int dy = -4; dy <= 4 && !has_black; ++dy) {
        for (int dx = -4; dx <= 4 && !has_black; ++dx) {
          const QPoint at = center + QPoint(dx, dy);
          if (!frame.rect().contains(at)) {
            continue;
          }
          const QColor color(frame.pixel(at));
          has_black = color.red() < 70 && color.green() < 70 && color.blue() < 70;
        }
      }
      CHECK(has_black);
    }
    CHECK(core_checked >= 8);

    // No ants along the sub-threshold outer extent (24 device px away).
    int extent_checked = 0;
    for (const auto& doc_point : extent_samples) {
      const auto center = canvas.widget_position_for_document_point(doc_point);
      if (!visible.contains(center)) {
        continue;
      }
      ++extent_checked;
      for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
          const QPoint at = center + QPoint(dx, dy);
          if (!frame.rect().contains(at)) {
            continue;
          }
          const QColor color(frame.pixel(at));
          CHECK(!(color.red() < 70 && color.green() < 70 && color.blue() < 70));
        }
      }
    }
    CHECK(extent_checked >= 8);
  }
  save_widget_artifact("ui_marching_ants_deep_zoom", canvas);
}

// End-to-end run of the profiling stress test at the tiny Smoke preset,
// pinning the machinery (scenario completes, reports land and parse, scene
// builds). Offscreen timing numbers are NOT comparable to real-screen runs;
// nothing here asserts durations. All CHECKs sit after run() returns, so no
// failure can unwind past a live inline text editor (see docs/testing.md).
void ui_stress_test_smoke_preset_writes_report() {
  patchy::ui::MainWindow window;
  window.resize(1000, 700);
  window.show();
  QApplication::processEvents();

  QTemporaryDir report_dir;
  CHECK(report_dir.isValid());
  patchy::ui::StressTestOptions options;
  options.preset = patchy::ui::StressPreset::Smoke;
  options.report_dir = report_dir.path();
  const auto report = patchy::ui::MainWindowTestAccess::run_stress_scenario(window, options);

  CHECK(report.success);
  CHECK(!report.steps.empty());
  for (const auto& step : report.steps) {
    CHECK(step.ms >= 0.0);
    CHECK(!step.timed_out);
  }
  // The retro scene stacks layers well past this even at smoke scale.
  CHECK(report.final_layer_count > 15);
  CHECK(report.composite_checksum != 0);

  QFile json_file(QDir(report_dir.path()).filePath(QStringLiteral("stress-latest.json")));
  CHECK(json_file.open(QIODevice::ReadOnly));
  QJsonParseError parse_error{};
  const auto json = QJsonDocument::fromJson(json_file.readAll(), &parse_error);
  CHECK(parse_error.error == QJsonParseError::NoError);
  CHECK(json.isObject());
  const auto steps = json.object().value(QStringLiteral("steps")).toArray();
  CHECK(steps.size() == static_cast<int>(report.steps.size()));
  bool found_move_step = false;
  for (const auto& step_value : steps) {
    const auto step_object = step_value.toObject();
    if (step_object.value(QStringLiteral("category")).toString() == QStringLiteral("move")) {
      found_move_step = true;
      // The outline-preview fallback counter must be reported per move step
      // (engagement itself is not asserted: smoke areas sit under the
      // thresholds by design).
      CHECK(step_object.value(QStringLiteral("diag"))
                .toObject()
                .contains(QStringLiteral("move_outline_previews")));
    }
  }
  CHECK(found_move_step);

  QFile txt_file(QDir(report_dir.path()).filePath(QStringLiteral("stress-latest.txt")));
  CHECK(txt_file.open(QIODevice::ReadOnly));
  CHECK(txt_file.size() > 200);

  // The scene doc is left open and marked saved, so teardown must not prompt.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(!document.layers().empty());
  save_widget_artifact("ui_stress_test_smoke_scene", window);
}

// save_debug_screenshot backs the `--screenshot` CLI flag (src/app/main.cpp): whole window,
// named child widget, sub-rect crop, and the unknown-widget failure path.
void ui_debug_screenshot_saves_window_widget_and_region() {
  patchy::ui::MainWindow window;
  window.add_document_session(patchy::Document(64, 48, patchy::PixelFormat::rgba8()), QStringLiteral("Shot"));
  show_window(window);

  QTemporaryDir dir;
  CHECK(dir.isValid());

  const auto window_path = dir.filePath(QStringLiteral("window.png"));
  CHECK(window.save_debug_screenshot(window_path));
  const QImage full(window_path);
  CHECK(full.size() == window.size() * window.devicePixelRatio());

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  const auto widget_path = dir.filePath(QStringLiteral("layer_list.png"));
  CHECK(window.save_debug_screenshot(widget_path, QStringLiteral("layerList")));
  const QImage widget_image(widget_path);
  CHECK(widget_image.size() == layer_list->size() * layer_list->devicePixelRatio());

  const auto region_path = dir.filePath(QStringLiteral("region.png"));
  CHECK(window.save_debug_screenshot(region_path, QString(), QRect(2, 3, 40, 30)));
  const QImage region_image(region_path);
  CHECK(region_image.size() == QSize(40, 30) * window.devicePixelRatio());

  CHECK(!window.save_debug_screenshot(dir.filePath(QStringLiteral("missing.png")),
                                      QStringLiteral("noSuchWidgetName")));
}

}  // namespace

std::vector<patchy::test::TestCase> misc_visuals_outline_stress_tests() {
  return {
      {"ui_transparency_checkerboard_and_copy_paste_preserve_alpha",
       ui_transparency_checkerboard_and_copy_paste_preserve_alpha},
      {"ui_crop_rotate_stroke_merge_and_filter_render_visually", ui_crop_rotate_stroke_merge_and_filter_render_visually},
      {"ui_crop_to_selection_centers_cropped_image", ui_crop_to_selection_centers_cropped_image},
      {"selection_outline_traces_rect_as_single_loop", selection_outline_traces_rect_as_single_loop},
      {"selection_outline_traces_rect_with_hole_as_two_loops", selection_outline_traces_rect_with_hole_as_two_loops},
      {"selection_outline_traces_single_pixels", selection_outline_traces_single_pixels},
      {"selection_outline_saddle_pixels_trace_as_two_loops", selection_outline_saddle_pixels_trace_as_two_loops},
      {"selection_outline_perimeter_matches_edge_subtraction", selection_outline_perimeter_matches_edge_subtraction},
      {"selection_outline_device_trace_simplifies_at_low_zoom", selection_outline_device_trace_simplifies_at_low_zoom},
      {"ui_marching_ants_visible_at_every_offset_and_zoom", ui_marching_ants_visible_at_every_offset_and_zoom},
      {"ui_marching_ants_deep_zoom_follows_feathered_display_region",
       ui_marching_ants_deep_zoom_follows_feathered_display_region},
      {"ui_stress_test_smoke_preset_writes_report", ui_stress_test_smoke_preset_writes_report},
      {"ui_debug_screenshot_saves_window_widget_and_region", ui_debug_screenshot_saves_window_widget_and_region},
  };
}
