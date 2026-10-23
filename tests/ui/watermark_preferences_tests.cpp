// Preferences > Watermarks (docs/watermarks.md, "Preferences"): the settings and their
// persistence, the mark on save in each mode (flat formats only, never the open document or
// a PSD), the check on open (background worker, status-bar indicator, non-modal notice) and
// its cancel when the document closes. The Stable Diffusion part of the check needs no
// models; the save tests need the staged watermark models and skip without them.

#include "ai/trustmark_watermark.hpp"
#include "app_shell_test_support.hpp"
#include "core/invisible_watermark.hpp"
#include "invisible_watermark_reference.hpp"
#include "local_psd_fixtures.hpp"
#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui/ui_test_access.hpp"
#include "ui/watermark_settings.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

namespace {

using namespace patchy::test::ui;
using patchy::test::ui::app_shell_support::run_preferences_dialog;
using patchy::test::ui::scripting_support::backlog_contains;
using patchy::test::ui::scripting_support::run_script;
using patchy::ui::MainWindowTestAccess;

// Restores the watermark preferences and the model folder after a test.
struct WatermarkPreferencesScope {
  explicit WatermarkPreferencesScope(bool with_models) {
    std::filesystem::path folder = std::filesystem::current_path() / "ai-models-download";
    if (!with_models) {
      folder = std::filesystem::current_path() / "test-artifacts" / "watermark-no-models";
      std::filesystem::create_directories(folder);
    }
    patchy::ai::set_downloaded_model_directory(folder);
    reset();
  }
  ~WatermarkPreferencesScope() {
    reset();
    patchy::ai::set_downloaded_model_directory({});
  }
  static void reset() {
    auto settings = patchy::ui::app_settings();
    settings.remove(QLatin1String(patchy::ui::kWatermarkTextSettingKey));
    settings.remove(QLatin1String(patchy::ui::kWatermarkSaveModeSettingKey));
    settings.remove(QLatin1String(patchy::ui::kWatermarkCheckOnOpenSettingKey));
  }
  WatermarkPreferencesScope(const WatermarkPreferencesScope&) = delete;
  WatermarkPreferencesScope& operator=(const WatermarkPreferencesScope&) = delete;
};

QString artifact_dir() {
  const auto dir = QDir(QStringLiteral("test-artifacts/watermark-preferences")).absolutePath();
  QDir().mkpath(dir);
  return dir;
}

QImage rgb_qimage(const std::vector<std::uint8_t>& rgb, int width, int height) {
  QImage image(width, height, QImage::Format_RGB888);
  for (int y = 0; y < height; ++y) {
    std::memcpy(image.scanLine(y), rgb.data() + static_cast<std::size_t>(y) * width * 3,
                static_cast<std::size_t>(width) * 3);
  }
  return image;
}

// A photo to save: the committed CC0 Havana photo at 800 px.
QString write_photo(const QString& name) {
  const auto source = QString::fromStdWString(
      (patchy::test::source_root_path() / "test-fixtures" / "readme" / "havana_vintage_car_cc0.jpg").wstring());
  QImage image(source);
  CHECK(!image.isNull());
  const auto path = artifact_dir() + QLatin1Char('/') + name;
  CHECK(image.scaledToWidth(800, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888).save(path));
  return path;
}

// What TrustMark reads from a saved file (8-bit RGB through Qt's readers).
std::vector<std::string> trustmark_texts_in_file(const QString& path) {
  const auto image = QImage(path).convertToFormat(QImage::Format_RGBA8888);
  const auto scan = patchy::ai::scan_trustmark(patchy::ui::pixels_from_image_rgba(image));
  std::vector<std::string> texts;
  for (const auto& mark : scan.marks) {
    texts.push_back(mark.text);
  }
  return texts;
}

void wait_for_watermark_checks(patchy::ui::MainWindow& window, int timeout_ms = 30000) {
  QElapsedTimer timer;
  timer.start();
  while (MainWindowTestAccess::watermark_checks_running(window) > 0 && timer.elapsed() < timeout_ms) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  // The completion is posted after the worker's last step.
  for (int i = 0; i < 5; ++i) {
    QApplication::processEvents();
  }
}

QMessageBox* visible_message_box(const QString& name) {
  for (auto* widget : QApplication::topLevelWidgets()) {
    auto* box = qobject_cast<QMessageBox*>(widget);
    if (box != nullptr && box->objectName() == name && box->isVisible()) {
      return box;
    }
  }
  return nullptr;
}

// The page, its controls, persistence through OK, the models row moved off Tools, and the
// scripting view of the same settings.
void ui_watermark_preferences_page_persists() {
  const WatermarkPreferencesScope scope(false);
  patchy::ui::MainWindow window;
  show_window(window);
  run_preferences_dialog(window, /*accept=*/true, [](QDialog& dialog) {
    auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("preferencesTabWidget"));
    CHECK(tabs != nullptr);
    if (tabs == nullptr) {
      return;
    }
    int page = -1;
    for (int index = 0; index < tabs->count(); ++index) {
      if (tabs->tabText(index) == QObject::tr("Watermarks")) {
        page = index;
      }
    }
    CHECK(page >= 0);
    tabs->setCurrentIndex(page);
    QApplication::processEvents();
    auto* text = dialog.findChild<QLineEdit*>(QStringLiteral("preferencesWatermarkTextEdit"));
    auto* remaining = dialog.findChild<QLabel*>(QStringLiteral("preferencesWatermarkRemainingLabel"));
    auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("preferencesWatermarkSaveCombo"));
    auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("preferencesWatermarkCheckOnOpenCheck"));
    CHECK(text != nullptr && remaining != nullptr && mode != nullptr && check != nullptr);
    if (text == nullptr || remaining == nullptr || mode == nullptr || check == nullptr) {
      return;
    }
    CHECK(text->text().isEmpty() && mode->currentIndex() == 0 && !check->isChecked());  // the defaults
    text->setText(QStringLiteral("Seth26"));
    CHECK(remaining->text().startsWith(QStringLiteral("2 ")));
    text->setText(QStringLiteral("TooLongText"));  // the field holds 8 characters
    CHECK(text->text() == QStringLiteral("TooLongT"));
    text->setText(QStringLiteral("Seth26"));
    mode->setCurrentIndex(mode->findData(QStringLiteral("ask")));
    check->setChecked(true);
    // The models row lives here now, not on Tools.
    if (patchy::ai::trustmark_runtime_available()) {
      auto* models = dialog.findChild<QWidget*>(QStringLiteral("preferencesWatermarkModelsGroup"));
      CHECK(models != nullptr && tabs->widget(page)->isAncestorOf(models));
      auto* needed = dialog.findChild<QLabel*>(QStringLiteral("preferencesWatermarkModelsNeededNote"));
      CHECK(needed != nullptr && needed->isVisible());  // Ask chosen, no models in this folder
    }
    dialog.resize(dialog.width(), std::max(dialog.height(), 640));
    QApplication::processEvents();
    capture_widget_artifact("ui_preferences_watermarks", dialog);
  });
  CHECK(patchy::ui::stored_watermark_text() == QStringLiteral("Seth26"));
  CHECK(patchy::ui::stored_watermark_save_mode() == patchy::ui::WatermarkSaveMode::Ask);
  CHECK(patchy::ui::stored_watermark_check_on_open());
  CHECK(run_script(window, QStringLiteral(R"JS(
var p = app.watermarkPreferences;
if (p.text !== "Seth26" || p.addOnSave !== "ask" || p.checkOnOpen !== true) throw new Error(JSON.stringify(p));
function throws(f) { try { f(); } catch (e) { return true; } return false; }
if (!throws(function () { app.setWatermarkPreferences({addOnSave: "sometimes"}); })) throw new Error("token");
if (!throws(function () { app.setWatermarkPreferences({text: "TooLongText"}); })) throw new Error("length");
if (!throws(function () { app.setWatermarkPreferences({bogus: 1}); })) throw new Error("key");
app.setWatermarkPreferences({addOnSave: "always", checkOnOpen: false, text: "Ann"});
var q = app.watermarkPreferences;
if (q.text !== "Ann" || q.addOnSave !== "always" || q.checkOnOpen !== false) throw new Error(JSON.stringify(q));
console.log("prefs-ok");
)JS")));
  CHECK(backlog_contains(window, QStringLiteral("prefs-ok")));
  CHECK(patchy::ui::stored_watermark_save_mode() == patchy::ui::WatermarkSaveMode::Always);
}

// Each save mode, PNG and JPEG against PSD, and the open document left as it was.
void ui_watermark_on_save_marks_flat_files_only() {
  const WatermarkPreferencesScope scope(true);
  if (!patchy::ai::trustmark_models_available()) {
    patchy::test::skip("the watermark models are not staged (setup-ai-deps provisions them)");
  }
  const auto photo = write_photo(QStringLiteral("photo.png"));
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::open_document_path(window, photo);
  QApplication::processEvents();
  const auto layer_bytes = [&window] {
    const auto& layers = std::as_const(MainWindowTestAccess::document(window)).layers();
    const auto data = layers.front().pixels().data();
    return std::vector<std::uint8_t>(data.begin(), data.end());
  };
  const auto before = layer_bytes();
  const auto undo_before = MainWindowTestAccess::active_session_undo_depth(window);
  patchy::ui::set_stored_watermark_text(QStringLiteral("Save26"));
  const auto dir = artifact_dir();

  // Never: nothing.
  patchy::ui::set_stored_watermark_save_mode(patchy::ui::WatermarkSaveMode::Never);
  CHECK(MainWindowTestAccess::save_document_to_path(window, dir + QStringLiteral("/never.png")));
  CHECK(trustmark_texts_in_file(dir + QStringLiteral("/never.png")).empty());

  // Every save: PNG and JPEG are marked; the open document is not.
  patchy::ui::set_stored_watermark_save_mode(patchy::ui::WatermarkSaveMode::Always);
  CHECK(MainWindowTestAccess::save_document_to_path(window, dir + QStringLiteral("/always.png")));
  CHECK(trustmark_texts_in_file(dir + QStringLiteral("/always.png")) == std::vector<std::string>{"Save26"});
  patchy::ui::ImageSaveOptions jpeg;
  jpeg.jpeg_quality = 90;
  CHECK(MainWindowTestAccess::save_document_to_path(window, dir + QStringLiteral("/always.jpg"), jpeg));
  CHECK(trustmark_texts_in_file(dir + QStringLiteral("/always.jpg")) == std::vector<std::string>{"Save26"});
  CHECK(layer_bytes() == before);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo_before);

  // A PSD keeps its layer pixels untouched.
  const auto psd = dir + QStringLiteral("/always.psd");
  CHECK(MainWindowTestAccess::save_document_to_path(window, psd));
  const auto reread = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(psd));
  CHECK(patchy::ai::scan_trustmark(patchy::Compositor{}.flatten_rgb8(reread)).marks.empty());

  // Ask: "Save Without" leaves the file alone, "Add Watermark" marks it.
  patchy::ui::set_stored_watermark_save_mode(patchy::ui::WatermarkSaveMode::Ask);
  for (const bool add : {false, true}) {
    bool answered = false;
    QTimer::singleShot(0, [&answered, add] {
      auto* box = visible_message_box(QStringLiteral("saveWatermarkPrompt"));
      CHECK(box != nullptr);
      if (box == nullptr) {
        return;
      }
      if (add) {
        capture_widget_artifact("ui_save_watermark_prompt", *box);
      }
      for (auto* button : box->buttons()) {
        if (box->buttonRole(button) == (add ? QMessageBox::AcceptRole : QMessageBox::RejectRole)) {
          answered = true;
          button->click();
          return;
        }
      }
    });
    const auto target = dir + (add ? QStringLiteral("/ask-add.png") : QStringLiteral("/ask-skip.png"));
    CHECK(MainWindowTestAccess::save_document_to_path(window, target));
    CHECK(answered);
    CHECK(trustmark_texts_in_file(target) == (add ? std::vector<std::string>{"Save26"} : std::vector<std::string>{}));
  }
  CHECK(patchy::ui::stored_watermark_save_mode() == patchy::ui::WatermarkSaveMode::Ask);  // nothing remembered
  CHECK(layer_bytes() == before);

  // A file that already carries the text is not marked twice.
  MainWindowTestAccess::open_document_path(window, dir + QStringLiteral("/always.png"));
  QApplication::processEvents();
  patchy::ui::set_stored_watermark_save_mode(patchy::ui::WatermarkSaveMode::Always);
  const auto marked_before = layer_bytes();
  CHECK(MainWindowTestAccess::save_document_to_path(window, dir + QStringLiteral("/again.png")));
  const auto again = QImage(dir + QStringLiteral("/again.png")).convertToFormat(QImage::Format_RGB888);
  const auto original = QImage(dir + QStringLiteral("/always.png")).convertToFormat(QImage::Format_RGB888);
  CHECK(again == original);
  CHECK(layer_bytes() == marked_before);
}

