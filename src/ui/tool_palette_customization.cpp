#include "ui/tool_palette_customization.hpp"

#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/icon_theme.hpp"
#include "ui/theme_palette.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>

#include <algorithm>
#include <iterator>
#include <utility>

namespace patchy::ui {
namespace {

// Persisted identifiers: never rename them (see AGENTS.md).
QString hidden_tools_key() { return QStringLiteral("toolPalette/hiddenTools"); }
QString layout_key() { return QStringLiteral("toolPalette/layout"); }
QString wheel_scroll_key() { return QStringLiteral("toolPalette/wheelScroll"); }
// True when every tool shows on its own button (GitHub issue 499); absent for tool groups.
QString ungroup_tools_key() { return QStringLiteral("toolPalette/ungroupTools"); }
// toolPalette/showCustomizeButton belonged to the palette's gear, since removed: never reuse it.
QString hidden_layer_buttons_key() { return QStringLiteral("layersPanel/hiddenFooterButtons"); }
// The optional footer buttons the user showed (kLayerFooterButtonIdsHiddenByDefault).
QString shown_layer_buttons_key() { return QStringLiteral("layersPanel/shownFooterButtons"); }
QString hidden_footer_items_key() { return QStringLiteral("toolPalette/hiddenFooterItems"); }
// The footer buttons' order (GitHub issue 471), every known id once; absent for the default order.
QString layer_button_order_key() { return QStringLiteral("layersPanel/footerButtonOrder"); }
// The layout token; anything else (or nothing) is one column.
constexpr auto kTwoColumnsToken = "two_columns";

QStringList normalized_ids(QStringList ids) {
  ids.removeAll(QString());
  ids.sort();
  ids.removeDuplicates();
  return ids;
}

// The id stored on a tree row or list item.
constexpr int kIdRole = Qt::UserRole;

// A painted miniature of the palette (mockup 1's preview): the slots in one or two columns
// with the cluster separators and the flyout corner arrows, then the bottom items the choice
// keeps (color well, Quick Mask, screen mode), drawn at the chosen toolbar icon size. It
// repaints from the choice the dialog's controls hold, so it follows every tick.
class ToolPalettePreview final : public QWidget {
public:
  explicit ToolPalettePreview(const ToolPaletteCatalog& catalog, QWidget* parent)
      : QWidget(parent), catalog_(catalog) {
    setObjectName(QStringLiteral("toolPaletteCustomizePreview"));
  }

  void set_choice(const ToolPaletteDialogChoice& value) {
    choice_ = value;
    const QSize size = content_size();
    const double factor = draw_factor();
    setFixedSize(qCeil(size.width() * factor), qCeil(size.height() * factor));
    update();
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    QWidget::paintEvent(event);
    const auto& colors = theme();
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillRect(rect(), colors.tool_palette_bg);
    painter.setPen(colors.tool_palette_border);
    painter.drawLine(width() - 1, 0, width() - 1, height() - 1);
    // Laid out at the Normal size, drawn at the chosen one.
    const double factor = draw_factor();
    painter.scale(factor, factor);
    const QSize size = content_size();
    const int columns = choice_.customization.two_columns ? 2 : 1;
    const QIcon corner = themed_svg_icon(QStringLiteral("tool-flyout-corner"));
    int y = kMargin;
    bool first = true;
    for (const auto& cluster : catalog_.clusters) {
      const auto shown = visible_slots(cluster);
      if (shown.empty()) {
        continue;
      }
      if (!first) {
        painter.fillRect(QRect(kMargin, y + kSeparatorMargin, size.width() - 1 - 2 * kMargin, 1),
                         colors.tool_palette_separator);
        y += kSeparatorExtent + kSpacing;
      }
      first = false;
      for (std::size_t index = 0; index < shown.size(); ++index) {
        const int column = static_cast<int>(index) % columns;
        if (index > 0 && column == 0) {
          y += kCellHeight + kSpacing;
        }
        const QRect cell(kMargin + column * (kCellWidth + kSpacing), y, kCellWidth, kCellHeight);
        shown[index].icon.paint(&painter, cell.adjusted((kCellWidth - kIcon) / 2, (kCellHeight - kIcon) / 2,
                                                        -(kCellWidth - kIcon) / 2, -(kCellHeight - kIcon) / 2));
        // A tool group shows the palette's corner arrow (GitHub issue 213 follow-up).
        if (shown[index].flyout) {
          corner.paint(&painter, QRect(cell.right() - kCorner, cell.bottom() - kCorner, kCorner, kCorner));
        }
      }
      y += kCellHeight + kSpacing;
    }
    y += kFooterGap;
    if (shows(kToolPaletteColorsId)) {
      // The color well: background swatch lower right, foreground over it.
      painter.setPen(colors.tool_palette_border);
      const QRect background(kMargin + 9, y + 7, kSwatch, kSwatch);
      const QRect foreground(kMargin + 2, y, kSwatch, kSwatch);
      painter.fillRect(background, catalog_.background);
      painter.drawRect(background.adjusted(0, 0, -1, -1));
      painter.fillRect(foreground, catalog_.foreground);
      painter.drawRect(foreground.adjusted(0, 0, -1, -1));
      y += kWellHeight;
    }
    if (shows(kToolPaletteQuickMaskId)) {
      catalog_.quick_mask_icon.paint(&painter, QRect(kMargin + (kCellWidth - kFooterIcon) / 2,
                                                     y + (kFooterRow - kFooterIcon) / 2, kFooterIcon, kFooterIcon));
      y += kFooterRow;
    }
    if (shows(kToolPaletteScreenModeId)) {
      catalog_.screen_mode_icon.paint(&painter, QRect(kMargin + (kCellWidth - kFooterIcon) / 2,
                                                      y + (kFooterRow - kFooterIcon) / 2, kFooterIcon, kFooterIcon));
    }
  }

private:
  static constexpr int kMargin = 3;
  static constexpr int kCellWidth = 24;
  static constexpr int kCellHeight = 19;
  static constexpr int kIcon = 15;
  static constexpr int kCorner = 5;
  static constexpr int kSpacing = 1;
  static constexpr int kSeparatorMargin = 2;
  static constexpr int kSeparatorExtent = 2 * kSeparatorMargin + 1;
  static constexpr int kSwatch = 14;
  static constexpr int kWellHeight = kSwatch + 7 + 4;
  static constexpr int kFooterIcon = 13;
  static constexpr int kFooterRow = 17;
  static constexpr int kFooterGap = 6;

