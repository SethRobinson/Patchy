#pragma once

#include <QElapsedTimer>
#include <QPointer>
#include <QString>
#include <QToolButton>

class QFrame;
class QListWidget;
class QPaintEvent;

namespace patchy::ui {

class BrushTipLibrary;

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

signals:
  void tip_selected(const QString& id);
  void import_requested();
  void define_requested();  // "New from Selection": make a tip from the current selection/image
  void manage_requested();

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  void show_popup();
  void rebuild_popup_list(QListWidget* list, const QString& folder_filter) const;
  void update_button_face();

  BrushTipLibrary& library_;
  QString current_tip_id_;
  QString working_name_;
  QPixmap working_preview_;
  QString popup_folder_filter_;  // remembered across popup opens; empty = all folders
  QPointer<QFrame> popup_;
  // Toggle guard: the click that dismisses the Qt::Popup is replayed onto the button (or lands
  // while the popup is still closing), which would instantly reopen it. See show_popup().
  QElapsedTimer popup_clock_;
  qint64 popup_dismissed_ms_{-1};
};

}  // namespace patchy::ui
