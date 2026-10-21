#include "ui/document_file_rename.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/qt_paths.hpp"
#include "ui/theme_qss.hpp"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <filesystem>
#include <system_error>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace patchy::ui {
namespace {

bool is_windows_device_name(const QString& name) {
  // Windows reserves these with any extension ("con.psd", "nul.tar.gz") and ignores
  // trailing spaces before the dot; superscript digits count too.
  const auto base = name.section(QLatin1Char('.'), 0, 0).trimmed().toUpper();
  static const std::array<QString, 4> plain = {QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"),
                                               QStringLiteral("NUL")};
  for (const auto& reserved : plain) {
    if (base == reserved) return true;
  }
  if (base.size() == 4 && (base.startsWith(QStringLiteral("COM")) || base.startsWith(QStringLiteral("LPT")))) {
    const auto digit = base.at(3);
    return (digit >= QLatin1Char('0') && digit <= QLatin1Char('9')) || digit == QChar(0x00B9) ||
           digit == QChar(0x00B2) || digit == QChar(0x00B3);
  }
  return false;
}

// Same file under two spellings: `to` names the file `from` names (a case-only
// change on a case-insensitive file system), not a second file.
bool names_same_file(const QString& from, const QString& to) {
  if (QFileInfo(from).fileName().compare(QFileInfo(to).fileName(), Qt::CaseInsensitive) != 0) {
    return false;
  }
  std::error_code error;
  const bool same = std::filesystem::equivalent(to_filesystem_path(from), to_filesystem_path(to), error);
  return same && !error;
}

#ifdef Q_OS_WIN
QString windows_error_text(DWORD code) {
  wchar_t* buffer = nullptr;
  const auto length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
      reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
  QString text = length > 0 ? QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed() : QString();
  if (buffer != nullptr) LocalFree(buffer);
  return text.isEmpty() ? QStringLiteral("Windows error %1").arg(code) : text;
}
#endif

// One rename that refuses an existing target. MoveFileExW without
// MOVEFILE_REPLACE_EXISTING on Windows, for its own error codes (a file another
// program holds open is a sharing violation there). Elsewhere QFile::rename, whose
// engine refuses an existing target (renameat2 RENAME_NOREPLACE or link/unlink on
// Linux, renamex_np RENAME_EXCL on macOS).
FileRenameResult rename_once(const QString& from, const QString& to) {
#ifdef Q_OS_WIN
  const auto source = to_filesystem_path(from);
  const auto target = to_filesystem_path(to);
  if (MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) != 0) {
    return {FileRenameStatus::Renamed, {}};
  }
  const auto code = GetLastError();
  switch (code) {
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
      return {FileRenameStatus::TargetExists, {}};
    case ERROR_FILE_NOT_FOUND:
      return {FileRenameStatus::SourceMissing, {}};
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
      return {FileRenameStatus::Failed,
              QCoreApplication::translate("patchy::ui::MainWindow", "The file is open in another program.")};
    default:
      return {FileRenameStatus::Failed, windows_error_text(code)};
  }
#else
  QFile file(from);
  if (file.rename(to)) {
    return {FileRenameStatus::Renamed, {}};
  }
  if (!QFileInfo::exists(from)) return {FileRenameStatus::SourceMissing, {}};
  if (QFileInfo::exists(to)) return {FileRenameStatus::TargetExists, {}};
  return {FileRenameStatus::Failed, file.errorString()};
#endif
}

QString unique_sibling_temporary(const QString& path) {
  const QFileInfo info(path);
  for (;;) {
    const auto candidate = info.dir().filePath(QStringLiteral("%1.%2-%3.patchy-rename")
                                                   .arg(info.fileName())
                                                   .arg(QCoreApplication::applicationPid())
                                                   .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0')));
    if (!QFileInfo::exists(candidate) && !QFileInfo(candidate).isSymLink()) {
      return candidate;
    }
  }
}

}  // namespace

FileNameParts split_file_name(const QString& file_name) {
  const auto dot = file_name.lastIndexOf(QLatin1Char('.'));
  if (dot <= 0) {
    return {file_name, QString()};
  }
  return {file_name.left(dot), file_name.mid(dot)};
}

QString file_name_problem(const QString& stem, const QString& extension) {
  if (stem.trimmed().isEmpty()) {
    return QCoreApplication::translate("patchy::ui::MainWindow", "Enter a file name.");
  }
  for (const auto character : stem) {
    if (character == QLatin1Char('/') || character == QLatin1Char('\\')) {
      return QCoreApplication::translate("patchy::ui::MainWindow", "A file name cannot contain / or \\.");
    }
    if (character.unicode() < 0x20 || character.unicode() == 0x7f) {
      return QCoreApplication::translate("patchy::ui::MainWindow", "A file name cannot contain control characters.");
    }
#if defined(Q_OS_WIN)
    if (QStringLiteral("<>:\"|?*").contains(character)) {
      return QCoreApplication::translate("patchy::ui::MainWindow",
                                         "A file name cannot contain any of these characters: %1")
          .arg(QStringLiteral("< > : \" | ? *"));
    }
#elif defined(Q_OS_MACOS)
    if (character == QLatin1Char(':')) {
      return QCoreApplication::translate("patchy::ui::MainWindow",
                                         "A file name cannot contain any of these characters: %1")
          .arg(QStringLiteral(":"));
    }
#endif
  }
  const auto name = stem + extension;
  if (std::all_of(stem.begin(), stem.end(),
                  [](QChar character) { return character == QLatin1Char('.') || character == QLatin1Char(' '); })) {
    return QCoreApplication::translate("patchy::ui::MainWindow", "A file name cannot be only dots and spaces.");
  }
#ifdef Q_OS_WIN
  if (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) {
    return QCoreApplication::translate("patchy::ui::MainWindow", "A file name cannot end with a dot or a space.");
  }
#endif
  if (is_windows_device_name(name)) {
    return QCoreApplication::translate("patchy::ui::MainWindow", "%1 is a reserved device name on Windows.")
        .arg(name.section(QLatin1Char('.'), 0, 0).trimmed());
  }
#ifdef Q_OS_WIN
  const bool too_long = name.size() > 255;
#else
  const bool too_long = name.toUtf8().size() > 255;
#endif
  if (too_long) {
    return QCoreApplication::translate("patchy::ui::MainWindow", "The file name is too long.");
  }
  return {};
}

