#include "ui/convert_profile_dialog.hpp"

#include "color/rgb_profiles.hpp"
#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/qt_paths.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace patchy::ui {
namespace {

// Item data of the file-picker row; it never stays selected.
const QString kChooseProfileFile = QStringLiteral("choose-file");

}  // namespace

std::optional<ConvertProfileChoice> request_convert_to_profile(QWidget* parent, const QString& current_profile_name) {
  auto settings = app_settings();
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("convertToProfileDialog"));
  dialog.setWindowTitle(QObject::tr("Convert to Color Profile"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  auto* source = new QLabel(current_profile_name, &dialog);
  source->setObjectName(QStringLiteral("convertToProfileSourceLabel"));
  source->setTextInteractionFlags(Qt::TextSelectableByMouse);
  form->addRow(QObject::tr("Current profile:"), source);

  auto* destination = new QComboBox(&dialog);
  destination->setObjectName(QStringLiteral("convertToProfileDestinationCombo"));
  destination->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  destination->setMinimumContentsLength(28);
  // Item data is the profile path: empty for sRGB, which needs no file.
  destination->addItem(QString::fromLatin1(kSrgbProfileName), QString());
  const auto add_entry = [destination](int row, const QString& path, QString description) {
    if (description.isEmpty()) {
      description = QFileInfo(path).completeBaseName();
    }
    destination->insertItem(row, description, path);
    destination->setItemData(row, QDir::toNativeSeparators(path), Qt::ToolTipRole);
  };
  const auto installed_profiles = installed_rgb_profiles();
  for (const auto& installed : installed_profiles) {
    add_entry(destination->count(), to_qstring(installed.path), QString::fromStdString(installed.description));
  }
#ifndef Q_OS_WASM
  destination->insertSeparator(destination->count());
  destination->addItem(QObject::tr("Choose Profile File..."), kChooseProfileFile);
#endif
  // A chosen file outside the scanned folders gets its own row above the separator.
  const auto select_path = [destination, add_entry](const QString& path) {
    auto index = destination->findData(path);
    if (index < 0 && !path.isEmpty()) {
      const auto bytes = read_rgb_profile(to_filesystem_path(path));
      if (!bytes.empty()) {
        index = destination->findData(kChooseProfileFile);
        index = index < 0 ? destination->count() : std::max(1, index - 1);
        add_entry(index, path, QString::fromStdString(rgb_profile_description(bytes)));
      }
    }
    const QSignalBlocker blocker(destination);
    destination->setCurrentIndex(std::max(index, 0));
  };
  select_path(settings.value(QStringLiteral("color/convertProfileDestination")).toString());
  auto last_choice = std::make_shared<QString>(destination->currentData().toString());
  QObject::connect(destination, &QComboBox::currentIndexChanged, &dialog,
                   [&dialog, destination, select_path, last_choice](int index) {
                     const auto choice = destination->itemData(index).toString();
                     if (choice != kChooseProfileFile) {
                       *last_choice = choice;
                       return;
                     }
                     select_path(*last_choice);
                     QTimer::singleShot(0, &dialog, [&dialog, select_path, last_choice] {
                       const auto path = get_open_file_name(&dialog, QObject::tr("Choose RGB Profile"), QString(),
                                                            QObject::tr("ICC profiles (*.icc *.icm)"));
                       if (path.isEmpty()) {
                         return;
                       }
                       if (read_rgb_profile(to_filesystem_path(path)).empty()) {
                         show_critical_message(&dialog, QObject::tr("Convert to Color Profile"),
                                               QObject::tr("\"%1\" is not an RGB profile Patchy can use.").arg(path),
                                               QStringLiteral("convertToProfileFailedMessageBox"));
                         return;
                       }
                       select_path(path);
                       *last_choice = path;
                     });
                   });
  form->addRow(QObject::tr("Destination profile:"), destination);
#ifndef Q_OS_WASM
  // Patchy ships no RGB profiles: the list is sRGB plus what this computer has installed.
  auto* sources = new QLabel(installed_profiles.empty()
                                 ? QObject::tr("sRGB is built into Patchy. No other RGB profiles are installed on "
                                               "this computer; use Choose Profile File... to pick a profile file.")
                                 : QObject::tr("sRGB is built into Patchy. The other profiles are the RGB profiles "
                                               "installed on this computer; use Choose Profile File... for one "
                                               "that is not listed."),
                             &dialog);
  sources->setObjectName(QStringLiteral("convertToProfileSourcesNote"));
  sources->setWordWrap(true);
  form->addRow(sources);
#endif

  auto* intent = new QComboBox(&dialog);
  intent->setObjectName(QStringLiteral("convertToProfileIntentCombo"));
  intent->addItem(QObject::tr("Perceptual"), static_cast<int>(RenderingIntent::Perceptual));
  intent->addItem(QObject::tr("Relative Colorimetric"), static_cast<int>(RenderingIntent::RelativeColorimetric));
  intent->addItem(QObject::tr("Saturation"), static_cast<int>(RenderingIntent::Saturation));
  intent->addItem(QObject::tr("Absolute Colorimetric"), static_cast<int>(RenderingIntent::AbsoluteColorimetric));
  const auto stored_intent =
      rendering_intent_from_token(settings.value(QStringLiteral("color/convertProfileIntent")).toString().toStdString())
          .value_or(RenderingIntent::RelativeColorimetric);
  intent->setCurrentIndex(std::max(intent->findData(static_cast<int>(stored_intent)), 0));
  form->addRow(QObject::tr("Rendering intent:"), intent);
  auto* black_point = new QCheckBox(QObject::tr("Black point compensation"), &dialog);
  black_point->setObjectName(QStringLiteral("convertToProfileBlackPointCheck"));
  black_point->setChecked(settings.value(QStringLiteral("color/convertProfileBlackPoint"), true).toBool());
  form->addRow(black_point);
  layout->addLayout(form);

  auto* note = new QLabel(QObject::tr("Patchy shows RGB colors as sRGB. A document converted to another profile "
                                      "looks different on screen here, and its saves carry that profile."),
                          &dialog);
  note->setObjectName(QStringLiteral("convertToProfileNote"));
  note->setWordWrap(true);
  layout->addWidget(note);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  ConvertProfileChoice choice;
  choice.destination_path = destination->currentData().toString();
  if (choice.destination_path == kChooseProfileFile) {
    choice.destination_path.clear();
  }
  if (!choice.destination_path.isEmpty()) {
    choice.destination = read_rgb_profile(to_filesystem_path(choice.destination_path));
    if (choice.destination.empty()) {
      show_critical_message(parent, QObject::tr("Convert to Color Profile"),
                            QObject::tr("\"%1\" is not an RGB profile Patchy can use.").arg(choice.destination_path),
                            QStringLiteral("convertToProfileFailedMessageBox"));
      return std::nullopt;
    }
  }
  choice.intent = static_cast<RenderingIntent>(intent->currentData().toInt());
  choice.black_point_compensation = black_point->isChecked();
  settings.setValue(QStringLiteral("color/convertProfileDestination"), choice.destination_path);
  settings.setValue(QStringLiteral("color/convertProfileIntent"),
                    QString::fromLatin1(rendering_intent_token(choice.intent).data(),
                                        static_cast<qsizetype>(rendering_intent_token(choice.intent).size())));
  settings.setValue(QStringLiteral("color/convertProfileBlackPoint"), choice.black_point_compensation);
  return choice;
}

}  // namespace patchy::ui
