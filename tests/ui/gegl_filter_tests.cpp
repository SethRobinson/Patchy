// GEGL filters (docs/gegl-filters.md, "Tests"): Patchy's own GEGL through patchy-gegl-host,
// its settings, exclusions, runs at every depth, the menu, scripts and the filter dialog.
// The tests skip in a build without the bundle.

#include "core/pixel_depth.hpp"
#include "filters/filter_engine.hpp"
#include "formats/document_flatten.hpp"
#include "formats/xcf_document_io.hpp"
#include "local_psd_fixtures.hpp"
#include "ui/command_search.hpp"
#include "ui/filter_parameter_panel.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/gegl_filters.hpp"
#include "ui/main_window.hpp"
#include "ui/script_engine.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <system_error>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <string_view>
#include <utility>

namespace {

using namespace patchy::test::ui;

bool run_script(patchy::ui::MainWindow& window, const QString& source, int timeout_ms = 120000) {
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("gegl-filter-test");
  (void)host.run_source(source, std::move(options));
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < timeout_ms) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  CHECK(!host.run_active());
  return !host.last_run_had_error();
}

// Prints the script's messages, so a failed run says why.
bool run_script_verbose(patchy::ui::MainWindow& window, const QString& source) {
  const bool ok = run_script(window, source);
  if (!ok) {
    for (const auto& line : window.script_engine_host().message_backlog()) {
      std::fprintf(stderr, "  script: %s\n", qPrintable(line));
    }
  }
  return ok;
}

bool backlog_contains(patchy::ui::MainWindow& window, const QString& needle) {
  for (const auto& line : window.script_engine_host().message_backlog()) {
    if (line.contains(needle)) {
      return true;
    }
  }
  return false;
}

