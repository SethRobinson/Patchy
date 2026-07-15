// CanvasWidget's tool cursor policy, split out of canvas_widget.cpp:
// update_tool_cursor (the per-tool switch with the cached brush cursors),
// apply_selection_cursor_for_mode and apply_zoom_cursor, and the selection /
// magic-wand cursor builders with their per-mode function-local caches. Pure
// function moves from canvas_widget.cpp; behavior must stay identical.

#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"

#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/smart_filter.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_tools.hpp"
#include "core/quick_select.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/image_document_io.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/tool_cursors.hpp"

#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QInputDevice>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMenu>
#include <QMetaObject>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointingDevice>
#include <QPolygon>
#include <QPolygonF>
#include <QPointer>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QScreen>
#include <QSet>
#include <QTabletEvent>
#include <QTimerEvent>
#include <QTransform>
#include <QWheelEvent>
#include <QRandomGenerator>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <functional>
#include <iostream>
#include <limits>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// The wand glyph is drawn within the top-left 24x24; the pixmap is larger to
// leave room for the lower-right selection-mode badge.
constexpr int kMagicWandCursorSize = 32;
constexpr int kMagicWandCursorHotspotX = 6;
constexpr int kMagicWandCursorHotspotY = 6;
constexpr int kMaxBrushCursorExtent = kMaxBrushSize + 5;

// A thin, antialiased black crosshair with a white halo, matching the default
// cross cursor. Used for every combine mode so toggling a modifier never
// recolours or shifts it (Replace just omits the badge).
QCursor build_selection_tool_cursor(CanvasWidget::SelectionMode mode) {
  constexpr int kSize = 32;
  constexpr double kArm = 9.0;
  QPixmap pixmap(kSize, kSize);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  const QPointF hotspot(10.0, 10.0);
  const auto cross = [&](const QColor& color, double width) {
    painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(hotspot + QPointF(-kArm, 0.0), hotspot + QPointF(kArm, 0.0));
    painter.drawLine(hotspot + QPointF(0.0, -kArm), hotspot + QPointF(0.0, kArm));
  };
  cross(kSelectionCursorHalo, kSelectionCursorHaloWidth);
  cross(kSelectionCursorInk, kSelectionCursorWidth);
  paint_selection_mode_badge(painter, mode, QPointF(23.0, 23.0));
  painter.end();
  return QCursor(pixmap, static_cast<int>(hotspot.x()), static_cast<int>(hotspot.y()));
}

