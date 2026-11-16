// Import PDF's "Recognize text on scanned pages (OCR)" (docs/ocr.md, "Import PDF"): the
// dialog's controls, which pages are read, what the three results make of a page, the
// merge targets, Cancel, and that nothing recognizes without the option. The PDFs are built
// here from pages these tests draw; the scripted fake engine reports the drawn lines.

#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/pixel_depth.hpp"
#include "import_print_resolution_test_support.hpp"
#include "ocr/ocr_fake_engine.hpp"
#include "ocr_page_fixture.hpp"
#include "ocr_test_support.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/pdf_export.hpp"
#include "ui/pdf_import.hpp"
#include "ui/script_engine.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QProgressDialog>
#include <QPushButton>
#include <QTimer>

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

#if defined(PATCHY_HAVE_QT_PDF)

using patchy::ocr::FakeEngineConfig;
using patchy::test::ui::find_top_level_dialog;
using patchy::test::ui::import_print_resolution_support::pdf_from_objects;
using patchy::test::ui::ocr_test_font;
using patchy::test::ui::OcrTestPage;
using patchy::test::ui::recognized_text_group;
using patchy::test::ui::ScopedOcrEngines;
using patchy::test::ui::SettingsGroupRestorer;
using patchy::test::ui::SettingsValueRestorer;
using patchy::test::ui::show_window;
using patchy::test::ui::when_dialog_visible;
using patchy::ui::MainWindow;
using patchy::ui::MainWindowTestAccess;

// The pages are 600 x 400 px and 288 x 192 pt: one pixel per pixel at 150 ppi.
constexpr int kPageWidth = 600;
constexpr int kPageHeight = 400;
constexpr int kImportPpi = 150;

std::string jpeg_of(const QImage& page) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  QImageWriter writer(&buffer, "JPEG");
  writer.setQuality(92);
  CHECK(writer.write(page.convertToFormat(QImage::Format_RGB888)));
  return bytes.toStdString();
}

// One entry per page: a scan (its JPEG) or, when empty, a page of real text ("Hello PDF").
QByteArray pdf_of(const std::vector<std::string>& page_jpegs) {
  std::vector<std::string> objects;
  objects.emplace_back("<</Type/Catalog/Pages 2 0 R>>");
  std::string kids;
  for (std::size_t index = 0; index < page_jpegs.size(); ++index) {
    kids += std::to_string(3 + index * 3) + " 0 R ";
  }
  objects.push_back("<</Type/Pages/Kids[" + kids + "]/Count " + std::to_string(page_jpegs.size()) + ">>");
  for (std::size_t index = 0; index < page_jpegs.size(); ++index) {
    const auto first = 3 + index * 3;
    const auto& jpeg = page_jpegs[index];
    const bool scan = !jpeg.empty();
    const std::string content = scan ? "q 288 0 0 192 0 0 cm /Im0 Do Q"
                                     : "BT /F1 18 Tf 1 0 0 1 20 100 Tm 0 0 0 rg (Hello PDF) Tj ET";
    const std::string resources = scan ? "/XObject<</Im0 " + std::to_string(first + 2) + " 0 R>>"
                                       : "/Font<</F1 " + std::to_string(first + 2) + " 0 R>>";
    objects.push_back("<</Type/Page/Parent 2 0 R/MediaBox[0 0 288 192]/Resources<<" + resources + ">>/Contents " +
                      std::to_string(first + 1) + " 0 R>>");
    objects.push_back("<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream");
    if (scan) {
      objects.push_back("<</Type/XObject/Subtype/Image/Width " + std::to_string(kPageWidth) + "/Height " +
                        std::to_string(kPageHeight) +
                        "/ColorSpace/DeviceRGB/BitsPerComponent 8/Filter/DCTDecode/Length " +
                        std::to_string(jpeg.size()) + ">>\nstream\n" + jpeg + "\nendstream");
    } else {
      objects.emplace_back("<</Type/Font/Subtype/Type1/BaseFont/Helvetica/Encoding/WinAnsiEncoding"
                           "/FirstChar 32/LastChar 122/Widths[500]>>");
    }
  }
  return pdf_from_objects(objects);
}

QString write_pdf(const char* name, const QByteArray& bytes) {
  QDir().mkpath(QStringLiteral("test-artifacts"));
  const auto path = QStringLiteral("test-artifacts/%1.pdf").arg(QLatin1String(name));
  QFile::remove(path);
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly));
  file.write(bytes);
  return path;
}

