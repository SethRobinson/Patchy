#pragma once

#include "ui/canvas_widget.hpp"

#include <QDateTime>
#include <QSize>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <vector>

namespace patchy::ui {

// Profiling stress test (Preferences > Application > Development, or
// `patchy --stress-test=<preset>`). The scenario builds the "PATCHY 64" retro
// desk scene while timing every step; see main_window_stress_test.cpp.

enum class StressPreset { Smoke, Quick, Small, Standard, Huge };

// "smoke"/"quick"/"small"/"standard"/"huge" -> preset; nullopt for anything else.
[[nodiscard]] std::optional<StressPreset> stress_preset_from_string(QStringView token);
[[nodiscard]] QString stress_preset_token(StressPreset preset);
// Square canvas edge in pixels: 512 / 1024 / 2048 / 4096 / 8192. Smoke exists
// for the automated offscreen test only and is deliberately not offered in the
// UI. Quick is the default (fast iteration); note the move outline-preview
// fallback thresholds are only reached at Standard and above.
[[nodiscard]] int stress_preset_canvas_size(StressPreset preset);

struct StressTestOptions {
  StressPreset preset{StressPreset::Quick};
  // Empty = <settings dir>/stress-reports.
  QString report_dir;
  // True when launched from Preferences: show the results dialog and leave the
  // scene document open. CLI runs quit instead.
  bool interactive{false};
  bool quit_when_done{false};
};

struct StressStepResult {
  // Stable machine key ("05_monitor_body"); never rename, reports are diffed
  // across runs and the baseline table is keyed on it.
  QString id;
  QString label;
  // setup/paint/text/styles/filters/adjustments/move/interact/history/io/composite
  QString category;
  double ms{0.0};
  // Document megapixels the step touched; < 0 = not applicable.
  double megapixels{-1.0};
  // Interactive phases only; < 0 = not applicable.
  double fps{-1.0};
  int frames{0};
  int inputs{0};
  bool timed_out{false};
  CanvasWidget::RenderCacheDiagnostics diag_delta{};
  // Reference duration from kStepBaselines; < 0 = unrated step.
  double baseline_ms{-1.0};
};

struct StressReport {
  QString app_version;
  QString build_type;  // "release" / "debug"
  QString preset_token;
  QString os;
  QString cpu_name;
  QString qt_version;
  int logical_cores{0};
  qint64 ram_mb{-1};
  QSize screen_size;
  double device_pixel_ratio{1.0};
  int canvas_size{0};
  QSize window_size;
  QSize canvas_viewport;
  bool offscreen{false};
  bool interactive{false};
  QDateTime started_utc;
  double total_ms{0.0};
  std::vector<StressStepResult> steps;
  double rating{-1.0};
  double geomean_ratio{-1.0};
  QString baseline_tag;
  qint64 peak_working_set_mb{-1};
  qint64 working_set_end_mb{-1};
  quint64 composite_checksum{0};
  int final_layer_count{0};
  QStringList warnings;
  bool success{false};
  // True when the user cancelled via the progress dialog; the partial scene
  // document is left open and modified in that case.
  bool cancelled{false};
  // Populated by the report writers.
  QString report_json_path;
  QString report_txt_path;
};

}  // namespace patchy::ui
