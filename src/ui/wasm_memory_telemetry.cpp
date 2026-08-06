#include "ui/wasm_memory_telemetry.hpp"

#include "ui/main_window.hpp"
#include "ui/memory_info.hpp"

#include <QString>
#include <QTimer>
#include <QtGlobal>

#include <emscripten/heap.h>
#include <emscripten/val.h>

namespace patchy::ui {
namespace {

// emscripten::val (not EM_ASM: its $-placeholders trip
// -Wdollar-in-identifier-extension, and this file is warning-clean like the
// rest of the wasm glue). Runs on the Qt GUI thread, which is the browser main
// thread, so val::global() is the page's own globalThis. The used/peak probes
// come from memory_info (emmalloc-level claim; see the comments there), MB
// granular, which is plenty for the curves the harness draws.
void publish_memory_stats(const MainWindow& window, bool log_to_console, double seq) {
  const double heap_bytes = static_cast<double>(emscripten_get_heap_size());
  const double used_bytes = static_cast<double>(current_process_memory_mb()) * 1048576.0;
  const double peak_used_bytes = static_cast<double>(peak_process_memory_mb()) * 1048576.0;
  const double limit_bytes = static_cast<double>(wasm_heap_limit_mb()) * 1048576.0;
  const double history_bytes = static_cast<double>(window.history_retained_bytes());
  const double history_budget_bytes = static_cast<double>(history_memory_budget_bytes());
  auto stats = emscripten::val::object();
  stats.set("heapBytes", heap_bytes);
  stats.set("usedBytes", used_bytes);
  stats.set("peakUsedBytes", peak_used_bytes);
  stats.set("limitBytes", limit_bytes);
  stats.set("historyBytes", history_bytes);
  stats.set("historyBudgetBytes", history_budget_bytes);
  stats.set("seq", seq);
  stats.set("timestampMs", emscripten::val::global("Date").call<double>("now"));
  emscripten::val::global("globalThis").set("patchyMemStats", stats);
  if (log_to_console) {
    const auto mb = [](double bytes) { return QString::number(bytes / 1048576.0, 'f', 1); };
    const auto line = QStringLiteral("Patchy mem: used=%1MB peak=%2MB heap=%3MB history=%4MB limit=%5MB")
                          .arg(mb(used_bytes), mb(peak_used_bytes), mb(heap_bytes),
                               mb(history_bytes), mb(limit_bytes));
    emscripten::val::global("console").call<void>("info",
                                                  emscripten::val(line.toStdString()));
  }
}

}  // namespace

void install_wasm_memory_telemetry(MainWindow& window) {
  // Diagnostics opt-in only: release visitors must not carry a publisher
  // timer or a memory readout on globalThis. ?PATCHY_MEM_STATS=1 enables the
  // publisher, ?PATCHY_MEM_LOG=1 additionally logs each sample (and implies
  // stats); both land in the environment via scripts/wasm/app-env-pre.js
  // before main (the memtest harness always opts in via patchyExtraEnv).
  const bool log_to_console = qEnvironmentVariableIntValue("PATCHY_MEM_LOG") != 0;
  if (!log_to_console && qEnvironmentVariableIntValue("PATCHY_MEM_STATS") == 0) {
    return;
  }
  auto* timer = new QTimer(&window);
  timer->setInterval(1000);
  QObject::connect(timer, &QTimer::timeout, &window,
                   [&window, log_to_console, seq = 0.0]() mutable {
                     seq += 1.0;
                     publish_memory_stats(window, log_to_console, seq);
                   });
  timer->start();
  publish_memory_stats(window, log_to_console, 0.0);
}

}  // namespace patchy::ui
