// Rename File on the document tab's context menu (docs/document-tabs.md): the file is
// renamed on disk in its folder, never over another file; the session takes the new
// path and title without being marked saved; recent files keep the entry's place; the
// recovery sidecar follows; a case-only change works; bad names are refused inline.
// Every test drives the real menu and dialog.
#include "core/document.hpp"
#include "core/document_recovery_store.hpp"
#include "psd/psd_document_io.hpp"
#include "support/path_utils.hpp"
#include "ui/app_settings.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui/theme_manager.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;

// A fresh, empty test-artifacts/rename-file/<leaf> folder (absolute, '/' separators).
QString fresh_folder(const QString& leaf) {
  const auto dir = std::filesystem::path("test-artifacts") / "rename-file" / leaf.toStdString();
  remove_test_scratch_dir(QString::fromStdString(dir.generic_string()));
  std::filesystem::create_directories(dir);
  return QFileInfo(QString::fromStdString(dir.generic_string())).absoluteFilePath();
}

// An 8x6 layer: `color` on the left half, white on the right, so a flip changes it.
void write_psd(const QString& path, QColor color) {
  patchy::Document document(8, 6, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(8, 6, patchy::PixelFormat::rgba8(), QColor(Qt::white));
  for (int y = 0; y < 6; ++y) {
    for (int x = 0; x < 4; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(color.red());
      px[1] = static_cast<std::uint8_t>(color.green());
      px[2] = static_cast<std::uint8_t>(color.blue());
    }
  }
  document.add_pixel_layer("Paint", std::move(pixels));
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, patchy::ui::to_filesystem_path(path));
}

QColor saved_corner(const QString& path) {
  const auto document = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(path));
  if (document.layers().empty()) return {};
  const auto* px = document.layers().front().pixels().pixel(0, 0);
  return QColor(px[0], px[1], px[2]);
}

QByteArray file_bytes(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QStringList folder_entries(const QString& folder) {
  return QDir(folder).entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
}

struct MenuProbe {
  bool seen{false};
  bool rename_present{false};
  bool rename_enabled{false};
};

// Opens the context menu of `tab_index` and records Rename File's state. With
// `trigger`, Rename File runs (its dialog is a nested modal loop, so `on_dialog` must
// drive it: it is called once, from a polling timer, with the open dialog). Nothing
// here throws inside Qt's event dispatch; the caller asserts on what was recorded.
MenuProbe open_tab_menu(patchy::ui::MainWindow& window, int tab_index, bool trigger,
                        const std::function<void(QDialog&)>& on_dialog = {}, const std::string& menu_capture = {}) {
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  auto* tab_bar = tabs != nullptr ? tabs->findChild<QTabBar*>() : nullptr;
  MenuProbe probe;
  if (tab_bar == nullptr) {
    return probe;
  }
  QTimer dialog_driver;
  dialog_driver.setInterval(10);
  bool dialog_driven = false;
  QObject::connect(&dialog_driver, &QTimer::timeout, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyRenameFileDialog"));
    if (dialog == nullptr || !dialog->isVisible() || dialog_driven) {
      return;
    }
    dialog_driven = true;
    dialog_driver.stop();
    if (on_dialog) {
      on_dialog(*dialog);
    }
    if (dialog->isVisible()) {
      dialog->reject();
    }
  });
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("documentTabContextMenu")) {
        continue;
      }
      probe.seen = true;
      QAction* rename = nullptr;
      for (auto* action : menu->actions()) {
        if (action->objectName() == QStringLiteral("documentTabRenameFileAction")) {
          rename = action;
        }
      }
      probe.rename_present = rename != nullptr && rename->isVisible();
      probe.rename_enabled = rename != nullptr && rename->isEnabled();
      if (!menu_capture.empty()) {
        if (rename != nullptr) {
          menu->setActiveAction(rename);
        }
        save_widget_artifact(menu_capture, *menu);
      }
      if (trigger && rename != nullptr && rename->isEnabled()) {
        dialog_driver.start();
        rename->trigger();
      }
      menu->close();
      return;
    }
  });
  const auto point = tab_bar->tabRect(tab_index).center();
  QContextMenuEvent event(QContextMenuEvent::Mouse, point, tab_bar->mapToGlobal(point));
  QApplication::sendEvent(tab_bar, &event);
  QApplication::processEvents();
  dialog_driver.stop();
  return probe;
}