// GEGL filters from Patchy's own GEGL (docs/gegl-filters.md): the scan through
// patchy-gegl-host maps GEGL's settings (ranges, pixel units, choices), offers the GPL-3
// operations (the helper's --gpl3), leaves out what is not a one-layer image filter, fills
// Filter > GEGL Filters, and a run lands on the layer through apply_filter as one undo
// step. Skips in a build without the bundle.
void ui_gegl_filters_list_and_apply_if_available() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::ensure_gegl_filters_scanned(window);
  const auto registry =
      patchy::ui::MainWindowTestAccess::gegl_filter_registry(window, QStringLiteral("gegl.gaussian-blur"));
  // Patchy's own GEGL (docs/gegl-filters.md, "Bundled GEGL").
  const auto bundled = patchy::ui::bundled_gegl_library();
  if (registry == nullptr || registry->find("gegl.gaussian-blur") == nullptr) {
    CHECK(bundled.isEmpty());  // a build with its own GEGL always offers it
    patchy::test::skip("this build has no GEGL of its own");
  }
  const auto* blur = registry->find("gegl.gaussian-blur");
  const auto parameter = [](const patchy::FilterDefinition& filter, std::string_view key)
      -> const patchy::FilterParameterDefinition* {
    for (const auto& p : filter.catalog.parameters) {
      if (p.key == key) return &p;
    }
    return nullptr;
  };
  const auto* std_dev = parameter(*blur, "std-dev-x");
  CHECK(std_dev != nullptr);
  if (std_dev != nullptr) {
    CHECK(std_dev->kind == patchy::FilterParameterKind::Double);
    CHECK(std_dev->spatial_scale == patchy::FilterSpatialScale::Pixels);
    CHECK(std_dev->practical_maximum.has_value() && *std_dev->practical_maximum > 1.0);
    CHECK(std::get<double>(std_dev->default_value) == 1.5);
  }
  const auto* abyss = parameter(*blur, "abyss-policy");
  CHECK(abyss != nullptr && abyss->kind == patchy::FilterParameterKind::Option && !abyss->options.empty());
  // The GPL-3 operations (the helper's --gpl3).
  CHECK(registry->find("gegl.cartoon") != nullptr);
  // Left out by the legal review (docs/gegl-filters.md, "Legal review"), and as not useful.
  for (const auto* id : {"gegl.median-blur", "gegl.focus-blur", "gegl.seamless-clone",
                         "gegl.absolute", "gegl.unpremultiply", "gegl.tile", "gegl.rectangle", "gegl.dropshadow"}) {
    CHECK(registry->find(id) == nullptr);
  }
  // Offered after the review: expired Shadow/Highlight patents, no superpixel patent found, and
  // Vibrance by the claim read of Apple US 8638338 (docs/patent-research-adjustments.md).
  for (const auto* id : {"gegl.shadows-highlights", "gegl.slic", "gegl.waterpixels", "gegl.vibrance"}) {
    CHECK(registry->find(id) != nullptr);
  }
  CHECK(registry->find("gegl.red-eye-removal") == nullptr);
  CHECK(registry->find("gegl.crop") == nullptr);       // geometry, not a filter
  CHECK(registry->find("gegl.over") == nullptr);       // two inputs
  CHECK(registry->find("gegl.text") == nullptr);       // a setting Patchy cannot hold (text)
  CHECK(registry->find("gegl.png-load") == nullptr);   // a file loader
  // A render operation without an input replaces the layer: Simplex Noise on flat gray.
  if (const auto* noise = registry->find("gegl.simplex-noise"); noise != nullptr) {
    patchy::PixelBuffer flat(32, 32, patchy::PixelFormat{patchy::ColorMode::RGB, patchy::BitDepth::UInt8, 4});
    std::fill(flat.data().begin(), flat.data().end(), std::uint8_t{128});
    const auto gray = flat;
    registry->apply(registry->default_invocation("gegl.simplex-noise"), flat);
    const auto rendered = std::as_const(flat).data();
    const auto flat_gray = std::as_const(gray).data();
    CHECK(!std::equal(rendered.begin(), rendered.end(), flat_gray.begin(), flat_gray.end()));
  } else {
    CHECK(false);  // GEGL 0.4 has Simplex Noise (LGPL)
  }

  // Straight through the registry: a run changes a two-color buffer at its edge.
  patchy::PixelBuffer pixels(16, 8, patchy::PixelFormat{patchy::ColorMode::RGB, patchy::BitDepth::UInt8, 4});
  auto bytes = pixels.data();
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 16; ++x) {
      auto* px = bytes.data() + static_cast<std::size_t>(y) * pixels.stride_bytes() + static_cast<std::size_t>(x) * 4U;
      px[0] = x < 8 ? 255 : 0;
      px[1] = 0;
      px[2] = x < 8 ? 0 : 255;
      px[3] = 255;
    }
  }
  const auto before = pixels;
  auto invocation = registry->default_invocation("gegl.gaussian-blur");
  invocation.parameters["std-dev-x"] = 3.0;
  registry->apply(invocation, pixels);
  CHECK(!(std::as_const(pixels).data()[7 * 4] == std::as_const(before).data()[7 * 4] &&
          std::as_const(pixels).data()[7 * 4 + 2] == std::as_const(before).data()[7 * 4 + 2]));

  // 16-bit and 32-bit (linear) layers run at their own depth.
  for (const auto depth : {patchy::BitDepth::UInt16, patchy::BitDepth::Float32}) {
    CHECK(patchy::deep_filter_support("gegl.gaussian-blur", depth) == patchy::DeepFilterSupport::Native);
    patchy::PixelBuffer deep(16, 8, patchy::PixelFormat{patchy::ColorMode::RGB, depth, 4});
    auto rows = deep.data();
    const auto pixel_bytes = patchy::bytes_per_pixel(deep.format());
    for (int y = 0; y < 8; ++y) {
      for (int x = 0; x < 16; ++x) {
        auto* px = rows.data() + static_cast<std::size_t>(y) * deep.stride_bytes() + static_cast<std::size_t>(x) * pixel_bytes;
        if (depth == patchy::BitDepth::UInt16) {
          const std::uint16_t v[4] = {static_cast<std::uint16_t>(x < 8 ? 65535 : 0), 0, 0, 65535};
          std::memcpy(px, v, sizeof(v));
        } else {
          const float v[4] = {x < 8 ? 1.0F : 0.0F, 0.0F, 0.0F, 1.0F};
          std::memcpy(px, v, sizeof(v));
        }
      }
    }
    const auto deep_before = deep;
    registry->apply(invocation, deep);
    const auto after = std::as_const(deep).data();
    const auto earlier = std::as_const(deep_before).data();
    CHECK(!std::equal(after.begin(), after.end(), earlier.begin(), earlier.end()));
  }

  // A color setting is a choice: Color Overlay in the foreground color paints it.
  if (const auto* overlay = registry->find("gegl.color-overlay"); overlay != nullptr) {
    const auto* value = parameter(*overlay, "value");
    CHECK(value != nullptr && value->kind == patchy::FilterParameterKind::Option);
    auto paint = registry->default_invocation("gegl.color-overlay", patchy::RgbColor{0, 200, 0});
    paint.parameters["value"] = std::string("foreground");
    patchy::PixelBuffer gray(4, 4, patchy::PixelFormat{patchy::ColorMode::RGB, patchy::BitDepth::UInt8, 4});
    std::fill(gray.data().begin(), gray.data().end(), std::uint8_t{128});
    registry->apply(paint, gray);
    const auto px = std::as_const(gray).data();
    CHECK(px[1] > 180 && px[0] < 40 && px[2] < 40);
  } else {
    CHECK(false);  // GEGL 0.4 has Color Overlay
  }

  // GEGL's labels follow the language Patchy asks for, from GEGL's own catalog.
  {
    const auto command = patchy::ui::bundled_gegl_command(bundled);
    const auto german = patchy::ui::scan_gegl_filters(command, QStringLiteral("de"));
    const auto* german_blur = german.registry != nullptr ? german.registry->find("gegl.gaussian-blur") : nullptr;
    CHECK(german_blur != nullptr && german_blur->display_name != blur->display_name);
    // A second scan in the same language comes from the cache, the same list.
    const auto again = patchy::ui::scan_gegl_filters(command, QStringLiteral("de"));
    CHECK(again.registry != nullptr && german.registry != nullptr &&
          again.registry->filters().size() == german.registry->filters().size());
    CHECK(QFileInfo::exists(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                                .filePath(QStringLiteral("gegl-filters/operations.json"))));
  }

  // Scripts list them and run one by id.
  CHECK(run_script_verbose(window, QStringLiteral(
      "var filters = patchy.gegl.filters();\n"
      "var blur = filters.filter(function (f) { return f.id === 'gegl.gaussian-blur'; })[0];\n"
      "var sd = blur.parameters.filter(function (p) { return p.name === 'std-dev-x'; })[0];\n"
      "console.log('gegl list ' + (filters.length > 50) + ' ' + sd.type + ' ' + sd['default']);\n"
      "var doc = app.newDocument(32, 16, {name: 'gegl script'});\n"
      "doc.activeLayer.fillRect(0, 0, 16, 16, '#ff0000');\n"
      "doc.activeLayer.fillRect(16, 0, 16, 16, '#0000ff');\n"
      "doc.activeLayer.applyFilter('gegl.gaussian-blur', {'std-dev-x': 4, 'std-dev-y': 4});\n")));
  CHECK(backlog_contains(window, QStringLiteral("gegl list true number 1.5")));
  {
    // The blur mixed red and blue where they meet.
    auto& scripted = patchy::ui::MainWindowTestAccess::document(window);
    const auto* layer = std::as_const(scripted).find_layer(*scripted.active_layer_id());
    CHECK(layer != nullptr);
    if (layer != nullptr) {
      const auto scripted_bytes = layer->pixels().data();
      const auto* mid = scripted_bytes.data() + 8 * layer->pixels().stride_bytes() +
                        16 * patchy::bytes_per_pixel(layer->pixels().format());
      CHECK(mid[0] > 30 && mid[2] > 30);
    }
  }

  // Filter > GEGL Filters lists it.
  bool listed = false;
  for (auto* action : window.findChildren<QAction*>(QStringLiteral("geglFilterAction"))) {
    listed = listed || action->data().toString() == QStringLiteral("gegl.gaussian-blur");
  }
  CHECK(listed);
  // The menu (under Filter) and its Blur submenu, saved to test-artifacts for visual QA.
  if (auto* gegl_menu = window.findChild<QMenu*>(QStringLiteral("geglFiltersMenu")); gegl_menu != nullptr) {
    gegl_menu->popup(window.mapToGlobal(QPoint(40, 40)));
    QApplication::processEvents();
    save_widget_artifact("gegl-filters-menu", *gegl_menu);
    for (auto* action : gegl_menu->actions()) {
      if (action->menu() != nullptr && action->menu()->actions().size() > 5 &&
          action->text().contains(QStringLiteral("Blur"))) {
        action->menu()->popup(window.mapToGlobal(QPoint(260, 40)));
        QApplication::processEvents();
        save_widget_artifact("gegl-filters-blur-menu", *action->menu());
        action->menu()->hide();
      }
    }
    gegl_menu->hide();
    QApplication::processEvents();
    // About GEGL ends the menu (docs/gegl-filters.md, "Credit").
    const auto entries = gegl_menu->actions();
    CHECK(entries.size() >= 2 && entries.back()->objectName() == QStringLiteral("geglAboutAction") &&
          entries[entries.size() - 2]->isSeparator());
  }

  // Through apply_filter onto a layer, one undo step.
  CHECK(run_script_verbose(window, QStringLiteral("var doc = app.newDocument(64, 48, {name: 'gegl'});\n"
                                                  "doc.activeLayer.fillRect(0, 0, 32, 48, '#ff0000');\n"
                                                  "doc.activeLayer.fillRect(32, 0, 32, 48, '#0000ff');\n")));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());
  if (!layer_id.has_value()) return;
  const auto original = std::as_const(document).find_layer(*layer_id)->pixels();
  patchy::ui::MainWindowTestAccess::apply_filter_without_dialog(window, QStringLiteral("gegl.gaussian-blur"), invocation);
  const auto& filtered = std::as_const(document).find_layer(*layer_id)->pixels();
  CHECK(!(filtered.width() == original.width() &&
          std::equal(filtered.data().begin(), filtered.data().end(), original.data().begin(), original.data().end())));
  CHECK(run_script_verbose(window, QStringLiteral("app.activeDocument.undo();\n")));
  const auto& undone = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(*layer_id)->pixels();
  CHECK(std::equal(undone.data().begin(), undone.data().end(), original.data().begin(), original.data().end()));

  // About GEGL from a filter dialog's About button (docs/gegl-filters.md, "Credit"): its
  // heading names GEGL's version, it links gegl.org and the license texts (saved to
  // test-artifacts/gegl-about-dialog.png).
  bool saw_about = false;
  QTimer about_poll;
  about_poll.setInterval(100);
  QObject::connect(&about_poll, &QTimer::timeout, &window, [&] {
    auto* about = find_top_level_dialog(QStringLiteral("geglAboutDialog"));
    if (about == nullptr) {
      return;
    }
    about_poll.stop();
    const auto* heading = about->findChild<QLabel*>(QStringLiteral("geglAboutHeading"));
    const auto* link = about->findChild<QLabel*>(QStringLiteral("geglAboutLink"));
    saw_about = heading != nullptr && heading->text().startsWith(QStringLiteral("GEGL 0.")) && link != nullptr &&
                link->text().contains(QStringLiteral("https://gegl.org")) &&
                about->findChild<QLabel*>(QStringLiteral("geglAboutLicensesLink")) != nullptr;
    save_widget_artifact("gegl-about-dialog", *about);
    about->accept();
  });

  // The menu command opens Patchy's filter dialog from GEGL's settings (saved to
  // test-artifacts/gegl-<id>-dialog.png for visual QA); Cancel leaves the layer alone.
  // Vignette shows a color setting as a choice. Each credits GEGL and its version, with
  // GPL-3 for the GPL-3 module's operations (Cartoon), and has the About GEGL button.
  const auto open_dialog = [&](const QString& identifier, const QString& control, bool gpl3, bool open_about) {
    bool saw_dialog = false;
    bool about_requested = false;
    int polls = 0;
    QTimer poll;
    poll.setInterval(250);
    QObject::connect(&poll, &QTimer::timeout, &window, [&] {
      ++polls;
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyFilterDialog"));
      if (dialog == nullptr) {
        if (polls > 80) {  // 20 s: close whatever is modal so a failure ends the test
          poll.stop();
          if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) modal->reject();
        }
        return;
      }
      if (polls < 10 || about_poll.isActive()) {
        return;  // let the previews land first, and About GEGL close
      }
      auto* about_button = dialog->findChild<QPushButton*>(QStringLiteral("filterDialogAboutButton"));
      if (open_about && !about_requested && about_button != nullptr) {
        about_requested = true;
        about_poll.start();
        QTimer::singleShot(0, about_button, &QPushButton::click);
        return;
      }
      poll.stop();
      const auto* credit = dialog->findChild<QLabel*>(QStringLiteral("filterDialogCreditLabel"));
      // About GEGL sits just right of Reset, with no other button between them.
      bool about_beside_reset = false;
      if (const auto* box = about_button != nullptr ? qobject_cast<QDialogButtonBox*>(about_button->parentWidget())
                                                    : nullptr;
          box != nullptr) {
        const auto* reset = box->button(QDialogButtonBox::Reset);
        about_beside_reset = reset != nullptr && reset->x() < about_button->x() &&
                             std::ranges::none_of(box->buttons(), [&](const QAbstractButton* other) {
                               return other->isVisible() && other->x() > reset->x() && other->x() < about_button->x();
                             });
      }
      saw_dialog = dialog->property("patchy.filterIdentifier").toString() == identifier &&
                   dialog->findChild<QWidget*>(control) != nullptr && about_beside_reset && credit != nullptr &&
                   credit->text().startsWith(QStringLiteral("Filter from GEGL 0.")) &&
                   credit->text().endsWith(QStringLiteral(", GPL-3")) == gpl3;
      save_widget_artifact(identifier.mid(5).prepend(QStringLiteral("gegl-")).toStdString() + "-dialog", *dialog);
      dialog->reject();
    });
    poll.start();
    patchy::ui::MainWindowTestAccess::apply_filter_with_dialog(window, identifier);
    CHECK(saw_dialog);
    const auto& cancelled = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(*layer_id)->pixels();
    CHECK(std::equal(cancelled.data().begin(), cancelled.data().end(), original.data().begin(), original.data().end()));
  };
  open_dialog(QStringLiteral("gegl.unsharp-mask"), QStringLiteral("geglParam_scaleSlider"), false, false);
  open_dialog(QStringLiteral("gegl.vignette"), QStringLiteral("geglParam_colorCombo"), false, false);
  open_dialog(QStringLiteral("gegl.cartoon"), QStringLiteral("geglParam_mask-radiusSlider"), true, true);
  CHECK(saw_about);

  // Search Commands finds them by name, GEGL's description as the line under it.
  bool searched = false;
  for (const auto& entry : patchy::ui::collect_menu_commands(window.menuBar())) {
    if (entry.action != nullptr && entry.action->data().toString() == QLatin1String("gegl.bloom")) {
      searched = entry.name.contains(QStringLiteral("Bloom"));
    }
  }
  CHECK(searched);
}

