// Edit in Default App for embedded smart objects whose contents Patchy can't re-encode
// (GitHub issue 620; docs/smart-object-editing.md, "Edit in Default App").
#include "ui/main_window.hpp"

#include "core/smart_object.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/smart_object_render.hpp"

#include <QApplication>
#include <QByteArray>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// Saves land as several change notifications (truncate, write, rename); one update
// runs once the file has been quiet this long.
constexpr int kExternalEditSettleMs = 400;

bool bytes_equal(const SmartObjectSource& source, const QByteArray& bytes) {
  return source.file_bytes != nullptr && source.file_bytes->size() == static_cast<std::size_t>(bytes.size()) &&
         std::equal(source.file_bytes->begin(), source.file_bytes->end(),
                    reinterpret_cast<const std::uint8_t*>(bytes.constData()));
}

// The element's own file name, made safe for a file system; "contents" when it has none.
QString external_edit_file_name(const SmartObjectSource& source) {
  auto name = QFileInfo(QString::fromStdString(source.filename)).fileName();
  static const QString kForbidden = QStringLiteral("<>:\"/\\|?*");
  for (auto& character : name) {
    if (character.unicode() < 0x20 || kForbidden.contains(character)) {
      character = QLatin1Char('_');
    }
  }
  while (name.startsWith(QLatin1Char('.'))) {
    name.remove(0, 1);
  }
  return name.isEmpty() ? QStringLiteral("contents") : name;
}

}  // namespace

void MainWindow::offer_external_smart_object_edit(const std::string& source_uuid) {
  const auto* source = std::as_const(document()).metadata().smart_objects.find(source_uuid);
  if (source == nullptr) {
    return;
  }
  const auto file_name = QString::fromStdString(source->filename);
#ifdef Q_OS_WASM
  show_status_error(tr("Patchy can't re-encode %1 contents; use Export Smart Object Contents or rasterize the layer")
                        .arg(file_name));
  return;
#else
  QMessageBox box(this);
  box.setObjectName(QStringLiteral("editSmartObjectInDefaultAppMessageBox"));
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(tr("Edit Smart Object Contents"));
  box.setText(tr("Patchy can't edit %1 contents itself.").arg(file_name));
  box.setInformativeText(
      tr("Edit in Default App opens a copy in the app your system uses for this file type. Each time you "
         "save it there, Patchy updates the smart object."));
  auto* edit_button = box.addButton(tr("Edit in Default App"), QMessageBox::AcceptRole);
  edit_button->setObjectName(QStringLiteral("editSmartObjectInDefaultAppButton"));
  auto* export_button = box.addButton(tr("Export..."), QMessageBox::ActionRole);
  export_button->setObjectName(QStringLiteral("editSmartObjectExportButton"));
  auto* cancel_button = box.addButton(QMessageBox::Cancel);
  box.setDefaultButton(edit_button);
  exec_dialog(box);
  if (box.clickedButton() == nullptr || box.clickedButton() == cancel_button) {
    return;
  }
  if (box.clickedButton() == export_button) {
    export_smart_object_contents();
    return;
  }
  if (!has_active_document()) {
    return;
  }
  (void)start_external_smart_object_edit(session(), source_uuid, true);
#endif
}

