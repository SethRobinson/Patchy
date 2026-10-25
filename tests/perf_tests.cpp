#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/rect_utils.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "core/worker_budget.hpp"
#include "formats/pdf_document_io.hpp"
#include "psd/psd_document_io.hpp"
#include "render/native_cmyk.hpp"
#include "ui/qt_paths.hpp"
#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/brush_automation.hpp"
#include "ui/canvas_widget.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include "ui/image_document_io.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/liquify_dialog.hpp"
#include "ui/main_window.hpp"
#include "ui/pdf_export.hpp"
#include "ui/pdf_import.hpp"

#include <QApplication>
#include <QByteArray>
#include <QComboBox>
#include <QDialog>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QImage>
#include <QPainter>
#if __has_include(<QPdfDocument>)
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#define PATCHY_PERF_HAVE_QPDF 1
#endif
#include <QPushButton>
#include <QRect>
#include <QRegion>
#include <QScopeGuard>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <span>
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
#include <tlhelp32.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace patchy::perf {
int run_startup_scenario();  // tests/perf_startup.cpp
}

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
  static void undo(MainWindow& window) { window.undo(); }
  static void duplicate_active_layer(MainWindow& window) { window.duplicate_active_layer(); }
  static void add_text_at(MainWindow& window, QPoint document_point) { window.add_text_at(document_point, QRect()); }
  static bool cmyk_proof(MainWindow& window) {
    return window.active_session() != nullptr && window.active_session()->cmyk_proof;
  }
  static void set_document_cmyk_proof(MainWindow& window, bool enabled) { window.set_document_cmyk_proof(enabled); }
  static void new_document(MainWindow& window, int width, int height, double ppi) {
    window.reset_document(width, height, Qt::white, QStringLiteral("New document"), ppi);
  }
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
  if (auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs")); tabs != nullptr) {
    return dynamic_cast<patchy::ui::CanvasWidget*>(tabs->currentWidget());
  }
  return nullptr;
}

std::filesystem::path perf_psd_path() {
  const auto env_path = qgetenv("PATCHY_PERF_PSD");
  if (!env_path.isEmpty()) {
    return patchy::ui::to_filesystem_path(env_path);
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
    // dbghelp accepts one SymInitialize per process; later samplers (one per
    // measured phase) reuse it or they would print raw addresses.
    static const bool symbols_initialized = [] {
      SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
      return SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
    }();
    symbols_ready_ = symbols_initialized;
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

// The main thread's handle, opened in main() for the scenario watchdog below.
HANDLE g_main_thread_handle = nullptr;

// One-shot main-thread stack for the scenario watchdog: when a scenario stalls, the
// watchdog thread suspends the main thread, walks it, and prints the frames after the
// resume (symbolizing under a suspended peer risks the loader lock, as in the sampler).
// A hang then names its blocked frame instead of leaving a silent process.
void print_main_thread_stack() {
  if (g_main_thread_handle == nullptr) {
    return;
  }
  HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  SymInitialize(process, nullptr, TRUE);  // FALSE when the sampler already did; lookups still work.
  DWORD64 addresses[48] = {};
  int depth = 0;
  if (SuspendThread(g_main_thread_handle) == static_cast<DWORD>(-1)) {
    std::fprintf(stderr, "  main thread stack: SuspendThread failed\n");
    return;
  }
  CONTEXT context = {};
  context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
  if (GetThreadContext(g_main_thread_handle, &context) != FALSE) {
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    while (depth < 48) {
      if (StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, g_main_thread_handle, &frame, &context, nullptr,
                      SymFunctionTableAccess64, SymGetModuleBase64, nullptr) == FALSE ||
          frame.AddrPC.Offset == 0) {
        break;
      }
      addresses[depth++] = frame.AddrPC.Offset;
    }
  }
  ResumeThread(g_main_thread_handle);
  std::fprintf(stderr, "  main thread stack (%d frames):\n", depth);
  for (int i = 0; i < depth; ++i) {
    alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + 512] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 511;
    DWORD64 displacement = 0;
    if (SymFromAddr(process, addresses[i], &displacement, symbol) != FALSE) {
      std::fprintf(stderr, "    %s+0x%llx\n", symbol->Name, static_cast<unsigned long long>(displacement));
    } else {
      std::fprintf(stderr, "    0x%llx\n", static_cast<unsigned long long>(addresses[i]));
    }
  }
  std::fflush(stderr);
}
#endif

// Names the windows open when a scenario stalls (the UI runner does the same): a
// modal dialog nobody answers is the usual cause. Built on the GUI thread, which a
// modal exec() still serves; a GUI thread that never answers is busy or deadlocked.
void report_open_windows() {
  auto listing = std::make_shared<std::promise<std::string>>();
  auto answer = listing->get_future();
  QMetaObject::invokeMethod(
      qApp,
      [listing] {
        std::string text;
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (!widget->isVisible()) {
            continue;
          }
          text += std::string("  open window: ") + widget->metaObject()->className() + " objectName='" +
                  widget->objectName().toStdString() + "' title='" + widget->windowTitle().toStdString() + "'" +
                  (widget->isModal() ? " modal" : "") + '\n';
        }
        if (auto* modal = QApplication::activeModalWidget(); modal != nullptr) {
          text += std::string("  active modal: ") + modal->metaObject()->className() + " objectName='" +
                  modal->objectName().toStdString() + "'\n";
        }
        listing->set_value(text.empty() ? std::string("  no visible windows\n") : text);
      },
      Qt::QueuedConnection);
  if (answer.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
    std::fputs(answer.get().c_str(), stderr);
  } else {
    std::fputs("  the GUI thread did not answer within 5 s: busy or deadlocked, not waiting in an event loop\n", stderr);
  }
#ifdef Q_OS_WIN
  print_main_thread_stack();
#endif
}

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

// Mouse events for the interaction scenarios (the visual suite's helper lives in
// its own support TU; this binary only needs the plain form).
void send_mouse(QWidget& widget, QEvent::Type type, QPoint position, Qt::MouseButton button,
                Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QMouseEvent event(type, QPointF(position), QPointF(widget.mapToGlobal(position)), button, buttons, modifiers);
  QApplication::sendEvent(&widget, &event);
}

// First fully opaque pixel of a rgba8 layer inside its opaque extent inset by
// `inset` document pixels, scanning outward from the middle row, as a
// document-space point. The inset keeps a press there inside the Move tool's
// passive transform box rather than on one of its edge handles (a handle grab
// starts Free Transform instead of a move). nullopt when nothing qualifies.
std::optional<QPoint> opaque_document_point(const patchy::Layer& layer, int inset) {
  const auto& pixels = layer.pixels();
  if (pixels.empty() || pixels.format().bit_depth != patchy::BitDepth::UInt8 || pixels.format().channels < 4) {
    return std::nullopt;
  }
  const auto extent = patchy::visible_alpha_local_bounds(layer);
  if (!extent.has_value()) {
    return std::nullopt;
  }
  const QRect scan(extent->x + inset, extent->y + inset, extent->width - 2 * inset, extent->height - 2 * inset);
  if (scan.isEmpty()) {
    return std::nullopt;
  }
  for (int dy = 0; dy < scan.height(); ++dy) {
    const auto y = scan.top() + (scan.height() / 2 + dy) % scan.height();
    for (int x = scan.left(); x <= scan.right(); ++x) {
      if (pixels.pixel(x, y)[3] >= 200) {
        return QPoint(layer.bounds().x + x, layer.bounds().y + y);
      }
    }
  }
  return std::nullopt;
}

// Layer-selection and move-start latency on a document with thousands of
// layers (Little-Everywhere-fixed.psd: 2600x2100 px, 7229 layers; September
// 2026 report: selecting a layer and starting a Move-tool drag each stalled
// about two seconds). Times a Layers-panel row click, a canvas auto-select
// click, and the press / first frame / release of a Move-tool drag, with
// render-diagnostics deltas. PATCHY_UI_PROFILE=1 adds the per-phase lines and
// PATCHY_PERF_SAMPLER=1 samples only the measured interactions: the sampler
// starts after the open has settled so the load cannot swamp the stacks.
void many_layers_select_and_move_perf_if_available() {
  // PATCHY_PERF_MANYLAYERS_PSD=<path> points the scenario at another document
  // (a styled poster, say); the default is the 2056-layer file.
  const auto env_path = qgetenv("PATCHY_PERF_MANYLAYERS_PSD");
  const auto path = env_path.isEmpty() ? patchy::test::local_psd_fixture_path("Little-Everywhere-fixed.psd")
                                       : patchy::ui::to_filesystem_path(env_path);
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] many-layers fixture missing: " << path.string() << '\n';
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
  const auto open_ms = elapsed_ms([&] {
    patchy::ui::MainWindowTestAccess::open_document_path(window, QString::fromStdString(path.string()));
    QApplication::processEvents();
  });
  auto* canvas = active_canvas(window);
  CHECK(canvas != nullptr);
  const auto settle_started = Clock::now();
  while (!canvas->render_settled() &&
         std::chrono::duration<double>(Clock::now() - settle_started).count() < 120.0) {
    canvas->repaint();
    QApplication::processEvents();
  }
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  // Visible pixel leaves (visible ancestors too), bottom to top.
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  std::vector<const patchy::Layer*> leaves;
  int layer_count = 0;
  int styled_count = 0;
  std::function<void(const std::vector<patchy::Layer>&, bool)> collect =
      [&](const std::vector<patchy::Layer>& layers, bool ancestors_visible) {
        for (const auto& layer : layers) {
          ++layer_count;
          if (layer.layer_style().effects_visible && !layer.layer_style().empty()) {
            ++styled_count;
          }
          const bool visible = ancestors_visible && layer.visible();
          if (layer.kind() == patchy::LayerKind::Group) {
            collect(layer.children(), visible);
          } else if (visible && layer.kind() == patchy::LayerKind::Pixel && !layer.pixels().empty()) {
            leaves.push_back(&layer);
          }
        }
      };
  collect(std::as_const(doc).layers(), true);
  CHECK(!leaves.empty());

  std::map<patchy::LayerId, QListWidgetItem*> rows;
  for (int row = 0; row < layer_list->count(); ++row) {
    auto* item = layer_list->item(row);
    rows[static_cast<patchy::LayerId>(item->data(patchy::ui::kLayerIdRole).toULongLong())] = item;
  }
  // Panel target: a leaf with a row, from the middle of the stack. Canvas
  // target: the topmost leaf with an opaque pixel, so nothing occludes it.
  std::vector<const patchy::Layer*> leaves_with_rows;
  for (const auto* leaf : leaves) {
    if (rows.contains(leaf->id())) {
      leaves_with_rows.push_back(leaf);
    }
  }
  CHECK(!leaves_with_rows.empty());
  const auto* panel_target = leaves_with_rows[leaves_with_rows.size() / 2];
  const patchy::Layer* canvas_target = nullptr;
  std::optional<QPoint> canvas_point;
  // 40 document px keeps the press clear of the passive box's handles at any
  // zoom above 25%.
  for (auto it = leaves.rbegin(); it != leaves.rend() && !canvas_point.has_value(); ++it) {
    if (patchy::layer_effectively_locks_position(std::as_const(doc).layers(), (*it)->id())) {
      continue;  // the Move tool skips these, so the click would land below
    }
    canvas_point = opaque_document_point(**it, 40);
    canvas_target = *it;
  }
  CHECK(canvas_point.has_value());

  // One sampler per measured phase so each dump attributes that phase alone.
#ifdef Q_OS_WIN
  std::unique_ptr<MainThreadSampler> sampler;
  const bool sampling = qEnvironmentVariableIsSet("PATCHY_PERF_SAMPLER");
  const auto begin_phase = [&] {
    if (sampling) {
      sampler = std::make_unique<MainThreadSampler>();
    }
  };
  const auto end_phase = [&](const char* name) {
    if (sampler) {
      std::cout << "[SAMPLER_PHASE] " << name << '\n';
      sampler.reset();
    }
  };
#else
  const auto begin_phase = [] {};
  const auto end_phase = [](const char*) {};
