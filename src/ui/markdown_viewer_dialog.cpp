// Read-only Markdown viewer dialog (markdown_viewer_dialog.hpp): QTextBrowser
// does the rendering (setSource detects .md and imports it as Markdown, and
// resolves relative image paths against the file), so the shipped scripting
// guide displays from the same file GitHub renders.

#include "ui/markdown_viewer_dialog.hpp"

#include <QColor>
#include <QFileInfo>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QUrl>
#include <QVBoxLayout>

namespace patchy::ui {

namespace {

// The Markdown importer colors links with the default palette's dark blue,
// which is unreadable on the dark theme; repaint anchor fragments in the
// app's accent blue (the run-spinner color).
void recolor_anchors(QTextDocument& document) {
  for (QTextBlock block = document.begin(); block != document.end(); block = block.next()) {
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const QTextFragment fragment = it.fragment();
      if (!fragment.isValid() || !fragment.charFormat().isAnchor()) {
        continue;
      }
      QTextCursor cursor(&document);
      cursor.setPosition(fragment.position());
      cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
      QTextCharFormat format;
      format.setForeground(QColor(0x6f, 0xb1, 0xe8));
      cursor.mergeCharFormat(format);
    }
  }
}

}  // namespace

MarkdownViewerDialog::MarkdownViewerDialog(QWidget* parent) : QDialog(parent) {
  setObjectName(QStringLiteral("markdownViewerDialog"));
  resize(780, 820);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  browser_ = new QTextBrowser(this);
  browser_->setObjectName(QStringLiteral("markdownViewerBrowser"));
  browser_->setOpenExternalLinks(true);
  layout->addWidget(browser_);
}

bool MarkdownViewerDialog::load_file(const QString& path) {
  if (path.isEmpty() || !QFileInfo::exists(path)) {
    return false;
  }
  browser_->setSource(QUrl::fromLocalFile(path));
  browser_->document()->setDocumentMargin(18);
  recolor_anchors(*browser_->document());
  return true;
}

}  // namespace patchy::ui
