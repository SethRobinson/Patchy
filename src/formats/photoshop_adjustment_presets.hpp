#pragma once

#include "core/adjustment_layer.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

// Photoshop's Hue/Saturation (.ahu), Levels (.alv) and Black & White (.blw) preset
// files. Each holds the same payload as the matching PSD adjustment block (hue2, levl,
// blwh), so they share the PSD codec (docs/psd-layer-blocks.md, "Adjustment presets").
// Readers throw std::runtime_error for malformed data; writers go through a sibling
// temporary and a replacing rename.

namespace patchy::ahu {

[[nodiscard]] HueSaturationAdjustment read(std::span<const std::uint8_t> bytes);
[[nodiscard]] HueSaturationAdjustment read_file(const std::filesystem::path& path);
// The hue2 shape Photoshop writes for a fresh layer: header, six range records, trailer.
[[nodiscard]] std::vector<std::uint8_t> write(const HueSaturationAdjustment& settings);
void write_file(const std::filesystem::path& path, const HueSaturationAdjustment& settings);

}  // namespace patchy::ahu

namespace patchy::alv {

// The composite, red, green and blue records are read; the dialog's channel tab is not
// part of the file and comes back as the composite.
[[nodiscard]] LevelsAdjustment read(std::span<const std::uint8_t> bytes);
[[nodiscard]] LevelsAdjustment read_file(const std::filesystem::path& path);
// Version 2 and 29 records, as in the levl block.
[[nodiscard]] std::vector<std::uint8_t> write(const LevelsAdjustment& settings);
void write_file(const std::filesystem::path& path, const LevelsAdjustment& settings);

}  // namespace patchy::alv

namespace patchy::blw {

[[nodiscard]] BlackWhiteAdjustment read(std::span<const std::uint8_t> bytes);
[[nodiscard]] BlackWhiteAdjustment read_file(const std::filesystem::path& path);
// Descriptor version 16 and Photoshop 2026's blwh descriptor with the modeled values.
[[nodiscard]] std::vector<std::uint8_t> write(const BlackWhiteAdjustment& settings);
void write_file(const std::filesystem::path& path, const BlackWhiteAdjustment& settings);

}  // namespace patchy::blw