#endif

  // 0. Optional compositor profile: PATCHY_PERF_RENDER_PHASE=1 renders the
  //    whole document once on this thread (set PATCHY_RENDER_SINGLE_THREADED=1
  //    too so the sampler sees the compositor rather than a worker wait).
  double full_render_ms = 0.0;
  if (qEnvironmentVariableIsSet("PATCHY_PERF_RENDER_PHASE")) {
    begin_phase();
    full_render_ms = elapsed_ms([&] {
      const auto image = patchy::ui::qimage_from_document(std::as_const(doc), true);
      CHECK(!image.isNull());
    });
    end_phase("full_render");
    std::cout << "[PERF_MANY_LAYERS_RENDER] full_render_ms=" << full_render_ms << '\n';
  }

  // 1. Layers-panel row click.
  begin_phase();
  const auto select_row_ms = elapsed_ms([&] {
    layer_list->setCurrentItem(rows[panel_target->id()], QItemSelectionModel::ClearAndSelect);
    QApplication::processEvents();
  });
  end_phase("select_row");

  // 2. Canvas auto-select click on the topmost layer's opaque pixel.
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();
  const auto click_position = canvas->widget_position_for_document_point(*canvas_point);
  begin_phase();
  const auto click_ms = elapsed_ms([&] {
    send_mouse(*canvas, QEvent::MouseButtonPress, click_position, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, click_position, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  });
  end_phase("canvas_click");
  const auto active_after_click = doc.active_layer_id();
  // The drag moves whatever the click activated (a masked or occluding layer
  // can win the hit test over the picked leaf); its bounds are the move check.
  CHECK(active_after_click.has_value());
  const auto* drag_target = std::as_const(doc).find_layer(*active_after_click);
  CHECK(drag_target != nullptr);
  const auto drag_target_bounds_before = drag_target->bounds();

  // 3. Move-tool drag of that layer: press, first frame past the drag
  //    threshold, a second frame, release (each timed on its own).
  const auto before = canvas->render_cache_diagnostics();
  begin_phase();
  const auto press_ms = elapsed_ms([&] {
    send_mouse(*canvas, QEvent::MouseButtonPress, click_position, Qt::LeftButton, Qt::LeftButton);
    QApplication::processEvents();
  });
  end_phase("move_press");
  // A press inside the box must start a move, never a transform session.
  CHECK(!canvas->free_transform_active());
  // Six frames: the live path's per-frame cost, and whether the slow-frame
  // latch (proxy/base build) engages and what that costs.
  constexpr int kDragFrames = 6;
  std::vector<double> frame_ms;
  QPoint drag_position = click_position;
  const auto preview_started = Clock::now();
  begin_phase();
  for (int frame = 1; frame <= kDragFrames; ++frame) {
    drag_position = click_position + QPoint(30 * frame, 20 * frame);
    frame_ms.push_back(elapsed_ms([&] {
      send_mouse(*canvas, QEvent::MouseMove, drag_position, Qt::NoButton, Qt::LeftButton);
      QApplication::processEvents();
      canvas->repaint();
    }));
  }
  end_phase("move_frames");
  // Report immediate feedback separately from a cold background preview.
  const auto settle_canvas = [&] {
    const auto deadline = Clock::now() + std::chrono::seconds(120);
    while (!canvas->render_settled() && Clock::now() < deadline) {
      QApplication::processEvents();
    }
    CHECK(canvas->render_settled());
  };
  settle_canvas();
  const auto preview_ready_ms = std::chrono::duration<double, std::milli>(Clock::now() - preview_started).count();
  begin_phase();
  const auto release_ms = elapsed_ms([&] {
    send_mouse(*canvas, QEvent::MouseButtonRelease, drag_position, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  });
  end_phase("move_release");
  // A second drag of the same layer: the retained caches and warm style masks
  // are what a user hits on every move after the first.
  begin_phase();
  const auto press2_ms = elapsed_ms([&] {
    send_mouse(*canvas, QEvent::MouseButtonPress, drag_position, Qt::LeftButton, Qt::LeftButton);
    QApplication::processEvents();
  });
  std::vector<double> frame2_ms;
  const auto drag2_origin = drag_position;
  for (int frame = 1; frame <= kDragFrames; ++frame) {
    drag_position = drag2_origin - QPoint(30 * frame, 20 * frame);
    frame2_ms.push_back(elapsed_ms([&] {
      send_mouse(*canvas, QEvent::MouseMove, drag_position, Qt::NoButton, Qt::LeftButton);
      QApplication::processEvents();
      canvas->repaint();
    }));
  }
  const auto release2_ms = elapsed_ms([&] {
    send_mouse(*canvas, QEvent::MouseButtonRelease, drag_position, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  });
  end_phase("move2");
  const auto exact_settle_ms = elapsed_ms(settle_canvas);
  std::ostringstream frames2;
  for (std::size_t index = 0; index < frame2_ms.size(); ++index) {
    frames2 << (index == 0 ? "" : "/") << frame2_ms[index];
  }
  const auto after = canvas->render_cache_diagnostics();
  const auto* moved_target = std::as_const(doc).find_layer(*active_after_click);
  const bool moved = moved_target != nullptr && (moved_target->bounds().x != drag_target_bounds_before.x ||
                                                 moved_target->bounds().y != drag_target_bounds_before.y);
  std::ostringstream frames;
  for (std::size_t index = 0; index < frame_ms.size(); ++index) {
    frames << (index == 0 ? "" : "/") << frame_ms[index];
  }

  std::cout << "[PERF_MANY_LAYERS] file=\"" << path.filename().string() << "\" layers=" << layer_count << " visible_pixel_leaves=" << leaves.size()
            << " rows=" << layer_list->count() << " open_ms=" << open_ms << " select_row_ms=" << select_row_ms
            << " canvas_click_ms=" << click_ms << " move_press_ms=" << press_ms
            << " move_frames_ms=" << frames.str() << " move_release_ms=" << release_ms
            << " move2_press_ms=" << press2_ms << " move2_frames_ms=" << frames2.str()
            << " move2_release_ms=" << release2_ms << " styled_layers=" << styled_count
            << " preview_ready_ms=" << preview_ready_ms << " exact_settle_ms=" << exact_settle_ms
            << " full_refresh_delta=" << (after.full_refreshes - before.full_refreshes)
            << " proxy_previews_delta=" << (after.move_proxy_previews - before.move_proxy_previews)
            << " outline_previews_delta=" << (after.move_outline_previews - before.move_outline_previews)
            << " panel_target=\"" << clean_name(panel_target->name()) << "\" canvas_target=\""
            << clean_name(canvas_target->name()) << "\" active_after_click="
            << (active_after_click.has_value() ? std::to_string(*active_after_click) : std::string("none"))
            << " (canvas_target_id=" << canvas_target->id() << ") drag_target=\""
            << (moved_target != nullptr ? clean_name(moved_target->name()) : std::string("?")) << "\" moved="
            << (moved ? 1 : 0) << '\n';
  CHECK(moved);
  if (qEnvironmentVariableIsSet("PATCHY_PERF_VERIFY_FINAL")) {
    const auto capture = [canvas] {
      QImage image(canvas->size(), QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::transparent);
      canvas->render(&image);
      return image;
    };
    const auto committed = capture();
    canvas->force_refresh();
    settle_canvas();
    const bool identical = images_equal_rgba(committed, capture());
    std::cout << "[PERF_FINAL_RENDER_CHECK] matches_full_refresh=" << (identical ? 1 : 0) << '\n';
    CHECK(identical);
  }
}

// Free Transform rotate-drag cost with and without a layer mask (GitHub #13
// and #15: "rotating a layer with a mask causes severe lag" / "massive CPU
// usage", reported on a 12-thread laptop). Synthetic, so it runs on any
// machine: a noise layer covering 60% of the canvas is rotated by dragging the
// rotate handle, plain (the cheap rotated-blit preview), with a linked
// canvas-sized reveal-all mask (must stay on the blit), and with a painted
// mask (the composited-patch preview), timing every mouse-move plus its repaint
// and then the commit. PATCHY_RENDER_THREADS=4 prices the low-core case.
//
// The ceilings are relative or latch-backed so machine load cannot flake them:
// the reveal-all mask never leaves the blit path (no proxy, frames within a
// small factor of the unmasked drag), and every configuration's median frame
// stays under the 100 ms live-frame latch, which the proxy guarantees for the
// composited cases once a frame runs over it.
void masked_layer_rotate_perf() {
  struct Size {
    int width;
    int height;
  };
  constexpr double kLiveFrameLatchMs = 100.0;  // live_preview_frame_latch_ms default
  for (const auto size : {Size{1920, 1080}, Size{4000, 3000}}) {
    double unmasked_median_ms = 0.0;
    // 0 = no mask, 1 = reveal-all mask (what Add Layer Mask creates), 2 = a
    // painted mask hiding the layer's right third.
    for (const int masked : {0, 1, 2}) {
      patchy::Document document(size.width, size.height, patchy::PixelFormat::rgba8());
      patchy::PixelBuffer backdrop(size.width, size.height, patchy::PixelFormat::rgba8());
      backdrop.clear(255);
      document.add_layer(patchy::Layer(document.allocate_layer_id(), "Backdrop", std::move(backdrop)));

      const int layer_width = size.width * 6 / 10;
      const int layer_height = size.height * 6 / 10;
      patchy::PixelBuffer noise(layer_width, layer_height, patchy::PixelFormat::rgba8());
      std::uint64_t seed = 0x9E3779B97F4A7C15ULL;
      for (int y = 0; y < layer_height; ++y) {
        auto row = noise.row(y);
        for (int x = 0; x < layer_width; ++x) {
          seed += 0x9E3779B97F4A7C15ULL;
          auto z = seed;
          z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
          z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
          z ^= z >> 31;
          auto* pixel = row.data() + static_cast<std::size_t>(x) * 4U;
          pixel[0] = static_cast<std::uint8_t>(z);
          pixel[1] = static_cast<std::uint8_t>(z >> 8);
          pixel[2] = static_cast<std::uint8_t>(z >> 16);
          pixel[3] = 255;
        }
      }
      patchy::Layer layer(document.allocate_layer_id(), "Rotated", std::move(noise));
      layer.set_bounds(patchy::Rect{(size.width - layer_width) / 2, (size.height - layer_height) / 2, layer_width,
                                    layer_height});
      const auto layer_id = layer.id();
      if (masked != 0) {
        patchy::PixelBuffer mask(size.width, size.height, patchy::PixelFormat::gray8());
        mask.clear(255);
        if (masked == 2) {
          for (int y = 0; y < size.height; ++y) {
            auto row = mask.row(y);
            std::fill(row.begin() + size.width * 6 / 10, row.end(), std::uint8_t{0});
          }
        }
        layer.set_mask(patchy::LayerMask{patchy::Rect{0, 0, size.width, size.height}, std::move(mask), 255, false});
      }
      document.add_layer(std::move(layer));

      patchy::ui::MainWindow window;
      window.resize(1600, 1000);
      if (qEnvironmentVariableIsSet("PATCHY_PERF_ONSCREEN")) {
        window.showMaximized();
      } else {
        window.show();
      }
      QApplication::processEvents();
      window.add_document_session(std::move(document), QStringLiteral("Mask Rotate Perf"));
      QApplication::processEvents();
      auto* canvas = active_canvas(window);
      CHECK(canvas != nullptr);
      const auto settle_started = Clock::now();
      while (!canvas->render_settled() &&
             std::chrono::duration<double>(Clock::now() - settle_started).count() < 60.0) {
        canvas->repaint();
        QApplication::processEvents();
      }
      auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
      CHECK(layer_list != nullptr);
      for (int row = 0; row < layer_list->count(); ++row) {
        auto* item = layer_list->item(row);
        if (static_cast<patchy::LayerId>(item->data(patchy::ui::kLayerIdRole).toULongLong()) == layer_id) {
          layer_list->clearSelection();
          layer_list->setCurrentItem(item);
          item->setSelected(true);
        }
      }
      QApplication::processEvents();

      auto* transform_action = window.findChild<QAction*>(QStringLiteral("editFreeTransformAction"));
      CHECK(transform_action != nullptr);
      transform_action->trigger();
      QApplication::processEvents();
      CHECK(canvas->free_transform_active());

      const auto bounds = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(layer_id)->bounds();
      const auto top_center =
          canvas->widget_position_for_document_point(QPoint(bounds.x + bounds.width / 2, bounds.y));
      const auto start = top_center + QPoint(0, -32);
      const auto before = canvas->render_cache_diagnostics();
      // PATCHY_PERF_SAMPLER=1 attributes the press + drag frames of each
      // configuration separately (one sampler per phase, Windows only).
#ifdef Q_OS_WIN
      std::unique_ptr<MainThreadSampler> sampler;
      if (qEnvironmentVariableIsSet("PATCHY_PERF_SAMPLER")) {
        sampler = std::make_unique<MainThreadSampler>();
      }
#endif
      const auto press_ms = elapsed_ms([&] {
        send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        canvas->repaint();
      });
      constexpr int kFrames = 40;
      std::vector<double> frames;
      frames.reserve(kFrames);
      for (int frame = 1; frame <= kFrames; ++frame) {
        const auto point = start + QPoint(frame * 6, frame * 2);
        frames.push_back(elapsed_ms([&] {
          send_mouse(*canvas, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
          canvas->repaint();
        }));
      }
      const auto release_ms = elapsed_ms([&] {
        send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(kFrames * 6, kFrames * 2), Qt::LeftButton,
                   Qt::NoButton);
        canvas->repaint();
      });
      const auto after = canvas->render_cache_diagnostics();
#ifdef Q_OS_WIN
      if (sampler) {
        std::cout << "[SAMPLER_PHASE] maskrotate " << size.width << 'x' << size.height << " masked=" << masked
                  << " press+drag+release\n";
        sampler.reset();
      }
#endif
      const auto commit_ms = elapsed_ms([&] {
        send_key(*canvas, Qt::Key_Return);
        const auto commit_started = Clock::now();
        canvas->repaint();
        while (!canvas->render_settled() &&
               std::chrono::duration<double>(Clock::now() - commit_started).count() < 60.0) {
          QApplication::processEvents();
        }
        CHECK(canvas->render_settled());
      });
      CHECK(!canvas->free_transform_active());

      auto sorted = frames;
      std::sort(sorted.begin(), sorted.end());
      double total = 0.0;
      for (const auto value : frames) {
        total += value;
      }
      std::cout << std::fixed << std::setprecision(1) << "[PERF] maskrotate " << size.width << 'x' << size.height
                << " masked=" << masked << " press_ms=" << press_ms << " first_frame_ms=" << frames.front()
                << " frame_avg_ms=" << total / kFrames << " frame_median_ms=" << sorted[sorted.size() / 2]
                << " frame_max_ms=" << sorted.back() << " release_ms=" << release_ms
                << " commit_ms=" << commit_ms
                << " proxy_latched=" << (after.transform_proxy_previews - before.transform_proxy_previews)
                << " threads=" << patchy::hardware_worker_threads() << '\n';
      const auto median_ms = sorted[sorted.size() / 2];
      const auto proxy_latched = after.transform_proxy_previews - before.transform_proxy_previews;
      CHECK(median_ms < kLiveFrameLatchMs);
      if (masked == 0) {
        unmasked_median_ms = median_ms;
      } else if (masked == 1) {
        CHECK(proxy_latched == 0);
        CHECK(median_ms <= unmasked_median_ms * 3.0 + 4.0);
      }
    }
  }
}

// Free Transform resize of a shape layer with a layer clipped to it (GitHub
// #398: "resizing a clipped shape is not smooth"). An ellipse shape layer is
// scaled up by dragging its top-right corner handle, alone (the cheap blit),
// with a canvas-sized noise layer clipped to it, and with that clipped layer
// plus a 0% fill on the shape (the clipped content shows only through the
// shape). Each mouse-move plus its repaint is timed. PATCHY_RENDER_THREADS=4
// prices the low-core case.
void clipped_shape_resize_perf() {
  struct Size {
    int width;
    int height;
  };
  for (const auto size : {Size{1920, 1080}, Size{4000, 3000}}) {
    double alone_median_ms = 0.0;
    // 0 = shape alone, 1 = a layer clipped to it, 2 = clipped with the
    // shape's fill at 0%.
    for (const int config : {0, 1, 2}) {
      patchy::Document document(size.width, size.height, patchy::PixelFormat::rgba8());
      patchy::PixelBuffer backdrop(size.width, size.height, patchy::PixelFormat::rgba8());
      backdrop.clear(255);
      document.add_layer(patchy::Layer(document.allocate_layer_id(), "Backdrop", std::move(backdrop)));

      patchy::LiveShapeParams params;
      params.kind = patchy::LiveShapeKind::Ellipse;
      params.left = size.width * 0.3;
      params.top = size.height * 0.3;
      params.right = size.width * 0.7;
      params.bottom = size.height * 0.6;
      patchy::VectorShapeContent shape;
      shape.path.subpaths = patchy::generate_live_shape_subpaths(params);
      shape.fill.color = {40, 140, 190};
      patchy::Layer shape_layer(document.allocate_layer_id(), "Ellipse", patchy::LayerKind::Pixel);
      shape_layer.set_vector_shape(std::move(shape));
      patchy::update_vector_shape_raster(shape_layer, patchy::Rect::from_size(size.width, size.height), nullptr);
      if (config == 2) {
        shape_layer.set_fill_opacity(0.0F);
      }
      const auto shape_id = shape_layer.id();
      document.add_layer(std::move(shape_layer));

      if (config != 0) {
        patchy::PixelBuffer noise(size.width, size.height, patchy::PixelFormat::rgba8());
        std::uint64_t seed = 0x9E3779B97F4A7C15ULL;
        for (int y = 0; y < size.height; ++y) {
          auto row = noise.row(y);
          for (int x = 0; x < size.width; ++x) {
            seed += 0x9E3779B97F4A7C15ULL;
            auto z = seed;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            z ^= z >> 31;
            auto* pixel = row.data() + static_cast<std::size_t>(x) * 4U;
            pixel[0] = static_cast<std::uint8_t>(z);
            pixel[1] = static_cast<std::uint8_t>(z >> 8);
            pixel[2] = static_cast<std::uint8_t>(z >> 16);
            pixel[3] = 255;
          }
        }
        patchy::Layer clipped(document.allocate_layer_id(), "Clipped", std::move(noise));
        clipped.set_clipped(true);
        document.add_layer(std::move(clipped));
      }

      patchy::ui::MainWindow window;
      window.resize(1600, 1000);
      if (qEnvironmentVariableIsSet("PATCHY_PERF_ONSCREEN")) {
        window.showMaximized();
      } else {
        window.show();
      }
      QApplication::processEvents();
      window.add_document_session(std::move(document), QStringLiteral("Clipped Shape Perf"));
      QApplication::processEvents();
      auto* canvas = active_canvas(window);
      CHECK(canvas != nullptr);
      const auto settle_started = Clock::now();
      while (!canvas->render_settled() &&
             std::chrono::duration<double>(Clock::now() - settle_started).count() < 60.0) {
        canvas->repaint();
        QApplication::processEvents();
      }
      auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
      CHECK(layer_list != nullptr);
      for (int row = 0; row < layer_list->count(); ++row) {
        auto* item = layer_list->item(row);
        if (static_cast<patchy::LayerId>(item->data(patchy::ui::kLayerIdRole).toULongLong()) == shape_id) {
          layer_list->clearSelection();
          layer_list->setCurrentItem(item);
          item->setSelected(true);
        }
      }
      QApplication::processEvents();

      auto* transform_action = window.findChild<QAction*>(QStringLiteral("editFreeTransformAction"));
      CHECK(transform_action != nullptr);
      transform_action->trigger();
      QApplication::processEvents();
      CHECK(canvas->free_transform_active());

      const auto bounds =
          std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(shape_id)->bounds();
      const auto start = canvas->widget_position_for_document_point(QPoint(bounds.x + bounds.width, bounds.y));
      const auto before = canvas->render_cache_diagnostics();
      const auto press_ms = elapsed_ms([&] {
        send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        canvas->repaint();
      });
      constexpr int kFrames = 40;
      std::vector<double> frames;
      frames.reserve(kFrames);
      for (int frame = 1; frame <= kFrames; ++frame) {
        const auto point = start + QPoint(frame * 5, -frame * 2);
        frames.push_back(elapsed_ms([&] {
          send_mouse(*canvas, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
          canvas->repaint();
        }));
      }
      const auto release_ms = elapsed_ms([&] {
        send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(kFrames * 5, -kFrames * 2), Qt::LeftButton,
                   Qt::NoButton);
        canvas->repaint();
      });
      const auto after = canvas->render_cache_diagnostics();
      const auto commit_ms = elapsed_ms([&] {
        send_key(*canvas, Qt::Key_Return);
        const auto commit_started = Clock::now();
        canvas->repaint();
        while (!canvas->render_settled() &&
               std::chrono::duration<double>(Clock::now() - commit_started).count() < 60.0) {
          QApplication::processEvents();
        }
        CHECK(canvas->render_settled());
      });
      CHECK(!canvas->free_transform_active());

      auto sorted = frames;
      std::sort(sorted.begin(), sorted.end());
      double total = 0.0;
      for (const auto value : frames) {
        total += value;
      }
      const auto median_ms = sorted[sorted.size() / 2];
      std::cout << std::fixed << std::setprecision(1) << "[PERF] clipshape " << size.width << 'x' << size.height
                << " config=" << (config == 0 ? "alone" : config == 1 ? "clipped" : "clipped_fill0")
                << " press_ms=" << press_ms << " first_frame_ms=" << frames.front()
                << " frame_avg_ms=" << total / kFrames << " frame_median_ms=" << median_ms
                << " frame_max_ms=" << sorted.back() << " release_ms=" << release_ms << " commit_ms=" << commit_ms
                << " proxy_latched=" << (after.transform_proxy_previews - before.transform_proxy_previews)
                << " threads=" << patchy::hardware_worker_threads() << '\n';
      if (config == 0) {
        alone_median_ms = median_ms;
      } else {
        // The clipped base keeps the unclipped drag's cost class.
        CHECK(median_ms <= alone_median_ms * 3.0 + 8.0);
      }
    }
  }
}

// PDF open and export timing (September 2026 report: an 86 MB, 85-page scanned
// PDF took minutes to open and saved as 700 MB). The file comes from argv[2],
// else PATCHY_PERF_PDF, else local-test-fixtures/pdf; PATCHY_PERF_PDF_PAGES
// caps the page count (default 8, "all" for every page). PATCHY_UI_PROFILE=1
// adds the per-stage lines from the import and export code itself.
std::filesystem::path perf_pdf_path(int argc, char* argv[]) {
  if (argc > 2) {
    return patchy::ui::to_filesystem_path(QString::fromLocal8Bit(argv[2]));
  }
  const auto env_path = qgetenv("PATCHY_PERF_PDF");
  if (!env_path.isEmpty()) {
    return patchy::ui::to_filesystem_path(env_path);
  }
  return patchy::test::local_format_fixture_path("pdf", "C2_Kyoto_House_Plans_Compressed.pdf");
}

int perf_pdf_page_cap() {
  const auto value = qgetenv("PATCHY_PERF_PDF_PAGES");
  if (value.isEmpty()) {
    return 8;
  }
  if (value == "all") {
    return 1 << 20;
  }
  return std::max(1, value.toInt());
}

// The import dialog and the open path persist settings and recent files; keep a perf
// run out of the real user store.
void isolate_perf_settings() {
  ensure_artifact_dir();
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("test-artifacts/perf-settings"));
}

void drive_perf_dialog(const std::shared_ptr<std::function<bool()>>& step, int attempts = 200000) {
  QTimer::singleShot(25, [step, attempts] {
    if ((*step)()) {
      return;
    }
    if (attempts > 0) {
      drive_perf_dialog(step, attempts - 1);
    }
  });
}

int pdf_page_count(const QString& path) {
#ifdef PATCHY_PERF_HAVE_QPDF
  QPdfDocument pdf;
  if (pdf.load(path) == QPdfDocument::Error::None) {
    return pdf.pageCount();
  }
#else
  (void)path;
#endif
  return 1;
}

void pdf_open_perf_if_available(int argc, char* argv[]) {
  const auto fs_path = perf_pdf_path(argc, argv);
  if (!std::filesystem::exists(fs_path)) {
    std::cout << "[SKIP] pdf fixture missing: " << fs_path.string() << '\n';
    return;
  }
  if (!patchy::ui::pdf_import_is_available()) {
    std::cout << "[SKIP] this build has no Qt PDF\n";
    return;
  }
  isolate_perf_settings();
  const QString path = patchy::ui::to_qstring(fs_path);
  const int total_pages = pdf_page_count(path);
  const int pages = std::min(total_pages, perf_pdf_page_cap());
  std::cout << "[PERF] pdfopen file_bytes=" << QFileInfo(path).size() << " total_pages=" << total_pages
            << " measured_pages=" << pages << '\n';

#ifdef PATCHY_PERF_HAVE_QPDF
  {
    QPdfDocument pdf;
    const auto load_ms = elapsed_ms([&] { (void)pdf.load(path); });
    std::cout << "[PERF] pdfopen qpdf_load ms=" << load_ms << '\n';
    double thumbs_ms = 0.0;
    for (int page = 0; page < pages; ++page) {
      const QSizeF points = pdf.pagePointSize(page);
      const int width = std::max(1, static_cast<int>(std::lround(96.0 * points.width() / points.height())));
      thumbs_ms += elapsed_ms([&] { (void)pdf.render(page, QSize(width, 96)); });
    }
    std::cout << "[PERF] pdfopen thumbnails pages=" << pages << " ms=" << thumbs_ms
              << " per_page_ms=" << thumbs_ms / pages << " projected_all_pages_ms=" << thumbs_ms / pages * total_pages
              << '\n';
    // Where a full-size render spends its time: one page at full, half, and quarter
    // size, with and without smoothing, run twice so the second pass is warm.
    const QSizeF points = pdf.pagePointSize(0);
    const QSize full(static_cast<int>(std::lround(points.width() / 72.0 * 300.0)),
                     static_cast<int>(std::lround(points.height() / 72.0 * 300.0)));
    QPdfDocumentRenderOptions aliased;
    aliased.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::ImageAliased |
                           QPdfDocumentRenderOptions::RenderFlag::TextAliased |
                           QPdfDocumentRenderOptions::RenderFlag::PathAliased);
    for (int pass = 0; pass < 2; ++pass) {
      const auto full_ms = elapsed_ms([&] { (void)pdf.render(0, full); });
      const auto aliased_ms = elapsed_ms([&] { (void)pdf.render(0, full, aliased); });
      const auto half_ms = elapsed_ms([&] { (void)pdf.render(0, full / 2); });
      const auto quarter_ms = elapsed_ms([&] { (void)pdf.render(0, full / 4); });
      const auto other_ms = elapsed_ms([&] { (void)pdf.render(std::min(1, total_pages - 1), full); });
      std::cout << "[PERF] pdfopen render_variants pass=" << pass << " full_ms=" << full_ms
                << " full_aliased_ms=" << aliased_ms << " half_ms=" << half_ms << " quarter_ms=" << quarter_ms
                << " other_page_full_ms=" << other_ms << '\n';
    }
    QImage scratch;
    const auto alloc_ms = elapsed_ms([&] {
      scratch = QImage(full, QImage::Format_ARGB32_Premultiplied);
      scratch.fill(Qt::transparent);
    });
    std::cout << "[PERF] pdfopen alloc_fill_full_argb ms=" << alloc_ms << '\n';
  }
#endif

  {
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    QByteArray bytes;
    const auto read_ms = elapsed_ms([&] { bytes = file.readAll(); });
    std::cout << "[PERF] pdfopen read_file ms=" << read_ms << '\n';
    const std::span<const std::uint8_t> span(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                             static_cast<std::size_t>(bytes.size()));
    double vector_ms = 0.0;
    int failed = 0;
    int layers = 0;
    for (int page = 0; page < pages; ++page) {
      patchy::pdf::VectorReadOptions options;
      options.page = page;
      options.pixels_per_point = 300.0 / 72.0;
      vector_ms += elapsed_ms([&] {
        try {
          const auto result = patchy::pdf::read_page_as_vectors(span, options);
          layers += static_cast<int>(result.document.layers().size());
        } catch (const std::exception&) {
          ++failed;
        }
      });
    }
    std::cout << "[PERF] pdfopen vector_reader pages=" << pages << " ms=" << vector_ms
              << " per_page_ms=" << vector_ms / pages << " failed_pages=" << failed << " layers=" << layers << '\n';
  }

  {
    patchy::ui::PdfImportOptions options;
    for (int page = 0; page < pages; ++page) {
      options.pages.push_back(page);
    }
    options.separate_documents = true;
    QString error;
    std::optional<patchy::ui::PdfImportResult> result;
    const auto raster_ms =
        elapsed_ms([&] { result = patchy::ui::load_pdf_document(path, options, QString(), &error); });
    CHECK(result.has_value());
    std::cout << "[PERF] pdfopen raster_import pages=" << pages << " ms=" << raster_ms
              << " per_page_ms=" << raster_ms / pages << '\n';
  }

  // The real thing: File > Open through the import dialog with its stored defaults
  // (editable, separate documents), the first `pages` pages selected.
  {
    patchy::ui::MainWindow window;
    window.resize(1600, 1000);
    window.show();
    QApplication::processEvents();
    const auto started = Clock::now();
    auto dialog_shown_ms = std::make_shared<double>(-1.0);
    auto step = std::make_shared<std::function<bool()>>();
    *step = [pages, started, dialog_shown_ms] {
      QDialog* dialog = nullptr;
      for (auto* widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("pdfImportDialog") && widget->isVisible()) {
          dialog = qobject_cast<QDialog*>(widget);
        }
      }
      if (dialog == nullptr) {
        return false;
      }
      *dialog_shown_ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
      auto* list = dialog->findChild<QListWidget*>(QStringLiteral("pdfImportPagesList"));
      auto* import_button = dialog->findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
      if (list == nullptr || import_button == nullptr) {
        return false;
      }
      list->clearSelection();
      for (int row = 0; row < std::min(pages, list->count()); ++row) {
        list->item(row)->setSelected(true);
      }
      import_button->click();
      return true;
    };
    drive_perf_dialog(step);
    const auto open_ms = elapsed_ms([&] {
      patchy::ui::MainWindowTestAccess::open_document_path(window, path);
      QApplication::processEvents();
    });
    std::cout << "[PERF] pdfopen gui_open pages=" << pages << " dialog_shown_ms=" << *dialog_shown_ms
              << " total_ms=" << open_ms << " after_dialog_ms=" << open_ms - *dialog_shown_ms
              << " after_dialog_per_page_ms=" << (open_ms - *dialog_shown_ms) / pages << '\n';
  }
}

