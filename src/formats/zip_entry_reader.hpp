#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Reads entries out of an in-memory zip for the zip-based layered formats (OpenRaster,
// Krita), through the vendored miniz reader (Zip64 included). Every size a directory
// claims is checked before anything is allocated for it (docs/ora.md "Limits"). Errors
// throw std::runtime_error carrying one of the caller's messages, which the caller marks
// for translation (PATCHY_TRANSLATE_NOOP) so each format names itself.
namespace patchy::formats {

struct ZipReaderMessages {
  const char* damaged_directory;  // not a zip, or its central directory is cut short
  const char* too_large;          // past an entry, total or entry-count cap
  const char* unsupported;        // encryption or an unknown compression method
  const char* damaged_entry;      // an entry that does not unpack
};

struct ZipReaderLimits {
  std::uint64_t max_entry_bytes{0};      // one extracted entry
  std::uint64_t max_total_extracted{0};  // everything extracted through one reader
  std::size_t max_entries{0};
};

class ZipEntryReader {
public:
  ZipEntryReader(std::span<const std::uint8_t> bytes, const ZipReaderMessages& messages, ZipReaderLimits limits);
  ~ZipEntryReader();
  ZipEntryReader(const ZipEntryReader&) = delete;
  ZipEntryReader& operator=(const ZipEntryReader&) = delete;

  // Names match case-sensitively.
  [[nodiscard]] bool contains(const std::string& name);
  [[nodiscard]] std::string first_entry_name();
  // Every file entry's name in directory order (directories left out).
  [[nodiscard]] std::vector<std::string> entry_names();
  // nullopt when the entry is missing or a directory; throws when it is damaged or past
  // `limit` or the reader's caps.
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> extract(const std::string& name, std::uint64_t limit);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// A name inside the archive that is safe to look up: no parent steps, no absolute or drive
// paths, no backslashes.
[[nodiscard]] bool safe_zip_entry_path(std::string_view path);

}  // namespace patchy::formats
