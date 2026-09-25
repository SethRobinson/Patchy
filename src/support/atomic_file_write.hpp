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
// through this or through QSaveFile (AGENTS.md); never truncate a user's file in
// place.
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

}  // namespace patchy
