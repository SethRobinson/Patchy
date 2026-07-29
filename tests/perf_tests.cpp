#include "core/layer_render_utils.hpp"
#include "core/rect_utils.hpp"
#include "psd/psd_document_io.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"

#include <QApplication>
#include <QByteArray>
#include <QDialog>
#include <QKeyEvent>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QRegion>
#include <QTabWidget>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace patchy::ui {

// Same friend-backdoor pattern as the visual suite's MainWindowTestAccess (one
// definition per binary; this one only exposes what the perf scenarios need).
class MainWindowTestAccess {
 public:
  static void open_document_path(MainWindow& window, const QString& path) { window.open_document_path(path); }
  static Document& document(MainWindow& window) { return window.document(); }
  static void toggle_layer_folder_expanded(MainWindow& window, patchy::LayerId id) {
    window.toggle_layer_folder_expanded(id);
  }
  static void refresh_layer_list(MainWindow& window) { window.refresh_layer_list(); }
  static void edit_active_layer_style(MainWindow& window) { window.edit_active_layer_style(); }
};

}  // namespace patchy::ui

namespace {

using Clock = std::chrono::steady_clock;

struct Candidate {
  patchy::Layer* layer{nullptr};
  patchy::LayerId layer_id{};
  QRegion dirty_region;
  QRect dirty_bounds;
  std::int64_t dirty_area{0};
  patchy::Rect moved_bounds{};
  std::string name;
};

struct Metric {
  std::string scenario;
  std::string name;
  std::int64_t dirty_area{0};
  int dirty_rects{0};
  double dirty_ms{0.0};
  double full_ms{0.0};
  bool identical{false};
};

QRect to_qrect(patchy::Rect rect) {
  return QRect(rect.x, rect.y, rect.width, rect.height);
}

std::int64_t rect_area(QRect rect) {
  return rect.isEmpty() ? 0 : static_cast<std::int64_t>(rect.width()) * static_cast<std::int64_t>(rect.height());
}

std::int64_t region_area(const QRegion& region) {
  std::int64_t area = 0;
  for (const auto& rect : region) {
    area += rect_area(rect);
  }
  return area;
}

std::string clean_name(std::string name) {
  for (auto& ch : name) {
    if (ch == '\n' || ch == '\r' || ch == '\t' || ch == '"') {
      ch = ' ';
    }
  }
  if (name.size() > 100U) {
    name.resize(100U);
  }
  return name;
}

void flatten_layers(std::vector<patchy::Layer>& layers, const std::string& prefix,
                    std::vector<std::pair<patchy::Layer*, std::string>>& out) {
  for (auto& layer : layers) {
    auto name = prefix.empty() ? layer.name() : prefix + "/" + layer.name();
    out.emplace_back(&layer, name);
    flatten_layers(layer.children(), name, out);
  }
}

bool images_equal_rgba(const QImage& left, const QImage& right) {
  if (left.size() != right.size()) {
    return false;
  }
  const auto left_rgba = left.convertToFormat(QImage::Format_RGBA8888);
  const auto right_rgba = right.convertToFormat(QImage::Format_RGBA8888);
  const auto row_bytes = static_cast<std::size_t>(left_rgba.width()) * 4U;
  for (int y = 0; y < left_rgba.height(); ++y) {
    if (std::memcmp(left_rgba.constScanLine(y), right_rgba.constScanLine(y), row_bytes) != 0) {
      return false;
    }
  }
  return true;
}

template <typename Callback>
double elapsed_ms(Callback&& callback) {
  const auto started = Clock::now();
  callback();
  return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

void ensure_artifact_dir() {
  std::filesystem::create_directories("test-artifacts");
}

void send_key(QWidget& widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent press(QEvent::KeyPress, key, modifiers);
  QApplication::sendEvent(&widget, &press);
  QKeyEvent release(QEvent::KeyRelease, key, modifiers);
  QApplication::sendEvent(&widget, &release);
  QApplication::processEvents();
}

patchy::ui::CanvasWidget* active_canvas(patchy::ui::MainWindow& window) {
  if (auto* canvas = dynamic_cast<patchy::ui::CanvasWidget*>(window.centralWidget()); canvas != nullptr) {
    return canvas;
  }
  if (auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget()); tabs != nullptr) {
    return dynamic_cast<patchy::ui::CanvasWidget*>(tabs->currentWidget());
  }
  return nullptr;
}

std::filesystem::path perf_psd_path() {
  const auto env_path = qgetenv("PATCHY_PERF_PSD");
  if (!env_path.isEmpty()) {
    return std::filesystem::path(env_path.toStdString());
  }
  return patchy::test::local_psd_fixture_path("Template.psd");
}

std::vector<Candidate> move_candidates(patchy::Document& document, QPoint delta, std::size_t max_count) {
  const QRect canvas_rect(0, 0, document.width(), document.height());
  std::vector<std::pair<patchy::Layer*, std::string>> flat;
  flatten_layers(document.layers(), {}, flat);

  std::vector<Candidate> candidates;
  for (auto& [layer, name] : flat) {
    if (layer == nullptr || !layer->visible() || layer->kind() != patchy::LayerKind::Pixel ||
        layer->pixels().empty()) {
      continue;
    }
    const auto old_bounds = layer->bounds();
    if (old_bounds.empty()) {
      continue;
    }
    const QRect old_rect = to_qrect(old_bounds);
    if (old_rect == canvas_rect || clean_name(name) == "Background") {
      continue;
    }
    const patchy::Rect moved_bounds{old_bounds.x + delta.x(), old_bounds.y + delta.y(), old_bounds.width,
                                    old_bounds.height};
    QRegion dirty;
    dirty += to_qrect(patchy::layer_bounds_with_effects(*layer, old_bounds));
    dirty += to_qrect(patchy::layer_bounds_with_effects(*layer, moved_bounds));
    dirty = dirty.intersected(canvas_rect);
    const auto area = region_area(dirty);
    if (area <= 0) {
      continue;
    }
    candidates.push_back(Candidate{layer, layer->id(), dirty, dirty.boundingRect(), area, moved_bounds, clean_name(name)});
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& lhs, const Candidate& rhs) { return lhs.dirty_area > rhs.dirty_area; });
  if (candidates.size() > max_count) {
    candidates.resize(max_count);
  }
  return candidates;
}

