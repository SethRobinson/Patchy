#include "ui/tool_palette_customization.hpp"

#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/theme_palette.hpp"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
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
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace patchy::ui {
namespace {

// Persisted identifiers: never rename them (see AGENTS.md).
QString hidden_tools_key() { return QStringLiteral("toolPalette/hiddenTools"); }
QString layout_key() { return QStringLiteral("toolPalette/layout"); }
QString wheel_scroll_key() { return QStringLiteral("toolPalette/wheelScroll"); }
// toolPalette/showCustomizeButton belonged to the palette's gear, since removed: never reuse it.
QString hidden_layer_buttons_key() { return QStringLiteral("layersPanel/hiddenFooterButtons"); }
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
// with the cluster separators, and the color well. It repaints from the choice the
// dialog's controls hold, so it follows every tick.
class ToolPalettePreview final : public QWidget {
public:
  explicit ToolPalettePreview(const ToolPaletteCatalog& catalog, QWidget* parent)
      : QWidget(parent), catalog_(catalog) {
    setObjectName(QStringLiteral("toolPaletteCustomizePreview"));
  }

  void set_customization(const ToolPaletteCustomization& value) {
    customization_ = value;
    setFixedSize(content_size());
    update();
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    QWidget::paintEvent(event);
    const auto& colors = theme();
    QPainter painter(this);
    painter.fillRect(rect(), colors.tool_palette_bg);
    painter.setPen(colors.tool_palette_border);
    painter.drawLine(width() - 1, 0, width() - 1, height() - 1);
    const int columns = customization_.two_columns ? 2 : 1;
    int y = kMargin;
    bool first = true;
    for (const auto& cluster : catalog_.clusters) {
      const auto icons = visible_icons(cluster);
      if (icons.empty()) {
        continue;
      }
      if (!first) {
        painter.fillRect(QRect(kMargin, y + kSeparatorMargin, width() - 1 - 2 * kMargin, 1),
                         colors.tool_palette_separator);
        y += kSeparatorExtent + kSpacing;
      }
      first = false;
      for (std::size_t index = 0; index < icons.size(); ++index) {
        const int column = static_cast<int>(index) % columns;
        if (index > 0 && column == 0) {
          y += kCellHeight + kSpacing;
        }
        const QRect cell(kMargin + column * (kCellWidth + kSpacing), y, kCellWidth, kCellHeight);
        icons[index].paint(&painter, cell.adjusted((kCellWidth - kIcon) / 2, (kCellHeight - kIcon) / 2,
                                                   -(kCellWidth - kIcon) / 2, -(kCellHeight - kIcon) / 2));
      }
      y += kCellHeight + kSpacing;
    }
    // The color well under the tools: background swatch lower right, foreground over it.
    y += kFooterGap;
    painter.setPen(colors.tool_palette_border);
    const QRect background(kMargin + 9, y + 7, kSwatch, kSwatch);
    const QRect foreground(kMargin + 2, y, kSwatch, kSwatch);
    painter.fillRect(background, catalog_.background);
    painter.drawRect(background.adjusted(0, 0, -1, -1));
    painter.fillRect(foreground, catalog_.foreground);
    painter.drawRect(foreground.adjusted(0, 0, -1, -1));
  }

private:
  static constexpr int kMargin = 3;
  static constexpr int kCellWidth = 24;
  static constexpr int kCellHeight = 19;
  static constexpr int kIcon = 15;
  static constexpr int kSpacing = 1;
  static constexpr int kSeparatorMargin = 2;
  static constexpr int kSeparatorExtent = 2 * kSeparatorMargin + 1;
  static constexpr int kSwatch = 14;
  static constexpr int kFooterGap = 6;
  static constexpr int kFooterHeight = kSwatch + 7 + 4;

  [[nodiscard]] std::vector<QIcon> visible_icons(const ToolPaletteCatalogCluster& cluster) const {
    std::vector<QIcon> icons;
    for (const auto& entry : cluster.entries) {
      for (const auto& member : entry.members) {
        if (!customization_.hidden_tools.contains(member.id)) {
          icons.push_back(member.icon);
          break;
        }
      }
    }
    return icons;
  }

  [[nodiscard]] QSize content_size() const {
    const int columns = customization_.two_columns ? 2 : 1;
    int height = kMargin;
    bool first = true;
    for (const auto& cluster : catalog_.clusters) {
      const auto count = static_cast<int>(visible_icons(cluster).size());
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
    height += kFooterGap + kFooterHeight + kMargin;
    const int width = 2 * kMargin + columns * kCellWidth + (columns - 1) * kSpacing + 1;
    return {width, height};
  }

  const ToolPaletteCatalog& catalog_;
  ToolPaletteCustomization customization_;
};

}  // namespace

ToolPaletteCustomization normalized_tool_palette_customization(ToolPaletteCustomization value) {
  value.hidden_tools = normalized_ids(std::move(value.hidden_tools));
  value.hidden_layer_buttons = normalized_ids(std::move(value.hidden_layer_buttons));
  return value;
}

ToolPaletteCustomization stored_tool_palette_customization() {
  const auto settings = app_settings();
  ToolPaletteCustomization value;
  value.hidden_tools = settings.value(hidden_tools_key()).toStringList();
  value.two_columns = settings.value(layout_key()).toString() == QLatin1String(kTwoColumnsToken);
  value.wheel_scroll = settings.value(wheel_scroll_key(), true).toBool();
  value.hidden_layer_buttons = settings.value(hidden_layer_buttons_key()).toStringList();
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
  store_list(hidden_layer_buttons_key(), normalized.hidden_layer_buttons);
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
}

std::optional<ToolPaletteCustomization> request_tool_palette_customization(QWidget* parent,
                                                                            const ToolPaletteCatalog& catalog,
                                                                            const ToolPaletteCustomization& current,
                                                                            ToolPaletteCustomizeTab tab) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("toolPaletteCustomizeDialog"));
  auto* root = new QVBoxLayout(&dialog);
  auto* content = install_dark_dialog_chrome(dialog, root, QObject::tr("Customize Tool Palette"));

  auto* tabs = new QTabWidget(&dialog);
  tabs->setObjectName(QStringLiteral("toolPaletteCustomizeTabs"));
  content->addWidget(tabs, 1);

  // Tools tab: the slot list on the left, layout options and the preview on the right.
  auto* tools_page = new QWidget(tabs);
  auto* tools_layout = new QHBoxLayout(tools_page);
  auto* list_column = new QVBoxLayout();
  auto* tools_hint = new QLabel(QObject::tr("Tick the tools to show. Hidden tools keep their shortcuts."), tools_page);
  tools_hint->setObjectName(QStringLiteral("toolPaletteCustomizeHint"));
  tools_hint->setWordWrap(true);
  list_column->addWidget(tools_hint);
  auto* tree = new QTreeWidget(tools_page);
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
  auto* wheel_scroll = new QCheckBox(QObject::tr("Mouse wheel scrolls the palette when it overflows"), tools_page);
  wheel_scroll->setObjectName(QStringLiteral("toolPaletteWheelScrollCheck"));
  side->addWidget(wheel_scroll);
  auto* wheel_hint = new QLabel(QObject::tr("Otherwise the > arrow opens more columns."), tools_page);
  wheel_hint->setObjectName(QStringLiteral("toolPaletteWheelScrollHint"));
  wheel_hint->setIndent(22);
  side->addWidget(wheel_hint);
  side->addSpacing(10);
  auto* preview_label = new QLabel(QObject::tr("Preview"), tools_page);
  preview_label->setFont(heading_font);
  side->addWidget(preview_label);
  auto* preview = new ToolPalettePreview(catalog, tools_page);
  side->addWidget(preview, 0, Qt::AlignLeft);
  side->addStretch(1);
  tools_layout->addLayout(side);
  tabs->addTab(tools_page, QObject::tr("Tools"));

  // Layers buttons tab: one tickable row per footer button.
  auto* layers_page = new QWidget(tabs);
  auto* layers_layout = new QVBoxLayout(layers_page);
  auto* layers_hint = new QLabel(
      QObject::tr("Tick the buttons the Layers panel shows. Hidden buttons' commands stay in the menus."), layers_page);
  layers_hint->setWordWrap(true);
  layers_layout->addWidget(layers_hint);
  auto* layer_list = new QListWidget(layers_page);
  layer_list->setObjectName(QStringLiteral("layerFooterButtonsList"));
  layer_list->setIconSize(QSize(20, 20));
  layer_list->setSelectionMode(QAbstractItemView::NoSelection);
  for (const auto& button : catalog.layer_buttons) {
    auto* item = new QListWidgetItem(button.icon, button.name, layer_list);
    item->setData(kIdRole, button.id);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
  }
  layers_layout->addWidget(layer_list, 1);
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
      row->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
      row->setForeground(1, theme().text_secondary);
      if (entry.flyout) {
        row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        for (const auto& member : entry.members) {
          auto* child = new QTreeWidgetItem(row);
          child->setText(0, member.name);
          child->setIcon(0, member.icon);
          child->setText(1, member.shortcut);
          child->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
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

  bool populating = false;
  const auto read_widgets = [&] {
    ToolPaletteCustomization value;
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
    // Ids the catalog does not list (a tool this build leaves out) stay as they were.
    for (const auto& id : current.hidden_tools) {
      bool listed = false;
      for (const auto& row : slot_rows) {
        for (const auto& member : row.entry->members) {
          listed = listed || member.id == id;
        }
      }
      if (!listed) {
        value.hidden_tools.push_back(id);
      }
    }
    value.two_columns = two_columns->isChecked();
    value.wheel_scroll = wheel_scroll->isChecked();
    for (int index = 0; index < layer_list->count(); ++index) {
      auto* item = layer_list->item(index);
      if (item->checkState() != Qt::Checked) {
        value.hidden_layer_buttons.push_back(item->data(kIdRole).toString());
      }
    }
    // As with tools, a button id this build does not know stays as it was.
    for (const auto& id : current.hidden_layer_buttons) {
      bool listed = false;
      for (const auto& button : catalog.layer_buttons) {
        listed = listed || button.id == id;
      }
      if (!listed) {
        value.hidden_layer_buttons.push_back(id);
      }
    }
    return normalized_tool_palette_customization(std::move(value));
  };
  const auto refresh_preview = [&] {
    if (!populating) {
      preview->set_customization(read_widgets());
    }
  };
  const auto populate = [&](const ToolPaletteCustomization& value) {
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
    for (int index = 0; index < layer_list->count(); ++index) {
      auto* item = layer_list->item(index);
      item->setCheckState(value.hidden_layer_buttons.contains(item->data(kIdRole).toString()) ? Qt::Unchecked
                                                                                            : Qt::Checked);
    }
    populating = false;
    refresh_preview();
  };
  populate(current);
  tree->resizeColumnToContents(1);

  QObject::connect(tree, &QTreeWidget::itemChanged, &dialog, [&](QTreeWidgetItem*, int) { refresh_preview(); });
  QObject::connect(layer_list, &QListWidget::itemChanged, &dialog, [&](QListWidgetItem*) { refresh_preview(); });
  for (auto* box : {static_cast<QAbstractButton*>(two_columns), static_cast<QAbstractButton*>(wheel_scroll)}) {
    QObject::connect(box, &QAbstractButton::toggled, &dialog, [&](bool) { refresh_preview(); });
  }

  // Reset to Default on the left, OK and Cancel on the right.
  auto* footer = new QHBoxLayout();
  auto* reset = new QPushButton(QObject::tr("Reset to Default"), &dialog);
  reset->setObjectName(QStringLiteral("toolPaletteCustomizeResetButton"));
  reset->setAutoDefault(false);
  QObject::connect(reset, &QPushButton::clicked, &dialog, [&] { populate(ToolPaletteCustomization{}); });
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
    return std::nullopt;
  }
  return read_widgets();
}

}  // namespace patchy::ui
