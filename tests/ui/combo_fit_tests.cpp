// Combo boxes that show every entry whole, closed and open (GitHub issue 655): Edit > Fill's
// Contents list and the New Layer dialog's Fill list (FillContentsField), and the generated
// choice lists of the GEGL filter dialogs (FilterParameterPanel) and the G'MIC library
// (GmicParameterPanel). All of them size through fit_combo_to_entries (ui/dialog_utils.hpp).
// The suite runs under Fusion, Patchy's style on Linux, where an open list is a menu whose rows
// need more room than the box: the case the issue's screenshot shows.

#include "combo_fit_checks.hpp"
#include "filters/gmic/gmic_catalog.hpp"
#include "test_harness.hpp"
#include "ui/fill_dialog.hpp"
#include "ui/filter_parameter_panel.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/gegl_filters.hpp"
#include "ui/gmic_parameter_panel.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/new_layer_dialog.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QImage>
#include <QVBoxLayout>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace patchy::test::ui;

void report(const std::vector<std::string>& problems) {
  for (const auto& line : problems) {
    std::fprintf(stderr, "  %s\n", line.c_str());
  }
}

// GitHub issue 655: Edit > Fill's Contents list and Mode list show every entry whole, closed
// and open, in English and the longer German and Russian. Captures the open Contents list.
void ui_fill_dialog_contents_fit_their_entries() {
  LanguageRestorer language_restorer;
  patchy::ui::MainWindow window;
  show_window(window);
  std::vector<std::string> problems;
  int checked = 0;
  for (const auto* language : {"en", "de", "ru"}) {
    CHECK(patchy::ui::LocalizationManager::instance().set_language(QString::fromLatin1(language), false));
    QApplication::processEvents();
    when_dialog_visible(QStringLiteral("patchyFillDialog"), [&problems, &checked, language](QDialog& shown) {
      auto* dialog = &shown;
      settle_combo_layout();
      for (const auto* name : {"editFillContentsCombo", "editFillModeCombo"}) {
        auto* combo = dialog->findChild<QComboBox*>(QLatin1String(name));
        CHECK(combo != nullptr);
        if (combo == nullptr) {
          continue;
        }
        ++checked;
        QImage open_list;
        const bool capture = std::string(name) == "editFillContentsCombo" && std::string(language) != "ru";
        const auto found = combo_entry_fit_problems(*combo, std::string(language) + " / " + name, true, dialog,
                                                    capture ? &open_list : nullptr);
        problems.insert(problems.end(), found.begin(), found.end());
        if (capture && !open_list.isNull()) {
          save_image_artifact(std::string("issue655_fill_contents_open_") + language, open_list);
        }
      }
      dialog->reject();
    });
    const auto settings = patchy::ui::request_fill_settings(&window, QColor(Qt::black), QColor(Qt::white));
    CHECK(!settings.has_value());
  }
  report(problems);
  CHECK(problems.empty());
  CHECK(checked == 6);
}

// GitHub issue 654: the New Layer dialog's Fill list (Edit > Fill's, with Transparent and the
// mode's neutral color) shows every entry whole, closed and open, with the neutral entry listed.
void ui_new_layer_fill_list_fits_its_entries() {
  LanguageRestorer language_restorer;
  patchy::ui::MainWindow window;
  show_window(window);
  std::vector<std::string> problems;
  int checked = 0;
  for (const auto* language : {"en", "de", "ru"}) {
    CHECK(patchy::ui::LocalizationManager::instance().set_language(QString::fromLatin1(language), false));
    QApplication::processEvents();
    when_dialog_visible(QStringLiteral("newLayerDialog"), [&problems, &checked, language](QDialog& shown) {
      auto* dialog = &shown;
      auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("newLayerModeCombo"));
      auto* fill = dialog->findChild<QComboBox*>(QStringLiteral("newLayerFillCombo"));
      CHECK(mode != nullptr && fill != nullptr);
      if (mode == nullptr || fill == nullptr) {
        dialog->reject();
        return;
      }
      mode->setCurrentIndex(mode->findData(static_cast<int>(patchy::BlendMode::Overlay)));
      settle_combo_layout();
      CHECK(fill->findData(QStringLiteral("neutral")) >= 0);
      ++checked;
      QImage open_list;
      const bool capture = std::string(language) == "en";
      const auto found = combo_entry_fit_problems(*fill, std::string(language) + " / newLayerFillCombo", true,
                                                  dialog, capture ? &open_list : nullptr);
      problems.insert(problems.end(), found.begin(), found.end());
      if (capture && !open_list.isNull()) {
        save_image_artifact("issue654_new_layer_fill_open", open_list);
      }
      dialog->reject();
    });
    const auto settings = patchy::ui::request_new_layer_settings(&window, QStringLiteral("Layer 1"), false,
                                                                 patchy::BitDepth::UInt8, {}, QColor(Qt::black),
                                                                 QColor(Qt::white));
    CHECK(!settings.has_value());
  }
  report(problems);
  CHECK(problems.empty());
  CHECK(checked == 3);
}

