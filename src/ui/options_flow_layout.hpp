#pragma once

// The Options bar's wrapping host (GitHub issue 91). FlowLayout packs the active
// tool's controls left to right and folds them onto more rows when the window is
// narrow; OptionsFlowContainer sizes the bar from every tool's row, not just the
// active one, so switching tools never changes the bar height (which would shift
// the tool palette, canvas and docks) and a tool's duplicate inline sliders drop out
// before its row would wrap.

#include <QFrame>
#include <QLayout>
#include <QList>
#include <QMargins>
#include <QSet>
#include <QWidget>

#include <functional>
#include <vector>

class QResizeEvent;

namespace patchy::ui {

// Set to true on an Options-bar widget that duplicates a control beside it (the
// inline sliders next to a spin box that already has a popup slider and a scrub
// label). The bar hides them for the active tool at widths where that tool's own row
// would otherwise wrap (other tools' rows do not count: GitHub issue 458 follow-up);
// MainWindow::refresh_options_bar applies it.
inline constexpr char kOptionsBarCollapsibleProperty[] = "optionsBarCollapsible";

// True on an Options-bar control while it holds keyboard focus that came from Tab or
// Backtab; the bar's QSS draws its focus outline only then, so a click shows no mark
// (GitHub issue 440; MainWindow::handle_options_bar_focus_event keeps it).
inline constexpr char kOptionsBarKeyboardFocusProperty[] = "optionsKeyboardFocus";

// An Options-bar separator (object name "optionSeparator"): a QFrame line that
// FlowLayout places so the line sits in the middle of the clear space between the
// controls on either side (FlowLayout::set_ink_insets; GitHub issue 181 follow-up).
class OptionsBarSeparator final : public QFrame {
public:
  explicit OptionsBarSeparator(QWidget* parent);
  // The line's column inside the box (QFrame draws a VLine at half its width).
  [[nodiscard]] static int line_offset(int box_width) { return box_width / 2; }
};

// A left-to-right layout that wraps its items onto additional rows when the
// available width is too small, skipping hidden widgets so the active tool's
// options pack tightly.
class FlowLayout final : public QLayout {
public:
  explicit FlowLayout(QWidget* parent, int horizontal_spacing = 6, int vertical_spacing = 4);
  ~FlowLayout() override;

  void addItem(QLayoutItem* item) override;
  int count() const override;
  QLayoutItem* itemAt(int index) const override;
  QLayoutItem* takeAt(int index) override;
  Qt::Orientations expandingDirections() const override;
  bool hasHeightForWidth() const override;
  int heightForWidth(int width) const override;
  void setGeometry(const QRect& rect) override;
  QSize sizeHint() const override;
  QSize minimumSize() const override;
  void invalidate() override;

  // Centers each control on its row's middle, so a 24 px label sits on a 26 px
  // field's line, and splits evenly only the height beyond what any tool's rows need
  // (the Options bar; GitHub issue 409). Rows start at the top margin: a short tool in
  // a bar that kept a second row's height stays on line one. Off: controls start at
  // the top of their row.
  void set_center_rows(bool center) { center_rows_ = center; }

  // How far a control's visible ink (text, glyph, frame) sits inside its box on the left
  // and right: a label's padding, a checkbox's glyph inset, an icon button's transparent
  // frame. Each OptionsBarSeparator then moves off its slot so its line sits halfway
  // between the ink on either side, whatever its neighbours are; nothing else moves and
  // the row keeps its width (GitHub issue 181 follow-up). Unset: the boxes' edges.
  void set_ink_insets(std::function<QMargins(const QWidget&)> insets) { ink_insets_ = std::move(insets); }

  // Every tool's row: the widgets each tool shows, in no particular order (the
  // layout keeps its own item order). Hidden widgets are measured too.
  void set_rows(std::vector<QSet<QWidget*>> rows);
  [[nodiscard]] bool has_rows() const { return !rows_.empty(); }
  // Bumped by every set_rows() that changes the rows, so a row index can be told
  // apart from the same index in an older set.
  [[nodiscard]] int rows_generation() const { return rows_generation_; }
  // The active tool's index into the rows (-1: none, nothing collapses).
  void set_active_row(int index) { active_row_ = index; }
  [[nodiscard]] int active_row() const { return active_row_; }

  struct RowsMeasure {
    // Per row: hide its kOptionsBarCollapsibleProperty widgets at this width, because
    // that row would wrap with them.
    std::vector<bool> collapse;
    int height = 0;  // the tallest row once each row's own collapsed widgets are dropped
  };
  // Cached per width until the next invalidate() or set_rows().
  [[nodiscard]] RowsMeasure measure_rows(int width) const;
  // Whether the active row drops its duplicate sliders at this width.
  [[nodiscard]] bool active_row_collapses(int width) const;

private:
  struct Flow {
    int height = 0;
    int lines = 0;
  };
  // include == nullptr lays out the widgets that are not hidden (the live row).
  Flow do_layout(const QRect& rect, bool test_only, const std::function<bool(QWidget*)>* include) const;

  QList<QLayoutItem*> items_;
  int horizontal_spacing_;
  int vertical_spacing_;
  bool center_rows_ = false;
  std::function<QMargins(const QWidget&)> ink_insets_;
  std::vector<QSet<QWidget*>> rows_;
  int rows_generation_ = 0;
  int active_row_ = -1;
  mutable int cached_width_ = -1;
  mutable RowsMeasure cached_measure_;
};

// Hosts the Options bar controls in a FlowLayout and reports the height for its
// current width so the surrounding QToolBar grows to a second row. The height
// is the current tool's wrapped height, kept sticky: once a tool has used a
// second row at this width the bar keeps it across tool switches (nothing moves
// when switching back), and a width change resets the reservation to what the
// current tool needs. Reserving the tallest row of every tool in advance showed
// an empty second row for tools that fit (GitHub issue 91).
class OptionsFlowContainer final : public QWidget {
public:
  using QWidget::QWidget;

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

  // Whether the active tool's duplicate sliders should hide at the current width.
  [[nodiscard]] bool wants_collapse() const;
  // MainWindow reports the collapse state it applied, and gets a callback when a
  // resize or a control's size change crosses the threshold so it can re-run its
  // visibility pass. A change of state on the same row also drops the height
  // reservation: it was learned with the other set of sliders (GitHub issue 364). A
  // tool switch keeps it, so the bar never changes height on one (GitHub issue 91).
  void set_collapse_applied(bool collapsed);
  [[nodiscard]] bool collapse_applied() const { return collapse_applied_; }
  void set_collapse_changed_callback(std::function<void()> callback) { collapse_changed_ = std::move(callback); }
  // While MainWindow::refresh_options_bar swaps control visibility, a layout pass
  // can see both the old and the new controls at once; the reservation must not
  // grow from that transient, so it is frozen until the swap is done.
  void set_updating(bool updating) { updating_ = updating; }

protected:
  bool event(QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  // One 26 px row with the flow's 4 px margins above and below.
  static constexpr int kOneRowHeight = 34;

  [[nodiscard]] bool laid_out() const;
  [[nodiscard]] int height_for(int width) const;

  bool collapse_applied_ = false;
  // The row (index and rows generation) collapse_applied_ was applied to.
  int applied_row_ = -1;
  int applied_rows_generation_ = -1;
  bool updating_ = false;
  std::function<void()> collapse_changed_;
  // The sticky reservation: the tallest height any tool row needed at reserved_width_.
  mutable int reserved_width_ = -1;
  mutable int reserved_height_ = 0;
};

}  // namespace patchy::ui
