#include "ui/custom_shape_picker.hpp"

#include "ui/app_settings.hpp"
#include "ui/custom_shape_library.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/preset_tree_widget.hpp"
#include "ui/theme_qss.hpp"

#include <QFrame>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// Down or Enter in the search field reach the tree: the field keeps the focus for typing.
class PickerFilterKeys final : public QObject {
public:
  PickerFilterKeys(QTreeWidget* tree, std::function<void()> pick_first, QObject* parent)
      : QObject(parent), tree_(tree), pick_first_(std::move(pick_first)) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::KeyPress) {
      const auto key = static_cast<QKeyEvent*>(event)->key();
      if (key == Qt::Key_Down) {
        tree_->setFocus(Qt::TabFocusReason);
        if (tree_->currentItem() == nullptr || tree_->currentItem()->isHidden()) {
          for (QTreeWidgetItemIterator it(tree_, QTreeWidgetItemIterator::NotHidden); *it != nullptr; ++it) {
            tree_->setCurrentItem(*it);
            break;
          }
        }
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
  QTreeWidget* tree_;
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

  auto* filter = new QLineEdit(popup);
  filter->setObjectName(QStringLiteral("customShapeComboFilter"));
  filter->setPlaceholderText(QObject::tr("Search shapes"));
  filter->setClearButtonEnabled(true);
  layout->addWidget(filter);

  auto* tree = new PresetTreeWidget(popup);
  tree->setObjectName(QStringLiteral("customShapeComboTree"));
  tree->setSelectionMode(QAbstractItemView::SingleSelection);
  // Enter or a double-click on a folder opens or closes it once (itemActivated below).
  tree->setExpandsOnDoubleClick(false);
  tree->setIconSize(QSize(32, 32));
  tree->set_entry_row_height(38);
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
  connect(tree, &QTreeWidget::itemActivated, popup, [pick](QTreeWidgetItem* item, int) {
    if (const auto id = PresetTreeWidget::item_entry_id(item); !id.isEmpty()) {
      pick(id);
    } else if (item != nullptr) {
      item->setExpanded(!item->isExpanded());
    }
  });
  connect(tree, &QTreeWidget::itemExpanded, popup, [save_collapsed](QTreeWidgetItem*) { save_collapsed(); });
  connect(tree, &QTreeWidget::itemCollapsed, popup, [save_collapsed](QTreeWidgetItem*) { save_collapsed(); });
  connect(filter, &QLineEdit::textChanged, popup, [tree](const QString& text) { tree->set_filter_text(text); });
  filter->installEventFilter(new PickerFilterKeys(
      tree,
      [tree, pick] {
        const auto current = PresetTreeWidget::item_entry_id(tree->currentItem());
        const bool current_visible = !current.isEmpty() && !tree->currentItem()->isHidden() &&
                                     (tree->currentItem()->parent() == nullptr || !tree->currentItem()->parent()->isHidden());
        pick(current_visible ? current : tree->first_visible_entry_id());
      },
      filter));
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
