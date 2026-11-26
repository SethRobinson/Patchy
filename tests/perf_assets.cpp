// The Assets panel scenario of patchy_perf_tests (explicit only: `patchy_perf_tests assets`;
// docs/assets-panel.md, "Startup and large libraries"). It writes asset libraries of several
// sizes into test-artifacts/perf-assets, points Patchy at them with PATCHY_ASSETS_DIR and times
// what a user would feel: startup with the panel closed and open, opening the panel, switching
// category, searching, one library change, a library whose thumbnails or index are gone, and
// adding and placing a large layer beside the comparable existing commands.
//
// Every step reports three numbers: call_ms (the action's own synchronous time), longest_ms
// (the longest stretch the UI thread spent without returning to the event loop, the stall a
// user would feel) and settle_ms (until the panel has nothing left to load).
//
// Nothing here reads or writes the user's own assets folder.

#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "core/layer_metadata.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/app_settings.hpp"
#include "ui/asset_library.hpp"
#include "ui/assets_panel.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui/script_engine.hpp"
#include "test_harness.hpp"
#include "test_scratch_remove.hpp"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLinearGradient>
#include <QListWidget>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScrollBar>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace patchy::perf {

// tests/perf_startup.cpp: drops the file cache (Windows, elevated); false with the reason.
bool purge_file_cache_for_cold_start(std::string& why);
// tests/perf_tests.cpp: the private MainWindow calls this scenario times, through that file's
// MainWindowTestAccess (one definition per binary).
Document& assets_active_document(ui::MainWindow& window);
std::int64_t assets_active_session_id(ui::MainWindow& window);
void assets_select_layers(ui::MainWindow& window, const std::vector<LayerId>& ids);
bool assets_duplicate_layers_to_session(ui::MainWindow& window, std::int64_t source, std::vector<LayerId> ids,
                                        std::int64_t target);
bool assets_add_files_as_layers(ui::MainWindow& window, const QStringList& paths);
void assets_activate_session(ui::MainWindow& window, std::int64_t session);

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Timing {
  double call_ms{0.0};
  double longest_ms{0.0};
  double settle_ms{0.0};
};

// True while the Assets panel of `window` still has background work: a library load, a
// thumbnail being made, an asset being written.
bool assets_busy(ui::MainWindow& window) {
  (void)window;
  return false;  // everything is synchronous in this build
}

// Runs `action`, then pumps the event loop until the panel is idle. One processEvents pass is
// one stretch in which the UI thread answers nothing, so the longest pass (or the action
// itself) is the stall a user would feel.
Timing measure(ui::MainWindow& window, const std::function<void()>& action) {
  Timing timing;
  const auto start = Clock::now();
  action();
  timing.call_ms = ms_since(start);
  timing.longest_ms = timing.call_ms;
  int quiet_passes = 0;
  while (ms_since(start) < 600000.0) {
    const auto pass_start = Clock::now();
    QApplication::processEvents(QEventLoop::AllEvents);
    const auto pass_ms = ms_since(pass_start);
    timing.longest_ms = std::max(timing.longest_ms, pass_ms);
    if (assets_busy(window)) {
      quiet_passes = 0;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    if (pass_ms < 1.0 && ++quiet_passes >= 3) {
      break;
    }
  }
  timing.settle_ms = ms_since(start);
  return timing;
}

void report(int assets, const char* step, const Timing& timing, const char* cache, const std::string& note = {}) {
  std::cout << "[PERF] assets n=" << assets << ' ' << step << " call_ms=" << std::fixed << std::setprecision(1)
            << timing.call_ms << " longest_ms=" << timing.longest_ms << " settle_ms=" << timing.settle_ms
            << " cache=" << cache;
  if (!note.empty()) {
    std::cout << ' ' << note;
  }
  std::cout << '\n' << std::flush;
}

double private_megabytes() {
#ifdef Q_OS_WIN
  PROCESS_MEMORY_COUNTERS_EX counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                           sizeof(counters)) != FALSE) {
    return static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
  }
#endif
  return 0.0;
}

QString scratch_root() { return QDir::current().filePath(QStringLiteral("test-artifacts/perf-assets")); }

bool run_script(ui::MainWindow& window, const QString& source) {
  auto& host = window.script_engine_host();
  ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("perf-assets");
  (void)host.run_source(source, std::move(options));
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 60000) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  return !host.run_active() && !host.last_run_had_error();
}

void drain() {
  for (int pass = 0; pass < 4; ++pass) {
    QApplication::processEvents(QEventLoop::AllEvents);
  }
}