void write_metrics(const std::vector<Metric>& metrics) {
  ensure_artifact_dir();
  {
    std::ofstream csv("test-artifacts/dirty_rect_perf.csv");
    csv << "scenario,name,dirty_area,dirty_rects,dirty_ms,full_ms,identical\n";
    csv << std::fixed << std::setprecision(3);
    for (const auto& metric : metrics) {
      csv << '"' << metric.scenario << '"' << ',' << '"' << metric.name << '"' << ',' << metric.dirty_area << ','
          << metric.dirty_rects << ',' << metric.dirty_ms << ',' << metric.full_ms << ','
          << (metric.identical ? "true" : "false") << '\n';
    }
  }
  {
    std::ofstream json("test-artifacts/dirty_rect_perf.json");
    json << std::fixed << std::setprecision(3);
    json << "{\n  \"metrics\": [\n";
    for (std::size_t index = 0; index < metrics.size(); ++index) {
      const auto& metric = metrics[index];
      json << "    {\"scenario\": \"" << metric.scenario << "\", \"name\": \"" << metric.name
           << "\", \"dirty_area\": " << metric.dirty_area << ", \"dirty_rects\": " << metric.dirty_rects
           << ", \"dirty_ms\": " << metric.dirty_ms << ", \"full_ms\": " << metric.full_ms
           << ", \"identical\": " << (metric.identical ? "true" : "false") << "}";
      json << (index + 1U == metrics.size() ? "\n" : ",\n");
    }
    json << "  ]\n}\n";
  }
}

