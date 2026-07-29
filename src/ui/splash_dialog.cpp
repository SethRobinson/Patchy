#include "ui/splash_dialog.hpp"

#include "ui/app_credits.hpp"
#include "ui/app_settings.hpp"
#include "ui/build_info.hpp"
#include "ui/splash_artwork.hpp"
#include "ui/update_checker.hpp"
#include "ui/theme_qss.hpp"
#include "ui/window_effects.hpp"

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
    apply_frameless_window_effects_on_show(*this, WindowCornerRadius::Standard);
    setModal(true);
    setFixedSize(650, 435);
    set_themed_style(*this, QStringLiteral(R"(
      QDialog#patchySplashScreen {
        background: @splash_bg;
        border: 1px solid @splash_border;
      }
      QLabel#splashTitle {
        color: @splash_title_text;
        font-size: 32px;
        font-weight: 800;
      }
      QLabel#splashSubtitle {
        color: @splash_subtitle_text;
        font-size: 15px;
      }
      QLabel#splashCredit {
        color: @splash_body_text;
        font-size: 13px;
      }
      QLabel#splashContributors {
        color: @splash_body_text;
        font-size: 13px;
      }
      QLabel#splashHome {
        color: @splash_body_text;
        font-size: 13px;
      }
      QLabel#splashStatus {
        color: @splash_status_text;
        font-size: 12px;
      }
      QLabel#splashSettingsCaption {
        color: @splash_body_text;
        font-size: 12px;
        font-weight: 700;
      }
      QLabel#splashSettingsPath {
        color: @splash_caption_text;
        font-size: 11px;
      }
      QPushButton#splashOpenSettingsFolderButton {
        background: @splash_button_bg;
        color: @splash_body_text;
        border: 1px solid @splash_button_border;
        padding: 5px 12px;
        min-width: 120px;
      }
      QPushButton#splashOpenSettingsFolderButton:hover {
        background: @splash_button_hover_bg;
      }
      QPushButton#splashCloseButton {
        background: @splash_primary_bg;
        color: @text_on_accent;
        border: 1px solid @splash_primary_border;
        padding: 5px 18px;
        min-width: 74px;
      }
      QPushButton#splashCloseButton:hover {
        background: @splash_primary_hover_bg;
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

    auto* title = new QLabel(QObject::tr("Patchy Image Editor"), this);
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
    set_themed_style(*divider, QStringLiteral("color: @splash_border; background: @splash_border;"));
    copy->addWidget(divider);

    auto* version = new QLabel(
        QObject::tr("Version %1 (built %2)").arg(QStringLiteral(PATCHY_VERSION), build_date_text()), this);
    version->setObjectName(QStringLiteral("splashCredit"));
    version->setTextFormat(Qt::PlainText);
    copy->addWidget(version);

    auto* credit = new QLabel(QObject::tr("Created by Seth A. Robinson"), this);
    credit->setObjectName(QStringLiteral("splashCredit"));
    credit->setTextFormat(Qt::PlainText);
    copy->addWidget(credit);

    auto* contributors = new QLabel(
        QObject::tr("Code contributions from %1").arg(code_contributors_link_html(QStringLiteral("@splash_link_text"))),
        this);
    contributors->setObjectName(QStringLiteral("splashContributors"));
    contributors->setTextFormat(Qt::RichText);
    contributors->setTextInteractionFlags(Qt::TextBrowserInteraction);
    contributors->setOpenExternalLinks(true);
    copy->addWidget(contributors);

    auto add_home_link = [this, copy](const QString& text) {
      auto* label = new QLabel(text, this);
      label->setObjectName(QStringLiteral("splashHome"));
      label->setTextFormat(Qt::RichText);
      label->setTextInteractionFlags(Qt::TextBrowserInteraction);
      label->setOpenExternalLinks(true);
      copy->addWidget(label);
    };
    const auto github_link = QStringLiteral("<a style=\"color:@splash_link_text; text-decoration:none;\" "
                                            "href=\"https://github.com/SethRobinson/Patchy\">SethRobinson/Patchy</a>");
    add_home_link(QObject::tr("GitHub: %1").arg(github_link));
    const auto seth_site_link = QStringLiteral("<a style=\"color:@splash_link_text; text-decoration:none;\" "
                                               "href=\"https://rtsoft.com\">rtsoft.com</a>");
    add_home_link(QObject::tr("Seth's site: %1").arg(seth_site_link));

#ifndef Q_OS_WASM
    // The wasm settings store is window.localStorage, so there is no settings
    // file to display and no folder a file manager could open.
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
#endif

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

#ifndef Q_OS_WASM
  void begin_update_check() {
    set_status(QObject::tr("Checking for updates..."));
    const QPointer<PatchySplashDialog> dialog_guard(this);
    request_update_check(this, QStringLiteral(PATCHY_VERSION), [dialog_guard](UpdateCheckResult result) {
      if (dialog_guard != nullptr) {
        dialog_guard->set_status(update_check_status_text(result));
      }
    });
  }
#endif

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
#ifndef Q_OS_WASM
  // The web build always runs the latest deployed site, so there is no update
  // to check for; the status label keeps its "Patchy is ready." text.
  splash.begin_update_check();
#endif
  splash.exec();
}

}  // namespace patchy::ui
