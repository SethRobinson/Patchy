// Preferences > Metadata and Watermarks, "Saved metadata" (docs/file-info.md, "Saved metadata
// preference"): the page and its persistence, each mode for PSD and JPEG, when the "add your
// name and copyright" prompt shows and when it does not, exports, and the scripting view.

#include "app_shell_test_support.hpp"
#include "formats/metadata/metadata_containers.hpp"
#include "formats/metadata/metadata_edit.hpp"
#include "formats/metadata/xmp.hpp"
#include "psd/psd_document_io.hpp"
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
#include "ui/document_session.hpp"
#include "ui/image_save_options.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui/save_metadata_settings.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QDate>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QTimer>

#include <algorithm>
#include <string>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::test::ui::app_shell_support::run_preferences_dialog;
using patchy::test::ui::scripting_support::backlog_contains;
using patchy::test::ui::scripting_support::run_script;
using patchy::ui::MainWindowTestAccess;
namespace md = patchy::metadata;

struct SaveMetadataScope {
  SaveMetadataScope() { reset(); }
  ~SaveMetadataScope() { reset(); }
  static void reset() {
    auto settings = patchy::ui::app_settings();
    for (const auto* key :
         {patchy::ui::kSaveMetadataModeSettingKey, patchy::ui::kSaveMetadataNameSettingKey,
          patchy::ui::kSaveMetadataCopyrightSettingKey, patchy::ui::kSaveMetadataEmailSettingKey,
          patchy::ui::kSaveMetadataWebsiteSettingKey, patchy::ui::kSaveMetadataAskSettingKey}) {
      settings.remove(QLatin1String(key));
    }
  }
  SaveMetadataScope(const SaveMetadataScope&) = delete;
  SaveMetadataScope& operator=(const SaveMetadataScope&) = delete;
};

QString artifact_dir() {
  const auto dir = QDir(QStringLiteral("test-artifacts/save-metadata")).absolutePath();
  QDir().mkpath(dir);
  return dir;
}

void new_document(patchy::ui::MainWindow& window, const QString& title) {
  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", patchy::PixelBuffer(64, 48, patchy::PixelFormat::rgb8()));
  window.add_document_session(std::move(document), title);
  QApplication::processEvents();
}

patchy::DocumentFileInfo saved_info(const QString& path) {
  if (path.endsWith(QStringLiteral(".psd"))) {
    return patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(path)).file_info();
  }
  return md::extract_file_info_from_path(patchy::ui::to_filesystem_path(path));
}

std::string creator_tool(const patchy::DocumentFileInfo& info) {
  if (info.xmp == nullptr) {
    return {};
  }
  return md::parse_xmp(*info.xmp).value_or(md::XmpPacket{}).text(md::xmp_ns::kXmp, "CreatorTool");
}

