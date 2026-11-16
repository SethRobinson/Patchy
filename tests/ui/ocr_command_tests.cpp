// Type > Recognize Text (OCR)... and Type > Copy Text from Image, the options dialog they
// share with Preferences, and what the dialog says when nothing can run (docs/ocr.md, "Type
// menu commands" and "Dialogs"). The scripted fake engine reads boxes these tests drew.

#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/pixel_depth.hpp"
#include "ocr/ocr_fake_engine.hpp"
#include "ocr_page_fixture.hpp"
#include "ocr_test_support.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/ocr_controls.hpp"
#include "ui/recognize_text_dialog.hpp"
#include "ui/script_engine.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFontComboBox>
#include <QImage>
#include <QLabel>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegion>
#include <QStatusBar>
#include <QTimer>

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using patchy::ocr::FakeEngineConfig;
using patchy::test::ui::add_ocr_scan_layer;
using patchy::test::ui::find_top_level_dialog;
using patchy::test::ui::guarded;
using patchy::test::ui::ocr_fake_for;
using patchy::test::ui::ocr_test_font;
using patchy::test::ui::OcrTestPage;
using patchy::test::ui::recognized_text_group;
using patchy::test::ui::ScopedOcrEngines;
using patchy::test::ui::SettingsGroupRestorer;
using patchy::test::ui::show_window;
using patchy::test::ui::when_dialog_visible;
using patchy::ui::MainWindow;
using patchy::ui::MainWindowTestAccess;

const QString kDialog = QStringLiteral("recognizeTextDialog");
const QString kProgress = QStringLiteral("recognizeTextProgressDialog");

QAction* action(MainWindow& window, const char* name) {
  auto* found = window.findChild<QAction*>(QLatin1String(name));
  CHECK(found != nullptr);
  return found;
}

void trigger(MainWindow& window, const char* name) {
  if (auto* found = action(window, name); found != nullptr) {
    CHECK(found->isEnabled());
    found->trigger();
  }
}

template <typename Widget>
Widget* child(QDialog& dialog, const char* name) {
  auto* found = dialog.findChild<Widget*>(QLatin1String(name));
  CHECK(found != nullptr);
  return found;
}

// Chooses the combo row whose data is `token`.
void choose(QDialog& dialog, const char* combo_name, const QString& token) {
  if (auto* combo = child<QComboBox>(dialog, combo_name); combo != nullptr) {
    const int row = combo->findData(token);
    CHECK(row >= 0);
    combo->setCurrentIndex(row);
  }
}

QStringList combo_tokens(QDialog& dialog, const char* combo_name) {
  QStringList tokens;
  if (auto* combo = child<QComboBox>(dialog, combo_name); combo != nullptr) {
    for (int row = 0; row < combo->count(); ++row) {
      tokens << combo->itemData(row).toString();
    }
  }
  return tokens;
}

// A hard rectangular selection (the script engine's select_region recipe).
void select_document_rect(patchy::ui::CanvasWidget& canvas, QRect rect) {
  auto snapshot = canvas.capture_selection_snapshot();
  snapshot.selection = QRegion(rect);
  snapshot.display_region = snapshot.selection;
  snapshot.mask_bounds = QRect();
  snapshot.mask_alpha = QImage();
  canvas.apply_selection_snapshot(snapshot);
  QApplication::processEvents();
}

// A 700 x 400 document with a "Scan" layer at (20, 20) holding two lines far enough apart
// to stay separate blocks, and the fake engine that reads them.
struct ScanFixture {
  MainWindow window;
  OcrTestPage page{600, 300};
  QRect first;
  QRect second;
  std::shared_ptr<patchy::ocr::FakeEngine> fake;
  std::unique_ptr<ScopedOcrEngines> engines;
  patchy::LayerId scan{};
  // Every ocr/ setting a test writes is put back.
  SettingsGroupRestorer settings{QStringLiteral("ocr")};

