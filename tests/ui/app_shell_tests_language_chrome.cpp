#include <QLayout>
#include <QScrollBar>
#include "ui/action_icons.hpp"
#include "ui/app_style.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/icon_theme.hpp"
#include "ui/theme_file.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"
#include "ui/app_data_migration.hpp"
#include "ui/app_settings.hpp"
#include "ui/build_info.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui/single_instance.hpp"
#include "test_harness.hpp"
#include <patchy_version.hpp>
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QLocalServer>
#include <QLocalSocket>
#include <QListWidget>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QIODevice>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <array>
#include <cstdint>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <map>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "app_shell_test_support.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using namespace patchy::test::ui::app_shell_support;

// True where the "Use the system title bar" preference exists and takes part in the
// chrome decision (Linux desktop builds).
constexpr bool kSystemTitleBarPreferenceApplies =
#if defined(Q_OS_LINUX) && !defined(Q_OS_WASM)
    true;
#else
    false;
#endif

QStringList top_level_menu_texts(QMenuBar& menu_bar);

void choose_preferences_language(patchy::ui::MainWindow& window, const QString& language_code);

void write_theme_file(const QString& path, const QColor& window_bg, const QString& name);

// Builds a window after the gate cache was reset and reports whether it carries the
// custom chrome (frameless flag, badge, window controls), checking the three agree.
bool window_has_custom_chrome();

QStringList top_level_menu_texts(QMenuBar& menu_bar) {
  QStringList texts;
  for (auto* action : menu_bar.actions()) {
    texts << action->text().remove('&');
  }
  return texts;
}

void choose_preferences_language(patchy::ui::MainWindow& window, const QString& language_code) {
  auto* preferences = require_action(window, "filePreferencesAction");
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesLanguageCombo"));
    CHECK(combo != nullptr);
    const auto index = combo->findData(language_code);
    CHECK(index >= 0);
    combo->setCurrentIndex(index);
    saw_dialog = true;
    dialog->accept();
  });
  preferences->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
}

void write_theme_file(const QString& path, const QColor& window_bg, const QString& name) {
  auto palette = patchy::ui::dark_palette();
  palette.window_bg = window_bg;
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(patchy::ui::serialize_theme_to_json(palette, patchy::ui::ColorScheme::Dark, name));
  file.close();
}

bool window_has_custom_chrome() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  const bool frameless = window.windowFlags().testFlag(Qt::FramelessWindowHint);
  const bool controls = window.findChild<QWidget*>(QStringLiteral("windowChromeControls")) != nullptr;
  const bool badge = window.menuBar()->findChild<QLabel*>(QStringLiteral("patchyBadge")) != nullptr;
  CHECK(frameless == controls);
  CHECK(frameless == badge);
  CHECK(frameless == patchy::ui::MainWindow::use_custom_window_chrome());
  return frameless;
}

// Redirects user_themes_directory() to a scratch folder for one test, so
// import/export/reload tests never touch the real AppData themes folder.
class ThemesDirEnvGuard {
public:
  ThemesDirEnvGuard() : previous_(qgetenv("PATCHY_THEMES_DIR")), had_previous_(qEnvironmentVariableIsSet("PATCHY_THEMES_DIR")) {
    CHECK(dir_.isValid());
    qputenv("PATCHY_THEMES_DIR", dir_.path().toUtf8());
  }
  ~ThemesDirEnvGuard() {
    if (had_previous_) {
      qputenv("PATCHY_THEMES_DIR", previous_);
    } else {
      qunsetenv("PATCHY_THEMES_DIR");
    }
  }
  [[nodiscard]] QString path() const { return dir_.path(); }

private:
  QTemporaryDir dir_;
  QByteArray previous_;
  bool had_previous_;
};


// Mirrors what ThemeManager::load_saved_preference() does at startup: read
// the persisted preference, then read and apply a persisted custom theme id.
// Calling it a second time on the live singleton is the established pattern
// for "simulated restart" in this suite (see the language-preference tests).
void ui_custom_theme_id_persists_and_reapplies_like_a_restart() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  SettingsValueRestorer restore_custom_id(QStringLiteral("preferences/customThemeId"));
  ThemesDirEnvGuard themes_dir;

  auto palette = patchy::ui::dark_palette();
  palette.window_bg = QColor(9, 8, 7);
  const auto json = patchy::ui::serialize_theme_to_json(palette, patchy::ui::ColorScheme::Dark,
                                                          QStringLiteral("Restart Test"));
  QFile file(QDir(themes_dir.path()).filePath(QStringLiteral("restart-test.patchytheme")));
  CHECK(file.open(QIODevice::WriteOnly));
  file.write(json);
  file.close();

  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/colorScheme"), QStringLiteral("dark"));
    settings.setValue(QStringLiteral("preferences/customThemeId"), QStringLiteral("restart-test.patchytheme"));
    settings.sync();
  }

  patchy::ui::ThemeManager::instance().load_saved_preference();

  const auto active_id = patchy::ui::ThemeManager::instance().active_custom_theme_id();
  CHECK(active_id.has_value());
  CHECK(*active_id == QStringLiteral("restart-test.patchytheme"));
  CHECK(patchy::ui::theme().window_bg == QColor(9, 8, 7));
}

// A custom theme id whose backing file has been moved or deleted outside
// Patchy must not fail startup: fall back cleanly to the built-in preference
// already applied.
void ui_custom_theme_missing_file_falls_back_to_builtin_preference() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  SettingsValueRestorer restore_custom_id(QStringLiteral("preferences/customThemeId"));
  ThemesDirEnvGuard themes_dir;

  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/colorScheme"), QStringLiteral("light"));
    settings.setValue(QStringLiteral("preferences/customThemeId"), QStringLiteral("does-not-exist.patchytheme"));
    settings.sync();
  }

  patchy::ui::ThemeManager::instance().load_saved_preference();

  CHECK(!patchy::ui::ThemeManager::instance().active_custom_theme_id().has_value());
  CHECK(!patchy::ui::has_active_custom_palette());
  CHECK(patchy::ui::ThemeManager::instance().preference() == patchy::ui::ColorSchemePreference::Light);
  CHECK(patchy::ui::active_color_scheme() == patchy::ui::ColorScheme::Light);
}

// A custom palette must resolve exactly like a built-in one: no leftover
// @token in the resolved stylesheet or in an ad hoc apply_theme_tokens() call.
void ui_custom_theme_qss_resolves_every_token() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);

  patchy::ui::CustomTheme custom;
  custom.base = patchy::ui::ColorScheme::Dark;
  custom.palette = patchy::ui::dark_palette();
  custom.palette.accent = QColor(0x77, 0x22, 0x99);
  patchy::ui::ThemeManager::instance().set_custom_theme(QStringLiteral("qss-check.patchytheme"), custom,
                                                         /*persist=*/false);

  const auto style = patchy::ui::photoshop_style();
  CHECK(!style.isEmpty());
  CHECK(!style.contains(QLatin1Char('@')));

  const auto resolved = patchy::ui::apply_theme_tokens(QStringLiteral("a: @accent;"));
  CHECK(resolved == QStringLiteral("a: %1;").arg(custom.palette.accent.name(QColor::HexRgb)));
}

// "format" is the one key that can refuse a file outright: a newer number means
// keys this build cannot interpret, and a silent partial load would be worse
// than an error. An absent key is format 1, so files written before the key
// existed still load.
void ui_theme_file_newer_format_is_a_hard_error() {
  QJsonObject object;
  object.insert(QStringLiteral("format"), 2);
  object.insert(QStringLiteral("base"), QStringLiteral("dark"));
  const auto newer = patchy::ui::load_theme_from_json(QJsonDocument(object).toJson());
  CHECK(!newer.theme.has_value());
  CHECK(newer.error.contains(QStringLiteral("2")));

  object.insert(QStringLiteral("format"), QStringLiteral("1"));
  const auto text = patchy::ui::load_theme_from_json(QJsonDocument(object).toJson());
  CHECK(!text.theme.has_value());

  object.remove(QStringLiteral("format"));
  const auto absent = patchy::ui::load_theme_from_json(QJsonDocument(object).toJson());
  CHECK(absent.theme.has_value());

  const auto exported = patchy::ui::serialize_theme_to_json(patchy::ui::dark_palette(), patchy::ui::ColorScheme::Dark,
                                                            QStringLiteral("Format"));
  CHECK(QJsonDocument::fromJson(exported).object().value(QStringLiteral("format")).toInt() ==
        patchy::ui::kThemeFileFormat);
}

// The authoring loop: edit the file in a text editor, click Reload Themes. The
// combo keeps the same entry selected and the fresh colors apply even though
// the selection never moved.
void ui_preferences_reload_reapplies_an_edited_theme_file() {
  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  SettingsValueRestorer restore_custom_id(QStringLiteral("preferences/customThemeId"));
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  ThemesDirEnvGuard themes_dir;
  const auto path = QDir(themes_dir.path()).filePath(QStringLiteral("editable.patchytheme"));
  write_theme_file(path, QColor(10, 20, 30), QStringLiteral("Editable"));

  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesColorSchemeCombo"));
    auto* reload = dialog->findChild<QPushButton*>(QStringLiteral("preferencesReloadThemesButton"));
    auto* remove = dialog->findChild<QPushButton*>(QStringLiteral("preferencesDeleteThemeButton"));
    CHECK(combo != nullptr && reload != nullptr && remove != nullptr);
    if (combo == nullptr || reload == nullptr || remove == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(!remove->isEnabled());
    const auto index = combo->findData(QStringLiteral("custom:editable.patchytheme"));
    CHECK(index >= 0);
    CHECK(combo->itemText(index) == QStringLiteral("Editable"));
    combo->setCurrentIndex(index);
    QApplication::processEvents();
    CHECK(patchy::ui::theme().window_bg == QColor(10, 20, 30));
    CHECK(remove->isEnabled());

    write_theme_file(path, QColor(40, 50, 60), QStringLiteral("Editable"));
    reload->click();
    QApplication::processEvents();
    CHECK(combo->currentData().toString() == QStringLiteral("custom:editable.patchytheme"));
    CHECK(patchy::ui::theme().window_bg == QColor(40, 50, 60));
    CHECK(remove->isEnabled());
    saw_dialog = true;
    dialog->reject();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);

  // Rejecting restores the entry scheme, which was the built-in Dark.
  CHECK(!patchy::ui::has_active_custom_palette());
  CHECK(patchy::ui::active_color_scheme() == patchy::ui::ColorScheme::Dark);
}

// Delete removes the file and its entry, and the dialog falls back to the
// first built-in entry so the live preview and the revert guard stay in step.
void ui_preferences_delete_removes_theme_file_and_entry() {
  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  SettingsValueRestorer restore_custom_id(QStringLiteral("preferences/customThemeId"));
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  ThemesDirEnvGuard themes_dir;
  const auto path = QDir(themes_dir.path()).filePath(QStringLiteral("doomed.patchytheme"));
  write_theme_file(path, QColor(70, 80, 90), QStringLiteral("Doomed"));

  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  bool saw_confirm = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesColorSchemeCombo"));
    auto* remove = dialog->findChild<QPushButton*>(QStringLiteral("preferencesDeleteThemeButton"));
    CHECK(combo != nullptr && remove != nullptr);
    if (combo == nullptr || remove == nullptr) {
      dialog->reject();
      return;
    }
    const auto index = combo->findData(QStringLiteral("custom:doomed.patchytheme"));
    CHECK(index >= 0);
    combo->setCurrentIndex(index);
    QApplication::processEvents();
    CHECK(patchy::ui::theme().window_bg == QColor(70, 80, 90));

    QTimer::singleShot(0, [&] {
      auto* confirm = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("preferencesDeleteThemeConfirm")));
      CHECK(confirm != nullptr);
      if (confirm == nullptr) {
        return;
      }
      saw_confirm = true;
      for (auto* button : confirm->buttons()) {
        if (confirm->buttonRole(button) == QMessageBox::AcceptRole) {
          button->click();
          return;
        }
      }
      CHECK(false);
    });
    remove->click();
    QApplication::processEvents();

    CHECK(!QFileInfo::exists(path));
    CHECK(combo->findData(QStringLiteral("custom:doomed.patchytheme")) < 0);
    // No user entry left, so its separator went too: the built-ins, one
    // separator, and the bundled set remain.
    CHECK(combo->count() == 4 + patchy::ui::bundled_theme_file_names().size());
    CHECK(combo->currentIndex() == 0);
    CHECK(!remove->isEnabled());
    CHECK(!patchy::ui::has_active_custom_palette());
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(saw_confirm);
  CHECK(!patchy::ui::has_active_custom_palette());
  CHECK(!patchy::ui::ThemeManager::instance().active_custom_theme_id().has_value());
}

// Every bundled theme (themes.qrc) parses cleanly with no unknown-role warning
// (the generator writes every role, so a warning means the palette and the
// generated files drifted), lists in Preferences under a "bundled:" id right
// after the built-in entries, and cannot be deleted.
void ui_bundled_themes_load_and_list_in_preferences() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  ThemesDirEnvGuard themes_dir;  // an empty user folder, so only bundled entries follow the built-ins

  const auto names = patchy::ui::bundled_theme_file_names();
  CHECK(names.size() >= 5);
  for (const auto& name : names) {
    const auto id = patchy::ui::kBundledThemeIdPrefix + name;
    CHECK(patchy::ui::is_bundled_theme_id(id));
    const auto result = patchy::ui::load_theme_by_id(id);
    if (!result.theme) {
      fprintf(stderr, "  %s: %s\n", name.toUtf8().constData(), result.error.toUtf8().constData());
    }
    CHECK(result.theme.has_value());
    CHECK(result.error.isEmpty());
    CHECK(result.warnings.isEmpty());
    CHECK(!result.theme->name.isEmpty());
  }
  CHECK(!patchy::ui::load_theme_by_id(QStringLiteral("bundled:missing.patchytheme")).theme.has_value());

  patchy::ui::MainWindow window;
  show_window(window);
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesColorSchemeCombo"));
    auto* remove = dialog->findChild<QPushButton*>(QStringLiteral("preferencesDeleteThemeButton"));
    CHECK(combo != nullptr && remove != nullptr);
    if (combo == nullptr || remove == nullptr) {
      dialog->reject();
      return;
    }
    // Three built-ins, one separator, then the bundled set in authored order.
    CHECK(combo->count() == 4 + names.size());
    for (int i = 0; i < names.size(); ++i) {
      CHECK(combo->itemData(4 + i).toString() == QStringLiteral("custom:bundled:") + names[i]);
    }
    save_widget_artifact("preferences_application_tab", *dialog);
    const auto index = combo->findData(QStringLiteral("custom:bundled:nord.patchytheme"));
    CHECK(index >= 0);
    combo->setCurrentIndex(index);
    QApplication::processEvents();
    CHECK(combo->currentText() == QStringLiteral("Nord (built-in)"));
    save_widget_artifact("preferences_application_tab_nord", *dialog);
    CHECK(patchy::ui::has_active_custom_palette());
    CHECK(patchy::ui::theme().window_bg == QColor(0x2e, 0x34, 0x40));
    CHECK(!remove->isEnabled());
    saw_dialog = true;
    dialog->reject();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(!patchy::ui::has_active_custom_palette());
}

