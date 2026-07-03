#pragma once

#include <QString>
#include <QToolButton>

class QFrame;
class QListWidget;

namespace patchy::ui {

class BrushTipLibrary;

// Options-bar control for choosing the active brush tip: a button showing the current tip's
// thumbnail and name that opens a Photoshop-style thumbnail-grid popup. The first entry is
// always the built-in procedural round brush; the rest is the BrushTipLibrary, with footer
// buttons for importing .abr files and opening the manager dialog.
class BrushTipPicker : public QToolButton {
  Q_OBJECT

public:
  explicit BrushTipPicker(BrushTipLibrary& library, QWidget* parent = nullptr);

  void set_current_tip_id(const QString& id);
  [[nodiscard]] const QString& current_tip_id() const noexcept;
  void refresh();  // re-reads the library (and current tip name) into the button face

signals:
  void tip_selected(const QString& id);
  void import_requested();
  void define_requested();  // "New from Selection": make a tip from the current selection/image
  void manage_requested();

private:
  void show_popup();
  void rebuild_popup_list(QListWidget* list, const QString& folder_filter) const;
  void update_button_face();

  BrushTipLibrary& library_;
  QString current_tip_id_;
  QString popup_folder_filter_;  // remembered across popup opens; empty = all folders
};

}  // namespace patchy::ui
