#pragma once

#include "ui/preset_tree_widget.hpp"

#include <QString>
#include <QStringList>

namespace patchy::ui {

class StyleLibrary;

// Folder-tree style browser shared by the Layer Style dialog's Styles page and
// the Style Manager: bold folder rows with counts, entry rows with rendered
// thumbnails, collapse-state preservation across reloads, and a context menu
// whose "Export to .asl..." exports the selection (a folder row exports its
// contents; the default filename comes from the folder). The widget reloads
// itself on StyleLibrary::changed(). A thin adapter over PresetTreeWidget:
// the tree mechanics live there, the style strings/signals/export menu here.
class StyleBrowserWidget : public PresetTreeWidget {
  Q_OBJECT

public:
  explicit StyleBrowserWidget(StyleLibrary* library, QWidget* parent = nullptr);

  void set_icon_extent(int extent);
  // Shows a "No Style" first row (the dialog page's clear-all-effects entry).
  void set_show_no_style_entry(bool show);

  // Storage id of the current row; empty for folder rows and No Style.
  [[nodiscard]] QString current_storage_id() const;
  [[nodiscard]] bool current_is_no_style() const;
  // Selected entries with folder rows expanded to their children, deduped, in
  // tree order.
  [[nodiscard]] QStringList selected_storage_ids() const;

  // The context menu's "Export to .asl..." body: file prompt + export_selection_to.
  void export_selection();
  // The prompt-free half (public so offscreen tests can drive the selection
  // expansion + export without a modal file dialog).
  bool export_selection_to(const QString& path);

signals:
  // A style row was clicked/activated (the Styles page applies on click).
  void style_clicked(const QString& storage_id);
  void style_double_clicked(const QString& storage_id);
  void no_style_clicked();

private:
  void show_context_menu(const QPoint& position);
  [[nodiscard]] QString export_suggested_name() const;

  StyleLibrary* library_{nullptr};
  int icon_extent_{48};
  bool show_no_style_entry_{false};
};

}  // namespace patchy::ui
