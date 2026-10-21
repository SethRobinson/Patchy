#pragma once

// The Artboard bar's Size preset picker (docs/artboards.md): a button that shows the chosen
// preset's name and opens a menu with a search field, Custom, and one submenu per New
// Document template group. Typing in the field swaps the submenus for one flat list of the
// matching presets (the New Document search rule); Up and Down move through the menu from
// the field, Enter picks, Right opens a group. The presets and their ids are the New
// Document ones (artboard_size_presets), so nothing here is persisted on its own.

#include "ui/new_document_dialog.hpp"

#include <QList>
#include <QSize>
#include <QString>
#include <QToolButton>

#include <vector>

class QAction;
class QEvent;
class QLineEdit;
class QMenu;
class QPaintEvent;

namespace patchy::ui {

class ArtboardPresetPicker final : public QToolButton {
  Q_OBJECT

 public:
  explicit ArtboardPresetPicker(QWidget* parent = nullptr);

  // Rebuilds the menu from artboard_size_presets(); call it again after a language change.
  void rebuild();
  // Shows the preset of this size (the first in group order), or Custom.
  void set_current_size(QSize size);
  // The chosen preset's New Document id; empty for Custom.
  [[nodiscard]] QString current_preset_id() const { return current_id_; }
  [[nodiscard]] QMenu* preset_menu() const { return menu_; }
  [[nodiscard]] QLineEdit* search_edit() const { return search_; }
  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

 signals:
  void preset_chosen(QSize size);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

 private:
  void apply_search(const QString& text);
  void choose(QAction* action);
  void show_current();
  [[nodiscard]] QList<QAction*> navigable_actions() const;

  QMenu* menu_{nullptr};
  QLineEdit* search_{nullptr};
  QAction* custom_{nullptr};
  QAction* no_match_{nullptr};
  QList<QAction*> group_actions_;
  QList<QAction*> result_actions_;
  std::vector<ArtboardSizePreset> presets_;
  QString current_id_;
  int preferred_width_{150};
};

}  // namespace patchy::ui
