#pragma once

// Internal to the .xcf reader (src/formats/xcf_*.cpp; docs/xcf.md): shared between the
// structure reader, the text-layer mapping and the container unwrapping. Do not include
// it elsewhere.

#include "core/layer.hpp"
#include "formats/xcf_document_io.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::xcf::detail {

// A structural read failure. Its text never reaches users (DocumentIo::read turns it into
// a translated message); PATCHY_XCF_TRACE prints it.
class ImportError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// A well-formed file past the size limits: its own message, not "damaged".
class SizeLimitError : public ImportError {
public:
  using ImportError::ImportError;
};

void trace(const std::string& message);

// Decompressed-size ceiling for a wrapped file and for the bzip2/xz decoders.
inline constexpr std::uint64_t kMaxUnwrappedBytes = 16ULL << 30U;

// bzip2 and xz streams, Patchy's own decoders (xcf_unwrap.cpp), up to `limit` bytes.
// Throw ImportError on damage and SizeLimitError past the limit.
[[nodiscard]] std::vector<std::uint8_t> decompress_bzip2(std::span<const std::uint8_t> bytes, std::uint64_t limit);
[[nodiscard]] std::vector<std::uint8_t> decompress_xz(std::span<const std::uint8_t> bytes, std::uint64_t limit);
[[nodiscard]] std::vector<std::uint8_t> decompress_gzip(std::span<const std::uint8_t> bytes, std::uint64_t limit);

// What the text mapping needs from the reader.
struct TextLayerContext {
  double ppi{72.0};
  std::int32_t layer_width{0};
  std::int32_t layer_height{0};
};

// Writes the patchy.text.* metadata for a GIMP text layer onto `layer` (its pixels stay
// GIMP's raster). Returns false when the parasite holds nothing Patchy can edit.
[[nodiscard]] bool apply_text_parasite(const TextParasite& parasite, const TextLayerContext& context, Layer& layer);

}  // namespace patchy::xcf::detail