void set_personal(bool ask) {
  patchy::ui::set_stored_save_metadata_mode(patchy::ui::SaveMetadataMode::Personal);
  patchy::ui::set_stored_personal_save_metadata(
      {QStringLiteral("Ann Example"), QStringLiteral("© 2026 Ann Example"), QStringLiteral("ann@example.com"),
       QStringLiteral("https://example.com"), ask});
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

// Saves `path`, answering the personal-metadata prompt with `answer` (Accept/Reject role)
// when it appears; returns whether it appeared.
bool save_answering(patchy::ui::MainWindow& window, const QString& path, QMessageBox::ButtonRole answer,
                    bool always = false, QString* informative = nullptr, bool capture = false) {
  bool prompted = false;
  QTimer::singleShot(0, [&] {
    auto* box = visible_message_box(QStringLiteral("savePersonalMetadataPrompt"));
    if (box == nullptr) {
      return;
    }
    prompted = true;
    if (informative != nullptr) {
      *informative = box->informativeText();
    }
    if (capture) {
      capture_widget_artifact("ui_save_personal_metadata_prompt", *box);
    }
    if (always) {
      if (auto* check = box->findChild<QCheckBox*>(QStringLiteral("savePersonalMetadataAlwaysCheck"))) {
        check->setChecked(true);
      }
    }
    for (auto* button : box->buttons()) {
      if (box->buttonRole(button) == answer) {
        button->click();
        return;
      }
    }
  });
  QFile::remove(path);
  CHECK(MainWindowTestAccess::save_document_to_path(window, path));
  QApplication::processEvents();
  return prompted;
}

void ui_save_metadata_preferences_page_persists() {
  const SaveMetadataScope scope;
  patchy::ui::MainWindow window;
  show_window(window);
  run_preferences_dialog(window, /*accept=*/true, [](QDialog& dialog) {
    auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("preferencesTabWidget"));
    CHECK(tabs != nullptr);
    if (tabs == nullptr) {
      return;
    }
    for (int index = 0; index < tabs->count(); ++index) {
      if (tabs->tabText(index) == QObject::tr("Metadata and Watermarks")) {
        tabs->setCurrentIndex(index);
      }
    }
    QApplication::processEvents();
    auto* basic = dialog.findChild<QRadioButton*>(QStringLiteral("preferencesSavedMetadataBasicRadio"));
    auto* personal = dialog.findChild<QRadioButton*>(QStringLiteral("preferencesSavedMetadataPersonalRadio"));
    auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("preferencesSavedMetadataNameEdit"));
    auto* copyright = dialog.findChild<QLineEdit*>(QStringLiteral("preferencesSavedMetadataCopyrightEdit"));
    auto* suggest = dialog.findChild<QPushButton*>(QStringLiteral("preferencesSavedMetadataSuggestButton"));
    auto* email = dialog.findChild<QLineEdit*>(QStringLiteral("preferencesSavedMetadataEmailEdit"));
    auto* ask = dialog.findChild<QCheckBox*>(QStringLiteral("preferencesSavedMetadataAskCheck"));
    CHECK(basic != nullptr && personal != nullptr && name != nullptr && copyright != nullptr && suggest != nullptr &&
          email != nullptr && ask != nullptr);
    if (basic == nullptr || personal == nullptr || name == nullptr || copyright == nullptr || suggest == nullptr ||
        email == nullptr || ask == nullptr) {
      return;
    }
    CHECK(basic->isChecked());  // the default
    CHECK(!name->isEnabled());  // the fields wait for "Basic plus my name and copyright"
    personal->setChecked(true);
    CHECK(name->isEnabled());
    name->setText(QStringLiteral("Ann Example"));
    CHECK(suggest->isEnabled());
    suggest->click();
    CHECK(copyright->text() == QStringLiteral("© %1 Ann Example").arg(QDate::currentDate().year()));
    email->setText(QStringLiteral("ann@example.com"));
    CHECK(ask->isChecked());
    dialog.resize(dialog.width(), std::max(dialog.height(), 860));
    QApplication::processEvents();
    capture_widget_artifact("ui_preferences_metadata_and_watermarks", dialog);
  });
  CHECK(patchy::ui::stored_save_metadata_mode() == patchy::ui::SaveMetadataMode::Personal);
  const auto stored = patchy::ui::stored_personal_save_metadata();
  CHECK(stored.name == QStringLiteral("Ann Example"));
  CHECK(stored.email == QStringLiteral("ann@example.com"));
  CHECK(stored.ask);
  CHECK(run_script(window, QStringLiteral(R"JS(
var p = app.saveMetadataPreferences;
if (p.mode !== "personal" || p.name !== "Ann Example" || p.email !== "ann@example.com" || p.askBeforeAdding !== true)
  throw new Error(JSON.stringify(p));
function throws(f) { try { f(); } catch (e) { return true; } return false; }
if (!throws(function () { app.setSaveMetadataPreferences({mode: "lots"}); })) throw new Error("mode");
if (!throws(function () { app.setSaveMetadataPreferences({bogus: 1}); })) throw new Error("key");
app.setSaveMetadataPreferences({mode: "none", website: "https://example.com", askBeforeAdding: false});
var q = app.saveMetadataPreferences;
if (q.mode !== "none" || q.website !== "https://example.com" || q.askBeforeAdding !== false || q.name !== "Ann Example")
  throw new Error(JSON.stringify(q));
console.log("save-metadata-prefs-ok");
)JS")));
  CHECK(backlog_contains(window, QStringLiteral("save-metadata-prefs-ok")));
}

// None, Basic and Personal (without asking) for PSD and JPEG, and exports.
void ui_save_metadata_modes_for_psd_and_jpeg() {
  const SaveMetadataScope scope;
  patchy::ui::MainWindow window;
  show_window(window);
  const auto dir = artifact_dir();

  patchy::ui::set_stored_save_metadata_mode(patchy::ui::SaveMetadataMode::None);
  new_document(window, QStringLiteral("None"));
  CHECK(MainWindowTestAccess::save_document_to_path(window, dir + QStringLiteral("/none.psd")));
  CHECK(saved_info(dir + QStringLiteral("/none.psd")).xmp == nullptr);  // no save stamp at all
  CHECK(MainWindowTestAccess::save_document_to_path(window, dir + QStringLiteral("/none.jpg")));
  CHECK(saved_info(dir + QStringLiteral("/none.jpg")).xmp == nullptr);

  patchy::ui::set_stored_save_metadata_mode(patchy::ui::SaveMetadataMode::Basic);
  new_document(window, QStringLiteral("Basic"));
  CHECK(MainWindowTestAccess::save_document_to_path(window, dir + QStringLiteral("/basic.psd")));
  CHECK(creator_tool(saved_info(dir + QStringLiteral("/basic.psd"))).rfind("Patchy ", 0) == 0U);
  CHECK(md::read_fields(saved_info(dir + QStringLiteral("/basic.psd"))).authors.empty());
  // Basic stamps PSD and PSB only (docs/file-info.md): a JPEG of a document without metadata
  // stays exactly as before. (A document saved as PSD first carries that stamp onward.)
  new_document(window, QStringLiteral("Basic JPEG"));
  CHECK(MainWindowTestAccess::save_document_to_path(window, dir + QStringLiteral("/basic.jpg")));
  CHECK(saved_info(dir + QStringLiteral("/basic.jpg")).xmp == nullptr);

  set_personal(/*ask=*/false);
  new_document(window, QStringLiteral("Personal"));
  CHECK(!save_answering(window, dir + QStringLiteral("/personal.jpg"), QMessageBox::AcceptRole));
  const auto jpeg_fields = md::read_fields(saved_info(dir + QStringLiteral("/personal.jpg")));
  CHECK(jpeg_fields.authors == std::vector<std::string>{"Ann Example"});
  CHECK(jpeg_fields.copyright_notice == "© 2026 Ann Example");
  CHECK(jpeg_fields.copyright_status == md::CopyrightStatus::Copyrighted);
  CHECK(jpeg_fields.contact_email == "ann@example.com" && jpeg_fields.contact_url == "https://example.com");
  CHECK(MainWindowTestAccess::save_document_to_path(window, dir + QStringLiteral("/personal.psd")));
  const auto psd_info = saved_info(dir + QStringLiteral("/personal.psd"));
  CHECK(md::read_fields(psd_info).authors == std::vector<std::string>{"Ann Example"});
  CHECK(creator_tool(psd_info).rfind("Patchy ", 0) == 0U);

  // Export "None" still writes nothing, and an export never adds personal info.
  new_document(window, QStringLiteral("Export"));
  patchy::ui::ImageSaveOptions none;
  none.metadata_policy = md::MetadataPolicy::None;
  const auto exported = dir + QStringLiteral("/export-none.jpg");
  QFile::remove(exported);
  CHECK(MainWindowTestAccess::export_document_to_path(window, exported, none));
  CHECK(saved_info(exported).xmp == nullptr);
  CHECK(md::read_fields(MainWindowTestAccess::document(window).file_info()).authors.empty());
}

// The prompt: shown and answered Add (with "Always"), not shown once the name is there, Skip
// remembered for the document, a format that cannot carry metadata, an existing creator.
void ui_save_metadata_prompt_cases() {
  const SaveMetadataScope scope;
  patchy::ui::MainWindow window;
  show_window(window);
  const auto dir = artifact_dir();
  set_personal(/*ask=*/true);

  // Add.
  new_document(window, QStringLiteral("Ask"));
  CHECK(save_answering(window, dir + QStringLiteral("/ask-add.png"), QMessageBox::AcceptRole));
  CHECK(md::read_fields(saved_info(dir + QStringLiteral("/ask-add.png"))).authors ==
        std::vector<std::string>{"Ann Example"});
  // The name is there now: no prompt on the next save.
  CHECK(!save_answering(window, dir + QStringLiteral("/ask-again.png"), QMessageBox::AcceptRole));

  // Skip, remembered for this document only.
  new_document(window, QStringLiteral("Skip"));
  CHECK(save_answering(window, dir + QStringLiteral("/skip.png"), QMessageBox::RejectRole));
  CHECK(md::read_fields(saved_info(dir + QStringLiteral("/skip.png"))).authors.empty());
  CHECK(!save_answering(window, dir + QStringLiteral("/skip-again.png"), QMessageBox::AcceptRole));
  CHECK(md::read_fields(saved_info(dir + QStringLiteral("/skip-again.png"))).authors.empty());

  // A format that cannot carry metadata never asks.
  new_document(window, QStringLiteral("Bitmap"));
  CHECK(!save_answering(window, dir + QStringLiteral("/plain.bmp"), QMessageBox::AcceptRole));
  CHECK(md::read_fields(MainWindowTestAccess::document(window).file_info()).authors.empty());

  // Another creator already there: the prompt names it, and Add keeps it beside the user's.
  new_document(window, QStringLiteral("Other"));
  {
    auto& info = MainWindowTestAccess::document(window).file_info();
    auto after = md::read_fields(info);
    const auto before = after;
    after.authors = {"Bob Other"};
    md::apply_fields(info, before, after);
  }
  QString informative;
  CHECK(save_answering(window, dir + QStringLiteral("/other.psd"), QMessageBox::AcceptRole, /*always=*/true,
                       &informative, /*capture=*/true));
  CHECK(informative.contains(QStringLiteral("Bob Other")));
  CHECK(md::read_fields(saved_info(dir + QStringLiteral("/other.psd"))).authors ==
        (std::vector<std::string>{"Bob Other", "Ann Example"}));
  // "Always add without asking" turned the question off.
  CHECK(!patchy::ui::stored_personal_save_metadata().ask);
  new_document(window, QStringLiteral("Silent"));
  CHECK(!save_answering(window, dir + QStringLiteral("/silent.png"), QMessageBox::AcceptRole));
  CHECK(md::read_fields(saved_info(dir + QStringLiteral("/silent.png"))).authors ==
        std::vector<std::string>{"Ann Example"});
}

}  // namespace

std::vector<patchy::test::TestCase> save_metadata_tests() {
  return {
      {"ui_save_metadata_preferences_page_persists", ui_save_metadata_preferences_page_persists},
      {"ui_save_metadata_modes_for_psd_and_jpeg", ui_save_metadata_modes_for_psd_and_jpeg},
      {"ui_save_metadata_prompt_cases", ui_save_metadata_prompt_cases},
  };
}
