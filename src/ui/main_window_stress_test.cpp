// Profiling stress test (Preferences > Application > Development, or
// `patchy --stress-test=<preset>`). Builds the "PATCHY 64" retro desk scene in
// the real window while timing every step, then writes JSON + TXT reports.
//
// Design notes:
// - Every step ends with settle(): repaint() (synchronous, includes the
//   blocking large-canvas recomposite) plus an event pump until
//   CanvasWidget::render_settled(), so a step's wall time is "user sees the
//   result".
// - Steps are keyed by stable ids ("05_monitor_body"); the baseline table and
//   report diffs depend on them, so never rename one.
// - push_undo_snapshot copies the whole Document (expensive at 4096 px), so
//   scene steps that would otherwise commit dozens of tool strokes write cells
//   directly into the layer buffer under ONE undo snapshot and invalidate per
//   cell (paint_cells_direct) - that per-rect invalidation is itself the
//   scattered-dirty-rect probe. The runner also trims the undo stack between
//   steps to keep peak memory flat.

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/stress_test.hpp"

#include "core/adjustment_layer.hpp"
#include "core/document_path.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/path_fit.hpp"
#include "core/pixel_tools.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_object.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_shape.hpp"
#include "core/rect_utils.hpp"
#include "ui/app_settings.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/custom_shape_library.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/image_document_io.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/paths_panel.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/selection_outline.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#define NOMINMAX
#define PSAPI_VERSION 2
#include <windows.h>

#include <psapi.h>
#endif
#ifdef Q_OS_LINUX
#include <unistd.h>
#endif
#ifdef Q_OS_MACOS
#include <sys/resource.h>
#include <sys/sysctl.h>
#endif

namespace patchy::ui {

namespace {

constexpr int kSettleTimeoutMs = 30'000;
constexpr quint32 kStressSeedBase = 0x5EED0001U;
// Keep in sync with the scenario's step() calls; drives the progress dialog.
constexpr int kTotalStepCount = 59;

// Reference step durations in ms, measured on the calibration machine at the
// preset named in the tag, in a release build. Only the rating depends on
// these; the TXT report ends with a ready-to-paste refreshed table. Bump the
// tag whenever the table is recalibrated.
constexpr const char* kBaselineTag = "quick@i9-12900KS-2026-07-19c";

struct StepBaseline {
  const char* id;
  double ms;
};

constexpr StepBaseline kStepBaselines[] = {
    {"01_create_document", 131.7},
    {"02_wall_gradient", 102.2},
    {"03_wall_texture", 176.1},
    {"04_desk_surface", 269.8},
    {"05_monitor_body", 236.2},
    {"06_monitor_screen", 222.0},
    {"07_c64_body", 177.8},
    {"08_c64_keys", 141.4},
    {"09_c64_badge", 259.6},
    {"10_mug_steam", 655.6},
    {"11_desk_props", 1378.0},
    {"12_boot_text", 743.9},
    {"13_boot_text_glow", 196.4},
    {"14_game_parody_pixels", 216.9},
    {"15_game_title_text", 616.8},
    {"16_sticky_notes", 1202.1},
    {"17_title_text", 265.0},
    {"18_text_reedit", 193.0},
    {"19_scanlines", 223.1},
    {"20_screen_glow", 477.3},
    {"21_screen_mask", 369.1},
    {"22_selection_blur", 180.4},
    {"23_vignette", 319.2},
    {"24_grain", 329.3},
    {"25_adjust_levels", 259.5},
    {"26_adjust_curves", 264.2},
    {"27_adjust_hue_sat", 338.1},
    {"28_adjust_color_balance", 379.2},
    {"29_move_small_live", 369.9},
    {"30_move_styled_outline", 1469.3},
    {"31_move_large_outline", 3653.8},
    {"32_move_multi_layer", 212.3},
    {"33_move_nudges", 473.8},
    {"34_free_transform", 431.4},
    {"35_zoom_pan", 139.5},
    {"36_history_build", 103.5},
    {"37_undo_redo", 4040.9},
    {"38_merge_visible", 1466.3},
    {"39_psd_save", 585.7},
    {"40_psd_reload", 892.7},
    {"41_multi_document", 507.7},
    {"44_canvas_expand", 921.5},
    {"45_arcade_shapes", 7452.2},
    {"46_shape_boolean", 3784.1},
    {"47_neon_pen_glow", 1353.6},
    {"48_custom_shapes", 3051.1},
    {"49_shape_restyle", 658.0},
    {"50_paths_roundtrip", 149.4},
    {"51_new_blurs", 2257.1},
    {"57_arcade_screen_game", 2250.1},
    {"58_marquee_text", 2735.4},
    {"52_sharpen_motion", 1518.4},
    {"53_plastic_wrap", 1054.8},
    {"59_poster_age_coin", 1204.6},
    {"54_smart_object_filter", 2349.1},
    {"55_warp_banner", 4166.7},
    {"56_psd_roundtrip_full", 3581.2},
    {"42_final_composite", 697.2},
    {"43_vector_shapes", 4519.8},
};

double baseline_for_step(const QString& id) {
  for (const auto& baseline : kStepBaselines) {
    if (id == QLatin1String(baseline.id)) {
      return baseline.ms;
    }
  }
  return -1.0;
}

class PaintCounterFilter final : public QObject {
public:
  int paint_events{0};

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::Paint) {
      ++paint_events;
    }
    return QObject::eventFilter(watched, event);
  }
};

quint64 fnv1a64_image(const QImage& image) {
  const auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
  quint64 hash = 0xCBF29CE484222325ULL;
  const auto row_bytes = static_cast<std::size_t>(rgba.width()) * 4U;
  for (int y = 0; y < rgba.height(); ++y) {
    const auto* row = rgba.constScanLine(y);
    for (std::size_t i = 0; i < row_bytes; ++i) {
      hash ^= row[i];
      hash *= 0x00000100000001B3ULL;
    }
  }
  return hash;
}

QString stress_cpu_name() {
#ifdef Q_OS_WIN
  QSettings cpu(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
                QSettings::NativeFormat);
  const auto name = cpu.value(QStringLiteral("ProcessorNameString")).toString().trimmed();
  if (!name.isEmpty()) {
    return name;
  }
#endif
#ifdef Q_OS_LINUX
  QFile cpuinfo(QStringLiteral("/proc/cpuinfo"));
  if (cpuinfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
    while (!cpuinfo.atEnd()) {
      const auto line = QString::fromUtf8(cpuinfo.readLine());
      if (line.startsWith(QStringLiteral("model name"))) {
        const auto colon = line.indexOf(QLatin1Char(':'));
        if (colon >= 0) {
          return line.mid(colon + 1).trimmed();
        }
      }
    }
  }
#endif
#ifdef Q_OS_MACOS
  char brand[256] = {};
  std::size_t size = sizeof(brand);
  if (sysctlbyname("machdep.cpu.brand_string", brand, &size, nullptr, 0) == 0 && brand[0] != '\0') {
    return QString::fromUtf8(brand).trimmed();
  }
#endif
  return QSysInfo::currentCpuArchitecture();
}

qint64 stress_total_ram_mb() {
#ifdef Q_OS_WIN
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status) != 0) {
    return static_cast<qint64>(status.ullTotalPhys / (1024ULL * 1024ULL));
  }
#endif
#ifdef Q_OS_LINUX
  const auto pages = sysconf(_SC_PHYS_PAGES);
  const auto page_size = sysconf(_SC_PAGE_SIZE);
  if (pages > 0 && page_size > 0) {
    return static_cast<qint64>((static_cast<long long>(pages) * page_size) / (1024LL * 1024LL));
  }
#endif
#ifdef Q_OS_MACOS
  std::uint64_t bytes = 0;
  std::size_t size = sizeof(bytes);
  if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0) {
    return static_cast<qint64>(bytes / (1024ULL * 1024ULL));
  }
#endif
  return -1;
}

qint64 stress_peak_working_set_mb() {
#ifdef Q_OS_WIN
  PROCESS_MEMORY_COUNTERS counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
    return static_cast<qint64>(counters.PeakWorkingSetSize / (1024ULL * 1024ULL));
  }
#endif
#ifdef Q_OS_LINUX
  QFile status(QStringLiteral("/proc/self/status"));
  if (status.open(QIODevice::ReadOnly | QIODevice::Text)) {
    while (!status.atEnd()) {
      const auto line = QString::fromUtf8(status.readLine());
      if (line.startsWith(QStringLiteral("VmHWM:"))) {
        const auto parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
          return parts[1].toLongLong() / 1024;
        }
      }
    }
  }
#endif
#ifdef Q_OS_MACOS
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    return static_cast<qint64>(usage.ru_maxrss / (1024LL * 1024LL));
  }
#endif
  return -1;
}

qint64 stress_current_working_set_mb() {
#ifdef Q_OS_WIN
  PROCESS_MEMORY_COUNTERS counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
    return static_cast<qint64>(counters.WorkingSetSize / (1024ULL * 1024ULL));
  }
#endif
#ifdef Q_OS_LINUX
  QFile status(QStringLiteral("/proc/self/status"));
  if (status.open(QIODevice::ReadOnly | QIODevice::Text)) {
    while (!status.atEnd()) {
      const auto line = QString::fromUtf8(status.readLine());
      if (line.startsWith(QStringLiteral("VmRSS:"))) {
        const auto parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
          return parts[1].toLongLong() / 1024;
        }
      }
    }
  }
#endif
  return -1;
}

// after-minus-before, tolerant of counter resets: creating/opening a document
// resets the canvas diagnostics mid-step (set_document), in which case the
// post-reset counts ARE the step's activity.
int diag_field_delta(int before, int after) {
  return after >= before ? after - before : after;
}

std::uint64_t diag_field_delta(std::uint64_t before, std::uint64_t after) {
  return after >= before ? after - before : after;
}

CanvasWidget::RenderCacheDiagnostics diag_delta(const CanvasWidget::RenderCacheDiagnostics& before,
                                                const CanvasWidget::RenderCacheDiagnostics& after) {
  CanvasWidget::RenderCacheDiagnostics delta;
  delta.full_refreshes = diag_field_delta(before.full_refreshes, after.full_refreshes);
  delta.partial_patches = diag_field_delta(before.partial_patches, after.partial_patches);
  delta.move_precommit_patches = diag_field_delta(before.move_precommit_patches, after.move_precommit_patches);
  delta.forced_refreshes = diag_field_delta(before.forced_refreshes, after.forced_refreshes);
  delta.dirty_region_batches = diag_field_delta(before.dirty_region_batches, after.dirty_region_batches);
  delta.dirty_region_rects = diag_field_delta(before.dirty_region_rects, after.dirty_region_rects);
  delta.dirty_region_pixels = diag_field_delta(before.dirty_region_pixels, after.dirty_region_pixels);
  delta.move_preview_patch_reuses =
      diag_field_delta(before.move_preview_patch_reuses, after.move_preview_patch_reuses);
  delta.processing_overlays_shown =
      diag_field_delta(before.processing_overlays_shown, after.processing_overlays_shown);
  delta.processing_overlay_frames =
      diag_field_delta(before.processing_overlay_frames, after.processing_overlay_frames);
  delta.move_outline_previews = diag_field_delta(before.move_outline_previews, after.move_outline_previews);
  return delta;
}

QJsonObject diag_to_json(const CanvasWidget::RenderCacheDiagnostics& diag) {
  QJsonObject object;
  object.insert(QStringLiteral("full_refreshes"), diag.full_refreshes);
  object.insert(QStringLiteral("partial_patches"), diag.partial_patches);
  object.insert(QStringLiteral("move_precommit_patches"), diag.move_precommit_patches);
  object.insert(QStringLiteral("forced_refreshes"), diag.forced_refreshes);
  object.insert(QStringLiteral("dirty_region_batches"), diag.dirty_region_batches);
  object.insert(QStringLiteral("dirty_region_rects"), diag.dirty_region_rects);
  object.insert(QStringLiteral("dirty_region_pixels"), static_cast<double>(diag.dirty_region_pixels));
  object.insert(QStringLiteral("move_preview_patch_reuses"), diag.move_preview_patch_reuses);
  object.insert(QStringLiteral("processing_overlays_shown"), diag.processing_overlays_shown);
  object.insert(QStringLiteral("processing_overlay_frames"), diag.processing_overlay_frames);
  object.insert(QStringLiteral("move_outline_previews"), diag.move_outline_previews);
  return object;
}

QJsonDocument report_to_json(const StressReport& report) {
  QJsonObject root;
  root.insert(QStringLiteral("schema_version"), 1);
  root.insert(QStringLiteral("app_version"), report.app_version);
  root.insert(QStringLiteral("build_type"), report.build_type);
  root.insert(QStringLiteral("qt_version"), report.qt_version);
  root.insert(QStringLiteral("timestamp_utc"), report.started_utc.toString(Qt::ISODate));

  QJsonObject platform;
  platform.insert(QStringLiteral("os"), report.os);
  platform.insert(QStringLiteral("cpu_name"), report.cpu_name);
  platform.insert(QStringLiteral("logical_cores"), report.logical_cores);
  platform.insert(QStringLiteral("ram_mb"), static_cast<double>(report.ram_mb));
  QJsonObject screen;
  screen.insert(QStringLiteral("width"), report.screen_size.width());
  screen.insert(QStringLiteral("height"), report.screen_size.height());
  screen.insert(QStringLiteral("device_pixel_ratio"), report.device_pixel_ratio);
  platform.insert(QStringLiteral("screen"), screen);
  root.insert(QStringLiteral("platform"), platform);

  QJsonObject run;
  run.insert(QStringLiteral("preset"), report.preset_token);
  run.insert(QStringLiteral("canvas_px"), report.canvas_size);
  QJsonObject window;
  window.insert(QStringLiteral("width"), report.window_size.width());
  window.insert(QStringLiteral("height"), report.window_size.height());
  run.insert(QStringLiteral("window"), window);
  QJsonObject viewport;
  viewport.insert(QStringLiteral("width"), report.canvas_viewport.width());
  viewport.insert(QStringLiteral("height"), report.canvas_viewport.height());
  run.insert(QStringLiteral("canvas_viewport"), viewport);
  run.insert(QStringLiteral("offscreen"), report.offscreen);
  run.insert(QStringLiteral("interactive"), report.interactive);
  run.insert(QStringLiteral("settle_timeout_ms"), kSettleTimeoutMs);
  root.insert(QStringLiteral("run"), run);

  QJsonArray steps;
  std::vector<std::pair<QString, double>> category_totals;
  QJsonArray unrated;
  for (const auto& step : report.steps) {
    QJsonObject entry;
    entry.insert(QStringLiteral("id"), step.id);
    entry.insert(QStringLiteral("label"), step.label);
    entry.insert(QStringLiteral("category"), step.category);
    entry.insert(QStringLiteral("ms"), step.ms);
    if (step.megapixels >= 0.0) {
      entry.insert(QStringLiteral("megapixels"), step.megapixels);
      entry.insert(QStringLiteral("mp_per_s"), step.ms > 0.0 ? step.megapixels / (step.ms / 1000.0) : 0.0);
    }
    if (step.fps >= 0.0) {
      entry.insert(QStringLiteral("fps"), step.fps);
    }
    entry.insert(QStringLiteral("frames"), step.frames);
    entry.insert(QStringLiteral("inputs"), step.inputs);
    entry.insert(QStringLiteral("timed_out"), step.timed_out);
    entry.insert(QStringLiteral("diag"), diag_to_json(step.diag_delta));
    if (step.baseline_ms >= 0.0) {
      entry.insert(QStringLiteral("baseline_ms"), step.baseline_ms);
      entry.insert(QStringLiteral("ratio"), step.baseline_ms / std::max(step.ms, 0.1));
    } else {
      unrated.append(step.id);
    }
    steps.append(entry);

    bool found = false;
    for (auto& [name, ms] : category_totals) {
      if (name == step.category) {
        ms += step.ms;
        found = true;
        break;
      }
    }
    if (!found) {
      category_totals.emplace_back(step.category, step.ms);
    }
  }
  root.insert(QStringLiteral("steps"), steps);

  QJsonArray categories;
  for (const auto& [name, ms] : category_totals) {
    QJsonObject category;
    category.insert(QStringLiteral("name"), name);
    category.insert(QStringLiteral("ms"), ms);
    categories.append(category);
  }
  root.insert(QStringLiteral("categories"), categories);

  QJsonObject totals;
  totals.insert(QStringLiteral("ms"), report.total_ms);
  totals.insert(QStringLiteral("rated_steps"),
                static_cast<int>(report.steps.size()) - static_cast<int>(unrated.size()));
  totals.insert(QStringLiteral("unrated_steps"), unrated);
  root.insert(QStringLiteral("totals"), totals);

  QJsonObject rating;
  rating.insert(QStringLiteral("score"), report.rating);
  rating.insert(QStringLiteral("geomean_ratio"), report.geomean_ratio);
  rating.insert(QStringLiteral("baseline_tag"), report.baseline_tag);
  root.insert(QStringLiteral("rating"), rating);

  QJsonObject memory;
  memory.insert(QStringLiteral("peak_working_set_mb"), static_cast<double>(report.peak_working_set_mb));
  memory.insert(QStringLiteral("working_set_end_mb"), static_cast<double>(report.working_set_end_mb));
  root.insert(QStringLiteral("memory"), memory);

  QJsonObject document_info;
  document_info.insert(QStringLiteral("final_layer_count"), report.final_layer_count);
  document_info.insert(QStringLiteral("composite_checksum_fnv1a64"),
                       QStringLiteral("0x%1").arg(report.composite_checksum, 16, 16, QLatin1Char('0')).toUpper());
  document_info.insert(QStringLiteral("checksum_note"),
                       QStringLiteral("comparable on this machine only (text AA varies)"));
  root.insert(QStringLiteral("document"), document_info);

  root.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(report.warnings));
  root.insert(QStringLiteral("cancelled"), report.cancelled);
  root.insert(QStringLiteral("success"), report.success);
  return QJsonDocument(root);
}