std::optional<QString> MainWindow::start_external_smart_object_edit(DocumentSession& target,
                                                                    const std::string& source_uuid, bool launch) {
  const auto* source = std::as_const(target.document).metadata().smart_objects.find(source_uuid);
  if (source == nullptr || source->kind != SmartObjectSourceKind::Embedded || source->file_bytes == nullptr ||
      source->file_bytes->empty()) {
    show_status_error(tr("This smart object's contents are not embedded in the document"));
    return std::nullopt;
  }

  // An edit already running for this element (any of its uuids) reuses its file.
  auto existing = std::find_if(external_smart_object_edits_.begin(), external_smart_object_edits_.end(),
                               [&](const ExternalSmartObjectEdit& edit) {
                                 return edit.session_id == target.session_id &&
                                        std::find(edit.source_uuids.begin(), edit.source_uuids.end(),
                                                  source_uuid) != edit.source_uuids.end();
                               });
  QString path;
  if (existing != external_smart_object_edits_.end()) {
    path = existing->path;
    if (existing->source_uuids.back() != source_uuid) {
      existing->source_uuids.push_back(source_uuid);
    }
  } else {
    // One patchy-so-edit-* folder per document; a second element with the same
    // file name gets a numbered name beside the first.
    QString folder;
    for (const auto& edit : external_smart_object_edits_) {
      if (edit.session_id == target.session_id && QFileInfo(edit.folder).isDir()) {
        folder = edit.folder;
        break;
      }
    }
    if (folder.isEmpty()) {
      QTemporaryDir temporary(QDir::temp().filePath(QStringLiteral("patchy-so-edit-XXXXXX")));
      if (!temporary.isValid()) {
        show_status_error(tr("Could not create a temporary folder for the contents"));
        return std::nullopt;
      }
      temporary.setAutoRemove(false);
      folder = temporary.path();
    }
    const auto file_name = external_edit_file_name(*source);
    path = QDir(folder).filePath(file_name);
    const QFileInfo name_info(file_name);
    for (int counter = 2; std::any_of(external_smart_object_edits_.begin(), external_smart_object_edits_.end(),
                                      [&](const ExternalSmartObjectEdit& edit) { return edit.path == path; });
         ++counter) {
      const auto suffix = name_info.suffix();
      path = QDir(folder).filePath(name_info.completeBaseName() + QStringLiteral("-%1").arg(counter) +
                                   (suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix));
    }
    external_smart_object_edits_.push_back(ExternalSmartObjectEdit{target.session_id, {source_uuid}, path, folder});
  }

  // The file always starts from the element's current bytes (after an undo, the
  // editor opens what the document shows). The change this write raises finds the
  // same bytes embedded and does nothing.
  {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(reinterpret_cast<const char*>(source->file_bytes->data()),
                   static_cast<qint64>(source->file_bytes->size())) !=
            static_cast<qint64>(source->file_bytes->size())) {
      show_status_error(tr("Could not write %1").arg(QDir::toNativeSeparators(path)));
      return std::nullopt;
    }
  }

  if (external_edit_watcher_ == nullptr) {
    external_edit_watcher_ = new QFileSystemWatcher(this);
    external_edit_timer_ = new QTimer(this);
    external_edit_timer_->setSingleShot(true);
    external_edit_timer_->setInterval(kExternalEditSettleMs);
    connect(external_edit_timer_, &QTimer::timeout, this, [this] { apply_pending_external_smart_object_edits(); });
    connect(external_edit_watcher_, &QFileSystemWatcher::fileChanged, this,
            [this](const QString& changed) { external_smart_object_edit_file_changed(changed); });
    // Editors that save by writing a new file and renaming it over the old one drop
    // the file watch; the folder's change notice catches those saves.
    connect(external_edit_watcher_, &QFileSystemWatcher::directoryChanged, this, [this](const QString& folder) {
      for (const auto& edit : external_smart_object_edits_) {
        if (QFileInfo(edit.folder) == QFileInfo(folder)) {
          external_smart_object_edit_file_changed(edit.path);
        }
      }
    });
  }
  const auto folder = QFileInfo(path).absolutePath();
  if (!external_edit_watcher_->directories().contains(folder)) {
    external_edit_watcher_->addPath(folder);
  }
  if (!external_edit_watcher_->files().contains(path)) {
    external_edit_watcher_->addPath(path);
  }

  if (launch) {
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
      show_status_error(tr("No app opened %1. Open the copy at %2 in any editor; saving it updates the smart object")
                            .arg(QFileInfo(path).fileName(), QDir::toNativeSeparators(path)));
      return path;
    }
    statusBar()->showMessage(tr("Opened %1 in its default app. Each save there updates the smart object")
                                 .arg(QFileInfo(path).fileName()));
  }
  return path;
}

void MainWindow::external_smart_object_edit_file_changed(const QString& path) {
  if (external_edit_timer_ == nullptr) {
    return;
  }
  external_edit_pending_paths_.insert(path);
  external_edit_timer_->start();
}

void MainWindow::apply_pending_external_smart_object_edits() {
  // Never swap a document under an open dialog (a filter preview, a save prompt):
  // try again once it has closed.
  if (QApplication::activeModalWidget() != nullptr || preview_dialog_edit_locked()) {
    external_edit_timer_->start();
    return;
  }
  const auto pending = std::exchange(external_edit_pending_paths_, {});
  for (const auto& path : pending) {
    // A replace-style save removed the old file from the watch list; watch the new one.
    if (external_edit_watcher_ != nullptr && QFileInfo::exists(path) &&
        !external_edit_watcher_->files().contains(path)) {
      external_edit_watcher_->addPath(path);
    }
    (void)apply_external_smart_object_edit(path);
  }
}

