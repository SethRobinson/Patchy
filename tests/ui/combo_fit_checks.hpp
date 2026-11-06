#pragma once

// Checks that a combo box shows every entry whole (GitHub issues 489, 655): closed, in the room
// its style and style sheet leave for the label, and open, as the list's delegate and style draw
// each row at the list's real width (the menu-style rows of Fusion, the Linux style the UI suite
// runs under; the inset rows of the Windows list styles).

#include "ui/dialog_utils.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QImage>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QWidget>

#include <algorithm>
#include <string>
#include <vector>

namespace patchy::test::ui {

inline void settle_combo_layout() {
  for (int pass = 0; pass < 4; ++pass) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QApplication::processEvents();
  }
}

// `root` (a shown dialog) with `combo`'s open list painted where it sits, cropped around the
// box and the list. Not save_window_popup_artifact: Qt's list window keeps its unthemed frame.
inline QImage grab_with_open_combo_list(QWidget& root, QComboBox& combo, int margin_left = 160) {
  auto* popup = combo.view()->window();
  auto shot = root.grab().toImage();
  const auto origin = root.mapToGlobal(QPoint(0, 0));
  const auto popup_position = popup->mapToGlobal(QPoint(0, 0)) - origin;
  {
    QPainter painter(&shot);
    painter.drawPixmap(popup_position, popup->grab());
  }
  const auto crop = QRect(combo.mapToGlobal(QPoint(0, 0)) - origin, combo.size())
                        .united(QRect(popup_position, popup->size()))
                        .adjusted(-margin_left, -14, 48, 14)
                        .intersected(shot.rect());
  return shot.copy(crop);
}

// One line per entry of `combo` (shown) that does not show whole: closed at the box's current
// width and, when `open`, in its open list (opened and closed here). When `capture` is given it
// receives `root` with the list open.
inline std::vector<std::string> combo_entry_fit_problems(QComboBox& combo, const std::string& where, bool open = true,
                                                         QWidget* root = nullptr, QImage* capture = nullptr) {
  std::vector<std::string> problems;
  const int room = patchy::ui::combo_box_text_room(combo);
  for (int row = 0; row < combo.count(); ++row) {
    const int needs = patchy::ui::combo_box_entry_width(combo, row);
    if (needs > room) {
      problems.push_back(where + ": closed box shows '" + combo.itemText(row).toStdString() + "' in " +
                         std::to_string(room) + " px of the " + std::to_string(needs) + " it needs");
    }
  }
  if (!open) {
    return problems;
  }
  combo.showPopup();
  settle_combo_layout();
  auto* view = combo.view();
  auto* popup = view->window();
  if (!popup->isVisible()) {
    problems.push_back(where + ": the list did not open");
    return problems;
  }
  const int list = view->viewport()->width();
  for (int row = 0; row < combo.count(); ++row) {
    const auto text = combo.itemText(row);
    if (text.isEmpty()) {
      continue;  // a separator
    }
    const auto index = view->model()->index(row, combo.modelColumn(), view->rootIndex());
    const int text_width =
        std::max(combo.fontMetrics().horizontalAdvance(text), view->fontMetrics().horizontalAdvance(text));
    auto* delegate = view->itemDelegateForIndex(index);
    QStyleOptionViewItem option;
    option.initFrom(view);
    option.font = view->font();
    int needs = 0;
    int has = list;
    if (qobject_cast<QStyledItemDelegate*>(delegate) != nullptr) {
      // A list row: the text fits the style's text rectangle less its text margins.
      option.widget = view;
      option.text = text;
      option.features |= QStyleOptionViewItem::HasDisplay;
      option.rect = view->visualRect(index);
      needs = text_width;
      has = view->style()->subElementRect(QStyle::SE_ItemViewItemText, &option, view).width() -
            2 * (view->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, view) + 1);
    } else if (delegate != nullptr) {
      // A menu row (Fusion): fits when the size the style asks for the text's size fits.
      option.rect = QRect(0, 0, text_width, combo.fontMetrics().height());
      needs = delegate->sizeHint(option, index).width();
    }
    if (needs > has || text_width >= list) {
      problems.push_back(where + ": open list shows '" + text.toStdString() + "' in " + std::to_string(has) +
                         " px of the " + std::to_string(needs) + " it needs (list " + std::to_string(list) + ")");
    }
  }
  if (capture != nullptr && root != nullptr) {
    *capture = grab_with_open_combo_list(*root, combo);
  }
  combo.hidePopup();
  settle_combo_layout();
  return problems;
}

}  // namespace patchy::test::ui