void collect_dirty_move_metrics(patchy::Document& document, const std::vector<Candidate>& candidates,
                                std::string_view scenario, std::vector<Metric>& metrics) {
  CHECK(!candidates.empty());
  const auto base_full = patchy::ui::qimage_from_document(document, true).convertToFormat(QImage::Format_RGBA8888);
  const bool strict = qEnvironmentVariableIsSet("PATCHY_PERF_STRICT");

  for (const auto& candidate : candidates) {
    CHECK(candidate.layer != nullptr);
    const auto old_bounds = candidate.layer->bounds();
    std::vector<patchy::ui::RenderedDocumentPatch> patches;
    double dirty_ms = elapsed_ms([&] {
      patches = patchy::ui::qimage_patches_from_document_region_with_layer_bounds(
          document, candidate.dirty_region, true, {{candidate.layer->id(), candidate.moved_bounds}});
      for (auto& patch : patches) {
        patch.image = patch.image.convertToFormat(QImage::Format_RGBA8888);
      }
    });

    auto patched = base_full.copy();
    {
      QPainter painter(&patched);
      painter.setCompositionMode(QPainter::CompositionMode_Source);
      for (const auto& patch : patches) {
        painter.drawImage(patch.document_rect.topLeft(), patch.image);
      }
    }

    QImage full_final;
    candidate.layer->set_bounds(candidate.moved_bounds);
    const double full_ms = elapsed_ms([&] {
      full_final = patchy::ui::qimage_from_document(document, true).convertToFormat(QImage::Format_RGBA8888);
    });
    candidate.layer->set_bounds(old_bounds);

    const bool identical = images_equal_rgba(patched, full_final);
    CHECK(identical);
    if (strict) {
      CHECK(dirty_ms < full_ms);
    }
    metrics.push_back(Metric{std::string(scenario), candidate.name, candidate.dirty_area,
                             candidate.dirty_region.rectCount(), dirty_ms, full_ms, identical});
    std::cout << "[PERF] " << scenario << ' ' << candidate.name << " dirty_ms=" << dirty_ms
              << " full_ms=" << full_ms << " area=" << candidate.dirty_area
              << " rects=" << candidate.dirty_region.rectCount() << '\n';
  }
}

void collect_cold_dirty_move_metric(patchy::Document& document, const Candidate& candidate,
                                    std::string_view scenario, std::vector<Metric>& metrics) {
  CHECK(candidate.layer != nullptr);
  const auto old_bounds = candidate.layer->bounds();
  std::vector<patchy::ui::RenderedDocumentPatch> patches;
  double dirty_ms = elapsed_ms([&] {
    patches = patchy::ui::qimage_patches_from_document_region_with_layer_bounds(
        document, candidate.dirty_region, true, {{candidate.layer->id(), candidate.moved_bounds}});
    for (auto& patch : patches) {
      patch.image = patch.image.convertToFormat(QImage::Format_RGBA8888);
    }
  });

  const auto base_full = patchy::ui::qimage_from_document(document, true).convertToFormat(QImage::Format_RGBA8888);
  auto patched = base_full.copy();
  {
    QPainter painter(&patched);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    for (const auto& patch : patches) {
      painter.drawImage(patch.document_rect.topLeft(), patch.image);
    }
  }

  QImage full_final;
  candidate.layer->set_bounds(candidate.moved_bounds);
  const double full_ms = elapsed_ms([&] {
    full_final = patchy::ui::qimage_from_document(document, true).convertToFormat(QImage::Format_RGBA8888);
  });
  candidate.layer->set_bounds(old_bounds);

  const bool identical = images_equal_rgba(patched, full_final);
  CHECK(identical);
  metrics.push_back(Metric{std::string(scenario), candidate.name, candidate.dirty_area,
                           candidate.dirty_region.rectCount(), dirty_ms, full_ms, identical});
  std::cout << "[PERF] " << scenario << ' ' << candidate.name << " dirty_ms=" << dirty_ms
            << " full_ms=" << full_ms << " area=" << candidate.dirty_area
            << " rects=" << candidate.dirty_region.rectCount() << '\n';
}

