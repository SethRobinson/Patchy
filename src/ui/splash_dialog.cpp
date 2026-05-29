#include "ui/splash_dialog.hpp"

#include "ui/app_settings.hpp"

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPushButton>
#include <QScreen>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#ifndef PATCHY_VERSION
#define PATCHY_VERSION "0.0.0"
#endif

namespace patchy::ui {
namespace {

class SplashArtwork final : public QWidget {
public:
  explicit SplashArtwork(QWidget* parent = nullptr) : QWidget(parent) {
    setMinimumSize(190, 230);
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF bounds = rect().adjusted(10, 14, -10, -14);
    QLinearGradient glow(bounds.topLeft(), bounds.bottomRight());
    glow.setColorAt(0.0, QColor(88, 170, 235));
    glow.setColorAt(0.58, QColor(132, 214, 169));
    glow.setColorAt(1.0, QColor(242, 177, 92));
    painter.setPen(Qt::NoPen);
    painter.setBrush(glow);
    painter.drawRoundedRect(bounds, 28, 28);

    painter.setBrush(QColor(22, 29, 39, 230));
    painter.drawRoundedRect(bounds.adjusted(9, 9, -9, -9), 22, 22);

    const QRectF canvas(bounds.left() + 34, bounds.top() + 30, bounds.width() - 68, bounds.height() - 74);
    painter.setPen(QPen(QColor(231, 237, 245), 3));
    painter.setBrush(QColor(247, 249, 252));
    painter.drawRoundedRect(canvas, 16, 16);

    const QRectF patch_a(canvas.left() + 20, canvas.top() + 20, 54, 46);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(88, 170, 235));
    painter.drawRoundedRect(patch_a, 11, 11);

    const QRectF patch_b(canvas.right() - 76, canvas.center().y() - 18, 58, 50);
    painter.setBrush(QColor(132, 214, 169));
    painter.drawRoundedRect(patch_b, 12, 12);

    QPainterPath cut;
    cut.moveTo(canvas.left() + 38, canvas.bottom() - 46);
    cut.lineTo(canvas.left() + 78, canvas.bottom() - 70);
    cut.lineTo(canvas.left() + 122, canvas.bottom() - 38);
    cut.lineTo(canvas.left() + 84, canvas.bottom() - 18);
    cut.closeSubpath();
    painter.setBrush(QColor(242, 177, 92));
    painter.drawPath(cut);

    painter.setPen(QPen(QColor(24, 31, 42), 5, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(canvas.left() + 36, canvas.bottom() + 14),
                     QPointF(canvas.right() - 18, canvas.bottom() + 14));
    painter.setPen(QPen(QColor(232, 238, 246), 3, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(canvas.left() + 44, canvas.bottom() + 14),
                     QPointF(canvas.right() - 26, canvas.bottom() + 14));
  }
};

class PatchySplashDialog final : public QDialog {
public:
  enum class Mode { Startup, About };

