#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QDialog;
class QWidget;

namespace patchy::ui {

class PatchyColorPickerPrivate;

class PatchyColorPicker final : public QWidget {
  Q_OBJECT

public:
  explicit PatchyColorPicker(QColor initial, QWidget* parent = nullptr);
  ~PatchyColorPicker() override;

  [[nodiscard]] QColor currentColor() const;

  // Edit > Cut/Copy/Paste route here from MainWindow while keyboard focus is
  // inside this picker (a parallel QShortcut would be ambiguous with the
  // application-context hotkeys — same pattern as the Palette panel routing).
  // Copy puts the current color on the clipboard (color mime + "#RRGGBB" text)
  // and returns it; Paste applies a clipboard color and returns it (nullopt =
  // no color on the clipboard); Cut copies like Copy, except with a custom
  // color slot selected it copies THAT slot and resets it to white
  // (cleared_custom_slot reports which happened).
  QColor copy_color_to_clipboard();
  std::optional<QColor> paste_color_from_clipboard();
  QColor cut_color_to_clipboard(bool& cleared_custom_slot);

public slots:
  void setCurrentColor(QColor color);

signals:
  void currentColorChanged(QColor color);

private:
  std::unique_ptr<PatchyColorPickerPrivate> impl_;
};

// QSettings key shared by the color picker's palette dropdown and the Palette
// panel's preset menu: the last palette choice ("basic", "current", or a built-in
// preset id). The picker re-opens on this choice while the document is not in
// palette (indexed) mode; in palette mode it defaults to the current palette.
inline constexpr const char* kColorPickerPaletteChoiceKey = "palettes/lastPaletteChoice";

// Publishes the active document's palette to every open color picker (and any
// opened later): the picker's palette dropdown shows it as "Current palette",
// defaults to it while palette mode is on, and refreshes it live. MainWindow's
// refresh_palette_panel pushes this on every palette change.
void set_color_picker_document_palette(std::vector<QColor> colors, bool palette_mode_active);

// Installs the write path for editing the "Current palette" from a picker
// (dropping or pasting a color onto a palette cell): MainWindow points this at
// apply_palette_entry_color so the edit is undoable and every view refreshes.
// Unset = the current palette is read-only in pickers.
void set_color_picker_document_palette_editor(std::function<void(int index, QColor color)> editor);

// Applies a color to the open transient color picker (request_patchy_color), if
// any, firing its live color_changed callback — used by the Palette panel so a
// swatch click lands in e.g. the layer-style "Choose color" picker. Returns false
// when no request picker is open.
bool apply_color_to_open_color_picker(QColor color);

// The picker containing this widget (walking up the parent chain), or null —
// MainWindow's Edit > Cut/Copy/Paste handlers use it for focus routing.
[[nodiscard]] PatchyColorPicker* color_picker_ancestor_of(QWidget* widget);

[[nodiscard]] QString color_button_style(QColor color);
[[nodiscard]] QString swatch_button_style(QColor color, bool large = false);
[[nodiscard]] QString inline_text_editor_style(QColor color, int pixel_size);
[[nodiscard]] QDialog* create_patchy_color_panel(QWidget* parent, QColor initial, const QString& title,
                                                    std::function<void(QColor)> color_changed);
[[nodiscard]] std::optional<QColor> request_patchy_color(QWidget* parent, QColor initial, const QString& title,
                                                         std::function<void(QColor)> color_changed = {});

}  // namespace patchy::ui