// Every filter Patchy's own GEGL offers runs at its defaults on a small gradient and gives
// back a buffer of the same size (docs/gegl-filters.md, "Tests"). Skips without the bundle.
void ui_gegl_bundled_filters_all_run() {
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
  const auto& filters = registry->filters();
  CHECK(filters.size() >= 100U);  // 117 from GEGL 0.4.72 with the GPL-3 module
  const patchy::PixelFormat format{patchy::ColorMode::RGB, patchy::BitDepth::UInt8, 4};
  patchy::PixelBuffer gradient(48, 32, format);
  {
    auto bytes = gradient.data();
    for (int y = 0; y < 32; ++y) {
      for (int x = 0; x < 48; ++x) {
        auto* px = bytes.data() + static_cast<std::size_t>(y) * gradient.stride_bytes() + static_cast<std::size_t>(x) * 4U;
        px[0] = static_cast<std::uint8_t>(x * 5);
        px[1] = static_cast<std::uint8_t>(y * 7);
        px[2] = static_cast<std::uint8_t>((x + y) * 3);
        px[3] = 255;
      }
    }
  }
  QStringList failed;
  QStringList unchanged;
  for (const auto& filter : filters) {
    auto pixels = gradient;
    try {
      registry->apply(registry->default_invocation(filter.identifier), pixels);
    } catch (const std::exception& error) {
      failed << QStringLiteral("%1 (%2)").arg(QString::fromStdString(filter.identifier), QString::fromUtf8(error.what()));
      continue;
    }
    if (pixels.width() != gradient.width() || pixels.height() != gradient.height()) {
      failed << QString::fromStdString(filter.identifier) + QStringLiteral(" (size changed)");
      continue;
    }
    const auto after = std::as_const(pixels).data();
    const auto before = std::as_const(gradient).data();
    if (std::equal(after.begin(), after.end(), before.begin(), before.end())) {
      unchanged << QString::fromStdString(filter.identifier);
    }
  }
  if (!failed.isEmpty()) {
    std::cerr << "GEGL filters that failed: " << failed.join(QStringLiteral(", ")).toStdString() << '\n';
  }
  // Informational: a filter can leave this gradient alone at its defaults (a clip within range).
  std::cerr << "GEGL filters run: " << filters.size() << ", unchanged at defaults: "
            << unchanged.join(QStringLiteral(", ")).toStdString() << '\n';
  CHECK(failed.isEmpty());
}

}  // namespace

