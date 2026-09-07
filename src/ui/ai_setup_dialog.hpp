#pragma once

// Help > Set up AI Control. Shows the English text a user pastes into their AI
// assistant (Claude Code, Codex, Cursor, ...) so the assistant reads the shipped
// setup guide and configures Patchy's MCP connector and skill itself. The dialog
// also offers copyable task prompts independently of that one-time setup.
// Opened non-modally by MainWindow::open_ai_setup_dialog. See
// docs/ai-control.md.

#include "ui/ai_control_paths.hpp"

#include <QDialog>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace patchy::ui {

class AiSetupDialog : public QDialog {
  Q_OBJECT

public:
  explicit AiSetupDialog(const AiControlPaths& paths, QWidget* parent = nullptr);

  [[nodiscard]] QString blurb_text() const;

signals:
  void blurb_copied();

private:
  void copy_blurb();

  AiControlPaths paths_;
  QPlainTextEdit* blurb_{nullptr};
  QLabel* status_{nullptr};
  QPushButton* copy_button_{nullptr};
};

}  // namespace patchy::ui
