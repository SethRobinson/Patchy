// Unicode and special-character file paths through the Qt-facing entry points:
// write_flat_image_file, MainWindow save/open, the Save As / Open / Export Flat
// dialogs (the offscreen QFileDialog, which is the same post-processing the native
// dialog feeds), recent-files persistence, and legacy plug-in registration. These pin
// the August 2026 Save As bug (QString::toStdString() handed to std::filesystem::path
// decoded with the ANSI code page, so a Japanese file name landed on disk as mojibake
// and could not be reopened). Names come from tests/unicode_path_names.hpp; every
// test lists the directory so a mojibake sibling fails even when the reader happens
// to find its own mangled name again.

#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"

#include "core/document.hpp"
#include "formats/bmp_document_io.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/app_settings.hpp"

#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLineEdit>
#include <QMenu>
#include <QStringList>
#include <QTimer>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using patchy::test::kUnicodeCombinedStem;
using patchy::test::kUnicodeDirName;
using patchy::test::kUnicodePathStems;
using patchy::test::ui::ensure_artifact_dir;
using patchy::test::ui::find_top_level_dialog;
using patchy::test::ui::require_action;
using patchy::test::ui::SettingsValueRestorer;
using patchy::test::ui::show_window;

namespace {

QString q(std::u8string_view text) {
  return QString::fromUtf8(reinterpret_cast<const char*>(text.data()), static_cast<qsizetype>(text.size()));
}

QString nfc(const QString& text) { return text.normalized(QString::NormalizationForm_C); }

// A fresh test-artifacts/<kUnicodeDirName>/<leaf> folder as an absolute QString path.
QString unicode_dir(const QString& leaf) {
  ensure_artifact_dir();
  const auto dir = QFileInfo(QStringLiteral("test-artifacts")).absoluteFilePath() + QLatin1Char('/') +
                   q(kUnicodeDirName) + QLatin1Char('/') + leaf;
  QDir(dir).removeRecursively();
  CHECK(QDir().mkpath(dir));
  return dir;
}

// Exactly `names` and nothing else (NFC on both sides: macOS stores NFD).
void check_dir_holds_only(const QString& dir, QStringList names) {
  auto listed = QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
  for (auto& entry : listed) {
    entry = nfc(entry);
  }
  for (auto& name : names) {
    name = nfc(name);
  }
  listed.sort();
  names.sort();
  CHECK(listed == names);
}

QString combined_name(const char* extension) { return q(kUnicodeCombinedStem) + QLatin1Char('.') + QLatin1String(extension); }

patchy::Document small_document() {
  QImage source(8, 6, QImage::Format_RGBA8888);
  source.fill(QColor(20, 120, 220, 255));
  source.setPixelColor(1, 0, QColor(90, 30, 180, 255));
  return patchy::ui::document_from_qimage(source, "Unicode");
}

void write_psd(const QString& path, const patchy::Document& document = small_document()) {
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, patchy::ui::to_filesystem_path(path));
}

}  // namespace

void ui_unicode_write_flat_image_file_every_extension() {
  const auto dir = unicode_dir(QStringLiteral("write-flat"));
  const auto document = small_document();

  // BMP reads its palette from a second path, also Unicode.
  const auto palette_name = q(u8"\u30D1\u30EC\u30C3\u30C8 caf\u00E9.pal");
  const auto palette_path = dir + QLatin1Char('/') + palette_name;
  {
    QFile file(palette_path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("JASC-PAL\r\n0100\r\n4\r\n0 0 0\r\n255 0 0\r\n0 255 0\r\n0 0 255\r\n");
  }
  QStringList expected{palette_name};

  const std::vector<const char*> extensions = {"png", "jpg", "bmp", "gif", "ico", "cur", "tga",
                                               "pcx", "lbm", "tif", "webp", "pdf"};
  for (const auto* extension : extensions) {
    const auto name = combined_name(extension);
    const auto path = dir + QLatin1Char('/') + name;
    patchy::ui::ImageSaveOptions options;
    options.ico_sizes = {16};
    if (QLatin1String(extension) == QLatin1String("bmp")) {
      options.bmp_encoding = patchy::bmp::BmpEncoding::Indexed4;
      options.bmp_palette_mode = patchy::bmp::BmpPaletteMode::PaletteFile;
      options.bmp_palette_path = palette_path;
    }
    patchy::ui::write_flat_image_file(document, path, QLatin1String(extension), options);
    expected << name;
    CHECK(QFileInfo(path).isFile());
    CHECK(QFileInfo(path).size() > 0);
  }
  check_dir_holds_only(dir, expected);

  {
    const auto bmp = patchy::bmp::DocumentIo::read_file(patchy::ui::to_filesystem_path(dir + QLatin1Char('/') + combined_name("bmp")));
    CHECK(bmp.indexed_palette().has_value());
    CHECK(bmp.indexed_palette()->colors.size() == 4);
  }

  // Every raster file opens again through the normal Open path. CLI automation mode
  // keeps a regression from blocking on the open-failed or palette-adoption boxes.
  patchy::ui::MainWindow window;
  show_window(window);
  window.set_cli_automation_mode(true);
  for (const auto* extension : extensions) {
    if (QLatin1String(extension) == QLatin1String("pdf")) {
      continue;
    }
    const auto path = dir + QLatin1Char('/') + combined_name(extension);
    const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);
    patchy::ui::MainWindowTestAccess::open_document_path(window, path);
    QApplication::processEvents();
    CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 1);
    const auto& opened = patchy::ui::MainWindowTestAccess::document(window);
    const bool icon = QLatin1String(extension) == QLatin1String("ico") || QLatin1String(extension) == QLatin1String("cur");
    CHECK(opened.width() == (icon ? 16 : 8));
    CHECK(opened.height() == (icon ? 16 : 6));
    CHECK(nfc(patchy::ui::MainWindowTestAccess::active_session_path(window)) == nfc(path));
  }
}

