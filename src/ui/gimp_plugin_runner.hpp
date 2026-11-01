#pragma once

// Runs one GIMP 3 plug-in procedure on one layer through patchy-gimp-host
// (docs/gimp-plugins.md, "Running"). The layer's pixels go through a temporary file
// both processes map; the request and the helper's reports travel over the helper's
// stdin and stdout. The caller's event loop keeps pumping, so Patchy repaints the live
// preview and the run box stays responsive; a plug-in crash ends only the plug-in.

#include "plugins/gimp/gimp_host_link.hpp"

#include <QByteArray>
#include <QRect>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace patchy::ui {

struct GimpPluginRunInput {
  QString helper;
  gimp::link::LaunchSpec launch;
  QString procedure;
  QByteArray install;
  int run_mode{0};               // GIMP RunMode: 0 interactive, 1 non-interactive, 2 last values
  QByteArray arguments;          // optional: GIMP wire PROC_RUN message carrying argument values
  std::uint32_t argument_count{0};
  int image_width{0};
  int image_height{0};
  int layer_x{0};
  int layer_y{0};
  int width{0};
  int height{0};
  std::uint32_t bit_depth{8};
  std::uint32_t channels{4};
  bool protect_alpha{false};
  QString layer_name;
  QString image_name;
  double resolution{72.0};
  const std::vector<std::uint8_t>* input{nullptr};  // width * height * pixel bytes, packed rows
  const std::vector<std::uint8_t>* mask{nullptr};   // width * height coverage, or null
  QRect mask_bounds;                                // in layer pixels
  double foreground[4]{0, 0, 0, 1};
  double background[4]{1, 1, 1, 1};
  std::uint64_t parent_window{0};
  QString language;
  std::uint32_t timeout_ms{0};
  std::vector<std::pair<std::string, std::vector<std::uint8_t>>> stored_data;
  bool trace{false};
  QString wire_log;
  std::vector<std::uint8_t> color_profile;  // sRGB, for the colors GIMP's protocol carries
  // Scripts and tests (Windows): once the plug-in's dialog has been up for settle_ms,
  // save an image of it and/or answer it with OK (1) or Cancel (2).
  QString capture_dialog_path;
  int answer_dialog{0};
  int settle_ms{1500};
};

struct GimpPluginRunCallbacks {
  // Progress (fraction < 0: busy) and the plug-in's progress text.
  std::function<void(double, const QString&)> progress;
  // The plug-in merged a result into this area (layer pixels); `pixels` is the whole
  // current layer, packed rows of the input's format, valid during the call.
  std::function<void(const QRect&, const std::uint8_t* pixels)> update;
  // The plug-in's largest visible window (screen pixels), invalid when none is up.
  std::function<void(const QRect&)> plugin_window;
  std::function<void(const QString&)> message;
  // Polled; true asks the helper to end the plug-in.
  std::function<bool()> cancelled;
  std::function<void()> tick;
};

enum class GimpPluginRunStatus { Ok, Cancelled, Error };

struct GimpPluginRunResult {
  GimpPluginRunStatus status{GimpPluginRunStatus::Error};
  QString code;     // the helper's code ("unsupported-procedure", "crashed", ...)
  QString detail;   // the unsupported procedure's name
  QString message;  // English detail from the helper or the plug-in
  bool changed{false};
  QRect changed_rect;
  std::vector<std::uint8_t> output;  // the whole layer after the run (status Ok)
  std::vector<std::pair<std::string, std::vector<std::uint8_t>>> stored_data;
  QStringList unsupported_calls;
  QStringList messages;  // what the plug-in said through gimp-message
  bool foreground_changed{false};
  double foreground[4]{0, 0, 0, 1};
  bool background_changed{false};
  double background[4]{1, 1, 1, 1};
};

// Blocks (pumping the event loop) until the helper reports a result or ends. Never throws.
[[nodiscard]] GimpPluginRunResult run_gimp_plugin_out_of_process(const GimpPluginRunInput& input,
                                                                 const GimpPluginRunCallbacks& callbacks);

}  // namespace patchy::ui
