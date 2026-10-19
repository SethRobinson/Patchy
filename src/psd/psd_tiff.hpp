#pragma once

#include "core/document.hpp"
#include "psd/psd_document_io.hpp"

#include <cstdint>
#include <filesystem>
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

// Full-resolution pages in the file (directories not marked as reduced-resolution
// previews), or 0 when the header or the first directory is unreadable.
[[nodiscard]] std::size_t tiff_page_count(std::span<const std::uint8_t> bytes) noexcept;

}  // namespace patchy::psd
