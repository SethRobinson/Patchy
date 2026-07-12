#pragma once

#include "core/layer.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace patchy::psd {

struct GrdGradient {
  std::string name;
  std::string folder;
  GradientDefinition definition;
};

struct GrdReadResult {
  std::vector<GrdGradient> gradients;
  std::vector<std::string> warnings;
};

[[nodiscard]] std::optional<GrdReadResult>
read_grd(std::span<const std::uint8_t> bytes, std::string &error);
[[nodiscard]] std::vector<std::uint8_t>
write_grd(std::span<const GrdGradient> gradients);

} // namespace patchy::psd
