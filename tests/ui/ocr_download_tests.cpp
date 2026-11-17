// The text recognition models as a download (docs/ocr-engines.md, "PP-OCR"): the pinned
// file list, what Delete removes, the Download offer in the engine picker and the rows in
// Preferences. Nothing here reaches the network unless PATCHY_TEST_OCR_DOWNLOAD=1 asks for
// it: otherwise the one button that would is never pressed without a stand-in. The two
// "_if_enabled" tests are that request (docs/ocr-engines.md, "PP-OCR"): the real download
// through the real button, and the fallback links.

#include "ai/ocr_model_engine.hpp"
#include "ai/ocr_models.hpp"
#include "ai/segmentation_engine.hpp"
#include "ocr/ocr_fake_engine.hpp"
#include "ocr_page_fixture.hpp"
#include "ocr_test_support.hpp"
#include "test_harness.hpp"
#include "ui/main_window.hpp"
#include "ui/object_selection_model_download.hpp"
#include "ui/ocr_controls.hpp"
#include "ui/ocr_model_download.hpp"
#include "ui/qt_paths.hpp"
#include "ui/recognize_text_dialog.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

using patchy::ocr::FakeEngineConfig;
using patchy::test::ui::find_top_level_dialog;
using patchy::test::ui::guarded;
using patchy::test::ui::ScopedOcrEngines;
using patchy::test::ui::SettingsGroupRestorer;
using patchy::test::ui::show_window;
using patchy::test::ui::when_dialog_visible;

// Points the engine's downloaded models folder at `folder` for a test, then back at nothing.
struct DownloadedModelsFolder {
  explicit DownloadedModelsFolder(const QString& folder) {
    patchy::ai::set_downloaded_model_directory(patchy::ui::to_filesystem_path(folder));
  }
  ~DownloadedModelsFolder() {
    patchy::ai::release_ocr_models();
    patchy::ai::set_downloaded_model_directory({});
  }
  DownloadedModelsFolder(const DownloadedModelsFolder&) = delete;
  DownloadedModelsFolder& operator=(const DownloadedModelsFolder&) = delete;
};

// A stand-in for every file of the download, one byte each.
void write_stand_ins(const QString& folder) {
  CHECK(QDir().mkpath(folder));
  for (const auto& file : patchy::ui::ocr_model_download_files()) {
    QFile out(QDir(folder).filePath(file.name));
    CHECK(out.open(QIODevice::WriteOnly));
    out.write("x");
  }
}

std::shared_ptr<patchy::ocr::FakeEngine> model_engine_stand_in(bool downloaded) {
  FakeEngineConfig config;
  config.id = "ppocr";
  config.display_name = "PP-OCR";
  config.languages = {{"en", "English"}, {"ja", "Japanese"}};
  if (!downloaded) {
    config.unavailable = patchy::ocr::Unavailable::NotDownloaded;
    config.unavailable_reason = "the text recognition models are not downloaded";
  }
  return patchy::ocr::make_fake_engine(std::move(config));
}

void ui_ocr_model_download_list_is_pinned() {
  const auto& files = patchy::ui::ocr_model_download_files();
  // Every file the engine can use, then the license text: nothing more, nothing missing.
  const auto engine_files = patchy::ai::ocr_model_files();
  CHECK(files.size() == engine_files.size() + 1U);
  CHECK(files.size() == 20);
  for (std::size_t index = 0; index < engine_files.size() && index < files.size(); ++index) {
    CHECK(files[index].name.toStdString() == std::string(engine_files[index]));
  }
  CHECK(files.back().name == QStringLiteral("ppocr_LICENSE.txt"));
  for (const auto& file : files) {
    CHECK(file.size > 0);
    CHECK(file.sha256_hex.size() == 64);
    CHECK(file.sha256_hex == file.sha256_hex.toLower());
    // Patchy's own release first, the publisher's own pinned copy as the fallback.
    CHECK(file.url.toString() ==
          QStringLiteral("https://github.com/SethRobinson/Patchy/releases/download/ai-models-4/") + file.name);
    CHECK(file.fallback_urls.size() == 1);
    if (file.fallback_urls.size() == 1) {
      const auto fallback = file.fallback_urls.front().toString();
      CHECK(fallback.startsWith(QStringLiteral("https://huggingface.co/PaddlePaddle/")) ||
            fallback.startsWith(QStringLiteral("https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/")));
      // Pinned to a commit, never a moving branch.
      CHECK(!fallback.contains(QStringLiteral("/main/")) && !fallback.contains(QStringLiteral("/master/")));
    }
  }
  CHECK(patchy::ui::ocr_model_download_megabytes() == 100);
  const auto summary = patchy::ui::ocr_model_download_summary();
  CHECK(summary.contains(QStringLiteral("100 MB")));
  CHECK(summary.contains(QStringLiteral("github.com/SethRobinson/Patchy")));
  CHECK(summary.contains(QStringLiteral("ai-models-4")));
}

