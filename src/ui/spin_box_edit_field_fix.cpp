#include "ui/spin_box_edit_field_fix.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QEvent>
#include <QLineEdit>
#include <QRect>
#include <QStyle>
#include <QStyleOptionSpinBox>

namespace patchy::ui {
namespace {

QStyleOptionSpinBox edit_field_option(const QAbstractSpinBox& spin) {
  QStyleOptionSpinBox option;
  option.initFrom(&spin);
  option.rect = spin.rect();
  option.subControls = QStyle::SC_SpinBoxFrame | QStyle::SC_SpinBoxEditField;
  option.frame = spin.hasFrame();
  option.buttonSymbols = spin.buttonSymbols();
  return option;
}

class SpinBoxEditFieldFilter : public QObject {
 public:
  explicit SpinBoxEditFieldFilter(QObject* parent) : QObject(parent) {}

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    // QAbstractSpinBox places its line edit on every resize and style change; the editor's own
    // move or resize follows, so correcting it there catches each placement.
    if (event->type() != QEvent::Resize && event->type() != QEvent::Move) {
      return false;
    }
    auto* editor = qobject_cast<QLineEdit*>(watched);
    auto* spin = editor != nullptr ? qobject_cast<QAbstractSpinBox*>(editor->parentWidget()) : nullptr;
    if (spin == nullptr) {
      return false;
    }
    const auto field = corrected_spin_box_edit_field(*spin);
    if (!field.isEmpty() && editor->geometry() != field) {
      editor->setGeometry(field);
    }
    return false;
  }
};

}  // namespace

QRect corrected_spin_box_edit_field(const QAbstractSpinBox& spin) {
  const auto option = edit_field_option(spin);
  const auto* style = spin.style();
  const auto field = style->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxEditField, &spin);
  if (option.buttonSymbols != QAbstractSpinBox::NoButtons || option.direction != Qt::LeftToRight ||
      field.left() != 0 || field.width() > 1 || option.rect.width() <= 2) {
    return {};
  }
  // The broken field keeps its top and bottom. Its right edge survives a right-to-left query,
  // which cuts nothing, and the style sheet adds both side insets to the base style's size.
  auto mirrored = option;
  mirrored.direction = Qt::RightToLeft;
  const int right_inset =
      option.rect.right() -
      style->subControlRect(QStyle::CC_SpinBox, &mirrored, QStyle::SC_SpinBoxEditField, &spin).right();
  const int both_insets = style->sizeFromContents(QStyle::CT_SpinBox, &option, QSize(0, 0), &spin).width() -
                          QApplication::style()->sizeFromContents(QStyle::CT_SpinBox, &option, QSize(0, 0), nullptr).width();
  int left_inset = both_insets - right_inset;
  if (right_inset < 0 || right_inset >= option.rect.width()) {
    return {};
  }
  if (left_inset < 0 || left_inset + right_inset >= option.rect.width()) {
    left_inset = right_inset;  // a base style that drew its own frame: assume even sides
  }
  const int width = option.rect.width() - left_inset - right_inset;
  return width > 1 ? QRect(left_inset, field.top(), width, field.height()) : QRect();
}

void install_spin_box_edit_field_fix() {
  if (auto* app = qApp; app != nullptr) {
    app->installEventFilter(new SpinBoxEditFieldFilter(app));
  }
}

}  // namespace patchy::ui