// Puts back every setting these tests write. Never the whole imports group: the suite pins
// keys in it at startup (imports/showMissingFontsDialog off, for one), and clearing those
// brings up dialogs nobody answers.
struct ImportSettings {
  SettingsGroupRestorer ocr{QStringLiteral("ocr")};
  SettingsValueRestorer pdf_ocr{QStringLiteral("imports/pdfOcr")};
  SettingsValueRestorer pdf_ocr_result{QStringLiteral("imports/pdfOcrResult")};
  SettingsValueRestorer mode{QStringLiteral("imports/pdfMode")};
  SettingsValueRestorer target{QStringLiteral("imports/pdfPagesTarget")};
  SettingsValueRestorer resolution{QStringLiteral("imports/pdfResolution")};
  SettingsValueRestorer color_mode{QStringLiteral("imports/pdfColorMode")};
  SettingsValueRestorer bit_depth{QStringLiteral("imports/pdfBitDepth")};

  ImportSettings() {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("imports/pdfOcr"));
    settings.remove(QStringLiteral("imports/pdfOcrResult"));
    settings.sync();
  }
};

// What a test picks in the Import PDF dialog.
struct ImportPick {
  QString mode{QStringLiteral("editable")};
  QString target{QStringLiteral("documents")};
  bool ocr{true};
  QString color_mode{QStringLiteral("rgb")};
  int bits{8};
};

void pick_combo(QDialog& dialog, const char* name, const QVariant& data) {
  auto* combo = dialog.findChild<QComboBox*>(QLatin1String(name));
  CHECK(combo != nullptr);
  if (combo != nullptr) {
    const int row = combo->findData(data);
    CHECK(row >= 0);
    combo->setCurrentIndex(row);
  }
}

// Arms a driver that fills in the Import PDF dialog and presses Import.
void drive_import(ImportPick pick, std::shared_ptr<bool> drove) {
  when_dialog_visible(QStringLiteral("pdfImportDialog"), [pick = std::move(pick), drove](QDialog& dialog) {
    pick_combo(dialog, "pdfImportModeCombo", pick.mode);
    pick_combo(dialog, "pdfImportPagesTargetCombo", pick.target);
    pick_combo(dialog, "pdfImportColorModeCombo", pick.color_mode);
    pick_combo(dialog, "pdfImportBitDepthCombo", pick.bits);
    auto* resolution = dialog.findChild<QComboBox*>(QStringLiteral("pdfImportResolutionCombo"));
    auto* pages = dialog.findChild<QListWidget*>(QStringLiteral("pdfImportPagesList"));
    auto* ocr = dialog.findChild<QCheckBox*>(QStringLiteral("pdfImportOcrCheck"));
    auto* import_button = dialog.findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
    CHECK(resolution != nullptr && pages != nullptr && ocr != nullptr && import_button != nullptr);
    if (resolution == nullptr || pages == nullptr || ocr == nullptr || import_button == nullptr) {
      dialog.reject();
      return;
    }
    resolution->setCurrentText(QString::number(kImportPpi));
    pages->selectAll();
    CHECK(ocr->isEnabled() || !pick.ocr);
    ocr->setChecked(pick.ocr);
    QApplication::processEvents();
    import_button->click();
    *drove = true;
  });
}

// Everything a test needs around one import: the engine, the settings it may write, and the
// window. `delay_ms` slows the engine (per page) for the Cancel test.
struct ImportFixture {
  ImportSettings restore_settings;
  OcrTestPage page{kPageWidth, kPageHeight};
  QRect first;
  QRect second;
  std::shared_ptr<patchy::ocr::FakeEngine> fake;
  std::unique_ptr<ScopedOcrEngines> engines;
  MainWindow window;
  std::size_t sessions_before{0};

  explicit ImportFixture(int delay_ms = 0) {
    first = page.draw(QStringLiteral("Scanned page heading"), 40, 40, ocr_test_font(40));
    second = page.draw(QStringLiteral("A line further down"), 40, 250, ocr_test_font(40));
    FakeEngineConfig config;
    config.lines = page.lines;
    config.delay_ms = delay_ms;
    fake = patchy::ocr::make_fake_engine(std::move(config));
    engines = std::make_unique<ScopedOcrEngines>(std::vector<std::shared_ptr<patchy::ocr::Engine>>{fake});
    show_window(window);
    sessions_before = MainWindowTestAccess::session_count(window);
  }