// Export opens in the themes folder with the shown theme's name, and a file
// saved there is listed and selected at once, beside the tagged built-in it
// was copied from, as a deletable user theme.
void ui_preferences_export_defaults_to_themes_folder_and_lists_the_copy() {
  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  SettingsValueRestorer restore_custom_id(QStringLiteral("preferences/customThemeId"));
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  ThemesDirEnvGuard themes_dir;
  const auto names = patchy::ui::bundled_theme_file_names();

  patchy::ui::MainWindow window;
  show_window(window);
  bool saw_dialog = false;
  bool saw_save_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesColorSchemeCombo"));
    auto* export_button = dialog->findChild<QPushButton*>(QStringLiteral("preferencesExportThemeButton"));
    auto* remove = dialog->findChild<QPushButton*>(QStringLiteral("preferencesDeleteThemeButton"));
    CHECK(combo != nullptr && export_button != nullptr && remove != nullptr);
    if (combo == nullptr || export_button == nullptr || remove == nullptr) {
      dialog->reject();
      return;
    }
    combo->setCurrentIndex(combo->findData(QStringLiteral("custom:bundled:nord.patchytheme")));
    QApplication::processEvents();

    QTimer::singleShot(0, [&] {
      auto* save = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("exportThemeFileDialog")));
      CHECK(save != nullptr);
      if (save == nullptr) {
        return;
      }
      saw_save_dialog = true;
      CHECK(QDir::cleanPath(save->directory().absolutePath()) == QDir::cleanPath(QDir(themes_dir.path()).absolutePath()));
      const auto selected = save->selectedFiles();
      CHECK(!selected.isEmpty());
      CHECK(!selected.isEmpty() && QFileInfo(selected.first()).fileName() == QStringLiteral("Nord.patchytheme"));
      save->selectFile(QDir(themes_dir.path()).filePath(QStringLiteral("Nord.patchytheme")));
      static_cast<QDialog*>(save)->accept();  // QFileDialog::accept is protected
    });
    export_button->click();
    QApplication::processEvents();

    const auto copy_path = QDir(themes_dir.path()).filePath(QStringLiteral("Nord.patchytheme"));
    CHECK(QFileInfo::exists(copy_path));
    // Three built-ins, a separator, the bundled set, a separator, the one user file.
    CHECK(combo->count() == 6 + names.size());
    CHECK(combo->currentData().toString() == QStringLiteral("custom:Nord.patchytheme"));
    CHECK(combo->currentText() == QStringLiteral("Nord"));
    CHECK(combo->findData(QStringLiteral("custom:bundled:nord.patchytheme")) >= 0);
    CHECK(remove->isEnabled());
    CHECK(patchy::ui::theme().window_bg == QColor(0x2e, 0x34, 0x40));
    const auto reread = patchy::ui::load_theme_by_id(QStringLiteral("Nord.patchytheme"));
    CHECK(reread.theme.has_value());
    CHECK(reread.theme.has_value() && reread.theme->name == QStringLiteral("Nord"));
    saw_dialog = true;
    dialog->reject();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(saw_save_dialog);
}

// A persisted bundled id needs no user folder at all (wasm has none), so it
// reapplies at startup through the resource path.
void ui_bundled_theme_id_persists_and_reapplies_like_a_restart() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  SettingsValueRestorer restore_custom_id(QStringLiteral("preferences/customThemeId"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/colorScheme"), QStringLiteral("dark"));
    settings.setValue(QStringLiteral("preferences/customThemeId"), QStringLiteral("bundled:dracula.patchytheme"));
    settings.sync();
  }
  patchy::ui::ThemeManager::instance().load_saved_preference();
  const auto active_id = patchy::ui::ThemeManager::instance().active_custom_theme_id();
  CHECK(active_id.has_value());
  CHECK(*active_id == QStringLiteral("bundled:dracula.patchytheme"));
  CHECK(patchy::ui::theme().window_bg == QColor(0x28, 0x2a, 0x36));
}

// The regression guard for "live, no restart": an already-built window has to
// restyle in place, and flipping back has to land exactly where it started.
void ui_color_scheme_switch_updates_existing_window() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);

  patchy::ui::MainWindow window;
  show_window(window);
  QApplication::processEvents();

  const auto dark_sheet = window.styleSheet();
  const auto dark_shot = window.grab().toImage();
  CHECK(dark_sheet.contains(patchy::ui::dark_palette().window_bg.name(QColor::HexRgb)));
  CHECK(!dark_shot.isNull());

  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Light);
  QApplication::processEvents();

  const auto light_sheet = window.styleSheet();
  CHECK(light_sheet != dark_sheet);
  CHECK(light_sheet.contains(patchy::ui::light_palette().window_bg.name(QColor::HexRgb)));
  CHECK(window.grab().toImage() != dark_shot);
  // The light variants of the stylesheet-referenced SVGs are a separate
  // mechanism from the icon engine, and the easiest one to forget.
  CHECK(light_sheet.contains(QStringLiteral("icons/light/scroll-dither.svg")));
  CHECK(dark_sheet.contains(QStringLiteral("icons/scroll-dither.svg")));
  CHECK(!dark_sheet.contains(QStringLiteral("icons/light/")));

  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  QApplication::processEvents();
  CHECK(window.styleSheet() == dark_sheet);
}

// Icons resolve their colors when painted, so the SAME QIcon a QAction was given
// at startup must render the new scheme after a flip. Capturing it by value up
// front is the whole point of the assertion: if this ever needs a setIcon call,
// the engine has regressed.
void ui_themed_icons_recolor_between_schemes() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);

  patchy::ui::MainWindow window;
  show_window(window);

  const QIcon action_icon = require_action(window, "layerNewAction")->icon();
  CHECK(!action_icon.isNull());
  const auto dark_render = action_icon.pixmap(QSize(32, 32)).toImage();
  CHECK(!dark_render.isNull());

  // The paint swatches depict black and white. Their outlines are icon ink and
  // SHOULD recolor so the glyph stays visible on a light toolbar; only the two
  // fills are the subject of the drawing and must not move.
  const QPoint white_fill(23, 23);
  const QPoint black_fill(13, 13);
  const auto dark_swatch =
      patchy::ui::themed_svg_icon(QStringLiteral("default-colors")).pixmap(QSize(32, 32)).toImage();
  CHECK(dark_swatch.pixelColor(white_fill) == QColor(Qt::white));
  CHECK(dark_swatch.pixelColor(black_fill) == QColor(0x11, 0x11, 0x11));

  // Glyphs with no authored SVG go through a second engine that resolves a
  // palette role instead of substituting SVG text. The clipping badge is the one
  // users notice, and it is the reason that overload exists.
  const QIcon clip_icon =
      patchy::ui::simple_icon(QStringLiteral("clip"), &patchy::ui::ThemePalette::layer_clip_badge);
  const auto dark_clip = clip_icon.pixmap(QSize(20, 20)).toImage();
  CHECK(!dark_clip.isNull());

  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Light);

  const auto light_render = action_icon.pixmap(QSize(32, 32)).toImage();
  CHECK(light_render != dark_render);

  const auto ink_luminance = [](const QImage& image) {
    qint64 total = 0;
    qint64 covered = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const auto pixel = image.pixelColor(x, y);
        if (pixel.alpha() < 128) {
          continue;
        }
        total += qGray(pixel.rgb());
        ++covered;
      }
    }
    return covered > 0 ? total / covered : qint64{0};
  };
  // Light-on-dark ink becomes dark-on-light ink.
  CHECK(ink_luminance(light_render) < ink_luminance(dark_render));

  const auto light_swatch =
      patchy::ui::themed_svg_icon(QStringLiteral("default-colors")).pixmap(QSize(32, 32)).toImage();
  CHECK(light_swatch.pixelColor(white_fill) == QColor(Qt::white));
  CHECK(light_swatch.pixelColor(black_fill) == QColor(0x11, 0x11, 0x11));
  // ...while the outline around them did follow the scheme.
  CHECK(light_swatch != dark_swatch);

  // Same QIcon instance, so the role really is resolved at paint time.
  const auto light_clip = clip_icon.pixmap(QSize(20, 20)).toImage();
  CHECK(light_clip != dark_clip);
  CHECK(ink_luminance(light_clip) < ink_luminance(dark_clip));
}

void ui_main_window_persists_window_geometry() {
  SettingsValueRestorer restore_geometry(QStringLiteral("window/normalGeometry"));
  SettingsValueRestorer restore_maximized(QStringLiteral("window/maximized"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("window/normalGeometry"));
    settings.remove(QStringLiteral("window/maximized"));
    settings.sync();
  }

  // Derive the target from the available screen so the on-screen clamp performed during restore is
  // an identity operation; otherwise a small offscreen test screen would shrink/move the geometry.
  const QScreen* primary = QApplication::primaryScreen();
  CHECK(primary != nullptr);
  const QRect available = primary->availableGeometry();
  CHECK(available.isValid());
  const QRect target = available.adjusted(20, 20, -120, -100);
  CHECK(target.width() > 0 && target.height() > 0);
  {
    patchy::ui::MainWindow window;
    window.show();
    QApplication::processEvents();
    window.setGeometry(target);
    QApplication::processEvents();
    // Closing a window with no modified documents accepts the close and persists geometry.
    window.close();
    QApplication::processEvents();
  }

  QRect stored;
  {
    auto settings = patchy::ui::app_settings();
    stored = settings.value(QStringLiteral("window/normalGeometry")).toRect();
    CHECK(stored.isValid());
    CHECK(stored.size() == target.size());
    CHECK(!settings.value(QStringLiteral("window/maximized"), false).toBool());
  }

  patchy::ui::MainWindow restored;
  restored.show();
  QApplication::processEvents();
  CHECK(restored.size() == stored.size());
}

void ui_psd_import_warning_preference_defaults_to_hidden() {
  SettingsValueRestorer restore_psd_warning_check(QStringLiteral("imports/showPsdWarningsAndInfo"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesShowPsdImportWarningsCheck"));
    CHECK(check != nullptr);
    CHECK(check->text() == QStringLiteral("Show import warnings and notes in a popup (status bar otherwise)"));
    CHECK(!check->isChecked());
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
}

void ui_psd_import_warning_preference_persists_enabled_setting() {
  SettingsValueRestorer restore_psd_warning_check(QStringLiteral("imports/showPsdWarningsAndInfo"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesShowPsdImportWarningsCheck"));
    CHECK(check != nullptr);
    CHECK(!check->isChecked());
    check->setChecked(true);
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);

  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("imports/showPsdWarningsAndInfo"), false).toBool());
}

void ui_transform_shift_aspect_preference_defaults_to_off() {
  SettingsValueRestorer restore_shift_aspect(QStringLiteral("input/shiftKeepsTransformAspect"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("input/shiftKeepsTransformAspect"));
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  CHECK(!canvas->shift_keeps_transform_aspect());

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesTransformShiftAspectCheck"));
    CHECK(check != nullptr);
    CHECK(check->text() == QStringLiteral("Hold Shift to keep the aspect ratio when transforming"));
    CHECK(!check->isChecked());
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
}

void ui_transform_shift_aspect_preference_persists_and_reaches_canvas() {
  SettingsValueRestorer restore_shift_aspect(QStringLiteral("input/shiftKeepsTransformAspect"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("input/shiftKeepsTransformAspect"));
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesTransformShiftAspectCheck"));
    CHECK(check != nullptr);
    CHECK(!check->isChecked());
    check->setChecked(true);
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);

  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("input/shiftKeepsTransformAspect"), false).toBool());
  // Accepting the dialog pushes the new value onto every open canvas.
  CHECK(canvas->shift_keeps_transform_aspect());
}

// input/snapTransformsToPixelGrid defaults on (Photoshop's default) and reaches the canvas.
void ui_transform_snap_preference_persists_and_reaches_canvas() {
  SettingsValueRestorer restore_snap(QStringLiteral("input/snapTransformsToPixelGrid"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("input/snapTransformsToPixelGrid"));
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  CHECK(canvas->snap_transforms_to_pixel_grid());

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesTransformSnapToPixelGridCheck"));
    CHECK(check != nullptr);
    CHECK(check->isChecked());
    check->setChecked(false);
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);

  auto settings = patchy::ui::app_settings();
  CHECK(!settings.value(QStringLiteral("input/snapTransformsToPixelGrid"), true).toBool());
  CHECK(!canvas->snap_transforms_to_pixel_grid());
}

