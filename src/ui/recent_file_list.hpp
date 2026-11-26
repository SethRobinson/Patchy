#pragma once

// The two-line recent list shared by the start panel's Recent Files box and the Open
// Recent File / Folder menus (docs/ui-conventions-menus.md): name over its dimmed folder,
// both elided in the middle, a fixed row height, and a box that scrolls instead of
// growing past its visible row cap.

#include <QListWidget>
#include <QString>
#include <QStringList>
#include <QStyledItemDelegate>

#include <functional>

namespace patchy::ui {

// The full path of a recent row. An empty path marks the "no matches" placeholder row,
// which the delegate, click and context-menu handlers all test for; never give a real
// row an empty path.
inline constexpr int kRecentPathRole = Qt::UserRole + 1;
inline constexpr int kRecentRowHeight = 40;
// kRecentRowHeight grown with the interface text size (two text lines; docs/ui-text-size.md).
[[nodiscard]] int recent_row_height();
inline constexpr int kRecentListChrome = 14;  // the list's own frame and padding

// Grid view cells: a picture box (4:3) over one line of file name. The cells share the row's
// width equally. As many columns as bring a cell closest to kRecentGridTargetCellWidth are
// used, so a wide box gets more columns of about that width instead of a few stretched ones;
// with two or more columns a cell is never narrower than kRecentGridMinCellWidth.
inline constexpr int kRecentGridTargetCellWidth = 200;
inline constexpr int kRecentGridMinCellWidth = 150;
inline constexpr int kRecentGridCellInset = 8;

class RecentThumbnailCache;

// The shared filter rule: every whitespace-separated token of `filter_text`, anywhere in
// the native path, case-insensitive, so a folder name narrows as well as a file name.
[[nodiscard]] QStringList recent_filter_tokens(const QString& filter_text);
[[nodiscard]] bool recent_path_matches(const QString& path, const QStringList& tokens);

// A list as tall as its rows (or `fixed_rows` when set), capped at `visible_rows`;
// longer lists scroll. setFixedHeight cannot express that: the application style sheet
// re-polishes the widget and drops the minimum that call sets. A real sizeHint survives
// the re-polish, and the Maximum policy lets a short window shrink the box.
class RecentFileList final : public QListWidget {
 public:
  RecentFileList(QWidget* parent, int visible_rows);

  // Entries the height is computed from instead of count(); 0 follows count(). The Open
  // Recent menus and the Home Screen pin this to the unfiltered list so filtering never
  // resizes the box (in grid view the entries are counted in rows of cells).
  void set_fixed_rows(int rows);

  // Grid view (the Home Screen's Recent Files grid, GitHub issue 750): the same rows laid
  // out as equal cells, left to right, each a picture from `thumbnails` over the file name;
  // at most `visible_rows` rows of cells are asked for before the box scrolls. The items, their path
  // role, clicks, the context menu and the remove button are those of list view. The list
  // never owns `thumbnails`, which must outlive it or be cleared with another call here.
  void set_grid_view(bool grid, RecentThumbnailCache* thumbnails = nullptr, int visible_rows = 3);
  [[nodiscard]] bool grid_view() const noexcept { return grid_view_; }
  // True while the rows are laid out as cells: grid view with at least one real row. The
  // no-match placeholder keeps list view's full-width row, where its text fits.
  [[nodiscard]] bool shows_cells() const noexcept { return cells_; }
  [[nodiscard]] RecentThumbnailCache* thumbnails() const noexcept { return thumbnails_; }
  [[nodiscard]] QSize grid_cell_size() const;
  // A cell's height at a width: the 4:3 picture, the name line (grown with the interface text
  // size) and the spacing around them.
  [[nodiscard]] static int grid_cell_height(int cell_width);
  // The picture box inside a cell's rectangle (viewport coordinates).
  [[nodiscard]] static QRect grid_picture_rect(const QRect& cell_rect);
  // The widget width at which exactly `columns` cells of the target width fit in a row: what
  // a box holding only a few entries is sized to, so they do not sit in a wide empty frame.
  [[nodiscard]] int width_for_grid_columns(int columns) const;
  // fill_recent_file_list calls this after it replaced the rows.
  void rows_changed();
  // Repaints the cell or row of `path` when it is in view. False when no row has that path.
  bool update_path(const QString& path);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

  // Gives every real row a remove (x) button at its right end while the row is
  // hovered (GitHub issue 157). A click on it calls `handler` with the row's path,
  // queued, instead of opening the row; `tooltip` labels the button.
  void set_remove_handler(std::function<void(const QString&)> handler, const QString& tooltip);
  [[nodiscard]] bool has_remove_button() const noexcept { return static_cast<bool>(remove_handler_); }
  // Whether the cursor is over the remove button of the row at `row_index`.
  [[nodiscard]] bool remove_button_hovered(int row_index) const noexcept { return remove_hover_row_ == row_index; }
  // The remove button inside a row's rectangle (viewport coordinates).
  [[nodiscard]] static QRect remove_button_rect(const QRect& row_rect);
  // The same button inside a grid cell: the cell's top right corner, over the picture.
  [[nodiscard]] static QRect grid_remove_button_rect(const QRect& cell_rect);
  // Whichever of the two this list is showing now.
  [[nodiscard]] QRect remove_button_rect_in(const QRect& item_rect) const;

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  bool viewportEvent(QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  // The real row whose remove button contains `position`, or -1.
  [[nodiscard]] int remove_button_row_at(const QPoint& position) const;
  void set_remove_hover_row(int row);
  // The width a row of cells is laid out in (the viewport, less the vertical bar's room).
  [[nodiscard]] int grid_layout_width() const;
  // Cells across that width: the count that brings a cell closest to the target width.
  [[nodiscard]] int grid_columns() const;
  // Rows of cells the box is as tall as (1 to the visible cap).
  [[nodiscard]] int grid_rows_shown() const;
  void apply_view_layout();

  int visible_rows_;
  int fixed_rows_{0};
  bool grid_view_{false};
  bool cells_{false};
  int visible_grid_rows_{3};
  int grid_height_laid_out_{0};
  RecentThumbnailCache* thumbnails_{nullptr};
  std::function<void(const QString&)> remove_handler_;
  QString remove_tooltip_;
  int remove_hover_row_{-1};
  int remove_pressed_row_{-1};
};

// Two-line recent row: file name over its dimmed directory, with hover and selection
// chrome. The placeholder row (empty kRecentPathRole) draws one dimmed line only.
class RecentFileDelegate final : public QStyledItemDelegate {
 public:
  explicit RecentFileDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

// Refills `list` with the `paths` matching `filter_text`, or with one `placeholder` row
// when paths exist but none match. Returns the number of real rows.
int fill_recent_file_list(QListWidget& list, const QStringList& paths, const QString& filter_text,
                          const QString& placeholder);

}  // namespace patchy::ui
