#pragma once

#include "core/document.hpp"
#include "psd/psd_document_io.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

// Layered TIFF: Photoshop's layers in TIFF tag 37724 (ImageSourceData), beside the flattened
// composite in the first directory (docs/tiff.md). The tag holds "Adobe Photoshop Document
// Data Block\0" followed by PSD-style tagged blocks ('Layr'/'Lr16'/'Lr32' layer info,
// 'LMsk', 'Patt', ...) in the TIFF's byte order. Reading rebuilds those blocks into a PSD
// stream (header from the TIFF's first directory, image resources from tag 34377) and
// parses it with the PSD reader, so layered TIFFs get every layer feature PSDs get. Qt-free.
namespace patchy::psd {

// True when the first directory carries tag 37724 with Photoshop's document data header.
[[nodiscard]] bool tiff_has_photoshop_layers(std::span<const std::uint8_t> bytes) noexcept;

// Opens the layers. Throws std::runtime_error naming the problem when the layer data is
// missing, damaged or in a layout Patchy does not read (another color mode, BigTIFF, an
// empty layer list); the caller then opens the composite the way it always has. Notices
// about converted or dropped data go to options.notices.
[[nodiscard]] Document read_layered_tiff(std::span<const std::uint8_t> bytes, ReadOptions options = {});

[[nodiscard]] Document read_layered_tiff_file(const std::filesystem::path& path, ReadOptions options = {});

// The PSD stream read_layered_tiff parses, for tests and diagnostics.
[[nodiscard]] std::vector<std::uint8_t> photoshop_document_from_tiff(std::span<const std::uint8_t> bytes,
                                                                     std::vector<std::string>* notices = nullptr);

struct LayeredTiffWriteOptions {
  // Called on the writing thread as the PSD writer advances (WriteOptions::progress).
  std::function<void(int done, int total, const std::string& label)> progress{};
};

// A big-endian ("Macintosh") TIFF that Photoshop opens with its layers: the flattened
// composite in IFD0 (Adobe Deflate with horizontal differencing, 8/16-bit or 32-bit float,
// unassociated alpha, saved channels as further extra samples), the PSD writer's layer
// data in tag 37724 and its image resources in tag 34377, minus the metadata resources a
// TIFF keeps in its own tags (the ICC profile goes to 34675; XMP, IPTC and EXIF are File
// Info's to inject). Always RGB. Throws std::runtime_error when the document cannot be
// written (more than 4 GB, too many layer records).
[[nodiscard]] std::vector<std::uint8_t> write_layered_tiff(const Document& document,
                                                           const LayeredTiffWriteOptions& options = {});
// The same through a sibling temporary and a replacing rename.
void write_layered_tiff_file(const Document& document, const std::filesystem::path& path,
                             const LayeredTiffWriteOptions& options = {});

// Full-resolution pages in the file (directories not marked as reduced-resolution
// previews), or 0 when the header or the first directory is unreadable.
[[nodiscard]] std::size_t tiff_page_count(std::span<const std::uint8_t> bytes) noexcept;

// The directory offsets of those pages, in file order (page 1 first); empty when the
// header or the first directory is unreadable.
[[nodiscard]] std::vector<std::uint32_t> tiff_page_directory_offsets(std::span<const std::uint8_t> bytes);

// One full-resolution page of a multi-page TIFF (docs/tiff.md, "Multi-page TIFF").
struct TiffPage {
  std::uint32_t directory_offset{0};
  // The directory's place in the file's chain counting every directory, reduced-resolution
  // previews included: the index libtiff's TIFFSetDirectory (Qt's jumpToImage) takes.
  std::size_t directory_index{0};
  std::uint32_t width{0};
  std::uint32_t height{0};
  // The PageName tag (285), empty when the page has none.
  std::string name;
};

// The file's pages in file order (page 1 first), the same set tiff_page_directory_offsets
// lists; empty when the header or the first directory is unreadable.
[[nodiscard]] std::vector<TiffPage> tiff_pages(std::span<const std::uint8_t> bytes);

// One decoded page for tiff_pages_as_layers: a flat document (one pixel layer) at the
// page's own size and depth.
struct TiffPageDocument {
  Document document;
  std::string layer_name;
};

// The pages as one document (docs/tiff.md, "Multi-page TIFF"): a canvas of the largest
// width and height among them, one pixel layer per page at the top left, page 1 on top and
// every layer visible (the GIF/APNG/WebP frame order), at the highest depth among the
// pages (the others converted). Resolution and color state come from page 1. Saved
// channels are left out, with one notice. Throws std::invalid_argument for no pages.
[[nodiscard]] Document tiff_pages_as_layers(std::vector<TiffPageDocument> pages,
                                            std::vector<std::string>* notices = nullptr);

struct FlatTiffReadOptions {
  // The image file directory to decode; 0 means the first one (page 1).
  std::uint32_t directory_offset{0};
  std::string layer_name{"Background"};
  // Notices about damaged data or extra samples left out (PATCHY_TRANSLATE_NOOP text).
  std::vector<std::string>* notices{nullptr};
};

// Flat TIFF fallback (docs/tiff.md, "Flat fallback decoder") for files Qt's TIFF plug-in
// refuses: little or big endian, chunky or planar, strips or tiles; no compression,
// PackBits, LZW or Deflate with predictor 1, 2 or 3; gray, RGB or palette color; 8, 12, 16
// or 32-bit unsigned integers and 16, 32 or 64-bit floats. 8-bit samples open as an 8-bit
// document, 12, 16 and 32-bit integers as 16-bit, floats as a 32-bit (linear) one. The first
// associated or unassociated extra sample becomes the layer's alpha (associated alpha is
// unpremultiplied); every other extra sample becomes a saved alpha channel. Throws
// std::runtime_error for layouts outside that set and for damaged structure.
[[nodiscard]] Document read_flat_tiff(std::span<const std::uint8_t> bytes, const FlatTiffReadOptions& options = {});
[[nodiscard]] Document read_flat_tiff_file(const std::filesystem::path& path, const FlatTiffReadOptions& options = {});

}  // namespace patchy::psd