void template_psd_dirty_move_perf_if_available() {
  const auto path = perf_psd_path();
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] Template PSD missing: " << path.string() << '\n';
    return;
  }

  auto document = patchy::psd::DocumentIo::read_file(path);
  std::vector<Metric> metrics;
  const auto cold_nudge_candidates = move_candidates(document, QPoint(1, 0), 1);
  CHECK(!cold_nudge_candidates.empty());
  collect_cold_dirty_move_metric(document, cold_nudge_candidates.front(), "cold_nudge_1px", metrics);
  collect_dirty_move_metrics(document, move_candidates(document, QPoint(16, 16), 5), "move_16px", metrics);
  collect_dirty_move_metrics(document, move_candidates(document, QPoint(1, 0), 5), "nudge_1px", metrics);

  write_metrics(metrics);
}

void template_psd_ui_keyboard_nudge_perf_if_available() {
  const auto path = perf_psd_path();
  if (!std::filesystem::exists(path)) {
    return;
  }

  auto document = patchy::psd::DocumentIo::read_file(path);
  const auto candidates = move_candidates(document, QPoint(1, 0), 1);
  CHECK(!candidates.empty());
  const auto layer_id = candidates.front().layer_id;
  const auto layer_name = candidates.front().name;

  patchy::ui::MainWindow window;
  window.resize(1200, 800);
  window.add_document_session(std::move(document), QStringLiteral("Template perf"),
                              QString::fromStdString(path.string()));
  auto* canvas = active_canvas(window);
  CHECK(canvas != nullptr);
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  canvas->set_auto_select_layer(false);
  canvas->set_show_transform_controls(false);
  canvas->set_selected_layer_ids({layer_id});
  window.show();
  QApplication::processEvents();
  canvas->force_refresh();
  QApplication::processEvents();

  const auto before = canvas->render_cache_diagnostics();
  const auto elapsed = elapsed_ms([&] {
    send_key(*canvas, Qt::Key_Right);
    QApplication::processEvents();
  });
  const auto after = canvas->render_cache_diagnostics();
  std::cout << "[PERF_UI_NUDGE_1PX] " << layer_name << " elapsed_ms=" << elapsed
            << " full_refresh_delta=" << (after.full_refreshes - before.full_refreshes)
            << " dirty_batches_delta=" << (after.dirty_region_batches - before.dirty_region_batches)
            << " dirty_rects_delta=" << (after.dirty_region_rects - before.dirty_region_rects)
            << " dirty_pixels_delta=" << (after.dirty_region_pixels - before.dirty_region_pixels) << '\n';
}