void ui_language_switch_updates_existing_window() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  const auto initial_tab_count = tabs->count();

  choose_preferences_language(window, QStringLiteral("ja"));

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("ja"));
  const auto japanese_menus = top_level_menu_texts(*window.menuBar());
  CHECK(japanese_menus.contains(QStringLiteral("ファイル(F)")));
  CHECK(!japanese_menus.contains(QStringLiteral("環境設定(P)")));
  CHECK(tabs->count() == initial_tab_count);
  CHECK(require_action(window, "helpAiSetupAction")->text() ==
        QStringLiteral("AI制御のセットアップ(&U)..."));

  choose_preferences_language(window, QStringLiteral("en"));

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("en"));
  const auto english_menus = top_level_menu_texts(*window.menuBar());
  CHECK(english_menus.contains(QStringLiteral("File")));
  CHECK(!english_menus.contains(QStringLiteral("Preferences")));
  CHECK(require_action(window, "fileSaveAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_S));
  CHECK(require_action(window, "helpAiSetupAction")->text() ==
        QStringLiteral("Set &up AI Control..."));
  CHECK(tabs->count() == initial_tab_count);
}

void ui_language_switch_survives_window_reactivation() {
  // GitHub issue 29. On macOS Qt merges a menubar item whose title starts with its translated
  // "Setting", "Setup", "Options", "About", "Quit"... into the application menu. A runtime switch
  // to Spanish or French renamed Image > Adjustments to "Ajustes"/"Réglages" (Chinese renames
  // Window > Set Screen Size), Qt swapped that submenu's native item for the merged one and freed
  // the old one while the submenu still pointed at it, and the next key-window change crashed in
  // setSubmenu:. Offscreen has no native menubar: reproduce on a mac with
  // PATCHY_UI_TEST_PLATFORM=cocoa NSZombieEnabled=YES (zombies turn the message to the freed item
  // into an abort; in a short run the freed memory is often still intact and nothing shows).
  // ui_menubar_submenus_have_no_native_menu_role is the platform-independent guard.
  patchy::ui::MainWindow window;
  show_window(window);
  window.raise();
  window.activateWindow();
  process_events_for(150);
  for (const char* code : {"es", "fr", "zh_CN", "en"}) {
    choose_preferences_language(window, QString::fromLatin1(code));
    CHECK(patchy::ui::LocalizationManager::instance().current_language() == QString::fromLatin1(code));
    // Another window takes key, then the main window regains it: the menubar re-sync that crashed.
    QDialog other(&window);
    other.setObjectName(QStringLiteral("languageSwitchOtherWindow"));
    other.resize(200, 100);
    other.show();
    other.raise();
    other.activateWindow();
    process_events_for(150);
    other.close();
    window.raise();
    window.activateWindow();
    process_events_for(150);
  }
  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("en"));
  CHECK(top_level_menu_texts(*window.menuBar()).contains(QStringLiteral("Image")));
}

void ui_menubar_submenus_have_no_native_menu_role() {
  // Every submenu under the menubar opts out of Qt's macOS menu-role text heuristic; see
  // ui_language_switch_survives_window_reactivation for the crash a merged submenu causes.
  patchy::ui::MainWindow window;
  show_window(window);
  int submenus = 0;
  std::function<void(QMenu&)> walk = [&](QMenu& menu) {
    for (auto* action : menu.actions()) {
      auto* submenu = action->menu();
      if (submenu == nullptr) {
        continue;
      }
      ++submenus;
      if (action->menuRole() != QAction::NoRole) {
        fprintf(stderr, "submenu keeps a native menu role: %s\n", qPrintable(action->text()));
      }
      CHECK(action->menuRole() == QAction::NoRole);
      walk(*submenu);
    }
  };
  for (auto* action : window.menuBar()->actions()) {
    if (auto* menu = action->menu()) {
      walk(*menu);
    }
  }
  CHECK(submenus >= 20);
}

void ui_language_preference_applies_at_startup() {
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/language"), QStringLiteral("ja"));
    settings.sync();
  }
  patchy::ui::LocalizationManager::instance().load_saved_language();

  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("ja"));
  const auto menus = top_level_menu_texts(*window.menuBar());
  CHECK(menus.contains(QStringLiteral("ファイル(F)")));
  CHECK(!menus.contains(QStringLiteral("環境設定(P)")));
}

void ui_language_missing_preference_uses_system_language() {
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("preferences/language"));
    settings.sync();
  }
  patchy::ui::LocalizationManager::instance().load_saved_language(QLocale(QLocale::Japanese, QLocale::Japan));

  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("ja"));
  const auto menus = top_level_menu_texts(*window.menuBar());
  CHECK(menus.contains(QStringLiteral("ファイル(F)")));
  auto settings = patchy::ui::app_settings();
  CHECK(!settings.contains(QStringLiteral("preferences/language")));
}

void ui_language_saved_preference_overrides_system_language() {
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/language"), QStringLiteral("en"));
    settings.sync();
  }
  patchy::ui::LocalizationManager::instance().load_saved_language(QLocale(QLocale::Japanese, QLocale::Japan));

  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("en"));
  const auto menus = top_level_menu_texts(*window.menuBar());
  CHECK(menus.contains(QStringLiteral("File")));
  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("preferences/language")).toString() == QStringLiteral("en"));
  for (const auto* tag : {"pt_PT", "ru_RU", "pl_PL", "ko_KR"}) {
    patchy::ui::LocalizationManager::instance().load_saved_language(QLocale(QString::fromLatin1(tag)));
    CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("en"));
  }
}

void ui_language_invalid_preference_falls_back_to_english() {
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/language"), QStringLiteral("zz"));
    settings.sync();
  }
  patchy::ui::LocalizationManager::instance().load_saved_language();

  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("en"));
  const auto menus = top_level_menu_texts(*window.menuBar());
  CHECK(menus.contains(QStringLiteral("File")));
  CHECK(!menus.contains(QStringLiteral("Preferences")));
}

void ui_language_matching_maps_locales_to_shipped_codes() {
  auto& manager = patchy::ui::LocalizationManager::instance();
  // Plain codes, locale names and BCP 47 tags all resolve to a catalog code.
  CHECK(manager.match_language(QStringLiteral("en")) == QStringLiteral("en"));
  CHECK(manager.match_language(QStringLiteral("en_US")) == QStringLiteral("en"));
  CHECK(manager.match_language(QStringLiteral("ja_JP")) == QStringLiteral("ja"));
  CHECK(manager.match_language(QStringLiteral("fr-CA")) == QStringLiteral("fr"));
  CHECK(manager.match_language(QStringLiteral("de_AT")) == QStringLiteral("de"));
  CHECK(manager.match_language(QStringLiteral("es-MX")) == QStringLiteral("es"));
  CHECK(manager.match_language(QStringLiteral("it")) == QStringLiteral("it"));
  // Chinese picks the variant by script: Traditional for Taiwan, Hong Kong and Macao
  // or an explicit Hant tag, Simplified otherwise.
  CHECK(manager.match_language(QStringLiteral("zh_CN")) == QStringLiteral("zh_CN"));
  CHECK(manager.match_language(QStringLiteral("zh")) == QStringLiteral("zh_CN"));
  CHECK(manager.match_language(QStringLiteral("zh_SG")) == QStringLiteral("zh_CN"));
  CHECK(manager.match_language(QStringLiteral("zh-Hans-CN")) == QStringLiteral("zh_CN"));
  CHECK(manager.match_language(QStringLiteral("zh_TW")) == QStringLiteral("zh_TW"));
  CHECK(manager.match_language(QStringLiteral("zh_HK")) == QStringLiteral("zh_TW"));
  CHECK(manager.match_language(QStringLiteral("zh-Hant")) == QStringLiteral("zh_TW"));
  for (const auto* tag : {"pt", "pt_BR", "pt-BR", "pt-PT", "pt-Latn-BR"}) {
    CHECK(manager.match_language(QString::fromLatin1(tag)) == QStringLiteral("pt_BR"));
  }
  CHECK(manager.match_language(QStringLiteral("ru-RU")) == QStringLiteral("ru"));
  CHECK(manager.match_language(QStringLiteral("pl_PL")) == QStringLiteral("pl"));
  CHECK(manager.match_language(QStringLiteral("ko-KR")) == QStringLiteral("ko"));
  // Languages Patchy does not ship match nothing; the caller falls back to English.
  CHECK(manager.match_language(QStringLiteral("zz")).isEmpty());
  CHECK(manager.match_language(QString()).isEmpty());

  CHECK(manager.language_for_locale(QLocale(QLocale::French, QLocale::Canada)) == QStringLiteral("fr"));
  CHECK(manager.language_for_locale(QLocale(QLocale::Chinese, QLocale::TraditionalHanScript, QLocale::Taiwan)) ==
        QStringLiteral("zh_TW"));
  CHECK(manager.language_for_locale(QLocale(QLocale::Chinese, QLocale::SimplifiedHanScript, QLocale::China)) ==
        QStringLiteral("zh_CN"));
  CHECK(manager.language_for_locale(QLocale(QLocale::Portuguese, QLocale::Brazil)) == QStringLiteral("pt_BR"));
  CHECK(manager.language_for_locale(QLocale(QLocale::Portuguese, QLocale::Portugal)) == QStringLiteral("pt_BR"));
  CHECK(manager.language_for_locale(QLocale(QLocale::Russian, QLocale::Russia)) == QStringLiteral("ru"));
  CHECK(manager.language_for_locale(QLocale(QLocale::Polish, QLocale::Poland)) == QStringLiteral("pl"));
  CHECK(manager.language_for_locale(QLocale(QLocale::Korean, QLocale::SouthKorea)) == QStringLiteral("ko"));
  CHECK(manager.language_for_locale(QLocale::c()) == QStringLiteral("en"));

  // Selecting an unshipped language reports failure and leaves English active.
  CHECK(!manager.set_language(QStringLiteral("nl_NL"), false));
  CHECK(manager.current_language() == QStringLiteral("en"));
}

void ui_language_combo_lists_every_shipped_language() {
  auto& manager = patchy::ui::LocalizationManager::instance();
  const auto& shipped = manager.languages();
  CHECK(!shipped.empty());
  CHECK(shipped.front().code == QStringLiteral("en"));
  // Every shipped catalog is built next to the test binary, so the installed set is
  // the full table.
  const auto available = manager.available_languages();
  CHECK(available.size() == shipped.size());

  patchy::ui::MainWindow window;
  show_window(window);
  auto* preferences = require_action(window, "filePreferencesAction");
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesLanguageCombo"));
    CHECK(combo != nullptr);
    CHECK(combo->count() == static_cast<int>(shipped.size()));
    for (int index = 0; index < combo->count(); ++index) {
      const auto& language = shipped[static_cast<std::size_t>(index)];
      CHECK(combo->itemData(index).toString() == language.code);
      CHECK(combo->itemText(index) == language.native_name);
    }
    CHECK(combo->currentData().toString() == manager.current_language());
    saw_dialog = true;
    dialog->reject();
  });
  preferences->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
}

void ui_language_every_catalog_loads_and_translates() {
  auto& manager = patchy::ui::LocalizationManager::instance();
  for (const auto& language : manager.languages()) {
    CHECK(manager.set_language(language.code, false));
    CHECK(manager.current_language() == language.code);
    // Two everyday strings that differ from English in every shipped language, so a
    // match means the catalog did not load and Qt fell back to the source text. "&File"
    // would not work here: Italian keeps "File", the way Adobe's Italian UI does.
    const auto layer_menu = QCoreApplication::translate("patchy::ui::MainWindow", "&Layer");
    const auto ready = QCoreApplication::translate("patchy::ui::MainWindow", "Ready");
    if (language.code == QStringLiteral("en")) {
      CHECK(layer_menu == QStringLiteral("&Layer"));
      CHECK(ready == QStringLiteral("Ready"));
    } else {
      CHECK(layer_menu != QStringLiteral("&Layer"));
      CHECK(ready != QStringLiteral("Ready"));
    }
  }
  CHECK(manager.set_language(QStringLiteral("en"), false));
}

