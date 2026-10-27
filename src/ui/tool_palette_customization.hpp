#pragma once

// Customize Tool Palette (GitHub issues 213, 316 and 199; docs/tool-palette.md, "Customizing
// the palette"): which tools the palette shows, one or two columns, wheel scrolling in a short
// window, which of the palette's bottom items (color swatches, Quick Mask, screen mode) show,
// and which Layers panel footer buttons show. The dialog also shows two Preferences settings,
// the toolbar icon size and when tool groups open, stored under their own keys. The settings keys and the tool and button
// ids stored under them are compatibility contracts. Only what the user hid is stored, never a
// full list: a tool or button the settings do not name shows, so ones added in a later version
// appear on a customized palette, and ids this build does not know are ignored (and kept).

#include "ui/app_settings.hpp"

#include <QColor>
#include <QIcon>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

class QWidget;

namespace patchy::ui {

struct ToolPaletteCustomization {
  // Tool hotkey ids ("tools.hand") left off the palette. Hidden tools keep their shortcuts.
  QStringList hidden_tools;
  bool two_columns{false};
  // The wheel scrolls an overflowing column instead of the > arrow opening more columns.
  bool wheel_scroll{true};
  // Layers panel footer button ids (kLayerFooterButtonIds) left out of the footer.
  QStringList hidden_layer_buttons;
  // The palette's bottom items (kToolPaletteFooterItemIds) left off it.
  QStringList hidden_footer_items;

  bool operator==(const ToolPaletteCustomization&) const = default;
};

// The footer buttons' permanent ids, in footer order.
inline constexpr const char* kLayerFooterButtonIds[] = {"new_layer",  "new_folder", "new_adjustment",
                                                        "layer_styles", "add_mask", "duplicate",
                                                        "animation",  "delete"};

// The ids of the palette's bottom items, top to bottom: the color swatches (with the swap
// and default glyphs), the Quick Mask button and the screen mode button.
inline constexpr const char* kToolPaletteColorsId = "colors";
inline constexpr const char* kToolPaletteQuickMaskId = "quick_mask";
inline constexpr const char* kToolPaletteScreenModeId = "screen_mode";
inline constexpr const char* kToolPaletteFooterItemIds[] = {kToolPaletteColorsId, kToolPaletteQuickMaskId,
                                                            kToolPaletteScreenModeId};

// Sorted, without duplicates or empty ids, so equal choices compare and store equal.
[[nodiscard]] ToolPaletteCustomization normalized_tool_palette_customization(ToolPaletteCustomization value);
[[nodiscard]] ToolPaletteCustomization stored_tool_palette_customization();
void set_stored_tool_palette_customization(const ToolPaletteCustomization& value);

// What the dialog lists: the palette's clusters and slots as they are built, and the
// Layers footer buttons.
struct ToolPaletteCatalogTool {
  QString id;
  QString name;
  QIcon icon;
  QString shortcut;
};

struct ToolPaletteCatalogSlot {
  QString name;
  QString shortcut;
  bool flyout{false};
  // A plain tool's slot has one member, the tool itself.
  std::vector<ToolPaletteCatalogTool> members;
};

struct ToolPaletteCatalogCluster {
  QString name;
  std::vector<ToolPaletteCatalogSlot> entries;
};

struct LayerFooterCatalogButton {
  QString id;
  QString name;
  QIcon icon;
};

struct ToolPaletteCatalog {
  std::vector<ToolPaletteCatalogCluster> clusters;
  std::vector<LayerFooterCatalogButton> layer_buttons;
  QColor foreground{Qt::black};
  QColor background{Qt::white};
  QIcon quick_mask_icon;
  QIcon screen_mode_icon;
};

// What the dialog edits: the customization, and the two Preferences settings it also shows.
struct ToolPaletteDialogChoice {
  ToolPaletteCustomization customization;
  ToolbarIconSize icon_size{ToolbarIconSize::Normal};
  ToolFlyoutOpenMode flyout_open_mode{ToolFlyoutOpenMode::Hold};
};

// Translated names of the Preferences choices, shared by Preferences and the dialog.
[[nodiscard]] QString toolbar_icon_size_label(ToolbarIconSize size);
[[nodiscard]] QString tool_flyout_open_mode_label(ToolFlyoutOpenMode mode);

enum class ToolPaletteCustomizeTab { Tools, LayerButtons };

// The Customize Tool Palette dialog (non-modal, run_non_modal_dialog). Returns the choice on
// OK, nothing on Cancel. Its icon size and tool group combos are named
// toolPaletteIconSizeCombo and toolPaletteFlyoutOpenCombo (item data: the stored token), so
// an open dialog can follow a change made in Preferences.
[[nodiscard]] std::optional<ToolPaletteDialogChoice> request_tool_palette_customization(
    QWidget* parent, const ToolPaletteCatalog& catalog, const ToolPaletteDialogChoice& current,
    ToolPaletteCustomizeTab tab = ToolPaletteCustomizeTab::Tools);

}  // namespace patchy::ui
