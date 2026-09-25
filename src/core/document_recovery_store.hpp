#pragma once

// On-disk layout of the automatic document recovery store (docs/document-recovery.md).
// Qt-free so the naming, the sidecar format, and the directory scan are pinned by
// patchy_core_tests. One instance folder holds, per open document session:
//   <session_id>.psb        the recovery copy (the PSB writer's bytes)
//   <session_id>.recovery   a UTF-8 key=value sidecar: title, original path, timestamp
// The PSB is written first and the sidecar second, so a crash between them leaves a
// recoverable document (listed as untitled), never a sidecar pointing at nothing.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::recovery {

inline constexpr std::string_view kDocumentExtension = ".psb";
inline constexpr std::string_view kSidecarExtension = ".recovery";

struct RecoveryEntry {
  // Decimal session id, the stem shared by the PSB and its sidecar.
  std::string file_stem;
  // UTF-8 document title; empty for an untitled document.
  std::string title;
  // UTF-8 path of the file the document was opened from or last saved to; empty
  // when it was never saved. Rebuild it with std::filesystem::path(std::u8string).
  std::string original_path;
  // Milliseconds since the Unix epoch when the recovery copy was written.
  std::int64_t saved_at_unix_ms{0};
};

[[nodiscard]] std::filesystem::path document_path(const std::filesystem::path& instance_dir, std::int64_t session_id);
[[nodiscard]] std::filesystem::path sidecar_path(const std::filesystem::path& instance_dir, std::int64_t session_id);

// "title=<title>\npath=<path>\nsavedAt=<ms>\n". Titles and paths cannot contain a
// newline on any supported platform, so the format needs no escaping.
[[nodiscard]] std::string encode_sidecar(const RecoveryEntry& entry);
// Unknown keys are ignored, missing keys stay default; nullopt only for text that
// holds no recognized key at all.
[[nodiscard]] std::optional<RecoveryEntry> decode_sidecar(std::string_view text, std::string file_stem);

// Every <stem>.psb in `instance_dir` whose stem is a decimal session id, paired
// with its sidecar when one exists (a PSB without a sidecar lists as untitled with
// no path). Other files are ignored. A missing or unreadable directory yields an
// empty list. Sorted by session id, the order the documents were created in.
[[nodiscard]] std::vector<RecoveryEntry> scan_instance_dir(const std::filesystem::path& instance_dir);

// Writes the PSB bytes and then the sidecar, each atomically (temp file plus
// rename), creating `instance_dir` when needed. Throws std::runtime_error on failure.
void write_entry(const std::filesystem::path& instance_dir, std::int64_t session_id,
                 std::span<const std::uint8_t> psb_bytes, const RecoveryEntry& entry);

// Removes both files of a session; silent when they do not exist.
void remove_entry(const std::filesystem::path& instance_dir, std::int64_t session_id) noexcept;

}  // namespace patchy::recovery