void ui_language_catalog_covers_dialog_status_and_properties() {
  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("ja"), false));
  QApplication::processEvents();

  const auto canvas_status = QCoreApplication::translate(
      "patchy::ui::CanvasWidget", "Select a normal pixel layer before painting on text");
  CHECK(canvas_status == QStringLiteral("テキスト上に描画する前に通常のピクセルレイヤーを選択してください"));

  const auto save_title = QCoreApplication::translate("patchy::ui::MainWindow", "Save changes?");
  CHECK(save_title == QStringLiteral("変更を保存しますか?"));
  const auto save_prompt =
      QCoreApplication::translate("patchy::ui::MainWindow", "Save changes to %1 before closing?");
  CHECK(save_prompt == QStringLiteral("閉じる前に %1 への変更を保存しますか?"));

  const auto no_layer = QCoreApplication::translate("patchy::ui::MainWindow", "Layer: No active layer");
  CHECK(no_layer == QStringLiteral("レイヤー: アクティブレイヤーなし"));
  const auto document_info = QCoreApplication::translate(
      "patchy::ui::MainWindow", "Document: %1 x %2 px | %3 x %4 %5 | %6 ppi | %7 | %8 layers | Zoom %9% | %10");
  CHECK(document_info.startsWith(QStringLiteral("ドキュメント:")));
  const auto bmp_depth = QCoreApplication::translate("QObject", "Color depth");
  CHECK(bmp_depth == QStringLiteral("色深度"));
  const auto bmp_quantize = QCoreApplication::translate("QObject", "Reduce colors automatically");
  CHECK(bmp_quantize == QStringLiteral("色数を自動的に減らす"));
  const auto bmp_palette_file = QCoreApplication::translate("QObject", "Use palette file");
  CHECK(bmp_palette_file == QStringLiteral("パレットファイルを使用"));
  const auto settings_file = QCoreApplication::translate("QObject", "Settings file:");
  CHECK(settings_file == QStringLiteral("設定ファイル:"));
  const auto open_settings_folder = QCoreApplication::translate("QObject", "Open Settings Folder");
  CHECK(open_settings_folder == QStringLiteral("設定フォルダーを開く"));
  const auto settings_folder_failed = QCoreApplication::translate("QObject", "Could not open settings folder.");
  CHECK(settings_folder_failed == QStringLiteral("設定フォルダーを開けませんでした。"));
  const auto data_folder = QCoreApplication::translate("QObject", "User data folder (fonts, scripts):");
  CHECK(data_folder == QStringLiteral("ユーザーデータフォルダー (フォント、スクリプト):"));
  const auto open_data_folder = QCoreApplication::translate("QObject", "Open Data Folder");
  CHECK(open_data_folder == QStringLiteral("データフォルダーを開く"));
  const auto data_folder_failed = QCoreApplication::translate("QObject", "Could not open data folder.");
  CHECK(data_folder_failed == QStringLiteral("データフォルダーを開けませんでした。"));

  // The About dialog's folder rows must translate in every shipped language, not only the
  // Japanese pins above: a string that comes back as its English source means the entry is
  // missing from that catalog or carries the wrong context.
  auto& manager = patchy::ui::LocalizationManager::instance();
  for (const auto& language : manager.languages()) {
    if (language.code == QStringLiteral("en")) {
      continue;
    }
    CHECK(manager.set_language(language.code, false));
    QApplication::processEvents();
    for (const char* source : {"Settings file:", "Open Settings Folder", "Could not open settings folder.",
                               "User data folder (fonts, scripts):", "Open Data Folder",
                               "Could not open data folder.", "Settings and data folder:", "Open Folder"}) {
      const auto translated = QCoreApplication::translate("QObject", source);
      if (translated == QString::fromUtf8(source)) {
        std::cerr << "untranslated in " << language.code.toStdString() << ": " << source << "\n";
      }
      CHECK(translated != QString::fromUtf8(source));
    }
  }
  CHECK(manager.set_language(QStringLiteral("ja"), false));
  QApplication::processEvents();
  const auto checking_updates = QCoreApplication::translate("QObject", "Checking for updates...");
  CHECK(checking_updates == QStringLiteral("更新を確認しています..."));
  const auto up_to_date = QCoreApplication::translate("QObject", "Patchy is up to date (%1).");
  CHECK(up_to_date == QStringLiteral("Patchy は最新です (%1)。"));
  const auto update_failed =
      QCoreApplication::translate("QObject", "Update check failed: invalid update manifest.");
  CHECK(update_failed == QStringLiteral("更新確認に失敗しました: 更新マニフェストが無効です。"));
  const auto show_effects = QCoreApplication::translate("QObject", "Show Effects");
  CHECK(show_effects == QStringLiteral("効果を表示"));
  const auto gloss_contour = QCoreApplication::translate("QObject", "Gloss Contour");
  CHECK(gloss_contour == QStringLiteral("光沢輪郭"));
  const auto link_with_layer = QCoreApplication::translate("QObject", "Link with Layer");
  CHECK(link_with_layer == QStringLiteral("レイヤーにリンク"));
  const auto pattern_basketweave = QCoreApplication::translate("QObject", "Basketweave");
  CHECK(pattern_basketweave == QStringLiteral("バスケット編み"));
  const auto contour_ring_double = QCoreApplication::translate("QObject", "Ring - Double");
  CHECK(contour_ring_double == QStringLiteral("リング - 二重"));
  const auto curves_graph = QCoreApplication::translate("QObject", "Curves graph");
  CHECK(curves_graph == QStringLiteral("トーンカーブグラフ"));
  const auto curves_input = QCoreApplication::translate("QObject", "Input:");
  CHECK(curves_input == QStringLiteral("入力:"));
  const auto curves_auto =
      QCoreApplication::translate("QObject", "Set the active channel from its histogram");
  CHECK(curves_auto == QStringLiteral("ヒストグラムに基づいて選択中のチャンネルを自動調整"));
  const auto curves_summary =
      QCoreApplication::translate("QObject", "Curves: RGB %1, Red %2, Green %3, Blue %4 points");
  CHECK(curves_summary == QStringLiteral("トーンカーブ: RGB %1、赤 %2、緑 %3、青 %4 ポイント"));
  const auto curves_presets = QCoreApplication::translate("QObject", "Curves presets");
  CHECK(curves_presets == QStringLiteral("トーンカーブのプリセット"));
  const auto medium_contrast = QCoreApplication::translate("QObject", "Medium Contrast");
  CHECK(medium_contrast == QStringLiteral("中程度のコントラスト"));
  const auto soft_glow = QCoreApplication::translate("QObject", "Soft Glow");
  CHECK(soft_glow == QStringLiteral("ソフトグロー"));
  const auto filter_radius = QCoreApplication::translate("QObject", "Radius");
  CHECK(filter_radius == QStringLiteral("半径"));
  CHECK(patchy::ui::filter_progress_stage_text(patchy::FilterProgressStage::GeneratingClouds) ==
        QStringLiteral("雲模様を生成しています"));
  const auto filter_gallery_action =
      QCoreApplication::translate("patchy::ui::MainWindow", "Filter &Gallery...");
  CHECK(filter_gallery_action == QStringLiteral("フィルターギャラリー(&G)..."));
  const auto filter_gallery_tip = QCoreApplication::translate(
      "patchy::ui::MainWindow", "Preview and apply visual filters and photo looks");
  CHECK(filter_gallery_tip == QStringLiteral("ビジュアルフィルターとフォトルックをプレビューして適用"));
  const auto filter_gallery_cancelled =
      QCoreApplication::translate("patchy::ui::MainWindow", "Cancelled Filter Gallery");
  CHECK(filter_gallery_cancelled == QStringLiteral("フィルターギャラリーをキャンセルしました"));
  const auto filter_gallery_none =
      QCoreApplication::translate("patchy::ui::MainWindow", "No visual filter applied");
  CHECK(filter_gallery_none == QStringLiteral("ビジュアルフィルターは適用されませんでした"));
  const auto filter_gallery_title = QCoreApplication::translate("QObject", "Filter Gallery");
  CHECK(filter_gallery_title == QStringLiteral("フィルターギャラリー"));
  const auto filter_gallery_original = QCoreApplication::translate("QObject", "Original");
  CHECK(filter_gallery_original == QStringLiteral("元画像"));
  const auto filter_gallery_canvas = QCoreApplication::translate("QObject", "Live Canvas Preview");
  CHECK(filter_gallery_canvas == QStringLiteral("キャンバスでライブプレビュー"));
  const auto filter_gallery_apply = QCoreApplication::translate("QObject", "Apply");
  CHECK(filter_gallery_apply == QStringLiteral("適用"));
  const auto filter_gallery_rendering = QCoreApplication::translate("QObject", "Rendering preview...");
  CHECK(filter_gallery_rendering == QStringLiteral("プレビューを描画しています..."));
  const auto filter_gallery_ready = QCoreApplication::translate("QObject", "Ready");
  CHECK(filter_gallery_ready == QStringLiteral("準備完了"));
  const auto curves_load = QCoreApplication::translate("QObject", "Load...");
  CHECK(curves_load == QStringLiteral("読み込み..."));
  const auto curves_save = QCoreApplication::translate("QObject", "Save...");
  CHECK(curves_save == QStringLiteral("保存..."));
  const auto curves_load_title = QCoreApplication::translate("QObject", "Load Curves Preset");
  CHECK(curves_load_title == QStringLiteral("トーンカーブプリセットを読み込み"));
  const auto curves_save_title = QCoreApplication::translate("QObject", "Save Curves Preset");
  CHECK(curves_save_title == QStringLiteral("トーンカーブプリセットを保存"));
  const auto curves_preset_filter =
      QCoreApplication::translate("QObject", "Photoshop Curves Preset (*.acv)");
  CHECK(curves_preset_filter == QStringLiteral("Photoshop トーンカーブプリセット (*.acv)"));
  const auto curves_load_error = QCoreApplication::translate(
      "QObject", "The Curves preset could not be loaded. The file may be damaged or unsupported.");
  CHECK(curves_load_error == QStringLiteral(
                                  "トーンカーブプリセットを読み込めませんでした。ファイルが破損しているか、対応していない可能性があります。"));
  const auto curves_save_error =
      QCoreApplication::translate("QObject", "The Curves preset could not be saved.");
  CHECK(curves_save_error == QStringLiteral("トーンカーブプリセットを保存できませんでした。"));
  const auto curves_target = QCoreApplication::translate("QObject", "Target");
  CHECK(curves_target == QStringLiteral("画像内調整"));
  const auto curves_before = QCoreApplication::translate("QObject", "Before");
  CHECK(curves_before == QStringLiteral("調整前"));
  const auto curves_clipping =
      QCoreApplication::translate("QObject", "Show shadow and highlight clipping together");
  CHECK(curves_clipping == QStringLiteral("シャドウとハイライトのクリッピングを同時に表示"));

  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false));
  QApplication::processEvents();
}

void ui_filter_gallery_action_retranslates() {
  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* action = require_action(window, "filterGalleryAction");
  CHECK(action->text() == QStringLiteral("Filter &Gallery..."));
  CHECK(action->statusTip() == QStringLiteral("Preview and apply visual filters and photo looks"));

  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("ja"), false));
  QApplication::processEvents();
  CHECK(action->text() == QStringLiteral("フィルターギャラリー(&G)..."));
  CHECK(action->statusTip() == QStringLiteral("ビジュアルフィルターとフォトルックをプレビューして適用"));
  patchy::ui::ZoomableImagePreview translated_preview;
  CHECK(translated_preview.toolTip() ==
        QStringLiteral("ドラッグで表示位置を移動できます。マウスホイールでズームします。"));

  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false));
  QApplication::processEvents();
  CHECK(action->text() == QStringLiteral("Filter &Gallery..."));
  CHECK(action->statusTip() == QStringLiteral("Preview and apply visual filters and photo looks"));
}

void ui_polygonal_lasso_tool_retranslates() {
  // GitHub issue 287: the new palette entry, its flyout row and its tooltip follow a
  // runtime language switch like the tools around it.
  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* action = require_action(window, "toolPolygonalLassoAction");
  CHECK(action->text() == QStringLiteral("Polygonal Lasso"));
  CHECK(action->toolTip().startsWith(QStringLiteral("Polygonal Lasso")));

  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("ja"), false));
  QApplication::processEvents();
  CHECK(action->text() == QStringLiteral("多角形選択"));
  CHECK(action->toolTip().startsWith(QStringLiteral("多角形選択")));
  CHECK(!action->toolTip().contains(QStringLiteral("Click to place points")));

  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false));
  QApplication::processEvents();
  CHECK(action->text() == QStringLiteral("Polygonal Lasso"));
  CHECK(action->toolTip().contains(QStringLiteral("Click to place points")));
}

// Points the settings root (and QSettings' user ini root) at `root`, and back at the suite's afterwards.
struct IniRootOverride {
  QString suite_root = QFileInfo(QFileInfo(QSettings(QSettings::IniFormat, QSettings::UserScope,
                                                     QStringLiteral("probe"), QStringLiteral("probe"))
                                               .fileName())
                                     .absolutePath())
                           .absolutePath();
  explicit IniRootOverride(const QString& root) { patchy::ui::set_app_settings_root(root); }
  ~IniRootOverride() { patchy::ui::set_app_settings_root(suite_root); }
  IniRootOverride(const IniRootOverride&) = delete;
  IniRootOverride& operator=(const IniRootOverride&) = delete;
};

void ui_about_dialog_shows_labeled_external_links() {
  bool inspected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchySplashScreen"));
    CHECK(dialog != nullptr);

    // The X in the upper right, like every chrome dialog's title bar (GitHub issue 107).
    auto* close_glyph = dialog->findChild<QToolButton*>(QStringLiteral("splashCloseGlyphButton"));
    CHECK(close_glyph != nullptr);
    if (close_glyph != nullptr) {
      CHECK(close_glyph->isVisible());
      CHECK(close_glyph->geometry().right() >= dialog->width() - 8);
      CHECK(close_glyph->geometry().top() <= 8);
    }

    const auto link_labels = dialog->findChildren<QLabel*>(QStringLiteral("splashHome"));
    CHECK(link_labels.size() == 2);
    QString combined_text;
    for (const auto* label : link_labels) {
      CHECK(label->textFormat() == Qt::RichText);
      CHECK(label->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse));
      CHECK(label->openExternalLinks());
      combined_text += label->text();
      combined_text += QLatin1Char('\n');
    }

    CHECK(combined_text.contains(QStringLiteral("GitHub: ")));
    CHECK(combined_text.contains(QStringLiteral("href=\"https://github.com/SethRobinson/Patchy\"")));
    CHECK(combined_text.contains(QStringLiteral(">SethRobinson/Patchy</a>")));
    CHECK(combined_text.contains(QStringLiteral("Seth's site: ")));
    CHECK(combined_text.contains(QStringLiteral("href=\"https://rtsoft.com\"")));
    CHECK(combined_text.contains(QStringLiteral(">rtsoft.com</a>")));

    // A link color lives inside the rich text, where the stylesheet token check
    // cannot see it. An unresolved one is not an error to Qt, just an unreadable
    // color: the parser drops it and falls back to its own blue.
    CHECK(!combined_text.contains(QLatin1Char('@')));
    CHECK(combined_text.contains(
        QStringLiteral("color:%1;").arg(patchy::ui::theme().splash_link_text.name(QColor::HexRgb))));

    // The version label shares the splashCredit object name with the credit
    // line below it; creation order puts the version first.
    const auto credit_labels = dialog->findChildren<QLabel*>(QStringLiteral("splashCredit"));
    CHECK(credit_labels.size() == 2);
    CHECK(credit_labels.first()->text().startsWith(QStringLiteral("Version ")));
    CHECK(credit_labels.first()->text().endsWith(
        QStringLiteral("(built %1)").arg(patchy::ui::build_timestamp_text())));
    // The credit names Seth and links to his GitHub profile in the themed link color.
    auto* credit = credit_labels.last();
    CHECK(credit->textFormat() == Qt::RichText);
    CHECK(credit->openExternalLinks());
    CHECK(credit->text().startsWith(QStringLiteral("Created by ")));
    CHECK(credit->text().contains(QStringLiteral("href=\"https://github.com/SethRobinson\"")));
    CHECK(credit->text().contains(QStringLiteral(">Seth A. Robinson</a>")));
    CHECK(!credit->text().contains(QStringLiteral("@splash_link_text")));
    CHECK(credit->text().contains(
        QStringLiteral("color:%1;").arg(patchy::ui::theme().splash_link_text.name(QColor::HexRgb))));

    auto* contributors = dialog->findChild<QLabel*>(QStringLiteral("splashContributors"));
    CHECK(contributors != nullptr);
    CHECK(contributors->textFormat() == Qt::RichText);
    CHECK(contributors->openExternalLinks());
    CHECK(contributors->text().startsWith(QStringLiteral("Incredible people who donated suggestions, bug reports, and code: ")));
    CHECK(contributors->text().contains(QStringLiteral("href=\"https://github.com/mcapogna\"")));
    CHECK(contributors->text().contains(QStringLiteral(">mcapogna</a>")));
    CHECK(contributors->text().contains(QStringLiteral("href=\"https://github.com/csbun\"")));
    CHECK(contributors->text().contains(QStringLiteral(">csbun</a>")));
    CHECK(contributors->text().contains(QStringLiteral("href=\"https://github.com/ifloppy\"")));
    CHECK(contributors->text().contains(QStringLiteral(">ifloppy</a>")));
    CHECK(contributors->text().contains(QStringLiteral("href=\"https://github.com/lucastucious\"")));
    CHECK(contributors->text().contains(QStringLiteral(">lucastucious</a>")));
    CHECK(contributors->text().contains(QStringLiteral("href=\"https://github.com/Oppa-YA\"")));
    CHECK(contributors->text().contains(QStringLiteral(">Oppa-YA</a>")));
    CHECK(contributors->text().contains(QStringLiteral("href=\"https://github.com/YuFu-Onship\"")));
    CHECK(contributors->text().contains(QStringLiteral(">YuFu-Onship</a>")));
    CHECK(contributors->text().contains(QStringLiteral("href=\"https://github.com/Uthopik\"")));
    CHECK(contributors->text().endsWith(QStringLiteral(">Uthopik</a>")));
    CHECK(!contributors->text().contains(QLatin1Char('@')));

    auto* settings_caption = dialog->findChild<QLabel*>(QStringLiteral("splashSettingsCaption"));
    CHECK(settings_caption != nullptr);
    CHECK(settings_caption->text() == QStringLiteral("Settings file:"));
    auto* settings_path = dialog->findChild<QLabel*>(QStringLiteral("splashSettingsPath"));
    CHECK(settings_path != nullptr);
    CHECK(settings_path->text() == QDir::toNativeSeparators(patchy::ui::app_settings().fileName()));
    CHECK(settings_path->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
    CHECK(settings_path->wordWrap());
    auto* open_settings_folder = dialog->findChild<QPushButton*>(QStringLiteral("splashOpenSettingsFolderButton"));
    CHECK(open_settings_folder != nullptr);
    CHECK(open_settings_folder->text() == QStringLiteral("Open Settings Folder"));

    // The per-user data folder (dropped fonts, user scripts) gets the same treatment.
    auto* data_caption = dialog->findChild<QLabel*>(QStringLiteral("splashDataCaption"));
    CHECK(data_caption != nullptr);
    CHECK(data_caption->text() == QStringLiteral("User data folder (fonts, scripts):"));
    auto* data_path = dialog->findChild<QLabel*>(QStringLiteral("splashDataPath"));
    CHECK(data_path != nullptr);
    CHECK(data_path->text() == QDir::toNativeSeparators(
                                   QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)));
    CHECK(data_path->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
    CHECK(data_path->wordWrap());
    auto* open_data_folder = dialog->findChild<QPushButton*>(QStringLiteral("splashOpenDataFolderButton"));
    CHECK(open_data_folder != nullptr);
    CHECK(open_data_folder->text() == QStringLiteral("Open Data Folder"));

    // The dialog is as tall as its content at its fixed width; a shorter fixed height
    // squeezed every row on Windows until the first move relaid the column out.
    CHECK(dialog->layout() != nullptr);
    CHECK(dialog->height() >= dialog->layout()->totalHeightForWidth(dialog->width()));
    CHECK(dialog->minimumHeight() == dialog->maximumHeight());
    CHECK(open_data_folder->height() >= open_data_folder->sizeHint().height());
    CHECK(data_path->height() >= data_path->heightForWidth(data_path->width()));

    save_widget_artifact("ui_about_dialog_links", *dialog);
    inspected = true;
    dialog->accept();
  });

  patchy::ui::show_about_splash();
  CHECK(inspected);
}

