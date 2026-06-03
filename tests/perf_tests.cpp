#include "core/layer_render_utils.hpp"
#include "core/rect_utils.hpp"
#include "psd/psd_document_io.hpp"
#include "test_harness.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"

#include <QApplication>
#include <QByteArray>
#include <QKeyEvent>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QRegion>
#include <QTabWidget>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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
  return std::filesystem::path("D:/projects/proton_svn/RTOpenCV/Template.psd");
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

}  // namespace

int main(int argc, char* argv[]) {
  patchy::test::suppress_crash_dialogs();
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  QApplication app(argc, argv);
  try {
    template_psd_dirty_move_perf_if_available();
    template_psd_ui_keyboard_nudge_perf_if_available();
  } catch (const std::exception& error) {
    std::cerr << "[FAIL] " << error.what() << '\n';
    return 1;
  }
  return 0;
}
