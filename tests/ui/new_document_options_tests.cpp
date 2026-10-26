// New Document options (docs/new-document.md): the Color Mode list and Color Profile line
// (GitHub issue 360), the empty-layer preference (issue 363), the Name field and saved
// presets (issue 374), and the templates' own units (local-013), plus app.newDocument's
// matching options.
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
#include "ui/main_window.hpp"
#include "ui/measurement_units.hpp"
#include "ui/new_document_dialog.hpp"
#include "ui/script_engine.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <utility>

namespace {

using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;
using patchy::test::ui::scripting_support::wait_for_run_end;

// Starts each test from no remembered New Document state (the Recent row, presets, the
// last group, the preference) and puts back whatever the store held afterwards.
class NewDocumentSettingsScope {
 public:
  NewDocumentSettingsScope() {
    patchy::ui::app_settings().remove(QStringLiteral("newDocument"));
    patchy::ui::app_settings().remove(QStringLiteral("color/cmykProfile"));
    QApplication::clipboard()->clear();
  }

 private:
  SettingsValueRestorer recent_{QStringLiteral("newDocument/recent")};
  SettingsValueRestorer presets_{QStringLiteral("newDocument/userPresets")};
  SettingsValueRestorer category_{QStringLiteral("newDocument/lastCategory")};
  SettingsValueRestorer layer_{QStringLiteral("newDocument/addEmptyLayer")};
  SettingsValueRestorer cmyk_profile_{QStringLiteral("color/cmykProfile")};
};

// Runs `inspect` on the New Document dialog once File > New opens it; `inspect` accepts or
// rejects it.
bool with_new_document_dialog(patchy::ui::MainWindow& window, const std::function<void(QDialog&)>& inspect) {
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyNewDocumentDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    saw_dialog = true;
    inspect(*dialog);
  });
  require_action(window, "fileNewAction")->trigger();
  QApplication::processEvents();
  return saw_dialog;
}

void set_pixel_size(QDialog& dialog, int width_value, int height_value) {
  auto* unit = dialog.findChild<QComboBox*>(QStringLiteral("newDocumentUnitCombo"));
  auto* width = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
  auto* height = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentHeightSpin"));
  CHECK(unit != nullptr && width != nullptr && height != nullptr);
  if (unit == nullptr || width == nullptr || height == nullptr) {
    return;
  }
  unit->setCurrentIndex(unit->findData(static_cast<int>(patchy::ui::MeasurementUnit::Pixels)));
  width->setValue(width_value);
  height->setValue(height_value);
  QApplication::processEvents();
}

QListWidgetItem* card_with_id(QListWidget& list, const QString& id) {
  for (int row = 0; row < list.count(); ++row) {
    if (list.item(row)->data(patchy::ui::kNewDocumentPresetIdRole).toString() == id) {
      return list.item(row);
    }
  }
  return nullptr;
}

QString card_size_text(const QListWidgetItem* item) {
  return item != nullptr ? item->data(patchy::ui::kNewDocumentPresetSizeTextRole).toString() : QString();
}

