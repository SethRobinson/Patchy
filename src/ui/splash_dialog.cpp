#include "ui/splash_dialog.hpp"

#include "ui/app_settings.hpp"
#include "ui/splash_artwork.hpp"
#include "ui/update_checker.hpp"

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QPointer>
#include <QScreen>
#include <QString>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#ifndef PATCHY_VERSION
#define PATCHY_VERSION "0.0.0"
#endif

namespace patchy::ui {
namespace {

// The modal Help > About dialog. Startup no longer shows a splash: the start
// panel carries the branding and the startup update check lives in MainWindow.
class PatchySplashDialog final : public QDialog {
public:
  explicit PatchySplashDialog(QWidget* parent = nullptr) : QDialog(parent) {
    setObjectName(QStringLiteral("patchySplashScreen"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);
    setFixedSize(650, 410);
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

    auto* subtitle = new QLabel(QObject::tr("Open source photo editing. Free forever, no subscriptions."), this);
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

    copy->addStretch(1);

    auto* bottom = new QHBoxLayout();
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->setSpacing(12);
    copy->addLayout(bottom);

    status_ = new QLabel(QObject::tr("Patchy is ready."), this);
    status_->setObjectName(QStringLiteral("splashStatus"));
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    bottom->addWidget(status_, 1);

    auto* close = new QPushButton(QObject::tr("Close"), this);
    close->setObjectName(QStringLiteral("splashCloseButton"));
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    bottom->addWidget(close, 0);
  }

  void begin_update_check() {
    set_status(QObject::tr("Checking for updates..."));
    const QPointer<PatchySplashDialog> dialog_guard(this);
    request_update_check(this, QStringLiteral(PATCHY_VERSION), [dialog_guard](UpdateCheckResult result) {
      if (dialog_guard != nullptr) {
        dialog_guard->set_status(update_check_status_text(result));
      }
    });
  }

  void set_status(const QString& text) {
    if (status_ != nullptr) {
      status_->setText(text);
    }
  }

private:
  QLabel* status_{nullptr};
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

void show_about_splash(QWidget* parent) {
  PatchySplashDialog splash(parent);
  center_on_screen(&splash, parent);
  splash.begin_update_check();
  splash.exec();
}

}  // namespace patchy::ui