QString report_to_text(const StressReport& report) {
  QString text;
  QTextStream out(&text);
  const auto line = QString(88, QLatin1Char('-'));
  out << "Patchy Stress Test Report\n";
  out << "=========================\n";
  out << "Patchy " << report.app_version << " (" << report.build_type << ")  Qt " << report.qt_version << "  |  "
      << report.started_utc.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) << " UTC\n";
  out << report.cpu_name << ", " << report.logical_cores << " threads, "
      << (report.ram_mb >= 0 ? QStringLiteral("%1 GB RAM").arg((report.ram_mb + 512) / 1024)
                             : QStringLiteral("RAM unknown"))
      << "  |  " << report.os << "\n";
  out << "Preset: " << report.preset_token << " (" << report.canvas_size << " px)  |  window "
      << report.window_size.width() << "x" << report.window_size.height() << ", viewport "
      << report.canvas_viewport.width() << "x" << report.canvas_viewport.height()
      << (report.offscreen ? "  |  OFFSCREEN (numbers not comparable to real-screen runs)" : "") << "\n\n";

  out << QStringLiteral("%1 %2 %3 %4 %5 %6\n")
             .arg(QStringLiteral("STEP"), -32)
             .arg(QStringLiteral("CATEGORY"), -12)
             .arg(QStringLiteral("MS"), 9)
             .arg(QStringLiteral("FPS"), 7)
             .arg(QStringLiteral("MP/S"), 7)
             .arg(QStringLiteral("RATIO"), 7);
  out << line << "\n";
  for (const auto& step : report.steps) {
    const auto fps_text = step.fps >= 0.0 ? QString::number(step.fps, 'f', 1) : QStringLiteral("-");
    const auto mps_text = (step.megapixels >= 0.0 && step.ms > 0.0)
                              ? QString::number(step.megapixels / (step.ms / 1000.0), 'f', 1)
                              : QStringLiteral("-");
    const auto ratio_text = step.baseline_ms >= 0.0
                                ? QString::number(step.baseline_ms / std::max(step.ms, 0.1), 'f', 2)
                                : QStringLiteral("-");
    out << QStringLiteral("%1 %2 %3 %4 %5 %6%7\n")
               .arg(step.id, -32)
               .arg(step.category, -12)
               .arg(QString::number(step.ms, 'f', 1), 9)
               .arg(fps_text, 7)
               .arg(mps_text, 7)
               .arg(ratio_text, 7)
               .arg(step.timed_out ? QStringLiteral("  [TIMED OUT]") : QString());
  }
  out << line << "\n";

  std::vector<std::pair<QString, double>> category_totals;
  for (const auto& step : report.steps) {
    bool found = false;
    for (auto& [name, ms] : category_totals) {
      if (name == step.category) {
        ms += step.ms;
        found = true;
        break;
      }
    }
    if (!found) {
      category_totals.emplace_back(step.category, step.ms);
    }
  }
  out << "Category subtotals: ";
  for (std::size_t i = 0; i < category_totals.size(); ++i) {
    out << category_totals[i].first << " " << QString::number(category_totals[i].second / 1000.0, 'f', 1) << "s";
    if (i + 1 < category_totals.size()) {
      out << " | ";
    }
  }
  out << "\n\n";
  out << "TOTAL: " << QString::number(report.total_ms / 1000.0, 'f', 1) << " s        RATING: ";
  if (report.rating >= 0.0) {
    out << QString::number(report.rating, 'f', 0) << "   (1000 = baseline " << report.baseline_tag << ")";
  } else {
    out << "n/a";
  }
  out << "\n";
  out << "Peak working set: "
      << (report.peak_working_set_mb >= 0 ? QStringLiteral("%1 MB").arg(report.peak_working_set_mb)
                                          : QStringLiteral("unknown"))
      << "   Final composite checksum: "
      << QStringLiteral("0x%1").arg(report.composite_checksum, 16, 16, QLatin1Char('0')).toUpper() << " ("
      << report.final_layer_count << " layers)\n";
  if (report.warnings.isEmpty()) {
    out << "Warnings: (none)\n";
  } else {
    out << "Warnings:\n";
    for (const auto& warning : report.warnings) {
      out << "  - " << warning << "\n";
    }
  }

  out << "\nSuggested baseline table (paste over kStepBaselines in main_window_stress_test.cpp\n";
  out << "after verifying this run, and bump kBaselineTag):\n";
  for (const auto& step : report.steps) {
    out << QStringLiteral("    {\"%1\", %2},\n").arg(step.id).arg(QString::number(step.ms, 'f', 1));
  }
  out.flush();
  return text;
}

bool write_text_file(const QString& path, const QString& content) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  const auto bytes = content.toUtf8();
  return file.write(bytes) == bytes.size();
}

// ---------------------------------------------------------------------------
// Scene palette + sprite art for the CAFE QUEST / RED CAVALIER game parody.

QColor sprite_color(char key) {
  switch (key) {
    case 'H': return QColor(96, 66, 44);     // Seth hair
    case 'h': return QColor(38, 32, 34);     // Akiko hair
    case 'F': return QColor(236, 200, 170);  // skin
    case 'S': return QColor(70, 110, 180);   // Seth shirt
    case 'D': return QColor(196, 62, 74);    // Akiko dress
    case 'P': return QColor(52, 56, 66);     // pants
    case 'B': return QColor(40, 36, 34);     // shoes
    case 'W': return QColor(242, 238, 228);  // cavalier white
    case 'C': return QColor(152, 84, 46);    // cavalier chestnut
    case 'Y': return QColor(232, 202, 64);   // duck yellow
    case 'O': return QColor(226, 130, 44);   // duck bill
    case 'L': return QColor(210, 118, 40);   // duck legs
    default: return QColor();
  }
}

const char* const kSethSprite[] = {
    ".HHH.",
    ".FFF.",
    ".FgF.",  // g reuses shoes color as glasses
    "SSSSS",
    ".SSS.",
    ".PPP.",
    ".P.P.",
    ".B.B.",
};

const char* const kAkikoSprite[] = {
    ".hhh.",
    "hhhhh",
    "hFFFh",
    ".FFF.",
    "DDDDD",
    ".DDD.",
    ".DDD.",
    ".B.B.",
};

const char* const kCavalierSprite[] = {
    ".CC...C.",
    "CWWC.CWC",
    "CWWWWWWC",
    ".WWWWWW.",
    ".W.WW.W.",
    ".B.BB.B.",
};

const char* const kDuckSprite[] = {
    "..YY",
    "OYYY",
    ".YYY",
    ".L.L",
};

}  // namespace

// ---------------------------------------------------------------------------
// Preset helpers (declared in ui/stress_test.hpp).

std::optional<StressPreset> stress_preset_from_string(QStringView token) {
  const auto lowered = token.toString().toLower();
  if (lowered == QStringLiteral("smoke")) {
    return StressPreset::Smoke;
  }
  if (lowered == QStringLiteral("quick") || lowered.isEmpty()) {
    return StressPreset::Quick;
  }
  if (lowered == QStringLiteral("small")) {
    return StressPreset::Small;
  }
  if (lowered == QStringLiteral("standard")) {
    return StressPreset::Standard;
  }
  if (lowered == QStringLiteral("huge")) {
    return StressPreset::Huge;
  }
  return std::nullopt;
}

QString stress_preset_token(StressPreset preset) {
  switch (preset) {
    case StressPreset::Smoke: return QStringLiteral("smoke");
    case StressPreset::Quick: return QStringLiteral("quick");
    case StressPreset::Small: return QStringLiteral("small");
    case StressPreset::Standard: return QStringLiteral("standard");
    case StressPreset::Huge: return QStringLiteral("huge");
  }
  return QStringLiteral("quick");
}

int stress_preset_canvas_size(StressPreset preset) {
  switch (preset) {
    case StressPreset::Smoke: return 512;
    case StressPreset::Quick: return 1024;
    case StressPreset::Small: return 2048;
    case StressPreset::Standard: return 4096;
    case StressPreset::Huge: return 8192;
  }
  return 1024;
}

// ---------------------------------------------------------------------------
// The runner. A friend of MainWindow; drives tools through synthetic mouse and
// key events (the real input pipeline) plus MainWindow's own commands.

class StressTestRunner {
public:
  StressTestRunner(MainWindow& window, const StressTestOptions& options, QString report_dir)
      : w(window), options_(options), report_dir_(std::move(report_dir)),
        size_(stress_preset_canvas_size(options.preset)) {
    report_.preset_token = stress_preset_token(options_.preset);
    report_.canvas_size = size_;
    report_.interactive = options_.interactive;
  }

  StressReport run();

  [[nodiscard]] StressReport& report() noexcept { return report_; }
  [[nodiscard]] bool was_cancelled() const noexcept { return cancel_requested_; }

private:
  // ---- infrastructure -----------------------------------------------------

  [[nodiscard]] CanvasWidget* canvas() const { return w.canvas_; }
  [[nodiscard]] Document& doc() { return w.document(); }

  // Scale a standard-preset (4096) design coordinate to this run's canvas.
  [[nodiscard]] int at(double fraction_of_canvas) const {
    return std::max(1, static_cast<int>(std::lround(fraction_of_canvas * size_)));
  }
  [[nodiscard]] QPoint pt(double fx, double fy) const { return QPoint(at(fx), at(fy)); }
  [[nodiscard]] QRect rect(double fx, double fy, double fw, double fh) const {
    return QRect(at(fx), at(fy), std::max(1, at(fw)), std::max(1, at(fh)));
  }
  [[nodiscard]] int motion_steps(int standard_steps) const {
    if (size_ >= 4096) {
      return standard_steps;
    }
    if (size_ >= 2048) {
      return std::max(6, (standard_steps * 2) / 3);
    }
    if (size_ >= 1024) {
      return std::max(5, standard_steps / 2);
    }
    return std::max(4, standard_steps / 4);
  }
  [[nodiscard]] double canvas_megapixels() const {
    return static_cast<double>(size_) * static_cast<double>(size_) / 1'000'000.0;
  }

  void pump() { QApplication::processEvents(); }

  void settle() {
    auto* target = canvas();
    if (target == nullptr) {
      return;
    }
    target->repaint();
    QElapsedTimer guard;
    guard.start();
    while ((!target->render_settled() || target->processing_operation_active()) &&
           guard.elapsed() < kSettleTimeoutMs) {
      QApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    if (!target->render_settled()) {
      step_timed_out_ = true;
      return;
    }
    // Big documents defer full recomposites to the async refresh and keep the
    // stale frame up (see CanvasWidget::should_defer_full_refresh_to_async);
    // flush the swapped-in frame so the step's timing ends at "new frame on
    // screen".
    target->repaint();
  }

  void trim_undo(std::size_t keep) {
    if (!w.has_active_document()) {
      return;
    }
    auto& stack = w.session().undo_stack;
    if (stack.size() > keep) {
      stack.erase(stack.begin(), stack.end() - static_cast<std::ptrdiff_t>(keep));
    }
    w.session().redo_stack.clear();
  }

  template <typename Body>
  void step(const char* id, const char* label, const char* category, Body&& body, double megapixels = -1.0,
            bool trim_history = true) {
    ++step_index_;
    if (cancel_requested_) {
      return;
    }
    if (progress_ != nullptr) {
      progress_->setLabelText(MainWindow::tr("Step %1 of %2: %3")
                                  .arg(step_index_)
                                  .arg(kTotalStepCount)
                                  .arg(QLatin1String(label)));
      progress_->setValue(step_index_ - 1);
      QApplication::processEvents();
      if (progress_->wasCanceled()) {
        cancel_requested_ = true;
        return;
      }
    }

    StressStepResult result;
    result.id = QLatin1String(id);
    result.label = QLatin1String(label);
    result.category = QLatin1String(category);
    result.megapixels = megapixels;
    result.baseline_ms = baseline_for_step(result.id);
    step_timed_out_ = false;
    inputs_this_step_ = 0;

    const auto diag_before =
        canvas() != nullptr ? canvas()->render_cache_diagnostics() : CanvasWidget::RenderCacheDiagnostics{};
    QElapsedTimer timer;
    timer.start();
    body();
    settle();
    result.ms = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
    const auto diag_after =
        canvas() != nullptr ? canvas()->render_cache_diagnostics() : CanvasWidget::RenderCacheDiagnostics{};
    result.diag_delta = diag_delta(diag_before, diag_after);
    result.inputs = inputs_this_step_;
    result.timed_out = step_timed_out_;
    if (result.timed_out) {
      report_.warnings.append(QStringLiteral("Step %1 did not settle within %2 ms").arg(result.id).arg(kSettleTimeoutMs));
    }
    report_.steps.push_back(std::move(result));
    if (trim_history) {
      trim_undo(2);
    }
    if (progress_ != nullptr) {
      progress_->setValue(step_index_);
      if (progress_->wasCanceled()) {
        cancel_requested_ = true;
      }
    }
    w.statusBar()->showMessage(
        MainWindow::tr("Running stress test... (%1)").arg(QString::number(report_.steps.size())));
  }

  template <typename Body>
  void fps_step(const char* id, const char* label, const char* category, Body&& body) {
    PaintCounterFilter counter;
    auto* target = canvas();
    if (target != nullptr) {
      target->installEventFilter(&counter);
    }
    step(id, label, category, std::forward<Body>(body));
    if (target != nullptr) {
      target->removeEventFilter(&counter);
    }
    auto& result = report_.steps.back();
    result.frames = counter.paint_events;
    if (result.ms > 0.0 && counter.paint_events > 0) {
      result.fps = counter.paint_events / (result.ms / 1000.0);
    }
  }

  // ---- synthetic input ----------------------------------------------------

  void send_mouse(QEvent::Type type, QPoint position, Qt::MouseButton button, Qt::MouseButtons buttons,
                  Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    auto* target = canvas();
    if (target == nullptr) {
      return;
    }
    QMouseEvent event(type, position, target->mapToGlobal(position), button, buttons, modifiers);
    QApplication::sendEvent(target, &event);
    ++inputs_this_step_;
    pump();
  }

  void send_key(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    auto* target = canvas();
    if (target == nullptr) {
      return;
    }
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers);
    QApplication::sendEvent(target, &release);
    ++inputs_this_step_;
    pump();
  }

  [[nodiscard]] QPoint doc_to_widget(QPoint document_point) {
    auto* target = canvas();
    if (target == nullptr) {
      return {};
    }
    auto widget_point = target->widget_position_for_document_point(document_point);
    if (!target->rect().contains(widget_point)) {
      target->fit_to_view();
      pump();
      widget_point = target->widget_position_for_document_point(document_point);
    }
    return widget_point;
  }

  // Press at path.front(), move through interpolated positions, release at
  // path.back(). Document coordinates.
  void drag_path(const std::vector<QPoint>& document_path, int steps_per_segment) {
    if (document_path.size() < 2) {
      return;
    }
    send_mouse(QEvent::MouseButtonPress, doc_to_widget(document_path.front()), Qt::LeftButton, Qt::LeftButton);
    for (std::size_t segment = 0; segment + 1 < document_path.size(); ++segment) {
      const auto from = document_path[segment];
      const auto to = document_path[segment + 1];
      for (int i = 1; i <= steps_per_segment; ++i) {
        const auto t = static_cast<double>(i) / steps_per_segment;
        const QPoint point(from.x() + static_cast<int>(std::lround((to.x() - from.x()) * t)),
                           from.y() + static_cast<int>(std::lround((to.y() - from.y()) * t)));
        send_mouse(QEvent::MouseMove, doc_to_widget(point), Qt::NoButton, Qt::LeftButton);
      }
    }
    send_mouse(QEvent::MouseButtonRelease, doc_to_widget(document_path.back()), Qt::LeftButton, Qt::NoButton);
  }

  void drag(QPoint from, QPoint to, int move_steps) { drag_path({from, to}, move_steps); }

  // ---- scene helpers ------------------------------------------------------

  void set_foreground(QColor color) { canvas()->set_primary_color(color); }

  void select_move_targets(const std::vector<LayerId>& ids) {
    if (ids.empty()) {
      return;
    }
    doc().set_active_layer(ids.front());
    canvas()->set_selected_layer_ids(ids);
    w.refresh_layer_list();
    pump();
  }

