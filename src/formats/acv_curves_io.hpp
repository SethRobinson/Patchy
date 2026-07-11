#pragma once

#include "core/adjustment_layer.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace patchy::acv {

// Reads Photoshop Curves preset data. Version 4 counted files and version 1
// bitmap files (including Photoshop's 32-bit bitmap variant and optional
// indexed `Crv ` extension) are accepted. Only Composite RGB, Red, Green, and
// Blue are mapped into Patchy's RGB model; any additional valid channel
// records are checked and ignored. Throws std::runtime_error for malformed or
// unsupported data.
[[nodiscard]] CurvesAdjustment read(std::span<const std::uint8_t> bytes);
[[nodiscard]] CurvesAdjustment read_file(const std::filesystem::path& path);

// Writes Photoshop's current RGB preset shape: version 4, five counted
// curves in Composite/Red/Green/Blue/reserved order. The reserved fifth curve
// is identity, matching Photoshop 2026. Throws if the model violates the ACV
// limits (2-19 points, byte coordinates, strictly increasing inputs).
[[nodiscard]] std::vector<std::uint8_t> write(const CurvesAdjustment& curves);
void write_file(const std::filesystem::path& path, const CurvesAdjustment& curves);

}  // namespace patchy::acv