  [[nodiscard]] std::string scan() const { return jpeg_of(page.image); }

  void set_result(const char* token) {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/pdfOcrResult"), QLatin1String(token));
    settings.sync();
  }

  // Opens `path` through the dialog with `pick`. True when the dialog was driven.
  bool import(const QString& path, ImportPick pick = {}) {
    auto drove = std::make_shared<bool>(false);
    drive_import(std::move(pick), drove);
    MainWindowTestAccess::open_document_path(window, path);
    for (int turn = 0; turn < 10; ++turn) {
      QApplication::processEvents();
    }
    CHECK(*drove);
    return *drove;
  }

  [[nodiscard]] std::size_t opened() { return MainWindowTestAccess::session_count(window) - sessions_before; }
  [[nodiscard]] const patchy::Document& document(std::size_t index) {
    return std::as_const(MainWindowTestAccess::session_document(window, sessions_before + index));
  }

  // The active document's import notes, one per line.
  [[nodiscard]] QString notes() {
    auto& host = window.script_engine_host();
    patchy::ui::ScriptEngineHost::RunOptions options;
    options.name = QStringLiteral("pdf-ocr-notes");
    options.unattended = true;
    CHECK(host.run_source(QStringLiteral("patchy.setResult({notes: app.activeDocument.importNotes.join('\\n')});"),
                          std::move(options)));
    CHECK(patchy::test::ui::process_events_until([&host] { return !host.run_active(); }, 30000));
    return host.last_result().toObject().value(QStringLiteral("notes")).toString();
  }
};

int text_children(const patchy::Layer& group) {
  int count = 0;
  for (const auto& child : group.children()) {
    count += patchy::layer_is_text(child) ? 1 : 0;
  }
  return count;
}

const patchy::Layer* find_recognized_group(const std::vector<patchy::Layer>& layers) {
  for (const auto& layer : layers) {
    if (patchy::layer_is_recognized_text_group(layer)) {
      return &layer;
    }
    if (const auto* inner = find_recognized_group(layer.children()); inner != nullptr) {
      return inner;
    }
  }
  return nullptr;
}

