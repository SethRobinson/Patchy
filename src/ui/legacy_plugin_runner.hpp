#pragma once

// Runs one classic Photoshop filter plug-in in the out-of-process host
// (patchy-8bf-host32.exe / patchy-8bf-host64.exe next to patchy.exe): the
// pixels go through a named file mapping, the request and result over a named
// pipe, and the caller's event loop keeps pumping so the progress dialog stays
// live and a plug-in crash only ends the helper. On Linux the same helper runs
// inside Raven (useraven.org) and talks over loopback TCP instead; macOS never
// reaches this (the probe rejects every plug-in there). See docs/plugins.md.

#include "plugins/pipl.hpp"

#include <QByteArray>
#include <QRect>
#include <QString>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace patchy::ui {

struct LegacyPluginRunInput {
  QString helper_executable;
  QString plugin_path;
  std::string entry_point;  // empty: the host tries the conventional export names
  bool show_dialog{true};   // false skips the Parameters selector (repeat with last settings)
  // With show_dialog false, some plug-ins still open their settings dialog from
  // the Start selector (typically on their first run, when they hold no
  // parameters yet). True answers any top-level window the helper shows with
  // its OK button after a short delay, so unattended runs never block.
  bool auto_accept_dialogs{false};
  // When set, a PNG of the plug-in's dialog (the helper's first visible
  // top-level window) is saved here while it is up, before any auto-accept.
  QString capture_dialog_path;
  bool protect_alpha{false};
  std::uintptr_t parent_window{0};  // HWND that owns the plug-in's dialogs, 0 for none
  // The largest screen the plug-in is told about, 0 for the whole work area of
  // the monitor showing parent_window (docs/plugins.md, the virtual screen).
  std::int32_t screen_max_width{0};
  std::int32_t screen_max_height{0};
  // Title of the movable frame a full-screen plug-in canvas is placed in.
  QString window_title;
  std::int32_t width{0};
  std::int32_t height{0};
  std::int32_t planes{3};  // 3 or 4, interleaved 8-bit
  QRect filter_rect;       // in layer pixel coordinates
  int filter_case{1};
  pipl::FilterCaseInfo case_info;
  const std::vector<std::uint8_t>* input{nullptr};  // width * height * planes
  std::vector<std::uint8_t>* output{nullptr};       // same size; receives the result
  const std::vector<std::uint8_t>* mask{nullptr};   // width * height coverage, or null
  std::array<std::uint8_t, 3> foreground{0, 0, 0};
  std::array<std::uint8_t, 3> background{255, 255, 255};
  std::int32_t resolution_fixed{72 << 16};
  QString document_title;
  QByteArray parameters;  // the plug-in's parameter block from a previous run
};

enum class LegacyPluginRunStatus { Ok, Cancelled, Error };

// The selector the helper is about to call, as reported over the pipe
// (kMessagePhase): Parameters is where a plug-in shows its settings dialog;
// Prepare, Start and Continue are the filtering pass (some plug-ins open their
// dialog from Start instead, so a visible window still means "waiting for the
// user" there).
enum class LegacyPluginPhase { Unknown, Parameters, Prepare, Start, Continue, Finish };

struct LegacyPluginRunResult {
  LegacyPluginRunStatus status{LegacyPluginRunStatus::Error};
  QString message;        // translated, for the user
  QByteArray parameters;  // the plug-in's parameter block after the run
};

struct LegacyPluginRunCallbacks {
  // Progress reported by the plug-in (done of total).
  std::function<void(int, int)> progress;
  // Polled while waiting; true asks the plug-in to stop (the helper is killed
  // when it does not answer within a few seconds).
  std::function<bool()> cancelled;
  // Called on every pump so the caller can tick its busy indicator.
  std::function<void()> tick;
  // The screen rectangle (physical pixels) of the plug-in's largest visible
  // window while one is up, an invalid rect while none is; reported on every
  // pump so the caller can keep its progress box out of the way.
  std::function<void(const QRect&)> plugin_window;
  // The selector the helper is about to call (see LegacyPluginPhase).
  std::function<void(LegacyPluginPhase)> phase;
  // Polled while waiting; true asks the runner to bring the plug-in's largest
  // visible window to the foreground once (the user lost it behind Patchy).
  std::function<bool()> raise_plugin_window;
};

// Blocks (pumping the event loop) until the helper reports a result, exits, or
// the caller cancels. Never throws.
[[nodiscard]] LegacyPluginRunResult run_legacy_plugin_out_of_process(const LegacyPluginRunInput& input,
                                                                     const LegacyPluginRunCallbacks& callbacks);

#ifdef Q_OS_WIN
// The helper and every process it starts, as one Windows job object. A plug-in
// may run its interface in a program of its own (G'MIC's 8bf plug-in starts
// G'MIC-Qt), which outlives a helper that is killed or cancelled; the runner
// ends the whole tree on every outcome except a successful run. Closing the job
// ends nothing, so a program a plug-in opened for the user (a help viewer) stays
// after a run that applied. See docs/plugins.md.
class LegacyPluginProcessTree {
public:
  LegacyPluginProcessTree();
  ~LegacyPluginProcessTree();
  LegacyPluginProcessTree(const LegacyPluginProcessTree&) = delete;
  LegacyPluginProcessTree& operator=(const LegacyPluginProcessTree&) = delete;

  // Puts a running process in the job; processes it starts afterwards join it.
  bool assign(qint64 process_id);
  // The processes in the job right now.
  [[nodiscard]] std::vector<qint64> process_ids() const;
  // Ends every process in the job.
  void terminate();

private:
  void* job_{nullptr};
};
#endif

#ifdef Q_OS_LINUX
// The raven program that runs the helper: PATCHY_RAVEN, the bundled copy beside the
// app (cmake/patchy_raven.cmake), raven on PATH, or a usual install location. Empty when
// none is found.
[[nodiscard]] QString legacy_plugin_raven_executable();
#endif

// The helper executable for a plug-in architecture ("x86" or "x64"), next to
// the running application.
[[nodiscard]] QString legacy_plugin_helper_path(const std::string& architecture);

}  // namespace patchy::ui