  explicit ScanFixture(int delay_ms = 0) {
    show_window(window);
    MainWindowTestAccess::create_document(window, 700, 400);
    first = page.draw(QStringLiteral("Top line of the scan"), 30, 30, ocr_test_font(36));
    second = page.draw(QStringLiteral("Bottom line"), 30, 210, ocr_test_font(36));
    FakeEngineConfig config;
    config.languages = {{"en-US", "English (United States)"}, {"ja", "Japanese"}};
    config.lines = page.lines;
    config.delay_ms = delay_ms;
    fake = patchy::ocr::make_fake_engine(std::move(config));
    engines = std::make_unique<ScopedOcrEngines>(
        std::vector<std::shared_ptr<patchy::ocr::Engine>>{fake});
    scan = add_ocr_scan_layer(window, page.image, QPoint(20, 20));
  }

  [[nodiscard]] const patchy::Document& document() {
    return std::as_const(MainWindowTestAccess::document(window));
  }
  [[nodiscard]] QString status() { return window.statusBar()->currentMessage(); }
};

void ui_recognize_text_dialog_explains_missing_engine() {
  const SettingsGroupRestorer settings{QStringLiteral("ocr")};
  MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 200, 100);
  const auto depth = MainWindowTestAccess::active_session_undo_depth(window);

  // The suite's default: no engine at all. The dialog opens, says so, and cannot run.
  bool saw = false;
  when_dialog_visible(kDialog, [&saw](QDialog& dialog) {
    auto* note = child<QLabel>(dialog, "recognizeTextUnavailableNote");
    auto* engine = child<QComboBox>(dialog, "recognizeTextEngineCombo");
    auto* language = child<QComboBox>(dialog, "recognizeTextLanguageCombo");
    auto* settings_button = child<QPushButton>(dialog, "recognizeTextLanguageSettingsButton");
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    CHECK(note != nullptr && engine != nullptr && language != nullptr && settings_button != nullptr &&
          buttons != nullptr);
    if (note != nullptr && engine != nullptr && language != nullptr && settings_button != nullptr &&
        buttons != nullptr) {
      CHECK(note->isVisibleTo(&dialog));
      CHECK(note->text().contains(QStringLiteral("no text recognition engine")));
      CHECK(engine->count() == 0 && !engine->isEnabled());
      CHECK(language->count() == 0 && !language->isEnabled());
      CHECK(!settings_button->isVisibleTo(&dialog));
      CHECK(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
      saw = true;
    }
    dialog.reject();
  });
  trigger(window, "typeRecognizeTextAction");
  CHECK(saw);

  // Copy Text from Image has nothing to run either: it shows the same explanation.
  saw = false;
  when_dialog_visible(kDialog, [&saw](QDialog& dialog) {
    saw = true;
    dialog.reject();
  });
  trigger(window, "typeCopyTextFromImageAction");
  CHECK(saw);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth);
}