void ui_pdf_import_ocr_controls_and_settings() {
  const ImportSettings settings;
  OcrTestPage page(kPageWidth, kPageHeight);
  page.draw(QStringLiteral("Controls"), 40, 40, ocr_test_font(40));
  const auto path = write_pdf("ui_pdf_import_ocr_controls", pdf_of({jpeg_of(page.image)}));

  // No engine (the suite's default): the option is off and says why; Options still opens.
  bool saw = false;
  when_dialog_visible(QStringLiteral("pdfImportDialog"), [&saw](QDialog& dialog) {
    auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("pdfImportOcrCheck"));
    auto* options = dialog.findChild<QPushButton*>(QStringLiteral("pdfImportOcrOptionsButton"));
    auto* summary = dialog.findChild<QLabel*>(QStringLiteral("pdfImportOcrSummaryLabel"));
    CHECK(check != nullptr && options != nullptr && summary != nullptr);
    if (check != nullptr && options != nullptr && summary != nullptr) {
      CHECK(!check->isEnabled() && !check->isChecked());
      CHECK(options->isEnabled());
      CHECK(summary->isVisibleTo(&dialog));
      CHECK(summary->text().contains(QStringLiteral("No text recognition engine")));
      saw = true;
    }
    dialog.reject();
  });
  CHECK(!patchy::ui::run_pdf_import_dialog(nullptr, path).has_value());
  CHECK(saw);

  // With an engine: off until ticked, then a line saying what will run and what it makes.
  const ScopedOcrEngines engines({patchy::test::ui::ocr_fake_for(page)});
  saw = false;
  when_dialog_visible(QStringLiteral("pdfImportDialog"), [&saw](QDialog& dialog) {
    auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("pdfImportOcrCheck"));
    auto* options = dialog.findChild<QPushButton*>(QStringLiteral("pdfImportOcrOptionsButton"));
    auto* summary = dialog.findChild<QLabel*>(QStringLiteral("pdfImportOcrSummaryLabel"));
    if (check == nullptr || options == nullptr || summary == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(check->isEnabled() && !check->isChecked());
    CHECK(!summary->isVisibleTo(&dialog));
    check->setChecked(true);
    CHECK(summary->isVisibleTo(&dialog));
    CHECK(summary->text() ==
          QStringLiteral("Scripted test engine, English (United States): Hidden text layers, page unchanged"));
    // Options: no Read row, the three results with hidden first, and the pick comes back
    // in the summary line.
    when_dialog_visible(QStringLiteral("pdfImportOcrOptionsDialog"), [](QDialog& inner) {
      CHECK(inner.findChild<QComboBox*>(QStringLiteral("recognizeTextSourceCombo")) == nullptr);
      auto* result = inner.findChild<QComboBox*>(QStringLiteral("recognizeTextResultCombo"));
      auto* language = inner.findChild<QComboBox*>(QStringLiteral("recognizeTextLanguageCombo"));
      CHECK(result != nullptr && language != nullptr);
      if (result != nullptr && language != nullptr) {
        CHECK(result->count() == 3);
        CHECK(result->itemData(0).toString() == QStringLiteral("hidden"));
        CHECK(result->currentData().toString() == QStringLiteral("hidden"));
        result->setCurrentIndex(result->findData(QStringLiteral("visible")));
        language->setCurrentIndex(language->findData(QStringLiteral("ja")));
      }
      inner.accept();
    });
    options->click();
    CHECK(summary->text() == QStringLiteral("Scripted test engine, Japanese: Text layers over the scan"));
    saw = true;
    dialog.reject();
  });
  CHECK(!patchy::ui::run_pdf_import_dialog(nullptr, path).has_value());
  CHECK(saw);
  auto stored = patchy::ui::app_settings();
  // The options were accepted, the import was not: the result pick is stored, the tick is not.
  CHECK(stored.value(QStringLiteral("imports/pdfOcrResult")).toString() == QStringLiteral("visible"));
  CHECK(stored.value(QStringLiteral("ocr/language")).toString() == QStringLiteral("ja"));
  CHECK(!stored.contains(QStringLiteral("imports/pdfOcr")));

  // An accepted import stores the tick and hands the choice on; the dialog recognizes nothing.
  auto drove = std::make_shared<bool>(false);
  drive_import(ImportPick{}, drove);
  const auto result = patchy::ui::run_pdf_import_dialog(nullptr, path);
  CHECK(*drove && result.has_value());
  if (result.has_value()) {
    CHECK(result->ocr.has_value());
    if (result->ocr.has_value()) {
      CHECK(result->ocr->mode == patchy::ui::OcrLayerMode::Visible);
      CHECK(result->ocr->engine.isEmpty());  // resolved from the stored picks when it runs
    }
    CHECK(find_recognized_group(result->document.layers()) == nullptr);
  }
  stored.sync();
  CHECK(stored.value(QStringLiteral("imports/pdfOcr")).toBool());
  // The next dialog starts ticked; unticking and importing stores that and sets no choice.
  saw = false;
  when_dialog_visible(QStringLiteral("pdfImportDialog"), [&saw](QDialog& dialog) {
    auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("pdfImportOcrCheck"));
    auto* import_button = dialog.findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
    if (check == nullptr || import_button == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(check->isChecked());
    check->setChecked(false);
    saw = true;
    import_button->click();
  });
  const auto plain = patchy::ui::run_pdf_import_dialog(nullptr, path);
  CHECK(saw && plain.has_value());
  CHECK(plain.has_value() && !plain->ocr.has_value());
  stored.sync();
  CHECK(!stored.value(QStringLiteral("imports/pdfOcr"), true).toBool());
}