  // Taller than this, the miniature is drawn smaller so it stays in the dialog: one column
  // with no tool groups (GitHub issue 499) is about twice the grouped column.
  static constexpr int kMaxHeight = 520;

  struct PreviewSlot {
    QIcon icon;
    bool flyout{false};
  };

  // The chosen toolbar icon size, reduced when that would pass kMaxHeight.
  [[nodiscard]] double draw_factor() const {
    const double factor = toolbar_icon_size_factor(choice_.icon_size);
    const int height = content_size().height();
    return height * factor > kMaxHeight ? static_cast<double>(kMaxHeight) / height : factor;
  }

  [[nodiscard]] bool shows(const char* footer_item) const {
    return !choice_.customization.hidden_footer_items.contains(QLatin1String(footer_item));
  }

  // A group with one tool left shows as a plain button, and with no tool groups every tool
  // gets its own (GitHub issue 499), as on the palette.
  [[nodiscard]] std::vector<PreviewSlot> visible_slots(const ToolPaletteCatalogCluster& cluster) const {
    std::vector<PreviewSlot> shown;
    for (const auto& entry : cluster.entries) {
      std::vector<const ToolPaletteCatalogTool*> visible;
      for (const auto& member : entry.members) {
        if (!choice_.customization.hidden_tools.contains(member.id)) {
          visible.push_back(&member);
        }
      }
      if (visible.empty()) {
        continue;
      }
      if (choice_.customization.ungroup_tools) {
        for (const auto* member : visible) {
          shown.push_back({member->icon, false});
        }
      } else {
        shown.push_back({visible.front()->icon, entry.flyout && visible.size() > 1});
      }
    }
    return shown;
  }

  // The miniature's size at the Normal toolbar icon size.
  [[nodiscard]] QSize content_size() const {
    const int columns = choice_.customization.two_columns ? 2 : 1;
    int height = kMargin;
    bool first = true;
    for (const auto& cluster : catalog_.clusters) {
      const auto count = static_cast<int>(visible_slots(cluster).size());
      if (count == 0) {
        continue;
      }
      if (!first) {
        height += kSeparatorExtent + kSpacing;
      }
      first = false;
      const int rows = (count + columns - 1) / columns;
      height += rows * (kCellHeight + kSpacing);
    }
    height += kFooterGap;
    if (shows(kToolPaletteColorsId)) {
      height += kWellHeight;
    }
    if (shows(kToolPaletteQuickMaskId)) {
      height += kFooterRow;
    }
    if (shows(kToolPaletteScreenModeId)) {
      height += kFooterRow;
    }
    height += kMargin;
    const int width = 2 * kMargin + columns * kCellWidth + (columns - 1) * kSpacing + 1;
    return {width, height};
  }

  const ToolPaletteCatalog& catalog_;
  ToolPaletteDialogChoice choice_;
};

// The slot list. A cluster heading is bottom-aligned in a row taller than a tool's, and Qt
// centers the expand/collapse sign in the row, above the name: the heading's sign is drawn in
// the band of its text instead, so the sign and the name line up (GitHub issue 213 follow-up).
// The shortcut column: its keys start together, left aligned (GitHub issue 499: right-aligned
// letters of different widths looked ragged), and the widest ends kGap px before the list's
// right edge, where the scrollbar sits (GitHub issue 213 follow-up). An item-view QSS
// padding-right only widens the column's size hint; the style still draws to the cell edge.
// The row's background is drawn across the whole cell, the text in the inset rect.
class ShortcutColumnDelegate final : public QStyledItemDelegate {
public:
  static constexpr int kGap = 12;
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    QStyleOptionViewItem cell(option);
    initStyleOption(&cell, index);
    auto* style = cell.widget != nullptr ? cell.widget->style() : QApplication::style();
    QStyleOptionViewItem background(cell);
    background.text.clear();
    style->drawControl(QStyle::CE_ItemViewItem, &background, painter, cell.widget);
    QStyleOptionViewItem text(cell);
    text.rect.adjust(0, 0, -kGap, 0);
    text.backgroundBrush = Qt::NoBrush;
    text.state &= ~(QStyle::State_MouseOver | QStyle::State_Selected | QStyle::State_HasFocus);
    style->drawControl(QStyle::CE_ItemViewItem, &text, painter, cell.widget);
  }

  [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    auto size = QStyledItemDelegate::sizeHint(option, index);
    size.rwidth() += kGap;
    return size;
  }
};

class ToolPaletteCustomizeTree final : public QTreeWidget {
public:
  using QTreeWidget::QTreeWidget;

protected:
  void drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const override {
    const auto* item = itemFromIndex(index);
    if (item == nullptr || item->parent() != nullptr) {
      QTreeWidget::drawBranches(painter, rect, index);
      return;
    }
    const int text_height = QFontMetrics(item->font(0)).height();
    const QRect band(rect.left(), rect.bottom() + 1 - text_height, rect.width(), text_height);
    QTreeWidget::drawBranches(painter, band.intersected(rect), index);
  }
};

QStringList default_layer_footer_order() {
  QStringList order;
  for (const auto* id : kLayerFooterButtonIds) {
    order.push_back(QString::fromLatin1(id));
  }
  return order;
}

}  // namespace