// Paper and print templates show their size in the unit they are specified in (local-013):
// the cards read "210 x 297 mm" or "8.5 x 11 in", picking one switches the Width/Height unit
// to it (a screen template back to Pixels), the document keeps the template's pixels, and
// search finds the shown size. Recent cards and saved presets show their stored unit.
void ui_new_document_templates_show_their_unit() {
  const NewDocumentSettingsScope scope;
  SettingsValueRestorer ruler_units(QStringLiteral("view/rulerUnits"));
  patchy::ui::app_settings().setValue(QStringLiteral("view/rulerUnits"), QStringLiteral("cm"));
  patchy::ui::MainWindow window;
  show_window(window);

  const auto pick = [](QDialog& dialog, const char* chip, const QString& id) {
    if (auto* group = dialog.findChild<QToolButton*>(QLatin1String(chip)); group != nullptr) {
      group->click();
      QApplication::processEvents();
    }
    auto* presets = dialog.findChild<QListWidget*>(QStringLiteral("newDocumentPresetList"));
    auto* card = presets != nullptr ? card_with_id(*presets, id) : nullptr;
    CHECK(card != nullptr);
    if (card != nullptr) {
      presets->setCurrentItem(card);
      QApplication::processEvents();
    }
    return card;
  };

  CHECK(with_new_document_dialog(window, [&](QDialog& dialog) {
    auto* presets = dialog.findChild<QListWidget*>(QStringLiteral("newDocumentPresetList"));
    auto* unit = dialog.findChild<QComboBox*>(QStringLiteral("newDocumentUnitCombo"));
    auto* width = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
    auto* height = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentHeightSpin"));
    auto* resolution = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentResolutionSpin"));
    auto* summary = dialog.findChild<QLabel*>(QStringLiteral("newDocumentSummaryLabel"));
    auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("newDocumentSearchEdit"));
    CHECK(presets != nullptr && unit != nullptr && width != nullptr && height != nullptr && resolution != nullptr &&
          summary != nullptr && search != nullptr);
    if (presets == nullptr || unit == nullptr || width == nullptr || height == nullptr || resolution == nullptr ||
        summary == nullptr || search == nullptr) {
      dialog.reject();
      return;
    }
    // The opening card (Default) keeps the remembered unit, here the ruler unit.
    CHECK(unit->currentText() == QStringLiteral("Centimeters"));
    CHECK(card_size_text(card_with_id(*presets, QStringLiteral("screen-1024x768"))) ==
          QStringLiteral("1024 x 768 px"));

    // Every paper and print card reads its nominal size in its own unit.
    const std::vector<std::pair<const char*, const char*>> print_cards = {
        {"print-a6", "105 x 148 mm"},
        {"print-a5", "148 x 210 mm"},
        {"print-a4", "210 x 297 mm"},
        {"print-a3", "297 x 420 mm"},
        {"print-a2", "420 x 594 mm"},
        {"print-us-letter", "8.5 x 11 in"},
        {"print-us-legal", "8.5 x 14 in"},
        {"print-tabloid", "11 x 17 in"},
        {"print-business-card-us", "3.5 x 2 in"},
        {"print-business-card-eu", "85 x 55 mm"},
        {"print-poster-18x24", "18 x 24 in"},
    };
    const std::vector<std::pair<const char*, const char*>> photo_cards = {
        {"photo-print-4x6", "4 x 6 in"},       {"print-5x7", "5 x 7 in"},
        {"print-8x10", "8 x 10 in"},           {"photo-print-11x14", "11 x 14 in"},
        {"photo-print-8x8", "8 x 8 in"},       {"photo-print-10x15cm", "10 x 15 cm"},
        {"photo-print-13x18cm", "13 x 18 cm"}, {"photo-3x2-3000", "3000 x 2000 px"},
    };
    for (const auto& [chip, cards] : {std::pair{"newDocumentPrintChip", &print_cards},
                                      std::pair{"newDocumentPhotoChip", &photo_cards}}) {
      if (auto* group = dialog.findChild<QToolButton*>(QLatin1String(chip)); group != nullptr) {
        group->click();
        QApplication::processEvents();
      }
      for (const auto& [id, text] : *cards) {
        const auto shown = card_size_text(card_with_id(*presets, QLatin1String(id)));
        if (shown != QLatin1String(text)) {
          std::cerr << id << " shows " << shown.toStdString() << '\n';
        }
        CHECK(shown == QLatin1String(text));
      }
    }

    // Picking a template takes its unit; the pixels and the resolution stay the template's.
    pick(dialog, "newDocumentPrintChip", QStringLiteral("print-a4"));
    CHECK(unit->currentText() == QStringLiteral("Millimeters"));
    CHECK(std::abs(width->value() - 210.0) < 1e-6);
    CHECK(std::abs(height->value() - 297.0) < 1e-6);
    CHECK(std::abs(resolution->value() - 300.0) < 1e-6);
    CHECK(summary->text().contains(QStringLiteral("2480 x 3508 px")));
    CHECK(summary->text().contains(QStringLiteral("210 x 297 mm at 300 ppi")));
    save_widget_artifact("local_013_new_document_print_a4", dialog);
    pick(dialog, "newDocumentPrintChip", QStringLiteral("print-us-letter"));
    CHECK(unit->currentText() == QStringLiteral("Inches"));
    CHECK(std::abs(width->value() - 8.5) < 1e-6);
    CHECK(std::abs(height->value() - 11.0) < 1e-6);
    pick(dialog, "newDocumentPhotoChip", QStringLiteral("photo-print-10x15cm"));
    CHECK(unit->currentText() == QStringLiteral("Centimeters"));
    CHECK(std::abs(width->value() - 10.0) < 1e-6);
    CHECK(std::abs(height->value() - 15.0) < 1e-6);
    pick(dialog, "newDocumentScreenChip", QStringLiteral("screen-1080p"));
    CHECK(unit->currentText() == QStringLiteral("Pixels"));
    CHECK(width->value() == 1920);
    CHECK(summary->text().contains(QStringLiteral("26.667 x 15 in at 72 ppi")));

    // Search matches the shown size as well as the pixels.
    const auto search_ids = [&](const QString& text) {
      search->setText(text);
      QApplication::processEvents();
      QStringList ids;
      for (int row = 0; row < presets->count(); ++row) {
        ids << presets->item(row)->data(patchy::ui::kNewDocumentPresetIdRole).toString();
      }
      return ids;
    };
    auto ids = search_ids(QStringLiteral("210"));
    CHECK(ids.contains(QStringLiteral("print-a4")) && ids.contains(QStringLiteral("print-a5")));
    CHECK(!ids.contains(QStringLiteral("print-us-letter")));
    ids = search_ids(QStringLiteral("8.5"));
    CHECK(ids.contains(QStringLiteral("print-us-letter")) && ids.contains(QStringLiteral("print-us-legal")));
    CHECK(!ids.contains(QStringLiteral("print-a4")));
    ids = search_ids(QStringLiteral("2480"));
    CHECK(ids.contains(QStringLiteral("print-a4")));
    search->clear();
    QApplication::processEvents();

    pick(dialog, "newDocumentPrintChip", QStringLiteral("print-a4"));
    dialog.accept();
  }));
  const auto& document = MainWindowTestAccess::document(window);
  CHECK(document.width() == 2480 && document.height() == 3508);
  CHECK(std::abs(document.print_settings().horizontal_ppi - 300.0) < 1e-6);

  // The Recent card shows its stored unit, and so does a saved preset.
  patchy::ui::app_settings().setValue(
      QStringLiteral("newDocument/userPresets"),
      QStringLiteral(R"([{"id":"user-1","name":"Inch card","w":600,"h":300,"ppi":300,"unit":"in",)"
                     R"("resolutionUnit":0,"background":"#ffffffff","bits":8,"artboards":false}])"));
  CHECK(with_new_document_dialog(window, [&](QDialog& dialog) {
    auto* recent = dialog.findChild<QListWidget*>(QStringLiteral("newDocumentRecentList"));
    auto* presets = dialog.findChild<QListWidget*>(QStringLiteral("newDocumentPresetList"));
    auto* unit = dialog.findChild<QComboBox*>(QStringLiteral("newDocumentUnitCombo"));
    auto* width = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
    CHECK(recent != nullptr && presets != nullptr && unit != nullptr && width != nullptr);
    if (recent == nullptr || presets == nullptr || unit == nullptr || width == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(card_size_text(card_with_id(*recent, QStringLiteral("last-used"))) == QStringLiteral("210 x 297 mm"));
    CHECK(unit->currentText() == QStringLiteral("Millimeters"));
    CHECK(std::abs(width->value() - 210.0) < 1e-6);
    auto* saved = pick(dialog, "newDocumentSavedChip", QStringLiteral("user-1"));
    CHECK(card_size_text(saved) == QStringLiteral("2 x 1 in"));
    CHECK(unit->currentText() == QStringLiteral("Inches"));
    CHECK(std::abs(width->value() - 2.0) < 1e-6);
    dialog.reject();
  }));
}

// GitHub issue 360: Color Mode holds RGB Color and CMYK Proof; CMYK Proof creates the
// document in CMYK Proof and the Color Profile line names the Preferences CMYK profile; the
// Last Used card brings the mode back; under the formula the CMYK Proof entry is greyed.
void ui_new_document_color_mode_creates_cmyk_proof() {
  NewDocumentSettingsScope scope;
  patchy::ui::MainWindow window;
  show_window(window);
  const auto srgb_name = QStringLiteral("sRGB IEC61966-2.1");

  CHECK(with_new_document_dialog(window, [&](QDialog& dialog) {
    auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("newDocumentColorModeCombo"));
    auto* profile = dialog.findChild<QLabel*>(QStringLiteral("newDocumentColorProfileLabel"));
    CHECK(mode != nullptr && profile != nullptr);
    if (mode == nullptr || profile == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(mode->count() == 2);
    CHECK(mode->itemData(0).toString() == QStringLiteral("rgb"));
    CHECK(mode->itemData(1).toString() == QStringLiteral("cmykProof"));
    CHECK(mode->currentIndex() == 0);
    CHECK(profile->text() == srgb_name);
    save_widget_artifact("issue_360_new_document_rgb", dialog);
    mode->setCurrentIndex(1);
    QApplication::processEvents();
    CHECK(profile->text().endsWith(QStringLiteral(", from Preferences")));
    CHECK(!profile->text().contains(srgb_name));
    save_widget_artifact("issue_360_new_document_cmyk_proof", dialog);
    set_pixel_size(dialog, 120, 80);
    dialog.accept();
  }));
  const auto proof_index = MainWindowTestAccess::session_count(window) - 1;
  CHECK(MainWindowTestAccess::session_cmyk_proof(window, proof_index));
  CHECK(require_action(window, "imageModeCmykProofAction")->isChecked());

  // Last Used restores CMYK Proof; RGB Color creates a plain RGB document.
  CHECK(with_new_document_dialog(window, [&](QDialog& dialog) {
    auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("newDocumentColorModeCombo"));
    CHECK(mode != nullptr && mode->currentIndex() == 1);
    if (mode != nullptr) {
      mode->setCurrentIndex(0);
    }
    dialog.accept();
  }));
  const auto rgb_index = MainWindowTestAccess::session_count(window) - 1;
  CHECK(!MainWindowTestAccess::session_cmyk_proof(window, rgb_index));
  CHECK(require_action(window, "imageModeRgbAction")->isChecked());
}

void ui_new_document_color_mode_needs_a_cmyk_profile() {
  NewDocumentSettingsScope scope;
  patchy::ui::app_settings().setValue(QStringLiteral("color/cmykProfile"), QStringLiteral("formula"));
  {
    patchy::ui::MainWindow window;
    show_window(window);
    CHECK(with_new_document_dialog(window, [&](QDialog& dialog) {
      auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("newDocumentColorModeCombo"));
      CHECK(mode != nullptr);
      if (mode != nullptr) {
        auto* model = qobject_cast<QStandardItemModel*>(mode->model());
        CHECK(model != nullptr && model->item(0)->isEnabled() && !model->item(1)->isEnabled());
      }
      dialog.reject();
    }));
  }
  // The next window installs the default (Automatic) space again.
  patchy::ui::app_settings().remove(QStringLiteral("color/cmykProfile"));
  patchy::ui::MainWindow restored;
  show_window(restored);
}

