#pragma once

#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QString>
#include <QTimer>

namespace patchy::test::ui::app_shell_support {
using namespace patchy::test::ui;

// Opens Preferences, hands the dialog to `inspect`, then accepts or rejects it.
template <typename Inspect>
void run_preferences_dialog(patchy::ui::MainWindow& window, bool accept, Inspect inspect) {
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    // A failed CHECK must not unwind through the dialog's event loop (that crashes the suite
    // with the message lost): record it and close the dialog as usual.
    try {
      inspect(*dialog);
    } catch (const std::exception& error) {
      patchy::test::report_failure(error.what());
    }
    saw_dialog = true;
    if (accept) {
      dialog->accept();
    } else {
      dialog->reject();
    }
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
}

}  // namespace patchy::test::ui::app_shell_support
