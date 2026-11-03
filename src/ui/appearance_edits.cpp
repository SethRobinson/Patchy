#include "ui/appearance_edits.hpp"

#include "ui/theme_qss.hpp"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QWidget>

namespace patchy::ui {
namespace {
class MixedBadge final : public QLabel {
public:
  explicit MixedBadge(QWidget* owner) : QLabel(QStringLiteral("\u2260"), owner), owner_(owner) {
    setObjectName(QStringLiteral("appearanceMixedBadge"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAlignment(Qt::AlignCenter);
    setFixedSize(12, 12);
    set_themed_style(*this, QStringLiteral("background: @window_bg; color: @text_primary;"));
    owner->installEventFilter(this);
    reposition();
  }
protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == owner_ && event->type() == QEvent::Resize) reposition();
    return QLabel::eventFilter(watched, event);
  }
private:
  void reposition() { move(std::max(0, owner_->width() - width()), 0); raise(); }
  QWidget* owner_;
};
}

void install_appearance_edit_intent(QWidget* widget, std::function<void()> committed) {
  if (auto* spin = qobject_cast<QAbstractSpinBox*>(widget)) {
    if (auto* edit = spin->findChild<QLineEdit*>()) {
      QObject::connect(edit, &QLineEdit::textEdited, spin, [spin] {
        spin->setProperty("appearanceUserTyped", true);
      });
      QObject::connect(spin, &QAbstractSpinBox::editingFinished, spin, [spin, committed] {
        if (spin->property("appearanceUserTyped").toBool()) {
          spin->setProperty("appearanceUserTyped", false);
          committed();
        }
      });
    }
  } else if (auto* combo = qobject_cast<QComboBox*>(widget)) {
    QObject::connect(combo, &QComboBox::activated, combo, [committed](int) { committed(); });
  } else if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
    QObject::connect(button, &QAbstractButton::clicked, button, [committed](bool) { committed(); });
  }
}

void set_appearance_mixed(QWidget* widget, bool mixed) {
  if (!widget) return;
  // The Layer Style dialog refreshes every bound field on each value change; an
  // unchanged state must cost nothing (the label search below walks the dialog,
  // about 5 ms a slider move, GitHub #601).
  if (const auto current = widget->property("appearanceMixed"); current.isValid() && current.toBool() == mixed) return;
  if (!widget->property("appearanceOriginalTooltip").isValid())
    widget->setProperty("appearanceOriginalTooltip", widget->toolTip());
  const auto explanation = QObject::tr("Mixed: selected layers have different values");
  widget->setProperty("appearanceMixed", mixed);
  widget->setAccessibleDescription(mixed ? explanation : QString());
  const auto original = widget->property("appearanceOriginalTooltip").toString();
  widget->setToolTip(mixed ? original + (original.isEmpty() ? QString() : QStringLiteral("\n")) + explanation
                           : original);
  // Prefer the field's existing form label, leaving its numeric editing area
  // and steppers untouched. Compact toolbar swatches use a small badge.
  QWidget* field = widget;
  for (int level = 0; field && field->parentWidget() && level < 5; ++level) {
    auto* parent = field->parentWidget();
    for (auto* form : parent->findChildren<QFormLayout*>()) {
      if (auto* label = qobject_cast<QLabel*>(form->labelForField(field))) {
        if (!label->property("appearanceOriginalText").isValid())
          label->setProperty("appearanceOriginalText", label->text());
        label->setText(label->property("appearanceOriginalText").toString() +
                       (mixed ? QObject::tr(" (Mixed)") : QString()));
        return;
      }
    }
    field = parent;
  }
  auto* badge = widget->findChild<QLabel*>(QStringLiteral("appearanceMixedBadge"), Qt::FindDirectChildrenOnly);
  if (!badge && mixed) badge = new MixedBadge(widget);
  if (badge) badge->setVisible(mixed);
}

QString appearance_selection_summary(std::size_t selected, std::size_t editable,
                                     const QString& reference, const QString& skipped) {
  auto result = QObject::tr("%n layers selected", nullptr, static_cast<int>(selected));
  result += QStringLiteral("\n") + QObject::tr("Values from: %1").arg(reference);
  if (editable != selected) {
    result += QStringLiteral("\n") + QObject::tr("Editable layers: %n", nullptr, static_cast<int>(editable));
    if (!skipped.isEmpty()) result += QStringLiteral(". ") + skipped;
  }
  return result;
}
}  // namespace patchy::ui
