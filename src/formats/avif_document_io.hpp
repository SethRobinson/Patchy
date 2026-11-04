#pragma once

#include "formats/format_registry.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::avif {

// AVIF (AV1 Image File Format, .avif/.avifs): read on every native platform through
// the vendored libavif 1.4.2 over the dav1d 1.5.4 AV1 decoder (src/formats/libavif,
// src/formats/dav1d; both BSD-2-Clause; cmake/patchy_avif.cmake). Design in docs/avif.md,
// the licensing record in docs/legal-licensing.md. The browser build compiles only the
// stub: is_available() is false there and read_avif() throws.
//
// The primary image opens as an 8-bit RGBA document, or 16-bit when the file stores 10 or
// 12-bit samples and deep editing is on. The clean aperture, rotation and mirror properties
// (clap, irot, imir) are applied in that order; an embedded ICC profile is kept on the
// document's color state. An image sequence (animated AVIF) becomes one full-canvas layer
// per frame, frame 1 on top, with the repetition count in kLoopCountMetadata. Writing goes
// through the vendored libaom 3.15.2 encoder (src/formats/libaom, BSD-2 plus the AOM patent
// grant): the flattened document as one 4:4:4 still, 8-bit or 12-bit samples.

inline constexpr const char* kLoopCountMetadata = "avif.loop_count";

// Lowercase extensions (no dot); the single source of truth for the registry, the dialog
// filter and the Windows "Open with" list.
[[nodiscard]] const std::vector<std::string>& avif_extensions();
[[nodiscard]] bool is_avif_extension(std::string_view extension);

// ISOBMFF ftyp whose major or compatible brand is avif or avis. HEVC HEIF brands belong to
// heif::sniff, never here.
[[nodiscard]] bool sniff(std::span<const std::uint8_t> bytes) noexcept;

// False in the browser build, where neither AV1 codec is compiled in.
[[nodiscard]] bool is_available() noexcept;

struct ReadOptions {
  // Names an animation frame layer; the UI passes translated "Frame N" text. Unset, frames
  // are named "Frame N <delay>s" in English.
  std::function<std::string(int index, std::uint32_t delay_ms)> frame_name;
};

struct ReadResult {
  FormatReadResult read;
  // True when the file carries an ICC profile: libavif hands over the file's own color
  // values, so that profile still describes them and is kept for saves.
  bool pixels_in_source_space{false};
  // The container's irot/imir properties were applied; the AVIF rules make them replace any
  // EXIF orientation, so the tag is written back as "normal".
  bool orientation_applied{true};
  std::uint32_t frame_count{1};
};

// Throws std::runtime_error with a PATCHY_TRANSLATE_NOOP message on a damaged, unsupported
// or oversized file; libavif's own English reason, when it gave one, follows a colon.
[[nodiscard]] ReadResult read_avif_detailed(std::span<const std::uint8_t> bytes, const ReadOptions& options = {});
// The registry's FormatReadFn.
[[nodiscard]] FormatReadResult read_avif(std::span<const std::uint8_t> bytes);

struct WriteOptions {
  // 0-100 on libavif's quality scale; ignored when lossless.
  int quality{80};
  // The identity matrix at quality 100: 8-bit pixels come back exactly.
  bool lossless{false};
  // libavif's encoder speed, 0 (slowest, smallest) to 10 (fastest); 6 is avifenc's default.
  int speed{6};
  // Written as Exif and XMP items when non-empty. `exif` is a TIFF stream.
  std::vector<std::uint8_t> exif;
  std::vector<std::uint8_t> xmp;
};

// Encodes the flattened document as one 4:4:4 AVIF still: 8-bit documents as 8-bit samples,
// 16-bit documents as 12-bit samples (the format's maximum), 32-bit documents through the
// 8-bit flatten. Alpha is written only when some pixel is not opaque; an RGB ICC profile is
// embedded, otherwise the sRGB nclx values. Throws std::runtime_error with a
// PATCHY_TRANSLATE_NOOP message, libavif's reason after a colon.
[[nodiscard]] std::vector<std::uint8_t> write_avif(const Document& document, const WriteOptions& options);
// The registry's FormatWriteFn: default options.
[[nodiscard]] std::vector<std::uint8_t> write_avif(const Document& document);

}  // namespace patchy::avif