  // MainWindow's layer commands (merge_down etc.) read the layer LIST
  // selection, not the canvas one; select the matching rows.
  void select_layer_rows(const std::vector<LayerId>& ids) {
    if (w.layer_list_ == nullptr) {
      return;
    }
    w.layer_list_->clearSelection();
    for (int row = 0; row < w.layer_list_->count(); ++row) {
      auto* item = w.layer_list_->item(row);
      if (item == nullptr) {
        continue;
      }
      const auto id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
      if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
        item->setSelected(true);
      }
    }
    pump();
  }

  // Filled raster shape via the real tool pipeline (one undo snapshot per
  // drag). The shape tools gained a Shape|Path|Pixels mode whose persisted
  // default is Shape (a vector layer styled by the options-bar fill/stroke,
  // which may be stroke-only); the scene's props need the legacy Pixels
  // commit, so force it for the drag and restore the mode after.
  void draw_shape(CanvasTool tool, QRect document_rect, QColor color, int corner_radius = 0) {
    w.activate_tool(tool);
    const auto previous_mode = canvas()->vector_tool_mode();
    canvas()->set_vector_tool_mode(VectorToolMode::Pixels);
    canvas()->set_fill_shapes(true);
    canvas()->set_shape_corner_radius(corner_radius);
    set_foreground(color);
    drag(document_rect.topLeft(), document_rect.bottomRight(), 4);
    canvas()->set_vector_tool_mode(previous_mode);
  }

  // ---- vector shape helpers (scene 2) -------------------------------------

  [[nodiscard]] static RgbColor rgb(QColor color) {
    return RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                    static_cast<std::uint8_t>(color.blue())};
  }

  [[nodiscard]] static VectorFill solid_vector_fill(QColor color) {
    VectorFill fill;
    fill.kind = VectorFillKind::Solid;
    fill.color = rgb(color);
    return fill;
  }

  [[nodiscard]] static VectorFill no_vector_fill() {
    VectorFill fill;
    fill.kind = VectorFillKind::None;
    return fill;
  }

  [[nodiscard]] static VectorFill gradient_vector_fill(QColor from, QColor to, float angle_degrees) {
    VectorFill fill;
    fill.kind = VectorFillKind::Gradient;
    fill.gradient.type = LayerStyleGradientType::Linear;
    fill.gradient.angle_degrees = angle_degrees;
    fill.gradient.color_stops = {{0.0F, rgb(from)}, {1.0F, rgb(to)}};
    fill.gradient.alpha_stops = {{0.0F, 1.0F}, {1.0F, 1.0F}};
    return fill;
  }

  // The vector steps drive the real Shape-mode commit pipeline, which styles
  // new layers from MainWindow's options-bar mirrors. Those mirrors are
  // persisted tool settings, so save them once before the vector steps and
  // restore them after (cancel between steps still restores: the phase
  // function runs to completion around the skipped step bodies).
  void begin_vector_shape_phase() {
    saved_vector_fill_ = w.current_vector_fill_;
    saved_vector_stroke_paint_ = w.current_vector_stroke_paint_;
    saved_vector_stroke_enabled_ = w.current_vector_stroke_enabled_;
    saved_vector_stroke_width_ = w.current_vector_stroke_width_;
    saved_vector_tool_mode_ = w.current_vector_tool_mode_;
    saved_vector_combine_index_ = w.current_vector_combine_index_;
    saved_shape_corner_radius_ = w.current_shape_corner_radius_;
    saved_vector_line_weight_ = w.current_vector_line_weight_;
    saved_line_arrow_start_ = w.current_line_arrow_start_;
    saved_line_arrow_end_ = w.current_line_arrow_end_;
    w.current_vector_tool_mode_ = VectorToolMode::Shape;
    w.current_vector_combine_index_ = 0;
    if (canvas() != nullptr) {
      canvas()->set_vector_tool_mode(VectorToolMode::Shape);
    }
  }

  void end_vector_shape_phase() {
    w.current_vector_fill_ = saved_vector_fill_;
    w.current_vector_stroke_paint_ = saved_vector_stroke_paint_;
    w.current_vector_stroke_enabled_ = saved_vector_stroke_enabled_;
    w.current_vector_stroke_width_ = saved_vector_stroke_width_;
    w.current_vector_tool_mode_ = saved_vector_tool_mode_;
    w.current_vector_combine_index_ = saved_vector_combine_index_;
    w.current_shape_corner_radius_ = saved_shape_corner_radius_;
    w.current_vector_line_weight_ = saved_vector_line_weight_;
    w.current_line_arrow_start_ = saved_line_arrow_start_;
    w.current_line_arrow_end_ = saved_line_arrow_end_;
    if (canvas() != nullptr) {
      canvas()->set_vector_tool_mode(saved_vector_tool_mode_);
    }
    w.update_vector_swatch_icons();
  }

  void set_vector_appearance(const VectorFill& fill, bool stroke_enabled = false,
                             double stroke_width = 1.0, QColor stroke_color = QColor(0, 0, 0)) {
    w.current_vector_fill_ = fill;
    w.current_vector_stroke_enabled_ = stroke_enabled;
    w.current_vector_stroke_width_ = stroke_width;
    w.current_vector_stroke_paint_ = solid_vector_fill(stroke_color);
  }

  // Vector shape through the real drag pipeline (live preview per mouse move,
  // options-bar appearance commit, bake). Needs begin_vector_shape_phase.
  // The appearance mirrors are set AFTER activate_tool: switching tools while
  // a shape layer is active syncs the mirrors from that layer, which would
  // claw back the previous shape's look over the caller's values.
  LayerId draw_vector_shape(CanvasTool tool, QRect document_rect, const VectorFill& fill,
                            bool stroke_enabled, double stroke_width, QColor stroke_color,
                            const QString& name, int corner_radius = 0) {
    w.activate_tool(tool);
    canvas()->set_vector_tool_mode(VectorToolMode::Shape);
    set_vector_appearance(fill, stroke_enabled, stroke_width, stroke_color);
    w.current_shape_corner_radius_ = corner_radius;
    drag(document_rect.topLeft(), document_rect.bottomRight(), 4);
    if (!name.isEmpty()) {
      rename_active_layer_to(name);
    }
    return doc().active_layer_id().value_or(LayerId{});
  }

  // add_layer anchors on the layer-list ROW selection first and the active
  // layer second; a scripted insert must drop the stale row selection AND set
  // the active layer (step 54 leaves a mid-stack scene-1 row selected).
  void activate_layer_for_insert(LayerId id) {
    if (id == LayerId{} || doc().find_layer(id) == nullptr) {
      return;
    }
    if (w.layer_list_ != nullptr) {
      // Unblocked, clearSelection fires the list's selection handler, which
      // re-asserts the OLD active layer from the still-current row.
      QSignalBlocker blocker(w.layer_list_);
      w.layer_list_->clearSelection();
    }
    doc().set_active_layer(id);
    w.refresh_layer_list();
    pump();
  }

  // Scene-2 prop layers must land above the arcade backdrop regardless of
  // what the previous step left active: hop to the top of the root stack.
  void activate_top_layer() {
    if (!doc().layers().empty()) {
      activate_layer_for_insert(doc().layers().back().id());
    }
  }

  // Folders are created DURING scene construction (the real workflow): group
  // the first layer(s) of an area into a named folder, then later steps
  // insert into it by anchoring on a child via activate_layer_for_insert.
  // create_layer_folder_from_layers keeps the ids' ORDER as the folder's
  // top-to-bottom child order (list-selection callers pass visual order), so
  // sort by the members' current root stacking position first - a wrongly
  // ordered list restacks the group (an opaque backdrop passed first covered
  // the whole folder). Returns the new folder's id (it is left active).
  LayerId group_layers_into_folder(std::vector<LayerId> ids, const QString& folder_name) {
    std::erase(ids, LayerId{});
    if (ids.empty()) {
      report_.warnings.append(QStringLiteral("Folder %1 had no members").arg(folder_name));
      return LayerId{};
    }
    const auto& root = std::as_const(doc()).layers();
    const auto root_index = [&root](LayerId id) {
      for (std::size_t i = 0; i < root.size(); ++i) {
        if (root[i].id() == id) {
          return static_cast<std::ptrdiff_t>(i);
        }
      }
      return static_cast<std::ptrdiff_t>(-1);
    };
    std::sort(ids.begin(), ids.end(),
              [&root_index](LayerId a, LayerId b) { return root_index(a) > root_index(b); });
    w.create_layer_folder_from_layers(std::move(ids));
    rename_active_layer_to(folder_name);
    pump();
    return doc().active_layer_id().value_or(LayerId{});
  }

  // Text layers always commit at the top of the ROOT stack, so scripted text
  // that belongs to an area folder is tucked in right after creation (the
  // layer-panel drag-drop equivalent; becomes the folder's top child).
  void move_layer_into_folder(LayerId layer_id, LayerId folder_id, const QString& undo_label) {
    if (layer_id == LayerId{} || folder_id == LayerId{}) {
      return;
    }
    w.push_undo_snapshot(undo_label);
    auto taken = take_layer_from_tree(doc().layers(), layer_id);
    if (!taken.has_value()) {
      return;
    }
    auto* folder = doc().find_layer(folder_id);
    if (folder == nullptr || folder->kind() != LayerKind::Group) {
      doc().add_layer(std::move(*taken));  // put it back rather than lose it
      return;
    }
    folder->add_child(std::move(*taken));
    doc().set_active_layer(layer_id);
    w.refresh_layer_list();
    w.refresh_layer_controls();
    canvas()->document_changed();
    pump();
  }

  // Document megapixels of the CURRENT canvas (scene 2 runs on the widened
  // one, so canvas_megapixels() understates).
  [[nodiscard]] double current_document_megapixels() {
    if (!w.has_active_document()) {
      return canvas_megapixels();
    }
    return static_cast<double>(doc().width()) * static_cast<double>(doc().height()) / 1'000'000.0;
  }

  // Direct cell writes under one undo snapshot; invalidates per rect so the
  // dirty-region machinery sees the scattered updates (that is the probe).
  void paint_cells_direct(LayerId layer_id, const std::vector<std::pair<QRect, QColor>>& cells,
                          const QString& undo_label) {
    if (cells.empty()) {
      return;
    }
    w.push_undo_snapshot(undo_label);
    auto* layer = doc().find_layer(layer_id);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
      return;
    }
    auto& pixels = layer->pixels();
    const auto bounds = layer->bounds();
    const auto channels = pixels.format().channels;
    for (const auto& [cell, color] : cells) {
      const auto clipped = cell.intersected(QRect(bounds.x, bounds.y, bounds.width, bounds.height));
      if (clipped.isEmpty()) {
        continue;
      }
      for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
          auto* px = pixels.pixel(x - bounds.x, y - bounds.y);
          px[0] = static_cast<std::uint8_t>(color.red());
          px[1] = static_cast<std::uint8_t>(color.green());
          px[2] = static_cast<std::uint8_t>(color.blue());
          if (channels >= 4) {
            px[3] = static_cast<std::uint8_t>(color.alpha());
          }
        }
      }
      canvas()->document_changed(clipped);
    }
    w.refresh_layer_thumbnails();
  }

  void append_sprite_cells(std::vector<std::pair<QRect, QColor>>& cells, const char* const* rows, int row_count,
                           QPoint origin_cell, int cell_px, QPoint game_origin, bool mirror = false) {
    for (int row = 0; row < row_count; ++row) {
      const auto* line = rows[row];
      const auto length = static_cast<int>(std::strlen(line));
      for (int column = 0; column < length; ++column) {
        const char key = line[mirror ? length - 1 - column : column];
        if (key == '.') {
          continue;
        }
        const auto color = key == 'g' ? sprite_color('B') : sprite_color(key);
        if (!color.isValid()) {
          continue;
        }
        cells.emplace_back(QRect(game_origin.x() + (origin_cell.x() + column) * cell_px,
                                 game_origin.y() + (origin_cell.y() + row) * cell_px, cell_px, cell_px),
                           color);
      }
    }
  }

  // The commit tail of MainWindow::apply_filter, without the settings dialog.
  void apply_filter_direct(const QString& identifier, const FilterParameterMap& parameters,
                           const QString& undo_label) {
    auto active = doc().active_layer_id();
    if (!active.has_value()) {
      return;
    }
    auto* layer = doc().find_layer(*active);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
      return;
    }
    const auto selection = canvas()->selected_document_region();
    const auto bounds = layer->bounds();
    const auto original = layer->pixels();
    const auto primary = canvas()->primary_color();
    const auto secondary = canvas()->secondary_color();
    auto invocation = w.filters_.default_invocation(
        identifier.toStdString(),
        RgbColor{static_cast<std::uint8_t>(primary.red()), static_cast<std::uint8_t>(primary.green()),
                 static_cast<std::uint8_t>(primary.blue())},
        RgbColor{static_cast<std::uint8_t>(secondary.red()), static_cast<std::uint8_t>(secondary.green()),
                 static_cast<std::uint8_t>(secondary.blue())});
    for (const auto& [key, value] : parameters) {
      invocation.parameters[key] = value;
    }
    Rect final_bounds = bounds;
    auto final_pixels = build_filter_preview_pixels(
        original, selection, bounds, w.filters_, FilterPreviewSettings{true, std::move(invocation)}, nullptr,
        &final_bounds);
    w.push_undo_snapshot(undo_label);
    layer = doc().find_layer(*active);
    if (layer == nullptr) {
      return;
    }
    set_layer_pixels_with_bounds(*layer, std::move(final_pixels), final_bounds);
    canvas()->document_changed(to_qrect(bounds).united(to_qrect(final_bounds)));
  }

  // Mirrors paste_layer_style_to_selected_layers for one programmatic style.
  void set_layer_style_direct(LayerId layer_id, const LayerStyle& style, const QString& undo_label) {
    auto* layer = doc().find_layer(layer_id);
    if (layer == nullptr) {
      return;
    }
    w.push_undo_snapshot(undo_label);
    layer = doc().find_layer(layer_id);
    auto affected = layer_render_bounds(*layer);
    clear_layer_psd_style_source(*layer);
    layer->layer_style() = style;
    affected = unite_rect(affected, layer_render_bounds(*layer));
    w.refresh_layer_list();
    w.refresh_layer_controls();
    canvas()->document_changed(to_qrect(affected));
  }

  void set_layer_blend_mode(BlendMode mode) {
    const auto index = w.blend_combo_ != nullptr ? w.blend_combo_->findData(static_cast<int>(mode)) : -1;
    if (index >= 0) {
      w.set_active_layer_blend(index);
    }
  }

  void set_layer_opacity_percent(int value) {
    w.set_active_layer_opacity(value);
    w.finish_pending_layer_opacity_edit();
  }

  [[nodiscard]] LayerId add_named_layer(const QString& name) {
    w.add_layer();
    const auto active = doc().active_layer_id();
    if (active.has_value()) {
      if (auto* layer = doc().find_layer(*active); layer != nullptr) {
        layer->set_name(name.toStdString());
      }
      return *active;
    }
    return LayerId{};
  }

  void rename_active_layer_to(const QString& name) {
    if (const auto active = doc().active_layer_id(); active.has_value()) {
      if (auto* layer = doc().find_layer(*active); layer != nullptr) {
        layer->set_name(name.toStdString());
      }
    }
  }

  void use_solid_fill_settings() {
    canvas()->set_fill_opacity(100);
    canvas()->set_fill_softness(0);
  }

  // Crop a freshly painted prop layer to its opaque pixels. add_layer() creates
  // full-canvas buffers, and the move machinery sizes its work (including the
  // outline-preview threshold) from raw layer bounds - a real user's prop
  // layers (paste, PSD, text) are tight, so the scene's should be too.
  void tighten_layer_to_opaque(LayerId layer_id) {
    auto* layer = doc().find_layer(layer_id);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel || layer->pixels().format().channels < 4) {
      return;
    }
    const auto& pixels = layer->pixels();
    const auto bounds = layer->bounds();
    int min_x = pixels.width();
    int min_y = pixels.height();
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < pixels.height(); ++y) {
      for (int x = 0; x < pixels.width(); ++x) {
        if (pixels.pixel(x, y)[3] != 0) {
          min_x = std::min(min_x, x);
          min_y = std::min(min_y, y);
          max_x = std::max(max_x, x);
          max_y = std::max(max_y, y);
        }
      }
    }
    if (max_x < min_x || (min_x == 0 && min_y == 0 && max_x == pixels.width() - 1 && max_y == pixels.height() - 1)) {
      return;  // Empty or already tight.
    }
    const auto width = max_x - min_x + 1;
    const auto height = max_y - min_y + 1;
    PixelBuffer cropped(width, height, pixels.format());
    const auto channels = pixels.format().channels;
    for (int y = 0; y < height; ++y) {
      std::memcpy(cropped.pixel(0, y), pixels.pixel(min_x, min_y + y),
                  static_cast<std::size_t>(width) * channels);
    }
    set_layer_pixels_with_bounds(*layer, std::move(cropped),
                                 Rect{bounds.x + min_x, bounds.y + min_y, width, height});
  }

  void set_linear_gradient(std::vector<GradientStop> stops, int opacity_percent = 100, bool reverse = false) {
    w.activate_tool(CanvasTool::Gradient);
    canvas()->set_gradient_method(GradientMethod::Linear);
    canvas()->set_gradient_reverse(reverse);
    canvas()->set_gradient_opacity(opacity_percent);
    canvas()->set_gradient_stops(std::move(stops));
  }

  void set_radial_gradient(std::vector<GradientStop> stops, int opacity_percent = 100, bool reverse = false) {
    w.activate_tool(CanvasTool::Gradient);
    canvas()->set_gradient_method(GradientMethod::Radial);
    canvas()->set_gradient_reverse(reverse);
    canvas()->set_gradient_opacity(opacity_percent);
    canvas()->set_gradient_stops(std::move(stops));
  }

  [[nodiscard]] static GradientStop gradient_stop(float location, QColor color) {
    GradientStop stop;
    stop.location = location;
    stop.color = EditColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                           static_cast<std::uint8_t>(color.blue()), static_cast<std::uint8_t>(color.alpha())};
    return stop;
  }

  void configure_brush(int size_px, int opacity_percent, int softness_percent, const QString& tip_id,
                       quint32 seed) {
    w.activate_tool(CanvasTool::Brush);
    w.set_active_brush_tip(tip_id, false);
    canvas()->set_brush_size(std::clamp(size_px, 1, kMaxBrushSize));
    canvas()->set_brush_opacity(opacity_percent);
    canvas()->set_brush_softness(softness_percent);
    canvas()->set_brush_dynamics_test_seed(seed);
  }

  [[nodiscard]] QString default_tip_id_by_name(const QString& name) const {
    for (const auto& entry : w.brush_tip_library().entries()) {
      if (entry.name.compare(name, Qt::CaseInsensitive) == 0) {
        return entry.id;
      }
    }
    return builtin_round_brush_tip_id();
  }

  // Creates a committed text layer and returns its id. Sized by target GLYPH
  // PIXELS: the size spin takes points, which the text pipeline converts at
  // the document's print PPI (300 by default, NOT screen 96 - the first cut
  // assumed 96 and every string rendered ~4x too big).
  [[nodiscard]] LayerId add_text_layer(QPoint document_point, const QString& text, int pixel_size,
                                       QColor color) {
    if (w.text_size_spin_ != nullptr) {
      w.text_size_spin_->setValue(std::max(1.0, text_pixels_to_points(std::max(4, pixel_size), doc())));
    }
    set_foreground(color);
    w.add_text_at(document_point);
    pump();
    auto* editor = canvas()->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    if (editor == nullptr) {
      report_.warnings.append(QStringLiteral("Text editor did not open at %1,%2")
                                  .arg(document_point.x())
                                  .arg(document_point.y()));
      return LayerId{};
    }
    editor->setPlainText(text);
    pump();
    w.finish_active_text_editor();
    pump();
    return doc().active_layer_id().value_or(LayerId{});
  }

  void set_monospace_text_family() {
    if (w.text_font_combo_ == nullptr) {
      return;
    }
    const auto families = QFontDatabase::families();
    for (const auto& candidate :
         {QStringLiteral("Consolas"), QStringLiteral("Courier New"), QStringLiteral("DejaVu Sans Mono"),
          QStringLiteral("Menlo"), QStringLiteral("Monaco"), QStringLiteral("Courier")}) {
      if (families.contains(candidate)) {
        w.text_font_combo_->setCurrentFont(QFont(candidate));
        return;
      }
    }
  }

  [[nodiscard]] static int count_layers(const std::vector<Layer>& layers) {
    int count = 0;
    for (const auto& layer : layers) {
      count += 1 + count_layers(layer.children());
    }
    return count;
  }

  // ---- scenario phases ----------------------------------------------------

  void phase_setup();
  void phase_paint_hardware();
  void phase_text();
  void phase_atmosphere();
  void phase_filters();
  void phase_adjustments();
  void phase_move_matrix();
  void phase_interact();
  void phase_history();
  void phase_io();
  void phase_arcade_vector();
  void phase_arcade_filters();
  void phase_smart_and_warp();
  void phase_vector_io();
  void phase_composite();

  MainWindow& w;
  StressTestOptions options_;
  QString report_dir_;
  int size_{4096};
  StressReport report_;
  bool step_timed_out_{false};
  int inputs_this_step_{0};
  int step_index_{0};
  bool cancel_requested_{false};
  QProgressDialog* progress_{nullptr};

  // Layer ids captured while building the scene; the move matrix and style
  // steps target them later.
  LayerId wall_texture_id_{};
  LayerId desk_id_{};
  LayerId monitor_body_id_{};
  LayerId screen_id_{};
  LayerId keys_id_{};
  LayerId mug_id_{};
  LayerId steam_id_{};
  LayerId modem_id_{};
  LayerId floppy_id_{};
  LayerId game_pixels_id_{};
  std::vector<LayerId> boot_text_ids_;
  LayerId load_line_id_{};
  QPoint load_line_point_;
  LayerId sticky_note_id_{};
  LayerId title_text_id_{};
  LayerId bloom_id_{};
  LayerId game_title_id_{};
  LayerId game_credit_id_{};
  LayerId desk_folder_id_{};
  LayerId boot_folder_id_{};
  LayerId signage_folder_id_{};

  // Screen geometry shared between steps (game parody, scanlines, glow).
  QRect screen_rect_;
  QRect game_area_;

  // Scene 2 (the arcade corner on the widened canvas).
  LayerId arcade_wall_id_{};
  LayerId cabinet_id_{};
  LayerId cabinet_folder_id_{};
  LayerId cabinet_screen_id_{};
  LayerId grille_id_{};
  LayerId neon_id_{};
  LayerId arrow_id_{};
  LayerId arcade_game_id_{};
  LayerId arcade_glass_id_{};
  LayerId arcade_poster_id_{};
  QRect arcade_screen_rect_;

  // Saved options-bar vector mirrors (begin/end_vector_shape_phase).
  VectorFill saved_vector_fill_{};
  VectorFill saved_vector_stroke_paint_{};
  bool saved_vector_stroke_enabled_{false};
  double saved_vector_stroke_width_{3.0};
  VectorToolMode saved_vector_tool_mode_{VectorToolMode::Shape};
  int saved_vector_combine_index_{0};
  int saved_shape_corner_radius_{0};
  int saved_vector_line_weight_{4};
  bool saved_line_arrow_start_{false};
  bool saved_line_arrow_end_{false};
};

