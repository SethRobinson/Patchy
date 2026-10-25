#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Document-level file metadata (File > File Info; docs/file-info.md): what was read
// from the source file and is written back on save. Qt-free and format-neutral; the
// parsers, editors and container writers live in src/formats/metadata. Bulk payloads
// are shared and immutable so undo snapshots copy pointers, not bytes.
namespace patchy {

using SharedMetadataBytes = std::shared_ptr<const std::vector<std::uint8_t>>;

// Why an item deserves attention before a file is shared. Persisted only through
// the scripting API's string ids (src/formats/metadata/metadata_privacy.hpp).
enum class MetadataPrivacy : std::uint8_t {
  None,
  Location,        // GPS coordinates, IPTC location fields
  DeviceIdentity,  // body/lens serial numbers, owner name, MakerNote
  Personal,        // author, creator contact details
  HiddenImage,     // embedded thumbnails and secondary images (may predate edits)
  EditHistory,     // XMP history, document ancestors, cached text-layer contents
  MachineInfo,     // software, host computer, local file paths
  Timestamp,       // capture and edit dates (time-zone offsets hint at location)
  AiPrompt,        // generator prompts, workflows, digital source type
  Provenance,      // C2PA / Content Credentials
  Watermark,       // a watermark declared in metadata or found in the pixels
};

enum class FileInfoTextKind : std::uint8_t {
  PngText,           // tEXt (Latin-1)
  PngCompressedText, // zTXt
  PngUnicodeText,    // iTXt
  Comment,           // JPEG COM, GIF Comment Extension
  PdfInfo,           // PDF /Info entry (read only; the writer derives /Info from XMP)
  SvgText,           // SVG <title>/<desc> (read only)
};

struct FileInfoText {
  FileInfoTextKind kind{FileInfoTextKind::PngText};
  std::string key;       // PNG keyword, PDF key, SVG element; empty for comments
  std::string value;     // UTF-8
  std::string language;  // iTXt language tag
};

// A PNG chunk carried as raw bytes (color-rendering chunks such as sRGB/gAMA/cHRM/cICP).
struct FileInfoChunk {
  std::array<char, 4> type{};
  std::vector<std::uint8_t> data;
};

// Something the source held that saves do not carry (C2PA manifests, MPF secondary
// images, embedded thumbnails outside EXIF, linked profile paths), kept for display.
struct FileInfoObserved {
  std::string container;  // "JPEG APP11", "PNG caBX", "HEIF item thmb"
  std::string label;      // "C2PA manifest", "Thumbnail"
  std::string summary;    // human-readable details
  std::uint64_t size{0};
  MetadataPrivacy privacy{MetadataPrivacy::None};
  bool is_thumbnail{false};  // `bytes` holds an encoded image File Info can show
  SharedMetadataBytes bytes;
};

// One segment/chunk/box/resource of the source file, in file order (File Info's File page).
struct FileInfoSegment {
  std::string name;  // "APP1 Exif", "IDAT", "8BIM 1060 XMP metadata"
  std::uint64_t offset{0};
  std::uint64_t size{0};
};

struct DocumentFileInfo {
  std::string source_format;  // "JPEG", "PNG", "PSD", ... empty for new documents
  std::uint64_t source_file_size{0};

  // Canonical metadata blocks (written back on save).
  SharedMetadataBytes xmp;   // XMP packet, UTF-8
  SharedMetadataBytes exif;  // TIFF stream ("II*\0"/"MM\0*"), no "Exif\0\0" prefix
  SharedMetadataBytes iptc;  // IPTC-IIM datasets
  // Photoshop resource 1061 as read: MD5 of the IIM when it was last written by an
  // XMP-aware app. File Info recomputes it whenever it rewrites the IIM.
  std::optional<std::array<std::uint8_t, 16>> iptc_digest;
  std::optional<bool> copyright_flag;  // PSD 1034
  std::string copyright_url;           // PSD 1035
  std::vector<FileInfoText> text;      // comments, PNG text chunks, PDF /Info, SVG title/desc
  // Other metadata-only Photoshop image resources (thumbnails 1033/1036, version
  // info 1057, EXIF 3 1059, print info 1084/1085, auto-save 1086/1087, workflow URL
  // 1051, Lightroom 8000), written back verbatim by the PSD writer.
  std::vector<std::pair<std::uint16_t, SharedMetadataBytes>> psd_resources;
  std::vector<FileInfoChunk> png_color_chunks;
  // The ICC profile the source carried, whatever its color space (display only; the
  // profile Patchy writes is DocumentColorState::embedded_icc_profile).
  SharedMetadataBytes source_icc;
  std::string source_icc_note;  // why the source profile was not kept, if it was not
  std::vector<FileInfoObserved> observed;
  std::vector<FileInfoSegment> structure;
  // Strip All Metadata ran on this document since it was opened: a PSD save stamp then
  // adds no EXIF block of its own (docs/file-info.md, "Save stamp"). Never written to a file.
  bool stripped{false};

  [[nodiscard]] bool has_metadata() const noexcept {
    return (xmp && !xmp->empty()) || (exif && !exif->empty()) || (iptc && !iptc->empty()) || copyright_flag ||
           !copyright_url.empty() || !text.empty() || !psd_resources.empty() || !observed.empty();
  }
};

[[nodiscard]] inline SharedMetadataBytes make_metadata_bytes(std::vector<std::uint8_t> bytes) {
  return std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
}

}  // namespace patchy
