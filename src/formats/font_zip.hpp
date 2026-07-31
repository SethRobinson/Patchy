#pragma once

// Extracts font files (.ttf/.otf/.ttc) from an in-memory zip archive, for the
// drag-a-zip-of-fonts feature. Deterministic: entries come back in archive
// order, and name collisions dedupe with a numeric suffix. Defensive by
// default: directories, __MACOSX resource forks, dotfiles, non-font
// extensions, empty entries, and entries over the size caps are skipped, and
// the caps bound the total allocation against zip bombs.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace patchy::formats {

struct ZipFontEntry {
  std::string name;  // sanitized basename, unique within the result
  std::vector<std::uint8_t> bytes;
};

struct ZipFontExtractLimits {
  std::size_t max_file_bytes = 64u * 1024 * 1024;
  std::size_t max_total_bytes = 256u * 1024 * 1024;
  std::size_t max_entries = 256;
};

// Returns the font entries of the archive. A malformed archive returns an
// empty list and sets *error (when provided); individual undecodable entries
// are skipped without failing the archive.
[[nodiscard]] std::vector<ZipFontEntry> extract_font_files_from_zip(
    const std::uint8_t* data, std::size_t size, const ZipFontExtractLimits& limits = {},
    std::string* error = nullptr);

}  // namespace patchy::formats
