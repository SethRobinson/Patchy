#include "ui/selection_modify_dialogs.hpp"

#include "core/selection_modify.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/measurement_units.hpp"

#include <QAbstractSpinBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QObject>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>

#include <algorithm>

namespace patchy::ui {
namespace {

// Shift+Up/Down steps `factor` single steps (Ctrl does the same through Qt's own step modifier).
class ShiftStepFilter final : public QObject {
public:
  ShiftStepFilter(QAbstractSpinBox* spin, int factor) : QObject(spin), spin_(spin), factor_(factor) {}

  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::KeyPress) {
      const auto* key = static_cast<QKeyEvent*>(event);
      if ((key->modifiers() & Qt::ShiftModifier) != 0 && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)) {
        spin_->stepBy(key->key() == Qt::Key_Up ? factor_ : -factor_);
        spin_->selectAll();
        return true;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QAbstractSpinBox* spin_;
  int factor_;
};

void install_shift_step(QAbstractSpinBox* spin, int factor) {
  auto* filter = new ShiftStepFilter(spin, factor);
  spin->installEventFilter(filter);
  if (auto* edit = spin->findChild<QLineEdit*>(); edit != nullptr) {
    edit->installEventFilter(filter);
  }
}

bool run_radius_dialog(const QString& object_name, const QString& title, const QString& label,
                       QAbstractSpinBox* spin, QDialog& dialog) {
  dialog.setObjectName(object_name);
  dialog.setWindowTitle(title);
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  form->addRow(label, spin);
  layout->addLayout(form);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  match_ok_cancel_widths(*buttons);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  install_shift_step(spin, 10);
  spin->selectAll();
  return exec_dialog(dialog) == QDialog::Accepted;
}

}  // namespace

std::optional<int> request_selection_smooth_radius(QWidget* parent, int initial) {
  QDialog dialog(parent);
  auto* spin = new QSpinBox(&dialog);
  spin->setObjectName(QStringLiteral("smoothSelectionRadiusSpin"));
  spin->setRange(kMinSelectionSmoothRadius, kMaxSelectionSmoothRadius);
  spin->setSingleStep(1);
  spin->setSuffix(pixel_suffix());
  spin->setValue(std::clamp(initial, kMinSelectionSmoothRadius, kMaxSelectionSmoothRadius));
  configure_dialog_spinbox(spin);
  if (!run_radius_dialog(QStringLiteral("patchySmoothSelectionDialog"),
                         QObject::tr("Smooth Selection"), QObject::tr("Sample radius"), spin, dialog)) {
    return std::nullopt;
  }
  return spin->value();
}

std::optional<double> request_selection_feather_radius(QWidget* parent, double initial) {
  QDialog dialog(parent);
  auto* spin = new QDoubleSpinBox(&dialog);
  spin->setObjectName(QStringLiteral("featherSelectionRadiusSpin"));
  spin->setDecimals(1);
  spin->setRange(kMinSelectionFeatherCommandRadius, kMaxSelectionFeatherCommandRadius);
  spin->setSingleStep(0.1);
  spin->setSuffix(pixel_suffix());
  spin->setValue(std::clamp(initial, kMinSelectionFeatherCommandRadius, kMaxSelectionFeatherCommandRadius));
  configure_dialog_spinbox(spin);
  if (!run_radius_dialog(QStringLiteral("patchyFeatherSelectionDialog"),
                         QObject::tr("Feather Selection"), QObject::tr("Feather radius"), spin, dialog)) {
    return std::nullopt;
  }
  return spin->value();
}

}  // namespace patchy::ui
