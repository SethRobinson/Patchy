// Adobe TrustMark in the app (docs/watermarks.md): File Info's Watermarks page with and
// without the watermark models, Image > Add Invisible Watermark with a JPEG round trip, and
// the scripting calls. The models come from the build's stand-in for the download folder
// (<build>/ai-models-download, staged when setup-ai-deps provisioned them); tests that need
// them skip without.

#include "ai/trustmark_watermark.hpp"
#include "local_psd_fixtures.hpp"
#include "render/compositor.hpp"
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "test_scratch_remove.hpp"
#include "ui/image_save_options.hpp"
#include "ui/main_window.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>

#include <filesystem>
#include <string>

namespace {

using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;
using patchy::test::ui::scripting_support::backlog_contains;
using patchy::test::ui::scripting_support::run_script;

// Points the engine's download folder at the staged models (or at an empty folder) for the
// scope, and back to unset afterwards.
struct WatermarkModelsScope {
  explicit WatermarkModelsScope(bool with_models) {
    std::filesystem::path folder = std::filesystem::current_path() / "ai-models-download";
    if (!with_models) {
      folder = std::filesystem::current_path() / "test-artifacts" / "watermark-no-models";
      std::filesystem::create_directories(folder);
    }
    patchy::ai::set_downloaded_model_directory(folder);
  }
  ~WatermarkModelsScope() { patchy::ai::set_downloaded_model_directory({}); }
  WatermarkModelsScope(const WatermarkModelsScope&) = delete;
  WatermarkModelsScope& operator=(const WatermarkModelsScope&) = delete;
};

QString artifact_dir() {
  const auto dir = QDir(QStringLiteral("test-artifacts/watermarks")).absolutePath();
  QDir().mkpath(dir);
  return dir;
}

// A photo to mark: the committed CC0 Havana photo at 1024 px.
QString write_photo(const QString& name) {
  const auto source = QString::fromStdWString(
      (patchy::test::source_root_path() / "test-fixtures" / "readme" / "havana_vintage_car_cc0.jpg").wstring());
  QImage image(source);
  CHECK(!image.isNull());
  const auto path = artifact_dir() + QLatin1Char('/') + name;
  CHECK(image.scaledToWidth(1024, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888).save(path));
  return path;
}

void select_page(QDialog& dialog, const QString& page) {
  auto* categories = dialog.findChild<QListWidget*>(QStringLiteral("fileInfoCategoryList"));
  CHECK(categories != nullptr);
  for (int row = 0; categories != nullptr && row < categories->count(); ++row) {
    if (categories->item(row)->data(Qt::UserRole).toString() == page) {
      categories->setCurrentRow(row);
    }
  }
  QApplication::processEvents();
}

QLabel* visible_scan_result(QDialog& dialog) {
  for (auto* label : dialog.findChildren<QLabel*>(QStringLiteral("fileInfoWatermarkScanResult"))) {
    if (label->isVisible()) {
      return label;
    }
  }
  return nullptr;
}

QPushButton* visible_button(QWidget& root, const QString& name) {
  for (auto* button : root.findChildren<QPushButton*>(name)) {
    if (button->isVisible()) {
      return button;
    }
  }
  return nullptr;
}

// Without the models the page offers the download; with them the scan reads the reference
// image Adobe's Python marked (Q, BCH_5, "Patchy26") and the banner names TrustMark.
void ui_file_info_trustmark_scan_and_download_states() {
  const auto marked = QString::fromStdWString(
      patchy::test::local_format_fixture_path("trustmark", "trustmark-q-bch5-text.png").wstring());
  if (!QFileInfo::exists(marked)) {
    patchy::test::skip("optional fixture missing: local-test-fixtures/trustmark");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::open_document_path(window, marked);
  QApplication::processEvents();

  {
    const WatermarkModelsScope scope(false);
    bool inspected = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyFileInfoDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      select_page(*dialog, QStringLiteral("fileInfoWatermarksPage"));
      if (patchy::ai::trustmark_runtime_available()) {
        CHECK(visible_button(*dialog, QStringLiteral("fileInfoDownloadWatermarkModelsButton")) != nullptr);
      }
      auto* scan = visible_button(*dialog, QStringLiteral("fileInfoScanWatermarksButton"));
      CHECK(scan != nullptr);
      if (scan != nullptr) {
        scan->click();
      }
      QApplication::processEvents();
      auto* result = visible_scan_result(*dialog);
      CHECK(result != nullptr &&
            result->text() == QCoreApplication::translate("QObject", "None of the Stable Diffusion watermarks was found."));
      inspected = true;
      dialog->reject();
    });
    require_action(window, "fileInfoAction")->trigger();
    QApplication::processEvents();
    CHECK(inspected);
  }

