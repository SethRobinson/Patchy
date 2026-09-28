#pragma once

// Runs one classic Photoshop filter plug-in in the out-of-process host
// (patchy-8bf-host32.exe / patchy-8bf-host64.exe next to patchy.exe): the
// pixels go through a named file mapping, the request and result over a named
// pipe, and the caller's event loop keeps pumping so the progress dialog stays
// live and a plug-in crash only ends the helper. Windows only; other platforms
// never reach it (the probe rejects every plug-in there). See docs/plugins.md.

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
};

// Blocks (pumping the event loop) until the helper reports a result, exits, or
// the caller cancels. Never throws.
[[nodiscard]] LegacyPluginRunResult run_legacy_plugin_out_of_process(const LegacyPluginRunInput& input,
                                                                     const LegacyPluginRunCallbacks& callbacks);

// The helper executable for a plug-in architecture ("x86" or "x64"), next to
// the running application.
[[nodiscard]] QString legacy_plugin_helper_path(const std::string& architecture);

}  // namespace patchy::ui
