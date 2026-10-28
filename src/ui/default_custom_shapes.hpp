#pragma once

#include "core/vector_shape.hpp"

#include <vector>

namespace patchy::ui {

// Code-generated builtin custom shapes (the bundled-asset rule: original
// geometry, never captured artwork). Paths are normalized to the unit box;
// ids are persisted in documents and settings, so they are append-only.
struct BuiltinCustomShape {
  const char* id;            // "shape.builtin.*"
  const char* english_name;  // translated for display via QObject context
  const char* english_folder;
  VectorPath path;
  // Default-library version that first shipped the shape: seeding adds only
  // shapes newer than the stored shapes/defaultShapesVersion stamp, so a
  // deleted default stays deleted (docs/vector-tools.md).
  int introduced_version{1};
};

// Bump when a builtin is added (and give it that introduced_version).
inline constexpr int kDefaultCustomShapesVersion = 1;

[[nodiscard]] const std::vector<BuiltinCustomShape>& builtin_custom_shapes();

}  // namespace patchy::ui
