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
};

[[nodiscard]] const std::vector<BuiltinCustomShape>& builtin_custom_shapes();

}  // namespace patchy::ui