void ui_pdf_import_ocr_hidden_text_keeps_pass_through() {
  ImportFixture fixture;
  const auto path = write_pdf("ui_pdf_import_ocr_hidden", pdf_of({fixture.scan(), fixture.scan()}));
  if (!fixture.import(path)) {
    return;
  }
  CHECK(fixture.opened() == 2);
  if (fixture.opened() != 2) {
    return;
  }
  // Both pages were read whole, at the import resolution.
  const auto calls = fixture.fake->calls();
  CHECK(calls.size() == 2);
  for (const auto& call : calls) {
    CHECK(call.image_width == kPageWidth && call.image_height == kPageHeight);
  }
  for (std::size_t index = 0; index < 2; ++index) {
    const auto& document = fixture.document(index);
    const auto* group = recognized_text_group(document);
    CHECK(group != nullptr);
    if (group == nullptr) {
      continue;
    }
    // On top of the page, hidden, with its text; the page itself is what it was: the scan's
    // original image data still stands for the document.
    CHECK(document.layers().back().id() == group->id());
    CHECK(!group->visible());
    CHECK(text_children(*group) == 2);
    CHECK(document.active_layer_id() != group->id());
    CHECK(document.metadata().pdf_source_page != nullptr);
    CHECK(patchy::ui::document_matches_pdf_source(document, patchy::ui::flat_export_qimage(document, true)));
  }
  const auto record = MainWindowTestAccess::open_progress_record(fixture.window);
  CHECK(record.steps.contains(QStringLiteral("Recognizing text on page 1 of 2")));
  CHECK(record.steps.contains(QStringLiteral("Recognizing text on page 2 of 2")));
  const auto notes = fixture.notes();
  CHECK(notes.contains(QStringLiteral("Text was recognized on 2 page")));
  CHECK(notes.contains(QStringLiteral("hidden Recognized Text group")));
}

void ui_pdf_import_ocr_visible_text_reencodes_page() {
  ImportFixture fixture;
  fixture.set_result("visible");
  const auto path = write_pdf("ui_pdf_import_ocr_visible", pdf_of({fixture.scan()}));
  if (!fixture.import(path) || fixture.opened() != 1) {
    CHECK(fixture.opened() == 1);
    return;
  }
  const auto& document = fixture.document(0);
  const auto* group = recognized_text_group(document);
  CHECK(group != nullptr);
  if (group == nullptr) {
    return;
  }
  CHECK(group->visible());
  CHECK(text_children(*group) == 2);
  // The text sits on the drawn lines, and the page is no longer its one original image.
  const auto& top = group->children().back();
  const QRect top_rect(top.bounds().x, top.bounds().y, top.bounds().width, top.bounds().height);
  CHECK(top_rect.intersects(fixture.first));
  CHECK(std::abs(top_rect.center().y() - fixture.first.center().y()) <= 12);
  CHECK(document.metadata().pdf_source_page == nullptr);
  CHECK(fixture.notes().contains(QStringLiteral("text layers over each scan")));
}

void ui_pdf_import_ocr_replace_hides_scan_on_white() {
  ImportFixture fixture;
  fixture.set_result("replace");
  const auto path = write_pdf("ui_pdf_import_ocr_replace", pdf_of({fixture.scan()}));
  ImportPick pick;
  pick.mode = QStringLiteral("flatten");
  if (!fixture.import(path, pick) || fixture.opened() != 1) {
    CHECK(fixture.opened() == 1);
    return;
  }
  const auto& document = fixture.document(0);
  const auto* group = recognized_text_group(document);
  CHECK(group != nullptr);
  if (group == nullptr) {
    return;
  }
  // Every other layer of the page is hidden, none is removed or changed; the text reads on
  // the group's white paper.
  CHECK(group->visible());
  CHECK(document.layers().size() >= 2);
  for (const auto& layer : document.layers()) {
    CHECK(layer.id() == group->id() || !layer.visible());
  }
  CHECK(!group->children().empty() && group->children().front().name() == "Paper");
  CHECK(text_children(*group) == 2);
  const QImage shown = patchy::ui::qimage_from_document(document, true);
  CHECK(shown.pixelColor(3, 3) == QColor(Qt::white));
  CHECK(fixture.notes().contains(QStringLiteral("Each scan is hidden")));
}

void ui_pdf_import_ocr_groups_land_in_folders_and_artboards() {
  for (const char* target : {"layers", "artboards"}) {
    ImportFixture fixture;
    const auto path = write_pdf("ui_pdf_import_ocr_merge", pdf_of({fixture.scan(), fixture.scan()}));
    ImportPick pick;
    pick.target = QLatin1String(target);
    if (!fixture.import(path, pick) || fixture.opened() != 1) {
      CHECK(fixture.opened() == 1);
      continue;
    }
    const auto& document = fixture.document(0);
    // One "Page N" container per page, each holding its own hidden Recognized Text group.
    CHECK(document.layers().size() == 2);
    int groups = 0;
    for (const auto& container : document.layers()) {
      CHECK(container.kind() == patchy::LayerKind::Group);
      const auto* group = find_recognized_group(container.children());
      CHECK(group != nullptr);
      if (group != nullptr) {
        CHECK(!group->visible());
        CHECK(text_children(*group) == 2);
        ++groups;
      }
    }
    CHECK(groups == 2);
    CHECK(fixture.fake->calls().size() == 2);
    // Hidden text changes no page: each still passes its original image through an export.
    CHECK(document.metadata().pdf_page_source_pages.size() == 2);
  }
}

