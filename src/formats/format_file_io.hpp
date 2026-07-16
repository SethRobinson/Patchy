#pragma once

// Shared whole-file read/write plumbing for the flat file-format DocumentIo
// implementations (BMP, TGA, PCX, ICO, IFF ILBM, Aseprite, GIF). Each format
// passes its display name so the historical error strings stay byte-identical
// ("Could not open BMP file", ...). Internal to src/formats - do not include
// this header elsewhere.

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace patchy {
class Document;
}

namespace patchy::formats {

// Read the whole file into bytes; throws
// std::runtime_error("Could not open <format_name> file").
[[nodiscard]] std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path,
                                                        std::string_view format_name);

// Rename the document's first layer to the file stem (no-op for empty stems or
// layerless documents) - the shared open-file behavior for flat single-image
// formats.
void rename_first_layer_to_stem(Document& document, const std::filesystem::path& path);

// Write bytes to path; throws
// std::runtime_error("Could not open <format_name> file for writing") when the
// file cannot be created and ("Could not write <format_name> file") when the
// write fails.
void write_file_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes,
                      std::string_view format_name);

}  // namespace patchy::formats
