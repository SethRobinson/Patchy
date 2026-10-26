#pragma once

// Customize Tool Palette (GitHub issues 213, 316 and 199; docs/tool-palette.md, "Customizing
// the palette"): which tools the palette shows, one or two columns, wheel scrolling in a short
// window, the gear button, and which Layers panel footer buttons show. The settings keys and
// the tool and button ids stored under them are compatibility contracts.

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
  bool show_customize_button{true};
  // Layers panel footer button ids (kLayerFooterButtonIds) left out of the footer.
  QStringList hidden_layer_buttons;

  bool operator==(const ToolPaletteCustomization&) const = default;
};

// The footer buttons' permanent ids, in footer order.
inline constexpr const char* kLayerFooterButtonIds[] = {"new_layer",  "new_folder", "new_adjustment",
                                                        "layer_styles", "add_mask", "duplicate",
                                                        "animation",  "delete"};

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
  QIcon customize_icon;
  QColor foreground{Qt::black};
  QColor background{Qt::white};
};

enum class ToolPaletteCustomizeTab { Tools, LayerButtons };

// The Customize Tool Palette dialog (non-modal, run_non_modal_dialog). Returns the choice on
// OK, nothing on Cancel.
[[nodiscard]] std::optional<ToolPaletteCustomization> request_tool_palette_customization(
    QWidget* parent, const ToolPaletteCatalog& catalog, const ToolPaletteCustomization& current,
    ToolPaletteCustomizeTab tab = ToolPaletteCustomizeTab::Tools);

}  // namespace patchy::ui
