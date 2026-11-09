// MainWindow's side of the PDF viewer (docs/pdf-viewer.md): opening or raising a viewer for
// a file, and the viewer's Edit..., which runs the ordinary PDF import over the viewer.

#include "ui/main_window.hpp"

#include "ui/pdf_import.hpp"

#include <QFileInfo>
#include <QScopeGuard>
#include <QStatusBar>
#include <QTimer>

#if defined(PATCHY_HAVE_QT_PDF)
#include "ui/pdf_viewer_window.hpp"
#endif

#include <algorithm>

namespace patchy::ui {

namespace {

[[maybe_unused]] bool same_file(const QString& a, const QString& b) {
  const QFileInfo left(a);
  const QFileInfo right(b);
  const QString left_path = left.canonicalFilePath().isEmpty() ? left.absoluteFilePath() : left.canonicalFilePath();
  const QString right_path =
      right.canonicalFilePath().isEmpty() ? right.absoluteFilePath() : right.canonicalFilePath();
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  return left_path.compare(right_path, Qt::CaseInsensitive) == 0;
#else
  return left_path == right_path;
#endif
}

}  // namespace

bool MainWindow::open_pdf_viewer(const QString& path) {
#if defined(PATCHY_HAVE_QT_PDF)
  std::erase_if(pdf_viewers_, [](const QPointer<QWidget>& viewer) { return viewer.isNull(); });
  for (const auto& entry : pdf_viewers_) {
    auto* viewer = qobject_cast<PdfViewerWindow*>(entry.data());
    if (viewer != nullptr && same_file(viewer->path(), path)) {
      if (viewer->isMinimized()) {
        viewer->showNormal();
      }
      viewer->raise();
      viewer->activateWindow();
      return true;
    }
  }
  auto* viewer = PdfViewerWindow::open(
      this, path, [this](PdfViewerWindow& window, const std::vector<int>& pages, const QString& password) {
        return import_pdf_pages_from_viewer(&window, window.path(), pages, password);
      });
  if (viewer == nullptr) {
    return false;
  }
  // Files dropped on the viewer open as if dropped on the main window: a PDF in its own
  // viewer, anything else as a document. The main window then comes forward to show it
  // (the opens run after the native drop returns, so this check queues behind them).
  viewer->set_file_drop_handlers([this](QDropEvent* event) { return accept_open_file_drag(event); },
                                 [this](QDropEvent* event) {
                                   const auto* before = active_session();
                                   const auto before_id = before != nullptr ? before->session_id : 0;
                                   if (!open_dropped_files(event)) {
                                     return false;
                                   }
                                   QTimer::singleShot(0, this, [this, before_id] {
                                     const auto* after = active_session();
                                     if (shutting_down_ || after == nullptr || after->session_id == before_id) {
                                       return;
                                     }
                                     if (isMinimized()) {
                                       showNormal();
                                     }
                                     raise();
                                     activateWindow();
                                   });
                                   return true;
                                 });
  pdf_viewers_.emplace_back(viewer);
  viewer->show();
  viewer->raise();
  viewer->activateWindow();
  statusBar()->showMessage(tr("%1 opened in the PDF viewer").arg(QFileInfo(path).fileName()));
  return true;
#else
  Q_UNUSED(path);
  return false;
#endif
}

bool MainWindow::import_pdf_pages_from_viewer(QWidget* viewer, const QString& path, const std::vector<int>& pages,
                                              const QString& password) {
  PdfImportDialogStart start;
  start.pages = pages;
  start.password = password;
  pdf_import_start_ = &start;
  pdf_import_dialog_parent_ = viewer;
  const auto reset = qScopeGuard([this] {
    pdf_import_start_ = nullptr;
    pdf_import_dialog_parent_ = nullptr;
  });
  const auto* before = active_session();
  const auto before_id = before != nullptr ? before->session_id : 0;
  // A second copy even when a page of this file is open already: Edit... asks for an import.
  open_document_path(path, OpenExisting::LoadCopy, PdfOpen::Import);
  const auto* after = active_session();
  const bool opened = after != nullptr && after->session_id != before_id;
  if (opened) {
    if (isMinimized()) {
      showNormal();
    }
    raise();
    activateWindow();
  }
  return opened;
}

}  // namespace patchy::ui