// A picture with soft and hard edges and no flat rows, so a PSD of it is a realistic size.
QImage painted_art(int width, int height, int seed) {
  QImage image(width, height, QImage::Format_RGBA8888);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  QLinearGradient gradient(0, 0, width, height);
  gradient.setColorAt(0.0, QColor::fromHsv((seed * 47) % 360, 160, 230));
  gradient.setColorAt(1.0, QColor::fromHsv((seed * 47 + 120) % 360, 200, 150));
  painter.setPen(Qt::NoPen);
  painter.setBrush(gradient);
  painter.drawRoundedRect(QRectF(width * 0.04, height * 0.04, width * 0.92, height * 0.92), width * 0.12, height * 0.12);
  std::uint32_t state = static_cast<std::uint32_t>(seed) * 2654435761U + 12345U;
  const auto next = [&state] {
    state = state * 1664525U + 1013904223U;
    return state >> 8;
  };
  const int blobs = std::max(4, (width * height) / 60000);
  for (int index = 0; index < std::min(blobs, 400); ++index) {
    painter.setBrush(QColor::fromHsv(static_cast<int>(next() % 360), 120 + static_cast<int>(next() % 120),
                                     150 + static_cast<int>(next() % 100), 150));
    const auto size = width / 12 + static_cast<int>(next() % static_cast<std::uint32_t>(std::max(2, width / 6)));
    painter.drawEllipse(QPoint(static_cast<int>(next() % static_cast<std::uint32_t>(width)),
                               static_cast<int>(next() % static_cast<std::uint32_t>(height))),
                        size / 2, size / 3);
  }
  return image;
}

Layer pixel_layer(Document& document, const char* name, const QImage& art, int x, int y) {
  Layer layer(document.allocate_layer_id(), name, ui::pixels_from_image_rgba(art));
  layer.set_bounds(Rect{x, y, art.width(), art.height()});
  return layer;
}

Layer shape_layer(Document& document, const char* name, Rect box, RgbColor color) {
  LiveShapeParams params;
  params.kind = LiveShapeKind::Rectangle;
  params.left = box.x;
  params.top = box.y;
  params.right = box.x + box.width;
  params.bottom = box.y + box.height;
  VectorShapeContent shape;
  shape.path.subpaths = generate_live_shape_subpaths(params);
  shape.fill.color = color;
  Layer layer(document.allocate_layer_id(), name, LayerKind::Pixel);
  layer.metadata()[kLayerMetadataVectorShape] = "1";
  layer.set_vector_shape(std::move(shape));
  update_vector_shape_raster(layer, Rect::from_size(document.width(), document.height()),
                             &std::as_const(document).metadata().patterns);
  return layer;
}

std::vector<LayerId> root_ids(const Document& document, bool skip_first) {
  std::vector<LayerId> ids;
  const auto& layers = document.layers();
  for (std::size_t index = skip_first ? 1U : 0U; index < layers.size(); ++index) {
    ids.push_back(layers[index].id());
  }
  return ids;
}

struct ScopedAssetsDir {
  explicit ScopedAssetsDir(const QString& directory) { qputenv("PATCHY_ASSETS_DIR", directory.toUtf8()); }
  ~ScopedAssetsDir() { qunsetenv("PATCHY_ASSETS_DIR"); }
  ScopedAssetsDir(const ScopedAssetsDir&) = delete;
  ScopedAssetsDir& operator=(const ScopedAssetsDir&) = delete;
};

struct AssetTemplate {
  QString name;
  QByteArray psd;
  QByteArray png;
  bool large{false};
};

QByteArray file_bytes(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void write_bytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  CHECK(file.write(bytes) == bytes.size());
}

Document large_document() {
  Document document(4000, 3000, PixelFormat::rgba8());
  QImage paper(4000, 3000, QImage::Format_RGBA8888);
  paper.fill(QColor(246, 244, 238));
  document.add_pixel_layer("Background", ui::pixels_from_image_rgba(paper));
  // "Photo": one full-size layer. "Poster": a group of three.
  document.add_layer(pixel_layer(document, "Photo", painted_art(4000, 3000, 3), 0, 0));
  Layer group(document.allocate_layer_id(), "Poster", LayerKind::Group);
  group.add_child(pixel_layer(document, "Sky", painted_art(4000, 3000, 5), 0, 0));
  group.add_child(pixel_layer(document, "Hills", painted_art(3600, 1800, 7), 200, 1100));
  group.add_child(pixel_layer(document, "Sign", painted_art(1800, 900, 9), 1100, 400));
  document.add_layer(std::move(group));
  return document;
}