QStringList resolved_layer_footer_order(const QStringList& stored) {
  const auto known = default_layer_footer_order();
  QStringList order;
  for (const auto& id : stored) {
    if (known.contains(id) && !order.contains(id)) {
      order.push_back(id);
    }
  }
  for (qsizetype index = 0; index < known.size(); ++index) {
    if (order.contains(known[index])) {
      continue;
    }
    qsizetype position = 0;
    for (auto previous = index; previous > 0; --previous) {
      if (const auto at = order.indexOf(known[previous - 1]); at >= 0) {
        position = at + 1;
        break;
      }
    }
    order.insert(position, known[index]);
  }
  return order;
}

QString layer_footer_preset_token(LayerFooterPreset preset) {
  switch (preset) {
    case LayerFooterPreset::Patchy:
      break;
    case LayerFooterPreset::Photoshop:
      return QStringLiteral("photoshop");
    case LayerFooterPreset::AffinityPhoto:
      return QStringLiteral("affinity_photo");
    case LayerFooterPreset::Gimp:
      return QStringLiteral("gimp");
    case LayerFooterPreset::Krita:
      return QStringLiteral("krita");
  }
  return QStringLiteral("patchy");
}

// The other editors' names are product names: translators keep them as they are.
QString layer_footer_preset_label(LayerFooterPreset preset) {
  switch (preset) {
    case LayerFooterPreset::Patchy:
      break;
    case LayerFooterPreset::Photoshop:
      return QObject::tr("Adobe Photoshop", "Layers buttons preset; a product name");
    case LayerFooterPreset::AffinityPhoto:
      return QObject::tr("Affinity Photo", "Layers buttons preset; a product name");
    case LayerFooterPreset::Gimp:
      return QObject::tr("GIMP", "Layers buttons preset; a product name");
    case LayerFooterPreset::Krita:
      return QObject::tr("Krita", "Layers buttons preset; a product name");
  }
  return QObject::tr("Patchy", "Layers buttons preset: Patchy's own default");
}

void apply_layer_footer_preset(ToolPaletteCustomization& value, LayerFooterPreset preset) {
  const auto known = default_layer_footer_order();
  QStringList unknown_hidden;
  for (const auto& id : value.hidden_layer_buttons) {
    if (!known.contains(id)) {
      unknown_hidden.push_back(id);
    }
  }
  if (preset == LayerFooterPreset::Patchy) {
    value.layer_button_order.clear();
    value.hidden_layer_buttons = unknown_hidden;
    for (const auto* id : kLayerFooterButtonIdsHiddenByDefault) {
      value.hidden_layer_buttons.push_back(QString::fromLatin1(id));
    }
    return;
  }
  // The other editors' Layers panel buttons, left to right, that Patchy also has (their link,
  // live filter, anchor and properties buttons have no Patchy counterpart).
  QStringList shown;
  switch (preset) {
    case LayerFooterPreset::Patchy:
      break;
    case LayerFooterPreset::Photoshop:
      shown = {QStringLiteral("layer_styles"), QStringLiteral("add_mask"),   QStringLiteral("new_adjustment"),
               QStringLiteral("new_folder"),   QStringLiteral("new_layer"), QStringLiteral("delete")};
      break;
    case LayerFooterPreset::AffinityPhoto:
      shown = {QStringLiteral("duplicate"),  QStringLiteral("add_mask"),  QStringLiteral("new_adjustment"),
               QStringLiteral("layer_styles"), QStringLiteral("new_folder"), QStringLiteral("new_layer"),
               QStringLiteral("delete")};
      break;
    case LayerFooterPreset::Gimp:
      shown = {QStringLiteral("new_layer"), QStringLiteral("new_folder"), QStringLiteral("move_up"),
               QStringLiteral("move_down"), QStringLiteral("duplicate"),  QStringLiteral("merge_down"),
               QStringLiteral("add_mask"),  QStringLiteral("delete")};
      break;
    case LayerFooterPreset::Krita:
      // Krita's add button also offers groups, adjustment layers and masks (#471 follow-up).
      shown = {QStringLiteral("new_layer"),      QStringLiteral("new_folder"), QStringLiteral("duplicate"),
               QStringLiteral("new_adjustment"), QStringLiteral("add_mask"),   QStringLiteral("move_down"),
               QStringLiteral("move_up"),        QStringLiteral("layer_styles"), QStringLiteral("delete")};
      break;
  }
  value.layer_button_order = shown;
  value.hidden_layer_buttons = unknown_hidden;
  for (const auto& id : known) {
    if (!shown.contains(id)) {
      value.layer_button_order.push_back(id);
      value.hidden_layer_buttons.push_back(id);
    }
  }
}

QStringList shown_layer_footer_buttons(const ToolPaletteCustomization& value) {
  QStringList shown;
  for (const auto& id : resolved_layer_footer_order(value.layer_button_order)) {
    if (!value.hidden_layer_buttons.contains(id)) {
      shown.push_back(id);
    }
  }
  return shown;
}

std::optional<LayerFooterPreset> matching_layer_footer_preset(const ToolPaletteCustomization& value) {
  const auto shown = shown_layer_footer_buttons(value);
  for (const auto preset : kLayerFooterPresets) {
    ToolPaletteCustomization arranged;
    apply_layer_footer_preset(arranged, preset);
    if (shown_layer_footer_buttons(arranged) == shown) {
      return preset;
    }
  }
  return std::nullopt;
}

ToolPaletteCustomization normalized_tool_palette_customization(ToolPaletteCustomization value) {
  value.hidden_tools = normalized_ids(std::move(value.hidden_tools));
  value.hidden_layer_buttons = normalized_ids(std::move(value.hidden_layer_buttons));
  value.hidden_footer_items = normalized_ids(std::move(value.hidden_footer_items));
  auto order = resolved_layer_footer_order(value.layer_button_order);
  value.layer_button_order = order == default_layer_footer_order() ? QStringList{} : std::move(order);
  return value;
}