// The check on open: the Stable Diffusion scan runs on a worker with the indicator shown,
// then a non-modal notice names the watermark; Show in File Info opens the Watermarks page.
// A document closed mid-check gets no notice.
void ui_watermark_check_on_open_shows_a_notice() {
  const WatermarkPreferencesScope scope(false);
  constexpr int kSize = 512;
  const auto clean = patchy::test::watermark_fixture_image(kSize, kSize, 41);
  const auto& known = patchy::known_invisible_watermarks();
  const auto xl =
      std::find_if(known.begin(), known.end(), [](const auto& item) { return item.id == "stableDiffusionXL"; });
  CHECK(xl != known.end());
  const auto marked = patchy::test::embed_dwt_dct_reference(clean, kSize, kSize, xl->bits,
                                                            patchy::test::WatermarkEmbedPath::Float);
  const auto marked_path = artifact_dir() + QStringLiteral("/sdxl-marked.png");
  const auto clean_path = artifact_dir() + QStringLiteral("/clean.png");
  CHECK(rgb_qimage(marked, kSize, kSize).save(marked_path));
  CHECK(rgb_qimage(clean, kSize, kSize).save(clean_path));

  patchy::ui::MainWindow window;
  show_window(window);
  // Off (the default): opening runs nothing.
  MainWindowTestAccess::open_document_path(window, marked_path);
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::watermark_checks_running(window) == 0);

  patchy::ui::set_stored_watermark_check_on_open(true);
  MainWindowTestAccess::open_document_path(window, marked_path);
  CHECK(MainWindowTestAccess::watermark_checks_running(window) == 1);
  auto* indicator = window.findChild<QWidget*>(QStringLiteral("watermarkCheckIndicator"));
  CHECK(indicator != nullptr && indicator->isVisible());
  wait_for_watermark_checks(window);
  CHECK(MainWindowTestAccess::watermark_checks_running(window) == 0);
  CHECK(indicator == nullptr || !indicator->isVisible());
  auto* notice = visible_message_box(QStringLiteral("watermarkFoundNotice"));
  CHECK(notice != nullptr);
  if (notice != nullptr) {
    CHECK(!notice->isModal());
    CHECK(notice->informativeText().contains(QStringLiteral("Stable Diffusion XL")));
    capture_widget_artifact("ui_watermark_found_notice", *notice);
    bool file_info_opened = false;
    QTimer::singleShot(100, [&file_info_opened] {
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyFileInfoDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      QApplication::processEvents();
      bool found = false;
      for (auto* label : dialog->findChildren<QLabel*>(QStringLiteral("fileInfoWatermarkScanResult"))) {
        found = found || (label->isVisible() && label->text().contains(QStringLiteral("Stable Diffusion XL")));
      }
      CHECK(found);
      file_info_opened = true;
      dialog->reject();
    });
    auto* show = notice->findChild<QPushButton*>(QStringLiteral("watermarkFoundShowButton"));
    CHECK(show != nullptr);
    if (show != nullptr) {
      show->click();
    }
    QElapsedTimer timer;
    timer.start();
    while (!file_info_opened && timer.elapsed() < 10000) {
      QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    CHECK(file_info_opened);
  }

  // A clean image: the check runs and stays silent.
  MainWindowTestAccess::open_document_path(window, clean_path);
  wait_for_watermark_checks(window);
  CHECK(visible_message_box(QStringLiteral("watermarkFoundNotice")) == nullptr);

  // Closed before the check finishes: cancelled, no notice.
  for (int index = MainWindowTestAccess::document_tab_count(window) - 1; index >= 0; --index) {
    MainWindowTestAccess::close_document_tab(window, index);
  }
  QApplication::processEvents();
  MainWindowTestAccess::open_document_path(window, marked_path);
  CHECK(MainWindowTestAccess::watermark_checks_running(window) == 1);
  CHECK(MainWindowTestAccess::close_document_tab(window, MainWindowTestAccess::document_tab_count(window) - 1));
  CHECK(MainWindowTestAccess::watermark_checks_running(window) == 0);
  patchy::ui::wait_for_tracked_background_workers();
  for (int i = 0; i < 10; ++i) {
    QApplication::processEvents();
  }
  CHECK(visible_message_box(QStringLiteral("watermarkFoundNotice")) == nullptr);
}

}  // namespace

std::vector<patchy::test::TestCase> watermark_preferences_tests() {
  return {
      {"ui_watermark_preferences_page_persists", ui_watermark_preferences_page_persists},
      {"ui_watermark_on_save_marks_flat_files_only", ui_watermark_on_save_marks_flat_files_only},
      {"ui_watermark_check_on_open_shows_a_notice", ui_watermark_check_on_open_shows_a_notice},
  };
}
