#include "formats/font_zip.hpp"

#include "formats/miniz/miniz.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace patchy::formats {

namespace {

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

bool has_font_extension(const std::string& name) {
  const auto dot = name.rfind('.');
  if (dot == std::string::npos) {
    return false;
  }
  const auto extension = lowercase(name.substr(dot + 1));
  return extension == "ttf" || extension == "otf" || extension == "ttc";
}

// Basename with Windows-hostile characters replaced, so the result is safe to
// write into either persistence store as-is.
std::string sanitized_basename(const std::string& entry_path) {
  const auto separator = entry_path.find_last_of("/\\");
  auto name = separator == std::string::npos ? entry_path : entry_path.substr(separator + 1);
  for (auto& ch : name) {
    if (ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '|' || ch == '?' || ch == '*' ||
        static_cast<unsigned char>(ch) < 0x20) {
      ch = '_';
    }
  }
  return name;
}

bool is_macos_resource_fork_path(const std::string& entry_path) {
  return entry_path.rfind("__MACOSX/", 0) == 0 || entry_path.find("/__MACOSX/") != std::string::npos;
}

// "Fira.ttf" -> "Fira (2).ttf" (then "(3)", ...) until unused.
std::string deduplicated_name(const std::string& name, const std::unordered_set<std::string>& used) {
  if (used.count(lowercase(name)) == 0) {
    return name;
  }
  const auto dot = name.rfind('.');
  const auto stem = dot == std::string::npos ? name : name.substr(0, dot);
  const auto extension = dot == std::string::npos ? std::string() : name.substr(dot);
  for (int counter = 2;; ++counter) {
    const auto candidate = stem + " (" + std::to_string(counter) + ")" + extension;
    if (used.count(lowercase(candidate)) == 0) {
      return candidate;
    }
  }
}

}  // namespace

std::vector<ZipFontEntry> extract_font_files_from_zip(const std::uint8_t* data, std::size_t size,
                                                      const ZipFontExtractLimits& limits,
                                                      std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  std::vector<ZipFontEntry> entries;
  if (data == nullptr || size == 0) {
    if (error != nullptr) {
      *error = "empty archive";
    }
    return entries;
  }

  mz_zip_archive zip{};
  if (mz_zip_reader_init_mem(&zip, data, size, 0) == MZ_FALSE) {
    if (error != nullptr) {
      *error = "not a zip archive";
    }
    return entries;
  }

  std::size_t total_bytes = 0;
  std::unordered_set<std::string> used_names;
  const auto file_count = mz_zip_reader_get_num_files(&zip);
  for (mz_uint index = 0; index < file_count; ++index) {
    if (entries.size() >= limits.max_entries) {
      break;
    }
    mz_zip_archive_file_stat stat{};
    if (mz_zip_reader_file_stat(&zip, index, &stat) == MZ_FALSE || stat.m_is_directory != MZ_FALSE) {
      continue;
    }
    const std::string entry_path(stat.m_filename);
    if (is_macos_resource_fork_path(entry_path)) {
      continue;
    }
    auto name = sanitized_basename(entry_path);
    if (name.empty() || name.front() == '.' || !has_font_extension(name)) {
      continue;
    }
    // Cap check on the declared size BEFORE extracting: the caps, not the
    // archive's claims, bound the allocation.
    const auto declared_size = static_cast<std::size_t>(stat.m_uncomp_size);
    if (declared_size == 0 || declared_size > limits.max_file_bytes ||
        declared_size > limits.max_total_bytes - total_bytes) {
      continue;
    }

    std::size_t extracted_size = 0;
    void* extracted = mz_zip_reader_extract_to_heap(&zip, index, &extracted_size, 0);
    if (extracted == nullptr) {
      continue;  // corrupt entry (CRC mismatch, truncated stream): skip, keep the rest
    }
    if (extracted_size == 0 || extracted_size > limits.max_file_bytes ||
        extracted_size > limits.max_total_bytes - total_bytes) {
      mz_free(extracted);
      continue;
    }

    ZipFontEntry entry;
    entry.name = deduplicated_name(name, used_names);
    const auto* bytes = static_cast<const std::uint8_t*>(extracted);
    entry.bytes.assign(bytes, bytes + extracted_size);
    mz_free(extracted);
    used_names.insert(lowercase(entry.name));
    total_bytes += extracted_size;
    entries.push_back(std::move(entry));
  }

  mz_zip_reader_end(&zip);
  return entries;
}

}  // namespace patchy::formats