// ---------------------------------------------------------------------------

void StressTestRunner::phase_setup() {
  step("01_create_document", "Create document + first composite", "setup", [&] {
    w.reset_document(size_, size_, QColor(58, 56, 60), MainWindow::tr("Stress test"));
    canvas()->fit_to_view();
    pump();
  }, canvas_megapixels());
}

void StressTestRunner::phase_paint_hardware() {
  // 02: full-height wall gradient (warm dusk room light).
  step("02_wall_gradient", "Wall gradient (full canvas)", "paint", [&] {
    doc().set_active_layer(doc().layers().front().id());
    rename_active_layer_to(QStringLiteral("Wall"));
    set_linear_gradient({gradient_stop(0.0F, QColor(44, 46, 62)), gradient_stop(0.55F, QColor(92, 74, 78)),
                         gradient_stop(1.0F, QColor(158, 116, 84))});
    drag(pt(0.5, 0.0), pt(0.5, 1.0), motion_steps(24));
  }, canvas_megapixels());

  // 03: clouds texture layer over the wall (the heaviest filter).
  step("03_wall_texture", "Clouds texture layer (multiply)", "filters", [&] {
    wall_texture_id_ = add_named_layer(QStringLiteral("Wall texture"));
    use_solid_fill_settings();
    w.fill_active_layer_with_color(QColor(128, 128, 128), MainWindow::tr("Texture base"));
    apply_filter_direct(
        QStringLiteral("patchy.filters.clouds"),
        {{"scale", std::int64_t{std::clamp(size_ / 16, 12, 512)}},
         {"detail", std::int64_t{5}},
         {"contrast", std::int64_t{45}},
         {"seed", std::int64_t{7}}},
        QStringLiteral("Clouds"));
    set_layer_blend_mode(BlendMode::Multiply);
    set_layer_opacity_percent(24);
  }, canvas_megapixels());

  // 04: desk band + shading + grain.
  step("04_desk_surface", "Desk band + gradient + grain", "paint", [&] {
    desk_id_ = add_named_layer(QStringLiteral("Desk"));
    draw_shape(CanvasTool::Rectangle, rect(0.0, 0.70, 1.0, 0.30), QColor(112, 78, 50));
    w.activate_tool(CanvasTool::Marquee);
    drag(pt(0.0, 0.70), pt(1.0, 1.0), 4);
    set_linear_gradient({gradient_stop(0.0F, QColor(134, 96, 62)), gradient_stop(1.0F, QColor(74, 50, 34))}, 60);
    drag(pt(0.5, 0.70), pt(0.5, 1.0), motion_steps(12));
    canvas()->clear_selection();
    apply_filter_direct(QStringLiteral("patchy.filters.film_grain"), {{"amount", std::int64_t{18}}},
                        QStringLiteral("Desk grain"));
  }, canvas_megapixels() * 0.3);

  // 05: CRT monitor case (bevel styled).
  step("05_monitor_body", "Monitor case shapes + bevel style", "paint", [&] {
    monitor_body_id_ = add_named_layer(QStringLiteral("Monitor"));
    draw_shape(CanvasTool::Rectangle, rect(0.16, 0.15, 0.44, 0.44), QColor(196, 190, 176), at(0.012));
    draw_shape(CanvasTool::Rectangle, rect(0.30, 0.59, 0.16, 0.03), QColor(176, 170, 156));
    draw_shape(CanvasTool::Rectangle, rect(0.26, 0.615, 0.24, 0.015), QColor(150, 144, 132));
    tighten_layer_to_opaque(monitor_body_id_);
    LayerStyle style;
    LayerBevelEmboss bevel;
    bevel.enabled = true;
    bevel.size = static_cast<float>(std::max(2, at(0.004)));
    bevel.depth = 1.4F;
    style.bevels.push_back(bevel);
    LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.opacity = 0.55F;
    shadow.distance = static_cast<float>(std::max(2, at(0.006)));
    shadow.size = static_cast<float>(std::max(3, at(0.01)));
    style.drop_shadows.push_back(shadow);
    set_layer_style_direct(monitor_body_id_, style, QStringLiteral("Monitor style"));
  });

  // 06: screen inset, classic C64 blue-on-blue.
  step("06_monitor_screen", "Boot screen face + inner shadow", "paint", [&] {
    screen_id_ = add_named_layer(QStringLiteral("Screen"));
    screen_rect_ = rect(0.20, 0.185, 0.36, 0.345);
    const auto border = at(0.018);
    draw_shape(CanvasTool::Rectangle, screen_rect_, QColor(134, 122, 222), at(0.008));
    draw_shape(CanvasTool::Rectangle,
               screen_rect_.adjusted(border, border, -border, -border), QColor(56, 44, 172));
    tighten_layer_to_opaque(screen_id_);
    LayerStyle style;
    LayerInnerShadow inner;
    inner.enabled = true;
    inner.opacity = 0.6F;
    inner.distance = static_cast<float>(std::max(2, at(0.004)));
    inner.size = static_cast<float>(std::max(3, at(0.01)));
    style.inner_shadows.push_back(inner);
    set_layer_style_direct(screen_id_, style, QStringLiteral("Screen style"));
    game_area_ = QRect(screen_rect_.left() + border * 2,
                       screen_rect_.top() + screen_rect_.height() / 2,
                       screen_rect_.width() - border * 4,
                       screen_rect_.height() / 2 - border * 2);
  });

  // 07: the breadbin.
  step("07_c64_body", "PATCHY 64 breadbin shapes", "paint", [&] {
    const auto body_id = add_named_layer(QStringLiteral("PATCHY 64"));
    draw_shape(CanvasTool::Rectangle, rect(0.19, 0.755, 0.38, 0.115), QColor(198, 189, 168), at(0.010));
    draw_shape(CanvasTool::Rectangle, rect(0.19, 0.755, 0.38, 0.022), QColor(178, 169, 148), at(0.006));
    tighten_layer_to_opaque(body_id);
  });

  // 08: 60-key grid written directly (one undo snapshot; per-key invalidation
  // is the many-small-dirty-rects probe), then one bevel for the key look.
  step("08_c64_keys", "Keyboard key grid (60 keys, scattered rects)", "paint", [&] {
    keys_id_ = add_named_layer(QStringLiteral("Keys"));
    std::vector<std::pair<QRect, QColor>> cells;
    const auto grid = rect(0.205, 0.785, 0.35, 0.075);
    constexpr int kRows = 5;
    constexpr int kColumns = 12;
    const int key_width = grid.width() / kColumns;
    const int key_height = grid.height() / kRows;
    const int gap = std::max(1, key_width / 6);
    for (int row = 0; row < kRows; ++row) {
      for (int column = 0; column < kColumns; ++column) {
        const QRect key(grid.left() + column * key_width, grid.top() + row * key_height,
                        key_width - gap, key_height - gap);
        cells.emplace_back(key, QColor(58, 50, 44));
        cells.emplace_back(key.adjusted(gap / 2, gap / 3, -gap / 2, -key_height / 3), QColor(82, 72, 62));
      }
    }
    paint_cells_direct(keys_id_, cells, QStringLiteral("Key grid"));
    tighten_layer_to_opaque(keys_id_);
    LayerStyle style;
    LayerBevelEmboss bevel;
    bevel.enabled = true;
    bevel.size = static_cast<float>(std::max(1, at(0.0015)));
    style.bevels.push_back(bevel);
    set_layer_style_direct(keys_id_, style, QStringLiteral("Key bevel"));
  });

  // 09: rainbow badge + tiny label text.
  step("09_c64_badge", "Rainbow badge + label", "paint", [&] {
    const auto badge_layer = add_named_layer(QStringLiteral("Badge"));
    std::vector<std::pair<QRect, QColor>> stripes;
    const auto badge = rect(0.205, 0.762, 0.052, 0.012);
    const QColor rainbow[] = {QColor(208, 60, 50), QColor(224, 146, 54), QColor(226, 210, 84),
                              QColor(96, 168, 90), QColor(70, 110, 180)};
    const int stripe_height = std::max(1, badge.height() / 5);
    for (int i = 0; i < 5; ++i) {
      stripes.emplace_back(QRect(badge.left(), badge.top() + i * stripe_height, badge.width(), stripe_height),
                           rainbow[i]);
    }
    paint_cells_direct(badge_layer, stripes, QStringLiteral("Badge"));
    tighten_layer_to_opaque(badge_layer);
    const auto label_id = add_text_layer(pt(0.265, 0.760), QStringLiteral("PATCHY 64"),
                                         at(0.010), QColor(58, 52, 46));
    LayerStyle style;
    LayerStroke stroke;
    stroke.enabled = true;
    stroke.size = 1.0F;
    stroke.color = RgbColor{220, 214, 198};
    style.strokes.push_back(stroke);
    if (label_id != LayerId{}) {
      set_layer_style_direct(label_id, style, QStringLiteral("Badge label style"));
    }
  });

  // 10: mug + smoke-tip steam + twirl.
  step("10_mug_steam", "C2 mug + smoke-tip steam + twirl", "paint", [&] {
    mug_id_ = add_named_layer(QStringLiteral("Mug"));
    draw_shape(CanvasTool::Rectangle, rect(0.655, 0.665, 0.055, 0.055), QColor(206, 74, 62), at(0.008));
    draw_shape(CanvasTool::Ellipse, rect(0.703, 0.676, 0.020, 0.030), QColor(206, 74, 62));
    draw_shape(CanvasTool::Ellipse, rect(0.660, 0.667, 0.045, 0.012), QColor(64, 40, 34));
    steam_id_ = add_named_layer(QStringLiteral("Steam"));
    configure_brush(std::max(4, at(0.012)), 55, 60, default_tip_id_by_name(QStringLiteral("Smoke")),
                    kStressSeedBase + 10);
    set_foreground(QColor(236, 236, 232));
    drag_path({pt(0.678, 0.655), pt(0.670, 0.615), pt(0.684, 0.575), pt(0.674, 0.540)}, motion_steps(10));
    drag_path({pt(0.690, 0.650), pt(0.697, 0.605), pt(0.688, 0.565)}, motion_steps(10));
    apply_filter_direct(QStringLiteral("patchy.filters.twirl"), {{"angle", std::int64_t{35}}},
                        QStringLiteral("Steam twirl"));
    set_layer_opacity_percent(70);
    tighten_layer_to_opaque(mug_id_);
    tighten_layer_to_opaque(steam_id_);
    // Folders are part of the workflow: the desk props collect into a folder
    // as they are painted (step 11 inserts into it via its anchors).
    desk_folder_id_ = group_layers_into_folder({mug_id_, steam_id_}, QStringLiteral("Desk props"));
  });

  // 11: modem, floppy stack, wall poster with the RTsoft-logo tip stamp -
  // all inserted into the Desk props folder created in step 10.
  step("11_desk_props", "Modem + floppies + RTsoft poster", "paint", [&] {
    activate_layer_for_insert(steam_id_);
    modem_id_ = add_named_layer(QStringLiteral("Modem"));
    draw_shape(CanvasTool::Rectangle, rect(0.63, 0.617, 0.14, 0.028), QColor(88, 84, 80), at(0.005));
    std::vector<std::pair<QRect, QColor>> leds;
    for (int i = 0; i < 4; ++i) {
      leds.emplace_back(rect(0.643 + i * 0.016, 0.625, 0.007, 0.007),
                        i == 3 ? QColor(224, 82, 68) : QColor(96, 214, 118));
    }
    paint_cells_direct(modem_id_, leds, QStringLiteral("Modem LEDs"));

    floppy_id_ = add_named_layer(QStringLiteral("Floppies"));
    draw_shape(CanvasTool::Rectangle, rect(0.795, 0.740, 0.115, 0.095), QColor(52, 56, 84), at(0.004));
    draw_shape(CanvasTool::Rectangle, rect(0.812, 0.748, 0.080, 0.040), QColor(226, 222, 208));
    draw_shape(CanvasTool::Rectangle, rect(0.785, 0.845, 0.115, 0.020), QColor(84, 52, 60), at(0.004));
    const auto dink_label =
        add_text_layer(pt(0.818, 0.752), QStringLiteral("DINK\nSMALLWOOD"), at(0.008), QColor(60, 54, 48));
    const auto scroll_label =
        add_text_layer(pt(0.792, 0.847), QStringLiteral("DUNGEON SCROLL"), at(0.0065), QColor(226, 218, 202));
    // Bake the labels into the floppy layer so step 34's free transform rotates
    // the disks WITH their labels (separate text layers stayed axis-aligned
    // over the rotated disk in the first cut).
    if (floppy_id_ != LayerId{} && dink_label != LayerId{} && scroll_label != LayerId{}) {
      select_layer_rows({floppy_id_, dink_label, scroll_label});
      w.merge_down();
      if (w.layer_list_ != nullptr) {
        w.layer_list_->clearSelection();
      }
      tighten_layer_to_opaque(floppy_id_);
    }

    const auto poster_layer = add_named_layer(QStringLiteral("Poster"));
    draw_shape(CanvasTool::Rectangle, rect(0.685, 0.135, 0.225, 0.30), QColor(70, 54, 40), at(0.004));
    draw_shape(CanvasTool::Rectangle, rect(0.697, 0.148, 0.201, 0.274), QColor(228, 222, 206));
    configure_brush(std::max(8, at(0.075)), 100, 0, default_tip_id_by_name(QStringLiteral("RTsoft Logo")),
                    kStressSeedBase + 11);
    set_foreground(QColor(178, 60, 52));
    const auto stamp_point = doc_to_widget(pt(0.7975, 0.24));
    send_mouse(QEvent::MouseButtonPress, stamp_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(QEvent::MouseButtonRelease, stamp_point, Qt::LeftButton, Qt::NoButton);
    const auto poster_text_id = add_text_layer(pt(0.716, 0.360), QStringLiteral("EVERYBODY MAKES"),
                                               at(0.013), QColor(74, 66, 58));
    move_layer_into_folder(poster_text_id, desk_folder_id_, QStringLiteral("Move poster text"));
    tighten_layer_to_opaque(poster_layer);
    tighten_layer_to_opaque(modem_id_);
  });
}

void StressTestRunner::phase_text() {
  // 12: the boot screen text, four committed editor lifecycles, collected
  // into a Boot screen folder (steps 14/15/19/20 insert into it).
  step("12_boot_text", "Boot text (4 editor lifecycles)", "text", [&] {
    activate_top_layer();  // out of the Desk props folder
    set_monospace_text_family();
    const auto text_color = QColor(150, 138, 232);
    const int line_px = at(0.0125);
    const double x = 0.222;
    boot_text_ids_.clear();
    boot_text_ids_.push_back(add_text_layer(pt(x, 0.212), QStringLiteral("**** PATCHY 64 STRESS TEST ****"),
                                            line_px, text_color));
    boot_text_ids_.push_back(add_text_layer(
        pt(x, 0.242), QStringLiteral("64K RAM SYSTEM  38911 STRESS BYTES FREE"), line_px, text_color));
    boot_text_ids_.push_back(add_text_layer(pt(x, 0.272), QStringLiteral("READY."), line_px, text_color));
    load_line_point_ = pt(x, 0.302);
    load_line_id_ = add_text_layer(load_line_point_, QStringLiteral("LOAD \"RED CAVALIER\",8,1"), line_px,
                                   text_color);
    boot_text_ids_.push_back(load_line_id_);
    // The blinking block cursor, one direct cell.
    if (!boot_text_ids_.empty() && screen_id_ != LayerId{}) {
      paint_cells_direct(screen_id_, {{QRect(pt(x + 0.238, 0.301), QSize(at(0.010), at(0.014))),
                                       QColor(150, 138, 232)}},
                         QStringLiteral("Cursor block"));
    }
    boot_folder_id_ = group_layers_into_folder({boot_text_ids_.begin(), boot_text_ids_.end()},
                                               QStringLiteral("Boot screen"));
  });

  // 13: phosphor bloom on the boot text.
  step("13_boot_text_glow", "Outer glow on boot text", "styles", [&] {
    LayerStyle style;
    LayerOuterGlow glow;
    glow.enabled = true;
    glow.color = RgbColor{124, 112, 230};
    glow.opacity = 0.8F;
    glow.size = static_cast<float>(std::max(2, at(0.004)));
    style.outer_glows.push_back(glow);
    for (const auto id : boot_text_ids_) {
      if (id != LayerId{}) {
        set_layer_style_direct(id, style, QStringLiteral("Boot glow"));
      }
    }
  });

  // 14: the game parody, hundreds of scattered cells (into the Boot screen
  // folder).
  step("14_game_parody_pixels", "CAFE QUEST pixel-art scene (scattered cells)", "paint", [&] {
    activate_layer_for_insert(load_line_id_);
    game_pixels_id_ = add_named_layer(QStringLiteral("Cafe Quest"));
    const int cell = std::max(2, game_area_.width() / 46);
    const auto origin = game_area_.topLeft();
    std::vector<std::pair<QRect, QColor>> cells;
    const auto grid_rect = [&](int cx, int cy, int cw, int ch, QColor color) {
      cells.emplace_back(QRect(origin.x() + cx * cell, origin.y() + cy * cell, cw * cell, ch * cell), color);
    };
    // Ground.
    grid_rect(0, 14, 46, 3, QColor(92, 80, 64));
    // Cafe building with sign band, noren, window, door.
    grid_rect(24, 2, 20, 12, QColor(226, 218, 198));
    grid_rect(24, 0, 20, 2, QColor(44, 40, 38));
    grid_rect(24, 2, 20, 1, QColor(150, 46, 42));
    for (int strip = 0; strip < 3; ++strip) {
      grid_rect(26 + strip * 4, 3, 3, 3, QColor(56, 62, 128));
    }
    grid_rect(26, 8, 6, 4, QColor(140, 176, 208));
    grid_rect(37, 8, 4, 6, QColor(96, 66, 44));
    // Torii to the left.
    grid_rect(1, 3, 8, 1, QColor(188, 52, 44));
    grid_rect(2, 5, 6, 1, QColor(188, 52, 44));
    grid_rect(2, 3, 1, 11, QColor(168, 46, 40));
    grid_rect(7, 3, 1, 11, QColor(168, 46, 40));
    // Cast: Seth, Akiko, Ten-chan, Pon-chan, and one Dink duck.
    append_sprite_cells(cells, kSethSprite, 8, QPoint(12, 6), cell, origin);
    append_sprite_cells(cells, kAkikoSprite, 8, QPoint(18, 6), cell, origin);
    append_sprite_cells(cells, kCavalierSprite, 6, QPoint(10, 8 + 4), cell, origin, true);
    append_sprite_cells(cells, kCavalierSprite, 6, QPoint(21, 8 + 4), cell, origin);
    append_sprite_cells(cells, kDuckSprite, 4, QPoint(32, 10 + 3), cell, origin);
    paint_cells_direct(game_pixels_id_, cells, QStringLiteral("Cafe Quest scene"));
    tighten_layer_to_opaque(game_pixels_id_);
  });

  // 15: the game title + cast credit.
  step("15_game_title_text", "Game title + starring credit", "text", [&] {
    const auto title_id =
        add_text_layer(QPoint(game_area_.left() + at(0.004), game_area_.top() - at(0.030)),
                       QStringLiteral("LEGEND OF THE RED CAVALIER"), at(0.012),
                       QColor(238, 216, 130));
    game_title_id_ = title_id;
    game_credit_id_ = add_text_layer(QPoint(game_area_.left() + at(0.004), game_area_.top() - at(0.012)),
                                     QStringLiteral("C2 KYOTO PRESENTS - STARRING TEN-CHAN & PON-CHAN"),
                                     at(0.008), QColor(150, 138, 232));
    LayerStyle style;
    LayerStroke stroke;
    stroke.enabled = true;
    stroke.size = static_cast<float>(std::max(1, at(0.0015)));
    stroke.color = RgbColor{64, 30, 24};
    style.strokes.push_back(stroke);
    LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.distance = static_cast<float>(std::max(1, at(0.002)));
    shadow.size = static_cast<float>(std::max(2, at(0.003)));
    style.drop_shadows.push_back(shadow);
    if (title_id != LayerId{}) {
      set_layer_style_direct(title_id, style, QStringLiteral("Game title style"));
    }
    move_layer_into_folder(game_credit_id_, boot_folder_id_, QStringLiteral("Move credit"));
    move_layer_into_folder(game_title_id_, boot_folder_id_, QStringLiteral("Move title"));
  });

  // 16: sticky notes (one rotated via free transform).
  step("16_sticky_notes", "Sticky notes + free-transform rotate", "text", [&] {
    activate_top_layer();  // out of the Boot screen folder
    sticky_note_id_ = add_named_layer(QStringLiteral("Sticky note"));
    draw_shape(CanvasTool::Rectangle, rect(0.588, 0.30, 0.068, 0.062), QColor(240, 222, 110), at(0.003));
    (void)add_text_layer(pt(0.593, 0.312), QStringLiteral("TODO: make\ndirty rects\nfaster ;)"),
                         at(0.0085), QColor(88, 74, 40));
    const auto note2 = add_named_layer(QStringLiteral("Sticky note 2"));
    draw_shape(CanvasTool::Rectangle, rect(0.075, 0.745, 0.070, 0.058), QColor(214, 232, 120), at(0.003));
    (void)add_text_layer(pt(0.080, 0.757), QStringLiteral("feed Ten\n& Pon"),
                         at(0.009), QColor(70, 82, 36));
    // Rotate the second note slightly with a real free-transform commit.
    select_move_targets({note2});
    w.activate_tool(CanvasTool::Move);
    if (canvas()->begin_free_transform()) {
      const auto state = canvas()->transform_controls_state();
      const auto reference = state.has_value() ? state->reference_position : QPointF(pt(0.11, 0.774));
      canvas()->set_transform_controls_state(reference, 100.0, 100.0, -6.0);
      pump();
      canvas()->finish_free_transform();
    }
    tighten_layer_to_opaque(sticky_note_id_);
    tighten_layer_to_opaque(note2);
    LayerStyle style;
    LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.opacity = 0.45F;
    shadow.distance = static_cast<float>(std::max(1, at(0.003)));
    shadow.size = static_cast<float>(std::max(2, at(0.005)));
    style.drop_shadows.push_back(shadow);
    set_layer_style_direct(sticky_note_id_, style, QStringLiteral("Sticky shadow"));
  });

  // 17: the big styled title; deliberately the expensive_style move subject.
  step("17_title_text", "Big styled title (heavy fx)", "text", [&] {
    title_text_id_ = add_text_layer(pt(0.30, 0.026), QStringLiteral("PATCHY STRESS TEST"),
                                    at(0.032), QColor(234, 228, 212));
    LayerStyle style;
    LayerGradientFill gradient_fill;
    gradient_fill.enabled = true;
    gradient_fill.gradient.type = LayerStyleGradientType::Linear;
    gradient_fill.gradient.angle_degrees = 90.0F;
    gradient_fill.gradient.color_stops = {{0.0F, RgbColor{255, 214, 130}}, {1.0F, RgbColor{214, 108, 70}}};
    gradient_fill.gradient.alpha_stops = {{0.0F, 1.0F}, {1.0F, 1.0F}};
    style.gradient_fills.push_back(gradient_fill);
    LayerStroke stroke;
    stroke.enabled = true;
    stroke.size = static_cast<float>(std::max(2, at(0.0025)));
    stroke.color = RgbColor{46, 34, 30};
    style.strokes.push_back(stroke);
    LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.distance = static_cast<float>(std::max(2, at(0.004)));
    shadow.size = static_cast<float>(std::max(3, at(0.006)));
    style.drop_shadows.push_back(shadow);
    LayerBevelEmboss bevel;
    bevel.enabled = true;
    bevel.size = static_cast<float>(std::max(1, at(0.002)));
    style.bevels.push_back(bevel);
    if (title_text_id_ != LayerId{}) {
      set_layer_style_direct(title_text_id_, style, QStringLiteral("Title style"));
    }
  });

  // 18: re-enter an existing text layer, extend it, re-commit.
  step("18_text_reedit", "Re-edit the LOAD line", "text", [&] {
    if (load_line_id_ == LayerId{}) {
      return;
    }
    doc().set_active_layer(load_line_id_);
    w.add_text_at(load_line_point_);
    pump();
    auto* editor = canvas()->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    if (editor == nullptr) {
      report_.warnings.append(QStringLiteral("Re-edit did not open the text editor"));
      return;
    }
    editor->setPlainText(QStringLiteral("LOAD \"RED CAVALIER\",8,1\nSEARCHING FOR RED CAVALIER\nLOADING"));
    pump();
    w.finish_active_text_editor();
    pump();
  });
}

void StressTestRunner::phase_atmosphere() {
  // 19: scanlines, direct thin rows (many small commits probe #2), into the
  // Boot screen folder (step 20's bloom follows it there).
  step("19_scanlines", "CRT scanlines (thin rows)", "styles", [&] {
    activate_layer_for_insert(game_pixels_id_);
    const auto scanline_layer = add_named_layer(QStringLiteral("Scanlines"));
    std::vector<std::pair<QRect, QColor>> rows;
    const int line_count = std::max(8, 64 * size_ / 4096);
    const int spacing = std::max(2, screen_rect_.height() / line_count);
    const int thickness = std::max(1, spacing / 5);
    for (int y = screen_rect_.top(); y < screen_rect_.bottom(); y += spacing) {
      rows.emplace_back(QRect(screen_rect_.left(), y, screen_rect_.width(), thickness), QColor(10, 8, 40));
    }
    paint_cells_direct(scanline_layer, rows, QStringLiteral("Scanlines"));
    set_layer_opacity_percent(22);
  });

  // 20: screen bloom + glow style.
  step("20_screen_glow", "Screen bloom + outer glow", "styles", [&] {
    bloom_id_ = add_named_layer(QStringLiteral("Screen bloom"));
    set_radial_gradient({gradient_stop(0.0F, QColor(150, 140, 255, 255)),
                         gradient_stop(1.0F, QColor(150, 140, 255, 0))});
    drag(QPoint(screen_rect_.center()), QPoint(screen_rect_.right() + at(0.05), screen_rect_.bottom()),
         motion_steps(12));
    set_layer_blend_mode(BlendMode::Screen);
    set_layer_opacity_percent(30);
    LayerStyle style;
    LayerOuterGlow glow;
    glow.enabled = true;
    glow.color = RgbColor{120, 110, 235};
    glow.opacity = 0.5F;
    glow.size = static_cast<float>(std::max(3, at(0.008)));
    style.outer_glows.push_back(glow);
    if (screen_id_ != LayerId{}) {
      set_layer_style_direct(screen_id_, style, QStringLiteral("Screen glow"));
    }
  });

  // 21: confine the bloom with a gradient-painted layer mask.
  step("21_screen_mask", "Bloom layer mask (radial gradient)", "styles", [&] {
    if (bloom_id_ == LayerId{}) {
      return;
    }
    doc().set_active_layer(bloom_id_);
    w.add_layer_mask();
    w.set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Mask, false);
    set_radial_gradient({gradient_stop(0.0F, QColor(255, 255, 255)), gradient_stop(1.0F, QColor(0, 0, 0))});
    drag(QPoint(screen_rect_.center()),
         QPoint(screen_rect_.right() + at(0.09), screen_rect_.bottom() + at(0.06)), motion_steps(12));
    w.set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Content, false);
  });
}