void pdf_save_perf_if_available(int argc, char* argv[]) {
  const auto fs_path = perf_pdf_path(argc, argv);
  if (!std::filesystem::exists(fs_path)) {
    std::cout << "[SKIP] pdf fixture missing: " << fs_path.string() << '\n';
    return;
  }
  if (!patchy::ui::pdf_import_is_available()) {
    std::cout << "[SKIP] this build has no Qt PDF\n";
    return;
  }
  isolate_perf_settings();
  const QString path = patchy::ui::to_qstring(fs_path);
  const int total_pages = pdf_page_count(path);
  const int pages = std::min(total_pages, perf_pdf_page_cap());

  patchy::ui::PdfImportOptions import_options;
  for (int page = 0; page < pages; ++page) {
    import_options.pages.push_back(page);
  }
  import_options.separate_documents = true;
  QString error;
  auto imported = patchy::ui::load_pdf_document(path, import_options, QString(), &error);
  CHECK(imported.has_value());
  std::vector<const patchy::Document*> documents{&imported->document};
  for (const auto& extra : imported->extra_documents) {
    documents.push_back(&extra.document);
  }
  const double source_bytes_per_page = static_cast<double>(QFileInfo(path).size()) / total_pages;
  std::cout << "[PERF] pdfsave pages=" << documents.size() << " source_bytes_per_page=" << source_bytes_per_page
            << '\n';

  struct Mode {
    const char* name;
    patchy::ui::PdfExportOptions options;
  };
  std::vector<Mode> modes;
  // The dialog default (editable on, the "high" preset), then each preset flat.
  {
    // What an untouched import exports as: every page's original bytes.
    patchy::ui::PdfExportOptions pass_through;
    patchy::ui::apply_pdf_image_quality(QLatin1String(patchy::ui::kDefaultPdfImageQualityId), pass_through);
    pass_through.editable_layers = true;
    modes.push_back({"pass_through_default", pass_through});
  }
  for (const auto& preset : patchy::ui::pdf_image_quality_presets()) {
    patchy::ui::PdfExportOptions options;
    patchy::ui::apply_pdf_image_quality(QLatin1String(preset.id), options);
    options.keep_original_image_data = false;  // time the encoders, not the pass-through
    if (QLatin1String(preset.id) == QLatin1String(patchy::ui::kDefaultPdfImageQualityId)) {
      auto editable = options;
      editable.editable_layers = true;
      modes.push_back({"editable_default", editable});
    }
    modes.push_back({preset.id, options});
  }
  for (const auto& mode : modes) {
    const auto out = QStringLiteral("test-artifacts/perf_pdfsave_%1.pdf").arg(QLatin1String(mode.name));
    QFile::remove(out);
    std::vector<std::string> notices;
    const auto ms = elapsed_ms([&] {
      CHECK(patchy::ui::write_multipage_pdf_file(documents, out, mode.options, &notices));
    });
    const auto bytes = QFileInfo(out).size();
    std::cout << "[PERF] pdfsave " << mode.name << " pages=" << documents.size() << " ms=" << ms
              << " per_page_ms=" << ms / static_cast<double>(documents.size()) << " bytes=" << bytes
              << " bytes_per_page=" << bytes / static_cast<qint64>(documents.size())
              << " vs_source=" << (bytes / static_cast<double>(documents.size())) / source_bytes_per_page << "x\n";
    if (!qEnvironmentVariableIsSet("PATCHY_PERF_PDF_KEEP")) {
      QFile::remove(out);
    }
  }
}

}  // namespace

