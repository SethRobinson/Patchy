#include <QStatusBar>
#include "ui/theme_manager.hpp"
#include "synthetic_dng.hpp"
#include <QDialog>
#include <QEventLoop>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include "ui_test_access.hpp"
// The G'MIC Filter Library (docs/gmic.md): the generated parameter panel over
// every filter the vendored library declares, the browser dialog (tree, search,
// selection, reset), and, in builds with the engine, applying a filter to the
// active layer with a selection and undo.
#include "core/document.hpp"
#include "filters/gmic/gmic_catalog.hpp"
#include "filters/gmic/gmic_engine.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/color_panel.hpp"
#include "ui/gmic_filter_library_dialog.hpp"
#include "ui/gmic_library.hpp"
#include "ui/gmic_parameter_panel.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/script_engine.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include <QAction>
#include <QPalette>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QColor>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;
namespace gmic = patchy::gmic;

// The whole vendored library, read from the source tree so these tests run in
// builds without the engine too.
const gmic::Catalog& whole_catalog() {
  static const gmic::Catalog catalog = [] {
    std::ifstream in(patchy::test::source_root_path() / "src" / "filters" / "gmic" / "gmic_stdlib.gmic",
                     std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return gmic::parse_catalog(text.str());
  }();
  return catalog;
}

// Equal as G'MIC arguments: numbers compare by value ("0.50" is "0.5").
bool same_value(const std::string& lhs, const std::string& rhs) {
  if (lhs == rhs) {
    return true;
  }
  const auto left = QString::fromStdString(lhs).split(QLatin1Char(','));
  const auto right = QString::fromStdString(rhs).split(QLatin1Char(','));
  if (left.size() != right.size()) {
    return false;
  }
  for (qsizetype index = 0; index < left.size(); ++index) {
    bool left_ok = false;
    bool right_ok = false;
    const double a = left[index].toDouble(&left_ok);
    const double b = right[index].toDouble(&right_ok);
    if (!left_ok || !right_ok || std::fabs(a - b) > 0.0006) {
      return false;
    }
  }
  return true;
}

void ui_gmic_parameter_panel_builds_every_filter() {
  CHECK(whole_catalog().filters.size() > 400U);
  patchy::ui::GmicParameterPanel panel;
  int mismatches = 0;
  for (const auto& filter : whole_catalog().filters) {
    panel.set_filter(filter);
    const auto values = panel.values();
    CHECK(values.size() == filter.value_count());
    // A fresh panel calls the filter exactly as its declaration does, except
    // where a declared default lies outside the declared range.
    std::size_t at = 0;
    for (const auto& parameter : filter.parameters) {
      if (!parameter.carries_value() || at >= values.size()) {
        continue;
      }
      const auto& value = values[at++];
      const bool ranged =
          parameter.type == gmic::ParameterType::Float || parameter.type == gmic::ParameterType::Int;
      const double declared = QString::fromStdString(parameter.default_value).toDouble();
      const bool in_range = !ranged || (declared >= std::min(parameter.minimum, parameter.maximum) &&
                                        declared <= std::max(parameter.minimum, parameter.maximum));
      if (in_range && parameter.type != gmic::ParameterType::Point && !same_value(value, parameter.default_value)) {
        ++mismatches;
        std::cerr << "  " << filter.id << " / " << parameter.name << ": panel '" << value << "', declared '"
                  << parameter.default_value << "'\n";
      }
    }
  }
  CHECK(mismatches == 0);
}

void ui_gmic_parameter_panel_reads_and_restores_values() {
  const auto catalog = gmic::parse_catalog(
      "#@gui Demo : fx_demo\n"
      "#@gui : note = note{\"<span color=\"#EE5500\"><b>Note</b></span>\\nsecond line\"}, sep = separator()\n"
      "#@gui : Amount = float(0.5,0,1), Count = int(3,1,9), On = bool(1)\n"
      "#@gui : Mode = choice(1,\"A\",\"B\",\"C\"), Tint = color(10,20,30,40), Label = text(\"hi\")\n"
      "#@gui : Center = point(25,75), Hidden = value(7)_0, Go = button()\n");
  CHECK(catalog.filters.size() == 1U);
  if (catalog.filters.empty()) {
    return;
  }
  const auto& filter = catalog.filters.front();
  patchy::ui::GmicParameterPanel panel;
  int changes = 0;
  panel.set_changed_callback([&changes](bool) { ++changes; });
  panel.set_filter(filter);
  CHECK(changes == 0);  // building and resetting are not edits
  CHECK(gmic::command_line(filter, panel.values(), false) == "fx_demo 0.5,3,1,1,10,20,30,40,\"hi\",25,75,\"7\",0");

  auto* note = panel.findChild<QLabel*>(QStringLiteral("gmicParameter0"));
  CHECK(note != nullptr);
  if (note != nullptr) {
    CHECK(note->text().contains(QStringLiteral("style=\"color:#EE5500\"")));
    CHECK(note->text().contains(QStringLiteral("<br>")));
  }

  auto* amount = panel.findChild<QDoubleSpinBox*>(QStringLiteral("gmicParameter2Spin"));
  auto* slider = panel.findChild<QSlider*>(QStringLiteral("gmicParameter2Slider"));
  CHECK(amount != nullptr && slider != nullptr);
  if (amount == nullptr || slider == nullptr) {
    return;
  }
  slider->setValue(slider->maximum());
  CHECK(amount->value() == 1.0);
  CHECK(changes > 0);
  panel.set_values({"0.25", "7", "0", "2", "1,2,3,4", "\"a \\\"b\\\"\"", "10,90", "\"7\"", "0"});
  CHECK(gmic::command_line(filter, panel.values(), false) == "fx_demo 0.25,7,0,2,1,2,3,4,\"a \\\"b\\\"\",10,90,\"7\",0");
  CHECK(slider->value() == slider->maximum() / 4);

  auto* go = panel.findChild<QPushButton*>(QStringLiteral("gmicParameter10"));
  CHECK(go != nullptr);
  if (go != nullptr) {
    go->click();
    CHECK(panel.values().back() == "1");
    // A button is a one-shot flag: once a run has read it, it goes back without a change report.
    auto remembered = panel.values();
    const int reported = changes;
    panel.release_buttons();
    CHECK(panel.values().back() == "0");
    CHECK(changes == reported);
    CHECK(remembered.back() == "1");
    gmic::release_button_values(filter, remembered);
    CHECK(remembered.back() == "0");
    CHECK(remembered.front() == panel.values().front());
  }
  panel.reset_to_defaults();
  CHECK(gmic::command_line(filter, panel.values(), false) == "fx_demo 0.5,3,1,1,10,20,30,40,\"hi\",25,75,\"7\",0");

  // A marker dragged along one axis reports every move. The canvas sends positions finer
  // than the fields keep, so the axis that stays put does not match its field exactly.
  panel.set_point(0, QPointF(40.03, 37.46));
  int before = changes;
  panel.set_point(0, QPointF(55.03, 37.46));
  CHECK(changes == before + 1);
  before = changes;
  panel.set_point(0, QPointF(55.03, 61.02));
  CHECK(changes == before + 1);
  CHECK(panel.points().size() == 1U && panel.points().front() == QPointF(55.0, 61.0));
}

void ui_gmic_library_dialog_browses_searches_and_resets() {
  patchy::ui::GmicFilterLibraryDialog dialog(whole_catalog(), QStringLiteral("4.0.5"));
  CHECK(dialog.visible_filter_count() == static_cast<int>(whole_catalog().filters.size()));
  CHECK(!dialog.choice().has_value());
  auto* title = dialog.findChild<QLabel*>(QStringLiteral("gmicFilterTitle"));
  auto* credit = dialog.findChild<QLabel*>(QStringLiteral("gmicCreditLabel"));
  auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("gmicFilterTree"));
  auto* reset = dialog.findChild<QPushButton*>(QStringLiteral("gmicFilterResetButton"));
  CHECK(title != nullptr && credit != nullptr && tree != nullptr && reset != nullptr);
  if (title == nullptr || credit == nullptr || tree == nullptr || reset == nullptr) {
    return;
  }
  CHECK(credit->text().contains(QStringLiteral("4.0.5")));
  CHECK(tree->topLevelItemCount() > 10);  // the library's folders
  CHECK(!reset->isEnabled());

  dialog.set_search_text(QStringLiteral("sepia"));
  CHECK(dialog.visible_filter_count() >= 1);
  CHECK(dialog.visible_filter_count() < 10);
  dialog.set_search_text(QStringLiteral("colors sepia"));  // every word, across folder and name
  CHECK(dialog.visible_filter_count() == 1);
  dialog.set_search_text(QStringLiteral("no such filter anywhere"));
  CHECK(dialog.visible_filter_count() == 0);
  dialog.set_search_text(QString());
  CHECK(dialog.visible_filter_count() == static_cast<int>(whole_catalog().filters.size()));

  CHECK(!dialog.select_filter("fx_no_such_filter"));
  CHECK(dialog.select_filter("fx_sepia"));
  CHECK(title->text() == QStringLiteral("Sepia"));
  CHECK(reset->isEnabled());
  auto choice = dialog.choice();
  CHECK(choice.has_value());
  if (!choice.has_value()) {
    return;
  }
  CHECK(choice->filter_id == "fx_sepia");
  CHECK(choice->values == (std::vector<std::string>{"0", "0", "0"}));

  auto* brightness = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("gmicParameter2Spin"));
  CHECK(brightness != nullptr);
  if (brightness != nullptr) {
    brightness->setValue(25.0);
    CHECK(dialog.choice()->values.front() == "25");
    reset->click();
    CHECK(dialog.choice()->values.front() == "0");
  }
  CHECK(dialog.select_filter("fx_sepia", {"10", "-20", "30"}));
  CHECK(dialog.choice()->values == (std::vector<std::string>{"10", "-20", "30"}));

  // The Input layers choice starts from what each filter declares and can be restored.
  auto* input = dialog.findChild<QComboBox*>(QStringLiteral("gmicInputLayersCombo"));
  CHECK(input != nullptr && input->count() == 6);
  CHECK(dialog.choice()->input_mode == '.');
  CHECK(dialog.select_filter("fx_blend_average_all"));
  CHECK(dialog.choice()->input_mode == '*');
  CHECK(dialog.select_filter("fx_sepia"));
  CHECK(dialog.choice()->input_mode == '.');
  CHECK(dialog.select_filter("fx_sepia", {}, 'v'));
  CHECK(dialog.choice()->input_mode == 'v');

  // Enter in the search field picks the first match. It must not reach OK, the default
  // button once a filter is selected, which would apply the filter.
  auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("gmicFilterSearchEdit"));
  CHECK(search != nullptr);
  if (search == nullptr) {
    return;
  }
  dialog.show();
  QApplication::processEvents();
  dialog.set_search_text(QStringLiteral("blur angular"));
  search->setFocus();
  QTest::keyClick(search, Qt::Key_Return);
  CHECK(dialog.isVisible());
  CHECK(dialog.result() != QDialog::Accepted);
  CHECK(dialog.choice().has_value() && dialog.choice()->filter_id == "fx_blur_angular");
}

