#include "ui/gradient_preset_popup.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/gradient_library.hpp"

#include <QFrame>
#include <QHash>
#include <QIcon>
#include <QObject>
#include <QPushButton>
#include <QSize>
#include <QString>
#include <QStyle>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace patchy::ui {

void show_gradient_preset_popup(
    QPushButton *anchor, GradientLibrary &library,
    std::function<void(const GradientLibraryEntry &)> use_gradient,
    std::function<void()> manage_gradients) {
  auto *popup = new QFrame(anchor, Qt::Popup);
  popup->setAttribute(Qt::WA_DeleteOnClose);
  popup->setObjectName(anchor->objectName() + QStringLiteral("Popup"));
  popup->setFrameShape(QFrame::StyledPanel);
  auto *layout = new QVBoxLayout(popup);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(4);
  auto *tree = new QTreeWidget(popup);
  tree->setObjectName(anchor->objectName() + QStringLiteral("Tree"));
  tree->setHeaderHidden(true);
  tree->setRootIsDecorated(true);
  tree->setIconSize(QSize(112, 28));
  QHash<QString, QTreeWidgetItem *> folders;
  for (const auto &entry : library.entries()) {
    QTreeWidgetItem *parent = nullptr;
    QString path;
    for (const auto &part :
         entry.folder.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
      if (!path.isEmpty())
        path += QLatin1Char('/');
      path += part;
      auto *folder = folders.value(path, nullptr);
      if (folder == nullptr) {
        folder = parent != nullptr
                     ? new QTreeWidgetItem(parent,
                                           {gradient_folder_display_name(part)})
                     : new QTreeWidgetItem(
                           tree, {gradient_folder_display_name(part)});
        folder->setIcon(0, anchor->style()->standardIcon(QStyle::SP_DirIcon));
        folders.insert(path, folder);
      }
      parent = folder;
    }
    auto *item = parent != nullptr
                     ? new QTreeWidgetItem(
                           parent, {gradient_library_entry_display_name(entry)})
                     : new QTreeWidgetItem(
                           tree, {gradient_library_entry_display_name(entry)});
    item->setIcon(0, QIcon(entry.thumbnail));
    item->setData(0, Qt::UserRole, entry.storage_id);
  }
  tree->expandAll();
  layout->addWidget(tree, 1);
  auto *manage = new QPushButton(QObject::tr("Manage Gradients..."), popup);
  manage->setObjectName(anchor->objectName() + QStringLiteral("ManageButton"));
  layout->addWidget(manage);
  const auto select = [popup, &library, use_gradient](QTreeWidgetItem *item) {
    const auto id = item->data(0, Qt::UserRole).toString();
    if (id.isEmpty()) {
      item->setExpanded(!item->isExpanded());
      return;
    }
    if (const auto *entry = library.find_entry(id); entry != nullptr)
      use_gradient(*entry);
    popup->close();
  };
  QObject::connect(tree, &QTreeWidget::itemClicked, popup,
                   [select](QTreeWidgetItem *item, int) { select(item); });
  QObject::connect(tree, &QTreeWidget::itemActivated, popup,
                   [select](QTreeWidgetItem *item, int) { select(item); });
  QObject::connect(manage, &QPushButton::clicked, popup,
                   [popup, manage_gradients] {
                     popup->close();
                     manage_gradients();
                   });
  popup->resize(std::max(anchor->width(), 380), 440);
  // Clamped/flipped like the pattern and brush-tip popups; the old plain move
  // could run this popup off the bottom or right screen edge.
  position_popup_below(*anchor, *popup);
  popup->show();
  tree->setFocus(Qt::PopupFocusReason);
}

} // namespace patchy::ui
