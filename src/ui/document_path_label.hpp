#pragma once

// The Properties panel's "File:" line (docs/document-tabs.md, "Full path"): the active
// document's full path, elided in the middle when the panel is too narrow. The full path
// stays in the tooltip and the context menu's Copy File Path; the line is selectable
// while it is shown whole.

#include <QLabel>
#include <QString>

class QContextMenuEvent;
class QResizeEvent;

namespace patchy::ui {

class DocumentPathLabel final : public QLabel {
public:
  explicit DocumentPathLabel(QWidget* parent = nullptr);
  // `prefix` is the translated "File: %1" form. An empty `path` shows `fallback` (for example
  // "Not saved yet") with nothing to copy.
  void set_path(const QString& prefix, const QString& path, const QString& fallback);
  [[nodiscard]] const QString& path() const noexcept { return path_; }
  [[nodiscard]] bool elided() const noexcept { return elided_; }
  void copy_path() const;

protected:
  void resizeEvent(QResizeEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;

private:
  void update_text();

  QString prefix_;
  QString path_;
  QString fallback_;
  bool elided_{false};
};

}  // namespace patchy::ui