void ui_pdf_import_ocr_skips_pages_with_text() {
  {
    ImportFixture fixture;
    // Page 1 a scan, page 2 real text: only the scan is read.
    const auto path = write_pdf("ui_pdf_import_ocr_mixed", pdf_of({fixture.scan(), std::string()}));
    if (!fixture.import(path) || fixture.opened() != 2) {
      CHECK(fixture.opened() == 2);
      return;
    }
    CHECK(fixture.fake->calls().size() == 1);
    CHECK(recognized_text_group(fixture.document(0)) != nullptr);
    CHECK(find_recognized_group(fixture.document(1).layers()) == nullptr);
    CHECK(fixture.notes().contains(QStringLiteral("Text was recognized on 1 page")));
  }
  {
    ImportFixture fixture;
    // Nothing but real text: nothing is read, and the notes say why the option did nothing.
    const auto path = write_pdf("ui_pdf_import_ocr_text_only", pdf_of({std::string()}));
    if (!fixture.import(path) || fixture.opened() != 1) {
      CHECK(fixture.opened() == 1);
      return;
    }
    CHECK(fixture.fake->calls().empty());
    CHECK(find_recognized_group(fixture.document(0).layers()) == nullptr);
    CHECK(fixture.notes().contains(QStringLiteral("every page already has editable text")));
  }
  {
    ImportFixture fixture;
    // The same text page flattened has no text of its own any more: it is read.
    const auto path = write_pdf("ui_pdf_import_ocr_text_flattened", pdf_of({std::string()}));
    ImportPick pick;
    pick.mode = QStringLiteral("flatten");
    if (!fixture.import(path, pick) || fixture.opened() != 1) {
      CHECK(fixture.opened() == 1);
      return;
    }
    CHECK(fixture.fake->calls().size() == 1);
    CHECK(recognized_text_group(fixture.document(0)) != nullptr);
  }
}

void ui_pdf_import_ocr_cancel_keeps_recognized_pages() {
  ImportFixture fixture(/*delay_ms=*/1200);
  const auto path =
      write_pdf("ui_pdf_import_ocr_cancel", pdf_of({fixture.scan(), fixture.scan(), fixture.scan()}));
  // Cancel while the second page is being read.
  bool pressed = false;
  QTimer watcher;
  QObject::connect(&watcher, &QTimer::timeout, &watcher, [&pressed] {
    auto* progress =
        qobject_cast<QProgressDialog*>(find_top_level_dialog(QStringLiteral("pdfPageImagesProgressDialog")));
    if (pressed || progress == nullptr || !progress->isVisible() ||
        !progress->labelText().contains(QStringLiteral("page 2 of 3"))) {
      return;
    }
    if (auto* cancel = progress->findChild<QPushButton*>(); cancel != nullptr) {
      pressed = true;
      cancel->click();
    }
  });
  watcher.start(20);
  const bool drove = fixture.import(path);
  watcher.stop();
  CHECK(pressed);
  if (!drove || fixture.opened() != 3) {
    CHECK(fixture.opened() == 3);
    return;
  }
  // Every page still opened; the one already read keeps its text, the others have none.
  CHECK(recognized_text_group(fixture.document(0)) != nullptr);
  CHECK(recognized_text_group(fixture.document(1)) == nullptr);
  CHECK(recognized_text_group(fixture.document(2)) == nullptr);
  CHECK(fixture.fake->calls().size() <= 2);
  const auto notes = fixture.notes();
  CHECK(notes.contains(QStringLiteral("Text recognition stopped after 1 of 3 pages")));
  CHECK(notes.contains(QStringLiteral("Text was recognized on 1 page")));
}

