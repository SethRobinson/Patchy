#include "ui/document_order_list.hpp"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QObject>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <cstddef>
#include <numeric>

namespace patchy::ui {

namespace {

int compare_folded_chars(QChar lhs, QChar rhs) {
  const auto lhs_folded = lhs.toCaseFolded();
  const auto rhs_folded = rhs.toCaseFolded();
  if (lhs_folded.unicode() < rhs_folded.unicode()) {
    return -1;
  }
  if (lhs_folded.unicode() > rhs_folded.unicode()) {
    return 1;
  }
  return 0;
}

int compare_digit_runs(QStringView lhs, qsizetype lhs_begin, qsizetype lhs_end,
                       QStringView rhs, qsizetype rhs_begin, qsizetype rhs_end) {
  while (lhs_begin < lhs_end && lhs[lhs_begin] == QLatin1Char('0')) {
    ++lhs_begin;
  }
  while (rhs_begin < rhs_end && rhs[rhs_begin] == QLatin1Char('0')) {
    ++rhs_begin;
  }

  const auto lhs_significant_length = lhs_end - lhs_begin;
  const auto rhs_significant_length = rhs_end - rhs_begin;
  if (lhs_significant_length < rhs_significant_length) {
    return -1;
  }
  if (lhs_significant_length > rhs_significant_length) {
    return 1;
  }

  for (qsizetype offset = 0; offset < lhs_significant_length; ++offset) {
    if (lhs[lhs_begin + offset] < rhs[rhs_begin + offset]) {
      return -1;
    }
    if (lhs[lhs_begin + offset] > rhs[rhs_begin + offset]) {
      return 1;
    }
  }
  return 0;
}

}  // namespace

int natural_name_compare(QStringView lhs, QStringView rhs) {
  qsizetype lhs_index = 0;
  qsizetype rhs_index = 0;
  while (lhs_index < lhs.size() && rhs_index < rhs.size()) {
    const auto lhs_is_digit = lhs[lhs_index].isDigit();
    const auto rhs_is_digit = rhs[rhs_index].isDigit();
    if (lhs_is_digit && rhs_is_digit) {
      auto lhs_end = lhs_index;
      while (lhs_end < lhs.size() && lhs[lhs_end].isDigit()) {
        ++lhs_end;
      }
      auto rhs_end = rhs_index;
      while (rhs_end < rhs.size() && rhs[rhs_end].isDigit()) {
        ++rhs_end;
      }
      const auto compared = compare_digit_runs(lhs, lhs_index, lhs_end, rhs, rhs_index, rhs_end);
      if (compared != 0) {
        return compared;
      }
      lhs_index = lhs_end;
      rhs_index = rhs_end;
      continue;
    }

    const auto compared = compare_folded_chars(lhs[lhs_index], rhs[rhs_index]);
    if (compared != 0) {
      return compared;
    }
    ++lhs_index;
    ++rhs_index;
  }

  if (lhs_index < lhs.size()) {
    return 1;
  }
  if (rhs_index < rhs.size()) {
    return -1;
  }
  return 0;
}

QStringList natural_sorted_titles(QStringList titles) {
  std::stable_sort(titles.begin(), titles.end(),
                   [](const QString& a, const QString& b) { return natural_name_compare(a, b) < 0; });
  return titles;
}

namespace {

std::vector<int> selected_rows(const QListWidget& list) {
  std::vector<int> rows;
  for (int row = 0; row < list.count(); ++row) {
    if (list.item(row)->isSelected()) {
      rows.push_back(row);
    }
  }
  return rows;
}

// Takes the row out and puts it back at `target`, selected again: takeItem drops the
// row from the selection model, and the plain setCurrentItem overload would replace
// the whole selection with the one item, so the current item is restored with
// NoUpdate.
void relocate_list_item(QListWidget& list, int from, int target) {
  auto* current = list.currentItem();
  auto* item = list.takeItem(from);
  list.insertItem(target, item);
  item->setSelected(true);
  if (current != nullptr) {
    list.setCurrentItem(current, QItemSelectionModel::NoUpdate);
  }
}

// Re-inserts every item in `order` (indices into the current rows). takeItem and
// addItem keep each QListWidgetItem intact, so its user data travels with it; the
// selection and the current item are restored by identity.
void reorder_list_items(QListWidget& list, const std::vector<int>& order) {
  auto* current = list.currentItem();
  std::vector<std::pair<QListWidgetItem*, bool>> items;
  items.reserve(order.size());
  for (const int index : order) {
    auto* item = list.item(index);
    items.emplace_back(item, item->isSelected());
  }
  while (list.count() > 0) {
    list.takeItem(list.count() - 1);
  }
  for (const auto& [item, selected] : items) {
    list.addItem(item);
    item->setSelected(selected);
  }
  if (current != nullptr) {
    list.setCurrentItem(current, QItemSelectionModel::NoUpdate);
  }
}

}  // namespace

void move_selected_list_items(QListWidget& list, int delta) {
  const auto rows = selected_rows(list);
  if (rows.empty() || delta == 0) {
    return;
  }
  // Every selected row shifts one step; a run already against the edge stays put
  // (and so do the selected rows stacked directly behind it).
  if (delta < 0) {
    int pinned = 0;
    for (const int row : rows) {
      if (row == pinned) {
        ++pinned;
        continue;
      }
      relocate_list_item(list, row, row - 1);
    }
  } else {
    int pinned = list.count() - 1;
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
      if (*it == pinned) {
        --pinned;
        continue;
      }
      relocate_list_item(list, *it, *it + 1);
    }
  }
}

