#include "support/atomic_file_write.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace patchy {
namespace {

constexpr std::intptr_t kNoHandle = -1;

long long current_process_id() {
#ifdef _WIN32
  return static_cast<long long>(_getpid());
#else
  return static_cast<long long>(getpid());
#endif
}

// splitmix64: an unpredictable-enough token for a file name, seeded from the clock,
// the pid, a per-process counter and this object's address. The core determinism rule
// is about algorithm output, not temporary file names.
std::uint64_t next_name_token(const void* salt) {
  static std::atomic<std::uint64_t> counter{0};
  auto x = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  x ^= static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()) << 7U;
  x ^= static_cast<std::uint64_t>(current_process_id()) << 32U;
  x ^= counter.fetch_add(1) * 0x9E3779B97F4A7C15ULL;
  x ^= static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(salt));
  x += 0x9E3779B97F4A7C15ULL;
  auto z = x;
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}

std::string hex_text(std::uint64_t value) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string text(16, '0');
  for (int i = 15; i >= 0; --i) {
    text[static_cast<std::size_t>(i)] = kDigits[value & 0xFU];
    value >>= 4U;
  }
  return text;
}

// `<name>.<pid>-<counter>-<token>.patchy-tmp`: readable, and never a name another
// process or a crashed earlier run can be relied on to have planted.
std::filesystem::path candidate_temporary_path(const std::filesystem::path& target, unsigned attempt,
                                               std::uint64_t token) {
  auto name = target.filename();
  name += "." + std::to_string(current_process_id()) + "-" + std::to_string(attempt) + "-" + hex_text(token) +
          std::string(kAtomicTemporarySuffix);
  return target.parent_path() / name;
}

enum class CreateResult { Created, Exists, Failed };

// Creates `path` as a new empty regular file. A name that already exists in any form
// (a file, a hard link, a symlink or junction, dangling or not) reports Exists and is
// left untouched.
CreateResult create_new_file(const std::filesystem::path& path, std::intptr_t& handle) noexcept {
#ifdef _WIN32
  // FILE_FLAG_OPEN_REPARSE_POINT keeps CREATE_NEW from following a dangling link and
  // creating its target; with it, an existing link of any kind is "exists".
  HANDLE created = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (created == INVALID_HANDLE_VALUE) {
    const auto error = GetLastError();
    return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS ? CreateResult::Exists : CreateResult::Failed;
  }
  handle = reinterpret_cast<std::intptr_t>(created);
  return CreateResult::Created;
#else
  // O_EXCL with O_CREAT fails on an existing symlink whatever it points at.
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0666);
  if (fd < 0) {
    return errno == EEXIST ? CreateResult::Exists : CreateResult::Failed;
  }
  handle = fd;
  return CreateResult::Created;
#endif
}

bool write_all(std::intptr_t handle, std::span<const std::uint8_t> bytes) noexcept {
#ifdef _WIN32
  auto* file = reinterpret_cast<HANDLE>(handle);
  while (!bytes.empty()) {
    const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), 1U << 30U));
    DWORD written = 0;
    if (!WriteFile(file, bytes.data(), chunk, &written, nullptr) || written == 0) {
      return false;
    }
    bytes = bytes.subspan(written);
  }
  return true;
#else
  const int fd = static_cast<int>(handle);
  while (!bytes.empty()) {
    const auto chunk = std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(1) << 30U);
    const auto written = ::write(fd, bytes.data(), chunk);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (written == 0) {
      return false;
    }
    bytes = bytes.subspan(static_cast<std::size_t>(written));
  }
  return true;
#endif
}

// Pushes the file's data and metadata to the device.
bool flush_to_device(std::intptr_t handle) noexcept {
#ifdef _WIN32
  return FlushFileBuffers(reinterpret_cast<HANDLE>(handle)) != 0;
#else
  const int fd = static_cast<int>(handle);
#if defined(__APPLE__)
  // fsync on macOS only reaches the drive's cache; F_FULLFSYNC asks the drive to
  // write that out too. Some file systems (network mounts) refuse it: fall back.
  if (::fcntl(fd, F_FULLFSYNC) == 0) {
    return true;
  }
#endif
  while (::fsync(fd) != 0) {
    if (errno != EINTR) {
      return false;
    }
  }
  return true;
#endif
}

bool close_handle(std::intptr_t handle) noexcept {
#ifdef _WIN32
  return CloseHandle(reinterpret_cast<HANDLE>(handle)) != 0;
#else
  // close reports a deferred write error on some file systems (NFS); treat it as one.
  return ::close(static_cast<int>(handle)) == 0;
#endif
}