// Zoom-step latency on a very large document (the 70 Mpx table tent PSB): each
// step times the synchronous zoom + repaint cycle after the initial composite has
// settled, printing render-diagnostics deltas so a full recomposite per step (the
// July 2026 slow-zoom report) is attributable from the output.
void tent_psb_zoom_step_perf_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("PSBtest/10cm table tent.psb");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] PSBtest/10cm table tent.psb missing\n";
    return;
  }

  patchy::ui::MainWindow window;
  window.resize(1600, 1000);
  if (qEnvironmentVariableIsSet("PATCHY_PERF_ONSCREEN")) {
    window.showMaximized();
  } else {
    window.show();
  }
  QApplication::processEvents();
  // The real user open path (import notices, canvas aid settings, link checks).
  patchy::ui::MainWindowTestAccess::open_document_path(window, QString::fromStdString(path.string()));
  QApplication::processEvents();
  auto* canvas = active_canvas(window);
  CHECK(canvas != nullptr);
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  QApplication::processEvents();
  canvas->force_refresh();
  QApplication::processEvents();
  // Settle the initial async composite before measuring steps.
  const auto settle_started = Clock::now();
  while (!canvas->render_settled() &&
         std::chrono::duration<double>(Clock::now() - settle_started).count() < 60.0) {
    canvas->repaint();
    QApplication::processEvents();
  }

  // PATCHY_PERF_ZOOM_SELECTION=1 zooms with an active selection: the marching-ants
  // outline retraces at device resolution on every zoom change below 100%.
  if (qEnvironmentVariableIsSet("PATCHY_PERF_ZOOM_SELECTION")) {
    canvas->select_all();
    QApplication::processEvents();
  }

  // PATCHY_PERF_ZOOM_BG=1 reproduces the July 2026 slow-zoom report: the Move tool's
  // passive transform box around the tent's 70 Mpx BG layer used to rescan the whole
  // alpha channel on every repaint.
  if (qEnvironmentVariableIsSet("PATCHY_PERF_ZOOM_BG")) {
    auto& doc = patchy::ui::MainWindowTestAccess::document(window);
    for (const auto& layer : std::as_const(doc).layers()) {
      if (layer.name() == "BG") {
        doc.set_active_layer(layer.id());
        canvas->set_selected_layer_ids({layer.id()});
        break;
      }
    }
    canvas->set_show_transform_controls(true);
    QApplication::processEvents();
  }

  // Start where a user starts on a huge document: fit-to-view (mip territory),
  // then sweep up through 100% into the deep-zoom pixel renderer and back.
  canvas->fit_to_view();
  canvas->repaint();
  QApplication::processEvents();
  std::cout << "[PERF_ZOOM] start zoom=" << canvas->zoom() << " dpr=" << canvas->devicePixelRatioF()
            << " canvas_size=" << canvas->width() << "x" << canvas->height() << '\n';
  for (int direction = 0; direction < 2; ++direction) {
    const double factor = direction == 0 ? 1.25 : 1.0 / 1.25;
    for (int step = 0; step < 26; ++step) {
      const auto before = canvas->render_cache_diagnostics();
      double paint_ms = 0.0;
      const auto elapsed = elapsed_ms([&] {
        canvas->zoom_at_widget_point(QPointF(canvas->width() / 2.0, canvas->height() / 2.0), factor);
        paint_ms = elapsed_ms([&] { canvas->repaint(); });
        QApplication::processEvents();
      });
      const auto after = canvas->render_cache_diagnostics();
      std::cout << "[PERF_ZOOM] " << (direction == 0 ? "in " : "out") << " step=" << step
                << " zoom=" << canvas->zoom() << " elapsed_ms=" << elapsed << " paint_ms=" << paint_ms
                << " full_refresh_delta=" << (after.full_refreshes - before.full_refreshes)
                << " dirty_batches_delta=" << (after.dirty_region_batches - before.dirty_region_batches)
                << '\n';
    }
  }
}