void StressTestRunner::phase_filters() {
  // 22: blur restricted to an elliptical selection (partial invalidation).
  step("22_selection_blur", "Gaussian blur inside elliptical selection", "filters", [&] {
    if (desk_id_ != LayerId{}) {
      doc().set_active_layer(desk_id_);
    }
    w.activate_tool(CanvasTool::EllipticalMarquee);
    drag(pt(0.04, 0.72), pt(0.55, 0.99), motion_steps(10));
    apply_filter_direct(
        QStringLiteral("patchy.filters.gaussian_blur"),
        {{"radius", std::int64_t{std::clamp(size_ / 1024 * 3, 1, 12)}}}, QStringLiteral("Selection blur"));
    canvas()->clear_selection();
  }, canvas_megapixels() * 0.2);

  // 23: vignette on its own layer (a whole-scene overlay: at root, above the
  // area folders, not accidentally anchored inside one).
  step("23_vignette", "Vignette layer", "filters", [&] {
    activate_top_layer();
    add_named_layer(QStringLiteral("Vignette"));
    use_solid_fill_settings();
    w.fill_active_layer_with_color(QColor(255, 255, 255), MainWindow::tr("Vignette base"));
    apply_filter_direct(QStringLiteral("patchy.filters.vignette"), {{"strength", std::int64_t{70}}},
                        QStringLiteral("Vignette"));
    set_layer_blend_mode(BlendMode::Multiply);
    set_layer_opacity_percent(45);
  }, canvas_megapixels());

  // 24: film grain overlay.
  step("24_grain", "Analog grain layer (overlay)", "filters", [&] {
    add_named_layer(QStringLiteral("Grain"));
    use_solid_fill_settings();
    w.fill_active_layer_with_color(QColor(128, 128, 128), MainWindow::tr("Grain base"));
    apply_filter_direct(QStringLiteral("patchy.filters.film_grain"), {{"amount", std::int64_t{55}}},
                        QStringLiteral("Grain"));
    set_layer_blend_mode(BlendMode::Overlay);
    set_layer_opacity_percent(18);
  }, canvas_megapixels());
}