// Scripts name GEGL filters, their settings and their choices by GEGL's own names (filter ids
// `gegl.<operation>`, property names, enum nicks), so a GEGL upgrade that renamed one would
// break saved scripts: these are pinned (docs/scripting-api-changes.md, 2026-11-02). Through
// patchy.gegl.filters(), as a script sees them. Skips in a build without the bundle.
void ui_gegl_script_identifiers_are_pinned() {
  if (patchy::ui::bundled_gegl_library().isEmpty()) {
    patchy::test::skip("this build has no GEGL of its own");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script_verbose(window, QStringLiteral(R"JS(
    var pinned = {
      "gegl.gaussian-blur": {"std-dev-x": "number", "std-dev-y": "number", "filter": "auto fir iir",
                             "abyss-policy": "none clamp black white", "clip-extent": "boolean"},
      "gegl.pixelize": {"norm": "diamond round square", "size-x": "integer", "size-y": "integer",
                        "background": "default foreground background black white"},
      "gegl.plasma": {"turbulence": "number", "seed": "integer"},
      "gegl.vignette": {"shape": "circle square diamond horizontal vertical", "radius": "number",
                        "softness": "number", "color": "default foreground background black white"},
      "gegl.unsharp-mask": {"std-dev": "number", "scale": "number", "threshold": "number"},
      "gegl.noise-hsv": {"holdness": "integer", "seed": "integer"},
      "gegl.cartoon": {"mask-radius": "number", "pct-black": "number"},
      "gegl.waves": {"amplitude": "number", "period": "number", "sampler-type": "nearest linear cubic nohalo lohalo"}
    };
    var filters = {};
    patchy.gegl.filters().forEach(function (f) { filters[f.id] = f; });
    Object.keys(pinned).forEach(function (id) {
      var filter = filters[id];
      if (!filter) throw new Error(id + " is not offered");
      Object.keys(pinned[id]).forEach(function (name) {
        var parameter = filter.parameters.filter(function (p) { return p.name === name; })[0];
        if (!parameter) throw new Error(id + " has no setting " + name);
        var expected = pinned[id][name];
        if (expected === "number" || expected === "integer" || expected === "boolean") {
          if (parameter.type !== expected) throw new Error(id + "." + name + " is " + parameter.type + ", not " + expected);
          return;
        }
        if (parameter.type !== "choice") throw new Error(id + "." + name + " is " + parameter.type + ", not a choice");
        var values = parameter.choices.map(function (c) { return typeof c === "object" ? c.value : c; });
        expected.split(" ").forEach(function (token) {
          if (values.indexOf(token) < 0) throw new Error(id + "." + name + " has no choice " + token + " (" + values.join(", ") + ")");
        });
      });
    });
    console.log("pinned ok");
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("pinned ok")));
}

// Every decimal GEGL setting's slider spans its practical range (GEGL's UI range) end to
// end: Exposure's Black Level (-0.1 to 0.1) once rounded to a 0..0 slider that could not go
// negative. A slider with thousands of ticks (Exposure, 0.001 over 20 stops) moves a
// usable amount per wheel notch and groove click. Skips without the bundle.
void ui_gegl_sliders_cover_their_practical_range() {
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
  QStringList wrong;
  int checked = 0;
  for (const auto& filter : registry->filters()) {
    patchy::ui::FilterParameterPanel panel;
    panel.rebuild(patchy::ui::filter_dialog_spec_for(filter), registry->default_invocation(filter.identifier),
                  patchy::ui::FilterParameterPanelOptions{});
    for (const auto& parameter : filter.catalog.parameters) {
      if (parameter.kind != patchy::FilterParameterKind::Double || !parameter.practical_minimum ||
          !parameter.practical_maximum) {
        continue;
      }
      const auto name = QString::fromStdString(parameter.control_object_name);
      auto* slider = panel.findChild<QSlider*>(name + QStringLiteral("Slider"));
      auto* spin = panel.findChild<QDoubleSpinBox*>(name + QStringLiteral("Spin"));
      if (slider == nullptr || spin == nullptr) {
        continue;  // shown another way (an angle dial, a choice)
      }
      ++checked;
      const double step = parameter.step.value_or(0.01);
      slider->setValue(slider->minimum());
      const double low = spin->value();
      slider->setValue(slider->maximum());
      const double high = spin->value();
      const auto label = QString::fromStdString(filter.identifier + "." + parameter.key);
      if (std::abs(low - *parameter.practical_minimum) > step || std::abs(high - *parameter.practical_maximum) > step) {
        wrong << QStringLiteral("%1 slider spans %2..%3, not %4..%5")
                     .arg(label).arg(low).arg(high).arg(*parameter.practical_minimum).arg(*parameter.practical_maximum);
      }
      const int ticks = slider->maximum() - slider->minimum();
      if (ticks > 2000 && (slider->singleStep() * 400 < ticks || slider->pageStep() * 20 < ticks)) {
        wrong << QStringLiteral("%1: %2 ticks, single step %3, page step %4")
                     .arg(label).arg(ticks).arg(slider->singleStep()).arg(slider->pageStep());
      }
    }
  }
  for (const auto& line : wrong) {
    std::fprintf(stderr, "  %s\n", qPrintable(line));
  }
  CHECK(wrong.isEmpty());
  CHECK(checked > 50);
  // The case that was reported, by name.
  const auto* exposure = registry->find("gegl.exposure");
  CHECK(exposure != nullptr);
  if (exposure != nullptr) {
    patchy::ui::FilterParameterPanel panel;
    panel.rebuild(patchy::ui::filter_dialog_spec_for(*exposure), registry->default_invocation("gegl.exposure"),
                  patchy::ui::FilterParameterPanelOptions{});
    auto* black = panel.findChild<QSlider*>(QStringLiteral("geglParam_black-levelSlider"));
    auto* black_spin = panel.findChild<QDoubleSpinBox*>(QStringLiteral("geglParam_black-levelSpin"));
    CHECK(black != nullptr && black_spin != nullptr);
    if (black != nullptr && black_spin != nullptr) {
      CHECK(black->maximum() - black->minimum() == 200);  // -0.1 to 0.1 in 0.001 steps
      CHECK(black->value() == 100 && std::abs(black_spin->value()) < 1e-9);
      black->setValue(0);
      CHECK(std::abs(black_spin->value() + 0.1) < 1e-9);
    }
  }
  // GEGL gives no UI range for these: an unbounded one becomes plus or minus 10 (pixel
  // coordinates 0 to 4096), an integer's whole range is capped, and the spin box still
  // takes the full range.
  const auto practical = [&](const char* id, const char* key) -> std::pair<double, double> {
    const auto* filter = registry->find(id);
    if (filter == nullptr) return {0.0, 0.0};
    for (const auto& parameter : filter->catalog.parameters) {
      if (parameter.key == key) {
        return {parameter.practical_minimum.value_or(-1.0e9), parameter.practical_maximum.value_or(1.0e9)};
      }
    }
    return {0.0, 0.0};
  };
  CHECK((practical("gegl.convolution-matrix", "a1") == std::pair<double, double>{-10.0, 10.0}));
  CHECK((practical("gegl.convolution-matrix", "divisor") == std::pair<double, double>{-1000.0, 1000.0}));
  CHECK((practical("gegl.linear-gradient", "start-x") == std::pair<double, double>{0.0, 4096.0}));
  CHECK((practical("gegl.dither", "red-levels") == std::pair<double, double>{2.0, 256.0}));
  if (const auto* dither = registry->find("gegl.dither"); dither != nullptr) {
    patchy::ui::FilterParameterPanel panel;
    panel.rebuild(patchy::ui::filter_dialog_spec_for(*dither), registry->default_invocation("gegl.dither"),
                  patchy::ui::FilterParameterPanelOptions{});
    auto* levels = panel.findChild<QSlider*>(QStringLiteral("geglParam_red-levelsSlider"));
    auto* levels_spin = panel.findChild<QSpinBox*>(QStringLiteral("geglParam_red-levelsSpin"));
    CHECK(levels != nullptr && levels_spin != nullptr);
    if (levels != nullptr && levels_spin != nullptr) {
      CHECK(levels->minimum() == 2 && levels->maximum() == 256 && levels->value() == 6);
      CHECK(levels_spin->maximum() == 65536);
    }
  }
}

// GIMP 3 layer effects through Patchy's GEGL
// (docs/xcf-layer-effects.md, "Layer effects (GIMP 3 filters)"):
// test-fixtures/xcf/gegl-effects.xcf (scripts/dev/xcf/make_gegl_effect_fixture.py) holds one
// 48 x 48 tile layer per effect over a gray background, with GIMP 3.2's render beside it.
// Each tile, with a margin for effects that reach past it, is compared with GIMP's render;
// effects outside the reader's table are left out and named in the notice. Saves
// test-artifacts/xcf-gegl-effects/{before,after,gimp}.png (before: the read without a runner).
void ui_xcf_gegl_layer_effects_match_gimp() {
  if (patchy::ui::bundled_gegl_library().isEmpty()) {
    patchy::test::skip("this build has no GEGL of its own");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(patchy::xcf::has_effect_runner());  // MainWindow installs it
  // Every operation the reader hands over is one Patchy offers as a GEGL filter.
  patchy::ui::MainWindowTestAccess::ensure_gegl_filters_scanned(window);
  const auto registry =
      patchy::ui::MainWindowTestAccess::gegl_filter_registry(window, QStringLiteral("gegl.gaussian-blur"));
  CHECK(registry != nullptr);
  if (registry == nullptr) {
    return;
  }
  QStringList not_offered;
  for (const auto& operation : patchy::xcf::effect_runner_operations()) {
    const auto id = "gegl." + operation.substr(operation.find(':') + 1);
    if (registry->find(id) == nullptr) {
      not_offered << QString::fromStdString(id);
    }
  }
  if (!not_offered.isEmpty()) {
    std::cerr << "  not offered: " << not_offered.join(QStringLiteral(", ")).toStdString() << '\n';
  }

  const auto path = patchy::test::committed_format_fixture_path("xcf", "gegl-effects.xcf");
  std::vector<std::string> notices;
  const auto after = patchy::xcf::DocumentIo::read_file(path, &notices);
  patchy::xcf::set_effect_runner({});
  std::vector<std::string> before_notices;
  const auto before = patchy::xcf::DocumentIo::read_file(path, &before_notices);
  CHECK(patchy::ui::install_xcf_effect_runner());

  const auto has = [](const std::vector<std::string>& list, std::string_view text) {
    return std::any_of(list.begin(), list.end(), [&](const std::string& n) { return n.find(text) != std::string::npos; });
  };
  CHECK(has(notices, "were applied"));
  CHECK(has(notices, std::string(patchy::xcf::skipped_effects_notice_prefix()) + "curves, median-blur" +
                         patchy::xcf::skipped_effects_notice_suffix()));
  CHECK(has(before_notices, "bloom"));

  const auto after_pixels = patchy::flatten_document_rgba8(after);
  const auto before_pixels = patchy::flatten_document_rgba8(before);
  QImage gimp(QString::fromStdWString(patchy::test::committed_format_fixture_path("xcf", "gegl-effects.png").wstring()));
  gimp = gimp.convertToFormat(QImage::Format_RGBA8888);
  CHECK(gimp.width() == after_pixels.width() && gimp.height() == after_pixels.height());
  if (gimp.width() != after_pixels.width() || gimp.height() != after_pixels.height()) {
    return;
  }
  const auto to_image = [](const patchy::PixelBuffer& pixels) {
    QImage image(pixels.width(), pixels.height(), QImage::Format_RGBA8888);
    for (int y = 0; y < pixels.height(); ++y) {
      std::memcpy(image.scanLine(y), pixels.pixel(0, y), static_cast<std::size_t>(pixels.width()) * 4U);
    }
    return image;
  };
  {
    std::error_code ignored;
    std::filesystem::create_directories(std::filesystem::path("test-artifacts") / "xcf-gegl-effects", ignored);
  }
  save_image_artifact("xcf-gegl-effects/after", to_image(after_pixels));
  save_image_artifact("xcf-gegl-effects/before", to_image(before_pixels));
  save_image_artifact("xcf-gegl-effects/gimp", gimp);

  // Mean and worst difference in 8-bit levels over a tile grown by `margin` pixels (shrunk
  // when negative).
  struct Tile {
    double mean{0.0};
    int worst{0};
  };
  const auto compare = [&](const patchy::PixelBuffer& pixels, const patchy::Rect& bounds, int margin) {
    Tile tile;
    double total = 0.0;
    int count = 0;
    for (int y = std::max(0, bounds.y - margin); y < std::min(pixels.height(), bounds.y + bounds.height + margin); ++y) {
      const auto* reference = gimp.constScanLine(y);
      for (int x = std::max(0, bounds.x - margin); x < std::min(pixels.width(), bounds.x + bounds.width + margin);
           ++x) {
        const auto* ours = pixels.pixel(x, y);
        for (int c = 0; c < 3; ++c) {
          const int delta = std::abs(static_cast<int>(ours[c]) - static_cast<int>(reference[x * 4 + c]));
          total += delta;
          tile.worst = std::max(tile.worst, delta);
        }
        ++count;
      }
    }
    tile.mean = count > 0 ? total / (count * 3.0) : 0.0;
    return tile;
  };
  // Each tile with an 8-pixel margin must be within a mean of one level of GIMP's. Exceptions:
  // Color to Alpha leaves every pixel partly transparent, so its tile shows the composite-space
  // difference (GIMP mixes over the background in linear light; docs/xcf.md, "Composite modes
  // and spaces"). GIMP 3.2 draws the pixels that Spread, Pick, Slur, Ripple and Motion Blur
  // move across the layer's edge a few pixels away from GEGL's own result, so those are
  // compared inside the tile, away from its edges.
  struct Bound {
    double mean;
    int margin;
  };
  const std::map<std::string, Bound> allowed = {
      {"color-to-alpha", {8.0, 8}},  {"noise-spread", {1.0, -6}}, {"noise-pick", {1.0, -3}},
      {"noise-slur", {1.0, -3}},     {"ripple", {1.0, -6}},       {"motion-blur-linear", {1.0, -8}},
  };
  QStringList failed;
  for (const auto& layer : before.layers()) {
    const auto& name = layer.name();
    if (name == "background" || name == "curves" || name == "median-blur") {
      continue;
    }
    const auto bounds = layer.bounds();
    const auto bound = allowed.contains(name) ? allowed.at(name) : Bound{1.0, 8};
    const auto ours = compare(after_pixels, bounds, bound.margin);
    const auto unfiltered = compare(before_pixels, bounds, bound.margin);
    const auto limit = bound.mean;
    std::cerr << "  " << name << ": mean " << ours.mean << " worst " << ours.worst << " (without the effect: mean "
              << unfiltered.mean << ")\n";
    if (ours.mean > limit) {
      failed << QString::fromStdString(name);
    }
  }
  if (!failed.isEmpty()) {
    std::cerr << "  effects off GIMP's render: " << failed.join(QStringLiteral(", ")).toStdString() << '\n';
  }
  CHECK(failed.isEmpty());
  CHECK(not_offered.isEmpty());
}

std::vector<patchy::test::TestCase> gegl_filter_tests() {
  return {
      {"ui_gegl_filters_list_and_apply_if_available", ui_gegl_filters_list_and_apply_if_available},
      {"ui_gegl_bundled_filters_all_run", ui_gegl_bundled_filters_all_run},
      {"ui_gegl_script_identifiers_are_pinned", ui_gegl_script_identifiers_are_pinned},
      {"ui_gegl_sliders_cover_their_practical_range", ui_gegl_sliders_cover_their_practical_range},
      {"ui_xcf_gegl_layer_effects_match_gimp", ui_xcf_gegl_layer_effects_match_gimp},
  };
}
