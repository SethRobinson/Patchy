#pragma once

// Explains why an image file the decoders rejected could not be opened, from its bytes
// alone (GitHub issue 117). Qt's QImageReader reports every failure as one of two generic
// strings ("Unable to read image data", "Unsupported image format"); this probe names the
// real reason for the common ones: an empty file, text saved under an image name, another
// format's bytes behind the extension, a JPEG cut short or using a coding process the
// decoder lacks, a TIFF compressed with a codec the bundled libtiff was built without.
// Never throws and never decodes pixels.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace patchy::formats {

enum class UndecodableImageReason {
  Unknown,                     // nothing recognizable: keep the decoder's own message
  EmptyFile,                   // 0 bytes
  TextContent,                 // HTML, XML or plain text saved under an image name
  DifferentFormat,             // `detail` names what the bytes really are
  UnsupportedFormat,           // the bytes match the extension, and Patchy has no decoder; `detail` names it
  JpegTruncated,               // the stream ends before its EOI marker, or a segment runs past the end
  JpegNoFrame,                 // no frame header before the scan data, or no JPEG header at all
  JpegLossless,                // SOF3/SOF7/SOF11/SOF15
  JpegHierarchical,            // SOF5/SOF6/SOF13/SOF14
  Jpeg12Bit,                   // sample precision other than 8
  TiffJpegCompression,         // compression 6 or 7: the bundled libtiff has no JPEG codec
  TiffUnsupportedCompression,  // another codec that libtiff lacks; `detail` names it
  TiffUnsupportedBitDepth,     // bits per sample outside what Qt's TIFF reader decodes; `detail` is the count
  TiffIntegerSamples,          // 32-bit samples that are integers (Qt reads 32-bit only as float)
  TiffPackedColor,             // 1, 2 or 4-bit samples on a color image (Qt packs only grayscale); `detail` is the count
  TiffUnreadable,              // no TIFF header, or the first directory cannot be parsed
};

struct UndecodableImageDiagnosis {
  UndecodableImageReason reason{UndecodableImageReason::Unknown};
  // DifferentFormat: the detected format's display name ("PNG", "HEIC", "JPEG 2000").
  // TiffUnsupportedCompression: the codec's name ("LZMA", "ZSTD", "WebP", "JBIG").
  // TiffUnsupportedBitDepth, TiffPackedColor: the bits per sample as decimal text.
  std::string detail;
  // DifferentFormat only: the extension that format normally carries, when Patchy opens
  // it ("png", "heic"); empty for formats Patchy cannot open (JPEG 2000, archives).
  std::string suggested_extension;
};

// `extension` is the file's own extension without the dot, in any case ("jpg", "TIF").
[[nodiscard]] UndecodableImageDiagnosis diagnose_undecodable_image(std::span<const std::uint8_t> bytes,
                                                                   std::string_view extension) noexcept;

// A format recognized from a file's signature alone, for opening a file by its content
// when its name says something else (GitHub issue 362).
struct SniffedImageFormat {
  std::string name;       // display name ("Photoshop", "PNG")
  std::string extension;  // the extension Patchy opens the format under ("psd", "png")
};

// The format the bytes start with, when Patchy can open it; nullopt for unrecognized bytes
// and for formats Patchy cannot open (JPEG 2000, archives). Looks at the first kilobyte only.
[[nodiscard]] std::optional<SniffedImageFormat> sniff_openable_image_format(std::span<const std::uint8_t> bytes) noexcept;

// True when two extensions (no dot, any case) name the same container: "jpg" and "JPEG",
// "tif" and "tiff", "psd" and "psb", "png" and "apng".
[[nodiscard]] bool extensions_share_format(std::string_view a, std::string_view b) noexcept;

}  // namespace patchy::formats
