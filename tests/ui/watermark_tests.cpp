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
#include "ui/watermark_scan_report.hpp"
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

#include <QStringList>

#include <filesystem>
#include <initializer_list>
#include <string>
#include <utility>

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

// The visible results row of one kind: {status token, detail text}.
std::pair<QString, QString> check_row(QDialog& dialog, const QString& id) {
  std::pair<QString, QString> row;
  for (auto* label : dialog.findChildren<QLabel*>()) {
    if (!label->isVisible() || label->property("watermarkCheck").toString() != id) {
      continue;
    }
    if (label->objectName() == QStringLiteral("fileInfoWatermarkCheckStatus")) {
      row.first = label->property("watermarkStatus").toString();
    } else if (label->objectName() == QStringLiteral("fileInfoWatermarkCheckDetail")) {
      row.second = label->text();
    }
  }
  return row;
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
      CHECK(result != nullptr && !result->text().contains(QStringLiteral("Adobe TrustMark")));
      for (const auto* id : {"trustmarkQ", "trustmarkP", "trustmarkC"}) {
        const auto row = check_row(*dialog, QString::fromLatin1(id));
        CHECK(row.first == QStringLiteral("notChecked"));
        if (patchy::ai::trustmark_runtime_available()) {
          CHECK(row.second == QCoreApplication::translate("WatermarkScanReport",
                                                          "The watermark models are not downloaded."));
        }
      }
      CHECK(check_row(*dialog, QStringLiteral("stableDiffusionXL")).first == QStringLiteral("notFound"));
      if (patchy::ai::trustmark_runtime_available()) {
        CHECK(visible_button(*dialog, QStringLiteral("fileInfoDownloadWatermarkModelsButton")) != nullptr);
      }
      dialog->resize(dialog->width(), std::max(dialog->height(), 640));
      QApplication::processEvents();
      capture_widget_artifact("ui_file_info_watermark_results_no_models", *dialog);
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
    CHECK(result != nullptr && result->text().contains(QStringLiteral("Adobe TrustMark Q")));
    const auto q = check_row(*dialog, QStringLiteral("trustmarkQ"));
    CHECK(q.first == QStringLiteral("found") && q.second.contains(QStringLiteral("Patchy26")) &&
          q.second.contains(QStringLiteral("BCH_5")));
    CHECK(check_row(*dialog, QStringLiteral("trustmarkP")).first == QStringLiteral("notFound"));
    CHECK(check_row(*dialog, QStringLiteral("trustmarkC")).first == QStringLiteral("notFound"));
    CHECK(check_row(*dialog, QStringLiteral("stableDiffusion1")).first == QStringLiteral("notFound"));
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

// The results rows (docs/watermarks.md, "Scan results") from synthetic scans: found, not
// found and each not-checked reason, the fixed order, and the C2PA declaration row.
void ui_watermark_scan_report_rows() {
  using patchy::ui::WatermarkCheckStatus;
  const auto ids = [](const std::vector<patchy::ui::WatermarkCheck>& checks) {
    QStringList list;
    for (const auto& check : checks) {
      list.push_back(check.id);
    }
    return list;
  };
  const QStringList order{QStringLiteral("stableDiffusion1"), QStringLiteral("stableDiffusion2"),
                          QStringLiteral("stableDiffusionXL"), QStringLiteral("trustmarkQ"),
                          QStringLiteral("trustmarkP"), QStringLiteral("trustmarkC"),
                          QStringLiteral("c2paDeclared")};

  patchy::InvisibleWatermarkScan sd;
  sd.scanned = true;
  sd.matches.push_back({"stableDiffusionXL", "Stable Diffusion XL", 48, 2, false});
  patchy::ai::TrustMarkScan tm;
  tm.ran = true;
  patchy::ai::TrustMarkMark mark;
  mark.variant = patchy::ai::TrustMarkVariant::P;
  mark.text = "Hi";
  mark.hex = "0x1";
  mark.confidence = 9.84F;
  tm.marks.push_back(mark);
  patchy::FileInfoObserved declared;
  declared.label = "C2PA manifest";
  declared.summary = "soft binding com.adobe.trustmark.P";
  declared.privacy = patchy::MetadataPrivacy::Watermark;
  patchy::FileInfoObserved other;
  other.summary = "a thumbnail";
  const auto found = patchy::ui::watermark_checks(sd, tm, {}, {declared, other});
  CHECK(ids(found) == order);
  CHECK(found[0].status == WatermarkCheckStatus::NotFound && found[0].reason.isEmpty());
  CHECK(found[2].status == WatermarkCheckStatus::Found && found[2].match.has_value());
  CHECK(found[2].detail == QCoreApplication::translate("WatermarkScanReport", "%1 of %2 bits match.").arg(46).arg(48));
  CHECK(found[2].name == QStringLiteral("Stable Diffusion XL"));
  CHECK(found[3].status == WatermarkCheckStatus::NotFound && found[3].name == QStringLiteral("Adobe TrustMark Q"));
  CHECK(found[4].status == WatermarkCheckStatus::Found && found[4].mark.has_value());
  CHECK(found[4].detail.contains(QStringLiteral("“Hi”")) && found[4].detail.contains(QStringLiteral("9.8")));
  CHECK(found[6].status == WatermarkCheckStatus::Found && found[6].declarations.size() == 1);
  CHECK(found[6].detail == QStringLiteral("soft binding com.adobe.trustmark.P"));
  CHECK(patchy::ui::watermark_check_status_token(found[4].status) == QStringLiteral("found"));
  CHECK(patchy::ui::watermark_check_status_text(WatermarkCheckStatus::NotChecked) ==
        QCoreApplication::translate("WatermarkScanReport", "Not checked"));

  // Too small for the Stable Diffusion marks; TrustMark failed.
  const auto too_small = patchy::ui::watermark_checks(patchy::InvisibleWatermarkScan{}, std::nullopt, "out of memory", {});
  CHECK(ids(too_small) == order);
  for (int i = 0; i < 3; ++i) {
    CHECK(too_small[static_cast<std::size_t>(i)].status == WatermarkCheckStatus::NotChecked &&
          too_small[static_cast<std::size_t>(i)].reason == QStringLiteral("imageTooSmall"));
  }
  for (int i = 3; i < 6; ++i) {
    CHECK(too_small[static_cast<std::size_t>(i)].reason == QStringLiteral("failed") &&
          too_small[static_cast<std::size_t>(i)].detail.contains(QStringLiteral("out of memory")));
  }
  CHECK(too_small[6].status == WatermarkCheckStatus::NotFound);

  // TrustMark not run and no error: the models are missing (or this build cannot run them).
  {
    const WatermarkModelsScope scope(false);
    const auto missing = patchy::ui::watermark_checks(sd, std::nullopt, {}, {});
    const auto expected = patchy::ai::trustmark_runtime_available() ? QStringLiteral("modelsNotDownloaded")
                                                                    : QStringLiteral("notSupported");
    for (int i = 3; i < 6; ++i) {
      CHECK(missing[static_cast<std::size_t>(i)].status == WatermarkCheckStatus::NotChecked &&
            missing[static_cast<std::size_t>(i)].reason == expected);
    }
  }
  CHECK(patchy::ui::watermark_scan_limits_note().contains(QStringLiteral("SynthID")) &&
        patchy::ui::watermark_scan_limits_note().contains(QStringLiteral("Digimarc")));
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
if (before.checks.length !== 7 || before.checks.some(function (c) { return c.status !== "notFound"; })) throw new Error("checks " + JSON.stringify(before.checks));
var added = d.addInvisibleWatermark({text: "Ann26", schema: "BCH_SUPER", strength: 1.2});
if (added.schema !== "BCH_SUPER" || added.text !== "Ann26") throw new Error(JSON.stringify(added));
var s = d.scanForWatermarks().trustmark.marks;
if (s.length !== 1 || s[0].variant !== "Q" || s[0].schema !== "BCH_SUPER" || s[0].text !== "Ann26") throw new Error(JSON.stringify(s));
if (s[0].bits.length !== 40 || s[0].confidence < 3) throw new Error("bits " + JSON.stringify(s[0]));
var q = d.scanForWatermarks().checks[3];
if (q.id !== "trustmarkQ" || q.status !== "found" || q.mark.text !== "Ann26" || q.detail.indexOf("Ann26") < 0) throw new Error("q " + JSON.stringify(q));
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
      {"ui_watermark_scan_report_rows", ui_watermark_scan_report_rows},
  };
}