// The selected filter in the folder tree stays readable in both schemes. The stylesheet sets
// the row's text color as well as its background: left to the platform palette, the text
// was white on GNOME and KDE, light on light under the Light scheme.
void ui_gmic_filter_tree_selection_is_readable_in_both_schemes() {
  ColorSchemeRestorer restore_scheme;
  for (const auto preference : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer::apply(preference);
    patchy::ui::MainWindow window;
    show_window(window);
    patchy::ui::GmicFilterLibraryDialog dialog(whole_catalog(), QStringLiteral("4.0.5"), &window);
    CHECK(dialog.select_filter("fx_sepia"));
    dialog.show();
    QApplication::processEvents();
    auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("gmicFilterTree"));
    CHECK(tree != nullptr && tree->currentItem() != nullptr);
    if (tree == nullptr) {
      continue;
    }
    const auto background = tree->palette().color(QPalette::Active, QPalette::Highlight);
    const auto text = tree->palette().color(QPalette::Active, QPalette::HighlightedText);
    // rgba(): QColor's == also compares the color spec the stylesheet happened to produce.
    CHECK(background.rgba() == patchy::ui::theme().list_selection_bg.rgba());
    CHECK(text.rgba() == patchy::ui::theme().list_selection_text.rgba());
    CHECK(std::abs(text.lightness() - background.lightness()) >= 90);
    dialog.close();
  }
}

// Visual QA artifacts: the dialog as the application styles it, for a few filters
// with different control mixes (test-artifacts/gmic/*.png).
void ui_gmic_library_dialog_renders_in_the_app_theme() {
  patchy::ui::MainWindow window;
  show_window(window);
  ensure_artifact_dir();
  CHECK(QDir().mkpath(QDir::current().filePath(QStringLiteral("test-artifacts/gmic"))));
  for (const char* id : {"fx_sepia", "fx_polaroid", "fx_vignette", "fx_blur_dof", "fx_frame_painting"}) {
    if (whole_catalog().find(id) == nullptr) {
      continue;
    }
    patchy::ui::GmicFilterLibraryDialog dialog(whole_catalog(), QStringLiteral("4.0.5"), &window);
    CHECK(dialog.select_filter(id));
    dialog.show();
    QApplication::processEvents();
    const auto image = dialog.grab().toImage();
    CHECK(!image.isNull() && image.width() > 600 && image.height() > 400);
    CHECK(image.save(QDir::current().filePath(QStringLiteral("test-artifacts/gmic/%1.png").arg(QLatin1String(id)))));
    dialog.close();
  }
  // The result notes banner and the About G'MIC box.
  patchy::ui::GmicFilterLibraryDialog dialog(whole_catalog(), QStringLiteral("4.0.5"), &window);
  CHECK(dialog.select_filter("fx_crt_scanlines"));
  dialog.set_result_notes({QStringLiteral("Changes the layer size from 200 x 150 to 800 x 600 pixels."),
                           QStringLiteral("Part of the layer will lie outside the canvas. It is kept, and the Move "
                                          "tool brings it into view.")});
  dialog.show();
  QApplication::processEvents();
  CHECK(dialog.grab().toImage().save(QDir::current().filePath(QStringLiteral("test-artifacts/gmic/notes.png"))));
  bool captured = false;
  QTimer::singleShot(0, &dialog, [&dialog, &captured] {
    auto* about = dialog.findChild<QDialog*>(QStringLiteral("gmicAboutDialog"));
    CHECK(about != nullptr);
    if (about != nullptr) {
      QApplication::processEvents();
      captured = about->grab().toImage().save(QDir::current().filePath(QStringLiteral("test-artifacts/gmic/about.png")));
      about->accept();
    }
  });
  dialog.show_about_gmic();
  CHECK(captured);
  dialog.close();
}

QColor layer_pixel(patchy::ui::MainWindow& window, int x, int y) {
  auto& document = MainWindowTestAccess::document(window);
  const auto active = document.active_layer_id();
  const auto* layer = active.has_value() ? document.find_layer(*active) : nullptr;
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return {};
  }
  const auto& pixels = std::as_const(*layer).pixels();
  const auto* px = pixels.pixel(x - layer->bounds().x, y - layer->bounds().y);
  return QColor(px[0], px[1], px[2]);
}

bool run_script(patchy::ui::MainWindow& window, const QString& source) {
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("gmic-test");
  (void)host.run_source(source, std::move(options));
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 60000) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  return !host.last_run_had_error();
}

// Runs `drive` on the library dialog once it is open (the command blocks in the
// dialog's own event loop until `drive` accepts or rejects it).
template <typename Drive>
void run_library_dialog(patchy::ui::MainWindow& window, Drive drive) {
  bool driven = false;
  QTimer::singleShot(0, &window, [&window, &driven, drive] {
    auto* dialog = window.findChild<patchy::ui::GmicFilterLibraryDialog*>();
    CHECK(dialog != nullptr);
    if (dialog != nullptr) {
      driven = true;
      drive(*dialog);
      if (dialog->isVisible()) {
        dialog->reject();
      }
    }
  });
  require_action(window, "filterGmicLibraryAction")->trigger();
  QApplication::processEvents();
  CHECK(driven);
}

template <typename Predicate>
bool wait_until(Predicate predicate, int timeout_ms = 30000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout_ms) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  return predicate();
}

// The dialog previews on the real layer from a background run: picking a filter
// returns at once, the result arrives later inside the selection only, Preview
// off puts the original back, and OK commits what was previewed as one undo step.
// A color swatch previews every color picked while the picker is still open (coalesced
// like a slider drag), and Cancel in the picker puts the original color back.
void ui_gmic_color_swatch_previews_while_picking() {
  // The first filter with a color that refreshes the preview, and where its value sits.
  const gmic::Filter* filter = nullptr;
  const gmic::Parameter* color_parameter = nullptr;
  std::size_t value_index = 0;
  for (const auto& candidate : whole_catalog().filters) {
    std::size_t index = 0;
    for (const auto& parameter : candidate.parameters) {
      if (parameter.type == gmic::ParameterType::Color && parameter.updates_preview) {
        filter = &candidate;
        color_parameter = &parameter;
        value_index = index;
        break;
      }
      index += parameter.carries_value() ? 1U : 0U;
    }
    if (filter != nullptr) {
      break;
    }
  }
  CHECK(filter != nullptr);
  if (filter == nullptr) {
    return;
  }
  patchy::ui::GmicFilterLibraryDialog dialog(whole_catalog(), QStringLiteral("4.0.5"));
  CHECK(dialog.select_filter(filter->id));
  dialog.show();
  QApplication::processEvents();
  int requests = 0;
  dialog.set_preview_callback([&requests](const patchy::ui::GmicPreviewRequest&) { ++requests; });
  QPushButton* swatch = nullptr;
  for (auto* button : dialog.findChildren<QPushButton*>()) {
    if (button->accessibleName() == QString::fromStdString(color_parameter->name)) {
      swatch = button;
    }
  }
  CHECK(swatch != nullptr);
  if (swatch == nullptr) {
    return;
  }
  const auto value = [&dialog, value_index] {
    const auto choice = dialog.choice();
    return choice.has_value() && value_index < choice->values.size() ? choice->values[value_index] : std::string();
  };
  const auto original = value();
  bool picked = false;
  QTimer::singleShot(0, &dialog, [&] {
    const int before = requests;
    CHECK(patchy::ui::apply_color_to_open_color_picker(QColor(10, 200, 30)));
    CHECK(value().rfind("10,200,30", 0) == 0);
    CHECK(wait_until([&] { return requests > before; }, 5000));
    picked = true;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("patchyColorDialog") && widget->isVisible()) {
        qobject_cast<QDialog*>(widget)->reject();
      }
    }
  });
  swatch->click();
  CHECK(picked);
  CHECK(value() == original);
}