void ui_pdf_import_ocr_palette_and_bit_depth() {
  struct DeepEditing {
    DeepEditing() { patchy::set_deep_editing_override(true); }
    ~DeepEditing() { patchy::set_deep_editing_override(std::nullopt); }
  } deep_editing;
  {
    ImportFixture fixture;
    fixture.set_result("visible");
    const auto path = write_pdf("ui_pdf_import_ocr_deep", pdf_of({fixture.scan()}));
    ImportPick pick;
    pick.bits = 16;
    if (!fixture.import(path, pick) || fixture.opened() != 1) {
      CHECK(fixture.opened() == 1);
      return;
    }
    const auto& document = fixture.document(0);
    CHECK(document.format().bit_depth == patchy::BitDepth::UInt16);
    const auto* group = recognized_text_group(document);
    CHECK(group != nullptr && text_children(*group) == 2);
    if (group != nullptr) {
      for (const auto& child : group->children()) {
        CHECK(child.pixels().format().bit_depth == patchy::BitDepth::UInt16);
      }
    }
  }
  {
    ImportFixture fixture;
    const auto path = write_pdf("ui_pdf_import_ocr_palette", pdf_of({fixture.scan()}));
    ImportPick pick;
    pick.mode = QStringLiteral("flatten");
    pick.color_mode = QStringLiteral("palette");
    if (!fixture.import(path, pick) || fixture.opened() != 1) {
      CHECK(fixture.opened() == 1);
      return;
    }
    // Indexed editing with the page's hidden text beside it.
    const auto& document = fixture.document(0);
    CHECK(document.palette_editing().has_value());
    const auto* group = recognized_text_group(document);
    CHECK(group != nullptr && !group->visible() && text_children(*group) == 2);
  }
}

void ui_pdf_non_interactive_import_never_recognizes() {
  ImportFixture fixture;
  // The tick is stored from an earlier import; a load without the dialog ignores it.
  auto settings = patchy::ui::app_settings();
  settings.setValue(QStringLiteral("imports/pdfOcr"), true);
  settings.setValue(QStringLiteral("imports/pdfOcrResult"), QStringLiteral("visible"));
  settings.sync();
  const auto path = write_pdf("ui_pdf_import_ocr_unattended", pdf_of({fixture.scan()}));
  patchy::ui::PdfImportOptions options;
  options.pages = {0};
  options.resolution_ppi = kImportPpi;
  options.separate_documents = true;
  QString error;
  const auto loaded = patchy::ui::load_pdf_document(path, options, QString(), &error);
  CHECK(loaded.has_value());
  CHECK(loaded.has_value() && !loaded->ocr.has_value());
  // A script's open is the same path: no dialog, no recognition.
  auto& host = fixture.window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions run;
  run.name = QStringLiteral("pdf-ocr-open");
  run.unattended = true;
  const auto absolute = QDir().absoluteFilePath(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
  CHECK(host.run_source(QStringLiteral("app.open('%1'); patchy.setResult({ok: true});").arg(absolute),
                        std::move(run)));
  CHECK(patchy::test::ui::process_events_until([&host] { return !host.run_active(); }, 60000));
  CHECK(!host.last_run_had_error());
  CHECK(fixture.fake->calls().empty());
  CHECK(find_recognized_group(std::as_const(MainWindowTestAccess::document(fixture.window)).layers()) == nullptr);
}

#endif  // PATCHY_HAVE_QT_PDF

}  // namespace

std::vector<patchy::test::TestCase> pdf_import_ocr_ui_tests() {
  return {
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_import_ocr_controls_and_settings", ui_pdf_import_ocr_controls_and_settings},
      {"ui_pdf_import_ocr_hidden_text_keeps_pass_through", ui_pdf_import_ocr_hidden_text_keeps_pass_through},
      {"ui_pdf_import_ocr_visible_text_reencodes_page", ui_pdf_import_ocr_visible_text_reencodes_page},
      {"ui_pdf_import_ocr_replace_hides_scan_on_white", ui_pdf_import_ocr_replace_hides_scan_on_white},
      {"ui_pdf_import_ocr_groups_land_in_folders_and_artboards",
       ui_pdf_import_ocr_groups_land_in_folders_and_artboards},
      {"ui_pdf_import_ocr_skips_pages_with_text", ui_pdf_import_ocr_skips_pages_with_text},
      {"ui_pdf_import_ocr_cancel_keeps_recognized_pages", ui_pdf_import_ocr_cancel_keeps_recognized_pages},
      {"ui_pdf_import_ocr_palette_and_bit_depth", ui_pdf_import_ocr_palette_and_bit_depth},
      {"ui_pdf_non_interactive_import_never_recognizes", ui_pdf_non_interactive_import_never_recognizes},
#endif
  };
}