// GitHub issue 363: the preference is on by default (Background plus an empty Paint Layer);
// off, File > New creates the Background alone, selected.
void ui_new_document_empty_layer_preference() {
  NewDocumentSettingsScope scope;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  bool saw_preferences = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesNewDocumentEmptyLayerCheck"));
    CHECK(check != nullptr);
    if (check != nullptr) {
      CHECK(check->isChecked());
      check->setChecked(false);
      if (auto* group = check->parentWidget(); group != nullptr) {
        save_widget_artifact("issue_363_preferences_empty_layer", *group);
      }
    }
    saw_preferences = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_preferences);
  CHECK(!patchy::ui::app_settings().value(QStringLiteral("newDocument/addEmptyLayer"), true).toBool());

  accept_new_document_dialog(200, 100);
  require_action(window, "fileNewAction")->trigger();
  QApplication::processEvents();
  const auto& alone = std::as_const(MainWindowTestAccess::document(window));
  CHECK(alone.layers().size() == 1);
  CHECK(alone.layers().size() == 1 && alone.layers().front().name() == "Background");
  CHECK(alone.active_layer_id() == alone.layers().front().id());
  CHECK(layer_list != nullptr && layer_list->count() == 1);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Background"))->isSelected());

  patchy::ui::app_settings().setValue(QStringLiteral("newDocument/addEmptyLayer"), true);
  accept_new_document_dialog(200, 100);
  require_action(window, "fileNewAction")->trigger();
  QApplication::processEvents();
  CHECK(std::as_const(MainWindowTestAccess::document(window)).layers().size() == 2);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Paint Layer"))->isSelected());
}