// GitHub issue 165: fast brush strokes on a 4872x6836 image came out as straight
// segments. Times what one mouse move and the following paint cost at fit zoom
// and at 100%, for a small and a large brush; no fixture needed.
// GitHub #200: a Warp Transform handle drag on a large image. A 6000x4000 noise
// layer over a white backdrop; one corner handle drags at fit zoom and one interior
// handle at 100%. Each frame is the mouse move plus its repaint. Asserts the frame
// ceiling, prints the numbers, and saves a mid-drag and a resting capture per zoom.
void warp_drag_perf() {
  ensure_artifact_dir();
  constexpr int kWidth = 6000;
  constexpr int kHeight = 4000;
  patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer backdrop(kWidth, kHeight, patchy::PixelFormat::rgba8());
  backdrop.clear(255);
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Backdrop", std::move(backdrop)));
  // Smooth bands plus noise: the captures show the bend, the noise keeps every
  // sample distinct so no path can shortcut on flat color.
  patchy::PixelBuffer art(kWidth, kHeight, patchy::PixelFormat::rgba8());
  std::uint64_t seed = 0x9E3779B97F4A7C15ULL;
  for (int y = 0; y < kHeight; ++y) {
    auto row = art.row(y);
    for (int x = 0; x < kWidth; ++x) {
      seed += 0x9E3779B97F4A7C15ULL;
      auto z = seed;
      z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
      z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
      z ^= z >> 31;
      const bool band = ((x / 400) + (y / 400)) % 2 == 0;
      auto* pixel = row.data() + static_cast<std::size_t>(x) * 4U;
      pixel[0] = static_cast<std::uint8_t>((band ? 200 : 40) + (z & 31U));
      pixel[1] = static_cast<std::uint8_t>((band ? 90 : 150) + ((z >> 8) & 31U));
      pixel[2] = static_cast<std::uint8_t>((band ? 40 : 200) + ((z >> 16) & 31U));
      pixel[3] = 255;
    }
  }
  patchy::Layer layer(document.allocate_layer_id(), "Warped", std::move(art));
  const auto layer_id = layer.id();
  document.add_layer(std::move(layer));

  patchy::ui::MainWindow window;
  window.resize(1600, 1000);
  if (qEnvironmentVariableIsSet("PATCHY_PERF_ONSCREEN")) {
    window.showMaximized();
  } else {
    window.show();
  }
  QApplication::processEvents();
  window.add_document_session(std::move(document), QStringLiteral("Warp Drag Perf"));
  QApplication::processEvents();
  auto* canvas = active_canvas(window);
  CHECK(canvas != nullptr);
  const auto settle = [canvas] {
    const auto started = Clock::now();
    while (!canvas->render_settled() && std::chrono::duration<double>(Clock::now() - started).count() < 60.0) {
      canvas->repaint();
      QApplication::processEvents();
    }
  };
  settle();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  for (int row = 0; row < layer_list->count(); ++row) {
    auto* item = layer_list->item(row);
    if (static_cast<patchy::LayerId>(item->data(patchy::ui::kLayerIdRole).toULongLong()) == layer_id) {
      layer_list->clearSelection();
      layer_list->setCurrentItem(item);
      item->setSelected(true);
    }
  }
  QApplication::processEvents();
  auto* warp_action = window.findChild<QAction*>(QStringLiteral("editWarpTransformAction"));
  CHECK(warp_action != nullptr);

  struct Result {
    std::string zoom;
    double median_ms{0.0};
    double release_ms{0.0};
  };
  std::vector<Result> results;
  for (const bool fit : {true, false}) {
    // Corner handle (index 3, top right) at fit; an interior handle (index 5) at
    // 100%, centered in the view so the whole drag stays on screen.
    const int handle = fit ? 3 : 5;
    canvas->fit_to_view();
    settle();
    const auto setup_started = Clock::now();
    warp_action->trigger();
    QApplication::processEvents();
    const auto begin_ms = std::chrono::duration<double, std::milli>(Clock::now() - setup_started).count();
    CHECK(canvas->warp_transform_active());
    if (!fit) {
      const auto handle_point = canvas->warp_handle_document_position(handle).toPoint();
      canvas->zoom_to_document_rect(QRect(handle_point - QPoint(400, 250), QSize(800, 500)));
      canvas->set_zoom_centered(1.0);
      settle();
    }
    const auto handle_widget = canvas->widget_position_f(canvas->warp_handle_document_position(handle)).toPoint();
    const QPoint step = fit ? QPoint(5, 4) : QPoint(-6, 5);
    send_mouse(*canvas, QEvent::MouseButtonPress, handle_widget, Qt::LeftButton, Qt::LeftButton);
    canvas->repaint();
    constexpr int kFrames = 30;
    std::vector<double> frames;
    frames.reserve(kFrames);
    for (int frame = 1; frame <= kFrames; ++frame) {
      frames.push_back(elapsed_ms([&] {
        send_mouse(*canvas, QEvent::MouseMove, handle_widget + step * frame, Qt::NoButton, Qt::LeftButton);
        canvas->repaint();
      }));
    }
    const auto zoom_name = std::string(fit ? "fit" : "100");
    CHECK(canvas->grab().save(QString::fromStdString("test-artifacts/warp_drag_" + zoom_name + "_drag.png")));
    const auto release_ms = elapsed_ms([&] {
      send_mouse(*canvas, QEvent::MouseButtonRelease, handle_widget + step * kFrames, Qt::LeftButton,
                 Qt::NoButton);
      canvas->repaint();
    });
    CHECK(canvas->grab().save(QString::fromStdString("test-artifacts/warp_drag_" + zoom_name + "_rest.png")));
    const auto commit_ms = elapsed_ms([&] {
      send_key(*canvas, Qt::Key_Return);
      settle();
    });
    CHECK(!canvas->warp_transform_active());
    CHECK(canvas->grab().save(QString::fromStdString("test-artifacts/warp_drag_" + zoom_name + "_commit.png")));

    auto sorted = frames;
    std::sort(sorted.begin(), sorted.end());
    double total = 0.0;
    for (const auto value : frames) {
      total += value;
    }
    std::cout << std::fixed << std::setprecision(1) << "[PERF] warpdrag " << kWidth << 'x' << kHeight
              << " zoom=" << zoom_name << " (" << canvas->zoom() << ") begin_ms=" << begin_ms
              << " first_frame_ms=" << frames.front() << " frame_avg_ms=" << total / kFrames
              << " frame_median_ms=" << sorted[sorted.size() / 2] << " frame_max_ms=" << sorted.back()
              << " release_ms=" << release_ms << " commit_ms=" << commit_ms
              << " threads=" << patchy::hardware_worker_threads() << '\n';
    results.push_back(Result{zoom_name, sorted[sorted.size() / 2], release_ms});
    // Undo the bake so the next zoom drags the same unwarped layer.
    patchy::ui::MainWindowTestAccess::undo(window);
    settle();
  }
  // Ceilings: a drag frame must stay interactive, and the resting render on release
  // is one bounded pass.
  constexpr double kFrameCeilingMs = 100.0;
  constexpr double kReleaseCeilingMs = 1000.0;
  for (const auto& result : results) {
    if (result.median_ms >= kFrameCeilingMs || result.release_ms >= kReleaseCeilingMs) {
      std::cout << "[FAIL] warpdrag zoom=" << result.zoom << " over its ceiling\n";
    }
    CHECK(result.median_ms < kFrameCeilingMs);
    CHECK(result.release_ms < kReleaseCeilingMs);
  }
}