// Seth's follow-up to GitHub issue 655 ("check all the gimp filters"): every choice list in
// every offered GEGL filter's settings shows its entries whole, closed and open, laid out as
// the filter dialog lays them out (two columns from kFilterDialogTwoColumnControls). Lists the
// lists that do not. Captures Emboss's Emboss type list open. Skips without the bundle.
void ui_gegl_filter_choice_lists_fit_their_entries() {
  if (patchy::ui::bundled_gegl_library().isEmpty()) {
    patchy::test::skip("this build has no GEGL of its own");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::ensure_gegl_filters_scanned(window);
  const auto registry =
      patchy::ui::MainWindowTestAccess::gegl_filter_registry(window, QStringLiteral("gegl.gaussian-blur"));
  CHECK(registry != nullptr);
  if (registry == nullptr) {
    return;
  }
  std::vector<std::string> problems;
  int checked = 0;
  for (const auto& filter : registry->filters()) {
    QDialog host(&window);
    auto* layout = new QVBoxLayout(&host);
    auto* panel = new patchy::ui::FilterParameterPanel(&host);
    patchy::ui::FilterParameterPanelOptions options;
    options.build_companions = true;
    options.two_columns_from = patchy::ui::kFilterDialogTwoColumnControls;
    panel->rebuild(patchy::ui::filter_dialog_spec_for(filter), registry->default_invocation(filter.identifier),
                   options);
    layout->addWidget(panel);
    const auto combos = panel->findChildren<QComboBox*>();
    if (combos.isEmpty()) {
      continue;
    }
    host.show();
    settle_combo_layout();
    for (auto* combo : combos) {
      if (!combo->isVisibleTo(&host) || combo->count() == 0) {
        continue;
      }
      ++checked;
      const auto where = filter.identifier + " / " + combo->objectName().toStdString();
      const bool capture = filter.identifier == "gegl.emboss" &&
                           combo->objectName() == QStringLiteral("geglParam_typeCombo");
      QImage open_list;
      const auto found = combo_entry_fit_problems(*combo, where, true, &host, capture ? &open_list : nullptr);
      problems.insert(problems.end(), found.begin(), found.end());
      if (capture && !open_list.isNull()) {
        save_image_artifact("issue655_gegl_emboss_type_open", open_list);
      }
    }
    host.hide();
  }
  report(problems);
  CHECK(problems.empty());
  CHECK(checked > 20);
}

// The whole vendored G'MIC library, read from the source tree.
const patchy::gmic::Catalog& whole_gmic_catalog() {
  static const patchy::gmic::Catalog catalog = [] {
    std::ifstream in(patchy::test::source_root_path() / "src" / "filters" / "gmic" / "gmic_stdlib.gmic",
                     std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return patchy::gmic::parse_catalog(text.str());
  }();
  return catalog;
}

// The same for every choice list of every G'MIC filter (GmicParameterPanel), closed and open.
void ui_gmic_choice_lists_fit_their_entries() {
  CHECK(whole_gmic_catalog().filters.size() > 400U);
  patchy::ui::MainWindow window;
  show_window(window);
  QDialog host(&window);
  auto* layout = new QVBoxLayout(&host);
  auto* panel = new patchy::ui::GmicParameterPanel(&host);
  layout->addWidget(panel);
  host.show();
  std::vector<std::string> problems;
  int checked = 0;
  for (const auto& filter : whole_gmic_catalog().filters) {
    panel->set_filter(filter);
    const auto combos = panel->findChildren<QComboBox*>();
    if (combos.isEmpty()) {
      continue;
    }
    host.adjustSize();
    settle_combo_layout();
    for (auto* combo : combos) {
      if (!combo->isVisibleTo(&host) || combo->count() == 0) {
        continue;
      }
      ++checked;
      const auto found = combo_entry_fit_problems(*combo, filter.id + " / list " + std::to_string(checked), true);
      problems.insert(problems.end(), found.begin(), found.end());
    }
  }
  report(problems);
  CHECK(problems.empty());
  CHECK(checked > 100);
}

}  // namespace

std::vector<patchy::test::TestCase> combo_fit_tests() {
  return {
      {"ui_fill_dialog_contents_fit_their_entries", ui_fill_dialog_contents_fit_their_entries},
      {"ui_new_layer_fill_list_fits_its_entries", ui_new_layer_fill_list_fits_its_entries},
      {"ui_gegl_filter_choice_lists_fit_their_entries", ui_gegl_filter_choice_lists_fit_their_entries},
      {"ui_gmic_choice_lists_fit_their_entries", ui_gmic_choice_lists_fit_their_entries},
  };
}