void ui_unicode_save_and_open_layered_formats() {
  const auto dir = unicode_dir(QStringLiteral("save-open"));
  QStringList expected;
  {
    // CLI automation mode: a regression fails the save instead of blocking on the
    // save-failed box, and the SVG save-a-copy question (masks, styles, and text bake
    // on save) is skipped the way --export skips it.
    patchy::ui::MainWindow window;
    show_window(window);
    window.set_cli_automation_mode(true);
    for (const auto* extension : {"psd", "psb", "ase", "svg"}) {
      const auto name = combined_name(extension);
      const auto path = dir + QLatin1Char('/') + name;
      CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, path));
      expected << name;
      CHECK(QFileInfo(path).isFile());
      if (QLatin1String(extension) != QLatin1String("svg")) {
        // SVG keeps save-a-copy semantics: the session stays on the previous path.
        CHECK(nfc(patchy::ui::MainWindowTestAccess::active_session_path(window)) == nfc(path));
      }
    }
  }
  // The literal bug case: a file whose name IS the mojibake (legal on NTFS) must open.
  const auto mojibake_name = q(kUnicodePathStems[3]) + QStringLiteral(".psd");
  write_psd(dir + QLatin1Char('/') + mojibake_name);
  expected << mojibake_name;
  check_dir_holds_only(dir, expected);

  patchy::ui::MainWindow window;
  show_window(window);
  window.set_cli_automation_mode(true);
  for (const auto& name : expected) {
    const auto path = dir + QLatin1Char('/') + name;
    const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);
    patchy::ui::MainWindowTestAccess::open_document_path(window, path);
    QApplication::processEvents();
    CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 1);
    CHECK(nfc(patchy::ui::MainWindowTestAccess::active_session_path(window)) == nfc(path));
    const auto& opened = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(opened.width() == (name == mojibake_name ? 8 : 1024));
  }
}