void ui_ocr_models_delete_removes_only_their_files() {
  QTemporaryDir scratch;
  CHECK(scratch.isValid());
  const auto folder = QDir(scratch.path()).filePath(QStringLiteral("ai-models"));
  CHECK(!patchy::ui::downloaded_ocr_models_present(folder));
  write_stand_ins(folder);
  CHECK(patchy::ui::downloaded_ocr_models_present(folder));
  // The license text is not what makes the models present.
  CHECK(QFile::remove(QDir(folder).filePath(QStringLiteral("ppocr_LICENSE.txt"))));
  CHECK(patchy::ui::downloaded_ocr_models_present(folder));
  CHECK(QFile::remove(QDir(folder).filePath(QStringLiteral("ppocr_korean_rec.yml"))));
  CHECK(!patchy::ui::downloaded_ocr_models_present(folder));

  // Delete takes exactly the listed names; anything else in the folder stays, and so does
  // the folder.
  write_stand_ins(folder);
  const auto other = QDir(folder).filePath(QStringLiteral("someone_elses_model.onnx"));
  {
    QFile keep(other);
    CHECK(keep.open(QIODevice::WriteOnly));
    keep.write("keep");
  }
  QString error;
  CHECK(patchy::ui::delete_downloaded_ocr_models(folder, &error));
  CHECK(error.isEmpty());
  CHECK(QFileInfo(other).isFile());
  for (const auto& file : patchy::ui::ocr_model_download_files()) {
    CHECK(!QFileInfo::exists(QDir(folder).filePath(file.name)));
  }
  // Nothing else left: the folder goes too (a plain rmdir).
  CHECK(QFile::remove(other));
  write_stand_ins(folder);
  CHECK(patchy::ui::delete_downloaded_ocr_models(folder, &error));
  CHECK(!QDir(folder).exists());
  // An unset folder is refused rather than guessed.
  CHECK(!patchy::ui::delete_downloaded_ocr_models(QString(), &error));
  CHECK(!error.isEmpty());
}

