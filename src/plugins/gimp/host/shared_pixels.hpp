#pragma once

// The pixel file Patchy shares with the helper (link::RunRequest describes its layout),
// mapped read-write.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace patchy::gimp::host {

class SharedPixels {
public:
  static std::unique_ptr<SharedPixels> open(const std::string& utf8_path, std::uint64_t size, std::string* error);
  ~SharedPixels();
  SharedPixels(const SharedPixels&) = delete;
  SharedPixels& operator=(const SharedPixels&) = delete;

  [[nodiscard]] std::uint8_t* data() const noexcept { return data_; }
  [[nodiscard]] std::uint64_t size() const noexcept { return size_; }

private:
  SharedPixels() = default;
  std::uint8_t* data_{nullptr};
  std::uint64_t size_{0};
#ifdef _WIN32
  void* file_{nullptr};
  void* mapping_{nullptr};
#else
  int fd_{-1};
#endif
};

}  // namespace patchy::gimp::host