void ui_recognize_text_dialog_offers_language_settings() {
  const SettingsGroupRestorer settings{QStringLiteral("ocr")};
  // An engine that exists and has no language: the Windows engine on a PC without an OCR
  // language pack. The note says what to install and the button opens the settings page.
  FakeEngineConfig bare;
  bare.id = "windows";
  bare.languages.clear();
  bare.unavailable = patchy::ocr::Unavailable::NoLanguages;
  bare.unavailable_reason = "no text recognition language is installed";
  const ScopedOcrEngines engines({patchy::ocr::make_fake_engine(bare)});
  int opened = 0;
  patchy::ui::set_ocr_language_settings_opener_for_testing([&opened] { ++opened; });

  bool saw = false;
  when_dialog_visible(kDialog, [&saw](QDialog& dialog) {
    auto* note = child<QLabel>(dialog, "recognizeTextUnavailableNote");
    auto* engine = child<QComboBox>(dialog, "recognizeTextEngineCombo");
    auto* settings_button = child<QPushButton>(dialog, "recognizeTextLanguageSettingsButton");
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    if (note != nullptr && engine != nullptr && settings_button != nullptr && buttons != nullptr) {
      CHECK(engine->count() == 1);
      CHECK(engine->currentText() == QStringLiteral("Windows (built in)"));
      CHECK(note->isVisibleTo(&dialog));
      CHECK(note->text().contains(QStringLiteral("Language & region")));
      CHECK(settings_button->isVisibleTo(&dialog));
      CHECK(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
      settings_button->click();
      saw = true;
    }
    dialog.reject();
  });
  CHECK(!patchy::ui::request_recognize_text(nullptr, {}).has_value());
  patchy::ui::set_ocr_language_settings_opener_for_testing({});
  CHECK(saw);
  CHECK(opened == 1);
}

void ui_recognize_text_dialog_remembers_its_options() {
  ScanFixture fixture;
  // First run: defaults. Layers over the image, one per block, the Type tool's font, style
  // matched; the active layer is what is read; no Selection row without a selection.
  when_dialog_visible(kDialog, [](QDialog& dialog) {
    CHECK(combo_tokens(dialog, "recognizeTextSourceCombo") ==
          QStringList({QStringLiteral("layer"), QStringLiteral("image")}));
    CHECK(combo_tokens(dialog, "recognizeTextResultCombo") ==
          QStringList({QStringLiteral("visible"), QStringLiteral("replace"), QStringLiteral("hidden"),
                       QStringLiteral("clipboard")}));
    if (auto* result = child<QComboBox>(dialog, "recognizeTextResultCombo"); result != nullptr) {
      CHECK(result->currentData().toString() == QStringLiteral("visible"));
      CHECK(result->itemText(1) == QStringLiteral("Show text and hide original"));
    }
    if (auto* grouping = child<QComboBox>(dialog, "recognizeTextGroupingCombo"); grouping != nullptr) {
      CHECK(grouping->currentData().toString() == QStringLiteral("blocks"));
    }
    if (auto* match = child<QCheckBox>(dialog, "recognizeTextMatchStyleCheck"); match != nullptr) {
      CHECK(match->isChecked());
      match->setChecked(false);
    }
    if (auto* language = child<QComboBox>(dialog, "recognizeTextLanguageCombo"); language != nullptr) {
      CHECK(language->count() == 2);
      CHECK(language->currentData().toString() == QStringLiteral("en-US"));
    }
    // Copying needs no layer options.
    choose(dialog, "recognizeTextResultCombo", QStringLiteral("clipboard"));
    if (auto* grouping = child<QComboBox>(dialog, "recognizeTextGroupingCombo"); grouping != nullptr) {
      CHECK(!grouping->isEnabled());
    }
    if (auto* font = child<QFontComboBox>(dialog, "recognizeTextFontCombo"); font != nullptr) {
      CHECK(!font->isEnabled());
    }
    choose(dialog, "recognizeTextResultCombo", QStringLiteral("hidden"));
    choose(dialog, "recognizeTextGroupingCombo", QStringLiteral("lines"));
    choose(dialog, "recognizeTextSourceCombo", QStringLiteral("image"));
    choose(dialog, "recognizeTextLanguageCombo", QStringLiteral("ja"));
    dialog.accept();
  });
  trigger(fixture.window, "typeRecognizeTextAction");
  const auto calls = fixture.fake->calls();
  CHECK(calls.size() == 1);
  if (calls.size() == 1) {
    // The whole image, in the language picked.
    CHECK(calls[0].language == "ja");
    CHECK(calls[0].image_width == 700 && calls[0].image_height == 400);
  }
  auto stored = patchy::ui::app_settings();
  CHECK(stored.value(QStringLiteral("ocr/engine")).toString() == QStringLiteral("fake"));
  CHECK(stored.value(QStringLiteral("ocr/language")).toString() == QStringLiteral("ja"));
  CHECK(stored.value(QStringLiteral("ocr/result")).toString() == QStringLiteral("hidden"));
  CHECK(stored.value(QStringLiteral("ocr/source")).toString() == QStringLiteral("image"));
  CHECK(stored.value(QStringLiteral("ocr/grouping")).toString() == QStringLiteral("lines"));
  CHECK(!stored.value(QStringLiteral("ocr/matchStyle"), true).toBool());
  CHECK(!stored.contains(QStringLiteral("ocr/fontFamily")));

  // Second run starts where the first ended; a selection, once there is one, is what is read.
  select_document_rect(*MainWindowTestAccess::canvas(fixture.window), QRect(30, 30, 300, 100));
  bool saw = false;
  when_dialog_visible(kDialog, [&saw](QDialog& dialog) {
    CHECK(combo_tokens(dialog, "recognizeTextSourceCombo") ==
          QStringList({QStringLiteral("layer"), QStringLiteral("selection"), QStringLiteral("image")}));
    if (auto* source = child<QComboBox>(dialog, "recognizeTextSourceCombo"); source != nullptr) {
      CHECK(source->currentData().toString() == QStringLiteral("selection"));
    }
    if (auto* result = child<QComboBox>(dialog, "recognizeTextResultCombo"); result != nullptr) {
      CHECK(result->currentData().toString() == QStringLiteral("hidden"));
    }
    if (auto* grouping = child<QComboBox>(dialog, "recognizeTextGroupingCombo"); grouping != nullptr) {
      CHECK(grouping->currentData().toString() == QStringLiteral("lines"));
    }
    if (auto* language = child<QComboBox>(dialog, "recognizeTextLanguageCombo"); language != nullptr) {
      CHECK(language->currentData().toString() == QStringLiteral("ja"));
    }
    if (auto* match = child<QCheckBox>(dialog, "recognizeTextMatchStyleCheck"); match != nullptr) {
      CHECK(!match->isChecked());
    }
    saw = true;
    dialog.reject();
  });
  trigger(fixture.window, "typeRecognizeTextAction");
  CHECK(saw);
  // Cancelling the dialog runs nothing.
  CHECK(fixture.fake->calls().size() == 1);
}

void ui_recognize_text_makes_one_group_in_one_undo_step() {
  ScanFixture fixture;
  const auto depth = MainWindowTestAccess::active_session_undo_depth(fixture.window);
  when_dialog_visible(kDialog, [](QDialog& dialog) {
    if (auto* font = child<QFontComboBox>(dialog, "recognizeTextFontCombo"); font != nullptr) {
      font->setCurrentFont(QFont(QStringLiteral("Arial")));
    }
    dialog.accept();
  });
  trigger(fixture.window, "typeRecognizeTextAction");

  const auto* group = recognized_text_group(fixture.document());
  CHECK(group != nullptr);
  if (group == nullptr) {
    return;
  }
  // The active layer on its own was read, and the group sits right above it, selected.
  const auto calls = fixture.fake->calls();
  CHECK(calls.size() == 1 && calls[0].image_width == 600 && calls[0].image_height == 300);
  CHECK(group->visible());
  CHECK(group->children().size() == 2);
  CHECK(fixture.document().active_layer_id() == group->id());
  const auto& layers = fixture.document().layers();
  CHECK(layers.size() >= 2 && layers.back().id() == group->id() &&
        layers[layers.size() - 2].id() == fixture.scan);
  const auto* scan = fixture.document().find_layer(fixture.scan);
  CHECK(scan != nullptr && scan->visible());
  for (const auto& line : group->children()) {
    CHECK(patchy::layer_is_text(line));
  }
  CHECK(fixture.status().contains(QStringLiteral("Recognized 2 line")));
  CHECK(MainWindowTestAccess::active_session_undo_depth(fixture.window) == depth + 1);
  CHECK(MainWindowTestAccess::current_history_label(fixture.window) == QStringLiteral("Recognize Text"));

  MainWindowTestAccess::undo(fixture.window);
  CHECK(recognized_text_group(fixture.document()) == nullptr);
  CHECK(MainWindowTestAccess::active_session_undo_depth(fixture.window) == depth);
}

void ui_recognize_text_copies_to_clipboard_only() {
  ScanFixture fixture;
  QApplication::clipboard()->clear();
  const auto depth = MainWindowTestAccess::active_session_undo_depth(fixture.window);
  const auto layer_count = fixture.document().layers().size();
  when_dialog_visible(kDialog, [](QDialog& dialog) {
    choose(dialog, "recognizeTextResultCombo", QStringLiteral("clipboard"));
    dialog.accept();
  });
  trigger(fixture.window, "typeRecognizeTextAction");
  CHECK(QApplication::clipboard()->text() == QStringLiteral("Top line of the scan\nBottom line"));
  CHECK(fixture.status().contains(QStringLiteral("Copied 2 line")));
  QApplication::clipboard()->clear();
  // Nothing in the document changed: no layer, no undo step.
  CHECK(recognized_text_group(fixture.document()) == nullptr);
  CHECK(fixture.document().layers().size() == layer_count);
  CHECK(MainWindowTestAccess::active_session_undo_depth(fixture.window) == depth);
}

void ui_recognize_text_reads_selection_only() {
  ScanFixture fixture;
  // Around the second line only, in document pixels (the layer sits at 20, 20).
  const QRect selected = fixture.second.translated(20, 20).adjusted(-12, -12, 12, 12);
  select_document_rect(*MainWindowTestAccess::canvas(fixture.window), selected);
  when_dialog_visible(kDialog, [](QDialog& dialog) {
    if (auto* source = child<QComboBox>(dialog, "recognizeTextSourceCombo"); source != nullptr) {
      CHECK(source->currentData().toString() == QStringLiteral("selection"));
    }
    if (auto* font = child<QFontComboBox>(dialog, "recognizeTextFontCombo"); font != nullptr) {
      font->setCurrentFont(QFont(QStringLiteral("Arial")));
    }
    dialog.accept();
  });
  // The fake reports its lines in the pixels it is handed: the second line, as the cropped
  // image holds it.
  auto line = fixture.page.lines[1];
  line.box.x = 12;
  line.box.y = 12;
  fixture.fake->set_lines({line});
  trigger(fixture.window, "typeRecognizeTextAction");

  const auto calls = fixture.fake->calls();
  CHECK(calls.size() == 1);
  if (calls.size() == 1) {
    CHECK(calls[0].image_width == selected.width() && calls[0].image_height == selected.height());
  }
  const auto* group = recognized_text_group(fixture.document());
  CHECK(group != nullptr);
  if (group == nullptr) {
    return;
  }
  // The whole image was read, so the group goes on top; its one line lands on the drawn
  // text, in document pixels.
  CHECK(group->children().size() == 1);
  CHECK(fixture.document().layers().back().id() == group->id());
  if (group->children().size() == 1) {
    const auto bounds = group->children()[0].bounds();
    const QRect drawn = fixture.second.translated(20, 20);
    const QRect layer_rect(bounds.x, bounds.y, bounds.width, bounds.height);
    CHECK(layer_rect.intersects(drawn));
    CHECK(std::abs(layer_rect.center().x() - drawn.center().x()) <= 12);
    CHECK(std::abs(layer_rect.center().y() - drawn.center().y()) <= 12);
  }
}

void ui_recognize_text_cancel_changes_nothing() {
  ScanFixture fixture(/*delay_ms=*/20000);
  const auto depth = MainWindowTestAccess::active_session_undo_depth(fixture.window);
  const auto layer_count = fixture.document().layers().size();
  when_dialog_visible(kDialog, [](QDialog& dialog) { dialog.accept(); });
  // The engine is slow: the progress dialog comes up and its Cancel ends the run.
  QElapsedTimer run;
  run.start();
  bool cancelled = false;
  when_dialog_visible(kProgress, [&cancelled](QDialog& dialog) {
    if (auto* progress = qobject_cast<QProgressDialog*>(&dialog); progress != nullptr) {
      progress->cancel();
      cancelled = true;
    }
  });
  trigger(fixture.window, "typeRecognizeTextAction");
  CHECK(cancelled);
  CHECK(run.elapsed() < 15000);
  CHECK(fixture.status().contains(QStringLiteral("cancelled")));
  CHECK(recognized_text_group(fixture.document()) == nullptr);
  CHECK(fixture.document().layers().size() == layer_count);
  CHECK(MainWindowTestAccess::active_session_undo_depth(fixture.window) == depth);
}

void ui_recognize_text_reports_no_text_and_engine_failures() {
  ScanFixture fixture;
  const auto depth = MainWindowTestAccess::active_session_undo_depth(fixture.window);
  fixture.fake->set_lines({});
  when_dialog_visible(kDialog, [](QDialog& dialog) { dialog.accept(); });
  trigger(fixture.window, "typeRecognizeTextAction");
  CHECK(fixture.status().contains(QStringLiteral("No text was recognized")));
  CHECK(recognized_text_group(fixture.document()) == nullptr);
  CHECK(MainWindowTestAccess::active_session_undo_depth(fixture.window) == depth);

  // An engine that fails mid-run: the message, and nothing changed.
  FakeEngineConfig failing;
  failing.fail_with = "the recognizer stopped";
  fixture.engines.reset();
  const ScopedOcrEngines engines({patchy::ocr::make_fake_engine(failing)});
  when_dialog_visible(kDialog, [](QDialog& dialog) { dialog.accept(); });
  trigger(fixture.window, "typeRecognizeTextAction");
  CHECK(fixture.status().contains(QStringLiteral("the recognizer stopped")));
  CHECK(recognized_text_group(fixture.document()) == nullptr);
  CHECK(MainWindowTestAccess::active_session_undo_depth(fixture.window) == depth);
}

void ui_recognize_text_in_16_bit_document() {
  // The suite runs with deep editing off; this test needs it for the conversion.
  struct DeepEditing {
    DeepEditing() { patchy::set_deep_editing_override(true); }
    ~DeepEditing() { patchy::set_deep_editing_override(std::nullopt); }
  } deep_editing;
  ScanFixture fixture;
  // Convert the document to 16 bits per channel through the script call the menu uses.
  auto& host = fixture.window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("ocr-depth");
  options.unattended = true;
  CHECK(host.run_source(QStringLiteral("app.activeDocument.convertBitDepth(16);"), std::move(options)));
  CHECK(patchy::test::ui::process_events_until([&host] { return !host.run_active(); }, 30000));
  CHECK(!host.last_run_had_error());
  CHECK(fixture.document().format().bit_depth == patchy::BitDepth::UInt16);
  // The conversion keeps the Scan layer active or not; make it the one read.
  MainWindowTestAccess::document(fixture.window).set_active_layer(fixture.scan);
  MainWindowTestAccess::refresh_layer_ui(fixture.window);

  when_dialog_visible(kDialog, [](QDialog& dialog) {
    choose(dialog, "recognizeTextSourceCombo", QStringLiteral("layer"));
    if (auto* font = child<QFontComboBox>(dialog, "recognizeTextFontCombo"); font != nullptr) {
      font->setCurrentFont(QFont(QStringLiteral("Arial")));
    }
    dialog.accept();
  });
  trigger(fixture.window, "typeRecognizeTextAction");
  // The engine got an 8-bit picture of the layer; the text layers are part of the deep
  // document like any typed text.
  const auto calls = fixture.fake->calls();
  CHECK(calls.size() == 1 && calls[0].image_width == 600 && calls[0].image_height == 300);
  const auto* group = recognized_text_group(fixture.document());
  CHECK(group != nullptr && group->children().size() == 2);
  if (group != nullptr && group->children().size() == 2) {
    // The text is black on the page, as drawn: the colour was sampled through the 8-bit view.
    const QImage shown = patchy::ui::qimage_from_document(fixture.document(), true);
    CHECK(!shown.isNull());
    for (const auto& line : group->children()) {
      CHECK(patchy::layer_is_text(line));
      CHECK(line.bounds().width > 100);
    }
  }
}

void ui_copy_text_from_image_runs_without_a_dialog() {
  ScanFixture fixture;
  QApplication::clipboard()->clear();
  const auto depth = MainWindowTestAccess::active_session_undo_depth(fixture.window);
  // Stored options that Copy Text ignores: it always copies and never adds layers.
  auto stored = patchy::ui::app_settings();
  stored.setValue(QStringLiteral("ocr/result"), QStringLiteral("replace"));
  stored.setValue(QStringLiteral("ocr/source"), QStringLiteral("image"));
  stored.setValue(QStringLiteral("ocr/language"), QStringLiteral("ja"));
  stored.sync();

  bool stray_dialog = false;
  QTimer::singleShot(0, [&stray_dialog] { stray_dialog = find_top_level_dialog(kDialog) != nullptr; });
  trigger(fixture.window, "typeCopyTextFromImageAction");
  QApplication::processEvents();
  CHECK(!stray_dialog);
  auto calls = fixture.fake->calls();
  CHECK(calls.size() == 1);
  if (calls.size() == 1) {
    // No selection: the active layer, with the stored language.
    CHECK(calls[0].image_width == 600 && calls[0].image_height == 300);
    CHECK(calls[0].language == "ja");
  }
  CHECK(QApplication::clipboard()->text() == QStringLiteral("Top line of the scan\nBottom line"));
  CHECK(fixture.status().contains(QStringLiteral("Copied 2 line")));
  CHECK(recognized_text_group(fixture.document()) == nullptr);
  CHECK(fixture.document().find_layer(fixture.scan)->visible());
  CHECK(MainWindowTestAccess::active_session_undo_depth(fixture.window) == depth);

  // With a selection: only that part of the image.
  const QRect selected = fixture.first.translated(20, 20).adjusted(-10, -10, 10, 10);
  select_document_rect(*MainWindowTestAccess::canvas(fixture.window), selected);
  auto line = fixture.page.lines[0];
  line.box.x = 10;
  line.box.y = 10;
  fixture.fake->set_lines({line});
  trigger(fixture.window, "typeCopyTextFromImageAction");
  calls = fixture.fake->calls();
  CHECK(calls.size() == 2);
  if (calls.size() == 2) {
    CHECK(calls[1].image_width == selected.width() && calls[1].image_height == selected.height());
  }
  CHECK(QApplication::clipboard()->text() == QStringLiteral("Top line of the scan"));
  CHECK(fixture.status().contains(QStringLiteral("Copied 1 line")));
  QApplication::clipboard()->clear();
}

void ui_preferences_text_recognition_stores_engine_and_language() {
  ScanFixture fixture;
  bool saw = false;
  QTimer::singleShot(0, guarded([&saw] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* heading = child<QLabel>(*dialog, "preferencesOcrHeading");
    auto* engine = child<QComboBox>(*dialog, "preferencesOcrEngineCombo");
    auto* language = child<QComboBox>(*dialog, "preferencesOcrLanguageCombo");
    auto* note = child<QLabel>(*dialog, "preferencesOcrUnavailableNote");
    if (heading != nullptr && engine != nullptr && language != nullptr && note != nullptr) {
      CHECK(heading->text() == QStringLiteral("Text Recognition"));
      CHECK(engine->count() == 1 && engine->currentData().toString() == QStringLiteral("fake"));
      CHECK(language->currentData().toString() == QStringLiteral("en-US"));
      CHECK(note->text().isEmpty());
      language->setCurrentIndex(language->findData(QStringLiteral("ja")));
      saw = true;
    }
    dialog->accept();
  }));
  MainWindowTestAccess::show_preferences(fixture.window);
  CHECK(saw);
  auto stored = patchy::ui::app_settings();
  CHECK(stored.value(QStringLiteral("ocr/engine")).toString() == QStringLiteral("fake"));
  CHECK(stored.value(QStringLiteral("ocr/language")).toString() == QStringLiteral("ja"));

  // The pick is what the next recognition uses.
  QApplication::clipboard()->clear();
  trigger(fixture.window, "typeCopyTextFromImageAction");
  const auto calls = fixture.fake->calls();
  CHECK(calls.size() == 1 && calls[0].language == "ja");
  QApplication::clipboard()->clear();
}

}  // namespace