void ui_gmic_library_dialog_previews_on_the_layer_and_commits() {
  if (!gmic::engine_available()) {
    patchy::test::skip("build without the G'MIC engine (PATCHY_ENABLE_GMIC off)");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  const QColor red(0xc0, 0x30, 0x20);
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.fillRect(0, 0, 200, 200, '#c03020');")));
  auto& document = MainWindowTestAccess::document(window);
  patchy::PixelBuffer mask(document.width(), document.height(), patchy::PixelFormat::gray8());
  for (int y = 0; y < mask.height(); ++y) {
    auto row = mask.row(y);
    for (int x = 0; x < mask.width(); ++x) {
      row[static_cast<std::size_t>(x)] = (x < 100 && y < 100) ? 255 : 0;
    }
  }
  canvas->replace_selection_from_grayscale(mask, QStringLiteral("Square selection"));

  QColor previewed;
  // The canvas badge carries a running preview's percentage and drops it when the run ends.
  CHECK(canvas->preview_render_progress() == -1);
  canvas->begin_preview_render();
  const QString plain_badge = canvas->preview_render_message();
  canvas->set_preview_render_progress(42);
  CHECK(canvas->preview_render_message().startsWith(plain_badge));
  CHECK(canvas->preview_render_message().contains(QStringLiteral("42")));
  canvas->set_preview_render_progress(250);
  CHECK(canvas->preview_render_progress() == 100);
  canvas->end_preview_render();
  CHECK(canvas->preview_render_progress() == -1);
  CHECK(canvas->preview_render_message() == plain_badge);

  run_library_dialog(window, [&](patchy::ui::GmicFilterLibraryDialog& dialog) {
    CHECK(dialog.preview_enabled());
    // Non-modal like the other filter dialogs: the canvas can be zoomed while it is open.
    CHECK(!dialog.isModal() && dialog.windowModality() == Qt::NonModal);
    const double zoom_before = canvas->zoom();
    auto* zoom_in = require_action(window, "viewZoomInAction");
    CHECK(zoom_in->isEnabled());
    zoom_in->trigger();
    QApplication::processEvents();
    CHECK(canvas->zoom() > zoom_before);
    CHECK(dialog.select_filter("fx_sepia"));
    // Nothing ran on this thread: the layer is untouched until the worker reports.
    CHECK(layer_pixel(window, 50, 50) == red);
    CHECK(wait_until([&] { return layer_pixel(window, 50, 50) != red; }));
    const auto sepia = layer_pixel(window, 50, 50);
    CHECK(std::abs(sepia.red() - 116) <= 1 && std::abs(sepia.green() - 85) <= 1 && std::abs(sepia.blue() - 45) <= 1);
    CHECK(layer_pixel(window, 150, 150) == red);

    auto* preview = dialog.findChild<QCheckBox*>(QStringLiteral("gmicPreviewCheck"));
    CHECK(preview != nullptr);
    if (preview == nullptr) {
      return;
    }
    // Preview off and on only hides and shows the result: both take effect at once, with
    // no event processed in between, because nothing is rendered again.
    for (int flip = 0; flip < 4; ++flip) {
      preview->setChecked(false);
      CHECK(layer_pixel(window, 50, 50) == red);
      preview->setChecked(true);
      CHECK(layer_pixel(window, 50, 50) == sepia);
    }

    // A changed value re-renders; the stale picture is replaced.
    auto* brightness = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("gmicParameter2Spin"));
    CHECK(brightness != nullptr);
    if (brightness == nullptr) {
      return;
    }
    brightness->setValue(40.0);
    CHECK(wait_until([&] { return layer_pixel(window, 50, 50) != sepia && layer_pixel(window, 50, 50) != red; }));
    previewed = layer_pixel(window, 50, 50);
    CHECK(previewed.red() > sepia.red());

    // Switching filters while one is still rendering starts the new one at once, and the
    // stale run, however long it takes to stop, never replaces the newer result.
    CHECK(dialog.select_filter("fx_brushify"));
    CHECK(dialog.select_filter("fx_sepia", {"40", "0", "0"}));
    CHECK(wait_until([&] { return layer_pixel(window, 50, 50) == previewed; }));
    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < 3000) {
      QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    CHECK(layer_pixel(window, 50, 50) == previewed);
    dialog.accept();
  });
  CHECK(previewed.isValid());
  CHECK(layer_pixel(window, 50, 50) == previewed);
  CHECK(layer_pixel(window, 150, 150) == red);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(layer_pixel(window, 50, 50) == red);

  // Cancel after a preview leaves the layer as it was, and the dialog reopens on
  // the filter and values accepted last time.
  run_library_dialog(window, [&](patchy::ui::GmicFilterLibraryDialog& dialog) {
    const auto choice = dialog.choice();
    CHECK(choice.has_value() && choice->filter_id == "fx_sepia" && choice->values.front() == "40");
    CHECK(wait_until([&] { return layer_pixel(window, 50, 50) == previewed; }));
    dialog.reject();
  });
  CHECK(layer_pixel(window, 50, 50) == red);
}
void ui_gmic_filter_applies_inside_selection_and_undoes() {
  patchy::ui::MainWindow window;
  show_window(window);
  // The command exists exactly in builds that have the engine.
  CHECK((window.findChild<QAction*>(QStringLiteral("filterGmicLibraryAction")) != nullptr) ==
        gmic::engine_available());
  if (!gmic::engine_available()) {
    CHECK(!patchy::ui::gmic_filters_available());
    return;
  }
  CHECK(patchy::ui::gmic_filters_available());
  CHECK(patchy::ui::gmic_library().catalog.find("fx_sepia") != nullptr);
  // An excluded filter is not offered, whatever the library declares.
  CHECK(patchy::ui::gmic_library().catalog.find("fx_inpaint_patch") == nullptr);
  CHECK(whole_catalog().find("fx_inpaint_patch") != nullptr);

  auto* canvas = require_canvas(window);
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.fillRect(0, 0, 200, 200, '#c03020');")));
  CHECK(layer_pixel(window, 50, 50) == QColor(0xc0, 0x30, 0x20));

  CHECK(!window.apply_gmic_filter("fx_inpaint_patch", {}));
  CHECK(layer_pixel(window, 50, 50) == QColor(0xc0, 0x30, 0x20));

  auto& document = MainWindowTestAccess::document(window);
  patchy::PixelBuffer mask(document.width(), document.height(), patchy::PixelFormat::gray8());
  for (int y = 0; y < mask.height(); ++y) {
    auto row = mask.row(y);
    for (int x = 0; x < mask.width(); ++x) {
      row[static_cast<std::size_t>(x)] = (x < 100 && y < 100) ? 255 : 0;
    }
  }
  canvas->replace_selection_from_grayscale(mask, QStringLiteral("Square selection"));
  CHECK(canvas->has_selection());

  CHECK(window.apply_gmic_filter("fx_sepia", {}));
  // G'MIC's Sepia of this red (docs/gmic.md pins 116.1, 85.4, 45.1).
  const auto inside = layer_pixel(window, 50, 50);
  CHECK(std::abs(inside.red() - 116) <= 1 && std::abs(inside.green() - 85) <= 1 &&
        std::abs(inside.blue() - 45) <= 1);
  CHECK(layer_pixel(window, 150, 150) == QColor(0xc0, 0x30, 0x20));
  CHECK(layer_pixel(window, 50, 150) == QColor(0xc0, 0x30, 0x20));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(layer_pixel(window, 50, 50) == QColor(0xc0, 0x30, 0x20));
}