#ifdef Q_OS_WIN
// PATCHY_PERF_SAMPLER=1: a diagnostic sampling profiler for the scenarios in
// this binary. A worker suspends the MAIN thread every ~10 ms, walks its
// stack, and aggregates identical stacks; the destructor prints the hottest
// ones. Addresses are collected while suspended and symbolized after resume
// (dbghelp under a suspended peer risks the loader lock). Diagnostic only.
class MainThreadSampler {
 public:
  MainThreadSampler() {
    process_ = GetCurrentProcess();
    main_thread_ = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                              FALSE, GetCurrentThreadId());
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    symbols_ready_ = SymInitialize(process_, nullptr, TRUE) != FALSE;
    if (main_thread_ != nullptr) {
      worker_ = std::thread([this] { run(); });
    }
  }
  MainThreadSampler(const MainThreadSampler&) = delete;
  MainThreadSampler& operator=(const MainThreadSampler&) = delete;
  ~MainThreadSampler() {
    stop_ = true;
    if (worker_.joinable()) {
      worker_.join();
    }
    dump();
    if (main_thread_ != nullptr) {
      CloseHandle(main_thread_);
    }
  }

 private:
  void run() {
    while (!stop_) {
      sample();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  void sample() {
    DWORD64 addresses[26] = {};
    int depth = 0;
    if (SuspendThread(main_thread_) == static_cast<DWORD>(-1)) {
      return;
    }
    CONTEXT context = {};
    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (GetThreadContext(main_thread_, &context) != FALSE) {
      STACKFRAME64 frame = {};
      frame.AddrPC.Offset = context.Rip;
      frame.AddrPC.Mode = AddrModeFlat;
      frame.AddrFrame.Offset = context.Rbp;
      frame.AddrFrame.Mode = AddrModeFlat;
      frame.AddrStack.Offset = context.Rsp;
      frame.AddrStack.Mode = AddrModeFlat;
      while (depth < 26) {
        if (StackWalk64(IMAGE_FILE_MACHINE_AMD64, process_, main_thread_, &frame, &context, nullptr,
                        SymFunctionTableAccess64, SymGetModuleBase64, nullptr) == FALSE ||
            frame.AddrPC.Offset == 0) {
          break;
        }
        addresses[depth++] = frame.AddrPC.Offset;
      }
    }
    ResumeThread(main_thread_);
    if (depth == 0) {
      return;
    }
    std::string key;
    for (int i = 0; i < depth; ++i) {
      key += symbol_name(addresses[i]);
      key += '\n';
    }
    ++stacks_[key];
    ++samples_;
  }

  std::string symbol_name(DWORD64 address) {
    if (!symbols_ready_) {
      std::ostringstream raw;
      raw << "0x" << std::hex << address;
      return raw.str();
    }
    alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + 512] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 511;
    DWORD64 displacement = 0;
    if (SymFromAddr(process_, address, &displacement, symbol) == FALSE) {
      std::ostringstream raw;
      raw << "0x" << std::hex << address;
      return raw.str();
    }
    return symbol->Name;
  }

  void dump() {
    std::vector<std::pair<int, const std::string*>> ranked;
    ranked.reserve(stacks_.size());
    for (const auto& [stack, count] : stacks_) {
      ranked.emplace_back(count, &stack);
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first > rhs.first; });
    std::cout << "[SAMPLER] total_samples=" << samples_ << " unique_stacks=" << stacks_.size() << '\n';
    const auto top = std::min<std::size_t>(ranked.size(), 8U);
    for (std::size_t index = 0; index < top; ++index) {
      std::cout << "[SAMPLER] ---- stack #" << (index + 1) << " samples=" << ranked[index].first << '\n';
      std::istringstream lines(*ranked[index].second);
      std::string line;
      while (std::getline(lines, line)) {
        std::cout << "[SAMPLER]   " << line << '\n';
      }
    }
  }

  HANDLE process_{nullptr};
  HANDLE main_thread_{nullptr};
  bool symbols_ready_{false};
  std::atomic<bool> stop_{false};
  std::thread worker_;
  std::map<std::string, int> stacks_;
  int samples_{0};
};
#endif

// Layers-panel and layer-style-dialog latency on a deep imported document (the
// Quintavius Affinity trading-card template: ~440 layers in ten stacked card
// folders). Times the full-row-rebuild paths a user hits constantly: folder
// collapse/expand and opening/closing the Layer Style dialog without changes.
// Run with PATCHY_UI_PROFILE=1 for the per-phase breakdown lines.
void quintavius_layer_panel_perf_if_available() {
  const auto path = patchy::test::local_format_fixture_path(
      "af-spike/web_samples2", "Quintavius_map-of-noo__Frame.afphoto");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] Quintavius afphoto fixture missing: " << path.string() << '\n';
    return;
  }

#ifdef Q_OS_WIN
  std::unique_ptr<MainThreadSampler> sampler;
  if (qEnvironmentVariableIsSet("PATCHY_PERF_SAMPLER")) {
    sampler = std::make_unique<MainThreadSampler>();
  }
