#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace patchy {

// Suffix of every temporary file the writers below create beside their target.
inline constexpr std::string_view kAtomicTemporarySuffix = ".patchy-tmp";

// Writes `bytes` to `path` through a sibling temporary file
// (`<name>.<pid>-<counter>-<random>.patchy-tmp`, same directory so the final rename
// stays on one volume) and a replace-existing rename. The temporary is created
// exclusively (a name that already exists, as a file, a hard link or a symlink, is
// never opened; another name is tried), the bytes go through that same handle, and
// the file is flushed to the device before the rename. Every document writer in
// Patchy goes through this, AtomicFileReplacement, or QSaveFile (AGENTS.md); never
// truncate a user's file in place.
//
// Guarantees: a process crash, a full disk, or a writer error never leaves the target
// truncated, since the old file is only replaced once the new bytes are completely
// written and flushed. Against an OS crash or power loss the new bytes are on the
// device before the rename (FlushFileBuffers, fsync, F_FULLFSYNC on macOS) and the
// rename itself is written through on Windows and followed by a best-effort
// directory fsync on POSIX; the one window left is a directory entry the file
// system has not yet persisted, in which case the old file is still what comes back.
//
// Throws std::runtime_error(open_message) when the temporary file cannot be
// created and std::runtime_error(write_message) when the write, the flush, or the
// rename fails. The temporary file is removed on every failure path. Semantics that
// differ from an in-place write: a target another process holds open with a
// share-deny lock (Windows) fails at the rename instead of being truncated; a
// symlink target is replaced by a regular file; the new file takes the
// directory's default permissions rather than the old file's.
void write_file_bytes_atomically(const std::filesystem::path& path, std::span<const std::uint8_t> bytes,
                                 std::string_view open_message, std::string_view write_message);

// The same rule for writers that stream to disk instead of building the bytes in
// memory (the PDF image writer, page by page) or that hand a path or device to a
// library (Qt's PDF engine). The constructor reserves temporary_path() by creating
// it exclusively (empty); write the new file there, close it, then commit(): the
// target is untouched until that rename succeeds. Without a successful commit() the
// temporary is removed by discard() or the destructor, so a cancel or an exception
// leaves the old file and no stray temporary. Only a temporary this object created
// is ever removed.
//
// Because the caller reopens the reserved name by path, a link swapped in under
// that name between the reservation and the caller's open would receive the
// caller's bytes; commit() refuses to rename anything that is not a regular file
// with a single hard link, so such a swap can never replace the target, but the
// swapped-in target's contents are the caller's writer's to protect. The
// in-memory writer above has no such window.
class AtomicFileReplacement {
public:
  explicit AtomicFileReplacement(std::filesystem::path target);
  ~AtomicFileReplacement();
  AtomicFileReplacement(const AtomicFileReplacement&) = delete;
  AtomicFileReplacement& operator=(const AtomicFileReplacement&) = delete;

  [[nodiscard]] const std::filesystem::path& target_path() const noexcept { return target_; }
  [[nodiscard]] const std::filesystem::path& temporary_path() const noexcept { return temporary_; }

  // Flushes the finished temporary to the device and renames it over the target.
  // Close every handle on the temporary first (Windows cannot rename an open file).
  // Throws std::runtime_error(write_message), with the temporary removed, when the
  // temporary is not a plain single-link regular file, when the flush or the rename
  // fails, or when the replacement was already committed or discarded.
  void commit(std::string_view write_message);
  // Removes the temporary unless commit() succeeded. Safe to call more than once.
  void discard() noexcept;

private:
  friend void write_file_bytes_atomically(const std::filesystem::path& path, std::span<const std::uint8_t> bytes,
                                          std::string_view open_message, std::string_view write_message);
  // `keep_open` leaves the exclusively created temporary open on `handle_` so the
  // in-memory writer writes through the very handle the reservation produced.
  AtomicFileReplacement(std::filesystem::path target, bool keep_open);
  [[nodiscard]] bool write_through_handle(std::span<const std::uint8_t> bytes) noexcept;
  // Flushes and closes handle_; false when either step reported an error.
  [[nodiscard]] bool flush_and_close_handle() noexcept;

  std::filesystem::path target_;
  std::filesystem::path temporary_;
  // The reservation's open handle (a HANDLE on Windows, a descriptor elsewhere), or
  // kNoHandle once closed or when the temporary was never created by this object.
  std::intptr_t handle_;
  bool reserved_{false};
  // True once the bytes were written and flushed through handle_, so commit() need
  // not reopen the temporary to flush it.
  bool flushed_{false};
  bool settled_{false};
};

}  // namespace patchy