void ui_ocr_picker_offers_the_model_download() {
  const SettingsGroupRestorer settings{QStringLiteral("ocr")};
  struct HookReset {
    ~HookReset() { patchy::ui::set_ocr_model_downloader({}); }
  } hook_reset;
  patchy::ui::set_ocr_model_downloader({});

  // Without anything to run the download, the note still explains and no button shows.
  {
    const ScopedOcrEngines engines({model_engine_stand_in(false)});
    bool saw = false;
    when_dialog_visible(QStringLiteral("recognizeTextDialog"), [&saw](QDialog& dialog) {
      auto* note = dialog.findChild<QLabel*>(QStringLiteral("recognizeTextUnavailableNote"));
      auto* download = dialog.findChild<QPushButton*>(QStringLiteral("recognizeTextDownloadButton"));
      CHECK(note != nullptr && download != nullptr);
      if (note != nullptr && download != nullptr) {
        CHECK(note->isVisibleTo(&dialog));
        CHECK(!download->isVisibleTo(&dialog));
        saw = true;
      }
      dialog.reject();
    });
    CHECK(!patchy::ui::request_recognize_text(nullptr, {}).has_value());
    CHECK(saw);
  }

  // With it: the row says it is a download, the note names size, source and license, the
  // languages are there to pick, and OK waits for the models. Download runs the hook once
  // with the dialog as parent; when it succeeds the picker reads the engine again.
  int downloads = 0;
  QWidget* asked_parent = nullptr;
  patchy::ui::set_ocr_model_downloader([&](QWidget* parent) {
    ++downloads;
    asked_parent = parent;
    patchy::ocr::set_engines_for_testing(
        [engine = model_engine_stand_in(true)] { return std::vector<std::shared_ptr<patchy::ocr::Engine>>{engine}; });
    return true;
  });
  {
    const ScopedOcrEngines engines({model_engine_stand_in(false)});
    bool saw = false;
    when_dialog_visible(QStringLiteral("recognizeTextDialog"), [&](QDialog& dialog) {
      auto* engine = dialog.findChild<QComboBox*>(QStringLiteral("recognizeTextEngineCombo"));
      auto* language = dialog.findChild<QComboBox*>(QStringLiteral("recognizeTextLanguageCombo"));
      auto* note = dialog.findChild<QLabel*>(QStringLiteral("recognizeTextUnavailableNote"));
      auto* download = dialog.findChild<QPushButton*>(QStringLiteral("recognizeTextDownloadButton"));
      auto* buttons = dialog.findChild<QDialogButtonBox*>();
      CHECK(engine != nullptr && language != nullptr && note != nullptr && download != nullptr && buttons != nullptr);
      if (engine == nullptr || language == nullptr || note == nullptr || download == nullptr || buttons == nullptr) {
        dialog.reject();
        return;
      }
      CHECK(engine->currentText() == QStringLiteral("PP-OCR (100 MB download)"));
      CHECK(language->count() == 2);
      CHECK(note->text().contains(QStringLiteral("100 MB")));
      CHECK(note->text().contains(QStringLiteral("ai-models-4")));
      CHECK(note->text().contains(QStringLiteral("Apache License 2.0")));
      CHECK(download->isVisibleTo(&dialog));
      CHECK(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
      CHECK(downloads == 0);  // showing the row and the note downloads nothing
      download->click();
      CHECK(downloads == 1);
      CHECK(asked_parent == &dialog);
      CHECK(engine->currentText() == QStringLiteral("PP-OCR"));
      CHECK(!note->isVisibleTo(&dialog));
      CHECK(!download->isVisibleTo(&dialog));
      CHECK(buttons->button(QDialogButtonBox::Ok)->isEnabled());
      saw = true;
      dialog.reject();
    });
    CHECK(!patchy::ui::request_recognize_text(nullptr, {}).has_value());
    CHECK(saw);
  }

  // A built-in engine with no language points at the downloadable one when it is listed.
  {
    FakeEngineConfig bare;
    bare.id = "windows";
    bare.languages.clear();
    bare.unavailable = patchy::ocr::Unavailable::NoLanguages;
    bare.unavailable_reason = "no text recognition language is installed";
    const ScopedOcrEngines engines({patchy::ocr::make_fake_engine(bare), model_engine_stand_in(false)});
    bool saw = false;
    when_dialog_visible(QStringLiteral("recognizeTextDialog"), [&saw](QDialog& dialog) {
      auto* engine = dialog.findChild<QComboBox*>(QStringLiteral("recognizeTextEngineCombo"));
      auto* note = dialog.findChild<QLabel*>(QStringLiteral("recognizeTextUnavailableNote"));
      if (engine != nullptr && note != nullptr) {
        CHECK(engine->count() == 2);
        CHECK(engine->currentData().toString() == QStringLiteral("windows"));
        CHECK(note->text().contains(QStringLiteral("Or pick PP-OCR above")));
        saw = true;
      }
      dialog.reject();
    });
    CHECK(!patchy::ui::request_recognize_text(nullptr, {}).has_value());
    CHECK(saw);
  }
}

void ui_preferences_text_recognition_models_status_and_delete() {
  if (!patchy::ai::ocr_models_supported()) {
    patchy::test::skip(patchy::test::SkipKind::platform, "this build cannot run the text recognition models");
  }
  QTemporaryDir scratch;
  CHECK(scratch.isValid());
  const auto folder = QDir(scratch.path()).filePath(QStringLiteral("ai-models"));
  write_stand_ins(folder);
  const DownloadedModelsFolder pointed(folder);
  patchy::ui::MainWindow window;
  show_window(window);

  // Already there: the main window's download answers without a dialog or a request.
  QString error;
  CHECK(window.download_ocr_models(&error));
  CHECK(error.isEmpty());
  CHECK(find_top_level_dialog(QStringLiteral("ocrModelDownloadDialog")) == nullptr);

  bool saw = false;
  QTimer::singleShot(0, guarded([&saw, &folder] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* status = dialog->findChild<QLabel*>(QStringLiteral("preferencesOcrModelsStatus"));
    auto* download = dialog->findChild<QPushButton*>(QStringLiteral("preferencesDownloadOcrModelsButton"));
    auto* remove = dialog->findChild<QPushButton*>(QStringLiteral("preferencesDeleteOcrModelsButton"));
    CHECK(status != nullptr && download != nullptr && remove != nullptr);
    if (status != nullptr && download != nullptr && remove != nullptr) {
      CHECK(status->text().contains(QStringLiteral("downloaded (100 MB)")));
      CHECK(!download->isEnabled());
      CHECK(remove->isEnabled());
      CHECK(download->toolTip().contains(QStringLiteral("Apache License 2.0")));
      remove->click();
      CHECK(status->text().contains(QStringLiteral("not downloaded")));
      CHECK(status->text().contains(QStringLiteral("ai-models-4")));
      CHECK(download->isEnabled());  // not clicked: it would reach the network
      CHECK(!remove->isEnabled());
      CHECK(!patchy::ui::downloaded_ocr_models_present(folder));
      saw = true;
    }
    dialog->reject();
  }));
  patchy::ui::MainWindowTestAccess::show_preferences(window);
  CHECK(saw);
}

