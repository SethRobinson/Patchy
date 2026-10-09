#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace patchy {

// Writes `bytes` to `path` through a sibling temporary file
// (`<name>.<pid>-<counter>.patchy-tmp`, same directory so the final rename stays on
// one volume) and a replace-existing rename. A crash, a full disk, or a writer
// error therefore never leaves the target truncated: the old file survives until
// the new bytes are completely on disk. Every document writer in Patchy goes
// through this, AtomicFileReplacement, or QSaveFile (AGENTS.md); never truncate a
// user's file in place.
//
// Throws std::runtime_error(open_message) when the temporary file cannot be
// created and std::runtime_error(write_message) when the write or the rename
// fails. The temporary file is removed on every failure path. Semantics that
// differ from an in-place write: a target another process holds open with a
// share-deny lock (Windows) fails at the rename instead of being truncated; a
// symlink target is replaced by a regular file; the new file takes the
// directory's default permissions rather than the old file's.
void write_file_bytes_atomically(const std::filesystem::path& path, std::span<const std::uint8_t> bytes,
                                 std::string_view open_message, std::string_view write_message);

// The same rule for writers that stream to disk instead of building the bytes in
// memory (the PDF image writer, page by page) or that hand a path or device to a
// library (Qt's PDF engine). Write the new file at temporary_path(), close it, then
// commit(): the target is untouched until that rename succeeds. Without a successful
// commit() the temporary is removed by discard() or the destructor, so a cancel or
// an exception leaves the old file and no stray temporary.
class AtomicFileReplacement {
public:
  explicit AtomicFileReplacement(std::filesystem::path target);
  ~AtomicFileReplacement();
  AtomicFileReplacement(const AtomicFileReplacement&) = delete;
  AtomicFileReplacement& operator=(const AtomicFileReplacement&) = delete;

  [[nodiscard]] const std::filesystem::path& target_path() const noexcept { return target_; }
  [[nodiscard]] const std::filesystem::path& temporary_path() const noexcept { return temporary_; }

  // Renames the finished temporary over the target. Close every handle on the
  // temporary first (Windows cannot rename an open file). Throws
  // std::runtime_error(write_message), with the temporary removed, when the rename
  // fails or the replacement was already committed or discarded.
  void commit(std::string_view write_message);
  // Removes the temporary unless commit() succeeded. Safe to call more than once.
  void discard() noexcept;

private:
  std::filesystem::path target_;
  std::filesystem::path temporary_;
  bool settled_{false};
};

}  // namespace patchy
