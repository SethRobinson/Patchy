#pragma once

#include "ui/brush_tool_window.hpp"

#include <QElapsedTimer>
#include <QPointer>
#include <QString>
#include <QToolButton>
#include <QWidget>

#include <functional>
#include <optional>

class QComboBox;
class QFrame;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPaintEvent;
class QToolButton;

namespace patchy::ui {

class BrushTipLibrary;
class BrushTipPicker;
class BrushToolWindow;

// The tip chooser itself: a Filter field (name or folder, case-insensitive), the folder combo,
// the thumbnail grid and the Import / New from Selection / Manage buttons. The picker's popup
// and the floating Brush Tips window each hold one; the folder choice lives in the picker, so
// both remember the same one, and the filter text is never stored (GitHub issue 506
// follow-up).
class BrushTipBrowser : public QWidget {
  Q_OBJECT

public:
  enum class Host { Popup, Window };
  BrushTipBrowser(BrushTipLibrary& library, BrushTipPicker& picker, Host host, QWidget* parent);

  void rebuild();  // re-reads the library, keeps the filter, selects the current tip
  void select_current();  // marks the picker's current tip without rebuilding the rows
  void focus_filter();
  [[nodiscard]] QLineEdit* filter_edit() const noexcept { return filter_; }
  [[nodiscard]] QListWidget* list() const noexcept { return list_; }

signals:
  void tip_chosen(const QString& id);
  void import_requested();
  void define_requested();
  void manage_requested();
  void keep_open_requested();  // the popup's Keep Open button
  void escape_with_empty_filter();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void choose_first_match();

  BrushTipLibrary& library_;
  BrushTipPicker& picker_;
  QLineEdit* filter_{nullptr};
  QComboBox* folder_combo_{nullptr};
  QListWidget* list_{nullptr};
};

// Options-bar control for choosing the active brush tip: a dropdown-style field showing the
// current tip's thumbnail and name that opens a Photoshop-style thumbnail-grid popup. The first
// entry is always the built-in procedural round brush; the rest is the BrushTipLibrary, with
// footer buttons for importing .abr files and opening the manager dialog. Its width is the same
// for every tip, long names elide and the tooltip carries the full one (GitHub issue 364: the
// width followed the name and a long one wrapped the Brush row).
class BrushTipPicker : public QToolButton {
  Q_OBJECT

public:
  explicit BrushTipPicker(BrushTipLibrary& library, QWidget* parent = nullptr);

  void set_current_tip_id(const QString& id);
  [[nodiscard]] const QString& current_tip_id() const noexcept;
  void refresh();  // re-reads the library (and current tip name) into the button face
  void set_working_preview(const QString& name, const QPixmap& image);
  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

  // The floating Brush Tips window (Window > Brush Tips, or Keep Open in the popup). While it is
  // open a click on the picker raises it instead of opening the popup, so one chooser edits
  // the tip. The owner is the main window; `key_target` is where unclaimed keys go.
  void open_window(std::optional<QPoint> global_top_left = std::nullopt, bool activate = true);
  [[nodiscard]] BrushToolWindow* tool_window() const noexcept { return window_; }
  void set_window_key_target(std::function<QWidget*()> key_target);
  // False while the active tool takes no brush tip (or no document is open): the window shows
  // why and its chooser is disabled.
  void set_tool_supported(bool supported, const QString& reason = {});
  void hide_window_for_shutdown();

  // The folder the choosers show (empty = all), remembered across openings.
  [[nodiscard]] const QString& folder_filter() const noexcept { return popup_folder_filter_; }
  void set_folder_filter(const QString& folder) { popup_folder_filter_ = folder; }
  // List rows for `list`: the procedural tips, then the library, narrowed by folder and text.
  void fill_tip_list(QListWidget* list, const QString& folder_filter, const QString& text_filter) const;

signals:
  void tip_selected(const QString& id);
  void import_requested();
  void define_requested();  // "New from Selection": make a tip from the current selection/image
  void manage_requested();

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  void show_popup();
  void update_button_face();
  void choose_tip(const QString& id);
  void refresh_window();

  BrushTipLibrary& library_;
  QString current_tip_id_;
  QString working_name_;
  QPixmap working_preview_;
  QString popup_folder_filter_;  // remembered across popup opens; empty = all folders
  QPointer<QFrame> popup_;
  QPointer<BrushToolWindow> window_;
  QPointer<BrushTipBrowser> window_browser_;
  std::function<QWidget*()> window_key_target_;
  bool tool_supported_{true};
  QString unsupported_reason_;
  // Toggle guard: the click that dismisses the Qt::Popup is replayed onto the button (or lands
  // while the popup is still closing), which would instantly reopen it. See show_popup().
  QElapsedTimer popup_clock_;
  qint64 popup_dismissed_ms_{-1};
};

}  // namespace patchy::ui