FileRenameResult rename_file_without_replacing(const QString& from, const QString& to) {
  if (!QFileInfo::exists(from)) {
    return {FileRenameStatus::SourceMissing, {}};
  }
  if (QFileInfo::exists(to) || QFileInfo(to).isSymLink()) {
    if (from == to || !names_same_file(from, to)) {
      return {FileRenameStatus::TargetExists, {}};
    }
    // A case-only change on a case-insensitive file system: `to` is this file.
    // Two plain renames through a name nothing else uses, each refusing a target.
    const auto temporary = unique_sibling_temporary(from);
    if (const auto moved = rename_once(from, temporary); moved.status != FileRenameStatus::Renamed) {
      return moved;
    }
    const auto renamed = rename_once(temporary, to);
    if (renamed.status != FileRenameStatus::Renamed) {
      (void)rename_once(temporary, from);
    }
    return renamed;
  }
  return rename_once(from, to);
}

bool request_file_rename(QWidget* parent, const QString& current_path,
                         const std::function<QString(const QString& new_path)>& attempt) {
  const QFileInfo current(current_path);
  const auto current_name = current.fileName();
  const auto parts = split_file_name(current_name);

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyRenameFileDialog"));
  auto* root = new QVBoxLayout(&dialog);
  auto* content =
      install_dark_dialog_chrome(dialog, root, QCoreApplication::translate("patchy::ui::MainWindow", "Rename File"));
  auto* body = new QVBoxLayout();
  body->setContentsMargins(14, 12, 14, 12);
  body->setSpacing(10);
  content->addLayout(body);

  auto* form = new QFormLayout();
  auto* field_row = new QHBoxLayout();
  field_row->setSpacing(4);
  auto* name_edit = new QLineEdit(parts.stem, &dialog);
  name_edit->setObjectName(QStringLiteral("renameFileNameEdit"));
  name_edit->setMinimumWidth(300);
  field_row->addWidget(name_edit, 1);
  auto* extension_label = new QLabel(parts.extension, &dialog);
  extension_label->setObjectName(QStringLiteral("renameFileExtensionLabel"));
  extension_label->setVisible(!parts.extension.isEmpty());
  field_row->addWidget(extension_label);
  form->addRow(QCoreApplication::translate("patchy::ui::MainWindow", "Name:"), field_row);
  body->addLayout(form);

  auto* problem_label = new QLabel(&dialog);
  problem_label->setObjectName(QStringLiteral("renameFileProblemLabel"));
  problem_label->setWordWrap(true);
  problem_label->setVisible(false);
  set_themed_style(*problem_label,
                   QStringLiteral("QLabel#renameFileProblemLabel { background: @warning_banner_bg; border: 1px solid "
                                  "@warning_banner_border; border-radius: 3px; color: @warning_banner_text; "
                                  "padding: 7px 9px; }"));
  body->addWidget(problem_label);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  body->addWidget(buttons);
  auto* ok_button = buttons->button(QDialogButtonBox::Ok);
  ok_button->setText(QCoreApplication::translate("patchy::ui::MainWindow", "Rename"));

  const auto show_problem = [problem_label, ok_button](const QString& problem, bool blocks_ok) {
    problem_label->setText(problem);
    problem_label->setVisible(!problem.isEmpty());
    ok_button->setEnabled(!blocks_ok || problem.isEmpty());
  };
  QObject::connect(name_edit, &QLineEdit::textChanged, &dialog, [show_problem, extension = parts.extension](const QString& stem) {
    show_problem(file_name_problem(stem, extension), true);
  });
  bool renamed = false;
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    const auto stem = name_edit->text();
    if (const auto problem = file_name_problem(stem, parts.extension); !problem.isEmpty()) {
      show_problem(problem, true);
      return;
    }
    const auto new_name = stem + parts.extension;
    if (new_name == current_name) {
      dialog.reject();
      return;
    }
    const auto error = attempt(current.dir().filePath(new_name));
    if (!error.isEmpty()) {
      // The disk said no (a file of that name, a lock): keep the dialog for
      // another name, with OK live so the same name can be retried.
      show_problem(error, false);
      name_edit->setFocus();
      name_edit->selectAll();
      return;
    }
    renamed = true;
    dialog.accept();
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  name_edit->selectAll();
  name_edit->setFocus();
  (void)exec_dialog(dialog);
  return renamed;
}

}  // namespace patchy::ui