#endif
  patchy::ui::MainWindow window;
  window.resize(1600, 1000);
  if (qEnvironmentVariableIsSet("PATCHY_PERF_ONSCREEN")) {
    window.showMaximized();
  } else {
    window.show();
  }
  QApplication::processEvents();
  const auto open_ms = elapsed_ms([&] {
    patchy::ui::MainWindowTestAccess::open_document_path(window,
                                                         QString::fromStdString(path.string()));
    QApplication::processEvents();
  });
  auto* canvas = active_canvas(window);
  CHECK(canvas != nullptr);
  const auto settle_started = Clock::now();
  while (!canvas->render_settled() &&
         std::chrono::duration<double>(Clock::now() - settle_started).count() < 60.0) {
    canvas->repaint();
    QApplication::processEvents();
  }

  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  std::function<int(const patchy::Layer&)> descendant_count = [&](const patchy::Layer& layer) {
    int count = 0;
    for (const auto& child : layer.children()) {
      count += 1 + descendant_count(child);
    }
    return count;
  };
  const patchy::Layer* big_group = nullptr;
  int big_group_descendants = 0;
  for (const auto& layer : std::as_const(doc).layers()) {
    if (layer.kind() != patchy::LayerKind::Group) {
      continue;
    }
    const auto count = descendant_count(layer);
    if (count > big_group_descendants) {
      big_group_descendants = count;
      big_group = &layer;
    }
  }
  CHECK(big_group != nullptr);
  const auto group_id = big_group->id();
  const auto group_name = clean_name(big_group->name());

  // Activate a pixel layer deep in the biggest folder, like a user clicking a
  // card element before styling it.
  std::function<const patchy::Layer*(const patchy::Layer&)> first_pixel =
      [&](const patchy::Layer& layer) -> const patchy::Layer* {
    if (layer.kind() == patchy::LayerKind::Pixel && !layer.pixels().empty()) {
      return &layer;
    }
    for (const auto& child : layer.children()) {
      if (const auto* found = first_pixel(child); found != nullptr) {
        return found;
      }
    }
    return nullptr;
  };
  const auto* style_target = first_pixel(*big_group);
  CHECK(style_target != nullptr);
  doc.set_active_layer(style_target->id());
  patchy::ui::MainWindowTestAccess::refresh_layer_list(window);
  QApplication::processEvents();

  const auto rebuild_ms = elapsed_ms([&] {
    patchy::ui::MainWindowTestAccess::refresh_layer_list(window);
    QApplication::processEvents();
  });
  const auto collapse_ms = elapsed_ms([&] {
    patchy::ui::MainWindowTestAccess::toggle_layer_folder_expanded(window, group_id);
    QApplication::processEvents();
  });
  const auto expand_ms = elapsed_ms([&] {
    patchy::ui::MainWindowTestAccess::toggle_layer_folder_expanded(window, group_id);
    QApplication::processEvents();
  });

  // Open the Layer Style dialog and close it without touching anything; the
  // repeating timer stands in for the user's Cancel click.
  QTimer dismisser;
  dismisser.setInterval(50);
  bool dismissed = false;
  QObject::connect(&dismisser, &QTimer::timeout, &window, [&] {
    for (auto* top_level : QApplication::topLevelWidgets()) {
      auto* dialog = qobject_cast<QDialog*>(top_level);
      if (dialog != nullptr && dialog->isVisible() &&
          dialog->objectName() == QStringLiteral("patchyLayerStyleDialog")) {
        dismissed = true;
        dialog->reject();
      }
    }
  });
  dismisser.start();
  const auto style_dialog_ms = elapsed_ms([&] {
    patchy::ui::MainWindowTestAccess::edit_active_layer_style(window);
    QApplication::processEvents();
  });
  dismisser.stop();
  CHECK(dismissed);

  std::cout << "[PERF_LAYER_PANEL] open_ms=" << open_ms << " rebuild_ms=" << rebuild_ms
            << " collapse_ms=" << collapse_ms << " expand_ms=" << expand_ms
            << " style_dialog_cancel_ms=" << style_dialog_ms << " group=\"" << group_name
            << "\" descendants=" << big_group_descendants
            << " style_layer=\"" << clean_name(style_target->name()) << "\"\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  patchy::test::suppress_crash_dialogs();
  // PATCHY_PERF_ONSCREEN=1 keeps the platform window real so paint timings include
  // the native raster backend and device pixel ratio.
  if (!qEnvironmentVariableIsSet("PATCHY_PERF_ONSCREEN")) {
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  }
  QApplication app(argc, argv);
  try {
    if (argc > 1 && std::string_view(argv[1]) == "zoom") {
      tent_psb_zoom_step_perf_if_available();
      return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "layerpanel") {
      quintavius_layer_panel_perf_if_available();
      return 0;
    }
    template_psd_dirty_move_perf_if_available();
    template_psd_ui_keyboard_nudge_perf_if_available();
    tent_psb_zoom_step_perf_if_available();
    quintavius_layer_panel_perf_if_available();
  } catch (const std::exception& error) {
    std::cerr << "[FAIL] " << error.what() << '\n';
    return 1;
  }
  return 0;
}