void StressTestRunner::phase_adjustments() {
  const auto add_adjustment = [&](const char* id, const char* label, AdjustmentSettings settings,
                                  const QString& name) {
    step(id, label, "adjustments", [&] {
      w.create_adjustment_layer(name, settings);
    }, canvas_megapixels());
  };

  AdjustmentSettings levels;
  levels.kind = AdjustmentKind::Levels;
  levels.levels.black_input = 8;
  levels.levels.white_input = 245;
  levels.levels.gamma_percent = 105;
  add_adjustment("25_adjust_levels", "Levels adjustment layer", levels, QStringLiteral("Levels"));

  AdjustmentSettings curves;
  curves.kind = AdjustmentKind::Curves;
  curves.curves = curves_adjustment_from_legacy_outputs(6, 132, 250);
  add_adjustment("26_adjust_curves", "Curves adjustment layer", curves, QStringLiteral("Curves"));

  AdjustmentSettings hue;
  hue.kind = AdjustmentKind::HueSaturation;
  hue.hue_saturation.hue_shift = 4;
  hue.hue_saturation.saturation_delta = 8;
  add_adjustment("27_adjust_hue_sat", "Hue/Saturation adjustment layer", hue, QStringLiteral("Hue/Saturation"));

  AdjustmentSettings balance;
  balance.kind = AdjustmentKind::ColorBalance;
  balance.color_balance.cyan_red = 8;
  balance.color_balance.yellow_blue = -10;
  add_adjustment("28_adjust_color_balance", "Color Balance adjustment layer", balance,
                 QStringLiteral("Color Balance"));
}

void StressTestRunner::phase_move_matrix() {
  w.activate_tool(CanvasTool::Move);
  canvas()->set_auto_select_layer(false);
  canvas()->set_show_transform_controls(false);

  // 29: small layer (tight bounds, drop shadow only): far below even the
  // styled 1 Mpx threshold, so the drag must stay on the live pixel-preview
  // path (expect move_outline_previews == 0 at every preset).
  fps_step("29_move_small_live", "Move drag: sticky note (live preview)", "move", [&] {
    select_move_targets({sticky_note_id_});
    // The path must return exactly to its start: the note's text is a separate
    // layer, so any net displacement strands the words off the note.
    drag_path({pt(0.622, 0.331), pt(0.615, 0.415), pt(0.622, 0.331)}, motion_steps(60) / 2);
  });

  // 30: the styled CRT screen (~2.1 Mpx at standard, glow + inner shadow):
  // crosses the styled 1 Mpx threshold while staying under the unstyled 4 Mpx
  // one, so the outline fallback that engages here is specifically the
  // expensive-style path. (The styled title is far too small for this - its
  // dirty area is ~0.26 Mpx at standard.)
  fps_step("30_move_styled_outline", "Move drag: styled CRT screen (styled outline)", "move", [&] {
    select_move_targets({screen_id_});
    drag_path({pt(0.38, 0.35), pt(0.40, 0.40), pt(0.38, 0.35)}, motion_steps(30));
  });

  // 31: full-canvas unstyled layer: the 4 Mpx threshold engages the outline.
  fps_step("31_move_large_outline", "Move drag: wall texture (16 Mpx outline)", "move", [&] {
    select_move_targets({wall_texture_id_});
    drag_path({pt(0.5, 0.5), pt(0.52, 0.55), pt(0.5, 0.5)}, motion_steps(30));
  });

  // 32: several tight unstyled props at once; their union stays under the
  // 4 Mpx threshold, so this pins the LIVE multi-layer drag path.
  fps_step("32_move_multi_layer", "Move drag: mug + floppies + modem", "move", [&] {
    select_move_targets({mug_id_, floppy_id_, modem_id_});
    drag_path({pt(0.70, 0.70), pt(0.72, 0.66), pt(0.70, 0.70)}, motion_steps(20));
  });

  // 33: keyboard nudges, immediate commits through move_active_layer_by.
  step("33_move_nudges", "Arrow-key nudges: key grid (24 commits)", "move", [&] {
    select_move_targets({keys_id_});
    const int cycles = 6;
    for (int i = 0; i < cycles; ++i) {
      send_key(Qt::Key_Right);
      send_key(Qt::Key_Down);
      send_key(Qt::Key_Left);
      send_key(Qt::Key_Up);
    }
  });
}

void StressTestRunner::phase_interact() {
  // 34: scale + rotate loop on the floppy stack, then the bicubic commit.
  step("34_free_transform", "Free transform: scale/rotate + commit", "interact", [&] {
    select_move_targets({floppy_id_});
    w.activate_tool(CanvasTool::Move);
    if (!canvas()->begin_free_transform()) {
      report_.warnings.append(QStringLiteral("Free transform did not start"));
      return;
    }
    const auto state = canvas()->transform_controls_state();
    const auto reference = state.has_value() ? state->reference_position : QPointF(pt(0.845, 0.79));
    const int iterations = std::max(5, motion_steps(15));
    for (int i = 1; i <= iterations; ++i) {
      canvas()->set_transform_controls_state(reference, 100.0 + i, 100.0 + i, static_cast<double>(i) / 2.0);
      pump();
    }
    canvas()->finish_free_transform();
  });

  // 35: zoom ladder + pan drag.
  fps_step("35_zoom_pan", "Zoom ladder + pan sweep", "interact", [&] {
    canvas()->fit_to_view();
    pump();
    canvas()->set_zoom_centered(1.0);
    pump();
    const auto center = QPointF(canvas()->rect().center());
    for (int i = 0; i < 8; ++i) {
      canvas()->zoom_at_widget_point(center, 1.19);
      pump();
    }
    const auto global_start = canvas()->mapToGlobal(canvas()->rect().center());
    if (canvas()->begin_pan_at_global_position(global_start)) {
      const int pan_steps = motion_steps(40);
      for (int i = 1; i <= pan_steps; ++i) {
        (void)canvas()->pan_to_global_position(global_start + QPoint(i * 6 - pan_steps * 3, (i % 7) * 4));
        pump();
      }
      (void)canvas()->end_pan();
    }
    canvas()->fit_to_view();
    pump();
  });
}

