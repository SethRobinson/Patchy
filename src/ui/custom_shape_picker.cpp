#include "ui/custom_shape_picker.hpp"

#include "ui/app_settings.hpp"
#include "ui/custom_shape_library.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/preset_grid_widget.hpp"
#include "ui/preset_tree_widget.hpp"
#include "ui/theme_qss.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// Down or Enter in the search field reach the list or grid: the field keeps the focus for typing.
class PickerFilterKeys final : public QObject {
public:
  PickerFilterKeys(PresetBrowserView* browser, std::function<void()> pick_first, QObject* parent)
      : QObject(parent), browser_(browser), pick_first_(std::move(pick_first)) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::KeyPress) {
      const auto key = static_cast<QKeyEvent*>(event)->key();
      if (key == Qt::Key_Down) {
        browser_->focus_first_visible();
        return true;
      }
      if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        pick_first_();
        return true;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  PresetBrowserView* browser_;
  std::function<void()> pick_first_;
};

}  // namespace

CustomShapePickerCombo::CustomShapePickerCombo(std::function<const CustomShapeLibrary*()> library,
                                               std::function<void()> manage, QWidget* parent)
    : QComboBox(parent), library_(std::move(library)), manage_(std::move(manage)) {}

void CustomShapePickerCombo::hidePopup() {
  if (popup_ != nullptr) {
    popup_->close();
  }
  QComboBox::hidePopup();
}