// GitHub issue 374: the Name field starts as Untitled-N, drops characters a file name cannot
// hold as they are typed, keeps Unicode, and becomes the tab title and the Save As name.
void ui_new_document_name_reaches_tab_and_save_as() {
  NewDocumentSettingsScope scope;
  CHECK(patchy::ui::sanitized_new_document_name(QStringLiteral("  Logo: v1/2*?.. ")) == QStringLiteral("Logo v12"));
  CHECK(patchy::ui::sanitized_new_document_name(QStringLiteral("<>|")).isEmpty());
  const auto unicode_name = QString::fromUtf8(patchy::test::utf8_string(patchy::test::kUnicodeCombinedStem).c_str());
  CHECK(patchy::ui::sanitized_new_document_name(unicode_name) == unicode_name);

  patchy::ui::MainWindow window;
  show_window(window);
  const auto expected_default = QStringLiteral("Untitled-%1").arg(MainWindowTestAccess::session_count(window) + 1);
  CHECK(with_new_document_dialog(window, [&](QDialog& dialog) {
    auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("newDocumentNameEdit"));
    CHECK(name != nullptr);
    if (name == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(name->text() == expected_default);
    name->clear();
    QTest::keyClicks(name, QStringLiteral("a?b:c"));
    CHECK(name->text() == QStringLiteral("abc"));
    name->setText(QStringLiteral("Poster: ") + unicode_name + QStringLiteral("."));
    save_widget_artifact("issue_374_new_document_name", dialog);
    set_pixel_size(dialog, 90, 60);
    dialog.accept();
  }));
  const auto expected_name = QStringLiteral("Poster ") + unicode_name;
  CHECK(MainWindowTestAccess::active_session_title(window) == expected_name);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr && tabs->tabText(tabs->currentIndex()).contains(expected_name));

  bool saw_save_as = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    const auto selected = dialog->selectedFiles();
    CHECK(!selected.isEmpty() && QFileInfo(selected.first()).fileName() == expected_name + QStringLiteral(".psd"));
    saw_save_as = true;
    dialog->reject();
  });
  require_action(window, "fileSaveAsAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_save_as);

  // An empty name falls back to the default.
  CHECK(with_new_document_dialog(window, [&](QDialog& dialog) {
    auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("newDocumentNameEdit"));
    if (name != nullptr) {
      name->setText(QStringLiteral(" ... "));
    }
    dialog.accept();
  }));
  CHECK(MainWindowTestAccess::active_session_title(window).startsWith(QStringLiteral("Untitled-")));
}