std::vector<patchy::test::TestCase> ocr_command_ui_tests() {
  return {
      {"ui_recognize_text_dialog_explains_missing_engine", ui_recognize_text_dialog_explains_missing_engine},
      {"ui_recognize_text_dialog_offers_language_settings", ui_recognize_text_dialog_offers_language_settings},
      {"ui_recognize_text_dialog_remembers_its_options", ui_recognize_text_dialog_remembers_its_options},
      {"ui_recognize_text_makes_one_group_in_one_undo_step", ui_recognize_text_makes_one_group_in_one_undo_step},
      {"ui_recognize_text_copies_to_clipboard_only", ui_recognize_text_copies_to_clipboard_only},
      {"ui_recognize_text_reads_selection_only", ui_recognize_text_reads_selection_only},
      {"ui_recognize_text_cancel_changes_nothing", ui_recognize_text_cancel_changes_nothing},
      {"ui_recognize_text_reports_no_text_and_engine_failures",
       ui_recognize_text_reports_no_text_and_engine_failures},
      {"ui_recognize_text_in_16_bit_document", ui_recognize_text_in_16_bit_document},
      {"ui_copy_text_from_image_runs_without_a_dialog", ui_copy_text_from_image_runs_without_a_dialog},
      {"ui_preferences_text_recognition_stores_engine_and_language",
       ui_preferences_text_recognition_stores_engine_and_language},
  };
}