// Where Patchy.ini sits in the data folder itself (Windows and macOS, app_settings_file_path())
// the About dialog lists that folder once; the suite's own settings live elsewhere, so the test
// above sees both rows, the layout Linux always gets.
void ui_about_dialog_lists_shared_settings_and_data_folder_once() {
  // Test mode keeps AppDataLocation, and so the settings this dialog may write, off the real profile.
  struct StandardPathsTestMode {
    StandardPathsTestMode() { QStandardPaths::setTestModeEnabled(true); }
    ~StandardPathsTestMode() { QStandardPaths::setTestModeEnabled(false); }
  } test_mode;
  // The app's identity, which keys AppDataLocation (main.cpp sets it; the suite has its own).
  struct AppIdentity {
    QString organization = QCoreApplication::organizationName();
    QString application = QCoreApplication::applicationName();
    AppIdentity() {
      QCoreApplication::setOrganizationName(QStringLiteral("RTsoft"));
      QCoreApplication::setApplicationName(QStringLiteral("Patchy"));
    }
    ~AppIdentity() {
      QCoreApplication::setOrganizationName(organization);
      QCoreApplication::setApplicationName(application);
    }
  } identity;
  const auto data_folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const auto organization_folder = QFileInfo(data_folder).absolutePath();
  CHECK(QFileInfo(data_folder).fileName() == QStringLiteral("Patchy"));
  CHECK(QFileInfo(organization_folder).fileName() == QStringLiteral("RTsoft"));
  IniRootOverride override_root(QFileInfo(organization_folder).absolutePath());
  CHECK(QDir::cleanPath(QFileInfo(patchy::ui::app_settings_file_path()).absolutePath()) ==
        QDir::cleanPath(data_folder));

  bool inspected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchySplashScreen"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    CHECK(dialog->findChild<QLabel*>(QStringLiteral("splashSettingsCaption")) == nullptr);
    CHECK(dialog->findChild<QLabel*>(QStringLiteral("splashSettingsPath")) == nullptr);
    CHECK(dialog->findChild<QPushButton*>(QStringLiteral("splashOpenSettingsFolderButton")) == nullptr);
    CHECK(dialog->findChildren<QLabel*>(QStringLiteral("splashDataCaption")).size() == 1);
    auto* caption = dialog->findChild<QLabel*>(QStringLiteral("splashDataCaption"));
    CHECK(caption != nullptr && caption->text() == QStringLiteral("Settings and data folder:"));
    auto* path = dialog->findChild<QLabel*>(QStringLiteral("splashDataPath"));
    CHECK(path != nullptr && path->text() == QDir::toNativeSeparators(data_folder));
    auto* open_folder = dialog->findChild<QPushButton*>(QStringLiteral("splashOpenDataFolderButton"));
    CHECK(open_folder != nullptr && open_folder->text() == QStringLiteral("Open Folder"));
    CHECK(dialog->height() >= dialog->layout()->totalHeightForWidth(dialog->width()));
    save_widget_artifact("ui_about_dialog_shared_folder", *dialog);
    inspected = true;
    dialog->accept();
  });

  patchy::ui::show_about_splash();
  CHECK(inspected);
}

void ui_about_dialog_shows_memory_row() {
  bool inspected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchySplashScreen"));
    CHECK(dialog != nullptr);

    // The row hides itself on platforms with no memory probe; where a probe
    // exists the value is live, so only the prefix is stable.
    auto* memory = dialog->findChild<QLabel*>(QStringLiteral("splashMemory"));
    CHECK(memory != nullptr);
    CHECK(memory->isHidden() || memory->text().startsWith(QStringLiteral("Memory used")));

    // The memory row must not join the splashCredit pair the links test pins.
    CHECK(dialog->findChildren<QLabel*>(QStringLiteral("splashCredit")).size() == 2);

    inspected = true;
    dialog->accept();
  });

  patchy::ui::show_about_splash();
  CHECK(inspected);
}

// The organization name changed from "Seth A. Robinson" to "RTsoft" after 0.98, which
// moves AppDataLocation (user fonts, user scripts). Startup merges the old folder into the
// new one without overwriting, drops identical leftovers, keeps differing ones, and removes
// the emptied legacy tree down to the organization folder.
void ui_app_data_migration_merges_legacy_folder() {
  namespace migration = patchy::ui::app_data_migration;
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto root = temp.path();
  const auto legacy = root + QStringLiteral("/Seth A. Robinson/Patchy");
  const auto current = root + QStringLiteral("/RTsoft/Patchy");
  const auto write = [](const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(bytes);
  };
  const auto read = [](const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  };

  // No legacy folder: nothing happens and the current folder is not created.
  auto result = migration::migrate_app_data_directory(legacy, current);
  CHECK(!result.legacy_found);
  CHECK(!QDir(current).exists());

  write(legacy + QStringLiteral("/user-fonts/Dropped.ttf"), "font-bytes");
  write(legacy + QStringLiteral("/user-fonts/Same.ttf"), "same");
  write(legacy + QStringLiteral("/scripts/Demos/mine.js"), "legacy edit");
  write(legacy + QStringLiteral("/scripts/Empty/.keep"), "");
  QDir().mkpath(legacy + QStringLiteral("/scripts/Nothing"));
  write(current + QStringLiteral("/user-fonts/Same.ttf"), "same");
  write(current + QStringLiteral("/user-fonts/Newer.ttf"), "newer");
  write(current + QStringLiteral("/scripts/Demos/mine.js"), "current edit");

  result = migration::migrate_app_data_directory(legacy, current);
  CHECK(result.legacy_found);
  CHECK(result.files_moved == 2);        // Dropped.ttf and .keep
  CHECK(result.duplicates_removed == 1); // Same.ttf
  CHECK(result.conflicts_kept == 1);     // mine.js
  CHECK(!result.completed);
  CHECK(read(current + QStringLiteral("/user-fonts/Dropped.ttf")) == "font-bytes");
  CHECK(read(current + QStringLiteral("/user-fonts/Newer.ttf")) == "newer");
  CHECK(read(current + QStringLiteral("/scripts/Demos/mine.js")) == "current edit");
  CHECK(QFileInfo::exists(current + QStringLiteral("/scripts/Empty/.keep")));
  CHECK(!QFileInfo::exists(legacy + QStringLiteral("/user-fonts/Dropped.ttf")));
  CHECK(!QFileInfo::exists(legacy + QStringLiteral("/user-fonts/Same.ttf")));
  CHECK(!QDir(legacy + QStringLiteral("/user-fonts")).exists());
  CHECK(!QDir(legacy + QStringLiteral("/scripts/Empty")).exists());
  CHECK(!QDir(legacy + QStringLiteral("/scripts/Nothing")).exists());
  CHECK(read(legacy + QStringLiteral("/scripts/Demos/mine.js")) == "legacy edit");

  // A second run is a no-op for the conflict, and once the user resolves it the legacy
  // tree disappears entirely, organization folder included.
  result = migration::migrate_app_data_directory(legacy, current);
  CHECK(result.legacy_found && result.conflicts_kept == 1 && result.files_moved == 0);
  CHECK(QFile::remove(legacy + QStringLiteral("/scripts/Demos/mine.js")));
  result = migration::migrate_app_data_directory(legacy, current);
  CHECK(result.legacy_found && result.completed);
  CHECK(!QDir(root + QStringLiteral("/Seth A. Robinson")).exists());
  CHECK(read(current + QStringLiteral("/scripts/Demos/mine.js")) == "current edit");

  // The running app resolves the legacy folder from the legacy organization name and
  // restores the current name afterwards.
  const auto organization = QCoreApplication::organizationName();
  QCoreApplication::setOrganizationName(QStringLiteral("RTsoft"));
  const auto legacy_dir = migration::legacy_app_data_directory();
  CHECK(legacy_dir.contains(QStringLiteral("Seth A. Robinson")));
  CHECK(QCoreApplication::organizationName() == QStringLiteral("RTsoft"));
  QCoreApplication::setOrganizationName(organization);
}

void ui_app_settings_migration_moves_legacy_settings_folder() {
  namespace migration = patchy::ui::app_data_migration;
  const auto same_path = [](const QString& a, const QString& b) {
    return QDir::cleanPath(QFileInfo(a).absoluteFilePath()) == QDir::cleanPath(QFileInfo(b).absoluteFilePath());
  };
  const auto write = [](const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(bytes);
  };

  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto root = temp.path();
  {
    IniRootOverride override_root(root);
    const auto legacy = root + QStringLiteral("/Patchy");
    const auto current = root + QStringLiteral("/RTsoft/Patchy");
    // Preferences now share the RTsoft/Patchy folder; the legacy one was the "Patchy" organization's.
    CHECK(same_path(patchy::ui::app_settings_file_path(), current + QStringLiteral("/Patchy.ini")));
    CHECK(same_path(patchy::ui::app_settings().fileName(), current + QStringLiteral("/Patchy.ini")));
    CHECK(same_path(migration::legacy_settings_directory(), legacy));
#ifdef Q_OS_MACOS
    // Unredirected, a Mac keeps settings in Application Support, not Qt's ~/.config ini root.
    patchy::ui::set_app_settings_root({});
    CHECK(same_path(patchy::ui::app_settings_file_path(),
                    QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                        QStringLiteral("/RTsoft/Patchy/Patchy.ini")));
    patchy::ui::set_app_settings_root(root);
#endif

    // No legacy folder: nothing happens.
    auto result = migration::migrate_legacy_settings();
    CHECK(!result.legacy_found);
    CHECK(!QDir(current).exists());

    write(legacy + QStringLiteral("/Patchy.ini"), "[preferences]\ncolorScheme=dark\n");
    write(legacy + QStringLiteral("/brushes/tip.json"), "{\"name\":\"Tip\"}");
    write(legacy + QStringLiteral("/patterns/pattern.png"), "png-bytes");
    result = migration::migrate_legacy_settings();
    CHECK(result.legacy_found && result.completed);
    CHECK(result.files_moved == 3 && result.conflicts_kept == 0);
    CHECK(!QDir(legacy).exists());
    CHECK(QDir(root).exists());
    CHECK(patchy::ui::app_settings().value(QStringLiteral("preferences/colorScheme")).toString() ==
          QStringLiteral("dark"));
    // The libraries keep resolving beside the settings file.
    const auto settings_dir = QFileInfo(patchy::ui::app_settings().fileName()).absolutePath();
    CHECK(QFileInfo::exists(settings_dir + QStringLiteral("/brushes/tip.json")));
    CHECK(QFileInfo::exists(settings_dir + QStringLiteral("/patterns/pattern.png")));

    // Idempotent once the legacy folder is gone.
    CHECK(!migration::migrate_legacy_settings().legacy_found);
  }

  // The user ini root is never removed, even when the migration leaves it empty.
  QTemporaryDir bare;
  CHECK(bare.isValid());
  {
    IniRootOverride override_root(bare.path());
    CHECK(QDir().mkpath(bare.path() + QStringLiteral("/Patchy/brushes")));
    const auto result = migration::migrate_legacy_settings();
    CHECK(result.legacy_found && result.completed);
    CHECK(!QDir(bare.path() + QStringLiteral("/Patchy")).exists());
    CHECK(QDir(bare.path()).exists());
  }
}

