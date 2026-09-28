#include "support/atomic_file_write.hpp"

#include <atomic>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace patchy {
namespace {

long long current_process_id() {
#ifdef _WIN32
  return static_cast<long long>(_getpid());
#else
  return static_cast<long long>(getpid());
#endif
}

// Unique across processes (pid) and across threads of this process (counter), so
// two concurrent writers of the same target never share a temporary file.
std::filesystem::path temporary_sibling_path(const std::filesystem::path& path) {
  static std::atomic<unsigned long long> counter{0};
  auto name = path.filename();
  name += "." + std::to_string(current_process_id()) + "-" + std::to_string(counter.fetch_add(1)) +
          ".patchy-tmp";
  return path.parent_path() / name;
}

void remove_quietly(const std::filesystem::path& path) noexcept {
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

}  // namespace

void write_file_bytes_atomically(const std::filesystem::path& path, std::span<const std::uint8_t> bytes,
                                 std::string_view open_message, std::string_view write_message) {
  const auto temp = temporary_sibling_path(path);
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) {
      throw std::runtime_error(std::string(open_message));
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.flush();
    const bool written = static_cast<bool>(file);
    file.close();
    if (!written || file.fail()) {
      remove_quietly(temp);
      throw std::runtime_error(std::string(write_message));
    }
  }
  // MSVC implements rename with MoveFileExW(MOVEFILE_REPLACE_EXISTING); POSIX rename
  // replaces atomically. Both keep the old target until the new one is in place.
  std::error_code error;
  std::filesystem::rename(temp, path, error);
  if (error) {
    remove_quietly(temp);
    throw std::runtime_error(std::string(write_message));
  }
}

}  // namespace patchy