QLineEdit* name_edit(QDialog& dialog) { return dialog.findChild<QLineEdit*>(QStringLiteral("renameFileNameEdit")); }

QLabel* problem_label(QDialog& dialog) { return dialog.findChild<QLabel*>(QStringLiteral("renameFileProblemLabel")); }

QPushButton* ok_button(QDialog& dialog) {
  auto* buttons = dialog.findChild<QDialogButtonBox*>();
  return buttons != nullptr ? buttons->button(QDialogButtonBox::Ok) : nullptr;
}

// Types `stem` and presses Rename. Returns the inline problem text left showing
// (empty when none showed).
QString type_and_rename(QDialog& dialog, const QString& stem) {
  auto* edit = name_edit(dialog);
  auto* ok = ok_button(dialog);
  if (edit == nullptr || ok == nullptr) {
    return QStringLiteral("<missing controls>");
  }
  edit->setText(stem);
  if (ok->isEnabled()) {
    ok->click();
  }
  auto* label = problem_label(dialog);
  return label != nullptr && !label->isHidden() ? label->text() : QString();
}

int open_and_find_tab(patchy::ui::MainWindow& window, const QString& path) {
  MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  return tabs != nullptr ? tabs->currentIndex() : -1;
}

QString tab_text(patchy::ui::MainWindow& window, int index) {
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  return tabs != nullptr ? tabs->tabText(index) : QString();
}

QStringList stored_recent_files() {
  auto settings = patchy::ui::app_settings();
  settings.sync();
  return settings.value(QStringLiteral("recentFiles")).toStringList();
}

}  // namespace

// The whole path: the file moves, the session follows (path, title, tab text), the
// document stays modified, recent files keep the entry where it was, the recovery
// sidecar names the new file, and the next Save writes to the new name.
void ui_document_tab_rename_file_updates_path_title_recent_and_recovery() {
  const auto folder = fresh_folder(QStringLiteral("basic"));
  const auto old_path = QDir(folder).filePath(QStringLiteral("holiday.psd"));
  const auto new_path = QDir(folder).filePath(QStringLiteral("holiday final.psd"));
  write_psd(old_path, QColor(200, 40, 40));

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto tab = open_and_find_tab(window, old_path);
  CHECK(tab >= 0);
  CHECK(MainWindowTestAccess::active_session_path(window) == old_path);

  // The entry sits between two others; a rename must not move it to the top.
  const auto before = QDir(folder).filePath(QStringLiteral("before.psd"));
  const auto after = QDir(folder).filePath(QStringLiteral("after.psd"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), QStringList{before, old_path, after});
    settings.sync();
  }

  // Unsaved edits, and a recovery copy that names the old file.
  require_action_by_text(window, QStringLiteral("Flip Layer Horizontal"))->trigger();
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::active_session_is_modified(window));
  const auto session_id = MainWindowTestAccess::session_id_for_canvas(window, require_canvas(window));
  CHECK(!window.write_recovery_now(true).isEmpty());
  const auto sidecar_for_session = [&window, session_id]() -> std::optional<patchy::recovery::RecoveryEntry> {
    for (const auto& entry : window.list_recovery_entries()) {
      if (entry.file_stem == std::to_string(session_id)) return entry;
    }
    return std::nullopt;
  };
  const auto old_sidecar = sidecar_for_session();
  CHECK(old_sidecar.has_value() &&
        old_sidecar->original_path == patchy::path_to_utf8(patchy::ui::to_filesystem_path(old_path)));

  QString initial_stem;
  QString initial_selection;
  QString extension_text;
  QString problem;
  const auto probe = open_tab_menu(window, tab, true, [&](QDialog& dialog) {
    if (auto* edit = name_edit(dialog); edit != nullptr) {
      initial_stem = edit->text();
      initial_selection = edit->selectedText();
    }
    if (auto* label = dialog.findChild<QLabel*>(QStringLiteral("renameFileExtensionLabel")); label != nullptr) {
      extension_text = label->text();
    }
    problem = type_and_rename(dialog, QStringLiteral("holiday final"));
  });
  CHECK(probe.seen && probe.rename_present && probe.rename_enabled);
  CHECK(initial_stem == QStringLiteral("holiday"));
  CHECK(initial_selection == QStringLiteral("holiday"));
  CHECK(extension_text == QStringLiteral(".psd"));
  CHECK(problem.isEmpty());

  CHECK(!QFileInfo::exists(old_path));
  CHECK(QFileInfo::exists(new_path));
  CHECK(MainWindowTestAccess::active_session_path(window) == new_path);
  CHECK(MainWindowTestAccess::active_session_title(window) == QStringLiteral("holiday final.psd"));
  CHECK(MainWindowTestAccess::active_session_is_modified(window));
  CHECK(tab_text(window, tab) == QStringLiteral("holiday final.psd*"));
  CHECK(window.windowTitle().startsWith(QStringLiteral("holiday final.psd")));
  CHECK(stored_recent_files() == QStringList({before, new_path, after}));

  const auto new_sidecar = sidecar_for_session();
  CHECK(new_sidecar.has_value());
  if (new_sidecar.has_value() && old_sidecar.has_value()) {
    CHECK(new_sidecar->original_path == patchy::path_to_utf8(patchy::ui::to_filesystem_path(new_path)));
    CHECK(new_sidecar->title == "holiday final.psd");
    CHECK(new_sidecar->saved_at_unix_ms == old_sidecar->saved_at_unix_ms);
  }

  // Save writes the kept edits (the flip) to the new name, and the old name stays gone.
  CHECK(saved_corner(new_path) == QColor(200, 40, 40));
  CHECK(MainWindowTestAccess::save_document(window));
  QApplication::processEvents();
  CHECK(!MainWindowTestAccess::active_session_is_modified(window));
  CHECK(!QFileInfo::exists(old_path));
  CHECK(saved_corner(new_path) == QColor(Qt::white));
  CHECK(folder_entries(folder) == QStringList({QStringLiteral("holiday final.psd")}));
}

