#pragma once

// GEGL filters from the user's GIMP 3 (docs/gegl-filters.md). The scan asks
// patchy-gegl-host for every operation the user's GEGL has, keeps the image filters
// Patchy can describe (gegl_operation_offered) and turns each into a FilterDefinition
// with the id "gegl.<operation>" in a registry of its own: Patchy's filter dialog,
// canvas preview, progress and Last Filter run them, and nothing persists them (no
// Smart Filters, Saved Looks or recipes). Each run starts the helper once.

#include "filters/filter_registry.hpp"
#include "plugins/gimp/gimp_install.hpp"

#include <QString>
#include <QStringList>

#include <map>
#include <memory>
#include <string>

class QJsonObject;

namespace patchy::ui {

// How Patchy starts patchy-gegl-host for one GIMP install: directly, or inside GIMP's
// Flatpak (flatpak run --command=<helper> org.gimp.GIMP), where GEGL's libraries live.
// The helper's own --gpl3 and --list / --run follow `arguments`.
struct GeglHelperCommand {
  QString program;
  QStringList arguments;
  QString library;        // the libgegl the helper loads (a sandbox path inside GIMP's Flatpak)
  QString library_file;   // the same file as this process sees it (the scan cache's key)
  bool flatpak{false};
  bool bundled{false};    // Patchy's own GEGL: its modules sit beside the library
};

struct GeglFilterScanResult {
  QString library;        // the libgegl the helper loads; empty when none was found
  QString problem;        // why nothing is offered (translated); empty otherwise
  std::shared_ptr<const FilterRegistry> registry;  // the offered operations
  // Menu category of each filter id (GEGL's first category, as GEGL spells it).
  std::map<std::string, QString> categories;
  // Status tip text: GEGL's description of each operation.
  std::map<std::string, QString> descriptions;
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
// The GEGL library of a GIMP 3 install (inside GIMP's Flatpak: its sandbox path); empty
// when it has none Patchy can load. From Patchy's own Flatpak, GIMP's Flatpak is not used.
[[nodiscard]] QString find_gegl_library(const gimp::GimpInstall& install);
// The helper's command line for that install and library.
[[nodiscard]] GeglHelperCommand gegl_helper_command(const gimp::GimpInstall& install, const QString& library);
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

}  // namespace patchy::ui
