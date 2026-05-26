#include "ui/dialog_utils.hpp"

#include <QAbstractSpinBox>
#include <QAction>
#include <QDialog>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

namespace photoslop::ui {

namespace {

QIcon dialog_close_icon() {
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(235, 238, 242), 2.0, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
  painter.drawLine(QPointF(10.0, 10.0), QPointF(22.0, 22.0));
  painter.drawLine(QPointF(22.0, 10.0), QPointF(10.0, 22.0));
  return QIcon(pixmap);
}

QString dialog_chrome_style() {
  return QStringLiteral(R"(
    QDialog {
      background: #262626;
      color: #e6e6e6;
      border: 1px solid #1f1f1f;
    }
    QWidget#dialogChromeTitleBar {
      background: #4f4f4f;
      border-bottom: 1px solid #343434;
      min-height: 34px;
      max-height: 34px;
    }
    QLabel#dialogChromeBadge {
      background: #001e36;
      border: 1px solid #1473e6;
      color: #31a8ff;
      font-size: 10px;
      font-weight: 700;
    }
    QLabel#dialogChromeTitleLabel {
      background: transparent;
      color: #f0f0f0;
      font-weight: 600;
    }
    QWidget#dialogChromeContent {
      background: #262626;
    }
    QToolButton#dialogChromeCloseButton {
      background: transparent;
      border: 0;
      border-radius: 0;
      padding: 0;
      min-width: 46px;
      max-width: 46px;
      min-height: 34px;
      max-height: 34px;
    }
    QToolButton#dialogChromeCloseButton:hover {
      background: #c42b1c;
      border: 0;
    }
    QToolButton#dialogChromeCloseButton:pressed {
      background: #9f2117;
    }
  )");
}

class DialogChromeDragFilter final : public QObject {
public:
  explicit DialogChromeDragFilter(QDialog& dialog, QObject* parent) : QObject(parent), dialog_(dialog) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    Q_UNUSED(watched);
    switch (event->type()) {
      case QEvent::MouseButtonPress: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::LeftButton) {
          drag_position_ = mouse_event->globalPosition().toPoint() - dialog_.frameGeometry().topLeft();
          dragging_ = true;
          if (auto* handle = dialog_.windowHandle(); handle != nullptr && handle->startSystemMove()) {
            dragging_ = false;
          }
          mouse_event->accept();
          return true;
        }
        break;
      }
      case QEvent::MouseMove: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (dragging_ && (mouse_event->buttons() & Qt::LeftButton) != 0) {
          if (!dialog_.isMaximized() && !dialog_.isFullScreen()) {
            dialog_.move(mouse_event->globalPosition().toPoint() - drag_position_);
          }
          mouse_event->accept();
          return true;
        }
        break;
      }
      case QEvent::MouseButtonRelease:
        dragging_ = false;
        break;
      default:
        break;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QDialog& dialog_;
  bool dragging_{false};
  QPoint drag_position_;
};

}  // namespace

void configure_toolbar_spinbox(QSpinBox* spin, int width) {
  spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
  spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  spin->setFixedWidth(width);
}

void configure_dialog_spinbox(QSpinBox* spin, int width) {
  spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
  spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  spin->setMinimumWidth(width);
  spin->setMinimumHeight(24);
}

QVBoxLayout* install_dark_dialog_chrome(QDialog& dialog, QVBoxLayout* root, const QString& title) {
  dialog.setWindowTitle(title);
  dialog.setWindowFlag(Qt::FramelessWindowHint, true);
  dialog.setStyleSheet(dialog.styleSheet() + dialog_chrome_style());
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* title_bar = new QWidget(&dialog);
  title_bar->setObjectName(QStringLiteral("dialogChromeTitleBar"));
  title_bar->setFixedHeight(34);
  title_bar->installEventFilter(new DialogChromeDragFilter(dialog, title_bar));
  auto* title_layout = new QHBoxLayout(title_bar);
  title_layout->setContentsMargins(9, 0, 0, 0);
  title_layout->setSpacing(8);

  auto* badge = new QLabel(QStringLiteral("Ps"), title_bar);
  badge->setObjectName(QStringLiteral("dialogChromeBadge"));
  badge->setAlignment(Qt::AlignCenter);
  badge->setFixedSize(18, 18);
  title_layout->addWidget(badge);

  auto* label = new QLabel(title, title_bar);
  label->setObjectName(QStringLiteral("dialogChromeTitleLabel"));
  title_layout->addWidget(label, 1);

  auto* close = new QToolButton(title_bar);
  close->setObjectName(QStringLiteral("dialogChromeCloseButton"));
  close->setAutoRaise(false);
  close->setFocusPolicy(Qt::NoFocus);
  close->setIcon(dialog_close_icon());
  close->setIconSize(QSize(16, 16));
  close->setToolTip(QObject::tr("Close"));
  close->setFixedSize(46, 34);
  title_layout->addWidget(close);
  QObject::connect(close, &QToolButton::clicked, &dialog, &QDialog::reject);

  auto* content = new QWidget(&dialog);
  content->setObjectName(QStringLiteral("dialogChromeContent"));
  auto* content_layout = new QVBoxLayout(content);
  content_layout->setContentsMargins(12, 12, 12, 12);
  content_layout->setSpacing(8);

  root->addWidget(title_bar);
  root->addWidget(content, 1);
  return content_layout;
}

int run_non_modal_dialog(QDialog& dialog) {
  dialog.setModal(false);
  dialog.setWindowModality(Qt::NonModal);
  QEventLoop loop;
  QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
  dialog.show();
  dialog.raise();
  dialog.activateWindow();
  loop.exec();
  return dialog.result();
}

void hide_menu_action_icons(QMenu* menu) {
  if (menu == nullptr) {
    return;
  }
  for (auto* action : menu->actions()) {
    action->setIconVisibleInMenu(false);
    if (auto* child_menu = action->menu(); child_menu != nullptr) {
      hide_menu_action_icons(child_menu);
    }
  }
}

}  // namespace photoslop::ui