// A name another file already has is refused inline: the dialog stays open, both
// files keep their bytes, and the document keeps its path.
void ui_document_tab_rename_file_refuses_existing_target() {
  const auto folder = fresh_folder(QStringLiteral("existing"));
  const auto source = QDir(folder).filePath(QStringLiteral("first.psd"));
  const auto other = QDir(folder).filePath(QStringLiteral("second.psd"));
  write_psd(source, QColor(200, 40, 40));
  write_psd(other, QColor(40, 200, 90));
  const auto source_bytes = file_bytes(source);
  const auto other_bytes = file_bytes(other);

  patchy::ui::MainWindow window;
  show_window(window);
  const auto tab = open_and_find_tab(window, source);
  QString problem;
  bool still_open = false;
  open_tab_menu(window, tab, true, [&](QDialog& dialog) {
    problem = type_and_rename(dialog, QStringLiteral("second"));
    still_open = dialog.isVisible();
    capture_widget_artifact("rename_file_dialog_target_exists", dialog);
  });
  CHECK(still_open);
  CHECK(problem.contains(QStringLiteral("already exists")));
  CHECK(problem.contains(QStringLiteral("second.psd")));
  CHECK(file_bytes(source) == source_bytes);
  CHECK(file_bytes(other) == other_bytes);
  CHECK(MainWindowTestAccess::active_session_path(window) == source);
  CHECK(folder_entries(folder) == QStringList({QStringLiteral("first.psd"), QStringLiteral("second.psd")}));
}

// photo.psd to Photo.psd: on a case-insensitive file system (Windows, default macOS)
// the target "exists" because it is the same file; the rename must still happen.
void ui_document_tab_rename_file_changes_case_only() {
  const auto folder = fresh_folder(QStringLiteral("case"));
  const auto old_path = QDir(folder).filePath(QStringLiteral("photo.psd"));
  write_psd(old_path, QColor(10, 120, 220));
  const auto bytes = file_bytes(old_path);

  patchy::ui::MainWindow window;
  show_window(window);
  const auto tab = open_and_find_tab(window, old_path);
  QString problem;
  open_tab_menu(window, tab, true, [&](QDialog& dialog) { problem = type_and_rename(dialog, QStringLiteral("Photo")); });
  CHECK(problem.isEmpty());
  CHECK(folder_entries(folder) == QStringList({QStringLiteral("Photo.psd")}));
  const auto new_path = QDir(folder).filePath(QStringLiteral("Photo.psd"));
  CHECK(file_bytes(new_path) == bytes);
  CHECK(MainWindowTestAccess::active_session_path(window) == new_path);
  CHECK(tab_text(window, tab) == QStringLiteral("Photo.psd"));
}