// GitHub issue 374: Save Preset is enabled only for settings no template or saved preset
// holds; it names the preset and adds it to the Saved group, which restores every field on
// the next open and loses the card on Delete. An older Recent entry without a color mode
// loads as RGB.
void ui_new_document_save_preset_and_reload() {
  NewDocumentSettingsScope scope;
  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(with_new_document_dialog(window, [&](QDialog& dialog) {
    auto* save = dialog.findChild<QPushButton*>(QStringLiteral("newDocumentSavePresetButton"));
    auto* saved_chip = dialog.findChild<QToolButton*>(QStringLiteral("newDocumentSavedChip"));
    auto* presets = dialog.findChild<QListWidget*>(QStringLiteral("newDocumentPresetList"));
    auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("newDocumentColorModeCombo"));
    auto* background = dialog.findChild<QComboBox*>(QStringLiteral("newDocumentBackgroundCombo"));
    CHECK(save != nullptr && saved_chip != nullptr && presets != nullptr && mode != nullptr && background != nullptr);
    if (save == nullptr || saved_chip == nullptr || presets == nullptr || mode == nullptr || background == nullptr) {
      dialog.reject();
      return;
    }
    // Default (a template) is selected: nothing to save.
    CHECK(!save->isEnabled());
    CHECK(saved_chip->isHidden());
    set_pixel_size(dialog, 1234, 567);
    CHECK(save->isEnabled());
    mode->setCurrentIndex(1);
    background->setCurrentIndex(background->findText(QStringLiteral("Black")));
    QApplication::processEvents();

    bool named = false;
    QTimer::singleShot(0, [&] {
      auto* prompt = qobject_cast<QInputDialog*>(find_top_level_dialog(QStringLiteral("newDocumentSavePresetDialog")));
      CHECK(prompt != nullptr);
      if (prompt == nullptr) {
        return;
      }
      CHECK(prompt->textValue() == QStringLiteral("1234 x 567 px"));
      prompt->setTextValue(QStringLiteral("My Banner"));
      named = true;
      prompt->accept();
    });
    save->click();
    QApplication::processEvents();
    CHECK(named);
    CHECK(!saved_chip->isHidden() && saved_chip->isChecked());
    CHECK(presets->count() == 1);
    auto* card = card_with_id(*presets, QStringLiteral("user-1"));
    CHECK(card != nullptr && card->text() == QStringLiteral("My Banner") && card->isSelected());
    CHECK(!save->isEnabled());
    save_widget_artifact("issue_374_new_document_saved_preset", dialog);
    // Any change makes the settings custom again; changing back matches the preset.
    background->setCurrentIndex(background->findText(QStringLiteral("White")));
    QApplication::processEvents();
    CHECK(save->isEnabled());
    background->setCurrentIndex(background->findText(QStringLiteral("Black")));
    QApplication::processEvents();
    CHECK(!save->isEnabled());
    dialog.reject();
  }));
  const auto stored = QJsonDocument::fromJson(
                          patchy::ui::app_settings().value(QStringLiteral("newDocument/userPresets")).toString().toUtf8())
                          .array();
  CHECK(stored.size() == 1);
  CHECK(stored.size() == 1 && stored.at(0).toObject().value(QStringLiteral("colorMode")).toString() ==
                                  QStringLiteral("cmykProof"));

  // Reopened: the Saved group (picked last) holds the preset, which restores every field.
  CHECK(with_new_document_dialog(window, [&](QDialog& dialog) {
    auto* saved_chip = dialog.findChild<QToolButton*>(QStringLiteral("newDocumentSavedChip"));
    auto* presets = dialog.findChild<QListWidget*>(QStringLiteral("newDocumentPresetList"));
    auto* width = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
    auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("newDocumentColorModeCombo"));
    auto* background = dialog.findChild<QComboBox*>(QStringLiteral("newDocumentBackgroundCombo"));
    auto* save = dialog.findChild<QPushButton*>(QStringLiteral("newDocumentSavePresetButton"));
    CHECK(saved_chip != nullptr && presets != nullptr && width != nullptr && mode != nullptr && background != nullptr &&
          save != nullptr);
    if (saved_chip == nullptr || presets == nullptr || width == nullptr || mode == nullptr || background == nullptr ||
        save == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(!saved_chip->isHidden() && saved_chip->isChecked());
    auto* card = card_with_id(*presets, QStringLiteral("user-1"));
    CHECK(card != nullptr);
    if (card != nullptr) {
      presets->setCurrentItem(card);
      QApplication::processEvents();
    }
    CHECK(width->value() == 1234);
    CHECK(mode->currentIndex() == 1);
    CHECK(background->currentText() == QStringLiteral("Black"));
    CHECK(!save->isEnabled());
    // Delete (the grid's Delete and Backspace shortcut) removes the card; with no saved
    // presets left the chip hides.
    auto* remove = presets->findChild<QAction*>(QStringLiteral("newDocumentDeleteCurrentPresetAction"));
    CHECK(remove != nullptr);
    if (remove == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(remove->shortcuts().contains(QKeySequence(Qt::Key_Delete)));
    CHECK(remove->shortcuts().contains(QKeySequence(Qt::Key_Backspace)));
    CHECK(remove->shortcutContext() == Qt::WidgetShortcut);
    remove->trigger();
    QApplication::processEvents();
    CHECK(card_with_id(*presets, QStringLiteral("user-1")) == nullptr);
    CHECK(saved_chip->isHidden());
    // The settings stay, now custom, so they can be saved again.
    CHECK(save->isEnabled());
    // Templates are never deleted.
    if (auto* screen_chip = dialog.findChild<QToolButton*>(QStringLiteral("newDocumentScreenChip")); screen_chip) {
      CHECK(screen_chip->isChecked());
      const auto before = presets->count();
      presets->setCurrentRow(0);
      remove->trigger();
      QApplication::processEvents();
      CHECK(presets->count() == before);
    }
    dialog.reject();
  }));
  CHECK(QJsonDocument::fromJson(
            patchy::ui::app_settings().value(QStringLiteral("newDocument/userPresets")).toString().toUtf8())
            .array()
            .isEmpty());

  // A Recent entry from before Color Mode existed opens as RGB.
  patchy::ui::app_settings().setValue(
      QStringLiteral("newDocument/recent"),
      QStringLiteral(R"([{"w":300,"h":200,"ppi":72,"unit":"px","resolutionUnit":0,"background":"#ffffffff","bits":8,"artboards":false}])"));
  CHECK(with_new_document_dialog(window, [&](QDialog& dialog) {
    auto* width = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
    auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("newDocumentColorModeCombo"));
    CHECK(width != nullptr && width->value() == 300);
    CHECK(mode != nullptr && mode->currentIndex() == 0);
    dialog.reject();
  }));
}

// app.newDocument's name and colorMode options.
void ui_script_new_document_options() {
  NewDocumentSettingsScope scope;
  patchy::ui::MainWindow window;
  show_window(window);
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("new-document-options");
  (void)host.run_source(QStringLiteral(R"JS(
    var plain = app.newDocument(20, 10);
    if (plain.name !== 'Untitled') throw Error('default name ' + plain.name);
    if (plain.cmykProof) throw Error('plain document in CMYK Proof');
    var named = app.newDocument(20, 10, {name: 'Café: art?', colorMode: 'cmykProof'});
    if (named.name !== 'Café art') throw Error('name ' + named.name);
    if (!named.cmykProof) throw Error('not CMYK Proof');
    var refused = 0;
    var before = app.documents.length;
    try { app.newDocument(20, 10, {colorMode: 'lab'}); } catch (e) { refused++; }
    try { app.newDocument(20, 10, {name: '<>'}); } catch (e) { refused++; }
    if (refused !== 2) throw Error('refused ' + refused);
    if (app.documents.length !== before) throw Error('a refused call created a document');
    console.log('done');
  )JS"),
                        std::move(options));
  wait_for_run_end(host);
  if (host.last_run_had_error()) {
    for (const auto& line : host.message_backlog()) std::cerr << line.toStdString() << '\n';
  }
  CHECK(!host.last_run_had_error());
}

}  // namespace

std::vector<patchy::test::TestCase> new_document_options_ui_tests() {
  return {
      {"ui_new_document_color_mode_creates_cmyk_proof", ui_new_document_color_mode_creates_cmyk_proof},
      {"ui_new_document_color_mode_needs_a_cmyk_profile", ui_new_document_color_mode_needs_a_cmyk_profile},
      {"ui_new_document_empty_layer_preference", ui_new_document_empty_layer_preference},
      {"ui_new_document_name_reaches_tab_and_save_as", ui_new_document_name_reaches_tab_and_save_as},
      {"ui_new_document_save_preset_and_reload", ui_new_document_save_preset_and_reload},
      {"ui_new_document_templates_show_their_unit", ui_new_document_templates_show_their_unit},
      {"ui_script_new_document_options", ui_script_new_document_options},
  };
}