// The assets every library is made of, written by the feature itself: eight small ones (pixel
// art, a shape, text) and two large ones (a 4000 x 3000 layer and a group of three).
std::vector<AssetTemplate> make_templates() {
  const auto directory = QDir(scratch_root()).filePath(QStringLiteral("templates"));
  const ScopedAssetsDir assets_dir(directory);
  std::vector<AssetTemplate> templates;
  {
    ui::MainWindow window;
    window.show();
    drain();
    Document small(800, 600, PixelFormat::rgba8());
    QImage paper(800, 600, QImage::Format_RGBA8888);
    paper.fill(Qt::white);
    small.add_pixel_layer("Background", ui::pixels_from_image_rgba(paper));
    const char* names[] = {"Leaf", "Star", "Heart", "Wave", "Plant", "Stamp"};
    for (int index = 0; index < 6; ++index) {
      small.add_layer(pixel_layer(small, names[index], painted_art(96 + index * 24, 80 + index * 16, index + 1),
                                  20 + index * 110, 40));
    }
    small.add_layer(shape_layer(small, "Badge", Rect{60, 320, 220, 110}, RgbColor{36, 110, 190}));
    window.add_document_session(std::move(small), QStringLiteral("Small"));
    drain();
    CHECK(run_script(window, QStringLiteral("var d = app.activeDocument; var t = d.addTextLayer('Summer Sale', "
                                            "{size: 40, x: 320, y: 420, color: '#1c4f8a', bold: true}); "
                                            "t.name = 'Label';")));
    drain();
    auto* add = window.findChild<QAction*>(QStringLiteral("layerAddToAssetsAction"));
    CHECK(add != nullptr);
    assets_select_layers(window, root_ids(std::as_const(assets_active_document(window)), true));
    drain();
    add->trigger();
    drain();
    window.add_document_session(large_document(), QStringLiteral("Large"));
    drain();
    assets_select_layers(window, root_ids(std::as_const(assets_active_document(window)), true));
    drain();
    add->trigger();
    for (int wait = 0; wait < 2000 && assets_busy(window); ++wait) {
      QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    drain();
    auto* library = window.findChild<ui::AssetLibrary*>();
    CHECK(library != nullptr);
    for (const auto& category : library->categories()) {
      for (const auto& asset : category.assets) {
        AssetTemplate entry;
        entry.name = asset.name;
        entry.psd = file_bytes(library->asset_path(asset.id));
        entry.png = file_bytes(library->thumbnail_path(asset.id));
        entry.large = asset.name == QStringLiteral("Photo") || asset.name == QStringLiteral("Poster");
        CHECK(!entry.psd.isEmpty() && !entry.png.isEmpty());
        templates.push_back(std::move(entry));
      }
    }
  }
  CHECK(templates.size() == 10U);
  return templates;
}

QString asset_id(int index) { return QStringLiteral("%1").arg(index, 32, 16, QLatin1Char('0')); }

// A library of `count` assets in four categories: General (a tenth), Icons (the big one, seven
// tenths), Photos (the large assets and a tenth) and Type (the rest).
void build_library(const QString& directory, int count, const std::vector<AssetTemplate>& templates) {
  CHECK(QDir().mkpath(directory));
  std::vector<const AssetTemplate*> small;
  std::vector<const AssetTemplate*> large;
  for (const auto& entry : templates) {
    (entry.large ? large : small).push_back(&entry);
  }
  const int large_count = count >= 1000 ? 20 : (count >= 100 ? 4 : 0);
  QJsonArray general;
  QJsonArray icons;
  QJsonArray photos;
  QJsonArray type;
  for (int index = 0; index < count; ++index) {
    const bool is_large = index < large_count;
    const auto& entry = is_large ? *large[static_cast<std::size_t>(index) % large.size()]
                                 : *small[static_cast<std::size_t>(index) % small.size()];
    const auto id = asset_id(index + 1);
    write_bytes(QDir(directory).filePath(id + QStringLiteral(".psd")), entry.psd);
    write_bytes(QDir(directory).filePath(id + QStringLiteral(".png")), entry.png);
    const QJsonObject asset{{QStringLiteral("id"), id},
                            {QStringLiteral("name"), QStringLiteral("%1 %2").arg(entry.name).arg(index, 4, 10,
                                                                                               QLatin1Char('0'))}};
    const int slot = index % 10;
    if (is_large || slot == 8) {
      photos.push_back(asset);
    } else if (slot == 0) {
      general.push_back(asset);
    } else if (slot == 9) {
      type.push_back(asset);
    } else {
      icons.push_back(asset);
    }
  }
  const auto category = [](const char* id, const char* name, const QJsonArray& assets) {
    return QJsonObject{{QStringLiteral("id"), QLatin1String(id)},
                       {QStringLiteral("name"), QLatin1String(name)},
                       {QStringLiteral("assets"), assets}};
  };
  const QJsonObject root{{QStringLiteral("format"), QStringLiteral("patchy-assets")},
                         {QStringLiteral("version"), 1},
                         {QStringLiteral("categories"),
                          QJsonArray{category("default", "", general), category("icons", "Icons", icons),
                                     category("photos", "Photos", photos), category("type", "Type", type)}}};
  write_bytes(QDir(directory).filePath(QStringLiteral("index.json")), QJsonDocument(root).toJson());
}

// A copy of a library without its thumbnails, or without its index.
void copy_library(const QString& from, const QString& to, bool thumbnails, bool index) {
  CHECK(QDir().mkpath(to));
  for (const auto& name : QDir(from).entryList(QDir::Files)) {
    if ((!thumbnails && name.endsWith(QStringLiteral(".png"))) || (!index && name == QStringLiteral("index.json"))) {
      continue;
    }
    CHECK(QFile::copy(QDir(from).filePath(name), QDir(to).filePath(name)));
  }
}

struct PanelWidgets {
  ui::AssetLibrary* library{nullptr};
  ui::AssetsPanel* panel{nullptr};
  QListWidget* grid{nullptr};
  QComboBox* categories{nullptr};
  QLineEdit* search{nullptr};
  QAction* open{nullptr};
};

PanelWidgets panel_widgets(ui::MainWindow& window) {
  PanelWidgets widgets;
  widgets.library = window.findChild<ui::AssetLibrary*>();
  widgets.panel = window.findChild<ui::AssetsPanel*>(QStringLiteral("assetsPanel"));
  widgets.grid = window.findChild<QListWidget*>(QStringLiteral("assetsGrid"));
  widgets.categories = window.findChild<QComboBox*>(QStringLiteral("assetsCategoryCombo"));
  widgets.search = window.findChild<QLineEdit*>(QStringLiteral("assetsSearchEdit"));
  widgets.open = window.findChild<QAction*>(QStringLiteral("windowPanelAssetsAction"));
  CHECK(widgets.library != nullptr && widgets.panel != nullptr && widgets.grid != nullptr &&
        widgets.categories != nullptr && widgets.search != nullptr && widgets.open != nullptr);
  return widgets;
}

// The saved panel layout with the Assets panel open, read at startup through PATCHY_PANEL_LAYOUT.
void write_open_panel_layout(QSettings& settings) {
  settings.setValue(QStringLiteral("window/panelLayoutVersion"), 5);
  settings.setValue(QStringLiteral("window/hiddenPanels"), QStringList{});
  settings.setValue(QStringLiteral("window/collapsedPanels"),
                    QStringList{QStringLiteral("colorDock"), QStringLiteral("propertiesDock"),
                                QStringLiteral("infoDock"), QStringLiteral("historyDock")});
  settings.sync();
}

void clear_panel_layout(QSettings& settings) {
  for (const auto* key : {"window/panelLayoutVersion", "window/hiddenPanels", "window/collapsedPanels",
                          "window/frontPanel", "window/frontPanels", "window/panelOrder", "window/panelGroups",
                          "window/panelSplitterState", "window/panelColumnWidth", "assets/currentCategory"}) {
    settings.remove(QLatin1String(key));
  }
  settings.sync();
}

// Window construction, show and the first idle, in this process: what startup costs with
// the given library, with the Assets panel closed or open.
Timing time_window_startup(bool panel_open, int* shown_items = nullptr) {
  auto settings = ui::app_settings();
  clear_panel_layout(settings);
  if (panel_open) {
    write_open_panel_layout(settings);
    qputenv("PATCHY_PANEL_LAYOUT", "1");
  }
  Timing timing;
  {
    std::optional<ui::MainWindow> window;
    const auto start = Clock::now();
    window.emplace();
    window->show();
    const auto constructed_ms = ms_since(start);
    timing = measure(*window, [] {});
    timing.call_ms = constructed_ms;
    timing.longest_ms = std::max(timing.longest_ms, constructed_ms);
    timing.settle_ms += constructed_ms;
    if (shown_items != nullptr) {
      *shown_items = panel_widgets(*window).grid->count();
    }
    qunsetenv("PATCHY_PANEL_LAYOUT");  // closing must not write the layout back
  }
  qunsetenv("PATCHY_PANEL_LAYOUT");
  clear_panel_layout(settings);
  drain();
  return timing;
}

void time_library_in_process(const QString& directory, int count, bool can_purge) {
  const ScopedAssetsDir assets_dir(directory);
  {
    auto settings = ui::app_settings();
    clear_panel_layout(settings);
  }
  // A window built once and thrown away: the first one in a process pays one-time costs
  // (fonts, style) that are no part of what is compared here.
  {
    ui::MainWindow warm_up;
    warm_up.show();
    drain();
  }
  report(count, "startup_panel_closed", time_window_startup(false), "warm");
  int shown = 0;
  report(count, "startup_panel_open", time_window_startup(true, &shown), "warm", "shown=" + std::to_string(shown));
  if (can_purge) {
    std::string why;
    if (purge_file_cache_for_cold_start(why)) {
      QThread::msleep(1500);
      report(count, "startup_panel_open", time_window_startup(true), "cold");
    }
  }

  {
    // The first open of the panel in a session, with the library's files in the file cache.
    ui::MainWindow first;
    first.show();
    drain();
    const auto first_widgets = panel_widgets(first);
    report(count, "open_panel", measure(first, [&] { first_widgets.open->trigger(); }), "warm",
           "shown=" + std::to_string(first_widgets.grid->count()));
  }
  const auto baseline_mb = private_megabytes();
  ui::MainWindow window;
  window.show();
  drain();
  const auto widgets = panel_widgets(window);
  bool purged = false;
  if (can_purge) {
    std::string why;
    purged = purge_file_cache_for_cold_start(why);
    if (purged) {
      QThread::msleep(1500);
    }
  }
  if (purged) {
    report(count, "open_panel", measure(window, [&] { widgets.open->trigger(); }), "cold",
           "shown=" + std::to_string(widgets.grid->count()));
  } else {
    widgets.open->trigger();
    measure(window, [] {});
  }
  const auto big = widgets.categories->findText(QStringLiteral("Icons"));
  const auto photos = widgets.categories->findText(QStringLiteral("Photos"));
  if (big >= 0) {
    report(count, "switch_to_big_category", measure(window, [&] { widgets.categories->setCurrentIndex(big); }),
           "warm", "shown=" + std::to_string(widgets.grid->count()));
  }
  if (photos >= 0) {
    report(count, "switch_to_photos_category",
           measure(window, [&] { widgets.categories->setCurrentIndex(photos); }), "warm",
           "shown=" + std::to_string(widgets.grid->count()));
  }
  // Every generated name holds an "a": the first keystroke matches the whole library.
  report(count, "search_first_keystroke", measure(window, [&] { widgets.search->setText(QStringLiteral("a")); }),
         "warm", "shown=" + std::to_string(widgets.grid->count()));
  report(count, "search_second_keystroke", measure(window, [&] { widgets.search->setText(QStringLiteral("al")); }),
         "warm", "shown=" + std::to_string(widgets.grid->count()));
  report(count, "search_cleared", measure(window, [&] { widgets.search->clear(); }), "warm");
  // What the panel holds once every asset has been shown once, scrolled end to end.
  widgets.search->setText(QStringLiteral("a"));
  measure(window, [] {});
  for (int step = 0; step <= 40; ++step) {
    widgets.grid->verticalScrollBar()->setValue(widgets.grid->verticalScrollBar()->maximum() * step / 40);
    measure(window, [] {});
  }
  std::cout << "[PERF] assets n=" << count << " memory_after_showing_every_asset private_mb_growth="
            << std::fixed << std::setprecision(1) << private_megabytes() - baseline_mb << '\n';
  widgets.search->clear();
  measure(window, [] {});

  if (count > 0) {
    const auto first = widgets.library->categories().front().assets.front().id;
    const auto second = widgets.library->categories().front().assets.back().id;
    report(count, "rename_one_asset",
           measure(window, [&] { CHECK(widgets.library->rename_asset(first, QStringLiteral("Renamed leaf"))); }),
           "warm");
    const auto bytes = widgets.library->read_asset_bytes(first);
    const auto picture = widgets.library->thumbnail(first);
    report(count, "add_one_small_asset", measure(window, [&] {
             CHECK(widgets.library
                       ->add_asset(widgets.panel->current_category_id(), QStringLiteral("Added leaf"),
                                   std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                                                 static_cast<std::size_t>(bytes.size())),
                                   picture)
                       .has_value());
           }),
           "warm");
    report(count, "delete_one_asset", measure(window, [&] { CHECK(widgets.library->remove_asset(second)); }), "warm");
  }
}

