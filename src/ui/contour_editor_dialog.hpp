#pragma once

#include "core/layer.hpp"

#include <QComboBox>
#include <QElapsedTimer>
#include <QIcon>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

class QFrame;

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

  // A read-only graph only shows the curve and its points (the Contour Manager's
  // preview): it takes no clicks, keys or focus and marks no point as selected.
  void set_read_only(bool read_only);
  [[nodiscard]] bool read_only() const noexcept { return read_only_; }

  // Where a level pair sits in the widget (tests click and drag through it).
  [[nodiscard]] QPointF position_for_levels(double input, double output) const;

  [[nodiscard]] QSize sizeHint() const override;

  // The framed square of the graph, and the plot inside it: input 0..255 and
  // output 0..255 map onto plot_rect(), which sits kPlotInset pixels inside the
  // frame, so a curve flat at 0 or 255 (where build_style_contour_lut clamps a
  // smooth run that would overshoot) runs visibly along the inside of the frame
  // instead of under it (Seth, November 17, 2026). The curve is clipped to the
  // inside of the frame; point handles are not.
  static constexpr int kPlotInset = 3;
  [[nodiscard]] QRect frame_rect() const;
  [[nodiscard]] QRect plot_rect() const;

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
  bool read_only_{false};
};

// Asks for a contour's name (New Contour, Rename Contour, Save as New Contour):
// one dialog, `layerStyleContourNameDialog`, with the field `contourNameEdit`.
// It has one fixed size, wide enough for its whole title in the window
// manager's title bar (GitHub issue 726 follow-up). Returns the trimmed name;
// nullopt on Cancel or an empty name.
[[nodiscard]] std::optional<QString> request_contour_name(QWidget* parent, const QString& title,
                                                          const QString& initial);

// The contour editor. `changed` hears every edit while the dialog is open (the
// Layer Style dialog previews it on the canvas). Returns the edited contour on
// OK: `initial` itself when the curve is unchanged, otherwise the new points
// under the name "Custom". nullopt on Cancel; the caller puts back what it
// showed before.
//
// With a `library` the dialog has a Save as New Contour button
// (`contourEditorSaveAsNewButton`): it asks for a name (request_contour_name),
// adds the graph's curve to the library and goes on from the saved contour
// (whole levels), which `changed` hears. The library keeps a contour saved
// that way whatever closes the dialog. While the curve is still the last one
// saved, OK returns that saved contour (its name with it) and
// `saved_storage_id`, when given, receives its storage id; it is cleared
// otherwise.
[[nodiscard]] std::optional<StyleContour> request_contour_edit(
    QWidget* parent, const StyleContour& initial, const std::function<void(const StyleContour&)>& changed = {},
    ContourLibrary* library = nullptr, QString* saved_storage_id = nullptr, const QString& title = {});

// The straight Linear curve a new contour starts from (New Contour in the Layer
// Style Library menu and in the Contour Manager; GitHub issue 726 follow-up).
[[nodiscard]] StyleContour straight_line_contour();

// A Layer Style contour picker (GitHub issue 726 follow-up). It stays a QComboBox,
// so callers keep currentData, findData, setCurrentIndex and currentIndexChanged,
// and the closed box shows the current contour's chip and name as before. Its list
// opens as a grid of curve chips (PresetGridWidget) instead: one cell per entry in
// the combo's order, the name in the tooltip, the current one selected; the rows
// after the combo's separator (the saved contours) sit under a "Saved Contours"
// header. A click or Enter picks the cell (setCurrentIndex, then activated), Esc
// closes the grid. Object names: <combo>Popup (the frame) and <combo>Grid.
class ContourPickerCombo final : public QComboBox {
public:
  explicit ContourPickerCombo(QWidget* parent = nullptr);

  void showPopup() override;
  void hidePopup() override;
  [[nodiscard]] QFrame* picker_popup() const { return popup_; }

  // Columns of the grid before it scrolls sideways (it never does: it wraps).
  static constexpr int kGridColumns = 6;
  // A grid cell, and the chip drawn in it (4:3, as in the closed box).
  static constexpr int kGridCellExtent = 56;
  static constexpr QSize kGridIconSize{48, 36};

private:
  QPointer<QFrame> popup_;
  // The click that dismisses the grid lands on the combo again; ignore it briefly.
  QElapsedTimer dismissed_;
};

// "Save Contours": a checklist of the user's saved contours, all ticked to
// start with. Returns the storage ids to write, in list order; empty on Cancel
// or when nothing is ticked.
[[nodiscard]] QStringList request_contours_to_save(QWidget* parent, const ContourLibrary& library);

// "Load Contours": asks for a Photoshop .shc file (the file dialog gets
// `file_dialog_object_name`) and appends its contours to `library`, reporting a
// failure or the reader's warnings in a message box. Returns the storage ids of
// the file's contours (added or already there); empty on Cancel or failure.
QStringList load_contours_from_file(QWidget* parent, ContourLibrary& library, const QString& file_dialog_object_name);

// "Save Contours": the request_contours_to_save checklist, then a save dialog
// (`file_dialog_object_name`), then the .shc file. False on Cancel or failure
// (a failure is reported in a message box).
bool save_contours_to_file(QWidget* parent, const ContourLibrary& library, const QString& file_dialog_object_name);

}  // namespace patchy::ui