void ui_app_data_migration_moves_machine_data_to_local() {
  namespace migration = patchy::ui::app_data_migration;
  const auto write = [](const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(bytes);
  };
  const auto read = [](const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  };
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto roaming = temp.path() + QStringLiteral("/Roaming/RTsoft/Patchy");
  const auto local = temp.path() + QStringLiteral("/Local/RTsoft/Patchy");
  const auto settings = temp.path() + QStringLiteral("/config/RTsoft/Patchy");

  // Where the folders coincide (macOS, Linux AppData vs AppLocalData) nothing moves.
  write(roaming + QStringLiteral("/ai-models/model.onnx"), "model");
  CHECK(!migration::migrate_local_data(roaming, roaming, roaming).legacy_found);
  CHECK(QFileInfo::exists(roaming + QStringLiteral("/ai-models/model.onnx")));

  write(roaming + QStringLiteral("/AutoRecover/1234-5678/document.psd"), "recovery");
  write(roaming + QStringLiteral("/stress-reports/stress-latest.json"), "roaming report");
  write(roaming + QStringLiteral("/user-fonts/Dropped.ttf"), "font");
  write(settings + QStringLiteral("/stress-reports/stress-old.json"), "settings report");
  write(local + QStringLiteral("/stress-reports/stress-latest.json"), "local report");
  const auto result = migration::migrate_local_data(roaming, local, settings);
  CHECK(result.legacy_found);
  CHECK(result.files_moved == 3);     // model.onnx, document.psd, stress-old.json
  CHECK(result.conflicts_kept == 1);  // the roaming stress-latest.json
  CHECK(!result.completed);
  CHECK(read(local + QStringLiteral("/ai-models/model.onnx")) == "model");
  CHECK(read(local + QStringLiteral("/AutoRecover/1234-5678/document.psd")) == "recovery");
  CHECK(read(local + QStringLiteral("/stress-reports/stress-old.json")) == "settings report");
  CHECK(read(local + QStringLiteral("/stress-reports/stress-latest.json")) == "local report");
  CHECK(!QDir(roaming + QStringLiteral("/ai-models")).exists());
  CHECK(!QDir(roaming + QStringLiteral("/AutoRecover")).exists());
  CHECK(!QDir(settings + QStringLiteral("/stress-reports")).exists());
  CHECK(read(roaming + QStringLiteral("/stress-reports/stress-latest.json")) == "roaming report");
  // Everything else in the roaming and settings folders stays, and so do the folders.
  CHECK(read(roaming + QStringLiteral("/user-fonts/Dropped.ttf")) == "font");
  CHECK(QDir(settings).exists());
}

