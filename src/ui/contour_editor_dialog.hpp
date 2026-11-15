#pragma once

#include "core/layer.hpp"

#include <QIcon>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

namespace patchy::ui {

class ContourLibrary;

// A contour's curve on a small chip, for the contour pickers and lists. Painted
// from theme() roles each time it is drawn, so it follows a scheme change.
[[nodiscard]] QIcon contour_preset_icon(const StyleContour& contour);

// The square curve graph of the contour editor (GitHub issue 726): input
// levels run left to right, output levels bottom to top. A click on empty
// graph adds a point, a drag moves one (off the graph removes it), Delete
// removes the selected one and the arrow keys nudge it. Point rules live in
// core/contour_editing.hpp; the curve is drawn from build_style_contour_lut,
// the table the effects render with.
class ContourGraphWidget final : public QWidget {
public:
  explicit ContourGraphWidget(QWidget* parent = nullptr);

  // Takes any contour and shows its editable form (editable_style_contour).
  void set_contour(const StyleContour& contour);
  [[nodiscard]] const StyleContour& contour() const noexcept { return contour_; }

  [[nodiscard]] int selected_point() const noexcept { return selected_; }
  void set_selected_point(int index);

  // The selected point's fields (Input, Output, Corner) and Delete Point.
  void set_selected_levels(int input, int output);
  void set_selected_corner(bool corner);
  void delete_selected_point();
  [[nodiscard]] bool can_delete_selected_point() const noexcept;

  // Where a level pair sits in the widget (tests click and drag through it).
  [[nodiscard]] QPointF position_for_levels(double input, double output) const;

  [[nodiscard]] QSize sizeHint() const override;

  // The curve changed (any edit, each drag move that changed it).
  std::function<void()> contour_changed;
  // Another point became the selected one, or the selected one's values changed.
  std::function<void()> selection_changed;

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;

private:
  [[nodiscard]] QRect graph_rect() const;
  [[nodiscard]] double input_for_x(double x) const;
  [[nodiscard]] double output_for_y(double y) const;
  [[nodiscard]] int hit_point(QPointF position) const;
  void apply_drag(QPointF position);
  void commit(const StyleContour& before, int selected_before);

  StyleContour contour_;
  std::array<std::uint8_t, 256> lut_{};
  int selected_{0};
  // A drag works from the curve as it was at the press, so a point pulled off
  // the graph comes back when the pointer returns before the release.
  StyleContour drag_origin_;
  int drag_index_{-1};
};

// The contour editor. `changed` hears every edit while the dialog is open (the
// Layer Style dialog previews it on the canvas). Returns the edited contour on
// OK: `initial` itself when the curve is unchanged, otherwise the new points
// under the name "Custom". nullopt on Cancel; the caller puts back what it
// showed before.
[[nodiscard]] std::optional<StyleContour> request_contour_edit(
    QWidget* parent, const StyleContour& initial, const std::function<void(const StyleContour&)>& changed = {});

// "Save Contours": a checklist of the user's saved contours, all ticked to
// start with. Returns the storage ids to write, in list order; empty on Cancel
// or when nothing is ticked.
[[nodiscard]] QStringList request_contours_to_save(QWidget* parent, const ContourLibrary& library);

}  // namespace patchy::ui
