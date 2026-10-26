// Build-time runtime checker. Never reads or writes the user's settings.
#include "test_harness.hpp"
#include "test_fonts.hpp"
#include "ui/app_settings.hpp"
#include "ui/localization.hpp"
#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdio>
#include <vector>

std::vector<patchy::test::TestCase> localization_tests();

int main(int argc, char** argv) {
  patchy::test::suppress_crash_dialogs();
  qputenv("QT_QPA_PLATFORM", "offscreen");
  // Avoid a desktop portal lookup when a Linux build runs without a user session.
  qunsetenv("DBUS_SESSION_BUS_ADDRESS");
  QApplication app(argc, argv);
  QTemporaryDir settings(QDir::current().filePath(QStringLiteral("translation-settings-XXXXXX")));
  if (!settings.isValid()) return 1;
  patchy::ui::set_app_settings_root(settings.path());
  qputenv("PATCHY_BRUSH_SETTINGS_FILE", settings.filePath(QStringLiteral("brushes.ini")).toUtf8());
  qputenv("PATCHY_RECENT_SETTINGS_FILE", settings.filePath(QStringLiteral("recent.ini")).toUtf8());
  app.setFont(patchy::test::visual_test_font());
  patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false);
  int failures = 0;
  for (const auto& test : localization_tests()) {
    // Catalog checks already ran before lrelease; use the same remaining checks
    // as the UI suite, including catalog loading and runtime source coverage.
    if (test.name == "ui_translation_template_is_current" || test.name == "ui_translation_catalogs_are_complete") continue;
    try {
      test.run();
      std::printf("[PASS] %s\n", test.name.c_str());
    } catch (const std::exception& error) {
      std::fprintf(stderr, "[FAIL] %s: %s\n", test.name.c_str(), error.what());
      ++failures;
    }
  }
  return failures == 0 ? 0 : 1;
}
