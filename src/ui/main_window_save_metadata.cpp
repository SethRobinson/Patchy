// Preferences > Metadata and Watermarks, "Saved metadata" (docs/file-info.md, "Saved
// metadata preference"): the user's name, copyright and contact added on save.

#include "ui/main_window.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/document_session.hpp"
#include "ui/save_metadata_settings.hpp"

#include <QCheckBox>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>

namespace patchy::ui {

bool MainWindow::add_personal_metadata_for_save(const QString& extension, bool quiet) {
  if (!has_active_document() || !save_format_carries_metadata(extension)) {
    return false;
  }
  auto personal = stored_personal_save_metadata();
  auto& target = session();
  if (personal.name.isEmpty() || target.personal_save_metadata_skipped ||
      file_info_names_creator(std::as_const(target.document).file_info(), personal.name)) {
    return false;
  }
  if (personal.ask) {
    if (quiet) {
      return false;  // a scripted or unattended save never asks, and adds nothing unasked
    }
    const auto creators = file_info_creators(std::as_const(target.document).file_info());
    QMessageBox box(this);
    box.setObjectName(QStringLiteral("savePersonalMetadataPrompt"));
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Add Your Name and Copyright"));
    box.setText(tr("Add your name and copyright to this file?"));
    QStringList details;
    details << tr("Name: %1").arg(personal.name);
    if (!personal.copyright.isEmpty()) {
      details << tr("Copyright: %1").arg(personal.copyright);
    }
    if (!creators.isEmpty()) {
      details << tr("The file already names %1. Your name is added beside it; nothing is replaced.")
                     .arg(creators.join(QStringLiteral(", ")));
    }
    box.setInformativeText(details.join(QLatin1Char('\n')));
    auto* add_button = box.addButton(tr("Add"), QMessageBox::AcceptRole);
    auto* skip_button = box.addButton(tr("Skip"), QMessageBox::RejectRole);
    box.setDefaultButton(add_button);
    box.setEscapeButton(skip_button);
    auto* always = new QCheckBox(tr("Always add without asking"), &box);
    always->setObjectName(QStringLiteral("savePersonalMetadataAlwaysCheck"));
    box.setCheckBox(always);
    exec_dialog(box);
    if (!has_active_document() || &session() != &target) {
      return false;
    }
    if (box.clickedButton() != add_button) {
      target.personal_save_metadata_skipped = true;
      return false;
    }
    if (always->isChecked()) {
      personal.ask = false;
      set_stored_personal_save_metadata(personal);
    }
  }
  // Like the save stamp, a save-time metadata change: no undo step of its own.
  return add_personal_save_metadata(target.document.file_info(), personal);
}

}  // namespace patchy::ui
