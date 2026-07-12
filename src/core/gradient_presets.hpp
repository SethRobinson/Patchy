#pragma once

#include "core/layer.hpp"

#include <span>
#include <string_view>

namespace patchy {

struct BuiltinGradientPreset {
  const char *id;
  const char *english_name;
  const char *english_folder;
  int introduced_version;
  GradientDefinition definition;
};

[[nodiscard]] std::span<const BuiltinGradientPreset> builtin_gradient_presets();
[[nodiscard]] const BuiltinGradientPreset *
find_builtin_gradient_preset(std::string_view id);

} // namespace patchy
