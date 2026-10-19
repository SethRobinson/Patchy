#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Cold-startup phase timing (docs/startup-performance.md). main() enables it for
// PATCHY_STARTUP_TRACE=1 (a phase table on stderr once startup is idle) and for
// --startup-benchmark (a JSON report, then exit). Off, every call is one test of a
// plain bool: nothing is read, stored or printed.
//
// Times are milliseconds since the OS created the process, so the table includes the
// loader, DLL mapping and static initializers before main(). Where the OS start is not
// available (wasm), they count from initialize() and process_start_known() is false.
//
// Record from the GUI thread only. Names must be string literals (they are stored as
// pointers) and are compatibility identifiers: baselines key on them, so never rename
// one; add a new name instead.
namespace patchy::startup_trace {

namespace detail {
extern bool g_enabled;
}

// Called first in main(). Captures the process start; records only when `enable`.
void initialize(bool enable) noexcept;

[[nodiscard]] inline bool enabled() noexcept { return detail::g_enabled; }

// Ends recording (the report has been written); later calls are no-ops again.
void stop() noexcept;

[[nodiscard]] double now_ms() noexcept;
[[nodiscard]] bool process_start_known() noexcept;
// Process creation to initialize(); 0 when the start is unknown.
[[nodiscard]] double pre_main_ms() noexcept;

enum class Kind { Phase, Mark, Background };

struct Event {
  const char* name = "";
  double start_ms = 0.0;
  double end_ms = 0.0;
  int depth = 0;
  Kind kind = Kind::Phase;
};

void mark(const char* name) noexcept;
// A span measured elsewhere (a worker's job reported back on the GUI thread).
void record_background(const char* name, double start_ms, double end_ms) noexcept;

// A nested phase: recorded when it ends (destruction or end()).
class Scope {
 public:
  explicit Scope(const char* name) noexcept;
  ~Scope() { end(); }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
  void end() noexcept;

 private:
  const char* name_;
  double start_ms_ = 0.0;
  int depth_ = 0;
  bool open_ = false;
};

[[nodiscard]] std::vector<Event> events();

// The first mark with this name, or a negative value.
[[nodiscard]] double mark_time(std::string_view name);

// Human-readable table: phases in start order, nested by depth, with each parent's
// unattributed time, then the marks and background spans.
[[nodiscard]] std::string format_table();

// The report the startup benchmark writes ("patchy-startup-report", version 1).
// `fields` are extra top-level string members (version, platform, ...).
[[nodiscard]] std::string format_json(const std::vector<std::pair<std::string, std::string>>& fields);

}  // namespace patchy::startup_trace
