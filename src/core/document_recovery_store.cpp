#include "core/document_recovery_store.hpp"

#include "support/atomic_file_write.hpp"
#include "support/path_utils.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace patchy::recovery {
namespace {

std::filesystem::path stem_path(const std::filesystem::path& instance_dir, std::int64_t session_id,
                                std::string_view extension) {
  return instance_dir / std::filesystem::path(std::to_string(session_id) + std::string(extension));
}

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  return std::string(std::istreambuf_iterator<char>(file), {});
}

bool has_extension(const std::filesystem::path& path, std::string_view extension) {
  return path_to_utf8(path.extension()) == extension;
}

// A stem the reopen can hand to std::stoll without a surprise: decimal digits only,
// short enough for an int64.
bool is_session_stem(std::string_view stem) {
  if (stem.empty() || stem.size() > 18) {
    return false;
  }
  return std::all_of(stem.begin(), stem.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool ends_with(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

// Format 2 value escaping: the three characters that could break the line layout.
std::string escape_value(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (const char c : value) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      default: out += c; break;
    }
  }
  return out;
}

// An escape the writer never produces is kept literally rather than dropped.
std::string unescape_value(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    const char c = value[i];
    if (c != '\\' || i + 1 == value.size()) {
      out += c;
      continue;
    }
    const char next = value[++i];
    switch (next) {
      case '\\': out += '\\'; break;
      case 'n': out += '\n'; break;
      case 'r': out += '\r'; break;
      default: out += '\\'; out += next; break;
    }
  }
  return out;
}

}  // namespace

std::filesystem::path document_path(const std::filesystem::path& instance_dir, std::int64_t session_id) {
  return stem_path(instance_dir, session_id, kDocumentExtension);
}

std::filesystem::path sidecar_path(const std::filesystem::path& instance_dir, std::int64_t session_id) {
  return stem_path(instance_dir, session_id, kSidecarExtension);
}

std::string encode_sidecar(const RecoveryEntry& entry) {
  return "format=" + std::to_string(kSidecarFormatVersion) + "\ntitle=" + escape_value(entry.title) +
         "\npath=" + escape_value(entry.original_path) + "\nsavedAt=" + std::to_string(entry.saved_at_unix_ms) + "\n";
}

std::optional<RecoveryEntry> decode_sidecar(std::string_view text, std::string file_stem) {
  RecoveryEntry entry;
  entry.file_stem = std::move(file_stem);
  bool recognized = false;
  bool escaped = false;
  int title_count = 0;
  int path_count = 0;
  int saved_at_count = 0;
  while (!text.empty()) {
    const auto end = text.find('\n');
    auto line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    const auto separator = line.find('=');
    if (separator == std::string_view::npos) {
      continue;
    }
    const auto key = line.substr(0, separator);
    const auto raw = line.substr(separator + 1);
    if (key == "format") {
      // Only the version this reader knows escapes; a legacy file has no format line.
      escaped = raw == "2";
      continue;
    }
    const auto value = escaped ? unescape_value(raw) : std::string(raw);
    if (key == "title") {
      if (++title_count == 1) {
        entry.title = value;
      }
      recognized = true;
    } else if (key == "path") {
      if (++path_count == 1) {
        entry.original_path = value;
      }
      recognized = true;
    } else if (key == "savedAt") {
      if (++saved_at_count == 1) {
        try {
          entry.saved_at_unix_ms = std::stoll(value);
        } catch (const std::exception&) {
          entry.saved_at_unix_ms = 0;
        }
      }
      recognized = true;
    }
  }
  if (!recognized) {
    return std::nullopt;
  }
  if (title_count > 1 || path_count > 1 || saved_at_count > 1) {
    // Malformed: a second `path=` line is exactly what an unescaped line break in a
    // title would have produced. The document still recovers, with Save prompting.
    entry.original_path.clear();
  }
  return entry;
}

std::vector<RecoveryEntry> scan_instance_dir(const std::filesystem::path& instance_dir) {
  std::vector<RecoveryEntry> entries;
  std::error_code error;
  std::filesystem::directory_iterator iterator(instance_dir, error);
  if (error) {
    return entries;
  }
  for (const auto& item : iterator) {
    if (!item.is_regular_file(error) || error || !has_extension(item.path(), kDocumentExtension)) {
      continue;
    }
    auto stem = path_to_utf8(item.path().stem());
    if (!is_session_stem(stem)) {
      continue;
    }
    auto sidecar = item.path();
    sidecar.replace_extension(std::filesystem::path(std::string(kSidecarExtension)));
    std::optional<RecoveryEntry> entry;
    if (std::filesystem::is_regular_file(sidecar, error) && !error) {
      entry = decode_sidecar(read_text_file(sidecar), stem);
    }
    if (!entry.has_value()) {
      entry = RecoveryEntry{};
      entry->file_stem = std::move(stem);
    }
    entries.push_back(std::move(*entry));
  }
  std::sort(entries.begin(), entries.end(), [](const RecoveryEntry& a, const RecoveryEntry& b) {
    return std::stoll(a.file_stem) < std::stoll(b.file_stem);
  });
  return entries;
}

bool is_store_file_name(std::string_view name) noexcept {
  if (ends_with(name, kAtomicTemporarySuffix)) {
    // `<session>.<ext>.<pid>-<n>-<token>.patchy-tmp`: drop the suffix and the writer's
    // own middle piece, then judge what is left like a finished file.
    name.remove_suffix(kAtomicTemporarySuffix.size());
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos) {
      return false;
    }
    name = name.substr(0, dot);
  }
  for (const auto extension : {kDocumentExtension, kSidecarExtension}) {
    if (ends_with(name, extension) && is_session_stem(name.substr(0, name.size() - extension.size()))) {
      return true;
    }
  }
  return false;
}

void write_sidecar(const std::filesystem::path& instance_dir, std::int64_t session_id, const RecoveryEntry& entry) {
  const auto sidecar = encode_sidecar(entry);
  write_file_bytes_atomically(sidecar_path(instance_dir, session_id),
                              std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(sidecar.data()),
                                                            sidecar.size()),
                              "Could not create the recovery file", "Could not write the recovery file");
}

void write_entry(const std::filesystem::path& instance_dir, std::int64_t session_id,
                 std::span<const std::uint8_t> psb_bytes, const RecoveryEntry& entry) {
  std::error_code error;
  std::filesystem::create_directories(instance_dir, error);
  write_file_bytes_atomically(document_path(instance_dir, session_id), psb_bytes,
                              "Could not create the recovery file", "Could not write the recovery file");
  write_sidecar(instance_dir, session_id, entry);
}

void remove_entry(const std::filesystem::path& instance_dir, std::int64_t session_id) noexcept {
  std::error_code ignored;
  std::filesystem::remove(document_path(instance_dir, session_id), ignored);
  std::filesystem::remove(sidecar_path(instance_dir, session_id), ignored);
}

}  // namespace patchy::recovery