// PATCHY_DOWNLOADED_MODELS_DIR (docs/visual-qa.md) moves the downloaded models folder for a
// run: where the models are looked for, downloaded to and deleted from. A package check
// points it at staged models, so it never reads or writes the user's own folder. The
// folder here has a Unicode name (the variable is a path).
void ui_downloaded_models_folder_follows_the_environment() {
  struct Restore {
    QByteArray previous{qgetenv("PATCHY_DOWNLOADED_MODELS_DIR")};
    bool had{qEnvironmentVariableIsSet("PATCHY_DOWNLOADED_MODELS_DIR")};
    std::filesystem::path engine_folder{patchy::ai::downloaded_model_directory()};
    ~Restore() {
      if (had) {
        qputenv("PATCHY_DOWNLOADED_MODELS_DIR", previous);
      } else {
        qunsetenv("PATCHY_DOWNLOADED_MODELS_DIR");
      }
      patchy::ai::release_ocr_models();
      patchy::ai::set_downloaded_model_directory(engine_folder);
    }
  } restore;
  const auto root = patchy::ui::to_qstring(
      std::filesystem::absolute(patchy::test::unicode_artifact_dir(patchy::test::kUnicodeCombinedStem)));
  const auto folder = QDir(root).filePath(QStringLiteral("redirected-models"));
  // qputenv takes local 8-bit bytes, the ANSI code page on Windows; the wide call keeps
  // every character.
#ifdef Q_OS_WIN
  CHECK(_wputenv_s(L"PATCHY_DOWNLOADED_MODELS_DIR", folder.toStdWString().c_str()) == 0);
#else
  qputenv("PATCHY_DOWNLOADED_MODELS_DIR", folder.toUtf8());
#endif
  // With no folder configured yet, the redirected one is the answer, and configuring
  // hands it to the engines.
  patchy::ai::set_downloaded_model_directory({});
  CHECK(QDir(patchy::ui::downloaded_models_folder()) == QDir(folder));
  patchy::ui::configure_downloaded_object_selection_model();
  CHECK(QDir(patchy::ui::to_qstring(patchy::ai::downloaded_model_directory())) == QDir(folder));
  CHECK(!patchy::ui::downloaded_ocr_models_present(patchy::ui::downloaded_models_folder()));
  write_stand_ins(folder);
  CHECK(patchy::ui::downloaded_ocr_models_present(patchy::ui::downloaded_models_folder()));
  QString error;
  CHECK(patchy::ui::delete_downloaded_ocr_models(patchy::ui::downloaded_models_folder(), &error));
  CHECK(!QDir(folder).exists());
}

// --- on request only: the real download ------------------------------------------------

// The two tests below reach the network, so they run only when PATCHY_TEST_OCR_DOWNLOAD=1
// (remote-build.ps1 -TestEnv 'PATCHY_TEST_OCR_DOWNLOAD=1'). Everything else in the suite
// stays offline.
void skip_unless_network_download_enabled() {
  if (qEnvironmentVariableIntValue("PATCHY_TEST_OCR_DOWNLOAD") == 0) {
    patchy::test::skip(patchy::test::SkipKind::other,
                       "opt-in: PATCHY_TEST_OCR_DOWNLOAD=1 downloads the text recognition models from the network");
  }
  if (!patchy::ai::ocr_models_supported()) {
    patchy::test::skip(patchy::test::SkipKind::platform, "this build cannot run the text recognition models");
  }
}