// A library whose thumbnails, or whose index, are gone: what recovering costs when the panel opens.
void time_damaged_library(const QString& directory, int count, const char* what, bool visit_categories) {
  const ScopedAssetsDir assets_dir(directory);
  ui::MainWindow window;
  window.show();
  drain();
  const auto widgets = panel_widgets(window);
  const auto step = std::string("open_panel_") + what;
  report(count, step.c_str(), measure(window, [&] { widgets.open->trigger(); }), "warm",
         "shown=" + std::to_string(widgets.grid->count()));
  if (!visit_categories) {
    return;
  }
  const auto big = widgets.categories->findText(QStringLiteral("Icons"));
  if (big >= 0) {
    const auto name = std::string("switch_to_big_category_") + what;
    report(count, name.c_str(), measure(window, [&] { widgets.categories->setCurrentIndex(big); }), "warm",
           "shown=" + std::to_string(widgets.grid->count()));
  }
  const auto photos = widgets.categories->findText(QStringLiteral("Photos"));
  if (photos >= 0) {
    const auto name = std::string("switch_to_photos_category_") + what;
    report(count, name.c_str(), measure(window, [&] { widgets.categories->setCurrentIndex(photos); }), "warm",
           "shown=" + std::to_string(widgets.grid->count()));
  }
}

