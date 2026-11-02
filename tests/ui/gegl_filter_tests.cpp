// GEGL filters (docs/gegl-filters.md, "Tests"): Patchy's own GEGL through patchy-gegl-host,
// its settings, exclusions, runs at every depth, the menu, scripts and the filter dialog.
// Both tests skip in a build without the bundle.

#include "core/pixel_depth.hpp"
#include "filters/filter_engine.hpp"
#include "ui/command_search.hpp"
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
#include <QElapsedTimer>
#include <QEvent>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
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
  // Cleared by the review: expired Shadow/Highlight patents, no superpixel patent found, and
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

std::vector<patchy::test::TestCase> gegl_filter_tests() {
  return {
      {"ui_gegl_filters_list_and_apply_if_available", ui_gegl_filters_list_and_apply_if_available},
      {"ui_gegl_bundled_filters_all_run", ui_gegl_bundled_filters_all_run},
  };
}