// A filter that changes the image size (CRT Scanlines enlarges it fourfold) still applies
// inside a selection: the whole result, placed where it lands without one, is seen through
// the layer's frame, so the layer keeps its size and nothing outside the selection changes.
// The dialog previews it and says so; OK commits the same.
void ui_gmic_size_changing_filter_stays_inside_the_selection() {
  if (!gmic::engine_available()) {
    patchy::test::skip("build without the G'MIC engine (PATCHY_ENABLE_GMIC off)");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  const QColor red(0xc0, 0x30, 0x20);
  // A layer of its own, 200 x 200, so the fourfold run stays small.
  CHECK(run_script(window, QStringLiteral("var doc = app.activeDocument; var red = doc.addLayer('Red');"
                                          "red.fillRect(0, 0, 200, 200, '#c03020'); doc.activeLayer = red;")));
  auto& document = MainWindowTestAccess::document(window);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  if (!active.has_value()) {
    return;
  }
  const auto bounds = [&] { return std::as_const(document).find_layer(*active)->bounds(); };
  const auto original_bounds = bounds();
  CHECK(original_bounds.width == 200 && original_bounds.height == 200);
  const auto kept_size = [&] {
    const auto now = bounds();
    return now.x == original_bounds.x && now.y == original_bounds.y && now.width == original_bounds.width &&
           now.height == original_bounds.height;
  };
  patchy::PixelBuffer mask(document.width(), document.height(), patchy::PixelFormat::gray8());
  for (int y = 0; y < mask.height(); ++y) {
    auto row = mask.row(y);
    for (int x = 0; x < mask.width(); ++x) {
      row[static_cast<std::size_t>(x)] = (x < 100 && y < 100) ? 255 : 0;
    }
  }
  canvas->replace_selection_from_grayscale(mask, QStringLiteral("Square selection"));
  CHECK(canvas->has_selection());
  // Scanlines darken some rows of the enlarged red; the selected square shows them.
  const auto selection_changed = [&] {
    for (int y = 40; y < 60; ++y) {
      if (layer_pixel(window, 50, y) != red) {
        return true;
      }
    }
    return false;
  };
  const auto outside_untouched = [&] {
    return layer_pixel(window, 150, 150) == red && layer_pixel(window, 50, 150) == red &&
           layer_pixel(window, 150, 50) == red;
  };

  CHECK(window.apply_gmic_filter("fx_crt_scanlines", {}));
  CHECK(kept_size());
  CHECK(selection_changed());
  CHECK(outside_untouched());
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(!selection_changed());

  const auto has_note = [](const patchy::ui::GmicFilterLibraryDialog& dialog, const QString& start) {
    for (const auto& note : dialog.result_notes()) {
      if (note.startsWith(start)) {
        return true;
      }
    }
    return false;
  };
  run_library_dialog(window, [&](patchy::ui::GmicFilterLibraryDialog& dialog) {
    CHECK(dialog.select_filter("fx_crt_scanlines"));
    CHECK(wait_until(selection_changed));
    CHECK(kept_size());
    CHECK(outside_untouched());
    const QString size = QStringLiteral("%1 x %2").arg(original_bounds.width * 4).arg(original_bounds.height * 4);
    CHECK(wait_until([&] {
      return has_note(dialog, QStringLiteral("CRT Scanlines returns a %1 pixel image.").arg(size));
    }));
    CHECK(!has_note(dialog, QStringLiteral("Changes the layer size")));
    dialog.accept();
  });
  CHECK(kept_size());
  CHECK(selection_changed());
  CHECK(outside_untouched());
}

// Filter > Last Filter repeats the choice the G'MIC Filter Library last accepted (filter,
// values and input layers) on the active layer and selection, without the dialog.
void ui_gmic_last_filter_repeats_the_accepted_choice() {
  if (!gmic::engine_available()) {
    patchy::test::skip("build without the G'MIC engine (PATCHY_ENABLE_GMIC off)");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  const QColor red(0xc0, 0x30, 0x20);
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.fillRect(0, 0, 200, 200, '#c03020');")));
  auto* last_filter = require_hotkey_action(window, QStringLiteral("filter.last"));
  QColor applied;
  run_library_dialog(window, [&](patchy::ui::GmicFilterLibraryDialog& dialog) {
    CHECK(dialog.select_filter("fx_sepia", {"40", "0", "0"}));
    CHECK(wait_until([&] { return layer_pixel(window, 50, 50) != red; }));
    applied = layer_pixel(window, 50, 50);
    dialog.accept();
  });
  CHECK(applied.isValid() && applied != red);
  CHECK(layer_pixel(window, 50, 50) == applied);
  CHECK(last_filter->isEnabled());
  CHECK(last_filter->text() == QStringLiteral("Last Filter: Sepia"));
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(layer_pixel(window, 50, 50) == red);

  auto& document = MainWindowTestAccess::document(window);
  patchy::PixelBuffer mask(document.width(), document.height(), patchy::PixelFormat::gray8());
  for (int y = 0; y < mask.height(); ++y) {
    auto row = mask.row(y);
    for (int x = 0; x < mask.width(); ++x) {
      row[static_cast<std::size_t>(x)] = (x < 100 && y < 100) ? 255 : 0;
    }
  }
  canvas->replace_selection_from_grayscale(mask, QStringLiteral("Square selection"));
  bool dialog_opened = false;
  QTimer::singleShot(0, [&] {
    if (auto* dialog = window.findChild<patchy::ui::GmicFilterLibraryDialog*>(); dialog != nullptr) {
      dialog_opened = true;
      dialog->reject();
    }
  });
  last_filter->trigger();
  QApplication::processEvents();
  CHECK(!dialog_opened);
  CHECK(layer_pixel(window, 50, 50) == applied);
  CHECK(layer_pixel(window, 150, 150) == red);
}

// A filter that returns several images: the first replaces the active layer, the
// others become new layers above it with the name and blend mode the filter gave
// them, all in one undo step.
// A filter that returns transparency (Ball draws a sphere on a clear square) keeps it on a
// layer that has no alpha channel, such as a flattened JPEG's: the layer gains one instead
// of the clear area turning black. A result with no transparency leaves the format alone.
void ui_gmic_filter_keeps_transparency_on_a_layer_without_alpha() {
  if (!gmic::engine_available()) {
    patchy::test::skip("build without the G'MIC engine (PATCHY_ENABLE_GMIC off)");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto& document = MainWindowTestAccess::document(window);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  if (!active.has_value()) {
    return;
  }
  const auto make_opaque_rgb = [&] {
    patchy::PixelBuffer pixels(240, 160, patchy::PixelFormat::rgb8());
    for (int y = 0; y < pixels.height(); ++y) {
      for (int x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        px[0] = 0x30;
        px[1] = 0x60;
        px[2] = 0xa0;
      }
    }
    auto* layer = document.find_layer(*active);
    layer->set_pixels(std::move(pixels));
    layer->set_bounds(patchy::Rect{0, 0, 240, 160});
  };
  const auto layer = [&]() -> const patchy::Layer& { return *std::as_const(document).find_layer(*active); };
  make_opaque_rgb();
  CHECK(layer().pixels().format().channels == 3);

  CHECK(window.apply_gmic_filter("fx_ball", {}, '.'));
  CHECK(layer().pixels().format().channels == 4);
  CHECK(layer().bounds().width == 200 && layer().bounds().height == 200);
  // The corner is outside the sphere: clear, not black.
  CHECK(std::as_const(layer().pixels()).pixel(0, 0)[3] == 0);
  CHECK(std::as_const(layer().pixels()).pixel(100, 100)[3] == 255);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(layer().pixels().format().channels == 3);

  // Sepia returns an opaque image: the layer keeps its format.
  CHECK(window.apply_gmic_filter("fx_sepia", {}, '.'));
  CHECK(layer().pixels().format().channels == 3);
}

void ui_gmic_filter_adds_output_layers_in_one_undo_step() {
  if (!gmic::engine_available()) {
    patchy::test::skip("build without the G'MIC engine (PATCHY_ENABLE_GMIC off)");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  const QColor red(0xc0, 0x30, 0x20);
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.fillRect(0, 0, 200, 200, '#c03020');")));
  auto& document = MainWindowTestAccess::document(window);
  const auto layers_before = document.layers().size();
  const auto active_before = document.active_layer_id();
  CHECK(active_before.has_value());
  if (!active_before.has_value()) {
    return;
  }
  const auto active_bounds = std::as_const(document).find_layer(*active_before)->bounds();
  const auto active_name = std::as_const(document).find_layer(*active_before)->name();

  CHECK(window.apply_gmic_filter("fx_channels2layers", {}));
  CHECK(document.layers().size() == layers_before + 2U);
  CHECK(document.active_layer_id() == active_before);
  // G'MIC names them "<layer> [green]" and "<layer> [blue]" with mode(add): the layer's
  // own name reaches the filter (it read "[unnamed]" before Patchy passed name(...)).
  int additive = 0;
  for (const auto& layer : std::as_const(document).layers()) {
    if (active_before.has_value() && layer.id() != *active_before && layer.name().find('[') != std::string::npos) {
      CHECK(layer.name() == active_name + " [green]" || layer.name() == active_name + " [blue]");
      CHECK(layer.blend_mode() == patchy::BlendMode::LinearDodge);
      // Same place and size as the layer they were split from.
      CHECK(layer.bounds().x == active_bounds.x && layer.bounds().y == active_bounds.y &&
            layer.bounds().width == active_bounds.width && layer.bounds().height == active_bounds.height);
      ++additive;
    }
  }
  CHECK(additive == 2);
  // The active layer now holds the red channel only.
  const auto channel = layer_pixel(window, 50, 50);
  CHECK(channel.red() == 0xc0 && channel.green() == 0 && channel.blue() == 0);

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_before);
  CHECK(layer_pixel(window, 50, 50) == red);

  // G'MIC lists images top of the stack first, so they go below the source layer. A
  // Background stays at the bottom: it takes the last image and the others stack above it,
  // still in G'MIC's order, and none of them is called Background.
  CHECK(run_script(window, QStringLiteral("var d = app.activeDocument;"
                                          "for (var i = 0; i < d.layers.length; ++i)"
                                          "  if (d.layers[i].name === 'Background') d.activeLayer = d.layers[i];")));
  const auto background = document.active_layer_id();
  CHECK(background.has_value() && std::as_const(document).find_layer(*background)->name() == "Background");
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.fillRect(0, 0, 200, 200, '#c03020');")));
  const auto background_pixel = [&](int x, int y) {
    const auto* layer = std::as_const(document).find_layer(*background);
    const auto* px = layer->pixels().pixel(x - layer->bounds().x, y - layer->bounds().y);
    return QColor(px[0], px[1], px[2]);
  };
  const auto layers_with_background = document.layers().size();
  CHECK(window.apply_gmic_filter("fx_channels2layers", {}));
  {
    const auto& layers = std::as_const(document).layers();
    CHECK(layers.size() == layers_with_background + 2U);
    if (layers.size() == layers_with_background + 2U) {
      // Bottom up: the Background with the last channel, then green, then red on top.
      CHECK(layers[0].name() == "Background");
      CHECK(layers[1].name() == "Background [green]");
      CHECK(layers[2].name() != "Background");
      const auto top = layers[2].pixels().pixel(50 - layers[2].bounds().x, 50 - layers[2].bounds().y);
      CHECK(top[0] == 0xc0 && top[1] == 0 && top[2] == 0);
    }
    const auto bottom = background_pixel(50, 50);
    CHECK(bottom.red() == 0 && bottom.green() == 0 && bottom.blue() == 0x20);
  }
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_with_background);

  // Ball returns its sphere first and a copy of the original second: on a Background the
  // sphere lands on top as a new layer and the Background keeps what it had (it used to take
  // the sphere and get covered by the copy, named Background as well).
  CHECK(window.apply_gmic_filter("fx_ball", {}));
  {
    const auto& layers = std::as_const(document).layers();
    CHECK(layers.size() == layers_with_background + 1U);
    if (layers.size() == layers_with_background + 1U) {
      CHECK(layers[0].name() == "Background");
      CHECK(layers[1].name() != "Background");
      const auto& sphere = layers[1].pixels();
      CHECK(sphere.format().channels == 4);
      CHECK(sphere.pixel(sphere.width() / 2, sphere.height() / 2)[3] == 255);
      CHECK(sphere.pixel(0, 0)[3] == 0);
    }
    CHECK(background_pixel(50, 50) == red);
  }
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_with_background);
}

// A filter made for several layers gets them lined up on the document and, when it
// combines them, puts the result on the active layer; the layers it read stay as they are.
void ui_gmic_filter_reads_the_layers_its_input_mode_names() {
  if (!gmic::engine_available()) {
    patchy::test::skip("build without the G'MIC engine (PATCHY_ENABLE_GMIC off)");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  const QColor red(0xc0, 0x30, 0x20);
  const QColor blue(0x20, 0x40, 0xc0);
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.fillRect(0, 0, 200, 200, '#c03020');")));
  auto& document = MainWindowTestAccess::document(window);
  const auto red_layer = document.active_layer_id();
  const auto layers_before = document.layers().size();
  CHECK(red_layer.has_value());
  if (!red_layer.has_value()) {
    return;
  }
  // A new layer goes directly above the active one, so the red layer is the one below it.
  CHECK(run_script(window, QStringLiteral("var doc = app.activeDocument;"
                                          "var top = doc.addLayer('Top');"
                                          "top.fillRect(0, 0, 200, 200, '#2040c0');"
                                          "doc.activeLayer = top;")));
  CHECK(document.layers().size() == layers_before + 1U);
  CHECK(document.active_layer_id() != red_layer);
  CHECK(layer_pixel(window, 50, 50) == blue);
  const auto red_pixel = [&document, red_layer] {
    const auto* layer = std::as_const(document).find_layer(*red_layer);
    const auto* px = layer->pixels().pixel(50 - layer->bounds().x, 50 - layer->bounds().y);
    return QColor(px[0], px[1], px[2]);
  };
  CHECK(red_pixel() == red);

  // Alone, the top layer averages to itself.
  CHECK(window.apply_gmic_filter("fx_blend_average_all", {}, '.'));
  CHECK(layer_pixel(window, 50, 50) == blue);

  // Given the layer below as well, the active layer becomes the average of the two,
  // and the layer it read is untouched.
  CHECK(window.apply_gmic_filter("fx_blend_average_all", {}, '+'));
  const auto average = layer_pixel(window, 50, 50);
  CHECK(std::abs(average.red() - 112) <= 2 && std::abs(average.green() - 56) <= 2 &&
        std::abs(average.blue() - 112) <= 2);
  CHECK(red_pixel() == red);
  CHECK(document.layers().size() == layers_before + 1U);

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(layer_pixel(window, 50, 50) == blue);
}
// On an image above the draft size the preview runs twice: a draft on a reduced copy,
// then the full-quality run, which is the only result OK commits. A filter measured as
// quick skips the draft the next time, and OK during the full run waits for that run.
void ui_gmic_library_dialog_drafts_then_refines() {
  if (!gmic::engine_available()) {
    patchy::test::skip("build without the G'MIC engine (PATCHY_ENABLE_GMIC off)");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  // One-pixel stripes: a draft, which averages them, cannot equal the full result.
  CHECK(run_script(window, QStringLiteral("var l = app.activeDocument.activeLayer;"
                                          "l.fillRect(0, 0, 200, 200, '#c03020');"
                                          "for (var x = 0; x < 200; x += 2) l.fillRect(x, 0, 1, 200, '#20c040');")));
  auto& document = MainWindowTestAccess::document(window);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  if (!active.has_value()) {
    return;
  }
  const auto layer_bounds = [&] { return std::as_const(document).find_layer(*active)->bounds(); };
  const auto original_bounds = layer_bounds();
  const QColor original_even = layer_pixel(window, 50, 50);
  const QColor original_odd = layer_pixel(window, 51, 50);
  CHECK(original_even != original_odd);
  const auto undo = [&window] {
    require_action_by_text(window, QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
  };

  // The reference: the filter applied directly, at full size.
  CHECK(window.apply_gmic_filter("fx_sepia", {}, '.'));
  const QColor sepia_even = layer_pixel(window, 50, 50);
  const QColor sepia_odd = layer_pixel(window, 51, 50);
  CHECK(sepia_even != original_even && sepia_even != sepia_odd);
  undo();
  CHECK(layer_pixel(window, 50, 50) == original_even);

  const EnvironmentVariableRestorer restore_budget("PATCHY_GMIC_DRAFT_PIXELS");
  qputenv("PATCHY_GMIC_DRAFT_PIXELS", "60000");  // the 1024x768 layer drafts at about a quarter size
  const auto drafts = [&window] { return MainWindowTestAccess::gmic_draft_previews_shown(window); };
  const auto shows_full_sepia = [&] {
    return layer_pixel(window, 50, 50) == sepia_even && layer_pixel(window, 51, 50) == sepia_odd;
  };
  CHECK(drafts() == 0U);

  // First time: a draft, then the full result; OK commits the full result.
  run_library_dialog(window, [&](patchy::ui::GmicFilterLibraryDialog& dialog) {
    CHECK(dialog.select_filter("fx_sepia"));
    CHECK(wait_until(shows_full_sepia));
    CHECK(drafts() == 1U);
    const auto draft_bounds = MainWindowTestAccess::gmic_last_draft_bounds(window);
    CHECK(draft_bounds.x == original_bounds.x && draft_bounds.y == original_bounds.y &&
          draft_bounds.width == original_bounds.width && draft_bounds.height == original_bounds.height);
    dialog.accept();
  });
  CHECK(shows_full_sepia());
  undo();
  CHECK(layer_pixel(window, 50, 50) == original_even && layer_pixel(window, 51, 50) == original_odd);

  // Reopened on the same filter, now measured as quick: no draft, and OK at once waits for
  // the full run already under way instead of starting another.
  run_library_dialog(window, [&](patchy::ui::GmicFilterLibraryDialog& dialog) {
    CHECK(dialog.choice().has_value() && dialog.choice()->filter_id == "fx_sepia");
    dialog.accept();
  });
  CHECK(drafts() == 1U);
  CHECK(shows_full_sepia());
  undo();
  CHECK(layer_pixel(window, 50, 50) == original_even);

  // A filter that enlarges its image: the draft lands where the full result does.
  run_library_dialog(window, [&](patchy::ui::GmicFilterLibraryDialog& dialog) {
    CHECK(dialog.select_filter("fx_frame"));
    CHECK(wait_until([&] { return drafts() == 2U; }));
    CHECK(wait_until([&] { return MainWindowTestAccess::gmic_full_preview_finished(window, "fx_frame"); }));
    dialog.accept();
  });
  const auto framed = layer_bounds();
  const auto draft_framed = MainWindowTestAccess::gmic_last_draft_bounds(window);
  CHECK(framed.width > original_bounds.width && framed.height > original_bounds.height);
  // Close, not equal: the frame's one-pixel outline is a pixel of the reduced image too,
  // several full-size pixels once enlarged.
  CHECK(std::abs(draft_framed.x - framed.x) <= 8 && std::abs(draft_framed.y - framed.y) <= 8 &&
        std::abs(draft_framed.width - framed.width) <= 16 && std::abs(draft_framed.height - framed.height) <= 16);
  undo();
  CHECK(layer_bounds().width == original_bounds.width);

  // Cancel during a preview restores the layer.
  run_library_dialog(window, [&](patchy::ui::GmicFilterLibraryDialog& dialog) {
    CHECK(dialog.select_filter("fx_frame"));
    dialog.reject();
  });
  CHECK(layer_bounds().width == original_bounds.width);
  CHECK(layer_pixel(window, 50, 50) == original_even && layer_pixel(window, 51, 50) == original_odd);
}

// A button is a one-shot flag: the preview run after its press reads 1, and the dialog
// then shows it released, so the next change does not press it again (Zoom In would zoom
// on every later preview).
void ui_gmic_library_dialog_releases_a_button_after_the_run_that_read_it() {
  if (!gmic::engine_available()) {
    patchy::test::skip("build without the G'MIC engine (PATCHY_ENABLE_GMIC off)");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.fillRect(0, 0, 120, 90, '#c03020');")));
  run_library_dialog(window, [&](patchy::ui::GmicFilterLibraryDialog& dialog) {
    for (const char* id : {"fx_mandelbrot", "fx_random_pattern"}) {
      CHECK(dialog.select_filter(id));
      const auto* filter = patchy::ui::gmic_library().catalog.find(id);
      CHECK(filter != nullptr);
      if (filter == nullptr) {
        return;
      }
      // The first button's place among the values, and its widget.
      std::size_t value_index = 0;
      QString label;
      for (const auto& parameter : filter->parameters) {
        if (parameter.type == gmic::ParameterType::Button) {
          label = QString::fromStdString(parameter.name);
          break;
        }
        value_index += parameter.carries_value() ? 1U : 0U;
      }
      QPushButton* button = nullptr;
      for (auto* candidate : dialog.findChildren<QPushButton*>()) {
        if (!label.isEmpty() && candidate->text() == label) {
          button = candidate;
        }
      }
      CHECK(button != nullptr);
      if (button == nullptr) {
        return;
      }
      const auto value = [&dialog, value_index] {
        const auto choice = dialog.choice();
        return choice.has_value() && value_index < choice->values.size() ? choice->values[value_index]
                                                                           : std::string("?");
      };
      CHECK(value() == "0");
      button->click();
      CHECK(value() == "1");
      CHECK(wait_until([&] { return value() == "0"; }));
    }
    dialog.reject();
  });
}

const patchy::Layer* find_named_layer(const std::vector<patchy::Layer>& layers, const std::string& name) {
  for (const auto& layer : layers) {
    if (layer.name() == name) {
      return &layer;
    }
    if (const auto* inner = find_named_layer(layer.children(), name); inner != nullptr) {
      return inner;
    }
  }
  return nullptr;
}

bool has_named_layer(patchy::ui::MainWindow& window, const char* name) {
  return find_named_layer(std::as_const(MainWindowTestAccess::document(window)).layers(), name) != nullptr;
}

const patchy::Layer* named_layer(patchy::ui::MainWindow& window, const char* name) {
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  const auto* layer = find_named_layer(document.layers(), name);
  CHECK(layer != nullptr);
  return layer;
}

QColor named_layer_pixel(patchy::ui::MainWindow& window, const char* name, int x, int y) {
  const auto* layer = named_layer(window, name);
  if (layer == nullptr) {
    return {};
  }
  const auto bounds = layer->bounds();
  if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width || y >= bounds.y + bounds.height) {
    return {};
  }
  const auto* px = layer->pixels().pixel(x - bounds.x, y - bounds.y);
  return QColor(px[0], px[1], px[2]);
}

bool same_rect(const patchy::Rect& a, const patchy::Rect& b) {
  return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

// A filter changes every chosen layer it may change. A filter made for one image runs on
// each chosen layer alone, at its own size, exactly as if it were applied to each in turn;
// locked layers (like text, shape and Smart Object layers) are only read; hidden means
// hidden in the document, enclosing groups included; the whole run is one undo step.
// Scripts get the menu command's gates as errors.
void ui_gmic_filter_changes_every_chosen_layer() {
  if (!gmic::engine_available()) {
    patchy::test::skip("build without the G'MIC engine (PATCHY_ENABLE_GMIC off)");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  const QString prelude = QStringLiteral(
      "var doc = app.activeDocument;"
      "function find(list, name) {"
      "  for (var i = 0; i < list.length; ++i) {"
      "    if (list[i].name === name) return list[i];"
      "    var inner = list[i].children ? find(list[i].children, name) : null;"
      "    if (inner) return inner;"
      "  }"
      "  return null;"
      "}"
      "function L(name) { var l = find(doc.layers, name); if (!l) throw new Error('no layer ' + name); return l; }"
      "function count(list) { var n = 0; for (var i = 0; i < list.length; ++i)"
      "  n += list[i].children && list[i].children.length ? count(list[i].children) : 1; return n; }");
  const auto script = [&](const QString& body) { return run_script(window, prelude + body); };
  const QColor red(0xc0, 0x30, 0x20);
  const QColor green(0x20, 0xc0, 0x40);
  const QColor blue(0x20, 0x40, 0xc0);
  CHECK(script(QStringLiteral(
      "var bottom = doc.activeLayer; bottom.name = 'Bottom'; bottom.fillRect(0, 0, 200, 200, '#c03020');"
      "var mid = doc.addLayer('Mid'); doc.activeLayer = mid; mid.fillRect(0, 0, 200, 200, '#20c040');"
      "var top = doc.addLayer('Top'); doc.activeLayer = top; top.fillRect(0, 0, 200, 200, '#2040c0');"
      "doc.activeLayer = mid;")));
  const auto pixel = [&window](const char* name) { return named_layer_pixel(window, name, 50, 50); };
  const auto undo = [&window] {
    require_action_by_text(window, QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
  };
  const auto untouched = [&] { return pixel("Bottom") == red && pixel("Mid") == green && pixel("Top") == blue; };
  CHECK(untouched());

  // The references: Sepia on each layer alone.
  std::map<std::string, QColor> sepia;
  for (const char* name : {"Bottom", "Mid", "Top"}) {
    CHECK(script(QStringLiteral("if (L('%1').applyGmic('fx_sepia') !== 0) throw new Error('added');")
                     .arg(QLatin1String(name))));
    sepia[name] = pixel(name);
    undo();
  }
  CHECK(untouched());
  CHECK(sepia["Bottom"] != red && sepia["Mid"] != green && sepia["Top"] != blue);

  // Over all three layers each gets its own result, nothing is added, and one undo puts
  // every layer back.
  CHECK(script(QStringLiteral(
      "var before = count(doc.layers);"
      "if (L('Mid').applyGmic('fx_sepia', [], {inputLayers: 'all'}) !== 0) throw new Error('added');"
      "if (count(doc.layers) !== before) throw new Error('layer count');")));
  CHECK(pixel("Bottom") == sepia["Bottom"] && pixel("Mid") == sepia["Mid"] && pixel("Top") == sepia["Top"]);
  undo();
  CHECK(untouched());

  // The active layer and the one below.
  CHECK(script(QStringLiteral("L('Mid').applyGmic('fx_sepia', [], {inputLayers: 'activeAndBelow'});")));
  CHECK(pixel("Bottom") == sepia["Bottom"] && pixel("Mid") == sepia["Mid"] && pixel("Top") == blue);
  undo();

  // A layer with locked pixels is chosen but stays as it is.
  CHECK(script(QStringLiteral("L('Bottom').locked = true;")));
  CHECK(script(QStringLiteral("L('Mid').applyGmic('fx_sepia', [], {inputLayers: 'all'});")));
  CHECK(pixel("Bottom") == red && pixel("Mid") == sepia["Mid"] && pixel("Top") == sepia["Top"]);
  undo();
  CHECK(script(QStringLiteral("L('Bottom').locked = false; doc.activeLayer = L('Mid');")));

  // A layer that covers part of the canvas keeps its own size: a filter that fills
  // transparency (Edges turns it opaque) sees the layer alone, not a canvas-sized copy.
  CHECK(script(QStringLiteral("var s = doc.addLayer('Small'); s.fillRect(20, 20, 60, 45, '#c03030');"
                              "doc.activeLayer = L('Mid');")));
  const auto* partial = named_layer(window, "Small");
  const auto partial_bounds = partial != nullptr ? partial->bounds() : patchy::Rect{};
  CHECK(partial_bounds.width == 60 && partial_bounds.height == 45);
  CHECK(script(QStringLiteral("L('Mid').applyGmic('fx_edges', [], {inputLayers: 'all'});")));
  partial = named_layer(window, "Small");
  CHECK(partial != nullptr && same_rect(partial->bounds(), partial_bounds));
  CHECK(pixel("Top") != blue);
  undo();
  undo();  // the Small layer itself
  CHECK(!has_named_layer(window, "Small"));
  CHECK(untouched());

  // Hidden layers: Top by its own eye, Bottom through its group. The active layer is not
  // among them, so it keeps its pixels; they change in place and nothing is added.
  CHECK(script(QStringLiteral("L('Top').visible = false; doc.groupLayers([L('Bottom')], 'Folder').visible = false;"
                              "doc.activeLayer = L('Mid');")));
  CHECK(script(QStringLiteral(
      "var before = count(doc.layers);"
      "var added = L('Mid').applyGmic('fx_sepia', [], {inputLayers: 'hidden'});"
      "if (added !== 0) throw new Error('expected no new layers, got ' + added);"
      "if (count(doc.layers) !== before) throw new Error('layer count');")));
  CHECK(pixel("Mid") == green && pixel("Bottom") == sepia["Bottom"] && pixel("Top") == sepia["Top"]);
  undo();
  // Visible layers: only the active one now.
  CHECK(script(QStringLiteral("if (L('Mid').applyGmic('fx_sepia', [], {inputLayers: 'visible'}) !== 0)"
                              "  throw new Error('added');")));
  CHECK(pixel("Mid") == sepia["Mid"] && pixel("Top") == blue && pixel("Bottom") == red);
  undo();
  // No hidden layer at all: refused, nothing changes.
  CHECK(script(QStringLiteral("L('Top').visible = true; L('Folder').visible = true; doc.activeLayer = L('Mid');")));
  CHECK(!script(QStringLiteral("L('Mid').applyGmic('fx_sepia', [], {inputLayers: 'hidden'});")));
  CHECK(untouched());

  // A run that would not fit in memory is refused before anything is allocated.
  {
    const EnvironmentVariableRestorer restore_limit("PATCHY_GMIC_MEMORY_LIMIT_MB");
    qputenv("PATCHY_GMIC_MEMORY_LIMIT_MB", "1");
    CHECK(!script(QStringLiteral("L('Mid').applyGmic('fx_sepia', [], {inputLayers: 'all'});")));
    CHECK(untouched());
  }

  // The menu command's gates, as script errors: locked pixels and text layers.
  CHECK(script(QStringLiteral("L('Mid').locked = true;")));
  CHECK(!script(QStringLiteral("L('Mid').applyGmic('fx_sepia');")));
  CHECK(script(QStringLiteral("L('Mid').locked = false; doc.activeLayer = L('Mid');")));
  CHECK(pixel("Mid") == green);
  CHECK(script(QStringLiteral("doc.addTextLayer('Type').name = 'Type';")));
  CHECK(!script(QStringLiteral("L('Type').applyGmic('fx_sepia');")));
}

// Runs `drive` on the About G'MIC box once the dialog's button opens it.
template <typename Drive>
void run_about_box(patchy::ui::GmicFilterLibraryDialog& dialog, Drive drive) {
  bool driven = false;
  QTimer::singleShot(0, &dialog, [&dialog, &driven, drive] {
    auto* about = dialog.findChild<QDialog*>(QStringLiteral("gmicAboutDialog"));
    CHECK(about != nullptr);
    if (about != nullptr) {
      driven = true;
      drive(*about);
      about->accept();
    }
  });
  auto* button = dialog.findChild<QPushButton*>(QStringLiteral("gmicAboutButton"));
  CHECK(button != nullptr);
  if (button != nullptr) {
    button->click();
  }
  CHECK(driven);
}

// The dialog previews on every chosen layer and spells out what applying will do beyond
// what the canvas shows: several layers changed, a layer's new size (here partly outside
// the canvas), layers added. Cancel puts every layer back. About G'MIC credits the project.
void ui_gmic_library_dialog_previews_every_chosen_layer_and_explains_the_result() {
  if (!gmic::engine_available()) {
    patchy::test::skip("build without the G'MIC engine (PATCHY_ENABLE_GMIC off)");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  const QColor red(0xc0, 0x30, 0x20);
  const QColor blue(0x20, 0x40, 0xc0);
  const QColor green(0x20, 0xc0, 0x40);
  CHECK(run_script(window, QStringLiteral("var doc = app.activeDocument;"
                                          "doc.activeLayer.name = 'Bottom';"
                                          "doc.activeLayer.fillRect(0, 0, 200, 200, '#c03020');"
                                          "var top = doc.addLayer('Top'); top.fillRect(0, 0, 200, 200, '#2040c0');"
                                          "var small = doc.addLayer('Small'); small.fillRect(40, 40, 40, 30, '#20c040');"
                                          "doc.activeLayer = small;")));
  const auto pixel = [&window](const char* name, int x, int y) { return named_layer_pixel(window, name, x, y); };
  const auto small_bounds = [&window] {
    const auto* layer = named_layer(window, "Small");
    return layer != nullptr ? layer->bounds() : patchy::Rect{};
  };
  const auto original_small = small_bounds();
  CHECK(original_small.x == 40 && original_small.y == 40 && original_small.width == 40 &&
        original_small.height == 30);
  const auto untouched = [&] {
    return pixel("Bottom", 20, 20) == red && pixel("Top", 20, 20) == blue && pixel("Small", 50, 50) == green &&
           same_rect(small_bounds(), original_small);
  };
  CHECK(untouched());
  const auto has_note = [](const patchy::ui::GmicFilterLibraryDialog& dialog, const QString& start) {
    for (const auto& note : dialog.result_notes()) {
      if (note.startsWith(start)) {
        return true;
      }
    }
    return false;
  };

  run_library_dialog(window, [&](patchy::ui::GmicFilterLibraryDialog& dialog) {
    CHECK(dialog.result_notes().isEmpty());
    // Every layer previews its own Sepia.
    CHECK(dialog.select_filter("fx_sepia", {}, '*'));
    CHECK(wait_until([&] {
      return pixel("Bottom", 20, 20) != red && pixel("Top", 20, 20) != blue && pixel("Small", 50, 50) != green;
    }));
    // The document's own Background layer is one of them.
    const auto layer_count = std::as_const(MainWindowTestAccess::document(window)).layers().size();
    CHECK(layer_count == 4U);
    CHECK(wait_until([&] {
      return has_note(dialog, QStringLiteral("Changes %1 layer(s).").arg(static_cast<qulonglong>(layer_count)));
    }));
    // A new filter clears the notes until its own result arrives.
    CHECK(dialog.select_filter("fx_crt_scanlines"));
    CHECK(!has_note(dialog, QStringLiteral("Changes ")));
    // CRT Scanlines enlarges its image fourfold, centred on the layer.
    CHECK(wait_until([&] { return small_bounds().width == 160; }));
    CHECK(pixel("Bottom", 20, 20) == red && pixel("Top", 20, 20) == blue);
    CHECK(wait_until(
        [&] { return has_note(dialog, QStringLiteral("Changes the layer size from 40 x 30 to 160 x 120 pixels.")); }));
    CHECK(has_note(dialog, QStringLiteral("Part of the layer will lie outside the canvas.")));
    // Channels to Layers adds two layers, and the preview shows them where OK will put them:
    // G'MIC lists images top of the stack first, so below the layer they came from, in the
    // document and so in the Layers panel.
    CHECK(dialog.select_filter("fx_channels2layers"));
    CHECK(wait_until([&] { return has_note(dialog, QStringLiteral("Adds 2 new layer(s).")); }));
    CHECK(same_rect(small_bounds(), original_small));
    const auto& document = std::as_const(MainWindowTestAccess::document(window));
    const auto top_names = [&document] {
      std::vector<std::string> names;
      for (const auto& layer : document.layers()) {
        names.push_back(layer.name());
      }
      return names;
    };
    CHECK(top_names() == (std::vector<std::string>{"Background", "Bottom", "Top", "Small [blue]", "Small [green]",
                                                    "Small"}));
    const auto active_name = [&document] {
      const auto id = document.active_layer_id();
      const auto* layer = id.has_value() ? document.find_layer(*id) : nullptr;
      return layer != nullptr ? layer->name() : std::string();
    };
    CHECK(active_name() == "Small");

    // The Layers panel answers its eyes during the preview, and nothing else: a click on
    // another row's name selects nothing.
    auto* layer_list = dynamic_cast<patchy::ui::LayerListWidget*>(window.findChild<QListWidget*>(QStringLiteral("layerList")));
    CHECK(layer_list != nullptr && layer_list->isEnabled() && layer_list->visibility_only());
    const auto row_widget = [layer_list](const QString& name) -> QWidget* {
      for (int row = 0; layer_list != nullptr && row < layer_list->count(); ++row) {
        if (layer_list->item(row)->text() == name) {
          return layer_list->itemWidget(layer_list->item(row));
        }
      }
      return nullptr;
    };
    auto* green_row = row_widget(QStringLiteral("Small [green]"));
    auto* green_eye = green_row != nullptr ? green_row->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"))
                                           : nullptr;
    CHECK(green_eye != nullptr);
    if (green_eye != nullptr) {
      QTest::mouseClick(green_eye, Qt::LeftButton);
      QApplication::processEvents();
    }
    CHECK(wait_until([&] { return has_named_layer(window, "Small [green]") &&
                                  !named_layer(window, "Small [green]")->visible(); }));
    auto* top_row = row_widget(QStringLiteral("Top"));
    auto* top_name = top_row != nullptr ? top_row->findChild<QLabel*>(QStringLiteral("layerRowName")) : nullptr;
    CHECK(top_name != nullptr);
    if (top_name != nullptr) {
      QTest::mouseClick(top_name, Qt::LeftButton);
      QApplication::processEvents();
    }
    CHECK(active_name() == "Small");
    // Preview off takes them away with the rest of the preview; on brings them back.
    auto* preview = dialog.findChild<QCheckBox*>(QStringLiteral("gmicPreviewCheck"));
    CHECK(preview != nullptr);
    if (preview != nullptr) {
      preview->setChecked(false);
      CHECK(top_names().size() == layer_count);
      preview->setChecked(true);
      CHECK(top_names().size() == layer_count + 2U);
    }
    // A filter that adds nothing takes them away too.
    CHECK(dialog.select_filter("fx_sepia", {}, '.'));
    CHECK(wait_until([&] { return top_names().size() == layer_count; }));
    CHECK(dialog.select_filter("fx_channels2layers"));
    CHECK(wait_until([&] { return top_names().size() == layer_count + 2U; }));

    // Ball returns its sphere first and the original second: the original goes below the
    // sphere, so the preview shows the sphere.
    CHECK(dialog.select_filter("fx_ball"));
    CHECK(wait_until([&] {
      const auto& layers = document.layers();
      return layers.size() == layer_count + 1U && layers.back().bounds().width == 200;
    }));
    {
      const auto& layers = document.layers();
      CHECK(layers.back().name() == "Small" && layers.back().bounds().height == 200);
      CHECK(layers[layers.size() - 2U].name() == "Small" && same_rect(layers[layers.size() - 2U].bounds(),
                                                                      original_small));
    }

    run_about_box(dialog, [](QDialog& about) {
      auto* heading = about.findChild<QLabel*>(QStringLiteral("gmicAboutHeading"));
      auto* text = about.findChild<QLabel*>(QStringLiteral("gmicAboutText"));
      auto* link = about.findChild<QLabel*>(QStringLiteral("gmicAboutLink"));
      CHECK(heading != nullptr && text != nullptr && link != nullptr);
      if (heading == nullptr || text == nullptr || link == nullptr) {
        return;
      }
      CHECK(heading->text().contains(QString::fromStdString(gmic::engine_version())));
      CHECK(text->text().contains(QStringLiteral("GREYC")));
      CHECK(text->text().contains(QStringLiteral("gimmick")));
      CHECK(text->text().contains(QStringLiteral("CeCILL-C")));
      CHECK(link->text().contains(QStringLiteral("https://gmic.eu")));
      CHECK(link->openExternalLinks());
    });
    dialog.reject();
  });
  CHECK(untouched());
  // Cancel removed the previewed layers.
  CHECK(std::as_const(MainWindowTestAccess::document(window)).layers().size() == 4U);
  CHECK(!has_named_layer(window, "Small [green]"));

  // OK adds the previewed layers for real, as one undo step that leaves no preview behind,
  // with the eyes the user set on them in the preview.
  run_library_dialog(window, [&](patchy::ui::GmicFilterLibraryDialog& dialog) {
    CHECK(dialog.select_filter("fx_channels2layers", {}, '.'));
    CHECK(wait_until([&] { return has_named_layer(window, "Small [blue]"); }));
    auto* layer_list = dynamic_cast<patchy::ui::LayerListWidget*>(window.findChild<QListWidget*>(QStringLiteral("layerList")));
    QToolButton* blue_eye = nullptr;
    for (int row = 0; layer_list != nullptr && row < layer_list->count(); ++row) {
      if (layer_list->item(row)->text() == QStringLiteral("Small [blue]")) {
        blue_eye = layer_list->itemWidget(layer_list->item(row))
                       ->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
      }
    }
    CHECK(blue_eye != nullptr);
    if (blue_eye != nullptr) {
      QTest::mouseClick(blue_eye, Qt::LeftButton);
    }
    CHECK(wait_until([&] { return !named_layer(window, "Small [blue]")->visible(); }));
    dialog.accept();
  });
  CHECK(std::as_const(MainWindowTestAccess::document(window)).layers().size() == 6U);
  CHECK(has_named_layer(window, "Small [green]") && has_named_layer(window, "Small [blue]"));
  CHECK(named_layer(window, "Small [green]")->visible() && !named_layer(window, "Small [blue]")->visible());
  // The panel is whole again once the dialog is gone.
  if (auto* layer_list = dynamic_cast<patchy::ui::LayerListWidget*>(window.findChild<QListWidget*>(QStringLiteral("layerList")));
      layer_list != nullptr) {
    CHECK(layer_list->isEnabled() && !layer_list->visibility_only());
  }
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(std::as_const(MainWindowTestAccess::document(window)).layers().size() == 4U);
  CHECK(untouched());

  // OK commits the size change and says what became of the layer.
  run_library_dialog(window, [&](patchy::ui::GmicFilterLibraryDialog& dialog) {
    CHECK(dialog.select_filter("fx_crt_scanlines", {}, '.'));
    CHECK(wait_until([&] { return small_bounds().width == 160; }));
    dialog.accept();
  });
  CHECK(small_bounds().width == 160 && small_bounds().height == 120);
  CHECK(small_bounds().x == -20 && small_bounds().y == -5);
  CHECK(window.statusBar()->currentMessage() ==
        QStringLiteral("Applied CRT Scanlines. The layer is now 160 x 120 pixels."));
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(untouched());
}

// patchy.gmic and layer.applyGmic: the list describes filters and parameters, a run
// is part of the script's single undo entry, values go by name or by position, and
// bad input throws instead of running.
void ui_gmic_custom_formula_rejects_excluded_commands_without_editing_layers() {
  if (!gmic::engine_available()) {
    patchy::test::skip("build without the G'MIC engine (PATCHY_ENABLE_GMIC off)");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(
      "var doc = app.activeDocument;"
      "doc.activeLayer.fillRect(0, 0, 200, 200, '#c03020');"
      "var top = doc.addLayer('Top');"
      "top.fillRect(0, 0, 200, 200, '#2040c0'); doc.activeLayer = top;")));
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  const auto layer_count = document.layers().size();
  const auto pixel = layer_pixel(window, 50, 50);
  CHECK(run_script(window, QStringLiteral(
      "var refused = false;"
      "try { app.activeDocument.activeLayer.applyGmic('fx_blend', {"
      "  'Mode': 'Custom formula', 'Custom Formula': \"run('fx_blur_motion') + (a+b)/2\""
      "}, {inputLayers:'all'}); }"
      "catch (e) { if (String(e).indexOf('Not available in this build') < 0) throw e; refused = true; }"
      "if (!refused) throw new Error('excluded command ran through a formula');")));
  CHECK(document.layers().size() == layer_count);
  CHECK(layer_pixel(window, 50, 50) == pixel);
}

void ui_gmic_script_api_lists_and_applies() {
  patchy::ui::MainWindow window;
  show_window(window);
  if (!gmic::engine_available()) {
    CHECK(run_script(window, QStringLiteral("if (patchy.gmic.available || patchy.gmic.list().length !== 0 ||"
                                            "    patchy.gmic.version !== '') throw new Error('engine reported');")));
    CHECK(!run_script(window, QStringLiteral("app.activeDocument.activeLayer.applyGmic('fx_sepia');")));
    return;
  }
  const QColor red(0xc0, 0x30, 0x20);
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.fillRect(0, 0, 200, 200, '#c03020');")));
  auto& document = MainWindowTestAccess::document(window);
  const auto layers_before = document.layers().size();

  CHECK(run_script(
      window,
      QStringLiteral(
          "var filters = patchy.gmic.list(), sepia = null;"
          "for (var i = 0; i < filters.length; ++i) {"
          "  if (filters[i].id === 'fx_sepia') sepia = filters[i];"
          "  if (filters[i].id === 'fx_inpaint_patch') throw new Error('an excluded filter is listed');"
          "}"
          "if (!patchy.gmic.available || patchy.gmic.version !== '4.0.5') throw new Error('engine');"
          "if (!sepia || sepia.name !== 'Sepia' || sepia.folder !== 'Colors' || sepia.inputLayers !== 'active')"
          "  throw new Error('sepia entry');"
          "var p = sepia.parameters;"
          "if (p.length !== 3 || p[0].name !== 'Brightness (%)' || p[0].type !== 'float' || p[0].default !== 0 ||"
          "    p[0].minimum !== -100 || p[0].maximum !== 100) throw new Error('sepia parameters');"
          "var added = app.activeDocument.activeLayer.applyGmic('fx_sepia', {'Brightness (%)': 0});"
          "if (added !== 0) throw new Error('sepia added layers');")));
  const auto sepia = layer_pixel(window, 50, 50);
  CHECK(std::abs(sepia.red() - 116) <= 1 && std::abs(sepia.green() - 85) <= 1 && std::abs(sepia.blue() - 45) <= 1);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(layer_pixel(window, 50, 50) == red);

  // By position, a choice by its label, and the count of added layers; still one undo.
  CHECK(run_script(window, QStringLiteral("var n = app.activeDocument.activeLayer.applyGmic('fx_channels2layers', ['rgb']);"
                                          "if (n !== 2) throw new Error('expected two new layers, got ' + n);")));
  CHECK(document.layers().size() == layers_before + 2U);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == layers_before);
  CHECK(layer_pixel(window, 50, 50) == red);

  const char* const rejected[] = {
      "app.activeDocument.activeLayer.applyGmic('fx_no_such_filter');",
      "app.activeDocument.activeLayer.applyGmic('fx_inpaint_patch');",
      "app.activeDocument.activeLayer.applyGmic('fx_sepia', {'No Such Parameter': 1});",
      "app.activeDocument.activeLayer.applyGmic('fx_sepia', {'Brightness (%)': 'lots'});",
      "app.activeDocument.activeLayer.applyGmic('fx_sepia', [1, 2, 3, 4]);",
      "app.activeDocument.activeLayer.applyGmic('fx_channels2layers', ['Not A Color Space']);",
      "app.activeDocument.activeLayer.applyGmic('fx_sepia', [], {inputLayers: 'everything'});",
      "app.activeDocument.activeLayer.applyGmic('fx_sepia', [], {preview: true});",
  };
  for (const char* script : rejected) {
    CHECK(!run_script(window, QString::fromLatin1(script)));
    CHECK(layer_pixel(window, 50, 50) == red);
  }
  CHECK(document.layers().size() == layers_before);
}
// A filter's point parameters are draggable markers on the canvas while the dialog
// is open: at the right place, moving with a drag and with typed values, absent for
// blur filters, and gone when the dialog closes.
void ui_gmic_library_dialog_puts_point_handles_on_the_canvas() {
  if (!gmic::engine_available()) {
    patchy::test::skip("build without the G'MIC engine (PATCHY_ENABLE_GMIC off)");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.fillRect(0, 0, 200, 200, '#c03020');")));
  auto& document = MainWindowTestAccess::document(window);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  if (!active.has_value()) {
    return;
  }
  const auto bounds = std::as_const(document).find_layer(*active)->bounds();
  const auto close_to = [](QPointF a, QPointF b, double tolerance) {
    return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance;
  };
  // "x,y" of the first point value (Stereographic Projection: after the Transform choice).
  const auto first_point = [](const patchy::ui::GmicFilterLibraryDialog& dialog) {
    const auto choice = dialog.choice();
    const auto parts = QString::fromStdString(choice.has_value() && choice->values.size() > 1 ? choice->values[1]
                                                                                              : std::string())
                           .split(QLatin1Char(','));
    return parts.size() == 2 ? QPointF(parts[0].toDouble(), parts[1].toDouble()) : QPointF(-1.0, -1.0);
  };

  canvas->set_tool(patchy::ui::CanvasTool::Brush);
  const auto brush_cursor = canvas->cursor().shape();
  CHECK(brush_cursor != Qt::ArrowCursor);
  CHECK(canvas->preview_point_handle_count() == 0);
  run_library_dialog(window, [&](patchy::ui::GmicFilterLibraryDialog& dialog) {
    CHECK(canvas->preview_point_handle_count() == 0);
    CHECK(dialog.select_filter("fx_project_stereographic"));
    // Center and Radius / Angle; its hidden bookkeeping value is not a handle.
    CHECK(canvas->preview_point_handle_count() == 2);
    if (canvas->preview_point_handle_count() != 2) {
      return;
    }
    const QPointF center(bounds.x + bounds.width / 2.0, bounds.y + bounds.height / 2.0);
    CHECK(close_to(canvas->preview_point_handle(0), center, 0.01));
    CHECK(close_to(first_point(dialog), QPointF(50.0, 50.0), 0.01));

    // The markers own the cursor, whatever the tool: a hand over a marker, a closed hand
    // while dragging one, an arrow elsewhere.
    const QPoint start = canvas->preview_point_handle_widget_position(0).toPoint();
    const QPoint end = start + QPoint(30, 20);
    QTest::mouseMove(canvas, start + QPoint(60, 60));
    CHECK(canvas->cursor().shape() == Qt::ArrowCursor);
    QTest::mouseMove(canvas, start);
    CHECK(canvas->cursor().shape() == Qt::OpenHandCursor);

    // Dragging the marker moves the value.
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
    CHECK(canvas->cursor().shape() == Qt::ClosedHandCursor);
    QTest::mouseMove(canvas, end);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
    CHECK(canvas->cursor().shape() == Qt::OpenHandCursor);
    QApplication::processEvents();
    const QPointF dragged = canvas->preview_point_handle(0);
    CHECK(dragged.x() > center.x() && dragged.y() > center.y());
    const QPointF percent = first_point(dialog);
    CHECK(percent.x() > 50.0 && percent.y() > 50.0);
    CHECK(close_to(QPointF(bounds.x + percent.x() / 100.0 * bounds.width,
                           bounds.y + percent.y() / 100.0 * bounds.height),
                   dragged, bounds.width / 100.0));
    // A press away from every marker is not taken by the handles.
    CHECK(canvas->preview_point_handle_count() == 2);

    // A value set from the dialog side moves the marker.
    dialog.move_point(0, QPointF(25.0, 75.0));
    CHECK(close_to(canvas->preview_point_handle(0),
                   QPointF(bounds.x + 0.25 * bounds.width, bounds.y + 0.75 * bounds.height), 0.01));
    CHECK(close_to(first_point(dialog), QPointF(25.0, 75.0), 0.01));

    // A filter made for one image runs on each chosen layer at its own size, so with
    // several layers the markers stay in the active layer's frame.
    CHECK(dialog.select_filter("fx_project_stereographic", {}, '*'));
    CHECK(canvas->preview_point_handle_count() == 2);
    CHECK(close_to(canvas->preview_point_handle(0),
                   QPointF(bounds.x + 0.25 * bounds.width, bounds.y + 0.75 * bounds.height), 0.01));

    // Blur [Radial] and Blur [Angular] show their centre; the two blur filters held back
    // for review are not offered at all (docs/legal-constraints.md).
    CHECK(dialog.select_filter("fx_blur_radial"));
    CHECK(canvas->preview_point_handle_count() == 1);
    CHECK(dialog.select_filter("fx_blur_angular"));
    CHECK(canvas->preview_point_handle_count() == 1);
    CHECK(!dialog.select_filter("fx_blur_dof"));
    CHECK(!dialog.select_filter("fx_blur_motion"));
    // A filter without points has none either.
    CHECK(dialog.select_filter("fx_sepia"));
    CHECK(canvas->preview_point_handle_count() == 0);
    CHECK(dialog.select_filter("fx_project_stereographic"));
    CHECK(canvas->preview_point_handle_count() == 2);
    dialog.reject();
  });
  CHECK(canvas->preview_point_handle_count() == 0);
  // The tool's own cursor comes back with the dialog gone.
  CHECK(canvas->cursor().shape() == brush_cursor);
}
}  // namespace

std::vector<patchy::test::TestCase> gmic_ui_tests() {
  return {
      {"ui_gmic_parameter_panel_builds_every_filter", ui_gmic_parameter_panel_builds_every_filter},
      {"ui_gmic_parameter_panel_reads_and_restores_values", ui_gmic_parameter_panel_reads_and_restores_values},
      {"ui_gmic_library_dialog_browses_searches_and_resets", ui_gmic_library_dialog_browses_searches_and_resets},
      {"ui_gmic_library_dialog_renders_in_the_app_theme", ui_gmic_library_dialog_renders_in_the_app_theme},
      {"ui_gmic_filter_tree_selection_is_readable_in_both_schemes",
       ui_gmic_filter_tree_selection_is_readable_in_both_schemes},
      {"ui_gmic_filter_applies_inside_selection_and_undoes", ui_gmic_filter_applies_inside_selection_and_undoes},
      {"ui_gmic_size_changing_filter_stays_inside_the_selection",
       ui_gmic_size_changing_filter_stays_inside_the_selection},
      {"ui_gmic_last_filter_repeats_the_accepted_choice", ui_gmic_last_filter_repeats_the_accepted_choice},
      {"ui_gmic_library_dialog_previews_on_the_layer_and_commits",
       ui_gmic_library_dialog_previews_on_the_layer_and_commits},
      {"ui_gmic_color_swatch_previews_while_picking", ui_gmic_color_swatch_previews_while_picking},
      {"ui_gmic_filter_adds_output_layers_in_one_undo_step", ui_gmic_filter_adds_output_layers_in_one_undo_step},
      {"ui_gmic_filter_keeps_transparency_on_a_layer_without_alpha",
       ui_gmic_filter_keeps_transparency_on_a_layer_without_alpha},
      {"ui_gmic_filter_reads_the_layers_its_input_mode_names", ui_gmic_filter_reads_the_layers_its_input_mode_names},
      {"ui_gmic_script_api_lists_and_applies", ui_gmic_script_api_lists_and_applies},
      {"ui_gmic_custom_formula_rejects_excluded_commands_without_editing_layers",
       ui_gmic_custom_formula_rejects_excluded_commands_without_editing_layers},
      {"ui_gmic_library_dialog_releases_a_button_after_the_run_that_read_it",
       ui_gmic_library_dialog_releases_a_button_after_the_run_that_read_it},
      {"ui_gmic_library_dialog_drafts_then_refines", ui_gmic_library_dialog_drafts_then_refines},
      {"ui_gmic_filter_changes_every_chosen_layer", ui_gmic_filter_changes_every_chosen_layer},
      {"ui_gmic_library_dialog_previews_every_chosen_layer_and_explains_the_result",
       ui_gmic_library_dialog_previews_every_chosen_layer_and_explains_the_result},
      {"ui_gmic_library_dialog_puts_point_handles_on_the_canvas",
       ui_gmic_library_dialog_puts_point_handles_on_the_canvas},
  };
}