bool MainWindow::apply_external_smart_object_edit(const QString& path) {
  if (shutting_down_) {
    return false;
  }
  auto edit = std::find_if(external_smart_object_edits_.begin(), external_smart_object_edits_.end(),
                           [&](const ExternalSmartObjectEdit& candidate) { return candidate.path == path; });
  if (edit == external_smart_object_edits_.end()) {
    return false;
  }
  auto* target = session_with_id(edit->session_id);
  if (target == nullptr) {
    stop_external_smart_object_edits_for_session(edit->session_id);
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;  // mid-save; the next change notice retries
  }
  const auto bytes = file.readAll();
  file.close();
  if (bytes.isEmpty()) {
    return false;  // truncated mid-save
  }

  // The newest of the element's uuids still in the document (undo can bring back an older one).
  const SmartObjectSource* source = nullptr;
  std::string current_uuid;
  for (auto uuid = edit->source_uuids.rbegin(); uuid != edit->source_uuids.rend(); ++uuid) {
    const auto* candidate = std::as_const(target->document).metadata().smart_objects.find(*uuid);
    if (candidate != nullptr && candidate->kind == SmartObjectSourceKind::Embedded) {
      source = candidate;
      current_uuid = *uuid;
      break;
    }
  }
  const auto title = target->title.isEmpty() ? tr("Untitled") : target->title;
  if (source == nullptr) {
    show_status_error(tr("The smart object no longer exists in %1").arg(title));
    return false;
  }
  if (bytes_equal(*source, bytes)) {
    return false;
  }

  SmartObjectSource updated = *source;
  updated.file_bytes = std::make_shared<const std::vector<std::uint8_t>>(bytes.begin(), bytes.end());
  updated.original_element_bytes = nullptr;
  updated.dirty = true;
  const auto rendered_image = decode_smart_object_source_image(updated);
  if (!rendered_image.has_value()) {
    show_status_error(tr("Could not decode %1").arg(QFileInfo(path).fileName()));
    return false;
  }
  const auto content_dpi =
      psd::DocumentIo::can_read({updated.file_bytes->data(), updated.file_bytes->size()})
          ? smart_object_source_dpi(updated)
          : 0.0;

  // The same element update as Edit Contents' commit: a fresh uuid, every sharing
  // layer re-rendered and re-keyed, one undo step on the document.
  const auto refreshed_uuid = generate_smart_object_uuid();
  updated.uuid = refreshed_uuid;
  auto updated_document = target->document;
  bool source_replaced = false;
  for (auto& block : updated_document.metadata().smart_objects.blocks) {
    for (auto& candidate_source : block.sources) {
      if (candidate_source.uuid == current_uuid) {
        candidate_source = updated;
        block.original_payload.reset();
        source_replaced = true;
        break;
      }
    }
    if (source_replaced) {
      break;
    }
  }
  if (!source_replaced ||
      !refresh_smart_object_layers_for_source(updated_document, current_uuid, *rendered_image, content_dpi, false,
                                              true, refreshed_uuid, &updated)) {
    show_status_error(tr("Could not rebuild the Smart Filter preview and cache"));
    return false;
  }

  record_history_push(*target,
                      DocumentSession::HistoryState{
                          target->document, std::nullopt, target->revision,
                          target->canvas != nullptr ? target->canvas->capture_selection_snapshot()
                                                    : CanvasWidget::SelectionSnapshot{},
                          {}, 0},
                      tr("Edit Smart Object Contents"));
  target->document = std::move(updated_document);
  edit->source_uuids.push_back(refreshed_uuid);
  if (target->canvas != nullptr) {
    target->canvas->document_changed();
  }
  mark_session_modified(*target);
  if (target == active_session()) {
    refresh_layer_list();
    refresh_layer_controls();
    refresh_history_panel();
  }
  statusBar()->showMessage(tr("Updated the smart object from %1").arg(QFileInfo(path).fileName()));
  return true;
}

void MainWindow::stop_external_smart_object_edits_for_session(std::int64_t session_id) {
  std::vector<QString> folders;
  for (auto edit = external_smart_object_edits_.begin(); edit != external_smart_object_edits_.end();) {
    if (edit->session_id != session_id) {
      ++edit;
      continue;
    }
    if (external_edit_watcher_ != nullptr) {
      external_edit_watcher_->removePath(edit->path);
    }
    external_edit_pending_paths_.remove(edit->path);
    QFile::remove(edit->path);
    if (std::find(folders.begin(), folders.end(), edit->folder) == folders.end()) {
      folders.push_back(edit->folder);
    }
    edit = external_smart_object_edits_.erase(edit);
  }
  for (const auto& folder : folders) {
    if (external_edit_watcher_ != nullptr) {
      external_edit_watcher_->removePath(folder);
    }
    // Only our own patchy-so-edit-* folder, and only once it is empty (never a
    // recursive delete: an editor's backup files stay for the user).
    if (QFileInfo(folder).fileName().startsWith(QStringLiteral("patchy-so-edit-"))) {
      QDir().rmdir(folder);
    }
  }
}

}  // namespace patchy::ui
