#pragma once

#include "formats/pdf_syntax.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// PDF stream filters (ISO 32000-1 clause 7.4). Qt-free; Flate goes through the
// miniz already vendored for the Aseprite reader.
//
// The image filters (DCTDecode, JPXDecode, CCITTFaxDecode, JBIG2Decode) are NOT
// decoded here. Their output is compressed image data that Qt decodes far better
// than we could, and the importer hands those streams to the Qt side still encoded,
// so nothing is transcoded on the way in.

namespace patchy::pdf {

enum class FilterKind {
  None,
  Flate,
  Lzw,
  Ascii85,
  AsciiHex,
  RunLength,
  Crypt,  // an identity pass-through; real decryption happens before filtering
  // Image codecs: recognized, never decoded here.
  Dct,
  Jpx,
  CcittFax,
  Jbig2,
  Unknown,
};

[[nodiscard]] FilterKind filter_kind_from_name(std::string_view name) noexcept;
// True for the codecs whose output is an encoded image rather than raw bytes, i.e.
// the ones a decode chain must stop at and hand off.
[[nodiscard]] bool filter_is_image_codec(FilterKind kind) noexcept;
// The file extension Qt needs to recognize an image codec's bytes ("jpg", "jp2"),
// or an empty view for anything else.
[[nodiscard]] std::string_view image_codec_extension(FilterKind kind) noexcept;

struct FilterStep {
  FilterKind kind{FilterKind::None};
  std::string name;
  Dictionary parms;  // the matching /DecodeParms entry, already resolved
};

struct DecodeResult {
  std::vector<std::uint8_t> data;
  // Set when the chain stopped at an image codec: `data` is that codec's still
  // encoded bytes and the caller must decode them as an image.
  FilterKind image_codec{FilterKind::None};
  // Human-readable reason the decode stopped early, empty on success.
  std::string error;

  [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

// Runs one filter. `parms` supplies /Predictor, /Colors, /BitsPerComponent,
// /Columns and, for LZW, /EarlyChange.
[[nodiscard]] DecodeResult apply_filter(FilterKind kind, std::span<const std::uint8_t> data, const Dictionary& parms);

// Runs a whole /Filter chain in order, stopping at the first image codec.
[[nodiscard]] DecodeResult apply_filter_chain(std::span<const std::uint8_t> data, const std::vector<FilterStep>& steps);

// Individual codecs, exposed for testing.
[[nodiscard]] DecodeResult inflate_bytes(std::span<const std::uint8_t> data);
[[nodiscard]] DecodeResult decode_lzw(std::span<const std::uint8_t> data, bool early_change);
[[nodiscard]] DecodeResult decode_ascii85(std::span<const std::uint8_t> data);
[[nodiscard]] DecodeResult decode_ascii_hex(std::span<const std::uint8_t> data);
[[nodiscard]] DecodeResult decode_run_length(std::span<const std::uint8_t> data);

// Undoes the PNG (predictor >= 10) or TIFF (predictor 2) prediction Flate and LZW
// streams may carry. Returns the input unchanged for predictor 1 or none.
[[nodiscard]] DecodeResult undo_predictor(std::vector<std::uint8_t> data, int predictor, int colors,
                                          int bits_per_component, int columns);

}  // namespace patchy::pdf