QCursor build_magic_wand_cursor(CanvasWidget::SelectionMode mode) {
  QPixmap pixmap(kMagicWandCursorSize, kMagicWandCursorSize);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  const QPointF hotspot(kMagicWandCursorHotspotX, kMagicWandCursorHotspotY);

  painter.setPen(QPen(QColor(18, 20, 24), 4.2, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(hotspot + QPointF(3.0, 3.0), QPointF(21.0, 21.0));
  painter.setPen(QPen(QColor(245, 248, 252), 2.1, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(hotspot + QPointF(3.0, 3.0), QPointF(21.0, 21.0));

  painter.setPen(QPen(QColor(18, 20, 24), 3.0, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(hotspot + QPointF(-5.0, 0.0), hotspot + QPointF(-1.5, 0.0));
  painter.drawLine(hotspot + QPointF(1.5, 0.0), hotspot + QPointF(5.0, 0.0));
  painter.drawLine(hotspot + QPointF(0.0, -5.0), hotspot + QPointF(0.0, -1.5));
  painter.drawLine(hotspot + QPointF(0.0, 1.5), hotspot + QPointF(0.0, 5.0));
  painter.setPen(QPen(QColor(80, 170, 255), 1.4, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(hotspot + QPointF(-5.0, 0.0), hotspot + QPointF(-1.5, 0.0));
  painter.drawLine(hotspot + QPointF(1.5, 0.0), hotspot + QPointF(5.0, 0.0));
  painter.drawLine(hotspot + QPointF(0.0, -5.0), hotspot + QPointF(0.0, -1.5));
  painter.drawLine(hotspot + QPointF(0.0, 1.5), hotspot + QPointF(0.0, 5.0));

  painter.setPen(QPen(QColor(80, 170, 255), 1.5, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(QPointF(17.0, 4.0), QPointF(17.0, 8.0));
  painter.drawLine(QPointF(15.0, 6.0), QPointF(19.0, 6.0));
  painter.drawLine(QPointF(21.0, 10.0), QPointF(21.0, 13.0));
  painter.drawLine(QPointF(19.5, 11.5), QPointF(22.5, 11.5));
  // Badge goes lower-left, clear of the wand handle and sparkle.
  paint_selection_mode_badge(painter, mode, QPointF(10.0, 25.0));
  painter.end();

  return QCursor(pixmap, kMagicWandCursorHotspotX, kMagicWandCursorHotspotY);
}

// Cursors are cached per combine mode: update_tool_cursor() runs on every mouse
// move, so we build the four variants once instead of re-rasterising each time.
QCursor selection_tool_cursor(CanvasWidget::SelectionMode mode) {
  static const std::array<QCursor, 4> cursors = [] {
    std::array<QCursor, 4> result;
    for (std::size_t i = 0; i < result.size(); ++i) {
      result[i] = build_selection_tool_cursor(static_cast<CanvasWidget::SelectionMode>(i));
    }
    return result;
  }();
  return cursors[static_cast<std::size_t>(mode)];
}

QCursor magic_wand_cursor(CanvasWidget::SelectionMode mode) {
  static const std::array<QCursor, 4> cursors = [] {
    std::array<QCursor, 4> result;
    for (std::size_t i = 0; i < result.size(); ++i) {
      result[i] = build_magic_wand_cursor(static_cast<CanvasWidget::SelectionMode>(i));
    }
    return result;
  }();
  return cursors[static_cast<std::size_t>(mode)];
}

}  // namespace

bool CanvasWidget::apply_selection_cursor_for_mode(SelectionMode mode) {
  switch (tool_) {
    case CanvasTool::MagicWand:
      setCursor(magic_wand_cursor(mode));
      return true;
    case CanvasTool::Marquee:
    case CanvasTool::EllipticalMarquee:
    case CanvasTool::Lasso:
    case CanvasTool::MagneticLasso:
      // Same drawn crosshair in every mode (only the badge differs), so toggling
      // Shift/Alt never makes the crosshair jump or change weight.
      setCursor(selection_tool_cursor(mode));
      return true;
    default:
      return false;
  }
}

void CanvasWidget::apply_zoom_cursor(bool zoom_out) {
  QPixmap pixmap(24, 24);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  // Two passes: a dark stroke first to lay a halo, then the light ink on top so
  // the magnifier (and its +/- badge) stays legible over any canvas colour. The
  // badge is a + for zoom in and a - for zoom out (Alt held).
  const auto draw = [&](const QPen& pen) {
    painter.setPen(pen);
    painter.drawEllipse(QRect(4, 4, 11, 11));
    painter.drawLine(13, 13, 21, 21);
    painter.drawLine(7, 10, 13, 10);
    if (!zoom_out) {
      painter.drawLine(10, 7, 10, 13);
    }
  };
  draw(QPen(QColor(20, 23, 28), 3));
  draw(QPen(QColor(245, 248, 252), 1.6));
  painter.end();
  setCursor(QCursor(pixmap, 10, 10));
}

void CanvasWidget::update_tool_cursor() {
  ZoomTraceScope trace("tool_cursor", zoom_);
  if (spacebar_panning_ || tool_ == CanvasTool::Pan) {
    setCursor(Qt::OpenHandCursor);
    return;
  }
  if (transient_read_callback_) {
    setCursor(transient_read_cursor_);
    return;
  }
  if (tool_ == CanvasTool::Move) {
    setCursor(Qt::SizeAllCursor);
    return;
  }
  // Alt turns a paint/shape/fill tool into a temporary colour picker; show the
  // eyedropper so the mode is obvious. Placed before the brush branches so it
  // also overrides the large-brush overlay crosshair while Alt is held. The
  // standalone Eyedropper tool uses the same cursor unconditionally.
  const bool alt_held = alt_color_pick_cursor_override_.has_value()
                            ? *alt_color_pick_cursor_override_
                            : (QApplication::keyboardModifiers() & Qt::AltModifier) != 0;
  if (tool_ == CanvasTool::Eyedropper ||
      (alt_held && tool_uses_alt_left_for_color_pick(tool_))) {
    setCursor(eyedropper_cursor());
    return;
  }
  if (tool_ == CanvasTool::QuickSelect) {
    if (brush_outline_uses_overlay()) {
      // Too big for an OS cursor: the circle follows the pointer as a canvas overlay.
      setCursor(Qt::CrossCursor);
      if (brush_hover_position_valid_) {
        update(brush_hover_outline_rect().adjusted(-2, -2, 2, 2));
      }
      return;
    }
    auto mode = selection_operation(QApplication::keyboardModifiers());
    if (mode == SelectionMode::Intersect) {
      mode = SelectionMode::Add;  // Quick Select clamps Intersect to Add
    }
    setCursor(quick_select_cursor(mode));
    return;
  }
  // Marquee/elliptical/lasso/wand show a crosshair (or wand) badged with the
  // active combine mode (+ add, - subtract, x intersect) from the toolbar mode
  // and live Shift/Alt.
  if (apply_selection_cursor_for_mode(selection_operation(QApplication::keyboardModifiers()))) {
    return;
  }
  if (tool_ == CanvasTool::Text) {
    setCursor(Qt::IBeamCursor);
    return;
  }
  if (tool_ == CanvasTool::Zoom) {
    apply_zoom_cursor((QApplication::keyboardModifiers() & Qt::AltModifier) != 0);
    return;
  }
  if (brush_outline_uses_overlay()) {
    // Too big for an OS cursor: the outline follows the pointer as a canvas overlay instead.
    setCursor(Qt::CrossCursor);
    if (brush_hover_position_valid_) {
      update(brush_hover_outline_rect().adjusted(-2, -2, 2, 2));
    }
    return;
  }
  if ((tool_ == CanvasTool::Brush || tool_ == CanvasTool::PatternStamp ||
       tool_ == CanvasTool::Eraser) &&
      brush_tip_ != nullptr) {
    if (apply_brush_tip_cursor()) {
      return;
    }
  }
  if (tool_ == CanvasTool::Brush || tool_ == CanvasTool::PatternStamp ||
      tool_ == CanvasTool::Clone || tool_ == CanvasTool::Healing ||
      tool_ == CanvasTool::Smudge || tool_ == CanvasTool::Dodge || tool_ == CanvasTool::Burn ||
      tool_ == CanvasTool::Sponge || tool_ == CanvasTool::BlurBrush ||
      tool_ == CanvasTool::SharpenBrush || tool_ == CanvasTool::Eraser) {
    const auto use_cached_brush_cursor = [&](bool one_pixel, int diameter, int extent) {
      if (brush_cursor_cache_.has_value() && brush_cursor_cache_->tool == tool_ &&
          brush_cursor_cache_->brush_size == brush_size_ &&
          brush_cursor_cache_->brush_softness == brush_softness_ &&
          brush_cursor_cache_->diameter == diameter && brush_cursor_cache_->extent == extent &&
          brush_cursor_cache_->one_pixel == one_pixel) {
        setCursor(brush_cursor_cache_->cursor);
        return true;
      }
      return false;
    };
    const auto cache_brush_cursor = [&](bool one_pixel, int diameter, int extent, QCursor cursor) {
      brush_cursor_cache_ =
          BrushCursorCache{tool_, brush_size_, brush_softness_, diameter, extent, one_pixel, std::move(cursor)};
      setCursor(brush_cursor_cache_->cursor);
    };

    if (brush_size_ == 1) {
      const auto pixel_extent = std::max(3, static_cast<int>(std::round(zoom_)));
      const auto extent = std::clamp(pixel_extent + 7, 17, kMaxBrushCursorExtent);
      if (use_cached_brush_cursor(true, pixel_extent, extent)) {
        return;
      }
      QPixmap pixmap(extent, extent);
      pixmap.fill(Qt::transparent);
      QPainter painter(&pixmap);
      painter.setRenderHint(QPainter::Antialiasing, false);
      const QPoint center(extent / 2, extent / 2);
      const QRect pixel_rect(center.x() - pixel_extent / 2, center.y() - pixel_extent / 2,
                             pixel_extent, pixel_extent);
      painter.setBrush(Qt::NoBrush);
      painter.setPen(QPen(tool_ == CanvasTool::Eraser ? QColor(255, 255, 255) : QColor(25, 25, 25), 1));
      painter.drawRect(pixel_rect);
      painter.setPen(QPen(tool_ == CanvasTool::Eraser ? QColor(25, 25, 25) : QColor(255, 255, 255), 1));
      painter.drawRect(pixel_rect.adjusted(1, 1, -1, -1));
      painter.drawLine(center + QPoint(-3, 0), center + QPoint(3, 0));
      painter.drawLine(center + QPoint(0, -3), center + QPoint(0, 3));
      painter.end();
      cache_brush_cursor(true, pixel_extent, extent, QCursor(pixmap, center.x(), center.y()));
      return;
    }
    const auto diameter = std::max(3, static_cast<int>(std::round(static_cast<double>(brush_size_) * zoom_)));
    const auto extent = std::clamp(diameter + 5, 17, kMaxBrushCursorExtent);
    if (use_cached_brush_cursor(false, diameter, extent)) {
      return;
    }
    QPixmap pixmap(extent, extent);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPoint center(extent / 2, extent / 2);
    const auto radius = std::max(2, std::min(diameter, extent - 5) / 2);
    painter.setPen(QPen(tool_ == CanvasTool::Eraser ? QColor(255, 255, 255) : QColor(25, 25, 25), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, radius, radius);
    painter.setPen(QPen(tool_ == CanvasTool::Eraser ? QColor(25, 25, 25) : QColor(255, 255, 255), 1));
    painter.drawEllipse(center, std::max(1, radius - 1), std::max(1, radius - 1));
    if (brush_softness_ > 0 && tool_ != CanvasTool::Eraser) {
      const auto edge_width = std::max(1, static_cast<int>(std::round(static_cast<double>(radius) *
                                                                      static_cast<double>(brush_softness_) / 100.0)));
      const auto inner_radius = std::max(1, radius - edge_width);
      QPen softness_pen(QColor(105, 150, 210, 175), 1, Qt::DashLine);
      painter.setPen(softness_pen);
      painter.drawEllipse(center, inner_radius, inner_radius);
    }
    painter.drawLine(center + QPoint(-3, 0), center + QPoint(3, 0));
    painter.drawLine(center + QPoint(0, -3), center + QPoint(0, 3));
    painter.end();
    cache_brush_cursor(false, diameter, extent, QCursor(pixmap, center.x(), center.y()));
    return;
  }
  setCursor(Qt::CrossCursor);
}

}  // namespace patchy::ui