  const WatermarkModelsScope scope(true);
  if (!patchy::ai::trustmark_models_available()) {
    patchy::test::skip("the watermark models are not staged (setup-ai-deps provisions them)");
  }
  bool inspected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyFileInfoDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    select_page(*dialog, QStringLiteral("fileInfoWatermarksPage"));
    CHECK(visible_button(*dialog, QStringLiteral("fileInfoDownloadWatermarkModelsButton")) == nullptr);
    auto* scan = visible_button(*dialog, QStringLiteral("fileInfoScanWatermarksButton"));
    CHECK(scan != nullptr);
    if (scan != nullptr) {
      scan->click();
    }
    QApplication::processEvents();
    auto* result = visible_scan_result(*dialog);
    CHECK(result != nullptr && result->text().contains(QStringLiteral("TrustMark")) &&
          result->text().contains(QStringLiteral("Patchy26")) && result->text().contains(QStringLiteral("BCH_5")));
    auto* banner = dialog->findChild<QLabel*>(QStringLiteral("fileInfoPrivacyBanner"));
    CHECK(banner != nullptr && !banner->isHidden() && banner->text().contains(QStringLiteral("TrustMark")));
    auto* tree = dialog->findChild<QTreeWidget*>(QStringLiteral("fileInfoAllMetadataTree"));
    CHECK(tree != nullptr &&
          tree->findItems(QStringLiteral("TrustMark watermark"), Qt::MatchExactly | Qt::MatchRecursive).size() == 1);
    dialog->resize(dialog->width(), std::max(dialog->height(), 640));
    QApplication::processEvents();
    capture_widget_artifact("ui_file_info_trustmark_found", *dialog);
    inspected = true;
    dialog->reject();
  });
  require_action(window, "fileInfoAction")->trigger();
  QApplication::processEvents();
  CHECK(inspected);
}

// Image > Add Invisible Watermark: without the models Add Watermark stays disabled and the
// download is offered; with them a typed text goes into the layer as one undo step and is
// read back after a JPEG save at quality 90.
void ui_invisible_watermark_dialog_adds_a_readable_mark() {
  const auto photo = write_photo(QStringLiteral("photo.png"));
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::open_document_path(window, photo);
  QApplication::processEvents();
  const auto undo_before = MainWindowTestAccess::active_session_undo_depth(window);

  {
    const WatermarkModelsScope scope(false);
    bool inspected = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = find_top_level_dialog(QStringLiteral("invisibleWatermarkDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      auto* text = dialog->findChild<QLineEdit*>(QStringLiteral("invisibleWatermarkTextEdit"));
      CHECK(text != nullptr);
      if (text != nullptr) {
        text->setText(QStringLiteral("Hello"));
      }
      auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("invisibleWatermarkButtonBox"));
      CHECK(buttons != nullptr && !buttons->button(QDialogButtonBox::Ok)->isEnabled());
      if (patchy::ai::trustmark_runtime_available()) {
        CHECK(visible_button(*dialog, QStringLiteral("invisibleWatermarkDownloadButton")) != nullptr);
      }
      capture_widget_artifact("ui_invisible_watermark_dialog_no_models", *dialog);
      inspected = true;
      dialog->reject();
    });
    require_action(window, "imageAddInvisibleWatermarkAction")->trigger();
    QApplication::processEvents();
    CHECK(inspected);
    CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo_before);
  }

  const WatermarkModelsScope scope(true);
  if (!patchy::ai::trustmark_models_available()) {
    patchy::test::skip("the watermark models are not staged (setup-ai-deps provisions them)");
  }
  bool accepted = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("invisibleWatermarkDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    CHECK(dialog->findChild<QLabel*>(QStringLiteral("invisibleWatermarkOpenNote")) != nullptr);
    CHECK(visible_button(*dialog, QStringLiteral("invisibleWatermarkDownloadButton")) == nullptr);
    auto* text = dialog->findChild<QLineEdit*>(QStringLiteral("invisibleWatermarkTextEdit"));
    CHECK(text != nullptr);
    if (text != nullptr) {
      text->setText(QStringLiteral("Seth2026x"));  // one past BCH_5's 8 characters: cut to 8
      CHECK(text->text() == QStringLiteral("Seth2026"));
    }
    auto* remaining = dialog->findChild<QLabel*>(QStringLiteral("invisibleWatermarkRemainingLabel"));
    CHECK(remaining != nullptr && remaining->text().startsWith(QStringLiteral("0 ")));
    QApplication::processEvents();
    capture_widget_artifact("ui_invisible_watermark_dialog", *dialog);
    auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("invisibleWatermarkButtonBox"));
    CHECK(buttons != nullptr && buttons->button(QDialogButtonBox::Ok)->isEnabled());
    accepted = true;
    if (buttons != nullptr) {
      buttons->button(QDialogButtonBox::Ok)->click();
    }
  });
  require_action(window, "imageAddInvisibleWatermarkAction")->trigger();
  QApplication::processEvents();
  CHECK(accepted);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  const auto scan = patchy::ai::scan_trustmark(patchy::Compositor{}.flatten_rgb8(MainWindowTestAccess::document(window)));
  CHECK(scan.marks.size() == 1 && scan.marks.front().text == "Seth2026");

  // A JPEG at quality 90 keeps it.
  patchy::ui::ImageSaveOptions options;
  options.jpeg_quality = 90;
  const auto jpeg = artifact_dir() + QStringLiteral("/marked-q90.jpg");
  QFile::remove(jpeg);
  CHECK(MainWindowTestAccess::save_document_to_path(window, jpeg, options));
  MainWindowTestAccess::open_document_path(window, jpeg);
  QApplication::processEvents();
  const auto reread = patchy::ai::scan_trustmark(patchy::Compositor{}.flatten_rgb8(MainWindowTestAccess::document(window)));
  CHECK(reread.marks.size() == 1);
  if (!reread.marks.empty()) {
    std::printf("  JPEG quality 90: %s, %d bits corrected, confidence %.1f\n", reread.marks.front().text.c_str(),
                reread.marks.front().corrected_bits, reread.marks.front().confidence);
    CHECK(reread.marks.front().text == "Seth2026");
  }
}