QByteArray sha256_hex_of(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  return hash.addData(&file) ? hash.result().toHex() : QByteArray();
}

// Every file in `folder` is one of the pinned files, whole: nothing partial, nothing else.
void check_only_whole_pinned_files(const QString& folder) {
  const auto& files = patchy::ui::ocr_model_download_files();
  const auto names = QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::System);
  for (const auto& name : names) {
    const auto pinned = std::find_if(files.begin(), files.end(), [&](const auto& file) { return file.name == name; });
    if (pinned == files.end()) {
      std::fprintf(stderr, "  unexpected file left in the models folder: %s\n", name.toUtf8().constData());
    }
    CHECK(pinned != files.end());
    if (pinned != files.end()) {
      CHECK(QFileInfo(QDir(folder).filePath(name)).size() == pinned->size);
      CHECK(sha256_hex_of(QDir(folder).filePath(name)) == pinned->sha256_hex);
    }
  }
}

QString squeezed_lower(const QString& text) {
  QString out;
  for (const auto ch : text) {
    if (!ch.isSpace()) {
      out.append(ch.toLower());
    }
  }
  return out;
}

// What the download's progress dialog showed, sampled while the Download button's own
// event loop runs.
struct DownloadWatch {
  int sightings{0};
  int highest_bar{0};
  QString heading;
  QString file;
  bool cancel_when_started{false};
  bool cancel_clicked{false};
};

// Presses `download` and returns when the download it starts has ended, one way or the
// other; the milliseconds it took.
qint64 press_download_and_watch(QPushButton& download, DownloadWatch& watch) {
  QTimer poll;
  poll.setInterval(15);
  QObject::connect(&poll, &QTimer::timeout, &download, guarded([&watch] {
    auto* progress = find_top_level_dialog(QStringLiteral("ocrModelDownloadDialog"));
    if (progress == nullptr) {
      return;
    }
    ++watch.sightings;
    auto* bar = progress->findChild<QProgressBar*>(QStringLiteral("modelDownloadProgressBar"));
    auto* heading = progress->findChild<QLabel*>(QStringLiteral("modelDownloadHeadingLabel"));
    auto* file = progress->findChild<QLabel*>(QStringLiteral("modelDownloadFileLabel"));
    auto* cancel = progress->findChild<QPushButton*>(QStringLiteral("modelDownloadCancelButton"));
    if (bar == nullptr || heading == nullptr || file == nullptr || cancel == nullptr) {
      return;
    }
    watch.highest_bar = std::max(watch.highest_bar, bar->value());
    watch.heading = heading->text();
    watch.file = file->text();
    if (watch.cancel_when_started && !watch.cancel_clicked && bar->value() > 0) {
      watch.cancel_clicked = true;
      cancel->click();
    }
  }));
  poll.start();
  QElapsedTimer timer;
  timer.start();
  download.click();
  poll.stop();
  return timer.elapsed();
}

