#pragma once

#include "core/palette.hpp"

#include <QWidget>

#include <optional>
#include <vector>

class QComboBox;
class QLabel;
class QToolButton;

namespace patchy::ui {

class PaletteSwatchGrid;

// Dockable palette panel: the document's palette-editing colors as a swatch grid
// plus preset/load/save/extract controls. The panel is a passive view; every
// mutation is emitted as a request and MainWindow owns the document edit, the
// undo snapshot, and the refresh round trip.
class PalettePanel final : public QWidget {
  Q_OBJECT

public:
  explicit PalettePanel(QWidget* parent = nullptr);

  // Replaces the displayed palette. mode_active = the document is in palette
  // (indexed) mode; when false the Convert button is offered instead of the
  // "editing constrained" hint.
  void set_palette(const std::vector<RgbColor>& colors, bool mode_active);
  // Ring-highlights the entry matching this color (foreground / eyedropper pick).
  void set_highlight_color(std::optional<RgbColor> color);
  [[nodiscard]] int selected_index() const noexcept;
  [[nodiscard]] int color_count() const noexcept;
  // The selected swatch color; nullopt when nothing is selected. Used by the
  // Edit > Copy/Paste handlers, which act on the palette while keyboard focus is
  // inside this panel.
  [[nodiscard]] std::optional<RgbColor> selected_color() const;

signals:
  void entry_clicked(int index);
  void entry_edit_requested(int index);
  void entry_swap_requested(int from_index, int to_index);
  // Copy this entry's hex code to the clipboard (the readout's Copy button and
  // the context menu); MainWindow owns the clipboard write and status message.
  void copy_color_requested(int index);
  void add_from_foreground_requested();
  void remove_entry_requested(int index);
  void preset_requested(const QString& preset_id);
  void load_from_file_requested();
  void save_to_file_requested();
  void extract_from_image_requested();
  void convert_requested();

private:
  void show_grid_context_menu(const QPoint& grid_position);
  void update_selection_readout();

  PaletteSwatchGrid* grid_{nullptr};
  QComboBox* preset_combo_{nullptr};
  QLabel* count_label_{nullptr};
  QLabel* empty_hint_{nullptr};
  QToolButton* convert_button_{nullptr};
  QToolButton* remove_button_{nullptr};
  QToolButton* copy_button_{nullptr};
  std::vector<RgbColor> colors_;
  bool has_duplicate_colors_{false};
  bool mode_active_{false};
};

}  // namespace patchy::ui