void StressTestRunner::phase_history() {
  const int depth =
      options_.preset == StressPreset::Smoke || options_.preset == StressPreset::Huge ? 3 : 5;

  // 36: brush dabs, each pushing a full-document undo snapshot.
  step("36_history_build", "Brush dabs pushing full undo snapshots", "history", [&] {
    if (game_pixels_id_ != LayerId{}) {
      doc().set_active_layer(game_pixels_id_);
    }
    configure_brush(std::max(2, at(0.004)), 100, 0, builtin_round_brush_tip_id(), kStressSeedBase + 36);
    set_foreground(QColor(238, 216, 130));
    for (int i = 0; i < depth; ++i) {
      const auto point = doc_to_widget(QPoint(game_area_.left() + at(0.01) + i * at(0.006),
                                              game_area_.top() + at(0.004)));
      send_mouse(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
      send_mouse(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    }
  }, -1.0, /*trim_history=*/false);

  // 37: walk the history both ways; full Document restore + recomposite each.
  step("37_undo_redo", "Undo/redo chain (full restores)", "history", [&] {
    for (int i = 0; i < depth; ++i) {
      w.undo();
      settle();
    }
    for (int i = 0; i < depth; ++i) {
      w.redo();
      settle();
    }
  }, -1.0, /*trim_history=*/false);
  trim_undo(2);
}

void StressTestRunner::phase_io() {
  // 38: flatten everything to a new layer, then put the stack back.
  step("38_merge_visible", "Merge visible to new layer (+undo)", "io", [&] {
    w.merge_visible_to_new_layer();
    settle();
    w.undo();
  }, canvas_megapixels());

  const auto psd_path = QDir(report_dir_).filePath(QStringLiteral("stress-scene.psd"));

  // 39: layered PSD write (text layers, masks, styles included).
  step("39_psd_save", "Save layered PSD", "io", [&] {
    if (!w.save_document_to_path(psd_path)) {
      report_.warnings.append(QStringLiteral("PSD save failed: %1").arg(psd_path));
    }
  }, canvas_megapixels());

  // 40: read it back into a second tab, then close that tab.
  step("40_psd_reload", "Reload the PSD (decode + first composite)", "io", [&] {
    w.open_document_path(psd_path);
    pump();
  }, canvas_megapixels());
  if (w.document_tabs_ != nullptr && w.document_tabs_->count() > 1) {
    w.set_session_saved(w.session());
    (void)w.close_document_tab(w.document_tabs_->currentIndex());
    pump();
  }

  // 41: two extra documents + tab switching.
  step("41_multi_document", "Two extra documents + 6 tab switches", "io", [&] {
    const auto half = std::max(256, size_ / 2);
    w.reset_document(half, half, QColor(96, 108, 96), MainWindow::tr("Stress extra 1"));
    w.set_session_saved(w.session());
    w.reset_document(half, half, QColor(108, 96, 108), MainWindow::tr("Stress extra 2"));
    w.set_session_saved(w.session());
    if (w.document_tabs_ != nullptr) {
      const auto count = w.document_tabs_->count();
      for (int i = 0; i < 6; ++i) {
        w.activate_document_tab(i % count);
        settle();
      }
      while (w.document_tabs_->count() > 1) {
        w.set_session_saved(w.session());
        (void)w.close_document_tab(w.document_tabs_->count() - 1);
        pump();
      }
      w.activate_document_tab(0);
      pump();
    }
  });
}

// ---------------------------------------------------------------------------
// Scene 2: the "C2 ARCADE" corner, built on a canvas widened by 50% with the
// features added after the original harness - vector shape layers, booleans,
// custom shapes, appearance edits, path roundtrips, the newer filters, smart
// objects + smart filters, and warp. Coordinates keep using the at()/rect()
// fractions of the PRESET edge, so scene 2 spans x = 1.0 .. 1.5. These steps
// run after 41 and before 42/43 so the final composite captures both scenes
// while the move matrix and history steps keep their original inputs.

void StressTestRunner::phase_arcade_vector() {
  // 44: widen the canvas (anchor Left keeps scene 1 in place) - the
  // geometry-op path that transforms vector and path data alongside pixels,
  // plus the first composite at the new width. Mirrors resize_canvas_dialog's
  // commit tail without the dialog.
  step("44_canvas_expand", "Canvas Size: widen 50% for the arcade corner", "setup", [&] {
    auto& document_ref = doc();
    w.push_undo_snapshot(QStringLiteral("Canvas size"));
    resize_canvas_and_layers(document_ref, size_ * 3 / 2, size_, CanvasAnchor::Left,
                             edit_color(QColor(24, 22, 30)));
    canvas()->clear_selection();
    canvas()->set_document(&document_ref);
    w.refresh_layer_list();
    w.refresh_layer_controls();
    w.refresh_document_info();
    canvas()->fit_to_view();
    pump();
  }, canvas_megapixels() * 1.5);

  begin_vector_shape_phase();

  // 45: the cabinet, drawn through the real Shape-mode drag pipeline (live
  // vector preview per mouse move, options-bar appearance commit, bake).
  // Folders are built as the corner grows: the wall starts the "C2 Arcade"
  // folder and the cabinet body starts its nested "Arcade cabinet" subfolder.
  step("45_arcade_shapes", "Vector arcade cabinet (Shape-mode drags)", "vector", [&] {
    // Raster backdrop for the new strip first (Pixels-mode shapes).
    activate_top_layer();
    arcade_wall_id_ = add_named_layer(QStringLiteral("Arcade wall"));
    draw_shape(CanvasTool::Rectangle, rect(0.995, 0.0, 0.51, 1.0), QColor(38, 30, 52));
    draw_shape(CanvasTool::Rectangle, rect(0.995, 0.70, 0.51, 0.30), QColor(52, 38, 40));
    (void)group_layers_into_folder({arcade_wall_id_}, QStringLiteral("C2 Arcade"));

    activate_layer_for_insert(arcade_wall_id_);
    cabinet_id_ = draw_vector_shape(CanvasTool::Rectangle, rect(1.07, 0.25, 0.36, 0.55),
                                    gradient_vector_fill(QColor(150, 48, 60), QColor(70, 24, 40), 90.0F),
                                    true, std::max(2.0, static_cast<double>(at(0.003))),
                                    QColor(30, 16, 22), QStringLiteral("Cabinet"), at(0.01));
    cabinet_folder_id_ = group_layers_into_folder({cabinet_id_}, QStringLiteral("Arcade cabinet"));
    activate_layer_for_insert(cabinet_id_);
    (void)draw_vector_shape(CanvasTool::Rectangle, rect(1.10, 0.28, 0.30, 0.08),
                            solid_vector_fill(QColor(34, 30, 56)), true,
                            std::max(1.0, static_cast<double>(at(0.0015))), QColor(226, 208, 130),
                            QStringLiteral("Marquee"), at(0.004));
    arcade_screen_rect_ = rect(1.11, 0.385, 0.28, 0.19);
    cabinet_screen_id_ = draw_vector_shape(CanvasTool::Rectangle, arcade_screen_rect_,
                                           solid_vector_fill(QColor(16, 14, 24)), true,
                                           std::max(1.0, static_cast<double>(at(0.002))),
                                           QColor(90, 84, 110), QStringLiteral("Arcade screen"),
                                           at(0.003));
    (void)draw_vector_shape(CanvasTool::Rectangle, rect(1.09, 0.60, 0.32, 0.055),
                            gradient_vector_fill(QColor(96, 92, 104), QColor(58, 54, 64), 90.0F),
                            false, 1.0, QColor(0, 0, 0), QStringLiteral("Control deck"));
    (void)draw_vector_shape(CanvasTool::Ellipse, rect(1.16, 0.612, 0.028, 0.028),
                            solid_vector_fill(QColor(214, 64, 58)), true, 1.0, QColor(40, 20, 20),
                            QString());
    (void)draw_vector_shape(CanvasTool::Ellipse, rect(1.21, 0.612, 0.028, 0.028),
                            solid_vector_fill(QColor(232, 202, 64)), true, 1.0, QColor(60, 48, 18),
                            QString());
  });

  // 46: speaker grille - one shape layer, then subtract-combine drags punch
  // the vent slots (a sequential-boolean re-rasterize per commit).
  step("46_shape_boolean", "Speaker grille (subtract combine drags)", "vector", [&] {
    const auto grille_fill = solid_vector_fill(QColor(24, 20, 34));
    grille_id_ = draw_vector_shape(CanvasTool::Rectangle, rect(1.12, 0.675, 0.26, 0.05), grille_fill,
                                   true, 1.0, QColor(120, 110, 140), QStringLiteral("Speaker grille"),
                                   at(0.002));
    w.current_vector_combine_index_ = 2;  // Subtract from the active shape layer.
    for (int i = 0; i < 4; ++i) {
      (void)draw_vector_shape(CanvasTool::Rectangle, rect(1.135 + i * 0.06, 0.683, 0.045, 0.008),
                              grille_fill, true, 1.0, QColor(120, 110, 140), QString());
      (void)draw_vector_shape(CanvasTool::Rectangle, rect(1.135 + i * 0.06, 0.698, 0.045, 0.008),
                              grille_fill, true, 1.0, QColor(120, 110, 140), QString());
    }
    w.current_vector_combine_index_ = 0;
  });

  // 47: neon tube - a programmatic smooth open path, stroke-only appearance,
  // plus an outer glow style (stroker + EDT glow over a curved band). Starts
  // the "Arcade signage" subfolder beside the cabinet one.
  step("47_neon_pen_glow", "Neon sign path + outer glow", "vector", [&] {
    activate_layer_for_insert(cabinet_folder_id_);
    set_vector_appearance(no_vector_fill(), true, std::max(2.0, static_cast<double>(at(0.005))),
                          QColor(96, 232, 220));
    PathSubpath tube;
    tube.closed = false;
    constexpr double kLeft = 1.09;
    constexpr double kRight = 1.41;
    constexpr int kAnchors = 7;
    for (int i = 0; i < kAnchors; ++i) {
      const double t = static_cast<double>(i) / (kAnchors - 1);
      PathAnchor anchor;
      anchor.anchor_x = at(kLeft + (kRight - kLeft) * t);
      anchor.anchor_y = at(0.115 + ((i % 2) == 0 ? -0.018 : 0.018));
      const double handle = at((kRight - kLeft) / (kAnchors * 2.5));
      anchor.in_x = anchor.anchor_x - handle;
      anchor.in_y = anchor.anchor_y;
      anchor.out_x = anchor.anchor_x + handle;
      anchor.out_y = anchor.anchor_y;
      anchor.smooth = true;
      tube.anchors.push_back(anchor);
    }
    w.create_or_extend_shape_layer({tube}, std::nullopt, QStringLiteral("Neon %1"));
    rename_active_layer_to(QStringLiteral("Neon sign"));
    neon_id_ = doc().active_layer_id().value_or(LayerId{});
    signage_folder_id_ = group_layers_into_folder({neon_id_}, QStringLiteral("Arcade signage"));
    LayerStyle style;
    LayerOuterGlow glow;
    glow.enabled = true;
    glow.color = RgbColor{80, 220, 210};
    glow.opacity = 0.85F;
    glow.size = static_cast<float>(std::max(3, at(0.008)));
    style.outer_glows.push_back(glow);
    if (neon_id_ != LayerId{}) {
      set_layer_style_direct(neon_id_, style, QStringLiteral("Neon glow"));
    }
  });

  // 48: a star polygon, library custom-shape stamps, and an arrowed line -
  // all plain-path shape layers through the real tool drags.
  step("48_custom_shapes", "Custom shape stamps + star + arrow", "vector", [&] {
    activate_layer_for_insert(neon_id_);  // into the signage folder
    // Appearance mirrors always AFTER activate_tool (the tool switch syncs
    // them from the active shape layer).
    w.activate_tool(CanvasTool::Polygon);
    set_vector_appearance(solid_vector_fill(QColor(238, 216, 130)), true, 1.0, QColor(90, 70, 30));
    canvas()->set_polygon_sides(5);
    canvas()->set_polygon_star_inset(55);
    // Center-out drag: the first vertex tracks the cursor.
    drag(pt(1.045, 0.155), pt(1.045, 0.155) + QPoint(at(0.022), -at(0.022)), 4);
    rename_active_layer_to(QStringLiteral("Star"));

    const auto& entries = w.custom_shape_library().entries();
    if (entries.empty()) {
      report_.warnings.append(QStringLiteral("Custom shape library is empty"));
    } else {
      const auto* pick = &entries.front();
      for (const auto& entry : entries) {
        if (entry.name.contains(QStringLiteral("arrow"), Qt::CaseInsensitive)) {
          pick = &entry;
          break;
        }
      }
      w.activate_tool(CanvasTool::CustomShape);
      set_vector_appearance(solid_vector_fill(QColor(226, 130, 60)));
      canvas()->set_custom_shape_path(std::make_shared<VectorPath>(pick->path));
      const auto stamp_a = rect(1.30, 0.13, 0.05, 0.05);
      drag(stamp_a.topLeft(), stamp_a.bottomRight(), 4);
      set_vector_appearance(solid_vector_fill(QColor(120, 200, 120)));
      canvas()->set_custom_shape_path(std::make_shared<VectorPath>(entries.front().path));
      const auto stamp_b = rect(1.36, 0.13, 0.05, 0.05);
      drag(stamp_b.topLeft(), stamp_b.bottomRight(), 4);
    }

    w.activate_tool(CanvasTool::Line);
    canvas()->set_vector_tool_mode(VectorToolMode::Shape);
    set_vector_appearance(solid_vector_fill(QColor(226, 208, 130)));
    w.current_vector_line_weight_ = std::max(2, at(0.003));
    w.current_line_arrow_end_ = true;
    drag(pt(1.035, 0.205), pt(1.10, 0.26), 4);
    w.current_line_arrow_end_ = false;
    rename_active_layer_to(QStringLiteral("Arrow"));
    arrow_id_ = doc().active_layer_id().value_or(LayerId{});
  });

  // 49: live appearance edit on the cabinet - the options-bar apply path
  // (model swap + full re-bake of the styled body).
  step("49_shape_restyle", "Cabinet appearance edit (re-bake)", "vector", [&] {
    if (cabinet_id_ == LayerId{}) {
      return;
    }
    w.activate_tool(CanvasTool::Rectangle);
    canvas()->set_vector_tool_mode(VectorToolMode::Shape);
    doc().set_active_layer(cabinet_id_);
    w.refresh_layer_list();
    w.refresh_layer_controls();
    pump();
    // Set the mirrors AFTER the refresh: activating the shape layer syncs
    // them from its current appearance.
    set_vector_appearance(gradient_vector_fill(QColor(64, 96, 168), QColor(28, 34, 76), 90.0F), true,
                          std::max(2.0, static_cast<double>(at(0.004))), QColor(16, 20, 40));
    if (!w.apply_options_bar_appearance_to_active_shape()) {
      report_.warnings.append(QStringLiteral("Shape appearance edit did not apply"));
    }
  });

  // 50: selection -> work path (trace + curve fit) -> selection again (path
  // coverage rasterize). Mirrors the two Paths-panel footer commands' commit
  // tails without their parameter dialogs.
  step("50_paths_roundtrip", "Selection to work path and back", "vector", [&] {
    w.activate_tool(CanvasTool::EllipticalMarquee);
    drag(arcade_screen_rect_.topLeft(), arcade_screen_rect_.bottomRight(), motion_steps(8));
    if (!canvas()->has_selection()) {
      report_.warnings.append(QStringLiteral("Paths step: selection did not commit"));
      return;
    }
    const auto loops = trace_selection_outlines(canvas()->selected_document_region());
    VectorPath fitted;
    for (const auto& loop : loops) {
      std::vector<FitPoint> points;
      points.reserve(static_cast<std::size_t>(loop.points.size()));
      for (const auto& point : loop.points) {
        points.push_back(FitPoint{point.x(), point.y()});
      }
      auto subpath = fit_closed_loop(points, 2.0);
      if (subpath.anchors.size() < 2) {
        continue;
      }
      subpath.op = loop_signed_area(points) >= 0.0 ? PathCombineOp::Add : PathCombineOp::Subtract;
      subpath.shape_group = static_cast<std::int32_t>(fitted.subpaths.size());
      fitted.subpaths.push_back(std::move(subpath));
    }
    if (fitted.subpaths.empty()) {
      report_.warnings.append(QStringLiteral("Paths step: selection trace produced no subpaths"));
      return;
    }
    w.push_undo_snapshot(QStringLiteral("Make work path"));
    auto& document_ref = doc();
    DocumentPathId work_id = 0;
    if (auto* work = document_ref.work_path(); work != nullptr) {
      work->set_path(std::move(fitted));
      work_id = work->id();
      canvas()->clear_path_edit_selection();
    } else {
      DocumentPath created(document_ref.allocate_path_id(), QStringLiteral("Work Path").toStdString(),
                           DocumentPathKind::Work, std::move(fitted));
      created.mark_dirty();
      work_id = document_ref.add_path(std::move(created)).id();
    }
    w.target_document_path_row(work_id);
    w.load_path_as_selection(static_cast<int>(PathsPanel::RowKind::WorkPath), work_id);
    pump();
    canvas()->clear_selection();
    // Dismiss the targeting: a targeted row draws the path outline with EVERY
    // tool, which would leave a stray ellipse overlay on the finished scene.
    w.handle_paths_panel_deselect();
    pump();
  });

  end_vector_shape_phase();
}

void StressTestRunner::phase_arcade_filters() {
  // 51: the patent design-around implementations with distinct perf profiles:
  // surface blur (no value histogram) and median (single-pixel histogram
  // updates), selection-restricted to a raster glass pane over the screen.
  step("51_new_blurs", "Surface blur + median (arcade glass)", "filters", [&] {
    activate_layer_for_insert(grille_id_);  // top of the cabinet folder
    const auto glass_id = add_named_layer(QStringLiteral("Arcade glass"));
    arcade_glass_id_ = glass_id;
    use_solid_fill_settings();
    const auto pane = arcade_screen_rect_.adjusted(at(0.004), at(0.004), -at(0.004), -at(0.004));
    draw_shape(CanvasTool::Rectangle, pane, QColor(120, 132, 150));
    w.activate_tool(CanvasTool::Marquee);
    drag(pane.topLeft(), pane.bottomRight(), 4);
    apply_filter_direct(QStringLiteral("patchy.filters.clouds"),
                        {{"scale", std::int64_t{std::clamp(size_ / 64, 8, 128)}},
                         {"detail", std::int64_t{4}},
                         {"contrast", std::int64_t{40}},
                         {"seed", std::int64_t{51}}},
                        QStringLiteral("Glass texture"));
    apply_filter_direct(QStringLiteral("patchy.filters.surface_blur"),
                        {{"radius", static_cast<double>(std::clamp(size_ / 512, 2, 12))},
                         {"threshold", std::int64_t{24}}},
                        QStringLiteral("Surface blur"));
    apply_filter_direct(QStringLiteral("patchy.filters.median"),
                        {{"radius", static_cast<double>(std::clamp(size_ / 1024, 1, 6))}},
                        QStringLiteral("Median"));
    canvas()->clear_selection();
    set_layer_blend_mode(BlendMode::Screen);
    set_layer_opacity_percent(18);
    tighten_layer_to_opaque(glass_id);
  }, canvas_megapixels() * 0.05);

  // 57: the game running on the arcade screen - sprite cells + score text
  // UNDER the glass (anchored above the screen shape), then color halftone
  // turns the glass texture into a CRT dot mask.
  step("57_arcade_screen_game", "Arcade screen game + CRT halftone", "paint", [&] {
    activate_layer_for_insert(cabinet_screen_id_);
    const auto game_id = add_named_layer(QStringLiteral("Arcade game"));
    const auto play = arcade_screen_rect_.adjusted(at(0.012), at(0.012), -at(0.012), -at(0.012));
    const int cell = std::max(2, play.width() / 40);
    const auto origin = play.topLeft();
    std::vector<std::pair<QRect, QColor>> cells;
    const auto grid_rect = [&](int cx, int cy, int cw, int ch, QColor color) {
      cells.emplace_back(QRect(origin.x() + cx * cell, origin.y() + cy * cell, cw * cell, ch * cell), color);
    };
    // Ground, platforms, and a goal flag.
    grid_rect(0, 16, 40, 2, QColor(70, 52, 96));
    grid_rect(8, 12, 7, 1, QColor(96, 74, 130));
    grid_rect(20, 9, 7, 1, QColor(96, 74, 130));
    grid_rect(34, 5, 1, 6, QColor(150, 138, 232));
    grid_rect(35, 5, 3, 2, QColor(226, 84, 84));
    // The cavalier hero chasing a duck across the ground.
    append_sprite_cells(cells, kCavalierSprite, 6, QPoint(4, 10), cell, origin);
    append_sprite_cells(cells, kDuckSprite, 4, QPoint(14, 12), cell, origin, true);
    paint_cells_direct(game_id, cells, QStringLiteral("Arcade game"));
    tighten_layer_to_opaque(game_id);
    arcade_game_id_ = game_id;
    const auto score_id = add_text_layer(QPoint(play.left() + at(0.004), play.top() + at(0.004)),
                                         QStringLiteral("SCORE 9999"), at(0.011), QColor(150, 138, 232));
    move_layer_into_folder(score_id, cabinet_folder_id_, QStringLiteral("Move score"));
    // CRT dot mask: halftone the glass pane over the game.
    if (arcade_glass_id_ != LayerId{}) {
      activate_layer_for_insert(arcade_glass_id_);
      apply_filter_direct(QStringLiteral("patchy.filters.color_halftone"),
                          {{"cell_size", std::int64_t{std::clamp(size_ / 256, 4, 16)}},
                           {"intensity", std::int64_t{70}},
                           {"contrast", std::int64_t{50}}},
                          QStringLiteral("CRT halftone"));
    }
  }, canvas_megapixels() * 0.05);

  // 58: marquee + neon sign lettering with glow styles (arcade lighting),
  // each into its area's folder.
  step("58_marquee_text", "Marquee + neon sign text (glow styles)", "text", [&] {
    const auto marquee_text = add_text_layer(pt(1.125, 0.295), QStringLiteral("RED CAVALIER II"),
                                             at(0.026), QColor(238, 216, 130));
    move_layer_into_folder(marquee_text, cabinet_folder_id_, QStringLiteral("Move marquee text"));
    if (marquee_text != LayerId{}) {
      LayerStyle style;
      LayerOuterGlow glow;
      glow.enabled = true;
      glow.color = RgbColor{226, 130, 44};
      glow.opacity = 0.85F;
      glow.size = static_cast<float>(std::max(2, at(0.006)));
      style.outer_glows.push_back(glow);
      set_layer_style_direct(marquee_text, style, QStringLiteral("Marquee glow"));
    }
    const auto neon_text = add_text_layer(pt(1.16, 0.052), QStringLiteral("C2 ARCADE"),
                                          at(0.024), QColor(96, 232, 220));
    move_layer_into_folder(neon_text, signage_folder_id_, QStringLiteral("Move neon text"));
    if (neon_text != LayerId{}) {
      LayerStyle style;
      LayerOuterGlow glow;
      glow.enabled = true;
      glow.color = RgbColor{80, 220, 210};
      glow.opacity = 0.9F;
      glow.size = static_cast<float>(std::max(2, at(0.007)));
      style.outer_glows.push_back(glow);
      set_layer_style_direct(neon_text, style, QStringLiteral("Neon text glow"));
    }
  });

  // 52: unsharp mask over the arcade wall + motion-blurred speed lines
  // trailing the game's duck, inside the screen (under the glass).
  step("52_sharpen_motion", "Unsharp mask + in-game motion blur", "filters", [&] {
    if (arcade_wall_id_ != LayerId{}) {
      doc().set_active_layer(arcade_wall_id_);
    }
    w.activate_tool(CanvasTool::Marquee);
    drag(pt(1.0, 0.0), pt(1.5, 0.7), motion_steps(6));
    apply_filter_direct(QStringLiteral("patchy.filters.unsharp_mask"),
                        {{"amount", std::int64_t{120}},
                         {"radius", static_cast<double>(std::clamp(size_ / 1024, 1, 8))},
                         {"threshold", std::int64_t{4}}},
                        QStringLiteral("Unsharp mask"));
    canvas()->clear_selection();
    activate_layer_for_insert(arcade_game_id_);
    const auto lines_id = add_named_layer(QStringLiteral("Game speed lines"));
    const auto play = arcade_screen_rect_.adjusted(at(0.012), at(0.012), -at(0.012), -at(0.012));
    const int cell = std::max(2, play.width() / 40);
    const auto origin = play.topLeft();
    std::vector<std::pair<QRect, QColor>> cells;
    for (int i = 0; i < 5; ++i) {
      cells.emplace_back(QRect(origin.x() + (2 + (i % 2) * 3) * cell, origin.y() + (12 + i) * cell,
                               (5 + (i % 3) * 2) * cell, std::max(1, cell / 2)),
                         QColor(226, 222, 240));
    }
    paint_cells_direct(lines_id, cells, QStringLiteral("Game speed lines"));
    // Tighten BEFORE the blur so the filter runs over the lines' hull, not
    // the full-canvas layer buffer.
    tighten_layer_to_opaque(lines_id);
    apply_filter_direct(QStringLiteral("patchy.filters.motion_blur"),
                        {{"angle", std::int64_t{0}},
                         {"distance", std::int64_t{std::clamp(size_ / 256, 4, 32)}}},
                        QStringLiteral("Motion blur"));
    set_layer_opacity_percent(70);
  }, canvas_megapixels() * 0.35);

  // 53: a shrink-wrapped game flyer on the wall left of the cabinet - frame,
  // paper, title bar, duck art - with plastic wrap for the glossy sheen.
  step("53_plastic_wrap", "Plastic wrap foil poster", "filters", [&] {
    activate_layer_for_insert(arrow_id_);  // wall decor lives with the signage
    arcade_poster_id_ = add_named_layer(QStringLiteral("Foil poster"));
    draw_shape(CanvasTool::Rectangle, rect(1.002, 0.30, 0.062, 0.115), QColor(46, 34, 60), at(0.002));
    draw_shape(CanvasTool::Rectangle, rect(1.006, 0.306, 0.054, 0.103), QColor(214, 202, 176));
    std::vector<std::pair<QRect, QColor>> art;
    art.emplace_back(rect(1.006, 0.306, 0.054, 0.016), QColor(196, 62, 74));
    append_sprite_cells(art, kDuckSprite, 4, QPoint(0, 0), std::max(2, at(0.008)), pt(1.017, 0.34));
    art.emplace_back(rect(1.012, 0.386, 0.042, 0.007), QColor(96, 66, 44));
    paint_cells_direct(arcade_poster_id_, art, QStringLiteral("Poster art"));
    tighten_layer_to_opaque(arcade_poster_id_);
    apply_filter_direct(QStringLiteral("patchy.filters.plastic_wrap"),
                        {{"highlight_strength", std::int64_t{7}},
                         {"detail", std::int64_t{9}},
                         {"smoothness", std::int64_t{5}}},
                        QStringLiteral("Plastic wrap"));
  }, canvas_megapixels() * 0.016);

  // 59: more filter families - dust & scratches + vintage fade age the foil
  // poster, and a radial-blur spin glint on a coin pickup above the game's
  // top platform (under the glass, like everything on the screen).
  step("59_poster_age_coin", "Poster aging + radial-blur coin glint", "filters", [&] {
    if (arcade_poster_id_ != LayerId{}) {
      activate_layer_for_insert(arcade_poster_id_);
      apply_filter_direct(QStringLiteral("patchy.filters.dust_and_scratches"),
                          {{"radius", std::int64_t{2}}, {"threshold", std::int64_t{12}}},
                          QStringLiteral("Dust & scratches"));
      apply_filter_direct(QStringLiteral("patchy.filters.vintage_fade"),
                          {{"amount", std::int64_t{70}}}, QStringLiteral("Vintage fade"));
    }
    activate_layer_for_insert(arcade_game_id_);
    const auto coin_id = add_named_layer(QStringLiteral("Coin"));
    const auto play = arcade_screen_rect_.adjusted(at(0.012), at(0.012), -at(0.012), -at(0.012));
    const int cell = std::max(2, play.width() / 40);
    const auto origin = play.topLeft();
    draw_shape(CanvasTool::Ellipse,
               QRect(origin.x() + 22 * cell, origin.y() + 6 * cell, 2 * cell, 2 * cell),
               QColor(238, 200, 74));
    std::vector<std::pair<QRect, QColor>> sparkle;
    sparkle.emplace_back(QRect(origin.x() + 21 * cell, origin.y() + 7 * cell, 4 * cell,
                               std::max(1, cell / 2)),
                         QColor(255, 244, 190));
    sparkle.emplace_back(QRect(origin.x() + 23 * cell - cell / 4, origin.y() + 5 * cell,
                               std::max(1, cell / 2), 4 * cell),
                         QColor(255, 244, 190));
    paint_cells_direct(coin_id, sparkle, QStringLiteral("Coin sparkle"));
    tighten_layer_to_opaque(coin_id);
    apply_filter_direct(QStringLiteral("patchy.filters.radial_blur"),
                        {{"amount", std::int64_t{55}}, {"samples", std::int64_t{20}}},
                        QStringLiteral("Coin glint"));
  }, canvas_megapixels() * 0.01);
}

void StressTestRunner::phase_smart_and_warp() {
  // 54: smart object end to end - convert the floppy stack, apply a Gaussian
  // smart-filter stack (SoLd descriptor regeneration + filtered rebake), then
  // rasterize so the later document-geometry steps stay legal (geometry ops
  // refuse while a smart object exists).
  step("54_smart_object_filter", "Smart object + Gaussian smart filter", "smart", [&] {
    if (floppy_id_ == LayerId{}) {
      return;
    }
    select_move_targets({floppy_id_});
    select_layer_rows({floppy_id_});
    w.convert_to_smart_object();
    pump();
    const auto smart_id = doc().active_layer_id().value_or(LayerId{});
    const auto* smart_layer = smart_id != LayerId{} ? std::as_const(doc()).find_layer(smart_id) : nullptr;
    if (smart_layer == nullptr || !layer_is_smart_object(*smart_layer)) {
      report_.warnings.append(QStringLiteral("Smart object conversion failed"));
      return;
    }
    SmartFilterStack stack;
    stack.support = SmartFilterStackSupport::Supported;
    stack.mask.linked = false;
    SmartFilterEntry entry;
    entry.kind = SmartFilterKind::GaussianBlur;
    entry.native_name = "Gaussian Blur...";
    entry.native_class_id = "GsnB";
    entry.native_filter_id = 0x47736e42U;
    entry.parameters = GaussianBlurSmartFilter{static_cast<double>(std::clamp(size_ / 1024, 1, 6))};
    stack.entries.push_back(std::move(entry));
    if (!w.commit_smart_filter_stack_edit(smart_id, std::move(stack), {std::nullopt},
                                          QStringLiteral("Smart filter"),
                                          QStringLiteral("Applied Gaussian Blur smart filter"))) {
      report_.warnings.append(QStringLiteral("Smart filter commit failed"));
    }
    settle();
    w.rasterize_active_layers();
  });

  // 55: warp transform - flag-preset cage, one handle drag, bicubic bake -
  // on a banner with its text baked in (warp refuses live text layers).
  step("55_warp_banner", "Warp banner (flag preset + bake)", "interact", [&] {
    activate_layer_for_insert(arrow_id_);  // into the signage folder
    const auto banner_id = add_named_layer(QStringLiteral("Banner"));
    draw_shape(CanvasTool::Rectangle, rect(1.10, 0.158, 0.30, 0.064), QColor(206, 74, 62), at(0.004));
    const auto banner_text = add_text_layer(pt(1.117, 0.171), QStringLiteral("GRAND OPENING"),
                                            at(0.024), QColor(244, 232, 200));
    if (banner_id != LayerId{} && banner_text != LayerId{}) {
      select_layer_rows({banner_id, banner_text});
      w.merge_down();
      if (w.layer_list_ != nullptr) {
        w.layer_list_->clearSelection();
      }
    }
    tighten_layer_to_opaque(banner_id);
    select_move_targets({banner_id});
    w.activate_tool(CanvasTool::Move);
    if (!canvas()->begin_warp_transform()) {
      report_.warnings.append(QStringLiteral("Warp transform did not start"));
      return;
    }
    canvas()->apply_warp_style_preset(QStringLiteral("warpFlag"), 28.0);
    pump();
    if (canvas()->warp_handle_count() > 5) {
      const auto handle = canvas()->warp_handle_document_position(5);
      canvas()->set_warp_handle_document_position(5, handle + QPointF(0.0, -at(0.01)));
      pump();
    }
    canvas()->finish_warp_transform();
  });
}

void StressTestRunner::phase_vector_io() {
  const auto psd_path = QDir(report_dir_).filePath(QStringLiteral("stress-scene.psd"));

  // 56: the full two-scene document (shape layers, vstk/vogk, warp bakes,
  // nested folders/lsct) through the PSD writer and back in - and the disk
  // artifact becomes the complete scene instead of step 39's scene-1-only
  // capture.
  step("56_psd_roundtrip_full", "Save + reload the full scene PSD", "io", [&] {
    if (!w.save_document_to_path(psd_path)) {
      report_.warnings.append(QStringLiteral("Full-scene PSD save failed: %1").arg(psd_path));
      return;
    }
    w.open_document_path(psd_path);
    pump();
  }, current_document_megapixels());
  if (w.document_tabs_ != nullptr && w.document_tabs_->count() > 1) {
    w.set_session_saved(w.session());
    (void)w.close_document_tab(w.document_tabs_->currentIndex());
    pump();
  }
}

void StressTestRunner::phase_composite() {
  // 42: flatten, checksum, and save the scene PNG next to the reports.
  step("42_final_composite", "Final flatten + checksum + scene PNG", "composite", [&] {
    const auto composite = qimage_from_document(doc(), true);
    report_.composite_checksum = fnv1a64_image(composite);
    const auto scaled = composite.width() > 1024
                            ? composite.scaledToWidth(1024, Qt::SmoothTransformation)
                            : composite;
    if (!scaled.save(QDir(report_dir_).filePath(QStringLiteral("stress-scene.png")))) {
      report_.warnings.append(QStringLiteral("Scene PNG save failed"));
    }
  }, current_document_megapixels());

  // 43: vector shape layer - create a live rounded rect, run the document
  // geometry ops over it (rotate round trip + flip), then rasterize it away
  // so the composite checksum stays comparable to earlier baselines. The
  // appearance is pinned via the mirror save/restore so the step's cost never
  // depends on the user's persisted fill/stroke settings.
  begin_vector_shape_phase();
  step("43_vector_shapes", "Shape layer create + geometry ops + rasterize", "vector", [&] {
    set_vector_appearance(solid_vector_fill(QColor(180, 100, 60)), true, 2.0, QColor(40, 30, 20));
    patchy::LiveShapeParams params;
    params.kind = patchy::LiveShapeKind::RoundedRectangle;
    params.left = size_ * 0.1;
    params.top = size_ * 0.1;
    params.right = size_ * 0.55;
    params.bottom = size_ * 0.45;
    params.corner_radii = {24.0, 24.0, 24.0, 24.0};
    patchy::populate_live_shape_box_corners(params);
    w.create_or_extend_shape_layer(patchy::generate_live_shape_subpaths(params), params,
                                   QStringLiteral("Stress Shape %1"));
    settle();
    if (const auto active = doc().active_layer_id(); active.has_value()) {
      static_cast<void>(patchy::flip_layer_horizontal(doc(), *active));
    }
    patchy::rotate_document_clockwise(doc());
    patchy::rotate_document_counterclockwise(doc());
    if (canvas() != nullptr) {
      canvas()->document_changed();
    }
    settle();
    w.rasterize_active_layers();
    settle();
    w.undo();  // rasterize
    w.undo();  // shape layer
    settle();
  }, current_document_megapixels());
  end_vector_shape_phase();
}

StressReport StressTestRunner::run() {
  report_.started_utc = QDateTime::currentDateTimeUtc();
  report_.app_version = QStringLiteral(PATCHY_VERSION);
#ifdef NDEBUG
  report_.build_type = QStringLiteral("release");
#else
  report_.build_type = QStringLiteral("debug");
  report_.warnings.append(QStringLiteral("DEBUG build - results will not reflect release performance"));
#endif
  report_.qt_version = QString::fromUtf8(qVersion());
  report_.os = QSysInfo::prettyProductName();
  report_.cpu_name = stress_cpu_name();
  report_.logical_cores = QThread::idealThreadCount();
  report_.ram_mb = stress_total_ram_mb();
  report_.offscreen = QGuiApplication::platformName() == QStringLiteral("offscreen");
  if (auto* screen = w.screen(); screen != nullptr) {
    report_.screen_size = screen->size();
    report_.device_pixel_ratio = screen->devicePixelRatio();
  }
  report_.baseline_tag = QLatin1String(kBaselineTag);

  QElapsedTimer total;
  total.start();

  // Untimed warm-up: absorb first-show/layout costs before step 01.
  pump();
  if (canvas() != nullptr) {
    settle();
  }

  // Progress + cancel. Cancelling stops at the next step boundary; the partial
  // scene document is left open and editable (the user may want to keep it).
  QProgressDialog progress(MainWindow::tr("Preparing stress test..."), MainWindow::tr("Cancel"), 0,
                           kTotalStepCount, &w);
  progress.setObjectName(QStringLiteral("stressTestProgressDialog"));
  progress.setWindowTitle(MainWindow::tr("Profiling Stress Test"));
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  progress.setMinimumWidth(420);
  progress.setAutoClose(false);
  progress.setAutoReset(false);
  remember_dialog_position(progress);
  progress.setValue(0);
  progress_ = &progress;

  phase_setup();
  report_.window_size = w.size();
  report_.canvas_viewport = canvas() != nullptr ? canvas()->size() : QSize();
  phase_paint_hardware();
  phase_text();
  phase_atmosphere();
  phase_filters();
  phase_adjustments();
  phase_move_matrix();
  phase_interact();
  phase_history();
  phase_io();
  phase_arcade_vector();
  phase_arcade_filters();
  phase_smart_and_warp();
  phase_vector_io();
  phase_composite();

  progress_ = nullptr;
  progress.close();
  if (cancel_requested_) {
    report_.warnings.append(QStringLiteral("Cancelled by user after step %1 of %2")
                                .arg(static_cast<int>(report_.steps.size()))
                                .arg(kTotalStepCount));
  } else if (static_cast<int>(report_.steps.size()) != kTotalStepCount) {
    report_.warnings.append(QStringLiteral("Step count mismatch: ran %1, expected %2 (update kTotalStepCount)")
                                .arg(static_cast<int>(report_.steps.size()))
                                .arg(kTotalStepCount));
  }

  report_.total_ms = static_cast<double>(total.nsecsElapsed()) / 1'000'000.0;
  report_.final_layer_count = count_layers(doc().layers());
  report_.peak_working_set_mb = stress_peak_working_set_mb();
  report_.working_set_end_mb = stress_current_working_set_mb();

  // Rating: geometric mean of baseline/actual over rated, non-timed-out steps.
  double log_sum = 0.0;
  int rated = 0;
  for (const auto& step_result : report_.steps) {
    if (step_result.baseline_ms < 0.0 || step_result.timed_out) {
      continue;
    }
    log_sum += std::log(step_result.baseline_ms / std::max(step_result.ms, 0.1));
    ++rated;
  }
  if (rated > 0) {
    report_.geomean_ratio = std::exp(log_sum / rated);
    report_.rating = std::round(1000.0 * report_.geomean_ratio);
  }

  report_.cancelled = cancel_requested_;
  report_.success = !cancel_requested_;
  return report_;
}

// ---------------------------------------------------------------------------
// MainWindow entry points.

namespace {

QString default_stress_report_dir() {
  auto settings = app_settings();
  return QFileInfo(settings.fileName()).absolutePath() + QStringLiteral("/stress-reports");
}

// Writes stress-<timestamp>.{json,txt} plus the stable stress-latest copies.
void write_stress_report_files(StressReport& report, const QString& directory) {
  QDir dir(directory);
  if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
    report.warnings.append(QStringLiteral("Could not create report directory %1").arg(directory));
    report.success = false;
    return;
  }
  const auto stamp = report.started_utc.toLocalTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
  const auto json = report_to_json(report).toJson(QJsonDocument::Indented);
  const auto text = report_to_text(report);
  report.report_json_path = dir.filePath(QStringLiteral("stress-%1.json").arg(stamp));
  report.report_txt_path = dir.filePath(QStringLiteral("stress-%1.txt").arg(stamp));
  bool ok = true;
  ok = write_text_file(report.report_json_path, QString::fromUtf8(json)) && ok;
  ok = write_text_file(report.report_txt_path, text) && ok;
  ok = write_text_file(dir.filePath(QStringLiteral("stress-latest.json")), QString::fromUtf8(json)) && ok;
  ok = write_text_file(dir.filePath(QStringLiteral("stress-latest.txt")), text) && ok;
  if (!ok) {
    report.warnings.append(QStringLiteral("One or more report files could not be written to %1").arg(directory));
    report.success = false;
  }
}

}  // namespace

StressReport MainWindow::run_stress_test_scenario(const StressTestOptions& options) {
  const auto report_dir = options.report_dir.isEmpty() ? default_stress_report_dir() : options.report_dir;
  // The scenario itself writes artifacts (scene PSD/PNG) into the report
  // directory before the report files land, so create it up front.
  QDir().mkpath(report_dir);
  StressTestRunner runner(*this, options, report_dir);
  auto& report = runner.report();

  // Sessions, not tab count (a floated document has no tab).
  if (!sessions_.empty()) {
    close_all_document_tabs();
    QApplication::processEvents();
    if (!sessions_.empty()) {
      report.warnings.append(QStringLiteral("Aborted: documents were left open (close was cancelled)"));
      report.success = false;
      write_stress_report_files(report, report_dir);
      return report;
    }
  }

  // Normalize the window so runs are comparable across sessions.
  showNormal();
  const auto target = options.preset == StressPreset::Smoke ? QSize(1000, 700) : QSize(1600, 1000);
  if (auto* window_screen = screen(); window_screen != nullptr) {
    const auto available = window_screen->availableGeometry().size();
    resize(target.boundedTo(available - QSize(32, 32)));
  } else {
    resize(target);
  }
  raise();
  activateWindow();
  QApplication::processEvents();

  statusBar()->showMessage(tr("Running stress test..."));
  try {
    runner.run();
  } catch (const std::exception& error) {
    report.warnings.append(QStringLiteral("Scenario failed: %1").arg(QString::fromUtf8(error.what())));
    report.success = false;
  } catch (...) {
    report.warnings.append(QStringLiteral("Scenario failed with an unknown exception"));
    report.success = false;
  }

  // Leave the scene document open. A completed run marks it saved (disposable
  // artifact, closing must not prompt); a CANCELLED run leaves it modified so
  // the user can keep working on it and gets the normal save prompt.
  if (has_active_document()) {
    if (!runner.was_cancelled()) {
      set_session_saved(session());
    }
    refresh_document_tab_titles();
  }
  write_stress_report_files(report, report_dir);
  statusBar()->showMessage(runner.was_cancelled() ? tr("Stress test cancelled")
                           : report.success      ? tr("Stress test complete")
                                                 : tr("Stress test failed"));
  return report;
}

void MainWindow::run_stress_test_interactive(StressPreset preset) {
  auto message = tr("The profiling stress test closes all open documents, then builds a large scripted "
                    "scene to measure performance. It takes several minutes; please leave the mouse and "
                    "keyboard alone while it runs. This is primarily a development tool.");
#ifndef NDEBUG
  message += QStringLiteral("\n\n") +
             tr("Warning: this is a DEBUG build - results will not reflect release performance.");
#endif
  const auto choice =
      show_warning_message(this, tr("Profiling Stress Test"), message, QMessageBox::Ok | QMessageBox::Cancel,
                           QMessageBox::Cancel, QStringLiteral("stressTestWarningMessageBox"));
  if (choice != QMessageBox::Ok) {
    return;
  }
  // Sessions, not tab count: a floated document has no tab but must still be
  // closed (or cancel the run) before the scene takes over.
  if (!sessions_.empty()) {
    close_all_document_tabs();
    if (!sessions_.empty()) {
      statusBar()->showMessage(tr("Stress test cancelled"));
      return;
    }
  }

  StressTestOptions options;
  options.preset = preset;
  options.interactive = true;
  const auto report = run_stress_test_scenario(options);
  if (report.cancelled) {
    // The user bailed; no results fanfare. The partial scene stays open for
    // them to keep or discard, and the partial report is already on disk.
    return;
  }

  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("stressTestResultsDialog"));
  auto* root = new QVBoxLayout(&dialog);
  auto* content = install_dark_dialog_chrome(dialog, root, tr("Stress Test Results"));
  auto* headline = new QLabel(
      report.rating >= 0.0 ? tr("Rating: %1").arg(QString::number(report.rating, 'f', 0)) : tr("Rating: n/a"),
      &dialog);
  auto headline_font = headline->font();
  headline_font.setPointSizeF(headline_font.pointSizeF() * 1.6);
  headline_font.setBold(true);
  headline->setFont(headline_font);
  content->addWidget(headline);
  auto* text_view = new QPlainTextEdit(&dialog);
  text_view->setObjectName(QStringLiteral("stressTestResultsText"));
  text_view->setReadOnly(true);
  text_view->setLineWrapMode(QPlainTextEdit::NoWrap);
  text_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  text_view->setPlainText(report_to_text(report));
  text_view->setMinimumSize(720, 420);
  content->addWidget(text_view, 1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
  auto* open_folder =
      buttons->addButton(tr("Open Report Folder"), QDialogButtonBox::ActionRole);
  const auto report_dir = QFileInfo(report.report_txt_path.isEmpty() ? default_stress_report_dir()
                                                                     : report.report_txt_path)
                              .absolutePath();
  connect(open_folder, &QPushButton::clicked, &dialog,
          [report_dir] { QDesktopServices::openUrl(QUrl::fromLocalFile(report_dir)); });
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  content->addWidget(buttons);
  exec_dialog(dialog);
}

void MainWindow::start_cli_stress_test(StressTestOptions options) {
  options.interactive = false;
  options.quit_when_done = true;
  QTimer::singleShot(0, this, [this, options] {
    const auto report = run_stress_test_scenario(options);
    // No document may prompt during shutdown.
    for (auto& stress_session : sessions_) {
      if (stress_session != nullptr) {
        set_session_saved(*stress_session);
      }
    }
    QCoreApplication::exit(report.success ? 0 : 1);
  });
}

}  // namespace patchy::ui