// Names that cannot be files are refused while typing (OK disabled, the reason shown),
// an unchanged name closes with nothing done, and nothing touches the disk.
void ui_document_tab_rename_file_rejects_invalid_names() {
  const auto folder = fresh_folder(QStringLiteral("invalid"));
  const auto path = QDir(folder).filePath(QStringLiteral("keep.psd"));
  write_psd(path, QColor(90, 90, 90));
  const auto bytes = file_bytes(path);

  patchy::ui::MainWindow window;
  show_window(window);
  const auto tab = open_and_find_tab(window, path);
  QStringList invalid{QString(), QStringLiteral("   "), QStringLiteral("a/b"), QStringLiteral("a\\b"),
                      QStringLiteral(".."),  QStringLiteral(". ."), QStringLiteral("CON"), QStringLiteral("nul"),
                      QStringLiteral("Com1"), QStringLiteral("lpt9"), QStringLiteral("tab\there"),
                      QString(260, QLatin1Char('x'))};
#ifdef Q_OS_WIN
  invalid << QStringLiteral("what?") << QStringLiteral("a:b") << QStringLiteral("a*b") << QStringLiteral("q\"t")
          << QStringLiteral("pipe|") << QStringLiteral("<x>");
#endif
  QStringList accepted_invalid;
  bool valid_name_enabled = false;
  bool valid_name_clears_problem = false;
  bool same_name_closed = false;
  open_tab_menu(window, tab, true, [&](QDialog& dialog) {
    auto* edit = name_edit(dialog);
    auto* ok = ok_button(dialog);
    auto* label = problem_label(dialog);
    if (edit == nullptr || ok == nullptr || label == nullptr) {
      accepted_invalid << QStringLiteral("<missing controls>");
      return;
    }
    for (const auto& name : invalid) {
      edit->setText(name);
      if (ok->isEnabled() || label->isHidden() || label->text().isEmpty()) {
        accepted_invalid << name;
      }
    }
    edit->setText(QStringLiteral("fine name (2)"));
    valid_name_enabled = ok->isEnabled();
    valid_name_clears_problem = label->isHidden();
    // Back to the current name: Rename closes the dialog and changes nothing.
    edit->setText(QStringLiteral("keep"));
    ok->click();
    same_name_closed = !dialog.isVisible();
  });
  CHECK(accepted_invalid.isEmpty());
  if (!accepted_invalid.isEmpty()) {
    std::cerr << "accepted: " << accepted_invalid.join(QStringLiteral(" | ")).toStdString() << '\n';
  }
  CHECK(valid_name_enabled);
  CHECK(valid_name_clears_problem);
  CHECK(same_name_closed);
  CHECK(folder_entries(folder) == QStringList({QStringLiteral("keep.psd")}));
  CHECK(file_bytes(path) == bytes);
  CHECK(MainWindowTestAccess::active_session_path(window) == path);
  CHECK(!MainWindowTestAccess::active_session_is_modified(window));
}

// A never-saved document has no file to rename: the item shows, disabled.
void ui_document_tab_rename_file_disabled_without_a_file() {
  const auto folder = fresh_folder(QStringLiteral("disabled"));
  const auto path = QDir(folder).filePath(QStringLiteral("saved.psd"));
  write_psd(path, QColor(30, 30, 160));

  patchy::ui::MainWindow window;
  show_window(window);  // Tab 0: the untitled startup document.
  const auto untitled = open_tab_menu(window, 0, false);
  CHECK(untitled.seen && untitled.rename_present);
  CHECK(!untitled.rename_enabled);
  const auto tab = open_and_find_tab(window, path);
  const auto saved = open_tab_menu(window, tab, false);
  CHECK(saved.rename_present && saved.rename_enabled);
}

