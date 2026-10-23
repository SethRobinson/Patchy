#include "ui/model_download_dialog.hpp"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLocale>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace patchy::ui {
namespace {

QString megabytes_text(qint64 bytes) {
  return QLocale().toString(static_cast<double>(bytes) / 1000000.0, 'f', bytes >= 10000000 ? 0 : 1);
}

}  // namespace

ElidedLinkLabel::ElidedLinkLabel(QWidget* parent) : QLabel(parent) {
  setObjectName(QStringLiteral("modelDownloadLinkLabel"));
  setWordWrap(false);
  setTextFormat(Qt::PlainText);
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
}

void ElidedLinkLabel::set_link(const QString& link) {
  link_ = link;
  setToolTip(link);
  update_text();
}

void ElidedLinkLabel::copy_link() const {
  if (!link_.isEmpty()) {
    QApplication::clipboard()->setText(link_);
  }
}

void ElidedLinkLabel::resizeEvent(QResizeEvent* event) {
  QLabel::resizeEvent(event);
  update_text();
}

void ElidedLinkLabel::contextMenuEvent(QContextMenuEvent* event) {
  QMenu menu(this);
  menu.setObjectName(QStringLiteral("modelDownloadLinkMenu"));
  auto* copy = menu.addAction(QCoreApplication::translate("ModelDownloadDialog", "Copy Link"));
  copy->setEnabled(!link_.isEmpty());
  if (menu.exec(event->globalPos()) == copy) {
    copy_link();
  }
}

void ElidedLinkLabel::update_text() {
  const auto shown = fontMetrics().elidedText(link_, Qt::ElideMiddle, std::max(1, contentsRect().width()));
  elided_ = shown != link_;
  // Selecting an elided text would copy the shortened link; then only Copy Link copies.
  setTextInteractionFlags(elided_ ? Qt::NoTextInteraction : Qt::TextSelectableByMouse);
  if (text() != shown) {
    setText(shown);
  }
}

ModelDownloadDialog::ModelDownloadDialog(QWidget* parent) : QDialog(parent) {
  setWindowModality(Qt::WindowModal);
  setWindowTitle(QCoreApplication::translate("ModelDownloadDialog", "Downloading"));
  auto* layout = new QVBoxLayout(this);
  layout->setSpacing(6);
  heading_ = new QLabel(this);
  heading_->setObjectName(QStringLiteral("modelDownloadHeadingLabel"));
  heading_->setWordWrap(true);
  layout->addWidget(heading_);
  file_ = new QLabel(this);
  file_->setObjectName(QStringLiteral("modelDownloadFileLabel"));
  file_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(file_);
  auto* from = new QLabel(QCoreApplication::translate("ModelDownloadDialog", "From:"), this);
  from->setObjectName(QStringLiteral("modelDownloadFromLabel"));
  link_ = new ElidedLinkLabel(this);
  auto* link_row = new QHBoxLayout();
  link_row->addWidget(from);
  link_row->addWidget(link_, 1);
  layout->addLayout(link_row);
  bar_ = new QProgressBar(this);
  bar_->setObjectName(QStringLiteral("modelDownloadProgressBar"));
  bar_->setRange(0, 1000);
  bar_->setValue(0);
  bar_->setTextVisible(false);
  layout->addWidget(bar_);
  auto* buttons = new QHBoxLayout();
  copy_ = new QPushButton(QCoreApplication::translate("ModelDownloadDialog", "Copy Link"), this);
  copy_->setObjectName(QStringLiteral("modelDownloadCopyLinkButton"));
  copy_->setAutoDefault(false);
  copy_->setEnabled(false);
  connect(copy_, &QPushButton::clicked, this, [this] { link_->copy_link(); });
  buttons->addWidget(copy_);
  buttons->addStretch(1);
  auto* cancel = new QPushButton(QCoreApplication::translate("ModelDownloadDialog", "Cancel"), this);
  cancel->setObjectName(QStringLiteral("modelDownloadCancelButton"));
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  buttons->addWidget(cancel);
  layout->addLayout(buttons);
  // Wide enough for a GitHub release link to show whole at the usual font sizes.
  resize(std::max(sizeHint().width(), 700), sizeHint().height());
}

void ModelDownloadDialog::set_heading(const QString& text) { heading_->setText(text); }

void ModelDownloadDialog::set_progress(const ModelDownloadProgress& progress) {
  if (progress.file_count > 0 && !progress.file_name.isEmpty()) {
    file_->setText(QCoreApplication::translate("ModelDownloadDialog", "File %1 of %2: %3 (%4 MB)")
                       .arg(progress.file_number)
                       .arg(progress.file_count)
                       .arg(progress.file_name, megabytes_text(progress.file_size)));
  }
  if (progress.url.isValid()) {
    link_->set_link(progress.url.isLocalFile() ? progress.url.toLocalFile() : progress.url.toString());
  }
  copy_->setEnabled(!link_->link().isEmpty());
  bar_->setValue(progress.total > 0 ? static_cast<int>(progress.received * 1000 / progress.total) : 0);
}

QString ModelDownloadDialog::heading_text() const { return heading_->text(); }
QString ModelDownloadDialog::file_text() const { return file_->text(); }

}  // namespace patchy::ui
