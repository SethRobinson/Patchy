#pragma once

// GEGL filters (docs/gegl-filters.md). The scan asks patchy-gegl-host for every
// operation Patchy's own GEGL has, keeps the image filters
// Patchy can describe (gegl_operation_offered) and turns each into a FilterDefinition
// with the id "gegl.<operation>" in a registry of its own: Patchy's filter dialog,
// canvas preview, progress and Last Filter run them, and nothing persists them (no
// Smart Filters, Saved Looks or recipes). Each run starts the helper once.

#include "filters/filter_registry.hpp"

#include <QString>
#include <QStringList>

#include <map>
#include <memory>
#include <string>

class QJsonObject;
class QWidget;

namespace patchy::ui {

// How Patchy starts patchy-gegl-host. The helper's own --gpl3 and --list / --run follow
// `arguments`.
struct GeglHelperCommand {
  QString program;
  QStringList arguments;
  QString library;        // the libgegl the helper loads (the scan cache's key)
};

struct GeglFilterScanResult {
  QString library;        // the libgegl the helper loads; empty when none was found
  QString problem;        // why nothing is offered (translated); empty otherwise
  std::shared_ptr<const FilterRegistry> registry;  // the offered operations
  // Menu category of each filter id (GEGL's first category, as GEGL spells it).
  std::map<std::string, QString> categories;
  // Status tip text: GEGL's description of each operation.
  std::map<std::string, QString> descriptions;
  // GEGL's license key of each filter id: "GPL3+" for the GPL-3 module's operations, empty
  // for the LGPL rest.
  std::map<std::string, QString> licenses;
  QString version;        // GEGL's version ("0.4.72"); empty before a scan
  int operations{0};      // everything GEGL listed
};

inline constexpr char kGeglFilterIdPrefix[] = "gegl.";

[[nodiscard]] QString gegl_helper_path();
// Patchy's own GEGL (docs/gegl-filters.md, "Bundled GEGL"), shipped beside the program:
// <program folder>/gegl on Windows and in build trees, Contents/Frameworks/gegl in the macOS
// bundle, the library folder of an installed or Flatpak Patchy. Empty when the build has none.
[[nodiscard]] QString bundled_gegl_library();
// The helper's command line for Patchy's own GEGL.
[[nodiscard]] GeglHelperCommand bundled_gegl_command(const QString& library);
// Whether an operation from the helper's list is offered; `why` gets the reason it is not.
[[nodiscard]] bool gegl_operation_offered(const QJsonObject& operation, QString* why = nullptr);
// Lists the operations of `library` through the helper and builds the registry, with
// GEGL's labels in `language` (a Patchy language code; empty: English). The helper's list
// is cached (<AppData>/gegl-filters/operations.json) while GEGL's library, the helper and
// the language stay the same. Blocking; run it on a worker.
[[nodiscard]] GeglFilterScanResult scan_gegl_filters(const GeglHelperCommand& command, const QString& language = {});
// A GEGL category as a menu label: Patchy's translated name for the known ones, else
// GEGL's word capitalized ("edge-detect" -> "Edge Detect").
[[nodiscard]] QString gegl_category_label(const QString& category);
// The credit line of a GEGL filter's dialog ("Filter from GEGL 0.4.72 (gegl.org)", with
// ", GPL-3" for the GPL-3 module's operations).
[[nodiscard]] QString gegl_filter_credit(const GeglFilterScanResult& scan, const std::string& identifier);
// The folder holding the license texts of Patchy's own GEGL; empty when it is not found.
[[nodiscard]] QString gegl_licenses_folder();
// About GEGL (docs/gegl-filters.md, "Credit"): what GEGL is, who makes it, its licenses and
// where its source is. `scan` may be null (before a scan, or with no GEGL found).
void show_about_gegl(QWidget* parent, const GeglFilterScanResult* scan);
// Lets GIMP files apply their layer effects through Patchy's own GEGL
// (docs/xcf-layer-effects.md, "Layer effects (GIMP 3 filters)"): sets the XCF reader's effect
// runner to one helper run per effect. Clears it and returns false in a build without the
// bundle.
bool install_xcf_effect_runner();

}  // namespace patchy::ui
