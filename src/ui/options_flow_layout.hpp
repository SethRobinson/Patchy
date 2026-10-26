#pragma once

// The Options bar's wrapping host (GitHub issue 91). FlowLayout packs the active
// tool's controls left to right and folds them onto more rows when the window is
// narrow; OptionsFlowContainer sizes the bar from every tool's row, not just the
// active one, so switching tools never changes the bar height (which would shift
// the tool palette, canvas and docks) and duplicate inline sliders drop out before
// anything wraps.

#include <QLayout>
#include <QList>
#include <QSet>
#include <QWidget>

#include <functional>
#include <vector>

class QResizeEvent;

namespace patchy::ui {

// Set to true on an Options-bar widget that duplicates a control beside it (the
// inline sliders next to a spin box that already has a popup slider and a scrub
// label). The bar hides every such widget at widths where a row that contains one
// would otherwise wrap; MainWindow::refresh_options_bar applies it.
inline constexpr char kOptionsBarCollapsibleProperty[] = "optionsBarCollapsible";

// True on an Options-bar control while it holds keyboard focus that came from Tab or
// Backtab; the bar's QSS draws its focus outline only then, so a click shows no mark
// (GitHub issue 440; MainWindow::handle_options_bar_focus_event keeps it).
inline constexpr char kOptionsBarKeyboardFocusProperty[] = "optionsKeyboardFocus";

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

  // Every tool's row: the widgets each tool shows, in no particular order (the
  // layout keeps its own item order). Hidden widgets are measured too.
  void set_rows(std::vector<QSet<QWidget*>> rows);
  [[nodiscard]] bool has_rows() const { return !rows_.empty(); }

  struct RowsMeasure {
    bool collapse = false;  // hide kOptionsBarCollapsibleProperty widgets at this width
    int height = 0;         // the tallest row once collapsed widgets are dropped
  };
  // Cached per width until the next invalidate() or set_rows().
  [[nodiscard]] RowsMeasure measure_rows(int width) const;

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
  std::vector<QSet<QWidget*>> rows_;
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

  // Whether the duplicate sliders should hide at the current width.
  [[nodiscard]] bool wants_collapse() const;
  // MainWindow reports the collapse state it applied, and gets a callback when a
  // resize or a control's size change crosses the threshold so it can re-run its
  // visibility pass. A change of state also drops the height reservation: it was
  // learned with the other set of sliders (GitHub issue 364).
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
  [[nodiscard]] int height_for(int width) const;

  bool collapse_applied_ = false;
  bool updating_ = false;
  std::function<void()> collapse_changed_;
  // The sticky reservation: the tallest height any tool row needed at reserved_width_.
  mutable int reserved_width_ = -1;
  mutable int reserved_height_ = 0;
};

}  // namespace patchy::ui