void ui_unicode_save_as_dialog_round_trip() {
  const auto dir = unicode_dir(QStringLiteral("save-as-dialog"));
  SettingsValueRestorer last_save_directory_restorer(QStringLiteral("lastSaveDirectory"));
  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));

  // A seeded recent file gives the offscreen dialog its editable recent-name combo.
  const auto seed_name = q(u8"\u65E2\u5B58 seed.psd");
  const auto seed_path = dir + QLatin1Char('/') + seed_name;
  write_psd(seed_path);
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), QStringList{seed_path});
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  // Pass 1: the dialog's own selection API (what a picked or pre-filled name yields).
  const auto selected_name = combined_name("psd");
  const auto selected_path = dir + QLatin1Char('/') + selected_name;
  bool accepted_selected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    dialog->setDirectory(dir);
    dialog->selectFile(selected_path);
    accepted_selected = true;
    static_cast<QDialog*>(dialog)->accept();
  });
  require_action(window, "fileSaveAsAction")->trigger();
  CHECK(accepted_selected);
  CHECK(QFileInfo(selected_path).isFile());
  CHECK(nfc(patchy::ui::MainWindowTestAccess::active_session_path(window)) == nfc(selected_path));

  // Pass 2: a name typed into the recent-name combo, which mirrors into Qt's
  // fileNameEdit and goes through the dialog's typed-name parsing.
  const auto typed_name = q(kUnicodePathStems[0]) + QStringLiteral(" typed ") + q(kUnicodePathStems[4]) +
                          QStringLiteral(".psd");
  const auto typed_path = dir + QLatin1Char('/') + typed_name;
  bool accepted_typed = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    dialog->setDirectory(dir);
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("saveAsRecentFileNameCombo"));
    CHECK(combo != nullptr);
    combo->lineEdit()->setText(typed_name);
    QApplication::processEvents();
    const auto selected_files = dialog->selectedFiles();
    CHECK(!selected_files.isEmpty());
    CHECK(nfc(QFileInfo(selected_files.first()).fileName()) == nfc(typed_name));
    accepted_typed = true;
    static_cast<QDialog*>(dialog)->accept();
  });
  require_action(window, "fileSaveAsAction")->trigger();
  CHECK(accepted_typed);
  CHECK(QFileInfo(typed_path).isFile());
  CHECK(nfc(patchy::ui::MainWindowTestAccess::active_session_path(window)) == nfc(typed_path));

  check_dir_holds_only(dir, {seed_name, selected_name, typed_name});
  {
    auto settings = patchy::ui::app_settings();
    settings.sync();
    CHECK(nfc(QFileInfo(settings.value(QStringLiteral("lastSaveDirectory")).toString()).absoluteFilePath()) ==
          nfc(QFileInfo(dir).absoluteFilePath()));
    const auto recent = settings.value(QStringLiteral("recentFiles")).toStringList();
    CHECK(!recent.isEmpty());
    CHECK(nfc(recent.front()) == nfc(QFileInfo(typed_path).absoluteFilePath()));
  }
}

void ui_unicode_open_dialog_round_trip() {
  const auto dir = unicode_dir(QStringLiteral("open-dialog"));
  SettingsValueRestorer last_open_directory_restorer(QStringLiteral("lastOpenDirectory"));
  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  const auto name = combined_name("psd");
  const auto path = dir + QLatin1Char('/') + name;
  write_psd(path);

  patchy::ui::MainWindow window;
  show_window(window);
  const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);
  bool accepted = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("openFileDialog")));
    CHECK(dialog != nullptr);
    dialog->setDirectory(dir);
    dialog->selectFile(path);
    accepted = true;
    static_cast<QDialog*>(dialog)->accept();
  });
  require_action(window, "fileOpenAction")->trigger();
  QApplication::processEvents();
  CHECK(accepted);
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 1);
  CHECK(nfc(patchy::ui::MainWindowTestAccess::active_session_path(window)) == nfc(path));
  CHECK(patchy::ui::MainWindowTestAccess::document(window).width() == 8);
  check_dir_holds_only(dir, {name});
  {
    auto settings = patchy::ui::app_settings();
    settings.sync();
    const auto recent = settings.value(QStringLiteral("recentFiles")).toStringList();
    CHECK(!recent.isEmpty());
    CHECK(nfc(recent.front()) == nfc(QFileInfo(path).absoluteFilePath()));
    CHECK(nfc(QFileInfo(settings.value(QStringLiteral("lastOpenDirectory")).toString()).absoluteFilePath()) ==
          nfc(QFileInfo(dir).absoluteFilePath()));
  }
}

void ui_unicode_export_flat_image_dialog() {
  const auto dir = unicode_dir(QStringLiteral("export-flat"));
  SettingsValueRestorer last_save_directory_restorer(QStringLiteral("lastSaveDirectory"));
  const auto name = combined_name("psd");
  const auto path = dir + QLatin1Char('/') + name;

  patchy::ui::MainWindow window;
  show_window(window);
  bool accepted = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("exportFlatImageFileDialog")));
    CHECK(dialog != nullptr);
    dialog->setDirectory(dir);
    dialog->selectFile(path);
    accepted = true;
    static_cast<QDialog*>(dialog)->accept();
  });
  require_action(window, "fileExportFlatAction")->trigger();
  CHECK(accepted);
  CHECK(QFileInfo(path).isFile());
  check_dir_holds_only(dir, {name});
  const auto exported = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(path));
  CHECK(exported.width() == 1024);
  CHECK(exported.height() == 768);
}

