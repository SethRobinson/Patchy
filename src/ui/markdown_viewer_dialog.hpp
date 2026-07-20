#pragma once

#include <QDialog>
#include <QString>

class QTextBrowser;

namespace patchy::ui {

// A small reusable read-only Markdown viewer: QTextBrowser renders the file
// natively (headings, bold, tables, fenced code, and images resolved relative
// to the file's folder), so one shipped .md can serve as both a GitHub page
// and in-app help. External links open in the system browser. Currently used
// for the scripting guide (Help in the Script Manager and Help > Scripting
// Guide, via MainWindow::open_scripting_guide); non-modal callers go through
// run_non_modal_dialog as usual.
class MarkdownViewerDialog : public QDialog {
  Q_OBJECT

public:
  explicit MarkdownViewerDialog(QWidget* parent = nullptr);

  // Loads and renders the markdown file; false when it does not exist (the
  // caller reports; the dialog just stays empty).
  bool load_file(const QString& path);

private:
  QTextBrowser* browser_{nullptr};
};

}  // namespace patchy::ui