// GitHub #298: Warp Transform on the reporter's 2560x1440 JPEG (a single opened flat
// layer, local-test-fixtures/issue-298/sample-1mb.jpg from samplelib.com). Times the
// whole flow per case: entering Warp, a corner drag pulled deep into the image (one
// move plus repaint per frame), a burst of 30 queued moves, the release, and the
// commit, and prints a digest of the committed layer. PATCHY_PERF_WARPPHOTO_CASE
// runs one case.
void warp_photo_perf_if_available() {
  const auto path = patchy::test::local_format_fixture_path("issue-298", "sample-1mb.jpg");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local-test-fixtures/issue-298/sample-1mb.jpg missing\n";
    return;
  }
  ensure_artifact_dir();
  struct Result {
    std::string name;
    double median_ms{0.0};
    double burst_ms{0.0};
    double release_ms{0.0};
  };
  std::vector<Result> results;
  // view_zoom 0 = fit. 66% is a fit view on a 2x HiDPI screen: the whole layer on
  // screen at a device zoom above 50%, so the preview composites at full resolution.
  // The recording enters Warp from Free Transform's options-bar toggle.
  struct Case {
    std::string name;
    QSize window_size;
    double view_zoom{0.0};
    bool from_free_transform{false};
  };
  for (const auto& [name, window_size, view_zoom, from_free_transform] :
       {Case{"fit_large", QSize(1600, 1000), 0.0, false}, Case{"fit_small", QSize(1000, 640), 0.0, false},
        Case{"zoom66", QSize(1900, 1150), 0.66, false}, Case{"fit_small_from_ft", QSize(1000, 640), 0.0, true}}) {
    if (const auto only = qEnvironmentVariable("PATCHY_PERF_WARPPHOTO_CASE");
        !only.isEmpty() && only != QString::fromStdString(name)) {
      continue;
    }
    patchy::ui::MainWindow window;
    window.resize(window_size);
    window.show();
    QApplication::processEvents();
    patchy::ui::MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(path));
    QApplication::processEvents();
    auto* canvas = active_canvas(window);
    CHECK(canvas != nullptr);
    const auto settle = [canvas] {
      const auto started = Clock::now();
      while (!canvas->render_settled() && std::chrono::duration<double>(Clock::now() - started).count() < 60.0) {
        canvas->repaint();
        QApplication::processEvents();
      }
    };
    canvas->fit_to_view();
    if (view_zoom > 0.0) {
      canvas->set_view_zoom_centered(view_zoom);
    }
    settle();
    auto* warp_action = window.findChild<QAction*>(QStringLiteral("editWarpTransformAction"));
    CHECK(warp_action != nullptr);
    if (from_free_transform) {
      auto* free_transform_action = window.findChild<QAction*>(QStringLiteral("editFreeTransformAction"));
      CHECK(free_transform_action != nullptr);
      free_transform_action->trigger();
      QApplication::processEvents();
      settle();
      CHECK(canvas->free_transform_active());
    }
    auto* warp_toggle = window.findChild<QPushButton*>(QStringLiteral("transformWarpModeButton"));
    CHECK(warp_toggle != nullptr);
    const auto begin_ms = elapsed_ms([&] {
      if (from_free_transform) {
        warp_toggle->click();
      } else {
        warp_action->trigger();
      }
      QApplication::processEvents();
      canvas->repaint();
    });
    CHECK(canvas->warp_transform_active());
    // The top-left corner pulled toward the middle, as in the issue's recording.
    const auto handle_widget = canvas->widget_position_f(canvas->warp_handle_document_position(0)).toPoint();
    const QPoint step(12, 7);
    send_mouse(*canvas, QEvent::MouseButtonPress, handle_widget, Qt::LeftButton, Qt::LeftButton);
    canvas->repaint();
    constexpr int kFrames = 30;
    std::vector<double> frames;
    frames.reserve(kFrames);
    for (int frame = 1; frame <= kFrames; ++frame) {
      frames.push_back(elapsed_ms([&] {
        send_mouse(*canvas, QEvent::MouseMove, handle_widget + step * frame, Qt::NoButton, Qt::LeftButton);
        canvas->repaint();
      }));
    }
    // A burst: 30 moves queued at once, the way a fast pointer delivers them while a
    // frame renders. It ends where the steady drag did (30 % 5 == 30 % 3 == 0), so
    // the release and commit match the drag above.
    const auto drag_end = handle_widget + step * kFrames;
    const auto burst_ms = elapsed_ms([&] {
      for (int move = 1; move <= 30; ++move) {
        const auto position = drag_end + QPoint(move % 5, move % 3);
        QApplication::postEvent(canvas, new QMouseEvent(QEvent::MouseMove, QPointF(position),
                                                        QPointF(canvas->mapToGlobal(position)), Qt::NoButton,
                                                        Qt::LeftButton, Qt::NoModifier));
      }
      QApplication::processEvents();
      canvas->repaint();
    });
    CHECK(canvas->grab().save(QString::fromStdString("test-artifacts/warp_photo_" + name + "_drag.png")));
    const auto release_ms = elapsed_ms([&] {
      send_mouse(*canvas, QEvent::MouseButtonRelease, handle_widget + step * kFrames, Qt::LeftButton, Qt::NoButton);
      canvas->repaint();
    });
    CHECK(canvas->grab().save(QString::fromStdString("test-artifacts/warp_photo_" + name + "_rest.png")));
    const auto commit_ms = elapsed_ms([&] {
      send_key(*canvas, Qt::Key_Return);
      settle();
    });
    CHECK(!canvas->warp_transform_active());
    CHECK(canvas->grab().save(QString::fromStdString("test-artifacts/warp_photo_" + name + "_commit.png")));
    // FNV-1a over the committed layer: the commit must stay byte-identical across
    // preview optimizations.
    std::uint64_t digest = 0xCBF29CE484222325ULL;
    {
      const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
      const auto active = document.active_layer_id();
      CHECK(active.has_value());
      const auto& pixels = document.find_layer(*active)->pixels();
      for (std::int32_t y = 0; y < pixels.height(); ++y) {
        for (const auto byte : pixels.row(y)) {
          digest = (digest ^ byte) * 0x100000001B3ULL;
        }
      }
    }
    auto sorted = frames;
    std::sort(sorted.begin(), sorted.end());
    double total = 0.0;
    for (const auto value : frames) {
      total += value;
    }
    std::cout << std::fixed << std::setprecision(1) << "[PERF] warpphoto 2560x1440 case=" << name << " view_zoom="
              << canvas->view_zoom() << " begin_ms=" << begin_ms << " first_frame_ms=" << frames.front()
              << " frame_avg_ms=" << total / kFrames << " frame_median_ms=" << sorted[sorted.size() / 2]
              << " frame_max_ms=" << sorted.back() << " burst30_ms=" << burst_ms << " release_ms=" << release_ms
              << " commit_ms=" << commit_ms << " threads=" << patchy::hardware_worker_threads()
              << " commit_digest=" << std::hex << digest
              << std::dec << '\n';
    results.push_back(Result{name, sorted[sorted.size() / 2], burst_ms, release_ms});
  }
  // Ceilings (16-thread laptop, October 2026: before the fix the 70% case took 75 ms
  // a frame and 5.5 s to drain the burst, the fit cases 0.2 to 0.6 s; after, every
  // frame is under 30 ms and every burst under 50 ms). A burst over its ceiling
  // means each queued move renders again.
  constexpr double kFrameCeilingMs = 100.0;
  constexpr double kBurstCeilingMs = 250.0;
  constexpr double kReleaseCeilingMs = 1000.0;
  for (const auto& result : results) {
    if (result.median_ms >= kFrameCeilingMs || result.burst_ms >= kBurstCeilingMs ||
        result.release_ms >= kReleaseCeilingMs) {
      std::cout << "[FAIL] warpphoto case=" << result.name << " over its ceiling\n";
    }
    CHECK(result.median_ms < kFrameCeilingMs);
    CHECK(result.burst_ms < kBurstCeilingMs);
    CHECK(result.release_ms < kReleaseCeilingMs);
  }
}

