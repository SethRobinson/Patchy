#include "ui/ai_setup_dialog.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/theme_qss.hpp"

#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace patchy::ui {

AiSetupDialog::AiSetupDialog(const AiControlPaths& paths, QWidget* parent)
    : QDialog(parent), paths_(paths) {
  setObjectName(QStringLiteral("aiSetupDialog"));
  setWindowTitle(tr("Set up AI Control"));
  resize(720, 540);
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  auto* content = install_dark_dialog_chrome(*this, root, tr("Set up AI Control"));

  auto* intro = new QLabel(
      tr("Copy the text below and paste it into your AI assistant (Claude Code, Codex, Cursor, "
         "or another tool that supports MCP). The assistant reads it and sets itself up to "
         "control Patchy. The text is in English because it is written for the assistant."),
      this);
  intro->setObjectName(QStringLiteral("aiSetupIntroLabel"));
  intro->setWordWrap(true);
  content->addWidget(intro);

  auto* mode_row = new QHBoxLayout();
  auto* mode_label = new QLabel(tr("AI workspace:"), this);
  auto* mode = new QComboBox(this);
  mode->setObjectName(QStringLiteral("aiSetupModeComboBox"));
  mode->addItem(tr("My open Patchy workspace"), static_cast<int>(AiWorkspaceMode::Attached));
  mode->addItem(tr("A separate visible window"), static_cast<int>(AiWorkspaceMode::Visible));
  mode->addItem(tr("A hidden workspace"), static_cast<int>(AiWorkspaceMode::Hidden));
  mode_label->setBuddy(mode);
  mode_row->addWidget(mode_label);
  mode_row->addWidget(mode, 1);
  content->addLayout(mode_row);
  auto* mode_hint = new QLabel(this);
  mode_hint->setObjectName(QStringLiteral("aiSetupModeHint"));
  mode_hint->setWordWrap(true);
  content->addWidget(mode_hint);

  blurb_ = new QPlainTextEdit(ai_setup_blurb_text(paths_), this);
  blurb_->setObjectName(QStringLiteral("aiSetupBlurbText"));
  blurb_->setReadOnly(true);
  // Prose for a person to skim before pasting, so the dialog font a step larger
  // rather than the small monospace face used for command lines.
  blurb_->setFont(scaled_font(font(), 1.2));
  blurb_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
  // The global sheet themes QTextEdit but not QPlainTextEdit; give it the same
  // field roles so it does not paint the platform's white box on the dark chrome.
  set_themed_style(*blurb_, QStringLiteral(R"(
    QPlainTextEdit {
      background: @field_bg_large;
      color: @text_primary;
      border: 1px solid @field_border;
      selection-background-color: @accent;
      selection-color: @text_on_accent;
    }
  )"));
  content->addWidget(blurb_, 1);
  const auto update_mode = [this, mode, mode_hint] {
    const auto selected = static_cast<AiWorkspaceMode>(mode->currentData().toInt());
    blurb_->setPlainText(ai_setup_blurb_text(paths_, selected));
    mode_hint->setText(selected == AiWorkspaceMode::Attached
        ? tr("The AI can inspect and edit your open documents, including unsaved changes. The status bar shows when it is connected or working. Open Patchy before connecting.")
        : selected == AiWorkspaceMode::Visible
        ? tr("Watch the AI work in a separate window. Save its documents before disconnecting or changing modes.")
        : tr("Work in the background and receive previews in chat. Save its documents before disconnecting or changing modes."));
  };
  connect(mode, &QComboBox::currentIndexChanged, this, update_mode);
  update_mode();

  status_ = new QLabel(this);
  status_->setObjectName(QStringLiteral("aiSetupStatusLabel"));
  status_->setWordWrap(true);
  status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QStringList status_lines;
  QStringList warnings;
  if (paths_.flatpak) {
    status_lines << tr("Connector: %1")
                        .arg(QStringLiteral("flatpak run --command=patchy-mcp %1")
                                 .arg(QString::fromLatin1(kFlatpakAppId)));
    status_lines << tr("Skill folder: %1").arg(QString::fromLatin1(kFlatpakSkillDirectory));
    warnings << tr("Patchy is running inside a Flatpak sandbox; the skill folder is only "
                   "visible from inside it.");
  } else {
    if (paths_.connector_path.isEmpty()) {
      warnings << tr("The patchy-mcp connector was not found next to Patchy. Reinstall Patchy or "
                     "download a full package.");
    } else {
      status_lines << tr("Connector: %1").arg(QDir::toNativeSeparators(paths_.connector_path));
    }
    if (paths_.skill_directory.isEmpty()) {
      warnings << tr("The patchy-control skill folder was not found. Reinstall Patchy or "
                     "download a full package.");
    } else {
      status_lines << tr("Skill folder: %1").arg(QDir::toNativeSeparators(paths_.skill_directory));
    }
  }
  QString status_html;
  for (const auto& line : status_lines) {
    status_html += line.toHtmlEscaped() + QStringLiteral("<br>");
  }
  for (const auto& warning : warnings) {
    status_html += QStringLiteral("<span style=\"color:@console_warning_text;\">") +
                   warning.toHtmlEscaped() + QStringLiteral("</span><br>");
  }
  if (status_html.endsWith(QStringLiteral("<br>"))) {
    status_html.chop(4);
  }
  set_themed_label_text(*status_, status_html);
  content->addWidget(status_);

  auto* buttons = new QHBoxLayout();
  copy_button_ = new QPushButton(tr("Copy to Clipboard"), this);
  copy_button_->setObjectName(QStringLiteral("aiSetupCopyButton"));
  copy_button_->setDefault(true);
  connect(copy_button_, &QPushButton::clicked, this, &AiSetupDialog::copy_blurb);
  buttons->addWidget(copy_button_);

  auto* open_skill = new QPushButton(tr("Open Skill Folder"), this);
  open_skill->setObjectName(QStringLiteral("aiSetupOpenSkillFolderButton"));
  open_skill->setEnabled(!paths_.flatpak && !paths_.skill_directory.isEmpty());
  connect(open_skill, &QPushButton::clicked, this, [this] {
    QDesktopServices::openUrl(QUrl::fromLocalFile(paths_.skill_directory));
  });
  buttons->addWidget(open_skill);

  auto* open_guide = new QPushButton(tr("Open Online Guide"), this);
  open_guide->setObjectName(QStringLiteral("aiSetupOpenGuideButton"));
  connect(open_guide, &QPushButton::clicked, this,
          [] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kAiControlSetupUrl))); });
  buttons->addWidget(open_guide);

  buttons->addStretch(1);
  auto* close = new QPushButton(tr("Close"), this);
  close->setObjectName(QStringLiteral("aiSetupCloseButton"));
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  buttons->addWidget(close);
  content->addLayout(buttons);
}

QString AiSetupDialog::blurb_text() const { return blurb_->toPlainText(); }

void AiSetupDialog::copy_blurb() {
  QGuiApplication::clipboard()->setText(blurb_->toPlainText());
  copy_button_->setText(tr("Copied"));
  QTimer::singleShot(1200, copy_button_,
                     [button = copy_button_] { button->setText(tr("Copy to Clipboard")); });
  emit blurb_copied();
}

}  // namespace patchy::ui