// The whole feature against the real links: Preferences says the models are missing, its
// Download button fetches them (cancelled once part way, which must leave nothing partial,
// then to the end), every file is the pinned one, the engine reads drawn text from the
// files that arrived, and Delete takes them away again. The models folder has a Unicode
// name, as a user profile named in Japanese gives it: the files are written there and the
// engine loads its networks and character lists from there.
void ui_ocr_models_download_from_the_network_if_enabled() {
  skip_unless_network_download_enabled();
  const auto root = patchy::ui::to_qstring(
      std::filesystem::absolute(patchy::test::unicode_artifact_dir(patchy::test::kUnicodeCombinedStem)));
  const auto folder = QDir(root).filePath(QStringLiteral("network-models"));
  const DownloadedModelsFolder pointed(folder);
  patchy::ui::MainWindow window;
  show_window(window);
  // The engines this machine really has, the downloadable one among them.
  const patchy::test::ui::ScopedRealOcrEngines real_engines;
  const auto engine = patchy::ai::ocr_model_engine();
  CHECK(engine != nullptr);
  if (engine == nullptr) {
    return;
  }
  CHECK(engine->unavailable() == patchy::ocr::Unavailable::NotDownloaded);

  bool saw = false;
  QTimer::singleShot(0, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* status = dialog->findChild<QLabel*>(QStringLiteral("preferencesOcrModelsStatus"));
    auto* download = dialog->findChild<QPushButton*>(QStringLiteral("preferencesDownloadOcrModelsButton"));
    auto* remove = dialog->findChild<QPushButton*>(QStringLiteral("preferencesDeleteOcrModelsButton"));
    auto* engines = dialog->findChild<QComboBox*>(QStringLiteral("preferencesOcrEngineCombo"));
    CHECK(status != nullptr && download != nullptr && remove != nullptr && engines != nullptr);
    if (status == nullptr || download == nullptr || remove == nullptr || engines == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(status->text().contains(QStringLiteral("not downloaded")));
    CHECK(download->isEnabled());
    CHECK(!remove->isEnabled());
    const auto row_text = [engines] {
      const int row = engines->findData(QStringLiteral("ppocr"));
      return row < 0 ? QString() : engines->itemText(row);
    };
    CHECK(row_text() == QStringLiteral("PP-OCR (100 MB download)"));

    // Cancelled once bytes are arriving: the models are still missing and nothing partial
    // stays (files that had already arrived whole may).
    DownloadWatch cancelled;
    cancelled.cancel_when_started = true;
    press_download_and_watch(*download, cancelled);
    CHECK(cancelled.cancel_clicked);
    CHECK(find_top_level_dialog(QStringLiteral("ocrModelDownloadDialog")) == nullptr);
    CHECK(status->text().contains(QStringLiteral("not downloaded")));
    CHECK(download->isEnabled());
    CHECK(!patchy::ui::downloaded_ocr_models_present(folder));
    check_only_whole_pinned_files(folder);
    CHECK(engine->unavailable() == patchy::ocr::Unavailable::NotDownloaded);

    // To the end.
    DownloadWatch whole;
    const auto elapsed = press_download_and_watch(*download, whole);
    std::fprintf(stderr, "  text recognition models: %d MB downloaded in %.1f s (dialog seen %d times, last: %s)\n",
                 patchy::ui::ocr_model_download_megabytes(), static_cast<double>(elapsed) / 1000.0, whole.sightings,
                 whole.file.toUtf8().constData());
    if (!status->text().contains(QStringLiteral("downloaded (100 MB)"))) {
      std::fprintf(stderr, "  status after the download: %s | status bar: %s\n", status->text().toUtf8().constData(),
                   window.statusBar()->currentMessage().toUtf8().constData());
    }
    CHECK(whole.sightings > 0);
    CHECK(whole.highest_bar > 0);
    CHECK(whole.heading.contains(QStringLiteral("of 100 MB")));
    CHECK(whole.file.contains(QStringLiteral("of 20")));
    CHECK(find_top_level_dialog(QStringLiteral("ocrModelDownloadDialog")) == nullptr);
    CHECK(status->text().contains(QStringLiteral("downloaded (100 MB)")));
    CHECK(!download->isEnabled());
    CHECK(remove->isEnabled());
    CHECK(row_text() == QStringLiteral("PP-OCR"));
    CHECK(patchy::ui::downloaded_ocr_models_present(folder));

    // All twenty, each the pinned bytes, and nothing else in the folder.
    for (const auto& file : patchy::ui::ocr_model_download_files()) {
      const auto path = QDir(folder).filePath(file.name);
      CHECK(QFileInfo(path).size() == file.size);
      CHECK(sha256_hex_of(path) == file.sha256_hex);
    }
    check_only_whole_pinned_files(folder);
    CHECK(QDir(folder).entryList(QDir::Files).size() == 20);

    // The engine runs from the files that arrived: drawn text comes back.
    CHECK(engine->unavailable() == patchy::ocr::Unavailable::None);
    if (engine->unavailable() == patchy::ocr::Unavailable::None) {
      patchy::test::ui::OcrTestPage page(900, 260);
      const auto font = patchy::test::ui::ocr_test_font(40);
      const QString first = QStringLiteral("Downloaded models read");
      const QString second = QStringLiteral("this line of text 12345");
      page.draw(first, 40, 40, font);
      page.draw(second, 40, 130, font);
      const patchy::ocr::ImageView view{page.image.constBits(), page.image.width(), page.image.height(),
                                        page.image.bytesPerLine(), patchy::ocr::PixelLayout::Rgba8};
      const auto result = engine->open("en")->recognize(view);
      const auto text = result.text();
      const auto read = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
      if (squeezed_lower(read) != squeezed_lower(first + second)) {
        std::fprintf(stderr, "  the engine read: %s\n", read.toUtf8().constData());
      }
      CHECK(result.lines.size() == 2);
      CHECK(squeezed_lower(read) == squeezed_lower(first + second));
    }

    // Delete: exactly those files go, and the engine is a download again.
    remove->click();
    CHECK(status->text().contains(QStringLiteral("not downloaded")));
    CHECK(download->isEnabled());
    CHECK(!remove->isEnabled());
    CHECK(row_text() == QStringLiteral("PP-OCR (100 MB download)"));
    CHECK(!QDir(folder).exists());
    CHECK(engine->unavailable() == patchy::ocr::Unavailable::NotDownloaded);
    saw = true;
    dialog->reject();
  }));
  patchy::ui::MainWindowTestAccess::show_preferences(window);
  CHECK(saw);
}

