#pragma once

// The two-line recent list shared by the start panel's Recent Files box and the Open
// Recent File / Folder menus (docs/ui-conventions.md): name over its dimmed folder,
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

  // Rows the height is computed from instead of count(); 0 follows count(). The Open
  // Recent menus pin this to the unfiltered list so filtering never resizes the menu.
  void set_fixed_rows(int rows);

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

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  bool viewportEvent(QEvent* event) override;

 private:
  // The real row whose remove button contains `position`, or -1.
  [[nodiscard]] int remove_button_row_at(const QPoint& position) const;
  void set_remove_hover_row(int row);

  int visible_rows_;
  int fixed_rows_{0};
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
