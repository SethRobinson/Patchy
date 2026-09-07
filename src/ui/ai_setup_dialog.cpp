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
  const auto body_font = scaled_font(font(), 1.2);
  const auto prompt_font = scaled_font(body_font, 1.1);
  setFont(body_font);
  resize(840, 740);
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  auto* content = install_dark_dialog_chrome(*this, root, tr("Set up AI Control"));

  auto* intro = new QLabel(
      tr("First, copy the setup prompt into your AI assistant (Claude Code, Codex, Cursor, "
         "or another tool that supports MCP). You only need to set up Patchy once. "
         "The setup prompt is in English because it is written for the assistant."),
      this);
  intro->setObjectName(QStringLiteral("aiSetupIntroLabel"));
  intro->setWordWrap(true);
  content->addWidget(intro);

  blurb_ = new QPlainTextEdit(ai_setup_blurb_text(paths_), this);
  blurb_->setObjectName(QStringLiteral("aiSetupBlurbText"));
  blurb_->setReadOnly(true);
  // Prose for a person to skim before pasting, so the dialog font a step larger
  // rather than the small monospace face used for command lines.
  blurb_->setFont(prompt_font);
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
  content->addWidget(blurb_, 3);
  auto* setup_buttons = new QHBoxLayout();
  copy_button_ = new QPushButton(tr("Copy Setup Prompt"), this);
  copy_button_->setObjectName(QStringLiteral("aiSetupCopyButton"));
  copy_button_->setDefault(true);
  connect(copy_button_, &QPushButton::clicked, this, &AiSetupDialog::copy_blurb);
  setup_buttons->addWidget(copy_button_);
  setup_buttons->addStretch(1);
  content->addLayout(setup_buttons);

  auto* examples_intro = new QLabel(
      tr("After setup, try an example prompt. Choose one below, then copy it into your "
         "assistant. You can change tasks or workspaces without installing again."), this);
  examples_intro->setObjectName(QStringLiteral("aiSetupExamplesLabel"));
  examples_intro->setWordWrap(true);
  content->addWidget(examples_intro);
  auto* examples = new QComboBox(this);
  examples->setObjectName(QStringLiteral("aiSetupExamplesComboBox"));
  examples->setAccessibleName(tr("Example prompts"));
  examples->addItem(tr("Fix the face in my open document"),
      tr("Edit the document I have open in Patchy. Make the face cuter on a separate "
         "correction layer, keep the other layers, and show me a before-and-after preview."));
  examples->addItem(tr("Create pixel art while I watch"),
      tr("Create a cute 64x64 pixel-art animal in a visible Patchy window so I can watch. "
         "Use editable layers, inspect the preview and refine it, then save a layered PSD "
         "and a 64x64 PNG."));
  examples->addItem(tr("Make icons in the background"),
      tr("Use Patchy in the background, without opening a window, to create three matching "
         "32x32 app icons: a folder, a paintbrush, and a heart. Give them transparent "
         "backgrounds and show me the previews and saved PNG files."));
  examples->addItem(tr("Turn a reference image into artwork"),
      tr("Use the image I attach as a reference for a cute 64x64 portrait in Patchy. "
         "Keep its recognizable features, compare your preview with the reference as "
         "you refine it, and save both an editable PSD and a PNG."));
  examples->addItem(tr("Export sizes from my open document"),
      tr("Use my open Patchy document to export transparent PNG copies at 64x64, 128x128, "
         "and 256x256. Preserve the proportions, leave the original document unchanged, "
         "and show me the exported files."));
  examples->addItem(tr("Make a contact sheet in the background"),
      tr("Use Patchy in the background to make a labeled contact sheet from a folder of "
         "images. Ask me which folder if I have not provided one, keep the original "
         "files unchanged, and show me the finished sheet."));
  examples->addItem(tr("Review my open document"),
      tr("Look at the document I have open in Patchy and suggest three specific "
         "improvements to its composition and colors. Show me the preview before "
         "making any edits."));
  content->addWidget(examples);
  auto* example_text = new QPlainTextEdit(this);
  example_text->setObjectName(QStringLiteral("aiSetupExampleText"));
  example_text->setAccessibleName(tr("Example prompt"));
  example_text->setReadOnly(true);
  example_text->setFont(blurb_->font());
  set_themed_style(*example_text, themed_style_template(*blurb_));
  content->addWidget(example_text, 1);
  auto* example_buttons = new QHBoxLayout();
  auto* example_copy = new QPushButton(tr("Copy Example Prompt"), this);
  example_copy->setObjectName(QStringLiteral("aiSetupExampleCopyButton"));
  const auto update_example = [examples, example_text, example_copy] {
    example_text->setPlainText(examples->currentData().toString());
    example_copy->setText(tr("Copy Example Prompt"));
  };
  connect(examples, &QComboBox::currentIndexChanged, this, update_example);
  update_example();
  connect(example_copy, &QPushButton::clicked, this, [example_text, example_copy] {
    QGuiApplication::clipboard()->setText(example_text->toPlainText());
    example_copy->setText(tr("Copied"));
    QTimer::singleShot(1200, example_copy,
        [example_copy] { example_copy->setText(tr("Copy Example Prompt")); });
  });
  example_buttons->addWidget(example_copy);
  example_buttons->addStretch(1);
  content->addLayout(example_buttons);

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

  // MainWindow's inherited QWidget rule pins text to 12px. A local font rule
  // must override it; setFont alone is overridden when the dialog is polished.
  const auto css_size = [](const QFont& font) {
    return font.pixelSize() > 0 ? QStringLiteral("%1px").arg(font.pixelSize())
                               : QStringLiteral("%1pt").arg(font.pointSizeF(), 0, 'f', 2);
  };
  append_themed_style(*this, QStringLiteral(
      "QWidget { font-size: %1; } QPlainTextEdit { font-size: %2; }")
      .arg(css_size(body_font), css_size(prompt_font)));
}

QString AiSetupDialog::blurb_text() const { return blurb_->toPlainText(); }

void AiSetupDialog::copy_blurb() {
  QGuiApplication::clipboard()->setText(blurb_->toPlainText());
  copy_button_->setText(tr("Copied"));
  QTimer::singleShot(1200, copy_button_,
                     [button = copy_button_] { button->setText(tr("Copy Setup Prompt")); });
  emit blurb_copied();
}

}  // namespace patchy::ui