// Opens the finished temporary by name without following a link, confirms it is a
// plain regular file with one hard link (nothing swapped in under the reserved name),
// and flushes it. False when any of that fails.
bool verify_and_flush_by_path(const std::filesystem::path& path) noexcept {
#ifdef _WIN32
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  BY_HANDLE_FILE_INFORMATION info{};
  bool ok = GetFileInformationByHandle(file, &info) != 0 &&
            (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE)) ==
                0 &&
            info.nNumberOfLinks == 1;
  ok = ok && FlushFileBuffers(file) != 0;
  CloseHandle(file);
  return ok;
#else
  const int fd = ::open(path.c_str(), O_WRONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) {
    return false;
  }
  struct stat status {};
  bool ok = ::fstat(fd, &status) == 0 && S_ISREG(status.st_mode) && status.st_nlink == 1;
  ok = ok && flush_to_device(fd);
  ::close(fd);
  return ok;
#endif
}

// Replaces `target` with `temporary` in one step. Windows writes the rename through;
// POSIX follows it with a best-effort fsync of the directory so the new entry is on
// the device (some file systems refuse a directory fsync, which is not an error here:
// the data is already durable and the rename is atomic either way).
bool rename_replacing(const std::filesystem::path& temporary, const std::filesystem::path& target) noexcept {
#ifdef _WIN32
  return MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  if (::rename(temporary.c_str(), target.c_str()) != 0) {
    return false;
  }
  const auto parent = target.has_parent_path() ? target.parent_path() : std::filesystem::path(".");
  const int dir = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dir >= 0) {
    (void)::fsync(dir);
    ::close(dir);
  }
  return true;
#endif
}

void remove_quietly(const std::filesystem::path& path) noexcept {
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

}  // namespace

AtomicFileReplacement::AtomicFileReplacement(std::filesystem::path target)
    : AtomicFileReplacement(std::move(target), /*keep_open=*/false) {}

AtomicFileReplacement::AtomicFileReplacement(std::filesystem::path target, bool keep_open)
    : target_(std::move(target)), handle_(kNoHandle) {
  // A fresh random name per attempt; a name that exists, whatever it is, is skipped.
  // A failure for any other reason (missing directory, permissions) leaves the last
  // candidate as temporary_path() unreserved, so the caller's own open of it fails and
  // reports the same cause; nothing is created and nothing will be removed.
  constexpr unsigned kAttempts = 16;
  for (unsigned attempt = 0; attempt < kAttempts; ++attempt) {
    temporary_ = candidate_temporary_path(target_, attempt, next_name_token(this));
    const auto result = create_new_file(temporary_, handle_);
    if (result == CreateResult::Exists) {
      continue;
    }
    if (result == CreateResult::Created) {
      reserved_ = true;
      if (!keep_open) {
        (void)close_handle(handle_);
        handle_ = kNoHandle;
      }
    }
    break;
  }
}

AtomicFileReplacement::~AtomicFileReplacement() { discard(); }

bool AtomicFileReplacement::write_through_handle(std::span<const std::uint8_t> bytes) noexcept {
  return handle_ != kNoHandle && write_all(handle_, bytes);
}

bool AtomicFileReplacement::flush_and_close_handle() noexcept {
  if (handle_ == kNoHandle) {
    return false;
  }
  const bool flushed = flush_to_device(handle_);
  const bool closed = close_handle(handle_);
  handle_ = kNoHandle;
  flushed_ = flushed && closed;
  return flushed_;
}

void AtomicFileReplacement::commit(std::string_view write_message) {
  if (settled_) {
    throw std::runtime_error(std::string(write_message));
  }
  if (handle_ != kNoHandle && !flush_and_close_handle()) {
    discard();
    throw std::runtime_error(std::string(write_message));
  }
  settled_ = true;
  // The temporary must still be the plain file this object created (or the caller
  // wrote) and its bytes must be on the device before the old target goes away.
  if ((!flushed_ && !verify_and_flush_by_path(temporary_)) || !rename_replacing(temporary_, target_)) {
    if (reserved_) {
      remove_quietly(temporary_);
    }
    throw std::runtime_error(std::string(write_message));
  }
}

void AtomicFileReplacement::discard() noexcept {
  if (settled_) {
    return;
  }
  settled_ = true;
  if (handle_ != kNoHandle) {
    (void)close_handle(handle_);
    handle_ = kNoHandle;
  }
  if (reserved_) {
    remove_quietly(temporary_);
  }
}

void write_file_bytes_atomically(const std::filesystem::path& path, std::span<const std::uint8_t> bytes,
                                 std::string_view open_message, std::string_view write_message) {
  AtomicFileReplacement replacement(path, /*keep_open=*/true);
  if (!replacement.reserved_) {
    throw std::runtime_error(std::string(open_message));
  }
  if (!replacement.write_through_handle(bytes) || !replacement.flush_and_close_handle()) {
    replacement.discard();
    throw std::runtime_error(std::string(write_message));
  }
  replacement.commit(write_message);
}

}  // namespace patchy