// Adding a large layer and placing a large asset, beside Duplicate Layer to Document and
// Files as Layers on the same content.
void time_large_layers(const QString& directory) {
  CHECK(QDir().mkpath(directory));
  const ScopedAssetsDir assets_dir(directory);
  ui::MainWindow window;
  window.show();
  drain();
  const auto widgets = panel_widgets(window);
  widgets.open->trigger();
  drain();
  Document target(4000, 3000, PixelFormat::rgba8());
  QImage paper(4000, 3000, QImage::Format_RGBA8888);
  paper.fill(Qt::white);
  target.add_pixel_layer("Background", ui::pixels_from_image_rgba(paper));
  window.add_document_session(std::move(target), QStringLiteral("Target"));
  drain();
  const auto target_session = assets_active_session_id(window);
  window.add_document_session(large_document(), QStringLiteral("Large"));
  drain();
  const auto source_session = assets_active_session_id(window);
  const auto& source = std::as_const(assets_active_document(window));
  const auto photo_id = source.layers()[1].id();
  const auto poster_id = source.layers()[2].id();
  auto* add = window.findChild<QAction*>(QStringLiteral("layerAddToAssetsAction"));
  CHECK(add != nullptr);

  // The same layers as files, for Files as Layers.
  const auto photo_file = QDir(directory).filePath(QStringLiteral("photo-layer.psd"));
  const auto poster_file = QDir(directory).filePath(QStringLiteral("poster-group.psd"));
  for (const auto& [id, path] : {std::pair{photo_id, photo_file}, std::pair{poster_id, poster_file}}) {
    Document single(4000, 3000, PixelFormat::rgba8());
    single.add_layer(*source.find_layer(id));
    psd::DocumentIo::write_layered_rgb8_file(single, ui::to_filesystem_path(path));
  }

  for (const auto& [id, label] : {std::pair{photo_id, "one_layer_4000x3000"}, std::pair{poster_id, "group_of_three"}}) {
    assets_select_layers(window, {id});
    drain();
    const auto before = widgets.library->asset_count();
    report(1, (std::string("add_large_") + label).c_str(), measure(window, [&] { add->trigger(); }), "warm");
    CHECK(widgets.library->asset_count() == before + 1);
    report(1, (std::string("duplicate_to_document_") + label).c_str(), measure(window, [&] {
             CHECK(assets_duplicate_layers_to_session(window, source_session, {id}, target_session));
           }),
           "warm");
    // Duplicate activated the target: place the asset and the file there, then go back.
    const auto asset = widgets.library->categories().front().assets.back().id;
    report(1, (std::string("place_large_") + label).c_str(),
           measure(window, [&] { emit widgets.panel->place_requested({asset}); }), "warm");
    const auto file = id == photo_id ? photo_file : poster_file;
    report(1, (std::string("files_as_layers_") + label).c_str(),
           measure(window, [&] { CHECK(assets_add_files_as_layers(window, {file})); }), "warm");
    // Back to the source document for the next round.
    assets_activate_session(window, source_session);
    drain();
    CHECK(assets_active_session_id(window) == source_session);
  }
}

