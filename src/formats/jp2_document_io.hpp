#pragma once

#include "formats/format_registry.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::jp2 {

// JPEG 2000 (.jp2 wrapped, .j2k/.j2c/.jpc raw codestreams, .jpx/.jpf): ISO/IEC 15444-1
// Part 1 through the vendored OpenJPEG 2.5.3 (src/formats/openjpeg, BSD-2-Clause;
// docs/hdr-formats.md and the licensing record in docs/legal-licensing.md). Reads the
// first image: gray, gray+alpha, RGB(A), sYCC (converted to RGB, chroma upsampled) and
// CMYK (naive ink mix) at 1 to 16 bits per component, 8-bit components opening as an 8-bit
// document and deeper ones as 16-bit. Writes the flattened document as a lossless (5/3
// reversible) JP2 at the document's integer depth, RGB(A) or grayscale.

[[nodiscard]] const std::vector<std::string>& jp2_extensions();
[[nodiscard]] bool is_jp2_extension(std::string_view extension);
[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes) noexcept;

// Throws std::runtime_error with a PATCHY_TRANSLATE_NOOP message on a damaged or
// unsupported file; OpenJPEG's own English reason, when it gave one, follows a colon.
[[nodiscard]] FormatReadResult read_jp2(std::span<const std::uint8_t> bytes);

[[nodiscard]] std::vector<std::uint8_t> write_jp2(const Document& document);

}  // namespace patchy::jp2