// GitHub #314: dragging a gradient-filled shape lagged, and on Wayland the app stopped
// responding for half a minute after the second drag (1280x720 document at 67.64%).
// The recording drags with Path Select: each mouse move re-baked the shape's gradient
// and recomposited its region before the next move was read. Synthetic, so it runs
// anywhere: two rounded-rectangle shapes with a five-stop gradient on a white layer,
// dragged twice with Path Select and with the Move tool. Each drag times 20 moves with a
// repaint after each, then a burst of 30 queued moves (a fast pointer while a frame
// renders), then the release. PATCHY_PERF_GRADIENTDRAG_CASE runs one case.
void gradient_shape_drag_perf() {
  ensure_artifact_dir();
  const auto rounded_gradient_shape = [](patchy::Document& document, const std::string& name, double left,
                                         double top, double right, double bottom) {
    patchy::LiveShapeParams params;
    params.kind = patchy::LiveShapeKind::RoundedRectangle;
    params.left = left;
    params.top = top;
    params.right = right;
    params.bottom = bottom;
    params.corner_radii = {40.0, 40.0, 40.0, 40.0};
    patchy::VectorShapeContent shape;
    shape.path.subpaths = patchy::generate_live_shape_subpaths(params);
    shape.fill.kind = patchy::VectorFillKind::Gradient;
    shape.fill.gradient.color_stops = {{0.0F, {22, 24, 28}},
                                       {0.3F, {90, 96, 112}},
                                       {0.5F, {240, 242, 250}},
                                       {0.75F, {150, 152, 160}},
                                       {1.0F, {30, 32, 38}}};
    shape.fill.gradient.alpha_stops = {{0.0F, 1.0F}, {1.0F, 1.0F}};
    patchy::Layer layer(document.allocate_layer_id(), name, patchy::LayerKind::Pixel);
    layer.metadata()[patchy::kLayerMetadataVectorShape] = "1";
    layer.set_vector_shape(std::move(shape));
    patchy::update_vector_shape_raster(layer, patchy::Rect::from_size(document.width(), document.height()),
                                       &std::as_const(document).metadata().patterns);
    return layer;
  };
  struct Result {
    std::string name;
    double median_ms{0.0};
    double burst_ms{0.0};
    double release_ms{0.0};
  };
  std::vector<Result> results;
  for (const auto tool : {patchy::ui::CanvasTool::PathSelect, patchy::ui::CanvasTool::Move}) {
    const std::string name = tool == patchy::ui::CanvasTool::PathSelect ? "path_select" : "move";
    if (const auto only = qEnvironmentVariable("PATCHY_PERF_GRADIENTDRAG_CASE");
        !only.isEmpty() && only != QString::fromStdString(name)) {
      continue;
    }
    patchy::Document document(1280, 720, patchy::PixelFormat::rgba8());
    patchy::PixelBuffer backdrop(1280, 720, patchy::PixelFormat::rgba8());
    backdrop.clear(255);
    document.add_layer(patchy::Layer(document.allocate_layer_id(), "Background", std::move(backdrop)));
    document.add_layer(rounded_gradient_shape(document, "Rounded Rectangle 1", 120.0, 100.0, 560.0, 500.0));
    auto dragged = rounded_gradient_shape(document, "Rounded Rectangle 2", 600.0, 340.0, 880.0, 620.0);
    const auto dragged_id = dragged.id();
    const auto bounds_before = dragged.bounds();
    document.add_layer(std::move(dragged));

    patchy::ui::MainWindow window;
    window.resize(1600, 1000);
    window.show();
    QApplication::processEvents();
    window.add_document_session(std::move(document), QStringLiteral("Gradient Drag Perf"));
    QApplication::processEvents();
    auto* canvas = active_canvas(window);
    CHECK(canvas != nullptr);
    const auto settle = [canvas] {
      const auto started = Clock::now();
      while (!canvas->render_settled() && std::chrono::duration<double>(Clock::now() - started).count() < 60.0) {
        canvas->repaint();
        QApplication::processEvents();
      }
    };
    settle();
    auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(layer_list != nullptr);
    for (int row = 0; row < layer_list->count(); ++row) {
      auto* item = layer_list->item(row);
      if (static_cast<patchy::LayerId>(item->data(patchy::ui::kLayerIdRole).toULongLong()) == dragged_id) {
        layer_list->clearSelection();
        layer_list->setCurrentItem(item);
        item->setSelected(true);
      }
    }
    QApplication::processEvents();
    canvas->set_view_zoom_centered(0.6764);
    canvas->set_tool(tool);
    QApplication::processEvents();
    settle();

    const auto before = canvas->render_cache_diagnostics();
    std::vector<double> frames;
    double burst_ms = 0.0;
    double release_ms = 0.0;
    // Two drags of the same shape, as in the recording: up and left, then back (about
    // 440 x 220 document pixels each way at this zoom, staying on the canvas).
    QPoint position = canvas->widget_position_for_document_point(QPoint(740, 480));
    for (int drag = 0; drag < 2; ++drag) {
      const QPoint step = drag == 0 ? QPoint(-6, -3) : QPoint(6, 3);
      send_mouse(*canvas, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
      canvas->repaint();
      for (int frame = 1; frame <= 20; ++frame) {
        position += step;
        frames.push_back(elapsed_ms([&] {
          send_mouse(*canvas, QEvent::MouseMove, position, Qt::NoButton, Qt::LeftButton);
          QApplication::processEvents();
          canvas->repaint();
        }));
      }
      burst_ms = std::max(burst_ms, elapsed_ms([&] {
        for (int move = 1; move <= 30; ++move) {
          position += step;
          QApplication::postEvent(canvas, new QMouseEvent(QEvent::MouseMove, QPointF(position),
                                                          QPointF(canvas->mapToGlobal(position)), Qt::NoButton,
                                                          Qt::LeftButton, Qt::NoModifier));
        }
        QApplication::processEvents();
        QApplication::processEvents();  // the queued drag apply runs on the next pass
        canvas->repaint();
      }));
      release_ms = std::max(release_ms, elapsed_ms([&] {
        send_mouse(*canvas, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
        QApplication::processEvents();
        canvas->repaint();
      }));
      CHECK(canvas->grab().save(
          QString::fromStdString("test-artifacts/gradient_drag_" + name + "_" + std::to_string(drag + 1) + ".png")));
    }
    settle();
    const auto after = canvas->render_cache_diagnostics();
    const auto& final_document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    const auto* moved = final_document.find_layer(dragged_id);
    CHECK(moved != nullptr);
    // Both drags travel the same distance, so the shape ends where it started.
    if (moved != nullptr) {
      CHECK(std::abs(moved->bounds().x - bounds_before.x) <= 2);
      CHECK(std::abs(moved->bounds().y - bounds_before.y) <= 2);
    }
    auto sorted = frames;
    std::sort(sorted.begin(), sorted.end());
    double total = 0.0;
    for (const auto value : frames) {
      total += value;
    }
    std::cout << std::fixed << std::setprecision(1) << "[PERF] gradientdrag 1280x720 case=" << name
              << " view_zoom=" << canvas->view_zoom() << " frame_avg_ms=" << total / static_cast<double>(frames.size())
              << " frame_median_ms=" << sorted[sorted.size() / 2] << " frame_max_ms=" << sorted.back()
              << " burst30_ms=" << burst_ms << " release_ms=" << release_ms
              << " path_drag_applies=" << (after.path_drag_applies - before.path_drag_applies)
              << " proxy_previews=" << (after.move_proxy_previews - before.move_proxy_previews)
              << " threads=" << patchy::hardware_worker_threads() << '\n';
    results.push_back(Result{name, sorted[sorted.size() / 2], burst_ms, release_ms});
  }
  // Ceilings: a burst over its ceiling means each queued move applies again.
  constexpr double kFrameCeilingMs = 100.0;
  constexpr double kBurstCeilingMs = 250.0;
  constexpr double kReleaseCeilingMs = 1000.0;
  for (const auto& result : results) {
    if (result.median_ms >= kFrameCeilingMs || result.burst_ms >= kBurstCeilingMs ||
        result.release_ms >= kReleaseCeilingMs) {
      std::cout << "[FAIL] gradientdrag case=" << result.name << " over its ceiling\n";
    }
    CHECK(result.median_ms < kFrameCeilingMs);
    CHECK(result.burst_ms < kBurstCeilingMs);
    CHECK(result.release_ms < kReleaseCeilingMs);
  }
}

// GitHub issue 296: Liquify drag latency on large layers. The workspace edits a proxy
// whose longest edge is 720 px, so a drag frame should not grow with the layer; Apply
// renders the full layer once. Drives the real dialog: the open time, each Warp brush
// move plus its preview repaint at the default and the largest brush, and the Apply
// render. Then a core breakdown on the proxy: one stroke segment and one proxy render.
void liquify_drag_perf() {
  struct Result {
    std::string name;
    double median_ms{0.0};
    double apply_ms{0.0};
  };
  std::vector<Result> results;
  for (const auto& [width, height] : {std::pair{4000, 3000}, std::pair{6000, 4000}}) {
    patchy::PixelBuffer source(width, height, patchy::PixelFormat::rgba8());
    std::uint64_t seed = 0x9E3779B97F4A7C15ULL;
    for (int y = 0; y < height; ++y) {
      auto row = source.row(y);
      for (int x = 0; x < width; ++x) {
        seed += 0x9E3779B97F4A7C15ULL;
        auto z = seed;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z ^= z >> 31;
        const bool band = ((x / 400) + (y / 400)) % 2 == 0;
        auto* pixel = row.data() + static_cast<std::size_t>(x) * 4U;
        pixel[0] = static_cast<std::uint8_t>((band ? 200 : 40) + (z & 31U));
        pixel[1] = static_cast<std::uint8_t>((band ? 90 : 150) + ((z >> 8) & 31U));
        pixel[2] = static_cast<std::uint8_t>((band ? 40 : 200) + ((z >> 16) & 31U));
        pixel[3] = 255;
      }
    }
    const auto size_name = std::to_string(width) + "x" + std::to_string(height);
    double open_ms = 0.0;
    std::vector<std::pair<int, std::vector<double>>> brushes;
    const auto opened = Clock::now();
    QTimer::singleShot(0, [&] {
      open_ms = std::chrono::duration<double, std::milli>(Clock::now() - opened).count();
      QDialog* dialog = nullptr;
      for (auto* widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("liquifyDialog") && widget->isVisible()) {
          dialog = qobject_cast<QDialog*>(widget);
        }
      }
      auto* preview = dialog != nullptr ? dialog->findChild<QWidget*>(QStringLiteral("liquifyPreview")) : nullptr;
      auto* size_spin = dialog != nullptr ? dialog->findChild<QSpinBox*>(QStringLiteral("liquifySizeSpin")) : nullptr;
      if (preview == nullptr || size_spin == nullptr) {
        std::cout << "[FAIL] liquifydrag: no Liquify dialog\n";
        if (dialog != nullptr) {
          dialog->reject();
        }
        return;
      }
      QApplication::processEvents();
      for (const int brush : {size_spin->value(), 2000}) {
        size_spin->setValue(brush);
        const auto start = preview->rect().center() - QPoint(120, 60);
        send_mouse(*preview, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        preview->repaint();
        std::vector<double> frames;
        for (int frame = 1; frame <= 40; ++frame) {
          frames.push_back(elapsed_ms([&] {
            send_mouse(*preview, QEvent::MouseMove, start + QPoint(6, 3) * frame, Qt::NoButton, Qt::LeftButton);
            preview->repaint();
          }));
        }
        send_mouse(*preview, QEvent::MouseButtonRelease, start + QPoint(6, 3) * 40, Qt::LeftButton, Qt::NoButton);
        brushes.emplace_back(brush, std::move(frames));
      }
      dialog->accept();
    });
    const auto mesh = patchy::ui::request_liquify(nullptr, source, patchy::Rect{0, 0, width, height}, QRegion{});
    CHECK(mesh.has_value() && !mesh->is_identity());
    if (!mesh.has_value()) {
      continue;
    }
    std::optional<patchy::PixelBuffer> rendered;
    const auto apply_ms = elapsed_ms([&] { rendered = mesh->render(source); });
    CHECK(rendered.has_value());
    for (const auto& [brush, frames] : brushes) {
      auto sorted = frames;
      std::sort(sorted.begin(), sorted.end());
      double total = 0.0;
      for (const auto value : frames) {
        total += value;
      }
      std::cout << std::fixed << std::setprecision(1) << "[PERF] liquifydrag " << size_name << " brush=" << brush
                << " open_ms=" << open_ms << " frame_avg_ms=" << total / static_cast<double>(frames.size())
                << " frame_median_ms=" << sorted[sorted.size() / 2] << " frame_max_ms=" << sorted.back()
                << " apply_ms=" << apply_ms << '\n';
      results.push_back(Result{size_name + " brush=" + std::to_string(brush), sorted[sorted.size() / 2], apply_ms});
    }
  }

  // Core breakdown on a 720x540 proxy: one Warp stroke segment (the mesh update for a
  // 6 px move) and one proxy render, each the median of 40.
  patchy::PixelBuffer proxy(720, 540, patchy::PixelFormat::rgba8());
  proxy.clear(200);
  for (const double size : {54.0, 360.0}) {
    patchy::LiquifyMesh mesh(proxy.width(), proxy.height());
    std::vector<double> strokes;
    std::vector<double> renders;
    for (int step = 0; step < 40; ++step) {
      const double x = 200.0 + step * 6.0;
      strokes.push_back(elapsed_ms(
          [&] { mesh.apply_stroke(patchy::LiquifyTool::ForwardWarp, x, 200.0, x + 6.0, 203.0, size, 50, 50); }));
      renders.push_back(elapsed_ms([&] { (void)mesh.render(proxy); }));
    }
    std::sort(strokes.begin(), strokes.end());
    std::sort(renders.begin(), renders.end());
    std::cout << std::fixed << std::setprecision(2) << "[PERF] liquifydrag core proxy=720x540 brush=" << size
              << " stroke_median_ms=" << strokes[strokes.size() / 2]
              << " render_median_ms=" << renders[renders.size() / 2] << '\n';
  }

  // Ceilings (M2 Ultra, October 2026: frames were 15 to 18 ms and Apply 0.5 to 1 s
  // before #296, 2 to 4 ms and 14 to 20 ms after). A frame must leave room for a 60 Hz
  // display on slower machines; Apply is one bounded pass.
  constexpr double kFrameCeilingMs = 10.0;
  constexpr double kApplyCeilingMs = 400.0;
  for (const auto& result : results) {
    if (result.median_ms >= kFrameCeilingMs || result.apply_ms >= kApplyCeilingMs) {
      std::cout << "[FAIL] liquifydrag " << result.name << " over its ceiling\n";
    }
    CHECK(result.median_ms < kFrameCeilingMs);
    CHECK(result.apply_ms < kApplyCeilingMs);
  }
}

void big_brush_stroke_perf() {
  ensure_artifact_dir();
  const auto image_path = std::filesystem::absolute("test-artifacts/perf_brush_stroke_4872x6836.png");
  if (!std::filesystem::exists(image_path)) {
    QImage image(4872, 6836, QImage::Format_RGB32);
    image.fill(Qt::white);
    CHECK(image.save(patchy::ui::to_qstring(image_path)));
  }
  patchy::ui::MainWindow window;
  window.resize(1600, 1000);
  if (qEnvironmentVariableIsSet("PATCHY_PERF_ONSCREEN")) {
    window.showMaximized();
  } else {
    window.show();
  }
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(image_path));
  QApplication::processEvents();
  auto* canvas = active_canvas(window);
  CHECK(canvas != nullptr);
  const auto settle = [canvas] {
    const auto started = Clock::now();
    while (!canvas->render_settled() && std::chrono::duration<double>(Clock::now() - started).count() < 60.0) {
      canvas->repaint();
      QApplication::processEvents();
    }
  };
  settle();
  canvas->set_tool(patchy::ui::CanvasTool::Brush);
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  // 0 is fit; 150% interpolates and 100% / 200% draw exact pixels (GitHub issue 210);
  // 800% is the deep-zoom raster with the pixel grid (GitHub issue 239).
  for (const double zoom : {0.0, 1.0, 1.5, 2.0, 8.0}) {
    const auto label = zoom == 0.0 ? std::string("fit") : std::to_string(static_cast<int>(zoom * 100.0)) + "%";
    for (const int brush_size : {40, 300}) {
      if (zoom == 0.0) {
        canvas->fit_to_view();
      } else {
        canvas->set_zoom_centered(zoom);
      }
      canvas->set_brush_size(brush_size);
      settle();
      if (brush_size == 40) {
        std::vector<double> full_ms;
        for (int index = 0; index < 20; ++index) {
          full_ms.push_back(elapsed_ms([&] { canvas->repaint(); }));
        }
        std::sort(full_ms.begin(), full_ms.end());
        std::cout << "[perf] full_view_paint " << label << " zoom=" << canvas->zoom() << " paint_ms median="
                  << std::fixed << std::setprecision(2) << full_ms[full_ms.size() / 2] << " max=" << full_ms.back()
                  << '\n';
      }
      const auto center = QPointF(canvas->width() / 2.0, canvas->height() / 2.0);
      const auto radius = std::min(canvas->width(), canvas->height()) * 0.35;
      const auto at = [&](int index) {
        const auto angle = index * 0.25;
        return QPointF(center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle));
      };
      const auto send = [canvas](QEvent::Type type, QPointF position, Qt::MouseButton button,
                                 Qt::MouseButtons buttons) {
        QMouseEvent event(type, position, canvas->mapToGlobal(position), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(canvas, &event);
      };
      send(QEvent::MouseButtonPress, at(0), Qt::LeftButton, Qt::LeftButton);
      canvas->repaint();
      std::vector<double> move_ms;
      std::vector<double> paint_ms;
#ifdef Q_OS_WIN
      // PATCHY_PERF_SAMPLER=1 attributes the moves and paints of each case.
      std::unique_ptr<MainThreadSampler> sampler;
      if (qEnvironmentVariableIsSet("PATCHY_PERF_SAMPLER")) {
        sampler = std::make_unique<MainThreadSampler>();
      }
#endif
      for (int index = 1; index <= 60; ++index) {
        move_ms.push_back(elapsed_ms([&] { send(QEvent::MouseMove, at(index), Qt::NoButton, Qt::LeftButton); }));
        paint_ms.push_back(elapsed_ms([&] { canvas->repaint(); }));
      }
#ifdef Q_OS_WIN
      if (sampler) {
        std::cout << "[SAMPLER_PHASE] brush_stroke " << label << " size=" << brush_size << '\n';
        sampler.reset();
      }
#endif
      send(QEvent::MouseButtonRelease, at(60), Qt::LeftButton, Qt::NoButton);
      QApplication::processEvents();
      settle();
      const auto summary = [](std::vector<double> values) {
        std::sort(values.begin(), values.end());
        std::ostringstream text;
        text << std::fixed << std::setprecision(2) << "median=" << values[values.size() / 2]
             << " max=" << values.back();
        return text.str();
      };
      std::cout << "[perf] brush_stroke " << label << " zoom=" << canvas->zoom()
                << " size=" << brush_size << " move_ms " << summary(move_ms) << " paint_ms " << summary(paint_ms)
                << '\n';
    }
  }
}

// Process CPU time across all threads, in milliseconds.
double process_cpu_ms() {
#ifdef Q_OS_WIN
  FILETIME created{};
  FILETIME exited{};
  FILETIME kernel{};
  FILETIME user{};
  if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) == 0) {
    return 0.0;
  }
  const auto ticks = [](FILETIME time) {
    return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32U) | time.dwLowDateTime;
  };
  return static_cast<double>(ticks(kernel) + ticks(user)) / 10000.0;