ToolPaletteCustomization stored_tool_palette_customization() {
  const auto settings = app_settings();
  ToolPaletteCustomization value;
  value.hidden_tools = settings.value(hidden_tools_key()).toStringList();
  value.two_columns = settings.value(layout_key()).toString() == QLatin1String(kTwoColumnsToken);
  value.wheel_scroll = settings.value(wheel_scroll_key(), true).toBool();
  value.ungroup_tools = settings.value(ungroup_tools_key(), false).toBool();
  value.hidden_layer_buttons = settings.value(hidden_layer_buttons_key()).toStringList();
  const auto shown_layer_buttons = settings.value(shown_layer_buttons_key()).toStringList();
  for (const auto* id : kLayerFooterButtonIdsHiddenByDefault) {
    if (!shown_layer_buttons.contains(QLatin1String(id))) {
      value.hidden_layer_buttons.push_back(QString::fromLatin1(id));
    }
  }
  value.hidden_footer_items = settings.value(hidden_footer_items_key()).toStringList();
  value.layer_button_order = settings.value(layer_button_order_key()).toStringList();
  return normalized_tool_palette_customization(std::move(value));
}

void set_stored_tool_palette_customization(const ToolPaletteCustomization& value) {
  const auto normalized = normalized_tool_palette_customization(value);
  auto settings = app_settings();
  // Defaults are removed rather than written, so a reset leaves no keys behind.
  const auto store_list = [&settings](const QString& key, const QStringList& ids) {
    if (ids.isEmpty()) {
      settings.remove(key);
    } else {
      settings.setValue(key, ids);
    }
  };
  store_list(hidden_tools_key(), normalized.hidden_tools);
  // An optional button is stored when shown, every other one when hidden.
  QStringList hidden_layer_buttons;
  QStringList shown_layer_buttons;
  for (const auto* id : kLayerFooterButtonIdsHiddenByDefault) {
    if (!normalized.hidden_layer_buttons.contains(QLatin1String(id))) {
      shown_layer_buttons.push_back(QString::fromLatin1(id));
    }
  }
  for (const auto& id : normalized.hidden_layer_buttons) {
    if (std::none_of(std::begin(kLayerFooterButtonIdsHiddenByDefault), std::end(kLayerFooterButtonIdsHiddenByDefault),
                     [&id](const char* optional) { return id == QLatin1String(optional); })) {
      hidden_layer_buttons.push_back(id);
    }
  }
  store_list(hidden_layer_buttons_key(), hidden_layer_buttons);
  store_list(shown_layer_buttons_key(), normalized_ids(std::move(shown_layer_buttons)));
  store_list(hidden_footer_items_key(), normalized.hidden_footer_items);
  store_list(layer_button_order_key(), normalized.layer_button_order);
  if (normalized.two_columns) {
    settings.setValue(layout_key(), QString::fromLatin1(kTwoColumnsToken));
  } else {
    settings.remove(layout_key());
  }
  if (normalized.wheel_scroll) {
    settings.remove(wheel_scroll_key());
  } else {
    settings.setValue(wheel_scroll_key(), false);
  }
  if (normalized.ungroup_tools) {
    settings.setValue(ungroup_tools_key(), true);
  } else {
    settings.remove(ungroup_tools_key());
  }
}

// The Preferences labels live in MainWindow's translation context, where Preferences
// has always had them.
QString toolbar_icon_size_label(ToolbarIconSize size) {
  switch (size) {
    case ToolbarIconSize::Smallest:
      return QCoreApplication::translate("patchy::ui::MainWindow", "Smallest", "toolbar icon size");
    case ToolbarIconSize::Small:
      return QCoreApplication::translate("patchy::ui::MainWindow", "Small", "toolbar icon size");
    case ToolbarIconSize::Normal:
      break;
    case ToolbarIconSize::Bigger:
      return QCoreApplication::translate("patchy::ui::MainWindow", "Bigger", "toolbar icon size");
    case ToolbarIconSize::Biggest:
      return QCoreApplication::translate("patchy::ui::MainWindow", "Biggest", "toolbar icon size");
  }
  return QCoreApplication::translate("patchy::ui::MainWindow", "Normal", "toolbar icon size");
}

QString tool_flyout_open_mode_label(ToolFlyoutOpenMode mode) {
  switch (mode) {
    case ToolFlyoutOpenMode::Hold:
      break;
    case ToolFlyoutOpenMode::Click:
      return QCoreApplication::translate("patchy::ui::MainWindow", "Click", "when a tool group opens");
    case ToolFlyoutOpenMode::Hover:
      return QCoreApplication::translate("patchy::ui::MainWindow", "Hover", "when a tool group opens");
  }
  return QCoreApplication::translate("patchy::ui::MainWindow", "Click and hold", "when a tool group opens");
}

