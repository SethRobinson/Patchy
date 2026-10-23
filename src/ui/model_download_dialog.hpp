#pragma once

// The progress dialog of every start_model_download transfer the user starts (the High
// Quality Object Selection model, the watermark models, a one-click update): what is being
// fetched, file N of M with its size, and the full link it comes from, so users can see and
// copy where the bytes come from (docs/ai-selection.md, "The High Quality model").

#include "ui/object_selection_model_download.hpp"

#include <QDialog>
#include <QLabel>
#include <QString>

class QContextMenuEvent;
class QProgressBar;
class QPushButton;
class QResizeEvent;

namespace patchy::ui {

// A one-line link: the middle is elided when it does not fit, the full link stays in the
// tooltip, the context menu's Copy Link and copy_link(). Selectable when shown whole.
class ElidedLinkLabel final : public QLabel {
public:
  explicit ElidedLinkLabel(QWidget* parent = nullptr);
  void set_link(const QString& link);
  [[nodiscard]] const QString& link() const noexcept { return link_; }
  [[nodiscard]] bool elided() const noexcept { return elided_; }
  void copy_link() const;

protected:
  void resizeEvent(QResizeEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;

private:
  void update_text();

  QString link_;
  bool elided_{false};
};

class ModelDownloadDialog final : public QDialog {
public:
  // Window-modal over `parent`. Cancel (and Escape) rejects: connect QDialog::rejected to the
  // job's cancel().
  explicit ModelDownloadDialog(QWidget* parent);

  // The line above the details, for example "Downloading the watermark models... 12 of 135 MB".
  void set_heading(const QString& text);
  void set_progress(const ModelDownloadProgress& progress);

  [[nodiscard]] QString heading_text() const;
  [[nodiscard]] QString file_text() const;
  [[nodiscard]] ElidedLinkLabel* link_label() const noexcept { return link_; }

private:
  QLabel* heading_{nullptr};
  QLabel* file_{nullptr};
  ElidedLinkLabel* link_{nullptr};
  QPushButton* copy_{nullptr};
  QProgressBar* bar_{nullptr};
};

}  // namespace patchy::ui