void CustomShapePickerCombo::showPopup() {
  if (popup_ != nullptr) {
    popup_->close();
    return;
  }
  if (dismissed_.isValid() && dismissed_.elapsed() < 250) {
    return;
  }
  const auto* library = library_ ? library_() : nullptr;
  if (library == nullptr) {
    QComboBox::showPopup();
    return;
  }
  auto* popup = new QFrame(this, Qt::Popup);
  popup->setAttribute(Qt::WA_DeleteOnClose);
  popup->setObjectName(QStringLiteral("customShapeComboPopup"));
  popup->setFrameShape(QFrame::StyledPanel);
  // A top-level popup gets no themed background from the app sheet (like the font picker's).
  set_themed_style(*popup, QStringLiteral(R"(
    QFrame#customShapeComboPopup {
      background: @field_bg_large;
      border: 1px solid @field_inset_border;
    }
  )"));
  popup_ = popup;
  connect(popup, &QObject::destroyed, this, [this] { dismissed_.start(); });
  auto* layout = new QVBoxLayout(popup);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(4);

  auto* top_row = new QHBoxLayout();
  top_row->setSpacing(2);
  auto* filter = new QLineEdit(popup);
  filter->setObjectName(QStringLiteral("customShapeComboFilter"));
  filter->setPlaceholderText(QObject::tr("Search shapes"));
  filter->setClearButtonEnabled(true);
  top_row->addWidget(filter, 1);
  auto* list_view_button = new QToolButton(popup);
  list_view_button->setObjectName(QStringLiteral("customShapeComboListViewButton"));
  list_view_button->setText(QObject::tr("List"));
  list_view_button->setToolTip(QObject::tr("Show the shapes as a list with their names"));
  auto* grid_view_button = new QToolButton(popup);
  grid_view_button->setObjectName(QStringLiteral("customShapeComboGridViewButton"));
  grid_view_button->setText(QObject::tr("Grid"));
  grid_view_button->setToolTip(QObject::tr("Show the shapes as a grid of thumbnails"));
  top_row->addWidget(list_view_button);
  top_row->addWidget(grid_view_button);
  layout->addLayout(top_row);

  // List (a row per shape with its name) or Grid (thumbnails, the name in the tooltip;
  // GitHub issue 486 follow-up), remembered under kCustomShapePickerGridViewKey.
  auto* tree = new PresetBrowserView(popup);
  tree->tree()->setObjectName(QStringLiteral("customShapeComboTree"));
  tree->tree()->setSelectionMode(QAbstractItemView::SingleSelection);
  // Enter or a double-click on a folder opens or closes it once (itemActivated below).
  tree->tree()->setExpandsOnDoubleClick(false);
  tree->tree()->setIconSize(QSize(32, 32));
  tree->tree()->set_entry_row_height(38);
  tree->grid()->setObjectName(QStringLiteral("customShapeComboGrid"));
  tree->grid()->setSelectionMode(QAbstractItemView::SingleSelection);
  tree->grid()->setIconSize(QSize(32, 32));
  tree->grid()->set_cell_extent(40);
  // A click on a header opens or closes it: choosing a folder means nothing here.
  tree->grid()->set_folder_click_toggles(true);
  tree->set_folder_label_callback([](const QString& folder, int count) {
    return QObject::tr("%1 (%2)").arg(custom_shape_folder_display_name(folder)).arg(count);
  });
  tree->set_entries_callback([library] {
    std::vector<PresetTreeEntry> rows;
    for (const auto& entry : library->entries()) {
      const auto name = custom_shape_display_name(entry);
      rows.push_back({entry.storage_id, name, QIcon(entry.thumbnail), name, entry.folder});
    }
    return rows;
  });
  tree->set_collapsed_folders(app_settings().value(QLatin1String(kCustomShapePickerCollapsedFoldersKey)).toStringList());
  tree->set_grid_view(app_settings().value(QLatin1String(kCustomShapePickerGridViewKey), true).toBool());
  tree->connect_view_buttons(list_view_button, grid_view_button, [](bool grid) {
    app_settings().setValue(QLatin1String(kCustomShapePickerGridViewKey), grid);
  });
  QString current_storage_id;
  if (const auto* entry = library->find_entry_by_shape_id(currentData().toString()); entry != nullptr) {
    current_storage_id = entry->storage_id;
  }
  tree->reload(current_storage_id);
  layout->addWidget(tree, 1);

  auto* manage = new QPushButton(QObject::tr("Shapes Manager..."), popup);
  manage->setObjectName(QStringLiteral("customShapeComboManageButton"));
  manage->setAutoDefault(false);
  layout->addWidget(manage);

  const auto pick = [this, popup, library](const QString& storage_id) {
    const auto* entry = library->find_entry(storage_id);
    if (entry == nullptr) {
      return;
    }
    if (const auto index = findData(entry->id); index >= 0) {
      setCurrentIndex(index);  // currentIndexChanged applies and saves it
    }
    popup->close();
  };
  const auto save_collapsed = [tree, filter] {
    if (filter->text().trimmed().isEmpty()) {
      app_settings().setValue(QLatin1String(kCustomShapePickerCollapsedFoldersKey), tree->collapsed_folders());
    }
  };
  tree->set_entry_clicked_callback(pick);
  connect(tree->tree(), &QTreeWidget::itemActivated, popup, [pick](QTreeWidgetItem* item, int) {
    if (const auto id = PresetTreeWidget::item_entry_id(item); !id.isEmpty()) {
      pick(id);
    } else if (item != nullptr) {
      item->setExpanded(!item->isExpanded());
    }
  });
  // Grid headers open and close inside the grid itself (a click, Enter).
  connect(tree->grid(), &QListWidget::itemActivated, popup, [pick](QListWidgetItem* item) {
    if (const auto id = PresetGridWidget::item_entry_id(item); !id.isEmpty()) {
      pick(id);
    }
  });
  tree->set_folder_toggled_callback(save_collapsed);
  connect(filter, &QLineEdit::textChanged, popup, [tree](const QString& text) { tree->set_filter_text(text); });
  filter->installEventFilter(new PickerFilterKeys(
      tree, [tree, pick] { pick(tree->current_or_first_visible_entry_id()); }, filter));
  connect(manage, &QPushButton::clicked, popup, [this, popup] {
    popup->close();
    if (manage_) {
      // After the popup's grab is gone: the manager is modal.
      QTimer::singleShot(0, this, [this] {
        if (manage_) {
          manage_();
        }
      });
    }
  });

  popup->resize(std::max(width(), 300), 440);
  position_popup_below(*this, *popup);
  popup->show();
  filter->setFocus(Qt::PopupFocusReason);
}

}  // namespace patchy::ui