struct LaunchStat {
  double ready_median{0.0};
  double ready_min{0.0};
  double show_median{0.0};
  int count{0};
};

QString patchy_executable() {
  const QDir dir(QCoreApplication::applicationDirPath());
  for (const auto& name : {QStringLiteral("patchy.exe"), QStringLiteral("patchy"),
                           QStringLiteral("Patchy.app/Contents/MacOS/Patchy")}) {
    if (QFileInfo(dir.filePath(name)).isExecutable()) {
      return dir.filePath(name);
    }
  }
  return {};
}

// One launch of the app itself with --startup-benchmark (docs/startup-performance.md):
// {ready_ms, main_window.show}, or nullopt.
std::optional<std::pair<double, double>> launch_patchy(const QString& exe, const QString& profile,
                                                       const QString& assets, bool panel_open) {
  const auto report_path = QDir(profile).filePath(QStringLiteral("report.json"));
  (void)QFile::remove(report_path);
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("PATCHY_SETTINGS_DIR"), QDir(profile).filePath(QStringLiteral("settings")));
  environment.insert(QStringLiteral("PATCHY_USER_FONTS_DIR"), QDir(profile).filePath(QStringLiteral("user-fonts")));
  environment.insert(QStringLiteral("PATCHY_RECOVERY_DIR"), QDir(profile).filePath(QStringLiteral("recovery")));
  environment.insert(QStringLiteral("PATCHY_MCP_ENDPOINT"),
                     QStringLiteral("PatchyMcp-perf-assets-%1").arg(QCoreApplication::applicationPid()));
  environment.insert(QStringLiteral("PATCHY_NO_SINGLE_INSTANCE"), QStringLiteral("1"));
  environment.insert(QStringLiteral("PATCHY_NO_SOUND"), QStringLiteral("1"));
  environment.insert(QStringLiteral("PATCHY_NO_UPDATE_CHECK"), QStringLiteral("1"));
  environment.insert(QStringLiteral("PATCHY_ASSETS_DIR"), assets);
  environment.remove(QStringLiteral("PATCHY_STARTUP_TRACE"));
  if (panel_open) {
    environment.insert(QStringLiteral("PATCHY_PANEL_LAYOUT"), QStringLiteral("1"));
  } else {
    environment.remove(QStringLiteral("PATCHY_PANEL_LAYOUT"));
  }
  QProcess process;
  process.setProcessEnvironment(environment);
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(exe, {QStringLiteral("--headless"), QStringLiteral("--startup-benchmark"), report_path});
  if (!process.waitForStarted(30000) || !process.waitForFinished(180000)) {
    process.kill();
    process.waitForFinished(10000);
    return std::nullopt;
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    std::cout << "[PERF] assets launch failed: exit " << process.exitCode() << ' '
              << process.readAll().left(400).toStdString() << '\n';
    return std::nullopt;
  }
  QFile file(report_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }
  const auto root = QJsonDocument::fromJson(file.readAll()).object();
  double show_ms = 0.0;
  for (const auto& value : root.value(QStringLiteral("events")).toArray()) {
    const auto event = value.toObject();
    if (event.value(QStringLiteral("name")).toString() == QStringLiteral("main_window.show")) {
      show_ms = event.value(QStringLiteral("duration_ms")).toDouble();
      break;
    }
  }
  return std::pair{root.value(QStringLiteral("ready_ms")).toDouble(-1.0), show_ms};
}

