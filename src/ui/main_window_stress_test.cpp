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
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pixel_tools.hpp"
#include "core/rect_utils.hpp"
#include "ui/app_settings.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/image_document_io.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/qt_geometry.hpp"

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
constexpr int kTotalStepCount = 42;

// Reference step durations in ms, measured on the calibration machine at the
// STANDARD preset in a release build. Only the rating depends on these; the
// TXT report ends with a ready-to-paste refreshed table. Bump the tag whenever
// the table is recalibrated.
constexpr const char* kBaselineTag = "quick@i9-12900KS-2026-07";

struct StepBaseline {
  const char* id;
  double ms;
};

constexpr StepBaseline kStepBaselines[] = {
    {"01_create_document", 73.7},   {"02_wall_gradient", 105.3},   {"03_wall_texture", 169.4},
    {"04_desk_surface", 241.8},     {"05_monitor_body", 217.5},    {"06_monitor_screen", 288.5},
    {"07_c64_body", 181.0},         {"08_c64_keys", 137.3},        {"09_c64_badge", 270.3},
    {"10_mug_steam", 563.3},        {"11_desk_props", 1449.8},     {"12_boot_text", 595.0},
    {"13_boot_text_glow", 249.0},   {"14_game_parody_pixels", 148.9}, {"15_game_title_text", 324.1},
    {"16_sticky_notes", 976.9},     {"17_title_text", 254.3},      {"18_text_reedit", 209.0},
    {"19_scanlines", 259.9},        {"20_screen_glow", 525.2},     {"21_screen_mask", 393.4},
    {"22_selection_blur", 867.6},   {"23_vignette", 295.6},        {"24_grain", 369.4},
    {"25_adjust_levels", 540.3},    {"26_adjust_curves", 548.8},   {"27_adjust_hue_sat", 666.6},
    {"28_adjust_color_balance", 756.5}, {"29_move_small_live", 518.6}, {"30_move_styled_outline", 2832.0},
    {"31_move_large_outline", 6501.6}, {"32_move_multi_layer", 153.1}, {"33_move_nudges", 529.8},
    {"34_free_transform", 724.4},   {"35_zoom_pan", 112.3},        {"36_history_build", 70.0},
    {"37_undo_redo", 12299.2},      {"38_merge_visible", 2485.8},  {"39_psd_save", 784.0},
    {"40_psd_reload", 1090.8},      {"41_multi_document", 436.3},  {"42_final_composite", 1089.3},
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

  // Filled shape via the real tool pipeline (one undo snapshot per drag).
  void draw_shape(CanvasTool tool, QRect document_rect, QColor color, int corner_radius = 0) {
    w.activate_tool(tool);
    canvas()->set_fill_shapes(true);
    canvas()->set_shape_corner_radius(corner_radius);
    set_foreground(color);
    drag(document_rect.topLeft(), document_rect.bottomRight(), 4);
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
  void apply_filter_direct(const QString& identifier, const std::vector<int>& values, const QString& undo_label) {
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
    Rect final_bounds = bounds;
    auto final_pixels =
        build_filter_preview_pixels(original, selection, bounds, identifier, w.filters_,
                                    FilterPreviewSettings{true, values}, canvas()->primary_color(),
                                    canvas()->secondary_color(), nullptr, &final_bounds);
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

  // Screen geometry shared between steps (game parody, scanlines, glow).
  QRect screen_rect_;
  QRect game_area_;
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
    apply_filter_direct(QStringLiteral("patchy.filters.clouds"),
                        {std::clamp(size_ / 16, 12, 512), 5, 45, 7}, QStringLiteral("Clouds"));
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
    apply_filter_direct(QStringLiteral("patchy.filters.film_grain"), {18}, QStringLiteral("Desk grain"));
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
    apply_filter_direct(QStringLiteral("patchy.filters.twirl"), {35}, QStringLiteral("Steam twirl"));
    set_layer_opacity_percent(70);
    tighten_layer_to_opaque(mug_id_);
    tighten_layer_to_opaque(steam_id_);
  });

  // 11: modem, floppy stack, wall poster with the RTsoft-logo tip stamp.
  step("11_desk_props", "Modem + floppies + RTsoft poster", "paint", [&] {
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
    (void)add_text_layer(pt(0.716, 0.360), QStringLiteral("EVERYBODY MAKES"), at(0.013),
                         QColor(74, 66, 58));
    tighten_layer_to_opaque(poster_layer);
    tighten_layer_to_opaque(modem_id_);
  });
}

void StressTestRunner::phase_text() {
  // 12: the boot screen text, four committed editor lifecycles.
  step("12_boot_text", "Boot text (4 editor lifecycles)", "text", [&] {
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

  // 14: the game parody, hundreds of scattered cells.
  step("14_game_parody_pixels", "CAFE QUEST pixel-art scene (scattered cells)", "paint", [&] {
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
    (void)add_text_layer(QPoint(game_area_.left() + at(0.004), game_area_.top() - at(0.012)),
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
  });

  // 16: sticky notes (one rotated via free transform).
  step("16_sticky_notes", "Sticky notes + free-transform rotate", "text", [&] {
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
  // 19: scanlines, direct thin rows (many small commits probe #2).
  step("19_scanlines", "CRT scanlines (thin rows)", "styles", [&] {
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
    apply_filter_direct(QStringLiteral("patchy.filters.gaussian_blur"),
                        {std::clamp(size_ / 1024 * 3, 1, 12)}, QStringLiteral("Selection blur"));
    canvas()->clear_selection();
  }, canvas_megapixels() * 0.2);

  // 23: vignette on its own layer.
  step("23_vignette", "Vignette layer", "filters", [&] {
    add_named_layer(QStringLiteral("Vignette"));
    use_solid_fill_settings();
    w.fill_active_layer_with_color(QColor(255, 255, 255), MainWindow::tr("Vignette base"));
    apply_filter_direct(QStringLiteral("patchy.filters.vignette"), {70}, QStringLiteral("Vignette"));
    set_layer_blend_mode(BlendMode::Multiply);
    set_layer_opacity_percent(45);
  }, canvas_megapixels());

  // 24: film grain overlay.
  step("24_grain", "Analog grain layer (overlay)", "filters", [&] {
    add_named_layer(QStringLiteral("Grain"));
    use_solid_fill_settings();
    w.fill_active_layer_with_color(QColor(128, 128, 128), MainWindow::tr("Grain base"));
    apply_filter_direct(QStringLiteral("patchy.filters.film_grain"), {55}, QStringLiteral("Grain"));
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
  curves.curves.shadow_output = 6;
  curves.curves.midtone_output = 132;
  curves.curves.highlight_output = 250;
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
  }, canvas_megapixels());
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
