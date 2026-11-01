#pragma once

// The settings of a GIMP 3 procedure as Patchy's own dialog shows them
// (docs/gimp-plugins.md, "Settings dialog and canvas preview"): the arguments after
// GIMP's image-procedure trio (run mode, image, drawables), read from the procedure's
// registration, their values, and the PROC_RUN message that carries them to the helper.
// Labels and help come from the plug-in (data, in its own language).

#include "plugins/gimp/gimp_protocol.hpp"

#include <QByteArray>
#include <QColor>
#include <QString>
#include <QVariant>

#include <cstdint>
#include <map>
#include <vector>

namespace patchy::ui {

struct GimpPluginProcedure;

// The run's own layer as the plug-in sees it (src/plugins/gimp/host/image_model.hpp).
inline constexpr std::int32_t kGimpRunLayerId = 2;

enum class GimpArgumentWidget { Integer, Double, Boolean, Choice, Color, Text, Layer };

struct GimpPluginArgument {
  gimp::ParamDef def;
  QString key;    // the registered name ("num-segments"), the value's key
  QString label;  // the plug-in's nick, or the name
  QString help;   // the plug-in's blurb
  GimpArgumentWidget widget{GimpArgumentWidget::Integer};
};

// Values by argument key: int (Integer, Boolean as 0/1 is not used: bool), double,
// bool, QString (Choice nick, Text), QColor (Color), QString "layer" or "none" (Layer).
using GimpPluginValues = std::map<QString, QVariant>;

struct GimpPluginArgumentSet {
  std::vector<gimp::ParamDef> defs;  // every registered argument, in order
  std::vector<GimpPluginArgument> editable;
  // Why Patchy's dialog cannot hold this procedure's settings (it then opens the
  // plug-in's own dialog); empty when it can. English, for logs and tests.
  QString own_dialog_reason;
};

[[nodiscard]] GimpPluginArgumentSet gimp_plugin_arguments(const GimpPluginProcedure& entry);
[[nodiscard]] GimpPluginValues gimp_plugin_default_values(const GimpPluginArgumentSet& arguments);
// `stored` (from a previous run) fills the values it names that are still valid.
[[nodiscard]] GimpPluginValues gimp_plugin_merge_values(const GimpPluginArgumentSet& arguments,
                                                        const GimpPluginValues& stored);

// The PROC_RUN message for the helper's argument override, every argument after the
// trio in registration order. False with `error` set when a value does not fit.
bool encode_gimp_plugin_arguments(const GimpPluginProcedure& entry, const GimpPluginArgumentSet& arguments,
                                  const GimpPluginValues& values, QByteArray* message, std::uint32_t* count,
                                  QString* error);

// Values as JSON text for the per-procedure memory (gimpPlugins/lastValues/<procedure>).
[[nodiscard]] QString gimp_plugin_values_to_json(const GimpPluginValues& values);
[[nodiscard]] GimpPluginValues gimp_plugin_values_from_json(const QString& json);

}  // namespace patchy::ui