LaunchStat launch_series(const QString& exe, const QString& profile, const QString& assets, bool panel_open,
                         int runs, bool cold) {
  std::vector<double> ready;
  std::vector<double> show;
  for (int run = 0; run < runs; ++run) {
    if (cold) {
      std::string why;
      if (!purge_file_cache_for_cold_start(why)) {
        break;
      }
      QThread::msleep(1500);
    }
    if (const auto sample = launch_patchy(exe, profile, assets, panel_open); sample.has_value() && sample->first >= 0.0) {
      ready.push_back(sample->first);
      show.push_back(sample->second);
    }
  }
  LaunchStat stat;
  stat.count = static_cast<int>(ready.size());
  if (!ready.empty()) {
    std::sort(ready.begin(), ready.end());
    std::sort(show.begin(), show.end());
    stat.ready_median = ready[ready.size() / 2];
    stat.ready_min = ready.front();
    stat.show_median = show[show.size() / 2];
  }
  return stat;
}

void time_app_startup(const std::vector<std::pair<int, QString>>& libraries, bool can_purge) {
  const auto exe = patchy_executable();
  if (exe.isEmpty()) {
    std::cout << "[SKIP] assets app startup: no patchy executable beside the perf tests\n";
    return;
  }
  const int runs = std::max(3, qEnvironmentVariableIntValue("PATCHY_PERF_ASSETS_STARTUP_RUNS") > 0
                                   ? qEnvironmentVariableIntValue("PATCHY_PERF_ASSETS_STARTUP_RUNS")
                                   : 7);
  for (const bool panel_open : {false, true}) {
    const auto profile = QDir(scratch_root()).filePath(panel_open ? QStringLiteral("profile-open")
                                                                  : QStringLiteral("profile-closed"));
    CHECK(QDir().mkpath(QDir(profile).filePath(QStringLiteral("settings/RTsoft/Patchy"))));
    if (panel_open) {
      QSettings settings(QDir(profile).filePath(QStringLiteral("settings/RTsoft/Patchy/Patchy.ini")),
                         QSettings::IniFormat);
      write_open_panel_layout(settings);
    }
    // The first launch on a fresh profile seeds the preset libraries: not what is compared.
    (void)launch_patchy(exe, profile, libraries.front().second, panel_open);
    (void)launch_patchy(exe, profile, libraries.front().second, panel_open);
    for (const auto& [count, directory] : libraries) {
      const auto warm = launch_series(exe, profile, directory, panel_open, runs, false);
      std::cout << "[PERF] assets n=" << count << " app_startup panel=" << (panel_open ? "open" : "closed")
                << " cache=warm ready_ms_median=" << std::fixed << std::setprecision(1) << warm.ready_median
                << " ready_ms_min=" << warm.ready_min << " main_window_show_ms=" << warm.show_median
                << " launches=" << warm.count << '\n'
                << std::flush;
      if (can_purge) {
        const auto cold = launch_series(exe, profile, directory, panel_open, 3, true);
        std::cout << "[PERF] assets n=" << count << " app_startup panel=" << (panel_open ? "open" : "closed")
                  << " cache=cold ready_ms_median=" << cold.ready_median << " ready_ms_min=" << cold.ready_min
                  << " main_window_show_ms=" << cold.show_median << " launches=" << cold.count << '\n'
                  << std::flush;
      }
    }
  }
}

}  // namespace