#else
  return 1000.0 * static_cast<double>(std::clock()) / static_cast<double>(CLOCKS_PER_SEC);
#endif
}

#ifdef Q_OS_WIN
// CPU milliseconds per thread of this process, keyed by thread id, with each thread's
// description (worker pools name their threads).
std::map<DWORD, std::pair<double, std::string>> thread_cpu_ms() {
  std::map<DWORD, std::pair<double, std::string>> result;
  const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return result;
  }
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  for (auto ok = Thread32First(snapshot, &entry); ok != FALSE; ok = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID != GetCurrentProcessId()) {
      continue;
    }
    const auto thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
    if (thread == nullptr) {
      continue;
    }
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetThreadTimes(thread, &created, &exited, &kernel, &user) != 0) {
      const auto ticks = [](FILETIME time) {
        return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32U) | time.dwLowDateTime;
      };
      std::string name = entry.th32ThreadID == GetCurrentThreadId() ? "main" : "";
      PWSTR description = nullptr;
      if (SUCCEEDED(GetThreadDescription(thread, &description)) && description != nullptr) {
        if (description[0] != L'\0') {
          name += (name.empty() ? "" : " ") + QString::fromWCharArray(description).toStdString();
        }
        LocalFree(description);
      }
      result[entry.th32ThreadID] = {static_cast<double>(ticks(kernel) + ticks(user)) / 10000.0, name};
    }
    CloseHandle(thread);
  }
  CloseHandle(snapshot);
  return result;
}

void print_busy_threads(const std::map<DWORD, std::pair<double, std::string>>& before,
                        const std::map<DWORD, std::pair<double, std::string>>& after) {
  for (const auto& [id, value] : after) {
    const auto found = before.find(id);
    const auto delta = value.first - (found == before.end() ? 0.0 : found->second.first);
    if (delta >= 15.0) {
      std::cout << "[perf]   thread " << id << " '" << value.second << "' cpu_ms=" << delta << '\n';
    }
  }
}
#endif

// Counts what the event loop delivers while nobody touches the app: paints per widget
// and timer events per timer owner (GitHub issue 135).
class IdleEventCounter final : public QObject {
 public:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::Paint) {
      ++paints[describe(watched)];
    } else if (event->type() == QEvent::Timer) {
      auto owner = describe(watched);
      if (const auto* timer = qobject_cast<QTimer*>(watched); timer != nullptr) {
        owner += " " + std::to_string(timer->interval()) + "ms";
      }
      if (watched->parent() != nullptr) {
        owner += " in " + describe(watched->parent());
      }
      ++timers[owner];
    }
    return false;
  }

  static int total(const std::map<std::string, int>& counts) {
    int sum = 0;
    for (const auto& [name, count] : counts) {
      sum += count;
    }
    return sum;
  }

  static void print_top(const char* label, const std::map<std::string, int>& counts) {
    std::vector<std::pair<int, std::string>> sorted;
    for (const auto& [name, count] : counts) {
      sorted.emplace_back(count, name);
    }
    std::sort(sorted.rbegin(), sorted.rend());
    for (std::size_t index = 0; index < sorted.size() && index < 8U; ++index) {
      std::cout << "[perf]   " << label << ' ' << sorted[index].first << "x " << clean_name(sorted[index].second)
                << '\n';
    }
  }

  std::map<std::string, int> paints;
  std::map<std::string, int> timers;

 private:
  static std::string describe(const QObject* object) {
    auto text = std::string(object->metaObject()->className());
    if (!object->objectName().isEmpty()) {
      text += "#" + object->objectName().toStdString();
    }
    return text;
  }
};

void wait_event_loop_ms(int milliseconds) {
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec();
}

// A fresh print-size document left alone with the Brush active must cost next to no CPU
// (GitHub issue 135: fans spun up as soon as an A5 canvas was created).
// PATCHY_PERF_IDLE_SECONDS sets the measured window (default 10).
void idle_new_document_perf() {
  isolate_perf_settings();
  patchy::ui::MainWindow window;
  window.resize(1600, 1000);
  if (qEnvironmentVariableIsSet("PATCHY_PERF_ONSCREEN")) {
    window.showMaximized();
  } else {
    window.show();
  }
  QApplication::processEvents();
  const auto seconds = std::max(1, qEnvironmentVariableIntValue("PATCHY_PERF_IDLE_SECONDS"));
  const auto window_ms = (qEnvironmentVariableIsSet("PATCHY_PERF_IDLE_SECONDS") ? seconds : 10) * 1000;
  struct Case {
    const char* name;
    int width;
    int height;
    double ppi;
  };
  for (const auto& size : {Case{"A5_300ppi", 1748, 2480, 300.0}, Case{"A4_300ppi", 2480, 3508, 300.0},
                           Case{"A5_72ppi", 420, 595, 72.0}}) {
    const auto create_cpu = process_cpu_ms();
    const auto create_ms = elapsed_ms([&] {
      patchy::ui::MainWindowTestAccess::new_document(window, size.width, size.height, size.ppi);
      QApplication::processEvents();
    });
    auto* canvas = active_canvas(window);
    CHECK(canvas != nullptr);
    if (canvas == nullptr) {
      return;
    }
    canvas->fit_to_view();
    canvas->set_tool(patchy::ui::CanvasTool::Brush);
    // Creation-time work (first render, thumbnails, debounced refreshes) finishes first.
    wait_event_loop_ms(2000);
    const auto settle_cpu = process_cpu_ms() - create_cpu;
    IdleEventCounter counter;
    qApp->installEventFilter(&counter);
#ifdef Q_OS_WIN
    const auto threads_before = thread_cpu_ms();
#endif
#ifdef Q_OS_WIN
    // PATCHY_PERF_SAMPLER=1 attributes the idle main thread's work.
    std::unique_ptr<MainThreadSampler> sampler;
    if (qEnvironmentVariableIsSet("PATCHY_PERF_SAMPLER")) {
      sampler = std::make_unique<MainThreadSampler>();
    }
#endif
    const auto cpu_before = process_cpu_ms();
    const auto wall_ms = elapsed_ms([&] { wait_event_loop_ms(window_ms); });
    const auto cpu_ms = process_cpu_ms() - cpu_before;
#ifdef Q_OS_WIN
    if (sampler) {
      std::cout << "[SAMPLER_PHASE] idle " << size.name << '\n';
      sampler.reset();
    }
#endif
    qApp->removeEventFilter(&counter);
    const auto percent = 100.0 * cpu_ms / wall_ms;
    std::cout << std::fixed << std::setprecision(2) << "[PERF] idle " << size.name << ' ' << size.width << 'x'
              << size.height << " create_ms=" << create_ms << " first_2s_cpu_ms=" << settle_cpu
              << " idle_cpu_ms=" << cpu_ms << " wall_ms=" << wall_ms << " cpu_pct_one_core=" << percent
              << " paints=" << IdleEventCounter::total(counter.paints)
              << " timer_events=" << IdleEventCounter::total(counter.timers) << '\n';
    IdleEventCounter::print_top("paint", counter.paints);
    IdleEventCounter::print_top("timer", counter.timers);
#ifdef Q_OS_WIN
    print_busy_threads(threads_before, thread_cpu_ms());
#endif
  }
  // The brush preset library's once-a-second change check (a folder listing), against the
  // full content check it replaced.
  auto& library = window.brush_automation_library();
  const auto median_ms = [](const std::function<void()>& work) {
    std::vector<double> times;
    for (int index = 0; index < 20; ++index) {
      times.push_back(elapsed_ms(work));
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
  };
  const auto check_ms = median_ms([&] { library.refresh_if_files_changed(); });
  const auto full_ms = median_ms([&] { library.refresh(); });
  std::cout << std::fixed << std::setprecision(2) << "[PERF] idle brush_library check_ms=" << check_ms
            << " full_read_ms=" << full_ms << " tips=" << library.tips().size()
            << " presets=" << library.presets().size() << '\n';
}

// Ctrl+J over and over on a fresh document (GitHub issue 402: the UI lagged past about
// 100 layers while the reporter duplicated a text layer). Each duplicate is timed as the
// call itself plus the event loop and canvas settling after it, the way a held Ctrl+J
// feels; the report gives the mean per bucket. PATCHY_PERF_DUP_COUNT (default 300) sets
// how many copies, PATCHY_PERF_DUP_FILL=1 duplicates the full-canvas white Background
// instead of a text layer, PATCHY_PERF_SAMPLER=1 samples the main thread per bucket.
void duplicate_layer_perf() {
  isolate_perf_settings();
  patchy::ui::MainWindow window;
  window.resize(1600, 1000);
  if (qEnvironmentVariableIsSet("PATCHY_PERF_ONSCREEN")) {
    window.showMaximized();
  } else {
    window.show();
  }
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::new_document(window, 1920, 1080, 72.0);
  QApplication::processEvents();
  auto* canvas = active_canvas(window);
  CHECK(canvas != nullptr);
  if (canvas == nullptr) {
    return;
  }
  canvas->fit_to_view();
  const auto settle = [&] {
    // A key press returns to exec(), which runs the rows the rebuild retired with
    // deleteLater(); this loop has no exec(), so it delivers them itself.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
    const auto started = Clock::now();
    while (!canvas->render_settled() && std::chrono::duration<double>(Clock::now() - started).count() < 30.0) {
      canvas->repaint();
      QApplication::processEvents();
    }
  };
  settle();
  if (!qEnvironmentVariableIsSet("PATCHY_PERF_DUP_FILL")) {
    patchy::ui::MainWindowTestAccess::add_text_at(window, QPoint(200, 120));
    QApplication::processEvents();
    auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    CHECK(editor != nullptr);
    if (editor == nullptr) {
      return;
    }
    editor->textCursor().insertText(QStringLiteral("NOT FOR YOU"));
    QApplication::processEvents();
    send_key(*editor, Qt::Key_Return, Qt::ControlModifier);
    settle();
    CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr ||
          canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"))->property("patchy.textEditorFinished").toBool());
  }
  const auto count = qEnvironmentVariableIsSet("PATCHY_PERF_DUP_COUNT")
                         ? std::max(10, qEnvironmentVariableIntValue("PATCHY_PERF_DUP_COUNT"))
                         : 300;
  const auto initial_layers = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size();
  struct Bucket {
    double call_ms{0.0};
    double total_ms{0.0};
    double worst_ms{0.0};
    int samples{0};
  };
  Bucket bucket;
#ifdef Q_OS_WIN
  std::unique_ptr<MainThreadSampler> sampler;
  const bool sample = qEnvironmentVariableIsSet("PATCHY_PERF_SAMPLER");
  if (sample) {
    sampler = std::make_unique<MainThreadSampler>();
  }
#endif
  for (int index = 1; index <= count; ++index) {
    const auto started = Clock::now();
    patchy::ui::MainWindowTestAccess::duplicate_active_layer(window);
    const auto call_ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    settle();
    const auto total_ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    bucket.call_ms += call_ms;
    bucket.total_ms += total_ms;
    bucket.worst_ms = std::max(bucket.worst_ms, total_ms);
    ++bucket.samples;
    const bool report = index == 10 || index % 50 == 0 || index == count;
    if (report) {
      const auto layers = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size();
      std::cout << std::fixed << std::setprecision(2) << "[PERF] duplicate layers=" << layers
                << " mean_call_ms=" << bucket.call_ms / bucket.samples
                << " mean_total_ms=" << bucket.total_ms / bucket.samples << " worst_total_ms=" << bucket.worst_ms
                << " samples=" << bucket.samples << '\n';
#ifdef Q_OS_WIN
      if (sampler) {
        std::cout << "[SAMPLER_PHASE] duplicate up_to_" << layers << '\n';
        sampler.reset();
        if (index != count) {
          sampler = std::make_unique<MainThreadSampler>();
        }
      }
#endif
      bucket = Bucket{};
    }
  }
  CHECK(std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().size() ==
        initial_layers + static_cast<std::size_t>(count));
}

// One stroke per brush size and hardness on a fresh A5 300 ppi document (GitHub issue
// 135: lag grew with the brush size). Moves are 12 widget px apart, a brisk stroke, or 3 px
// apart ("slow"), the short steps a high-rate mouse delivers.
void a5_brush_stroke_perf() {
  isolate_perf_settings();
  patchy::ui::MainWindow window;
  window.resize(1600, 1000);
  if (qEnvironmentVariableIsSet("PATCHY_PERF_ONSCREEN")) {
    window.showMaximized();
  } else {
    window.show();
  }
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::new_document(window, 1748, 2480, 300.0);
  QApplication::processEvents();
  auto* canvas = active_canvas(window);
  CHECK(canvas != nullptr);
  if (canvas == nullptr) {
    return;
  }
  const auto settle = [canvas] {
    const auto started = Clock::now();
    while (!canvas->render_settled() && std::chrono::duration<double>(Clock::now() - started).count() < 60.0) {
      canvas->repaint();
      QApplication::processEvents();
    }
  };
  canvas->set_tool(patchy::ui::CanvasTool::Brush);
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_opacity(100);
  const auto send = [canvas](QEvent::Type type, QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, position, canvas->mapToGlobal(position), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(canvas, &event);
  };
  struct Pass {
    const char* label;
    double zoom;
    double step;
  };
  for (const auto& pass : {Pass{"fit", 0.0, 12.0}, Pass{"100%", 1.0, 12.0}, Pass{"100%_slow", 1.0, 3.0}}) {
    const auto zoom = pass.zoom;
    const std::string label = pass.label;
    for (const int softness : {0, 100}) {
      for (const int brush_size : {50, 300, 731}) {
        if (zoom == 0.0) {
          canvas->fit_to_view();
        } else {
          canvas->set_zoom_centered(zoom);
        }
        canvas->set_brush_size(brush_size);
        canvas->set_brush_softness(softness);
        settle();
        const auto start = QPointF(canvas->width() * 0.2, canvas->height() * 0.5);
        const auto at = [&](int index) {
          return start + QPointF(pass.step * index, pass.step * 3.0 * std::sin(index * 0.15));
        };
        send(QEvent::MouseButtonPress, at(0), Qt::LeftButton, Qt::LeftButton);
        canvas->repaint();
        std::vector<double> move_ms;
        std::vector<double> paint_ms;
#ifdef Q_OS_WIN
        // PATCHY_PERF_SAMPLER=1 attributes each case's moves and paints.
        std::unique_ptr<MainThreadSampler> sampler;
        if (qEnvironmentVariableIsSet("PATCHY_PERF_SAMPLER")) {
          sampler = std::make_unique<MainThreadSampler>();
        }
#endif
        const auto cpu_before = process_cpu_ms();
        const auto stroke_ms = elapsed_ms([&] {
          for (int index = 1; index <= 60; ++index) {
            move_ms.push_back(elapsed_ms([&] { send(QEvent::MouseMove, at(index), Qt::NoButton, Qt::LeftButton); }));
            paint_ms.push_back(elapsed_ms([&] { canvas->repaint(); }));
          }
        });
        const auto stroke_cpu = process_cpu_ms() - cpu_before;
#ifdef Q_OS_WIN
        if (sampler) {
          std::cout << "[SAMPLER_PHASE] a5brush " << label << " size=" << brush_size << " softness=" << softness
                    << '\n';
          sampler.reset();
        }
#endif
        const auto release_ms = elapsed_ms([&] {
          send(QEvent::MouseButtonRelease, at(60), Qt::LeftButton, Qt::NoButton);
          QApplication::processEvents();
          settle();
        });
        const auto summary = [](std::vector<double> values) {
          std::sort(values.begin(), values.end());
          double total = 0.0;
          for (const auto value : values) {
            total += value;
          }
          std::ostringstream text;
          text << std::fixed << std::setprecision(2) << "avg=" << total / static_cast<double>(values.size())
               << " median=" << values[values.size() / 2] << " max=" << values.back();
          return text.str();
        };
        std::cout << std::fixed << std::setprecision(2) << "[PERF] a5brush " << label << " zoom=" << canvas->zoom()
                  << " size=" << brush_size << " softness=" << softness << " stroke_ms=" << stroke_ms
                  << " stroke_cpu_ms=" << stroke_cpu << " release_ms=" << release_ms << " move_ms "
                  << summary(move_ms) << " paint_ms " << summary(paint_ms) << '\n';
        patchy::ui::MainWindowTestAccess::undo(window);
        settle();
      }
    }
  }
}

// A small 8-bit CMYK PSD painted with the Brush, in CMYK Proof and after leaving it,
// against a plain RGB document of the same size (GitHub issue 220's second sample, ag-psd's
// test/read/cmyk/src.psd). PATCHY_PERF_CMYK_PSD overrides the fixture path.
void cmyk_brush_stroke_perf_if_available() {
  const auto env_path = qEnvironmentVariable("PATCHY_PERF_CMYK_PSD");
  const auto path = env_path.isEmpty()
      ? patchy::test::local_format_fixture_path("cmyk-brush-perf", "ag-psd-cmyk-src.psd")
      : patchy::ui::to_filesystem_path(env_path);
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local-test-fixtures/cmyk-brush-perf/ag-psd-cmyk-src.psd missing\n";
    return;
  }
  isolate_perf_settings();
  {
    // Both would block the interactive open (tests/ui/main.cpp turns them off the same way).
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/cmykProofOpenNotice"), false);
    settings.setValue(QStringLiteral("imports/showMissingFontsDialog"), false);
    settings.sync();
  }
  const auto rgb_path = std::filesystem::absolute("test-artifacts/perf_cmyk_brush_rgb_200.png");
  {
    QImage image(200, 200, QImage::Format_RGB32);
    image.fill(Qt::white);
    CHECK(image.save(patchy::ui::to_qstring(rgb_path)));
  }
  patchy::ui::MainWindow window;
  window.resize(1600, 1000);
  if (qEnvironmentVariableIsSet("PATCHY_PERF_ONSCREEN")) {
    window.showMaximized();
  } else {
    window.show();
  }
  QApplication::processEvents();
  const auto stroke = [&](const std::string& label, double radius_fraction = 0.3) {
    auto* canvas = active_canvas(window);
    CHECK(canvas != nullptr);
    const auto settle = [canvas] {
      const auto started = Clock::now();
      while (!canvas->render_settled() && std::chrono::duration<double>(Clock::now() - started).count() < 60.0) {
        canvas->repaint();
        QApplication::processEvents();
      }
    };
    canvas->set_tool(patchy::ui::CanvasTool::Brush);
    canvas->set_primary_color(Qt::black);
    canvas->set_brush_opacity(100);
    canvas->set_brush_softness(0);
    canvas->set_brush_size(12);
    canvas->set_zoom_centered(3.0);
    settle();
    const auto center = QPointF(canvas->width() / 2.0, canvas->height() / 2.0);
    const auto radius = 200.0 * canvas->zoom() * radius_fraction;
    const auto at = [&](int index) {
      const auto angle = index * 0.1;
      return QPointF(center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle));
    };
    const auto send = [canvas](QEvent::Type type, QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons) {
      QMouseEvent event(type, position, canvas->mapToGlobal(position), button, buttons, Qt::NoModifier);
      QApplication::sendEvent(canvas, &event);
    };
    std::vector<double> move_ms;
    std::vector<double> paint_ms;
    const auto stats_before = patchy::native_cmyk_render_stats();
    const auto started = Clock::now();
    send(QEvent::MouseButtonPress, at(0), Qt::LeftButton, Qt::LeftButton);
    canvas->repaint();
    for (int index = 1; index <= 60; ++index) {
      move_ms.push_back(elapsed_ms([&] { send(QEvent::MouseMove, at(index), Qt::NoButton, Qt::LeftButton); }));
      paint_ms.push_back(elapsed_ms([&] { canvas->repaint(); }));
    }
    send(QEvent::MouseButtonRelease, at(60), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    settle();
    const auto total = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    const auto summary = [](std::vector<double> values) {
      std::sort(values.begin(), values.end());
      std::ostringstream text;
      text << std::fixed << std::setprecision(2) << "median=" << values[values.size() / 2] << " max=" << values.back();
      return text.str();
    };
    std::cout << "[perf] cmyk_brush " << label << " move_ms " << summary(move_ms) << " paint_ms "
              << summary(paint_ms) << " stroke_total_ms=" << std::fixed << std::setprecision(1) << total << " transform_builds="
              << patchy::native_cmyk_render_stats().transform_builds - stats_before.transform_builds
              << " separated_px=" << patchy::native_cmyk_render_stats().separated_pixels - stats_before.separated_pixels
              << std::endl;
  };
  patchy::ui::MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(path));
  QApplication::processEvents();
  std::cout << "[perf] cmyk_brush opened cmyk_proof=" << patchy::ui::MainWindowTestAccess::cmyk_proof(window) << std::endl;
  stroke("cmyk_proof");
  stroke("cmyk_proof_second_stroke", 0.2);
  // A fresh copy left in RGB mode before its first stroke.
  const auto rgb_mode_copy = std::filesystem::absolute("test-artifacts/perf_cmyk_brush_rgb_mode.psd");
  std::filesystem::copy_file(path, rgb_mode_copy, std::filesystem::copy_options::overwrite_existing);
  patchy::ui::MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(rgb_mode_copy));
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::set_document_cmyk_proof(window, false);
  QApplication::processEvents();
  std::cout << "[perf] cmyk_brush copy cmyk_proof=" << patchy::ui::MainWindowTestAccess::cmyk_proof(window) << std::endl;
  stroke("converted_to_rgb");
  patchy::ui::MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(rgb_path));
  QApplication::processEvents();
  stroke("plain_rgb_200");
}

