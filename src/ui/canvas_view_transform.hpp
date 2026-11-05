#pragma once

#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>

namespace patchy::ui {

// The canvas view's mapping between document and widget coordinates
// (docs/view-navigation-rotation.md):
//
//   widget = pan + R(angle) * zoom * document
//
// `pan` is the widget position of the document origin, as before rotation, so a
// change of document size keeps the origin in place. Panning, scroll bars, Fit and
// centering work per axis on the rotated document's bounding box, whose top-left is
// `pan + bounds_offset()` and whose size is `extent()`. At angle 0 the offset is
// exactly zero and every mapping is the plain per-axis `pan + zoom * document`
// arithmetic (and its exact inverse), so an unrotated view stays bit-identical to
// the code that predates rotation.
class CanvasViewTransform {
 public:
  CanvasViewTransform() = default;
  CanvasViewTransform(double zoom, QPointF pan, double angle_degrees, QSizeF document_size) noexcept;

  [[nodiscard]] double zoom() const noexcept { return zoom_; }
  [[nodiscard]] QPointF pan() const noexcept { return pan_; }
  [[nodiscard]] double angle_degrees() const noexcept { return angle_; }
  // True for angle 0 only: the upright view every axis-aligned fast path needs.
  [[nodiscard]] bool axis_aligned() const noexcept { return angle_ == 0.0; }

  [[nodiscard]] QPointF map_to_widget(QPointF document_point) const noexcept;
  [[nodiscard]] QPointF map_to_document(QPointF widget_point) const noexcept;
  // Direction-only mappings (no translation): a document-space delta on screen and back.
  [[nodiscard]] QPointF map_vector_to_widget(QPointF document_vector) const noexcept;
  [[nodiscard]] QPointF map_vector_to_document(QPointF widget_vector) const noexcept;
  [[nodiscard]] QTransform to_widget() const noexcept;
  [[nodiscard]] QTransform to_document() const noexcept;

  // The four corners of a document rect on screen, clockwise from its top-left.
  [[nodiscard]] QPolygonF map_document_rect(const QRectF& document_rect) const;
  // Bounding boxes across the two spaces (the rect itself at angle 0).
  [[nodiscard]] QRectF widget_bounds_of_document_rect(const QRectF& document_rect) const noexcept;
  [[nodiscard]] QRectF document_bounds_of_widget_rect(const QRectF& widget_rect) const noexcept;

  // The zoomed, rotated document's bounding-box size in widget units, and the
  // vector from `pan` to that box's top-left corner ((0, 0) at angle 0).
  [[nodiscard]] QSizeF extent() const noexcept;
  [[nodiscard]] QPointF bounds_offset() const noexcept { return bounds_offset_; }
  // The pan that puts `document_point` at `widget_point` at this zoom and angle.
  [[nodiscard]] QPointF pan_placing(QPointF document_point, QPointF widget_point) const noexcept;

  // An angle in degrees wrapped into (-180, 180]; non-finite input gives 0.
  [[nodiscard]] static double normalized_angle(double degrees) noexcept;

 private:
  double zoom_{1.0};
  QPointF pan_{};
  double angle_{0.0};
  QSizeF document_size_{};
  // cos and sin of the angle; exact 0/1/-1 at right angles.
  double cos_{1.0};
  double sin_{0.0};
  QPointF bounds_offset_{};
};

}  // namespace patchy::ui
