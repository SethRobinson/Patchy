#include "ui/color_panel.hpp"

#include "ui/dialog_utils.hpp"

#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <memory>

namespace patchy::ui {

namespace {

QColor normalized_rgb_color(QColor color) {
  color.setAlpha(255);
  return color;
}

QColorDialog* add_advanced_color_picker(QDialog& dialog, QVBoxLayout* layout, QColor initial) {
  auto* picker = new QColorDialog(normalized_rgb_color(initial), &dialog);
  picker->setObjectName(QStringLiteral("patchyAdvancedColorPicker"));
  picker->setWindowFlags(Qt::Widget);
  picker->setOption(QColorDialog::DontUseNativeDialog, true);
  picker->setOption(QColorDialog::NoButtons, true);
  picker->setOption(QColorDialog::ShowAlphaChannel, false);
  picker->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  picker->setMinimumSize(430, 360);
  layout->addWidget(picker, 1);
  picker->setCurrentColor(normalized_rgb_color(initial));
  return picker;
}

}  // namespace

QString color_button_style(QColor color) {
  const auto text = color.lightness() < 128 ? QStringLiteral("white") : QStringLiteral("black");
  return QStringLiteral(R"(
    QPushButton {
      background: rgb(%1, %2, %3);
      color: %4;
      border: 1px solid #f0f0f0;
      border-radius: 0;
      min-width: 26px;
      max-width: 26px;
      min-height: 24px;
      max-height: 24px;
      font-weight: 700;
      padding: 0;
    }
    QPushButton:hover {
      border-color: #4aa3ff;
    }
  )")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(text);
}

QString swatch_button_style(QColor color, bool large) {
  return QStringLiteral(
             "QPushButton { background: rgb(%1, %2, %3); border: 1px solid #747b86; border-radius: 2px; min-width: %4px; "
             "min-height: %4px; max-width: %4px; max-height: %4px; }"
             "QPushButton:hover { border: 2px solid #63a6ff; }")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(large ? 30 : 24);
}

QString inline_text_editor_style(QColor color, int pixel_size) {
  return QStringLiteral(
             "QTextEdit { background: transparent; color: rgb(%1, %2, %3); "
             "border: 1px dashed #63a8ff; padding: 0; font-size: %4px; } "
             "QTextEdit QWidget { background: transparent; } "
             "QTextEdit::selection { background: rgba(49, 116, 190, 130); }")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(pixel_size);
}

QDialog* create_patchy_color_panel(QWidget* parent, QColor initial, const QString& title,
                                      std::function<void(QColor)> color_changed) {
  auto* dialog = new QDialog(parent);
  dialog->setObjectName(QStringLiteral("patchyColorDialog"));
  dialog->setModal(false);
  dialog->setWindowModality(Qt::NonModal);
  dialog->setAttribute(Qt::WA_DeleteOnClose, true);
  dialog->resize(560, 520);

  auto* layout = install_dark_dialog_chrome(*dialog, new QVBoxLayout(dialog), title);
  auto* picker = add_advanced_color_picker(*dialog, layout, initial);
  auto changed_callback = std::make_shared<std::function<void(QColor)>>(std::move(color_changed));
  QObject::connect(picker, &QColorDialog::currentColorChanged, dialog, [changed_callback](QColor color) {
    color = normalized_rgb_color(color);
    if (*changed_callback) {
      (*changed_callback)(color);
    }
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::close);
  remember_dialog_position(*dialog);
  return dialog;
}

std::optional<QColor> request_patchy_color(QWidget* parent, QColor initial, const QString& title) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyColorDialog"));
  dialog.resize(560, 520);

  auto* layout = install_dark_dialog_chrome(dialog, new QVBoxLayout(&dialog), title);
  auto* picker = add_advanced_color_picker(dialog, layout, initial);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return normalized_rgb_color(picker->currentColor());
}

}  // namespace patchy::ui