int main(int argc, char* argv[]) {
  patchy::test::suppress_crash_dialogs();
#ifdef Q_OS_WIN
  g_main_thread_handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                                    GetCurrentThreadId());
#endif
  // PATCHY_PERF_ONSCREEN=1 keeps the platform window real so paint timings include
  // the native raster backend and device pixel ratio.
  if (!qEnvironmentVariableIsSet("PATCHY_PERF_ONSCREEN")) {
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  }
  QApplication app(argc, argv);
  const auto finish_workers = qScopeGuard([] { patchy::ui::wait_for_tracked_background_workers(); });
  // Shared by every scenario: the user's settings store stays untouched (an interactive
  // Patchy may be open), and the two open-time notices that would block an unattended
  // open stay off, as in tests/ui/main.cpp. Before October 25, 2026 only the cmykbrush
  // scenario did this, so the default run sat in the Missing Fonts dialog of the
  // Quintavius document on any box whose fonts the offscreen platform cannot see.
  isolate_perf_settings();
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/cmykProofOpenNotice"), false);
    settings.setValue(QStringLiteral("imports/showMissingFontsDialog"), false);
    settings.setValue(QStringLiteral("imports/showMissingLinksDialog"), false);
    settings.setValue(QStringLiteral("imports/afImageLayers"), QStringLiteral("smart"));  // no Affinity question
    settings.setValue(QStringLiteral("text/warnMissingFontsOnTransform"), false);
    settings.setValue(QStringLiteral("updates/checkOnStartup"), false);
    settings.sync();
  }
  // Every scenario runs under the suites' watchdog (15 minutes here; the scenarios open
  // 70 Mpx documents and time whole drags): a stalled scenario prints its name, the
  // open windows and the main thread's stack, then exits 124 (October 25, 2026: a
  // default run sat silently for two hours in a modal dialog nobody could see).
  patchy::test::TestWatchdog watchdog(patchy::test::test_timeout_seconds(900), report_open_windows);
  const auto run_scenario = [&watchdog](const char* name, auto&& scenario) {
    watchdog.begin(name);
    scenario();
    watchdog.end();
    std::cout << std::flush;
  };
  try {
    if (argc > 1 && std::string_view(argv[1]) == "zoom") {
      run_scenario("zoom", tent_psb_zoom_step_perf_if_available);
      return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "layerpanel") {
      run_scenario("layerpanel", quintavius_layer_panel_perf_if_available);
      return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "brushstroke") {
      run_scenario("brushstroke", big_brush_stroke_perf);
      return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "idle") {
      run_scenario("idle", idle_new_document_perf);
      return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "a5brush") {
      run_scenario("a5brush", a5_brush_stroke_perf);
      return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "cmykbrush") {
      run_scenario("cmykbrush", cmyk_brush_stroke_perf_if_available);
      return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "maskrotate") {
      run_scenario("maskrotate", masked_layer_rotate_perf);
      return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "clipshape") {
      run_scenario("clipshape", clipped_shape_resize_perf);
      return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "warpdrag") {
      run_scenario("warpdrag", warp_drag_perf);
      return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "warpphoto") {
      run_scenario("warpphoto", warp_photo_perf_if_available);
      return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "gradientdrag") {
      run_scenario("gradientdrag", gradient_shape_drag_perf);
      return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "liquifydrag") {
      run_scenario("liquifydrag", liquify_drag_perf);
      return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "duplicate") {
      run_scenario("duplicate", duplicate_layer_perf);
      return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "manylayers") {
      run_scenario("manylayers", many_layers_select_and_move_perf_if_available);
      return 0;
    }
    // Explicit only: launches the app a dozen times (docs/startup-performance.md).
    if (argc > 1 && std::string_view(argv[1]) == "startup") {
      return patchy::perf::run_startup_scenario();
    }
    // Explicit only (never part of the default run): minutes on the 85-page fixture.
    if (argc > 1 && std::string_view(argv[1]) == "pdfopen") {
      run_scenario("pdfopen", [&] { pdf_open_perf_if_available(argc, argv); });
      return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "pdfsave") {
      run_scenario("pdfsave", [&] { pdf_save_perf_if_available(argc, argv); });
      return 0;
    }
    run_scenario("template_dirty_move", template_psd_dirty_move_perf_if_available);
    run_scenario("template_keyboard_nudge", template_psd_ui_keyboard_nudge_perf_if_available);
    run_scenario("zoom", tent_psb_zoom_step_perf_if_available);
    run_scenario("layerpanel", quintavius_layer_panel_perf_if_available);
    run_scenario("manylayers", many_layers_select_and_move_perf_if_available);
    run_scenario("maskrotate", masked_layer_rotate_perf);
    run_scenario("clipshape", clipped_shape_resize_perf);
    run_scenario("warpdrag", warp_drag_perf);
    run_scenario("warpphoto", warp_photo_perf_if_available);
    run_scenario("gradientdrag", gradient_shape_drag_perf);
    run_scenario("liquifydrag", liquify_drag_perf);
  } catch (const std::exception& error) {
    std::cerr << "[FAIL] " << error.what() << '\n';
    return 1;
  }
  return 0;
}