  explicit PatchySplashDialog(Mode mode, QWidget* parent = nullptr) : QDialog(parent) {
    setObjectName(QStringLiteral("patchySplashScreen"));
    setAttribute(Qt::WA_DeleteOnClose, mode == Mode::Startup);
    setWindowFlags(mode == Mode::Startup ? Qt::SplashScreen | Qt::FramelessWindowHint
                                         : Qt::Dialog | Qt::FramelessWindowHint);
    setModal(mode == Mode::About);
    setFixedSize(mode == Mode::About ? 650 : 590, mode == Mode::About ? 410 : 330);
    setStyleSheet(QStringLiteral(R"(
      QDialog#patchySplashScreen {
        background: #171d26;
        border: 1px solid #3b4655;
      }
      QLabel#splashTitle {
        color: #f5f8fb;
        font-size: 42px;
        font-weight: 800;
      }
      QLabel#splashSubtitle {
        color: #c9d5e2;
        font-size: 15px;
      }
      QLabel#splashCredit {
        color: #edf3f8;
        font-size: 13px;
      }
      QLabel#splashHome {
        color: #edf3f8;
        font-size: 13px;
      }
      QLabel#splashStatus {
        color: #93a2b3;
        font-size: 12px;
      }
      QLabel#splashSettingsCaption {
        color: #edf3f8;
        font-size: 12px;
        font-weight: 700;
      }
      QLabel#splashSettingsPath {
        color: #b7c3d2;
        font-size: 11px;
      }
      QPushButton#splashOpenSettingsFolderButton {
        background: #263242;
        color: #edf3f8;
        border: 1px solid #536277;
        padding: 5px 12px;
        min-width: 120px;
      }
      QPushButton#splashOpenSettingsFolderButton:hover {
        background: #304057;
      }
      QPushButton#splashCloseButton {
        background: #2f7fc1;
        color: #ffffff;
        border: 1px solid #63a9df;
        padding: 5px 18px;
        min-width: 74px;
      }
      QPushButton#splashCloseButton:hover {
        background: #358fd9;
      }
    )"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(28, 26, 30, 24);
    layout->setSpacing(28);

    auto* artwork = new SplashArtwork(this);
    artwork->setFixedSize(210, 270);
    layout->addWidget(artwork);

    auto* copy = new QVBoxLayout();
    copy->setContentsMargins(0, 12, 0, 6);
    copy->setSpacing(10);
    layout->addLayout(copy, 1);

    auto* title = new QLabel(QObject::tr("Patchy"), this);
    title->setObjectName(QStringLiteral("splashTitle"));
    title->setTextFormat(Qt::PlainText);
    copy->addWidget(title);

    auto* subtitle = new QLabel(QObject::tr("Open source photo editing. No subscriptions, no gatekeeping."), this);
    subtitle->setObjectName(QStringLiteral("splashSubtitle"));
    subtitle->setTextFormat(Qt::PlainText);
    subtitle->setWordWrap(true);
    copy->addWidget(subtitle);

    auto* divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(QStringLiteral("color: #3b4655; background: #3b4655;"));
    copy->addWidget(divider);

    auto* version = new QLabel(QObject::tr("Version %1").arg(QStringLiteral(PATCHY_VERSION)), this);
    version->setObjectName(QStringLiteral("splashCredit"));
    version->setTextFormat(Qt::PlainText);
    copy->addWidget(version);

    auto* credit = new QLabel(QObject::tr("Created by Seth A. Robinson"), this);
    credit->setObjectName(QStringLiteral("splashCredit"));
    credit->setTextFormat(Qt::PlainText);
    copy->addWidget(credit);

    auto add_home_link = [this, copy](const QString& text) {
      auto* label = new QLabel(text, this);
      label->setObjectName(QStringLiteral("splashHome"));
      label->setTextFormat(Qt::RichText);
      label->setTextInteractionFlags(Qt::TextBrowserInteraction);
      label->setOpenExternalLinks(true);
      copy->addWidget(label);
    };
    const auto github_link = QStringLiteral("<a style=\"color:#9ed0ff; text-decoration:none;\" "
                                            "href=\"https://github.com/SethRobinson/Patchy\">SethRobinson/Patchy</a>");
    add_home_link(QObject::tr("GitHub: %1").arg(github_link));
    const auto seth_site_link = QStringLiteral("<a style=\"color:#9ed0ff; text-decoration:none;\" "
                                               "href=\"https://rtsoft.com\">rtsoft.com</a>");
    add_home_link(QObject::tr("Seth's site: %1").arg(seth_site_link));

    if (mode == Mode::About) {
      auto settings = app_settings();
      const auto settings_file_path = settings.fileName();
      const QFileInfo settings_file_info(settings_file_path);
      const auto settings_dir_path = settings_file_info.absolutePath();

      auto* settings_caption = new QLabel(QObject::tr("Settings file:"), this);
      settings_caption->setObjectName(QStringLiteral("splashSettingsCaption"));
      settings_caption->setTextFormat(Qt::PlainText);
      copy->addWidget(settings_caption);

      auto* settings_path = new QLabel(QDir::toNativeSeparators(settings_file_path), this);
      settings_path->setObjectName(QStringLiteral("splashSettingsPath"));
      settings_path->setTextFormat(Qt::PlainText);
      settings_path->setTextInteractionFlags(Qt::TextSelectableByMouse);
      settings_path->setWordWrap(true);
      copy->addWidget(settings_path);

      auto* settings_button_row = new QHBoxLayout();
      settings_button_row->setContentsMargins(0, 0, 0, 0);
      auto* open_settings_folder = new QPushButton(QObject::tr("Open Settings Folder"), this);
      open_settings_folder->setObjectName(QStringLiteral("splashOpenSettingsFolderButton"));
      connect(open_settings_folder, &QPushButton::clicked, this, [this, settings_dir_path] {
        if (settings_dir_path.isEmpty() || !QDir().mkpath(settings_dir_path) ||
            !QDesktopServices::openUrl(QUrl::fromLocalFile(settings_dir_path))) {
          auto* status = findChild<QLabel*>(QStringLiteral("splashStatus"));
          if (status != nullptr) {
            status->setText(QObject::tr("Could not open settings folder."));
          }
        }
      });
      settings_button_row->addWidget(open_settings_folder, 0);
      settings_button_row->addStretch(1);
      copy->addLayout(settings_button_row);
    }

    copy->addStretch(1);

    auto* bottom = new QHBoxLayout();
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->setSpacing(12);
    copy->addLayout(bottom);

    auto* status = new QLabel(mode == Mode::Startup ? QObject::tr("Starting workspace...")
                                                    : QObject::tr("Patchy is ready."),
                              this);
    status->setObjectName(QStringLiteral("splashStatus"));
    status->setTextFormat(Qt::PlainText);
    bottom->addWidget(status, 1);

    if (mode == Mode::About) {
      auto* close = new QPushButton(QObject::tr("Close"), this);
      close->setObjectName(QStringLiteral("splashCloseButton"));
      connect(close, &QPushButton::clicked, this, &QDialog::accept);
      bottom->addWidget(close, 0);
    }
  }
};

void center_on_screen(QWidget* widget, QWidget* parent) {
  if (widget == nullptr) {
    return;
  }

  QRect area;
  if (parent != nullptr && parent->window() != nullptr) {
    area = parent->window()->frameGeometry();
  } else if (auto* screen = QApplication::primaryScreen(); screen != nullptr) {
    area = screen->availableGeometry();
  }

  if (!area.isEmpty()) {
    widget->move(area.center() - widget->rect().center());
  }
}

}  // namespace

void show_startup_splash(QWidget* parent) {
  auto* splash = new PatchySplashDialog(PatchySplashDialog::Mode::Startup, parent);
  center_on_screen(splash, parent);
  splash->show();
  splash->raise();
  QTimer::singleShot(3500, splash, &QWidget::close);
}

void show_about_splash(QWidget* parent) {
  PatchySplashDialog splash(PatchySplashDialog::Mode::About, parent);
  center_on_screen(&splash, parent);
  splash.exec();
}

}  // namespace patchy::ui
