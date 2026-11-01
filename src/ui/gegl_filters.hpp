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
// The GEGL library of a GIMP 3 install; empty when it has none Patchy can load (GIMP's
// Flatpak keeps its libraries inside its sandbox).
[[nodiscard]] QString find_gegl_library(const gimp::GimpInstall& install);
// Whether an operation from the helper's list is offered; `why` gets the reason it is not.
[[nodiscard]] bool gegl_operation_offered(const QJsonObject& operation, QString* why = nullptr);
// Lists the operations of `library` through the helper and builds the registry.
// Blocking; run it on a worker.
[[nodiscard]] GeglFilterScanResult scan_gegl_filters(const QString& library);
// "Edge-detect" -> "Edge Detect": a GEGL category as a menu label.
[[nodiscard]] QString gegl_category_label(const QString& category);

}  // namespace patchy::ui
