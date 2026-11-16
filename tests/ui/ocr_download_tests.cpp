// The text recognition models as a download (docs/ocr-engines.md, "PP-OCR"): the pinned
// file list, what Delete removes, the Download offer in the engine picker and the rows in
// Preferences. Nothing here reaches the network: the one button that would is never pressed
// without a stand-in.

#include "ai/ocr_model_engine.hpp"
#include "ai/ocr_models.hpp"
#include "ai/segmentation_engine.hpp"
#include "ocr/ocr_fake_engine.hpp"
#include "ocr_test_support.hpp"
#include "test_harness.hpp"
#include "ui/main_window.hpp"
#include "ui/ocr_controls.hpp"
#include "ui/ocr_model_download.hpp"
#include "ui/qt_paths.hpp"
#include "ui/recognize_text_dialog.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
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

}  // namespace

std::vector<patchy::test::TestCase> ocr_download_ui_tests() {
  return {
      {"ui_ocr_model_download_list_is_pinned", ui_ocr_model_download_list_is_pinned},
      {"ui_ocr_models_delete_removes_only_their_files", ui_ocr_models_delete_removes_only_their_files},
      {"ui_ocr_picker_offers_the_model_download", ui_ocr_picker_offers_the_model_download},
      {"ui_preferences_text_recognition_models_status_and_delete",
       ui_preferences_text_recognition_models_status_and_delete},
  };
}