// The scripting calls: embed text and an ID, read them back, the option errors, one undo step.
void ui_script_trustmark_round_trip() {
  const WatermarkModelsScope scope(true);
  if (!patchy::ai::trustmark_models_available()) {
    patchy::test::skip("the watermark models are not staged (setup-ai-deps provisions them)");
  }
  const auto quote = [](const QString& path) {
    return QString::fromUtf8(QJsonDocument(QJsonArray{path}).toJson(QJsonDocument::Compact)).chopped(1).mid(1);
  };
  const auto quoted = quote(write_photo(QStringLiteral("script-photo.png")));
  const auto quoted_second = quote(write_photo(QStringLiteral("script-photo-2.png")));
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
if (!app.watermarkModelsAvailable) throw new Error("models");
var d = app.open(%1);
function throws(f) { try { f(); } catch (e) { return true; } return false; }
if (!throws(function () { d.addInvisibleWatermark({text: "TooLongForIt", schema: "BCH_SUPER"}); })) throw new Error("length");
if (!throws(function () { d.addInvisibleWatermark({text: "a", id: "0x1"}); })) throw new Error("both");
if (!throws(function () { d.addInvisibleWatermark({bogus: 1}); })) throw new Error("option");
if (!throws(function () { d.addInvisibleWatermark({schema: "BCH_9"}); })) throw new Error("schema");
var before = d.scanForWatermarks();
if (!before.trustmark.available || before.trustmark.marks.length !== 0) throw new Error("clean " + JSON.stringify(before.trustmark));
var added = d.addInvisibleWatermark({text: "Ann26", schema: "BCH_SUPER", strength: 1.2});
if (added.schema !== "BCH_SUPER" || added.text !== "Ann26") throw new Error(JSON.stringify(added));
var s = d.scanForWatermarks().trustmark.marks;
if (s.length !== 1 || s[0].variant !== "Q" || s[0].schema !== "BCH_SUPER" || s[0].text !== "Ann26") throw new Error(JSON.stringify(s));
if (s[0].bits.length !== 40 || s[0].confidence < 3) throw new Error("bits " + JSON.stringify(s[0]));
var d2 = app.open(%2);
var withId = d2.addInvisibleWatermark({id: "0x1234ABCD"});
if (withId.id.indexOf("0x1234ABCD") !== 0) throw new Error(JSON.stringify(withId));
var t = d2.scanForWatermarks().trustmark.marks;
if (t.length !== 1 || t[0].schema !== "BCH_5" || t[0].id !== withId.id) throw new Error(JSON.stringify(t));
console.log("trustmark-ok");
)JS").arg(quoted, quoted_second)));
  CHECK(backlog_contains(window, QStringLiteral("trustmark-ok")));
}

}  // namespace

std::vector<patchy::test::TestCase> watermark_tests() {
  return {
      {"ui_file_info_trustmark_scan_and_download_states", ui_file_info_trustmark_scan_and_download_states},
      {"ui_invisible_watermark_dialog_adds_a_readable_mark", ui_invisible_watermark_dialog_adds_a_readable_mark},
      {"ui_script_trustmark_round_trip", ui_script_trustmark_round_trip},
  };
}