// Non-ASCII folder and names on both sides of the rename (tests/unicode_path_names.hpp).
void ui_document_tab_rename_file_handles_unicode_paths() {
  const auto dir = patchy::test::unicode_artifact_dir(u8"rename-file");
  const auto old_fs = dir / patchy::test::unicode_path_piece(patchy::test::kUnicodePathStems[0]);
  const auto old_path = QFileInfo(patchy::ui::to_qstring(old_fs) + QStringLiteral(".psd")).absoluteFilePath();
  const auto new_stem = patchy::ui::to_qstring(patchy::test::unicode_path_piece(patchy::test::kUnicodeCombinedStem));
  write_psd(old_path, QColor(250, 160, 20));
  const auto bytes = file_bytes(old_path);

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto tab = open_and_find_tab(window, old_path);
  CHECK(MainWindowTestAccess::active_session_path(window) == old_path);
  QString problem;
  open_tab_menu(window, tab, true, [&](QDialog& dialog) { problem = type_and_rename(dialog, new_stem); });
  CHECK(problem.isEmpty());
  const auto new_path = QFileInfo(old_path).dir().filePath(new_stem + QStringLiteral(".psd"));
  std::error_code error;
  CHECK(std::filesystem::is_regular_file(patchy::ui::to_filesystem_path(new_path), error));
  CHECK(!std::filesystem::exists(patchy::ui::to_filesystem_path(old_path), error));
  CHECK(file_bytes(new_path) == bytes);
  CHECK(MainWindowTestAccess::active_session_path(window) == new_path);
  CHECK(MainWindowTestAccess::active_session_title(window) == new_stem + QStringLiteral(".psd"));
  CHECK(stored_recent_files().value(0) == new_path);
}

// Review captures for the issue desk: the tab menu with the item and the dialog, in
// Dark and Light (build\issue-shots\rename-file).
void ui_document_tab_rename_file_review_captures() {
  const auto folder = fresh_folder(QStringLiteral("captures"));
  const auto path = QDir(folder).filePath(QStringLiteral("beach sunset.psd"));
  write_psd(path, QColor(240, 140, 60));
  ColorSchemeRestorer restore_scheme;
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer::apply(scheme);
    const std::string suffix = scheme == patchy::ui::ColorSchemePreference::Dark ? "dark" : "light";
    patchy::ui::MainWindow window;
    show_window(window);
    const auto tab = open_and_find_tab(window, path);
    const auto probe = open_tab_menu(window, tab, false, {}, "rename_file_tab_menu_" + suffix);
    CHECK(probe.rename_enabled);
    bool captured = false;
    open_tab_menu(window, tab, true, [&](QDialog& dialog) {
      if (auto* edit = name_edit(dialog); edit != nullptr) {
        edit->selectAll();
      }
      capture_widget_artifact("rename_file_dialog_" + suffix, dialog);
      if (auto* edit = name_edit(dialog); edit != nullptr) {
        edit->setText(QStringLiteral("beach?"));
#ifndef Q_OS_WIN
        edit->setText(QStringLiteral("CON"));
#endif
      }
      capture_widget_artifact("rename_file_dialog_invalid_" + suffix, dialog);
      captured = true;
    });
    CHECK(captured);
  }
}

std::vector<patchy::test::TestCase> document_rename_tests() {
  return {
      {"ui_document_tab_rename_file_updates_path_title_recent_and_recovery",
       ui_document_tab_rename_file_updates_path_title_recent_and_recovery},
      {"ui_document_tab_rename_file_refuses_existing_target", ui_document_tab_rename_file_refuses_existing_target},
      {"ui_document_tab_rename_file_changes_case_only", ui_document_tab_rename_file_changes_case_only},
      {"ui_document_tab_rename_file_rejects_invalid_names", ui_document_tab_rename_file_rejects_invalid_names},
      {"ui_document_tab_rename_file_disabled_without_a_file", ui_document_tab_rename_file_disabled_without_a_file},
      {"ui_document_tab_rename_file_handles_unicode_paths", ui_document_tab_rename_file_handles_unicode_paths},
      {"ui_document_tab_rename_file_review_captures", ui_document_tab_rename_file_review_captures},
  };
}
