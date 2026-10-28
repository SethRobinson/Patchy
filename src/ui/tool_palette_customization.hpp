#pragma once

// Customize Tool Palette (GitHub issues 213, 316 and 199; docs/tool-palette.md, "Customizing
// the palette"): which tools the palette shows, one or two columns, wheel scrolling in a short
// window, which of the palette's bottom items (color swatches, Quick Mask, screen mode) show,
// and which Layers panel footer buttons show. The dialog also shows two Preferences settings,
// the toolbar icon size and when tool groups open, stored under their own keys. The settings keys and the tool and button
// ids stored under them are compatibility contracts. Only what the user hid is stored, never a
// full list: a tool or button the settings do not name shows, so ones added in a later version
// appear on a customized palette, and ids this build does not know are ignored (and kept). The
// optional Layers footer buttons are the one exception: their key stores what was shown. The
// footer's order (GitHub issue 471) is stored only once the user changed it.

#include "ui/app_settings.hpp"

#include <QColor>
#include <QIcon>
#include <QString>
#include <QStringList>

#include <functional>
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
  // Layers panel footer button ids (kLayerFooterButtonIds) left out of the footer. The
  // buttons of kLayerFooterButtonIdsHiddenByDefault start in it (GitHub issue 470).
  QStringList hidden_layer_buttons{QStringLiteral("merge_down"), QStringLiteral("move_down"),
                                   QStringLiteral("move_up")};
  // The palette's bottom items (kToolPaletteFooterItemIds) left off it.
  QStringList hidden_footer_items;
  // The Layers footer buttons in the order the user set (GitHub issue 471), hidden ones
  // included; empty is kLayerFooterButtonIds' order. See resolved_layer_footer_order.
  QStringList layer_button_order;

  bool operator==(const ToolPaletteCustomization&) const = default;
};

// The footer buttons' permanent ids, in footer order.
inline constexpr const char* kLayerFooterButtonIds[] = {"new_layer", "new_folder", "new_adjustment", "layer_styles",
                                                        "add_mask",  "duplicate",  "move_up",        "move_down",
                                                        "merge_down", "animation", "delete"};
// The optional buttons (GitHub issue 470): hidden until the user shows them, so the footer
// keeps its one row. Shown ones are stored under their own key (layersPanel/shownFooterButtons),
// so settings from before them, which name only what was hidden, keep them hidden.
inline constexpr const char* kLayerFooterButtonIdsHiddenByDefault[] = {"move_up", "move_down", "merge_down"};

// The ids of the palette's bottom items, top to bottom: the color swatches (with the swap
// and default glyphs), the Quick Mask button and the screen mode button.
inline constexpr const char* kToolPaletteColorsId = "colors";
inline constexpr const char* kToolPaletteQuickMaskId = "quick_mask";
inline constexpr const char* kToolPaletteScreenModeId = "screen_mode";
inline constexpr const char* kToolPaletteFooterItemIds[] = {kToolPaletteColorsId, kToolPaletteQuickMaskId,
                                                            kToolPaletteScreenModeId};

// Every footer button id once, in the order `stored` names them: ids this build does not know
// are dropped, and a button `stored` leaves out (one added in a later version) goes right after
// the button it follows in kLayerFooterButtonIds, or first when none of those is placed.
[[nodiscard]] QStringList resolved_layer_footer_order(const QStringList& stored);

// The Layers buttons tab's presets (GitHub issue 471): Patchy's own default and, at Seth's
// request, arrangements of Patchy's own buttons in the order other editors' Layers panels use
// (docs/tool-palette.md, "Customizing the palette"). A preset is not stored: the dialog shows
// the one whose shown buttons, in order, match the customization, else Custom.
enum class LayerFooterPreset { Patchy, Photoshop, AffinityPhoto, Gimp, Krita };
inline constexpr LayerFooterPreset kLayerFooterPresets[] = {LayerFooterPreset::Patchy, LayerFooterPreset::Photoshop,
                                                            LayerFooterPreset::AffinityPhoto, LayerFooterPreset::Gimp,
                                                            LayerFooterPreset::Krita};
// The combo's item token for each preset ("patchy", "photoshop", "affinity_photo", "gimp",
// "krita"; "custom" for none) and its translated label.
[[nodiscard]] QString layer_footer_preset_token(LayerFooterPreset preset);
[[nodiscard]] QString layer_footer_preset_label(LayerFooterPreset preset);
// Sets the footer order and hidden buttons to the preset's: its buttons shown in its order,
// every other one hidden after them in kLayerFooterButtonIds' order.
void apply_layer_footer_preset(ToolPaletteCustomization& value, LayerFooterPreset preset);
// The footer buttons the customization shows, in footer order.
[[nodiscard]] QStringList shown_layer_footer_buttons(const ToolPaletteCustomization& value);
// The preset showing the same buttons in the same order, or nothing (Custom).
[[nodiscard]] std::optional<LayerFooterPreset> matching_layer_footer_preset(const ToolPaletteCustomization& value);

// Sorted, without duplicates or empty ids, so equal choices compare and store equal; the
// footer order resolved, and empty when it is the default one.
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

// What the dialog edits: the customization, the two Preferences settings it also shows, and
// the Layers footer's own icon size (layersPanel/footerIconSize, app_settings.hpp).
struct ToolPaletteDialogChoice {
  ToolPaletteCustomization customization;
  ToolbarIconSize icon_size{ToolbarIconSize::Normal};
  ToolFlyoutOpenMode flyout_open_mode{ToolFlyoutOpenMode::Hold};
  ToolbarIconSize layer_icon_size{ToolbarIconSize::Normal};
};

// Translated names of the Preferences choices, shared by Preferences and the dialog.
[[nodiscard]] QString toolbar_icon_size_label(ToolbarIconSize size);
[[nodiscard]] QString tool_flyout_open_mode_label(ToolFlyoutOpenMode mode);

enum class ToolPaletteCustomizeTab { Tools, LayerButtons };

// The Customize Tool Palette dialog (non-modal, run_non_modal_dialog). Returns the choice on
// OK, nothing on Cancel. Its icon size and tool group combos are named
// toolPaletteIconSizeCombo and toolPaletteFlyoutOpenCombo (item data: the stored token), so
// an open dialog can follow a change made in Preferences. The Layers buttons tab's icon size
// combo is layerFooterIconSizeCombo; while the dialog is open each pick there goes to
// `preview_layer_icon_size` (when set) so the footer shows it at once, and Cancel sends the
// size the dialog opened with.
[[nodiscard]] std::optional<ToolPaletteDialogChoice> request_tool_palette_customization(
    QWidget* parent, const ToolPaletteCatalog& catalog, const ToolPaletteDialogChoice& current,
    ToolPaletteCustomizeTab tab = ToolPaletteCustomizeTab::Tools,
    const std::function<void(ToolbarIconSize)>& preview_layer_icon_size = {});

}  // namespace patchy::ui