void ui_frameless_window_edges_resize() {
  if (!patchy::ui::MainWindow::use_custom_window_chrome()) {
    // macOS uses the native frame; the OS owns the resize borders and the Qt-level edge
    // machinery under test here is deliberately inert.
    patchy::test::skip("native window frame owns resize borders on this platform");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  window.resize(980, 720);
  QApplication::processEvents();

#ifdef Q_OS_WIN
  if (QGuiApplication::platformName() == QStringLiteral("windows")) {
    const auto style = GetWindowLongPtrW(reinterpret_cast<HWND>(window.winId()), GWL_STYLE);
    CHECK((style & WS_THICKFRAME) != 0);
    CHECK((style & WS_CAPTION) == 0);
  }
#endif

  const auto start = window.geometry();
  const QPoint right_edge(window.width() - 2, window.height() / 2);
  send_mouse(window, QEvent::MouseButtonPress, right_edge, Qt::LeftButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseMove, right_edge + QPoint(90, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseButtonRelease, right_edge + QPoint(90, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.width() >= start.width() + 70);
  CHECK(window.height() == start.height());

  const auto widened = window.geometry();
  const QPoint bottom_right(window.width() - 2, window.height() - 2);
  send_mouse(window, QEvent::MouseButtonPress, bottom_right, Qt::LeftButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseMove, bottom_right + QPoint(45, 55), Qt::NoButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseButtonRelease, bottom_right + QPoint(45, 55), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.width() >= widened.width() + 30);
  CHECK(window.height() >= widened.height() + 40);

  const auto expanded = window.geometry();
  const QPoint left_edge(2, window.height() / 2);
  send_mouse(window, QEvent::MouseButtonPress, left_edge, Qt::LeftButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseMove, left_edge - QPoint(60, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseButtonRelease, left_edge - QPoint(60, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.x() <= expanded.x() - 45);
  CHECK(window.width() >= expanded.width() + 45);
}

void ui_maximized_title_bar_click_keeps_window_maximized() {
  if (!patchy::ui::MainWindow::use_custom_window_chrome()) {
    patchy::test::skip("native window frame owns the title bar on this platform");
  }
  // The Qt-level title-bar path (Linux; the real Windows window answers HTCAPTION instead,
  // see MainWindow::nativeEvent) must follow the native contract: a click or a
  // sub-threshold wiggle leaves a maximized window maximized, a real drag restores it, and
  // a double-click toggles the state. Offscreen has no startSystemMove, so the drag takes
  // the Qt-level restore.
  patchy::ui::MainWindow window;
  show_window(window);
  window.showMaximized();
  QApplication::processEvents();
  CHECK(window.isMaximized());

  auto* bar = window.menuBar();
  CHECK(bar != nullptr);
  CHECK(!bar->actions().isEmpty());
  auto* controls = window.findChild<QWidget*>(QStringLiteral("windowChromeControls"));
  CHECK(controls != nullptr);
  // The offscreen screen is small, so a maximized menu bar may have titles running up to
  // the window controls; find a real blank spot (the badge gutter before the first title
  // also counts, it is transparent for mouse events) rather than assuming one past the
  // last title.
  QPoint blank(-1, bar->height() / 2);
  for (int x = controls->x() - 1; x >= 0; --x) {
    if (bar->actionAt(QPoint(x, blank.y())) == nullptr) {
      blank.setX(x);
      break;
    }
  }
  CHECK(blank.x() >= 0);
  CHECK(bar->actionAt(blank) == nullptr);

  send_mouse(*bar, QEvent::MouseButtonPress, blank, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*bar, QEvent::MouseButtonRelease, blank, Qt::LeftButton, Qt::NoButton);
  CHECK(window.isMaximized());

  const QPoint wiggle = blank + QPoint(2, 1);
  send_mouse(*bar, QEvent::MouseButtonPress, blank, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*bar, QEvent::MouseMove, wiggle, Qt::NoButton, Qt::LeftButton);
  send_mouse(*bar, QEvent::MouseButtonRelease, wiggle, Qt::LeftButton, Qt::NoButton);
  CHECK(window.isMaximized());

  const QPoint dragged = blank + QPoint(QApplication::startDragDistance() * 4, 30);
  send_mouse(*bar, QEvent::MouseButtonPress, blank, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*bar, QEvent::MouseMove, dragged, Qt::NoButton, Qt::LeftButton);
  CHECK(!window.isMaximized());
  send_mouse(*bar, QEvent::MouseButtonRelease, dragged, Qt::LeftButton, Qt::NoButton);
  CHECK(!window.isMaximized());

  const auto double_click = [&] {
    send_mouse(*bar, QEvent::MouseButtonPress, blank, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*bar, QEvent::MouseButtonRelease, blank, Qt::LeftButton, Qt::NoButton);
    send_mouse(*bar, QEvent::MouseButtonDblClick, blank, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*bar, QEvent::MouseButtonRelease, blank, Qt::LeftButton, Qt::NoButton);
  };
  double_click();
  CHECK(window.isMaximized());
  double_click();
  CHECK(!window.isMaximized());

  // Menu titles and the window controls are not title bar: a maximized window keeps its
  // state when they are pressed through the same path.
  window.showMaximized();
  QApplication::processEvents();
  CHECK(window.isMaximized());
  const QPoint on_controls = controls->pos() + QPoint(controls->width() / 2, controls->height() / 2);
  CHECK(!patchy::ui::MainWindowTestAccess::title_bar_drag_area_contains(window, on_controls));
  CHECK(!patchy::ui::MainWindowTestAccess::title_bar_drag_area_contains(
      window, bar->actionGeometry(bar->actions().constFirst()).center()));
  CHECK(patchy::ui::MainWindowTestAccess::title_bar_drag_area_contains(window, blank));
}

// The custom chrome is a compile-time platform contract: Windows and Linux draw their own
// frame (Linux through Qt::FramelessWindowHint, so Qt's Wayland client-side decoration
// never appears), macOS keeps the native one. Where it applies, the menu-bar row holds the
// three window controls and they drive the window state.
void ui_window_chrome_controls_drive_window_state() {
#if defined(Q_OS_WIN) || (defined(Q_OS_LINUX) && !defined(Q_OS_WASM))
  CHECK(patchy::ui::MainWindow::use_custom_window_chrome());
#else
  CHECK(!patchy::ui::MainWindow::use_custom_window_chrome());
#endif
  if (!patchy::ui::MainWindow::use_custom_window_chrome()) {
    std::cout << "  note: native window frame owns the window controls on this platform\n";
    return;
  }
  patchy::ui::MainWindow window;
  show_window_empty(window);
  CHECK(window.windowFlags().testFlag(Qt::FramelessWindowHint));
  auto* bar = window.menuBar();
  CHECK(bar != nullptr);
  // The menu bar is the title bar, so it must stay inside the window (never a global
  // menu bar) and be tall enough to hold the controls.
  CHECK(!bar->isNativeMenuBar());

  auto* controls = window.findChild<QWidget*>(QStringLiteral("windowChromeControls"));
  auto* minimize = window.findChild<QToolButton*>(QStringLiteral("windowMinimizeButton"));
  auto* maximize = window.findChild<QToolButton*>(QStringLiteral("windowMaximizeButton"));
  auto* close = window.findChild<QToolButton*>(QStringLiteral("windowCloseButton"));
  CHECK(controls != nullptr);
  CHECK(minimize != nullptr && maximize != nullptr && close != nullptr);
  if (controls == nullptr || minimize == nullptr || maximize == nullptr || close == nullptr) {
    return;
  }
  CHECK(controls->parentWidget() == bar);
  CHECK(controls->isVisible());
  CHECK(bar->height() >= controls->height());
  // Flush right in the menu-bar row, close last.
  CHECK(controls->x() + controls->width() == bar->width());
  CHECK(minimize->x() < maximize->x());
  CHECK(maximize->x() < close->x());
  for (auto* button : {minimize, maximize, close}) {
    CHECK(button->property("windowChromeButton").toBool());
    CHECK(!button->icon().isNull());
    CHECK(button->focusPolicy() == Qt::NoFocus);
  }

  CHECK(!window.isMaximized());
  maximize->click();
  QApplication::processEvents();
  CHECK(window.isMaximized());
  maximize->click();
  QApplication::processEvents();
  CHECK(!window.isMaximized());

  minimize->click();
  QApplication::processEvents();
  CHECK(window.isMinimized());
  window.showNormal();
  QApplication::processEvents();
  CHECK(!window.isMinimized());

  CHECK(window.isVisible());
  close->click();
  QApplication::processEvents();
  CHECK(!window.isVisible());
}

// The Qt-level title-bar drag of a restored window (Linux without a system move, the
// offscreen platform): a click or a sub-threshold wiggle leaves the window where it is,
// and a drag past the threshold moves it by the full pointer travel, the threshold
// included.
void ui_title_bar_drag_moves_window_only_past_threshold() {
  if (!patchy::ui::MainWindow::use_custom_window_chrome()) {
    patchy::test::skip("native window frame owns the title bar on this platform");
  }
  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.move(120, 90);
  QApplication::processEvents();
  CHECK(!window.isMaximized());

  auto* bar = window.menuBar();
  auto* controls = window.findChild<QWidget*>(QStringLiteral("windowChromeControls"));
  CHECK(bar != nullptr && controls != nullptr);
  QPoint blank(-1, bar->height() / 2);
  for (int x = controls->x() - 1; x >= 0; --x) {
    if (bar->actionAt(QPoint(x, blank.y())) == nullptr) {
      blank.setX(x);
      break;
    }
  }
  CHECK(blank.x() >= 0);
  CHECK(patchy::ui::MainWindowTestAccess::title_bar_drag_area_contains(window, blank));

  const QPoint start = window.pos();
  send_mouse(*bar, QEvent::MouseButtonPress, blank, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*bar, QEvent::MouseButtonRelease, blank, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.pos() == start);

  const QPoint wiggle = blank + QPoint(2, 1);
  send_mouse(*bar, QEvent::MouseButtonPress, blank, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*bar, QEvent::MouseMove, wiggle, Qt::NoButton, Qt::LeftButton);
  send_mouse(*bar, QEvent::MouseButtonRelease, wiggle, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.pos() == start);

  // send_mouse derives the global position from the widget, so once the window has
  // followed the pointer the same local point is the pointer's new global place: each
  // step's local offset is the travel since the previous step.
  const QPoint travel(QApplication::startDragDistance() * 3, 24);
  send_mouse(*bar, QEvent::MouseButtonPress, blank, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*bar, QEvent::MouseMove, blank + travel, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(window.pos() == start + travel);
  send_mouse(*bar, QEvent::MouseMove, blank + QPoint(15, -6), Qt::NoButton, Qt::LeftButton);
  send_mouse(*bar, QEvent::MouseButtonRelease, blank + QPoint(15, -6), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.pos() == start + travel + QPoint(15, -6));

  // Released: further motion with no button moves nothing.
  send_mouse(*bar, QEvent::MouseMove, blank, Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.pos() == start + travel + QPoint(15, -6));

  // A press on a menu title or the window controls is not a title-bar press.
  const QPoint before_controls = window.pos();
  const QPoint on_controls = controls->pos() + QPoint(controls->width() / 2, controls->height() / 2);
  CHECK(!patchy::ui::MainWindowTestAccess::title_bar_drag_area_contains(window, on_controls));
  send_mouse(*bar, QEvent::MouseButtonPress, on_controls, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*bar, QEvent::MouseMove, on_controls + travel, Qt::NoButton, Qt::LeftButton);
  send_mouse(*bar, QEvent::MouseButtonRelease, on_controls + travel, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.pos() == before_controls);
}

// GitHub issue 196: the menu bar's 35 px of left padding makes room for the app badge the
// frameless chrome draws there. Where the desktop draws the title bar (the Linux system
// title bar) there is no badge, so the first menu starts at the edge like GIMP's and
// Krita's, one item padding from it, which is half the space between two menu titles.
// The bare bar below is that menu bar on any platform: configure_window_chrome leaves
// it untouched when the chrome is off. The style's own menu-bar margin (5 px on Windows,
// 0 with Fusion) applies to both, so the bars differ by exactly the badge's padding.
void ui_menu_bar_reserves_badge_space_only_with_custom_chrome() {
  constexpr int kBadgePadding = 35;
  const auto menu_titles = {QStringLiteral("File"), QStringLiteral("Edit"), QStringLiteral("Image"),
                            QStringLiteral("Layer")};
  QList<QImage> strips;
  std::optional<int> chrome_first;
  int badge_right = 0;
  bool chrome_has_badge = false;
  int chrome_actions = 0;
  {
    patchy::ui::MainWindow window;
    show_window_empty(window);
    auto* bar = window.menuBar();
    const auto actions = bar->actions();
    chrome_actions = static_cast<int>(actions.size());
    auto* badge = bar->findChild<QLabel*>(QStringLiteral("patchyBadge"));
    chrome_has_badge = badge != nullptr;
    if (patchy::ui::MainWindow::use_custom_window_chrome() && !actions.isEmpty()) {
      chrome_first = bar->actionGeometry(actions.first()).left();
      badge_right = badge != nullptr ? badge->geometry().right() + 1 : 0;
      std::cout << "custom chrome: badge ends at " << badge_right << ", first menu at " << *chrome_first
                << std::endl;
      strips << bar->grab(QRect(0, 0, std::min(bar->width(), 360), bar->height())).toImage();
    }
  }

  QMainWindow plain;
  plain.setStyleSheet(patchy::ui::photoshop_style());
  auto* plain_bar = new QMenuBar(&plain);
  plain_bar->setNativeMenuBar(false);
  plain.setMenuBar(plain_bar);
  for (const auto& title : menu_titles) {
    plain_bar->addMenu(title);
  }
  plain.setCentralWidget(new QWidget(&plain));
  plain.resize(420, 120);
  plain.show();
  QApplication::processEvents();
  const auto plain_actions = plain_bar->actions();
  const auto plain_first = plain_actions.isEmpty() ? -1 : plain_bar->actionGeometry(plain_actions.first()).left();
  std::cout << "system title bar: first menu at " << plain_first << std::endl;
  strips << plain_bar->grab(QRect(0, 0, std::min(plain_bar->width(), 360), plain_bar->height())).toImage();
  save_stacked_artifact("issue196_menu_bar_left_edge", strips);

  CHECK(chrome_actions >= 2);
  CHECK(plain_actions.size() == 4);
  CHECK(chrome_has_badge == patchy::ui::MainWindow::use_custom_window_chrome());
  CHECK(plain_first >= 0);
  CHECK(plain_first < kBadgePadding / 2);
  if (chrome_first.has_value()) {
    CHECK(*chrome_first >= badge_right);
    CHECK(*chrome_first - plain_first == kBadgePadding);
  }
}

// GitHub issue 145: window/systemTitleBar (a compatibility contract) turns the Linux
// frameless chrome off for the process, giving the desktop its own title bar and frame;
// the key is stored but inert on Windows and macOS.
void ui_system_title_bar_preference_disables_custom_chrome() {
  {
    SettingsValueRestorer restore_key(QStringLiteral("window/systemTitleBar"));
    patchy::ui::set_stored_system_title_bar(true);
    CHECK(patchy::ui::stored_system_title_bar());
    patchy::ui::MainWindowTestAccess::reset_window_chrome_gate_cache();
#if defined(Q_OS_WIN)
    CHECK(patchy::ui::MainWindow::use_custom_window_chrome());
    CHECK(window_has_custom_chrome());
#else
    CHECK(!patchy::ui::MainWindow::use_custom_window_chrome());
    CHECK(!window_has_custom_chrome());
#endif
  }
  patchy::ui::MainWindowTestAccess::reset_window_chrome_gate_cache();
}

// Without the key the chrome is the platform default: frameless on Windows and Linux,
// the native frame on macOS.
void ui_system_title_bar_preference_defaults_off() {
  {
    SettingsValueRestorer restore_key(QStringLiteral("window/systemTitleBar"));
    {
      auto settings = patchy::ui::app_settings();
      settings.remove(QStringLiteral("window/systemTitleBar"));
      settings.sync();
    }
    CHECK(!patchy::ui::stored_system_title_bar());
    CHECK(!patchy::ui::stored_system_title_bar_choice().has_value());
    // A desktop without a global menu bar (offscreen reports none either).
    patchy::ui::MainWindowTestAccess::set_global_menu_bar_probe(false);
    patchy::ui::MainWindowTestAccess::reset_window_chrome_gate_cache();
#if defined(Q_OS_WIN) || (defined(Q_OS_LINUX) && !defined(Q_OS_WASM))
    CHECK(patchy::ui::MainWindow::use_custom_window_chrome());
    CHECK(window_has_custom_chrome());
#else
    CHECK(!patchy::ui::MainWindow::use_custom_window_chrome());
    CHECK(!window_has_custom_chrome());
#endif
  }
  patchy::ui::MainWindowTestAccess::set_global_menu_bar_probe(std::nullopt);
  patchy::ui::MainWindowTestAccess::reset_window_chrome_gate_cache();
}

// GitHub issue 161: until the user sets the preference, Linux follows the desktop. Where a
// global menu bar takes the menus (KDE's Global Menu, elementary), the frameless chrome
// would keep them in the window, so the system title bar is the default there; an
// explicit choice still wins either way.
void ui_system_title_bar_follows_global_menu_bar_until_set() {
  {
    SettingsValueRestorer restore_key(QStringLiteral("window/systemTitleBar"));
    {
      auto settings = patchy::ui::app_settings();
      settings.remove(QStringLiteral("window/systemTitleBar"));
      settings.sync();
    }
    patchy::ui::MainWindowTestAccess::set_global_menu_bar_probe(true);
    patchy::ui::MainWindowTestAccess::reset_window_chrome_gate_cache();
    CHECK(patchy::ui::MainWindow::system_title_bar_effective() == kSystemTitleBarPreferenceApplies);
#if defined(Q_OS_LINUX) && !defined(Q_OS_WASM)
    CHECK(patchy::ui::MainWindow::desktop_has_global_menu_bar());
    CHECK(!patchy::ui::MainWindow::use_custom_window_chrome());
    CHECK(!window_has_custom_chrome());
#elif defined(Q_OS_WIN)
    CHECK(patchy::ui::MainWindow::use_custom_window_chrome());
#endif
    // Nothing was written: the default keeps following the desktop.
    CHECK(!patchy::ui::stored_system_title_bar_choice().has_value());

    // The user turned it off on that desktop: their choice wins.
    patchy::ui::set_stored_system_title_bar(false);
    CHECK(patchy::ui::stored_system_title_bar_choice() == std::optional<bool>(false));
    patchy::ui::MainWindowTestAccess::reset_window_chrome_gate_cache();
    CHECK(!patchy::ui::MainWindow::system_title_bar_effective());
#if defined(Q_OS_WIN) || (defined(Q_OS_LINUX) && !defined(Q_OS_WASM))
    CHECK(patchy::ui::MainWindow::use_custom_window_chrome());
#endif
  }
  patchy::ui::MainWindowTestAccess::set_global_menu_bar_probe(std::nullopt);
  patchy::ui::MainWindowTestAccess::reset_window_chrome_gate_cache();
}

// Preferences shows the setting in effect: on a global-menu desktop with the key unset the
// box starts checked, accepting it unchanged writes nothing, and unchecking stores false.
void ui_preferences_system_title_bar_checkbox_shows_desktop_default() {
  SettingsValueRestorer restore_key(QStringLiteral("window/systemTitleBar"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("window/systemTitleBar"));
    settings.sync();
  }
  patchy::ui::MainWindowTestAccess::set_global_menu_bar_probe(true);
  patchy::ui::MainWindowTestAccess::reset_window_chrome_gate_cache();
  {
    patchy::ui::MainWindow window;
    show_window(window);
    run_preferences_dialog(window, /*accept=*/true, [&](QDialog& dialog) {
      auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("preferencesSystemTitleBarCheck"));
      CHECK((check != nullptr) == kSystemTitleBarPreferenceApplies);
      if (check != nullptr) {
        CHECK(check->isChecked());
      }
    });
    CHECK(find_top_level_dialog(QStringLiteral("preferencesSystemTitleBarMessageBox")) == nullptr);
    CHECK(!patchy::ui::stored_system_title_bar_choice().has_value());

    if (kSystemTitleBarPreferenceApplies) {
      bool dismissed_message = false;
      run_preferences_dialog(window, /*accept=*/true, [&](QDialog& dialog) {
        auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("preferencesSystemTitleBarCheck"));
        CHECK(check != nullptr);
        if (check == nullptr) {
          return;
        }
        check->setChecked(false);
        QTimer::singleShot(0, [&] {
          auto* message = qobject_cast<QMessageBox*>(
              find_top_level_dialog(QStringLiteral("preferencesSystemTitleBarMessageBox")));
          CHECK(message != nullptr);
          if (message != nullptr) {
            message->accept();
            dismissed_message = true;
          }
        });
      });
      CHECK(dismissed_message);
      CHECK(patchy::ui::stored_system_title_bar_choice() == std::optional<bool>(false));
    }
  }
  patchy::ui::MainWindowTestAccess::set_global_menu_bar_probe(std::nullopt);
  patchy::ui::MainWindowTestAccess::reset_window_chrome_gate_cache();
}

// Preferences > Application offers the checkbox on Linux only. Accepting a change stores
// the key and raises the restart notice, like the interface scale; the running window
// keeps its chrome until the next launch.
void ui_preferences_system_title_bar_checkbox_persists_key() {
  SettingsValueRestorer restore_key(QStringLiteral("window/systemTitleBar"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("window/systemTitleBar"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  const bool chrome_before = window.windowFlags().testFlag(Qt::FramelessWindowHint);

  bool dismissed_message = false;
  run_preferences_dialog(window, /*accept=*/true, [&](QDialog& dialog) {
    auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("preferencesSystemTitleBarCheck"));
    CHECK((check != nullptr) == kSystemTitleBarPreferenceApplies);
    if (check == nullptr) {
      return;
    }
    CHECK(!check->isChecked());
    CHECK(check->parentWidget() == dialog.findChild<QWidget*>(QStringLiteral("preferencesApplicationGroup")));
    check->setChecked(true);
    QTimer::singleShot(0, [&] {
      auto* message =
          qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("preferencesSystemTitleBarMessageBox")));
      CHECK(message != nullptr);
      if (message != nullptr) {
        message->accept();
        dismissed_message = true;
      }
    });
  });
  CHECK(patchy::ui::stored_system_title_bar() == kSystemTitleBarPreferenceApplies);
  CHECK(dismissed_message == kSystemTitleBarPreferenceApplies);
  CHECK(find_top_level_dialog(QStringLiteral("preferencesSystemTitleBarMessageBox")) == nullptr);
  // Applies on the next launch only.
  CHECK(window.windowFlags().testFlag(Qt::FramelessWindowHint) == chrome_before);

  if (kSystemTitleBarPreferenceApplies) {
    // Reopening shows the stored state; accepting without a change raises no notice.
    run_preferences_dialog(window, /*accept=*/true, [&](QDialog& dialog) {
      auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("preferencesSystemTitleBarCheck"));
      CHECK(check != nullptr && check->isChecked());
    });
    CHECK(find_top_level_dialog(QStringLiteral("preferencesSystemTitleBarMessageBox")) == nullptr);
    CHECK(patchy::ui::stored_system_title_bar());
  }
}

void ui_right_edge_scrollbars_remain_draggable() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  for (int index = 0; index < 48; ++index) {
    document.add_pixel_layer("Scrollable Layer " + std::to_string(index + 1),
                             solid_pixels(64, 64, patchy::PixelFormat::rgba8(),
                                          QColor(40 + index * 3 % 180, 80, 220, 255)));
  }

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Scrollbar Edge"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  layer_list->setFixedHeight(180);
  QApplication::processEvents();

  auto* scroll = layer_list->verticalScrollBar();
  CHECK(scroll != nullptr);
  CHECK(scroll->isVisible());
  CHECK(scroll->maximum() > 0);
  scroll->setValue(scroll->maximum() / 3);
  QApplication::processEvents();

  QStyleOptionSlider option;
  option.initFrom(scroll);
  option.orientation = scroll->orientation();
  option.minimum = scroll->minimum();
  option.maximum = scroll->maximum();
  option.singleStep = scroll->singleStep();
  option.pageStep = scroll->pageStep();
  option.sliderPosition = scroll->sliderPosition();
  option.sliderValue = scroll->value();
  option.upsideDown = scroll->invertedAppearance();
  const auto handle = scroll->style()->subControlRect(QStyle::CC_ScrollBar, &option,
                                                      QStyle::SC_ScrollBarSlider, scroll);
  CHECK(handle.isValid());
  const auto start_x = std::clamp(scroll->width() - 2, handle.left(), handle.right());
  const QPoint start(start_x, handle.center().y());
  CHECK(handle.contains(start));
  CHECK(scroll->mapTo(&window, start).x() >= window.width() - 10);

  const auto geometry_before = window.geometry();
  const auto scroll_before = scroll->value();
  const auto end = start + QPoint(0, 55);
  send_mouse(*scroll, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*scroll, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  send_mouse(*scroll, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(window.geometry() == geometry_before);
  CHECK(scroll->value() > scroll_before);
}

// A relaunch hands its foreground right to the running instance at the far end of the
// single-instance pipe, so it must be able to name that process.
void ui_single_instance_socket_names_the_server_process() {
  QLocalServer server;
  const auto name = QStringLiteral("Patchy-SingleInstanceTest-%1").arg(QCoreApplication::applicationPid());
  QLocalServer::removeServer(name);
  CHECK(server.listen(name));
  QLocalSocket client;
  client.connectToServer(name);
  CHECK(client.waitForConnected(2000));
  const auto process_id = patchy::ui::local_socket_server_process_id(client);
#ifdef Q_OS_WIN
  CHECK(process_id.has_value() && *process_id == QCoreApplication::applicationPid());
#else
  CHECK(!process_id.has_value());
  CHECK(!patchy::ui::allow_local_socket_server_to_take_foreground(client));
#endif
  client.disconnectFromServer();
  QLocalSocket unconnected;
  CHECK(!patchy::ui::local_socket_server_process_id(unconnected).has_value());
}

// A relaunch restores a minimized window, and with a modal dialog open it is the dialog that
// ends up active: file opens wait for the dialog, the foreground must not.
void ui_second_instance_brings_window_and_modal_dialog_forward() {
  patchy::ui::MainWindow window;
  show_window(window);
  window.showMinimized();
  QApplication::processEvents();
  CHECK(window.isMinimized());
  window.bring_to_front_for_second_instance();
  QApplication::processEvents();
  CHECK(!window.isMinimized());
  CHECK(window.isVisible());

  QDialog dialog(&window);
  dialog.setObjectName(QStringLiteral("secondInstanceTestDialog"));
  dialog.setModal(true);
  dialog.show();
  QApplication::processEvents();
  CHECK(QApplication::activeModalWidget() == &dialog);
  // Offscreen lets the blocked window take activation, which is the state a relaunch must undo.
  window.activateWindow();
  CHECK(QTest::qWaitFor([&window] { return QApplication::activeWindow() == &window; }, 2000));
  window.bring_to_front_for_second_instance();
  CHECK(QTest::qWaitFor([&dialog] { return QApplication::activeWindow() == &dialog; }, 2000));
  dialog.reject();
}

void ui_svg_icon_resources_are_registered() {
  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/new.svg")));
  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/mask.svg")));
  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/selection-add.svg")));
  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/channel-save-selection.svg")));
  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/channel-load-selection.svg")));

  const QIcon icon(QStringLiteral(":/patchy/icons/new.svg"));
  CHECK(!icon.isNull());
  CHECK(!icon.pixmap(QSize(32, 32)).isNull());

  CHECK(!require_action(window, "layerNewAction")->icon().isNull());
  CHECK(!require_action(window, "layerAddMaskAction")->icon().isNull());
}

// The icon engine recolors a fixed ten-color vocabulary. A new icon drawn with a
// color outside it would render fine in Dark and then stay dark-on-dark in Light,
// which no rendered test would catch. Walk the resources instead.
void ui_icon_color_map_covers_every_authored_color() {
  QSet<QString> mapped;
  for (const auto& [source, member] : patchy::ui::icon_color_roles()) {
    mapped.insert(QString(source).toLower());
  }
  CHECK(!mapped.isEmpty());

  QSet<QString> exempt;
  for (const auto& name : patchy::ui::literal_color_icon_names()) {
    exempt.insert(name);
  }
  for (const auto& name : patchy::ui::stylesheet_referenced_icon_names()) {
    exempt.insert(name);
  }

  const QDir icons(QStringLiteral(":/patchy/icons"));
  const auto entries = icons.entryList({QStringLiteral("*.svg")}, QDir::Files, QDir::Name);
  CHECK(entries.size() > 50);

  static const QRegularExpression hex(QStringLiteral("#[0-9a-fA-F]{6}"));
  int checked = 0;
  for (const auto& entry : entries) {
    QFile file(icons.filePath(entry));
    CHECK(file.open(QIODevice::ReadOnly));
    const QString svg = QString::fromUtf8(file.readAll());
    const QString stem = QFileInfo(entry).completeBaseName();

    auto matches = hex.globalMatch(svg);
    while (matches.hasNext()) {
      const auto value = matches.next().captured(0);
      // The substitution is a plain byte replace, so authored hex must be lowercase.
      if (value != value.toLower()) {
        fprintf(stderr, "  %s: uppercase hex %s\n", qPrintable(entry), qPrintable(value));
      }
      CHECK(value == value.toLower());
      if (exempt.contains(stem)) {
        continue;
      }
      if (!mapped.contains(value)) {
        fprintf(stderr, "  %s: %s is not in the icon color map\n", qPrintable(entry), qPrintable(value));
      }
      CHECK(mapped.contains(value));
      ++checked;
    }
  }
  CHECK(checked > 100);

  // Every stylesheet-referenced icon must actually exist under the base path; the
  // light variants are checked separately once they exist.
  for (const auto& name : patchy::ui::stylesheet_referenced_icon_names()) {
    CHECK(QFile::exists(QStringLiteral(":/patchy/icons/%1.svg").arg(name)));
  }
}

// GitHub issue 99: the application style keeps the desktop's icon theme out of Patchy's
// chrome. Dialog buttons carry text only (Linux desktops otherwise add OK/Cancel glyphs
// drawn for their own scheme), and a toolbar's overflow button shows Patchy's chevron in
// the scheme's icon ink.
void ui_app_style_keeps_desktop_icons_off_dialog_buttons_and_toolbars() {
  patchy::ui::AppStyle style;
  CHECK(style.styleHint(QStyle::SH_DialogButtonBox_ButtonsHaveIcons, nullptr, nullptr, nullptr) == 0);
  QDialogButtonBox box;
  box.setStyle(&style);
  box.setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  CHECK(box.button(QDialogButtonBox::Ok) != nullptr && box.button(QDialogButtonBox::Ok)->icon().isNull());
  CHECK(box.button(QDialogButtonBox::Cancel) != nullptr && box.button(QDialogButtonBox::Cancel)->icon().isNull());

  const auto rendered = [](const QIcon& icon) { return icon.pixmap(QSize(16, 16)).toImage(); };
  const auto horizontal = style.standardIcon(QStyle::SP_ToolBarHorizontalExtensionButton, nullptr, nullptr);
  const auto vertical = style.standardIcon(QStyle::SP_ToolBarVerticalExtensionButton, nullptr, nullptr);
  CHECK(!horizontal.isNull() && !vertical.isNull());
  CHECK(rendered(horizontal) == rendered(patchy::ui::themed_svg_icon(QStringLiteral("chevron-right"))));
  // A vertical bar's overflow opens columns to the right (issue 122).
  CHECK(rendered(vertical) == rendered(patchy::ui::themed_svg_icon(QStringLiteral("chevron-right"))));
  // Other standard icons still come from the platform style.
  CHECK(!style.standardIcon(QStyle::SP_DirIcon, nullptr, nullptr).isNull());
}

// Handing token text to setStyleSheet is silently destructive: Qt drops the whole
// declaration, so a rule simply stops existing and only shows up as a wrongly
// drawn control in one state. ThemedQss makes that a compile error for the shared
// builders; this catches an inline blob that forgot to go through the helpers.
// GitHub issue 123: the platform style's tab-base line (Fusion: 1px of the
// palette's light color along the bar) showed as a stray white line beside
// the Layers / Channels / Paths tabs on Linux. The panel column's tab bar
// draws no base.
void ui_dock_tab_bars_draw_no_native_base() {
  patchy::ui::MainWindow window;
  show_window(window);
  QApplication::processEvents();

  auto* tab_bar = window.findChild<QTabBar*>(QStringLiteral("panelTabGroupTabBar"));
  CHECK(tab_bar != nullptr);
  CHECK(tab_bar != nullptr && !tab_bar->drawBase());
}

void ui_no_widget_ships_unresolved_theme_tokens() {
  patchy::ui::MainWindow window;
  show_window(window);
  QApplication::processEvents();

  int styled = 0;
  for (auto* widget : QApplication::allWidgets()) {
    if (widget == nullptr) {
      continue;
    }
    const auto sheet = widget->styleSheet();
    if (sheet.isEmpty()) {
      continue;
    }
    ++styled;
    if (sheet.contains(QLatin1Char('@'))) {
      fprintf(stderr, "  %s (%s) ships an unresolved theme token\n",
              qPrintable(widget->objectName()), widget->metaObject()->className());
    }
    CHECK(!sheet.contains(QLatin1Char('@')));
  }
  CHECK(styled > 0);
}

}  // namespace

std::vector<patchy::test::TestCase> app_shell_tests_part3() {
  return {
      {"ui_custom_theme_id_persists_and_reapplies_like_a_restart", ui_custom_theme_id_persists_and_reapplies_like_a_restart},
      {"ui_custom_theme_missing_file_falls_back_to_builtin_preference", ui_custom_theme_missing_file_falls_back_to_builtin_preference},
      {"ui_custom_theme_qss_resolves_every_token", ui_custom_theme_qss_resolves_every_token},
      {"ui_theme_file_newer_format_is_a_hard_error", ui_theme_file_newer_format_is_a_hard_error},
      {"ui_preferences_reload_reapplies_an_edited_theme_file", ui_preferences_reload_reapplies_an_edited_theme_file},
      {"ui_preferences_delete_removes_theme_file_and_entry", ui_preferences_delete_removes_theme_file_and_entry},
      {"ui_bundled_themes_load_and_list_in_preferences", ui_bundled_themes_load_and_list_in_preferences},
      {"ui_preferences_export_defaults_to_themes_folder_and_lists_the_copy", ui_preferences_export_defaults_to_themes_folder_and_lists_the_copy},
      {"ui_bundled_theme_id_persists_and_reapplies_like_a_restart", ui_bundled_theme_id_persists_and_reapplies_like_a_restart},
      {"ui_color_scheme_switch_updates_existing_window", ui_color_scheme_switch_updates_existing_window},
      {"ui_themed_icons_recolor_between_schemes", ui_themed_icons_recolor_between_schemes},
      {"ui_main_window_persists_window_geometry", ui_main_window_persists_window_geometry},
      {"ui_psd_import_warning_preference_defaults_to_hidden", ui_psd_import_warning_preference_defaults_to_hidden},
      {"ui_psd_import_warning_preference_persists_enabled_setting", ui_psd_import_warning_preference_persists_enabled_setting},
      {"ui_transform_shift_aspect_preference_defaults_to_off", ui_transform_shift_aspect_preference_defaults_to_off},
      {"ui_transform_shift_aspect_preference_persists_and_reaches_canvas", ui_transform_shift_aspect_preference_persists_and_reaches_canvas},
      {"ui_transform_snap_preference_persists_and_reaches_canvas", ui_transform_snap_preference_persists_and_reaches_canvas},
      {"ui_language_switch_updates_existing_window", ui_language_switch_updates_existing_window},
      {"ui_language_switch_survives_window_reactivation", ui_language_switch_survives_window_reactivation},
      {"ui_menubar_submenus_have_no_native_menu_role", ui_menubar_submenus_have_no_native_menu_role},
      {"ui_language_preference_applies_at_startup", ui_language_preference_applies_at_startup},
      {"ui_language_missing_preference_uses_system_language", ui_language_missing_preference_uses_system_language},
      {"ui_language_saved_preference_overrides_system_language", ui_language_saved_preference_overrides_system_language},
      {"ui_language_invalid_preference_falls_back_to_english", ui_language_invalid_preference_falls_back_to_english},
      {"ui_language_matching_maps_locales_to_shipped_codes", ui_language_matching_maps_locales_to_shipped_codes},
      {"ui_language_combo_lists_every_shipped_language", ui_language_combo_lists_every_shipped_language},
      {"ui_language_every_catalog_loads_and_translates", ui_language_every_catalog_loads_and_translates},
      {"ui_language_catalog_covers_dialog_status_and_properties", ui_language_catalog_covers_dialog_status_and_properties},
      {"ui_filter_gallery_action_retranslates", ui_filter_gallery_action_retranslates},
      {"ui_polygonal_lasso_tool_retranslates", ui_polygonal_lasso_tool_retranslates},
      {"ui_about_dialog_shows_labeled_external_links", ui_about_dialog_shows_labeled_external_links},
      {"ui_about_dialog_lists_shared_settings_and_data_folder_once",
       ui_about_dialog_lists_shared_settings_and_data_folder_once},
      {"ui_about_dialog_shows_memory_row", ui_about_dialog_shows_memory_row},
      {"ui_app_data_migration_merges_legacy_folder", ui_app_data_migration_merges_legacy_folder},
      {"ui_app_settings_migration_moves_legacy_settings_folder", ui_app_settings_migration_moves_legacy_settings_folder},
      {"ui_app_data_migration_moves_machine_data_to_local", ui_app_data_migration_moves_machine_data_to_local},
      {"ui_frameless_window_edges_resize", ui_frameless_window_edges_resize},
      {"ui_maximized_title_bar_click_keeps_window_maximized", ui_maximized_title_bar_click_keeps_window_maximized},
      {"ui_window_chrome_controls_drive_window_state", ui_window_chrome_controls_drive_window_state},
      {"ui_title_bar_drag_moves_window_only_past_threshold", ui_title_bar_drag_moves_window_only_past_threshold},
      {"ui_menu_bar_reserves_badge_space_only_with_custom_chrome",
       ui_menu_bar_reserves_badge_space_only_with_custom_chrome},
      {"ui_system_title_bar_preference_disables_custom_chrome", ui_system_title_bar_preference_disables_custom_chrome},
      {"ui_system_title_bar_preference_defaults_off", ui_system_title_bar_preference_defaults_off},
      {"ui_system_title_bar_follows_global_menu_bar_until_set", ui_system_title_bar_follows_global_menu_bar_until_set},
      {"ui_preferences_system_title_bar_checkbox_shows_desktop_default",
       ui_preferences_system_title_bar_checkbox_shows_desktop_default},
      {"ui_preferences_system_title_bar_checkbox_persists_key", ui_preferences_system_title_bar_checkbox_persists_key},
      {"ui_right_edge_scrollbars_remain_draggable", ui_right_edge_scrollbars_remain_draggable},
      {"ui_single_instance_socket_names_the_server_process", ui_single_instance_socket_names_the_server_process},
      {"ui_second_instance_brings_window_and_modal_dialog_forward", ui_second_instance_brings_window_and_modal_dialog_forward},
      {"ui_svg_icon_resources_are_registered", ui_svg_icon_resources_are_registered},
      {"ui_icon_color_map_covers_every_authored_color", ui_icon_color_map_covers_every_authored_color},
      {"ui_app_style_keeps_desktop_icons_off_dialog_buttons_and_toolbars", ui_app_style_keeps_desktop_icons_off_dialog_buttons_and_toolbars},
      {"ui_dock_tab_bars_draw_no_native_base", ui_dock_tab_bars_draw_no_native_base},
      {"ui_no_widget_ships_unresolved_theme_tokens", ui_no_widget_ships_unresolved_theme_tokens},
  };
}