int run_assets_scenario() {
  const auto root = scratch_root();
  (void)patchy::test::remove_test_scratch_tree(ui::to_filesystem_path(root));
  CHECK(QDir().mkpath(root));
  std::vector<int> sizes{0, 100, 1000, 5000};
  if (const auto listed = qEnvironmentVariable("PATCHY_PERF_ASSETS_SIZES"); !listed.isEmpty()) {
    sizes.clear();
    for (const auto& text : listed.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
      sizes.push_back(text.toInt());
    }
  }
  std::string purge_reason;
  const bool can_purge =
      !qEnvironmentVariableIsSet("PATCHY_PERF_ASSETS_NO_COLD") && purge_file_cache_for_cold_start(purge_reason);
  std::cout << "[PERF] assets cold runs: " << (can_purge ? "yes (standby list purged before each)"
                                                         : "no (" + purge_reason + ")")
            << '\n';

  const auto templates = make_templates();
  qint64 small_bytes = 0;
  qint64 large_bytes = 0;
  for (const auto& entry : templates) {
    (entry.large ? large_bytes : small_bytes) += entry.psd.size();
  }
  std::cout << "[PERF] assets templates: 8 small (" << small_bytes / 8 / 1024 << " KB each on average), 2 large ("
            << large_bytes / 2 / (1024 * 1024) << " MB each on average)\n";

  std::vector<std::pair<int, QString>> libraries;
  for (const int count : sizes) {
    const auto directory = QDir(root).filePath(QStringLiteral("lib-%1").arg(count));
    build_library(directory, count, templates);
    libraries.emplace_back(count, directory);
  }
  if (!qEnvironmentVariableIsSet("PATCHY_PERF_ASSETS_NO_APP")) {
    time_app_startup(libraries, can_purge);
  }
  for (const auto& [count, directory] : libraries) {
    if (count > 0) {
      const auto no_thumbnails = directory + QStringLiteral("-no-thumbnails");
      const auto no_index = directory + QStringLiteral("-no-index");
      copy_library(directory, no_thumbnails, false, true);
      copy_library(directory, no_index, true, false);
      time_damaged_library(no_thumbnails, count, "thumbnails_deleted", true);
      time_damaged_library(no_index, count, "index_deleted", false);
    }
    time_library_in_process(directory, count, can_purge);
  }
  time_large_layers(QDir(root).filePath(QStringLiteral("lib-large-layers")));
  return 0;
}

}  // namespace patchy::perf