void ui_unicode_recent_files_persist_through_settings() {
  const auto dir = unicode_dir(QStringLiteral("recent-files"));
  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  const auto name = combined_name("psd");
  const auto path = QFileInfo(dir + QLatin1Char('/') + name).absoluteFilePath();
  write_psd(path);
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), QStringList{path});
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  QAction* entry = nullptr;
  for (auto* action : recent_menu->actions()) {
    if (action != nullptr && !action->isSeparator() && !action->data().toString().isEmpty()) {
      CHECK(entry == nullptr);
      entry = action;
    }
  }
  CHECK(entry != nullptr);
  CHECK(nfc(entry->data().toString()) == nfc(path));
  CHECK(entry->text().contains(q(kUnicodePathStems[0])));

  const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);
  entry->trigger();
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 1);
  CHECK(nfc(patchy::ui::MainWindowTestAccess::active_session_path(window)) == nfc(path));

  // The ini store round-trips the path unchanged.
  auto settings = patchy::ui::app_settings();
  settings.sync();
  const auto stored = settings.value(QStringLiteral("recentFiles")).toStringList();
  CHECK(stored.size() == 1);
  CHECK(nfc(stored.front()) == nfc(path));
}

void ui_unicode_legacy_plugin_probe_from_unicode_dir() {
  const auto dir = unicode_dir(QStringLiteral("plugins"));
  const auto source = patchy::test::source_root_path() / "test-fixtures" / "photoshop-plugins" / "Greyscale64.8bf";
  CHECK(std::filesystem::exists(source));
  const auto name = combined_name("8bf");
  const auto path = dir + QLatin1Char('/') + name;
  CHECK(QFile::copy(patchy::ui::to_qstring(source), path));
  check_dir_holds_only(dir, {name});

  patchy::ui::MainWindow window;
  show_window(window);
  QStringList report;
  const bool registered = patchy::ui::MainWindowTestAccess::register_legacy_plugin_path(window, path, &report);
  CHECK(report.size() == 1);
  CHECK(nfc(report.front()).startsWith(nfc(name)));
  // The probe must have read the PE header through the Unicode path: the report
  // names the plug-in kind and architecture, not a "could not open" reason.
  CHECK(report.front().contains(QStringLiteral("x64")));
#if defined(_WIN32) && defined(_M_X64)
  CHECK(registered);
  bool found_action = false;
  for (auto* action : window.findChildren<QAction*>(QStringLiteral("legacyPluginAction"))) {
    if (action->text().contains(q(kUnicodePathStems[0]))) {
      found_action = true;
    }
  }
  CHECK(found_action);
#else
  CHECK(!registered);
#endif
}

// The Divide Scanned Photos folder save is a file-writing entry point, so it
// gets the standard Unicode-path coverage: numbered names derived from a
// Unicode base name, then one output reopened through the session loader.
void ui_unicode_divide_photos_folder_save() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto dir = unicode_dir(QStringLiteral("divide-photos"));
  std::vector<patchy::PixelBuffer> photos;
  for (int i = 0; i < 2; ++i) {
    patchy::PixelBuffer photo(12 + i * 4, 8, patchy::PixelFormat::rgba8());
    photo.clear(200);
    photos.push_back(std::move(photo));
  }
  patchy::DocumentPrintSettings print_settings;
  const auto base = q(kUnicodePathStems[0]);
  const auto chosen = dir + QLatin1Char('/') + base + QStringLiteral("_001.png");
  CHECK(patchy::ui::MainWindowTestAccess::save_divided_photos_to_folder(window, photos, print_settings,
                                                                        chosen, QStringLiteral("png")));
  check_dir_holds_only(dir, {base + QStringLiteral("_001.png"), base + QStringLiteral("_002.png")});
  patchy::ui::MainWindowTestAccess::open_document_path(window,
                                                       dir + QLatin1Char('/') + base + QStringLiteral("_002.png"));
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::document(window).width() == 16);
  CHECK(patchy::ui::MainWindowTestAccess::document(window).height() == 8);
}

std::vector<patchy::test::TestCase> unicode_path_tests() {
  return {
      {"ui_unicode_write_flat_image_file_every_extension", ui_unicode_write_flat_image_file_every_extension},
      {"ui_unicode_save_and_open_layered_formats", ui_unicode_save_and_open_layered_formats},
      {"ui_unicode_save_as_dialog_round_trip", ui_unicode_save_as_dialog_round_trip},
      {"ui_unicode_open_dialog_round_trip", ui_unicode_open_dialog_round_trip},
      {"ui_unicode_export_flat_image_dialog", ui_unicode_export_flat_image_dialog},
      {"ui_unicode_recent_files_persist_through_settings", ui_unicode_recent_files_persist_through_settings},
      {"ui_unicode_legacy_plugin_probe_from_unicode_dir", ui_unicode_legacy_plugin_probe_from_unicode_dir},
      {"ui_unicode_divide_photos_folder_save", ui_unicode_divide_photos_folder_save},
  };
}