std::optional<ToolPaletteDialogChoice> request_tool_palette_customization(QWidget* parent,
                                                                           const ToolPaletteCatalog& catalog,
                                                                           const ToolPaletteDialogChoice& current,
                                                                           ToolPaletteCustomizeTab tab,
                                                                           const std::function<void(ToolbarIconSize)>&
                                                                               preview_layer_icon_size) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("toolPaletteCustomizeDialog"));
  auto* root = new QVBoxLayout(&dialog);
  auto* content = install_dark_dialog_chrome(dialog, root, QObject::tr("Customize Tool Palette"));

  auto* tabs = new QTabWidget(&dialog);
  tabs->setObjectName(QStringLiteral("toolPaletteCustomizeTabs"));
  content->addWidget(tabs, 1);

  // Tools tab: the slot list on the left, the options in the middle, the preview on the right.
  auto* tools_page = new QWidget(tabs);
  auto* tools_layout = new QHBoxLayout(tools_page);
  auto* list_column = new QVBoxLayout();
  auto* tools_hint = new QLabel(QObject::tr("Tick the tools to show. Hidden tools keep their shortcuts."), tools_page);
  tools_hint->setObjectName(QStringLiteral("toolPaletteCustomizeHint"));
  tools_hint->setWordWrap(true);
  list_column->addWidget(tools_hint);
  auto* tree = new ToolPaletteCustomizeTree(tools_page);
  tree->setObjectName(QStringLiteral("toolPaletteCustomizeTree"));
  tree->setColumnCount(2);
  tree->setHeaderHidden(true);
  tree->setRootIsDecorated(true);
  tree->setSelectionMode(QAbstractItemView::NoSelection);
  tree->setFocusPolicy(Qt::StrongFocus);
  tree->setIconSize(QSize(16, 16));
  // The cluster headings are taller than the tool rows.
  tree->setUniformRowHeights(false);
  tree->header()->setStretchLastSection(false);
  tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  tree->setItemDelegateForColumn(1, new ShortcutColumnDelegate(tree));
  tree->setMinimumSize(340, 420);
  list_column->addWidget(tree, 1);
  tools_layout->addLayout(list_column, 1);

  auto* side = new QVBoxLayout();
  auto* layout_label = new QLabel(QObject::tr("Layout"), tools_page);
  QFont heading_font = layout_label->font();
  heading_font.setBold(true);
  layout_label->setFont(heading_font);
  side->addWidget(layout_label);
  auto* one_column = new QRadioButton(QObject::tr("One column"), tools_page);
  one_column->setObjectName(QStringLiteral("toolPaletteOneColumnRadio"));
  auto* two_columns = new QRadioButton(QObject::tr("Two columns, tools side by side"), tools_page);
  two_columns->setObjectName(QStringLiteral("toolPaletteTwoColumnsRadio"));
  auto* layout_group = new QButtonGroup(tools_page);
  layout_group->addButton(one_column);
  layout_group->addButton(two_columns);
  side->addWidget(one_column);
  side->addWidget(two_columns);
  side->addSpacing(6);
  // GitHub issue 499: every tool on its own button, nothing hidden in a flyout.
  auto* ungroup_tools = new QCheckBox(QObject::tr("No tool groups: every tool gets its own button"), tools_page);
  ungroup_tools->setObjectName(QStringLiteral("toolPaletteUngroupToolsCheck"));
  side->addWidget(ungroup_tools);
  auto* wheel_scroll = new QCheckBox(QObject::tr("Mouse wheel scrolls the palette when it overflows"), tools_page);
  wheel_scroll->setObjectName(QStringLiteral("toolPaletteWheelScrollCheck"));
  side->addWidget(wheel_scroll);
  auto* wheel_hint = new QLabel(QObject::tr("Otherwise the > arrow opens more columns."), tools_page);
  wheel_hint->setObjectName(QStringLiteral("toolPaletteWheelScrollHint"));
  wheel_hint->setIndent(22);
  side->addWidget(wheel_hint);
  side->addSpacing(10);

  // The two Preferences settings that shape the palette, kept in step with Preferences
  // (GitHub issues 213 and 459): the same keys, and an open Preferences follows OK here.
  auto* buttons_label = new QLabel(QObject::tr("Buttons"), tools_page);
  buttons_label->setFont(heading_font);
  side->addWidget(buttons_label);
  auto* buttons_form = new QFormLayout();
  buttons_form->setContentsMargins(0, 0, 0, 0);
  auto* icon_size = new QComboBox(tools_page);
  icon_size->setObjectName(QStringLiteral("toolPaletteIconSizeCombo"));
  icon_size->setToolTip(QCoreApplication::translate(
      "patchy::ui::MainWindow", "Size of the tool palette's icons and buttons, relative to the interface scale."));
  for (const auto size : kToolbarIconSizes) {
    icon_size->addItem(toolbar_icon_size_label(size), toolbar_icon_size_token(size));
  }
  icon_size->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  buttons_form->addRow(QCoreApplication::translate("patchy::ui::MainWindow", "Toolbar icon size:"), icon_size);
  auto* flyout_open = new QComboBox(tools_page);
  flyout_open->setObjectName(QStringLiteral("toolPaletteFlyoutOpenCombo"));
  flyout_open->setToolTip(QCoreApplication::translate(
      "patchy::ui::MainWindow", "How a tool group's menu opens: press and hold its button, click it, or rest the pointer on it."));
  for (const auto mode : kToolFlyoutOpenModes) {
    flyout_open->addItem(tool_flyout_open_mode_label(mode), tool_flyout_open_mode_token(mode));
  }
  flyout_open->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  buttons_form->addRow(QCoreApplication::translate("patchy::ui::MainWindow", "Open tool groups on:"), flyout_open);
  side->addLayout(buttons_form);
  side->addSpacing(10);

  // The bottom of the palette (GitHub issues 212 and 213): each item can leave it.
  auto* footer_label = new QLabel(QObject::tr("Bottom of the palette"), tools_page);
  footer_label->setFont(heading_font);
  side->addWidget(footer_label);
  struct FooterCheck {
    const char* id;
    QCheckBox* box;
  };
  const std::vector<FooterCheck> footer_checks{
      {kToolPaletteColorsId, new QCheckBox(QObject::tr("Color swatches"), tools_page)},
      {kToolPaletteQuickMaskId, new QCheckBox(QObject::tr("Quick Mask button"), tools_page)},
      {kToolPaletteScreenModeId, new QCheckBox(QObject::tr("Screen mode button"), tools_page)}};
  footer_checks[0].box->setObjectName(QStringLiteral("toolPaletteShowColorsCheck"));
  footer_checks[1].box->setObjectName(QStringLiteral("toolPaletteShowQuickMaskCheck"));
  footer_checks[2].box->setObjectName(QStringLiteral("toolPaletteShowScreenModeCheck"));
  for (const auto& check : footer_checks) {
    side->addWidget(check.box);
  }
  side->addStretch(1);
  tools_layout->addSpacing(8);
  tools_layout->addLayout(side);

  auto* preview_column = new QVBoxLayout();
  auto* preview_label = new QLabel(QObject::tr("Preview"), tools_page);
  preview_label->setFont(heading_font);
  preview_column->addWidget(preview_label);
  auto* preview = new ToolPalettePreview(catalog, tools_page);
  preview_column->addWidget(preview, 0, Qt::AlignLeft);
  preview_column->addStretch(1);
  tools_layout->addSpacing(8);
  tools_layout->addLayout(preview_column);
  tabs->addTab(tools_page, QObject::tr("Tools"));

  // Layers buttons tab: one tickable row per footer button, in footer order. Move Up and Move
  // Down beside the list, or a drag, change the order (GitHub issue 471).
  auto* layers_page = new QWidget(tabs);
  auto* layers_layout = new QVBoxLayout(layers_page);
  auto* layers_hint = new QLabel(QObject::tr("Tick the buttons the Layers panel shows, and drag them or use Move Up "
                                             "and Move Down to set their order. Hidden buttons' commands stay in the "
                                             "menus."),
                                 layers_page);
  layers_hint->setWordWrap(true);
  layers_layout->addWidget(layers_hint);
  // A preset sets the order and which buttons show; any other change makes it Custom.
  auto* preset_row = new QHBoxLayout();
  auto* preset_label = new QLabel(QObject::tr("Preset:"), layers_page);
  auto* preset_combo = new QComboBox(layers_page);
  preset_combo->setObjectName(QStringLiteral("layerFooterPresetCombo"));
  for (const auto preset : kLayerFooterPresets) {
    preset_combo->addItem(layer_footer_preset_label(preset), layer_footer_preset_token(preset));
  }
  preset_combo->addItem(QObject::tr("Custom", "Layers buttons preset: the user's own arrangement"),
                        QStringLiteral("custom"));
  preset_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  preset_label->setBuddy(preset_combo);
  preset_row->addWidget(preset_label);
  preset_row->addWidget(preset_combo);
  // The footer's own icon size (GitHub issue 213 follow-up): the toolbar icon size's steps,
  // stored apart, shown on the footer as soon as it is picked.
  preset_row->addSpacing(16);
  auto* layer_icon_size_label = new QLabel(QObject::tr("Icon size:"), layers_page);
  auto* layer_icon_size = new QComboBox(layers_page);
  layer_icon_size->setObjectName(QStringLiteral("layerFooterIconSizeCombo"));
  layer_icon_size->setToolTip(
      QObject::tr("Size of the Layers panel's buttons, relative to the interface scale."));
  for (const auto size : kToolbarIconSizes) {
    layer_icon_size->addItem(toolbar_icon_size_label(size), toolbar_icon_size_token(size));
  }
  layer_icon_size->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  layer_icon_size_label->setBuddy(layer_icon_size);
  preset_row->addWidget(layer_icon_size_label);
  preset_row->addWidget(layer_icon_size);
  preset_row->addStretch(1);
  layers_layout->addLayout(preset_row);
  auto* layers_row = new QHBoxLayout();
  auto* layer_list = new QListWidget(layers_page);
  layer_list->setObjectName(QStringLiteral("layerFooterButtonsList"));
  layer_list->setIconSize(QSize(20, 20));
  layer_list->setSelectionMode(QAbstractItemView::SingleSelection);
  layer_list->setDragDropMode(QAbstractItemView::InternalMove);
  layer_list->setDefaultDropAction(Qt::MoveAction);
  for (const auto& button : catalog.layer_buttons) {
    auto* item = new QListWidgetItem(button.icon, button.name, layer_list);
    item->setData(kIdRole, button.id);
    // No ItemIsDropEnabled: a drop lands between rows, never onto one.
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
  }
  layers_row->addWidget(layer_list, 1);
  auto* order_column = new QVBoxLayout();
  auto* order_up = new QPushButton(QObject::tr("Move Up"), layers_page);
  order_up->setObjectName(QStringLiteral("layerFooterOrderUpButton"));
  order_up->setAutoDefault(false);
  auto* order_down = new QPushButton(QObject::tr("Move Down"), layers_page);
  order_down->setObjectName(QStringLiteral("layerFooterOrderDownButton"));
  order_down->setAutoDefault(false);
  order_column->addWidget(order_up);
  order_column->addWidget(order_down);
  order_column->addStretch(1);
  layers_row->addLayout(order_column);
  layers_layout->addLayout(layers_row, 1);
  tabs->addTab(layers_page, QObject::tr("Layers buttons"));

  // The tree: a header row per cluster, a tickable row per slot, and a flyout's members
  // under it (auto-tristate: a partly hidden flyout shows a partial check).
  struct SlotRow {
    QTreeWidgetItem* item;
    const ToolPaletteCatalogSlot* entry;
  };
  std::vector<SlotRow> slot_rows;
  for (const auto& cluster : catalog.clusters) {
    auto* header = new QTreeWidgetItem(tree);
    header->setText(0, cluster.name.toUpper());
    header->setFlags(Qt::ItemIsEnabled);
    header->setFirstColumnSpanned(true);
    header->setFont(0, heading_font);
    header->setForeground(0, theme().text_secondary);
    // Padding above and below the heading, from the font rather than a fixed height.
    header->setSizeHint(0, QSize(0, QFontMetrics(heading_font).height() + 18));
    header->setTextAlignment(0, Qt::AlignLeft | Qt::AlignBottom);
    for (const auto& entry : cluster.entries) {
      auto* row = new QTreeWidgetItem(header);
      const auto& lead = entry.members.front();
      row->setText(0, entry.flyout ? entry.name : lead.name);
      row->setIcon(0, lead.icon);
      row->setText(1, entry.shortcut);
      row->setTextAlignment(1, Qt::AlignLeft | Qt::AlignVCenter);
      row->setForeground(1, theme().text_secondary);
      if (entry.flyout) {
        row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        for (const auto& member : entry.members) {
          auto* child = new QTreeWidgetItem(row);
          child->setText(0, member.name);
          child->setIcon(0, member.icon);
          child->setText(1, member.shortcut);
          child->setTextAlignment(1, Qt::AlignLeft | Qt::AlignVCenter);
          child->setForeground(1, theme().text_secondary);
          child->setData(0, kIdRole, member.id);
          child->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
          child->setCheckState(0, Qt::Checked);
        }
      } else {
        row->setData(0, kIdRole, lead.id);
        row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        row->setCheckState(0, Qt::Checked);
      }
      slot_rows.push_back({row, &entry});
    }
    header->setExpanded(true);
  }

  // Ids this build does not list stay as they were (a newer version's settings survive).
  const auto keep_unlisted = [](const QStringList& previous, const QStringList& listed, QStringList& out) {
    for (const auto& id : previous) {
      if (!listed.contains(id)) {
        out.push_back(id);
      }
    }
  };
  QStringList listed_tools;
  for (const auto& row : slot_rows) {
    for (const auto& member : row.entry->members) {
      listed_tools.push_back(member.id);
    }
  }
  QStringList listed_layer_buttons;
  for (const auto& button : catalog.layer_buttons) {
    listed_layer_buttons.push_back(button.id);
  }
  QStringList listed_footer_items;
  for (const auto& check : footer_checks) {
    listed_footer_items.push_back(QString::fromLatin1(check.id));
  }

  bool populating = false;
  const auto read_widgets = [&] {
    ToolPaletteDialogChoice choice;
    auto& value = choice.customization;
    for (const auto& row : slot_rows) {
      if (row.entry->flyout) {
        for (int index = 0; index < row.item->childCount(); ++index) {
          auto* child = row.item->child(index);
          if (child->checkState(0) != Qt::Checked) {
            value.hidden_tools.push_back(child->data(0, kIdRole).toString());
          }
        }
      } else if (row.item->checkState(0) != Qt::Checked) {
        value.hidden_tools.push_back(row.item->data(0, kIdRole).toString());
      }
    }
    keep_unlisted(current.customization.hidden_tools, listed_tools, value.hidden_tools);
    value.two_columns = two_columns->isChecked();
    value.wheel_scroll = wheel_scroll->isChecked();
    value.ungroup_tools = ungroup_tools->isChecked();
    // The default choice already hides the optional buttons: start from the rows alone.
    value.hidden_layer_buttons.clear();
    for (int index = 0; index < layer_list->count(); ++index) {
      auto* item = layer_list->item(index);
      if (item->checkState() != Qt::Checked) {
        value.hidden_layer_buttons.push_back(item->data(kIdRole).toString());
      }
    }
    keep_unlisted(current.customization.hidden_layer_buttons, listed_layer_buttons, value.hidden_layer_buttons);
    for (int index = 0; index < layer_list->count(); ++index) {
      value.layer_button_order.push_back(layer_list->item(index)->data(kIdRole).toString());
    }
    for (const auto& check : footer_checks) {
      if (!check.box->isChecked()) {
        value.hidden_footer_items.push_back(QString::fromLatin1(check.id));
      }
    }
    keep_unlisted(current.customization.hidden_footer_items, listed_footer_items, value.hidden_footer_items);
    value = normalized_tool_palette_customization(std::move(value));
    choice.icon_size = toolbar_icon_size_from_token(icon_size->currentData().toString());
    choice.flyout_open_mode = tool_flyout_open_mode_from_token(flyout_open->currentData().toString());
    choice.layer_icon_size = toolbar_icon_size_from_token(layer_icon_size->currentData().toString());
    return choice;
  };
  const auto refresh_preview = [&] {
    if (!populating) {
      preview->set_choice(read_widgets());
    }
  };
  // The Layers rows' order and hidden buttons, and the rows set from a customization's.
  const auto layer_rows_customization = [&] {
    ToolPaletteCustomization value;
    value.hidden_layer_buttons.clear();
    for (int index = 0; index < layer_list->count(); ++index) {
      auto* item = layer_list->item(index);
      const auto id = item->data(kIdRole).toString();
      value.layer_button_order.push_back(id);
      if (item->checkState() != Qt::Checked) {
        value.hidden_layer_buttons.push_back(id);
      }
    }
    keep_unlisted(current.customization.hidden_layer_buttons, listed_layer_buttons, value.hidden_layer_buttons);
    return value;
  };
  const auto set_layer_rows = [&](const ToolPaletteCustomization& value) {
    // The rows in the footer order; a row the order does not name stays below.
    int next_row = 0;
    for (const auto& id : resolved_layer_footer_order(value.layer_button_order)) {
      for (int index = next_row; index < layer_list->count(); ++index) {
        if (layer_list->item(index)->data(kIdRole).toString() == id) {
          layer_list->insertItem(next_row++, layer_list->takeItem(index));
          break;
        }
      }
    }
    layer_list->setCurrentRow(-1);
    for (int index = 0; index < layer_list->count(); ++index) {
      auto* item = layer_list->item(index);
      item->setCheckState(value.hidden_layer_buttons.contains(item->data(kIdRole).toString()) ? Qt::Unchecked
                                                                                            : Qt::Checked);
    }
  };
  // The combo names the preset the rows match, else Custom.
  const auto refresh_preset_combo = [&] {
    const auto preset = matching_layer_footer_preset(layer_rows_customization());
    const auto token = preset.has_value() ? layer_footer_preset_token(*preset) : QStringLiteral("custom");
    const QSignalBlocker block(preset_combo);
    preset_combo->setCurrentIndex(std::max(0, preset_combo->findData(token)));
  };
  const auto populate = [&](const ToolPaletteDialogChoice& choice) {
    const auto& value = choice.customization;
    populating = true;
    for (const auto& row : slot_rows) {
      if (row.entry->flyout) {
        bool partial = false;
        for (int index = 0; index < row.item->childCount(); ++index) {
          auto* child = row.item->child(index);
          const bool hidden = value.hidden_tools.contains(child->data(0, kIdRole).toString());
          child->setCheckState(0, hidden ? Qt::Unchecked : Qt::Checked);
          partial = partial || hidden;
        }
        // A partly hidden flyout opens, so its unticked members show.
        row.item->setExpanded(partial && row.item->checkState(0) == Qt::PartiallyChecked);
      } else {
        row.item->setCheckState(0, value.hidden_tools.contains(row.item->data(0, kIdRole).toString())
                                       ? Qt::Unchecked
                                       : Qt::Checked);
      }
    }
    (value.two_columns ? two_columns : one_column)->setChecked(true);
    wheel_scroll->setChecked(value.wheel_scroll);
    ungroup_tools->setChecked(value.ungroup_tools);
    set_layer_rows(value);
    refresh_preset_combo();
    for (const auto& check : footer_checks) {
      check.box->setChecked(!value.hidden_footer_items.contains(QString::fromLatin1(check.id)));
    }
    icon_size->setCurrentIndex(std::max(0, icon_size->findData(toolbar_icon_size_token(choice.icon_size))));
    flyout_open->setCurrentIndex(
        std::max(0, flyout_open->findData(tool_flyout_open_mode_token(choice.flyout_open_mode))));
    layer_icon_size->setCurrentIndex(
        std::max(0, layer_icon_size->findData(toolbar_icon_size_token(choice.layer_icon_size))));
    populating = false;
    refresh_preview();
  };
  populate(current);
  tree->resizeColumnToContents(1);

  QObject::connect(tree, &QTreeWidget::itemChanged, &dialog, [&](QTreeWidgetItem*, int) { refresh_preview(); });
  QObject::connect(layer_list, &QListWidget::itemChanged, &dialog, [&](QListWidgetItem*) {
    if (!populating) {
      refresh_preset_combo();
    }
    refresh_preview();
  });
  // A preset sets the rows; Custom leaves them as they are.
  QObject::connect(preset_combo, &QComboBox::activated, &dialog, [&](int index) {
    const auto token = preset_combo->itemData(index).toString();
    for (const auto preset : kLayerFooterPresets) {
      if (layer_footer_preset_token(preset) != token) {
        continue;
      }
      auto value = layer_rows_customization();
      apply_layer_footer_preset(value, preset);
      populating = true;
      set_layer_rows(value);
      populating = false;
      refresh_preset_combo();
      refresh_preview();
      return;
    }
  });
  // Move Up and Move Down act on the current row; each is off where it would do nothing.
  const auto refresh_order_buttons = [&] {
    const int row = layer_list->currentRow();
    order_up->setEnabled(row > 0);
    order_down->setEnabled(row >= 0 && row + 1 < layer_list->count());
  };
  const auto move_current_row = [&](int step) {
    const int row = layer_list->currentRow();
    const int target = row + step;
    if (row < 0 || target < 0 || target >= layer_list->count()) {
      return;
    }
    layer_list->insertItem(target, layer_list->takeItem(row));
    layer_list->setCurrentRow(target);
    refresh_order_buttons();
    refresh_preset_combo();
  };
  QObject::connect(layer_list, &QListWidget::currentRowChanged, &dialog, [&](int) { refresh_order_buttons(); });
  QObject::connect(layer_list->model(), &QAbstractItemModel::rowsMoved, &dialog, [&] {
    refresh_order_buttons();
    if (!populating) {
      refresh_preset_combo();
    }
  });
  QObject::connect(order_up, &QPushButton::clicked, &dialog, [&] { move_current_row(-1); });
  QObject::connect(order_down, &QPushButton::clicked, &dialog, [&] { move_current_row(1); });
  refresh_order_buttons();
  std::vector<QAbstractButton*> toggles{two_columns, wheel_scroll, ungroup_tools};
  // Without tool groups there is no flyout to open.
  const auto refresh_flyout_open = [&] { flyout_open->setEnabled(!ungroup_tools->isChecked()); };
  QObject::connect(ungroup_tools, &QAbstractButton::toggled, &dialog, [&](bool) { refresh_flyout_open(); });
  refresh_flyout_open();
  for (const auto& check : footer_checks) {
    toggles.push_back(check.box);
  }
  for (auto* box : toggles) {
    QObject::connect(box, &QAbstractButton::toggled, &dialog, [&](bool) { refresh_preview(); });
  }
  for (auto* combo : {icon_size, flyout_open}) {
    QObject::connect(combo, &QComboBox::currentIndexChanged, &dialog, [&](int) { refresh_preview(); });
  }
  if (preview_layer_icon_size) {
    QObject::connect(layer_icon_size, &QComboBox::currentIndexChanged, &dialog, [&](int) {
      preview_layer_icon_size(toolbar_icon_size_from_token(layer_icon_size->currentData().toString()));
    });
  }

  // Reset to Default on the left, OK and Cancel on the right.
  auto* footer = new QHBoxLayout();
  auto* reset = new QPushButton(QObject::tr("Reset to Default"), &dialog);
  reset->setObjectName(QStringLiteral("toolPaletteCustomizeResetButton"));
  reset->setAutoDefault(false);
  QObject::connect(reset, &QPushButton::clicked, &dialog, [&] { populate(ToolPaletteDialogChoice{}); });
  footer->addWidget(reset);
  footer->addStretch(1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->setObjectName(QStringLiteral("toolPaletteCustomizeButtonBox"));
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  footer->addWidget(buttons);
  content->addSpacing(4);
  content->addLayout(footer);

  tabs->setCurrentIndex(tab == ToolPaletteCustomizeTab::LayerButtons ? 1 : 0);
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    if (preview_layer_icon_size) {
      preview_layer_icon_size(current.layer_icon_size);
    }
    return std::nullopt;
  }
  return read_widgets();
}

}  // namespace patchy::ui
