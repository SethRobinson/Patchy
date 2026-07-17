#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace patchy::svg {

class DocumentIo {
public:
  // Imports .svg/.svgz as editable shape layers (groups -> folders,
  // rect/circle/ellipse/line -> live shapes, gradients/simple patterns ->
  // vector fills, clip paths -> vector masks, basic text -> text layers with
  // a post-open render marker). Throws for files past the supported subset;
  // the UI's QImageReader fallback then rasterizes them.
  [[nodiscard]] static Document read(std::span<const std::uint8_t> bytes,
                                     std::vector<std::string>* notices = nullptr);
  // Exports vector shape layers as real SVG vectors; everything SVG cannot
  // composite per-element (adjustment spans, clipping runs, unmapped blend
  // modes, effects) flattens into embedded PNG <image> chunks, reported via
  // notices.
  [[nodiscard]] static std::vector<std::uint8_t> write(const Document& document,
                                                       std::vector<std::string>* notices = nullptr);
  static void write_file(const Document& document, const std::filesystem::path& path,
                         std::vector<std::string>* notices = nullptr);
};

// Includes the leading dot, matching the format-registry convention. SVGZ is
// read-only; the UI advertises only .svg when saving.
[[nodiscard]] const std::vector<std::string>& svg_extensions();
[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes) noexcept;

}  // namespace patchy::svg
