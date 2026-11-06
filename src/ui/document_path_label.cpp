#include "ui/document_path_label.hpp"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QMenu>
#include <QResizeEvent>

#include <algorithm>

namespace patchy::ui {

DocumentPathLabel::DocumentPathLabel(QWidget* parent) : QLabel(parent) {
  setObjectName(QStringLiteral("documentPathLabel"));
  setWordWrap(false);
  setTextFormat(Qt::PlainText);
  // Ignored: a long path must never widen the panel; the text is elided to fit instead.
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Maximum);
}

void DocumentPathLabel::set_path(const QString& prefix, const QString& path, const QString& fallback) {
  if (prefix == prefix_ && path == path_ && fallback == fallback_) {
    return;
  }
  prefix_ = prefix;
  path_ = path;
  fallback_ = fallback;
  setToolTip(path);
  update_text();
}

void DocumentPathLabel::copy_path() const {
  if (!path_.isEmpty()) {
    QApplication::clipboard()->setText(path_);
  }
}

void DocumentPathLabel::resizeEvent(QResizeEvent* event) {
  QLabel::resizeEvent(event);
  update_text();
}

void DocumentPathLabel::contextMenuEvent(QContextMenuEvent* event) {
  QMenu menu(this);
  menu.setObjectName(QStringLiteral("documentPathLabelMenu"));
  // The document tab menu's own item, so both read (and translate) the same.
  auto* copy = menu.addAction(QCoreApplication::translate("patchy::ui::MainWindow", "Copy File Path"));
  copy->setEnabled(!path_.isEmpty());
  if (menu.exec(event->globalPos()) == copy) {
    copy_path();
  }
}

void DocumentPathLabel::update_text() {
  if (prefix_.isEmpty()) {
    elided_ = false;
    setText(QString());
    return;
  }
  const auto body = path_.isEmpty() ? fallback_ : path_;
  const auto metrics = fontMetrics();
  const auto prefix_width = metrics.horizontalAdvance(prefix_.arg(QString()));
  const auto room = std::max(1, contentsRect().width() - prefix_width);
  const auto shown = path_.isEmpty() ? body : metrics.elidedText(body, Qt::ElideMiddle, room);
  elided_ = shown != body;
  // Selecting an elided path would copy the shortened text; then only Copy File Path copies.
  setTextInteractionFlags(elided_ ? Qt::NoTextInteraction : Qt::TextSelectableByMouse);
  const auto text_now = prefix_.arg(shown);
  if (text() != text_now) {
    setText(text_now);
  }
}

}  // namespace patchy::ui