void sort_list_items_naturally(QListWidget& list) {
  std::vector<int> order(static_cast<std::size_t>(list.count()));
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&list](int a, int b) {
    return natural_name_compare(list.item(a)->text(), list.item(b)->text()) < 0;
  });
  reorder_list_items(list, order);
}

void reverse_list_items(QListWidget& list) {
  std::vector<int> order(static_cast<std::size_t>(list.count()));
  std::iota(order.rbegin(), order.rend(), 0);
  reorder_list_items(list, order);
}

std::vector<std::int64_t> selected_session_ids(const QListWidget& list) {
  std::vector<std::int64_t> ids;
  for (const int row : selected_rows(list)) {
    ids.push_back(static_cast<std::int64_t>(list.item(row)->data(Qt::UserRole).toLongLong()));
  }
  return ids;
}

void sync_document_order_controls(const DocumentOrderControls& controls, bool enabled) {
  const auto rows = selected_rows(*controls.list);
  const int count = controls.list->count();
  controls.list->setEnabled(enabled);
  controls.select_all->setEnabled(enabled && static_cast<int>(rows.size()) < count);
  controls.move_up->setEnabled(enabled && !rows.empty() && rows.front() > 0);
  controls.move_down->setEnabled(enabled && !rows.empty() && rows.back() + 1 < count);
  controls.auto_sort->setEnabled(enabled && count > 1);
  controls.reverse->setEnabled(enabled && count > 1);
}

DocumentOrderControls build_document_order_controls(QWidget* parent, const QString& object_prefix,
                                                    const std::vector<DocumentOrderEntry>& entries,
                                                    std::int64_t active_session_id) {
  DocumentOrderControls controls;
  controls.row = new QWidget(parent);
  auto* row_layout = new QHBoxLayout(controls.row);
  row_layout->setContentsMargins(0, 0, 0, 0);

  controls.list = new QListWidget(controls.row);
  controls.list->setObjectName(object_prefix + QStringLiteral("DocumentsList"));
  // Picked by selection, like the Import PDF page list: click, Shift-click and
  // Ctrl-click do the choosing, and everything starts selected.
  controls.list->setSelectionMode(QAbstractItemView::ExtendedSelection);
  for (const auto& entry : entries) {
    auto* item = new QListWidgetItem(entry.title, controls.list);
    item->setData(Qt::UserRole, QVariant::fromValue(static_cast<qlonglong>(entry.session_id)));
    if (entry.session_id == active_session_id) {
      controls.list->setCurrentItem(item, QItemSelectionModel::NoUpdate);
    }
  }
  controls.list->selectAll();
  row_layout->addWidget(controls.list, 1);

  auto* buttons = new QVBoxLayout();
  controls.select_all = new QPushButton(QObject::tr("Select All"), controls.row);
  controls.select_all->setObjectName(object_prefix + QStringLiteral("SelectAllButton"));
  controls.move_up = new QPushButton(QObject::tr("Move Up"), controls.row);
  controls.move_up->setObjectName(object_prefix + QStringLiteral("MoveUpButton"));
  controls.move_down = new QPushButton(QObject::tr("Move Down"), controls.row);
  controls.move_down->setObjectName(object_prefix + QStringLiteral("MoveDownButton"));
  controls.auto_sort = new QPushButton(QObject::tr("Auto Sort"), controls.row);
  controls.auto_sort->setObjectName(object_prefix + QStringLiteral("AutoSortButton"));
  controls.auto_sort->setToolTip(QObject::tr("Order the documents by name, numbering-aware (2 before 10)."));
  controls.reverse = new QPushButton(QObject::tr("Reverse"), controls.row);
  controls.reverse->setObjectName(object_prefix + QStringLiteral("ReverseButton"));
  controls.reverse->setToolTip(QObject::tr("Reverse the current order."));
  buttons->addWidget(controls.select_all);
  buttons->addSpacing(8);
  buttons->addWidget(controls.move_up);
  buttons->addWidget(controls.move_down);
  buttons->addSpacing(8);
  buttons->addWidget(controls.auto_sort);
  buttons->addWidget(controls.reverse);
  buttons->addStretch(1);
  row_layout->addLayout(buttons);

  auto* list = controls.list;
  const auto resync = [controls] { sync_document_order_controls(controls, controls.list->isEnabled()); };
  QObject::connect(controls.select_all, &QPushButton::clicked, controls.row, [list, resync] {
    list->selectAll();
    resync();
  });
  QObject::connect(controls.move_up, &QPushButton::clicked, controls.row, [list, resync] {
    move_selected_list_items(*list, -1);
    resync();
  });
  QObject::connect(controls.move_down, &QPushButton::clicked, controls.row, [list, resync] {
    move_selected_list_items(*list, +1);
    resync();
  });
  QObject::connect(controls.auto_sort, &QPushButton::clicked, controls.row, [list, resync] {
    sort_list_items_naturally(*list);
    resync();
  });
  QObject::connect(controls.reverse, &QPushButton::clicked, controls.row, [list, resync] {
    reverse_list_items(*list);
    resync();
  });
  QObject::connect(list, &QListWidget::currentRowChanged, controls.row, [resync](int) { resync(); });
  QObject::connect(list, &QListWidget::itemSelectionChanged, controls.row, resync);
  sync_document_order_controls(controls, true);
  return controls;
}

}  // namespace patchy::ui