// The fallback links through the app's own downloader: a first link that cannot serve the
// file (GitHub answers 404) must end at the publisher's pinned copy, with the same bytes.
// One file from each fallback host, and one more from the first.
void ui_ocr_model_fallback_links_serve_the_pinned_files_if_enabled() {
  skip_unless_network_download_enabled();
  QTemporaryDir scratch;
  CHECK(scratch.isValid());
  const auto folder = QDir(scratch.path()).filePath(QStringLiteral("ai-models"));
  std::vector<patchy::ui::DownloadableModelFile> list;
  for (const auto& file : patchy::ui::ocr_model_download_files()) {
    if (file.name == QStringLiteral("ppocr_v6_small_rec.yml") || file.name == QStringLiteral("ppocr_el_rec.yml") ||
        file.name == QStringLiteral("ppocr_LICENSE.txt")) {
      auto broken = file;
      broken.url = QUrl(file.url.toString() + QStringLiteral(".missing"));
      list.push_back(std::move(broken));
    }
  }
  CHECK(list.size() == 3);
  QObject owner;
  std::vector<QUrl> links;
  patchy::ui::ModelDownloadResult result;
  bool done = false;
  QEventLoop loop;
  const auto job = patchy::ui::start_model_download(
      &owner, folder, list,
      [&links](const patchy::ui::ModelDownloadProgress& progress) {
        if (links.empty() || links.back() != progress.url) {
          links.push_back(progress.url);
        }
      },
      [&](patchy::ui::ModelDownloadResult finished) {
        result = std::move(finished);
        done = true;
        loop.quit();
      });
  if (!done) {
    QTimer::singleShot(240000, &loop, &QEventLoop::quit);
    loop.exec();
  }
  CHECK(done);
  if (!result.ok) {
    std::fprintf(stderr, "  fallback download failed: %s\n", result.error.toUtf8().constData());
    for (const auto& link : links) {
      std::fprintf(stderr, "    tried %s\n", link.toString().toUtf8().constData());
    }
  }
  CHECK(result.ok);
  for (const auto& file : list) {
    const auto path = QDir(folder).filePath(file.name);
    CHECK(QFileInfo(path).size() == file.size);
    CHECK(sha256_hex_of(path) == file.sha256_hex);
    // The broken first link was tried, then the fallback.
    CHECK(std::find(links.begin(), links.end(), file.url) != links.end());
    CHECK(std::find(links.begin(), links.end(), file.fallback_urls.front()) != links.end());
  }
  check_only_whole_pinned_files(folder);
}

}  // namespace

std::vector<patchy::test::TestCase> ocr_download_ui_tests() {
  return {
      {"ui_ocr_model_download_list_is_pinned", ui_ocr_model_download_list_is_pinned},
      {"ui_ocr_models_delete_removes_only_their_files", ui_ocr_models_delete_removes_only_their_files},
      {"ui_ocr_picker_offers_the_model_download", ui_ocr_picker_offers_the_model_download},
      {"ui_preferences_text_recognition_models_status_and_delete",
       ui_preferences_text_recognition_models_status_and_delete},
      {"ui_downloaded_models_folder_follows_the_environment", ui_downloaded_models_folder_follows_the_environment},
      {"ui_ocr_models_download_from_the_network_if_enabled", ui_ocr_models_download_from_the_network_if_enabled},
      {"ui_ocr_model_fallback_links_serve_the_pinned_files_if_enabled",
       ui_ocr_model_fallback_links_serve_the_pinned_files_if_enabled},
  };
}
