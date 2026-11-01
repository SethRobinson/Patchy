#include "plugins/gimp/host/shared_pixels.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace patchy::gimp::host {

#ifdef _WIN32
namespace {
std::wstring widen(const std::string& utf8) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
  return out;
}
}  // namespace
#endif

std::unique_ptr<SharedPixels> SharedPixels::open(const std::string& utf8_path, std::uint64_t size, std::string* error) {
  std::unique_ptr<SharedPixels> pixels(new SharedPixels());
  pixels->size_ = size;
  if (size == 0) {
    if (error) *error = "the shared pixel file is empty";
    return nullptr;
  }
#ifdef _WIN32
  const auto path = widen(utf8_path);
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    if (error) *error = "could not open the shared pixel file";
    return nullptr;
  }
  pixels->file_ = file;
  HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READWRITE, static_cast<DWORD>(size >> 32),
                                      static_cast<DWORD>(size), nullptr);
  if (mapping == nullptr) {
    if (error) *error = "could not map the shared pixel file";
    return nullptr;
  }
  pixels->mapping_ = mapping;
  pixels->data_ = static_cast<std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, static_cast<SIZE_T>(size)));
#else
  const int fd = ::open(utf8_path.c_str(), O_RDWR);
  if (fd < 0) {
    if (error) *error = "could not open the shared pixel file";
    return nullptr;
  }
  pixels->fd_ = fd;
  void* data = mmap(nullptr, static_cast<std::size_t>(size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  pixels->data_ = data == MAP_FAILED ? nullptr : static_cast<std::uint8_t*>(data);
#endif
  if (pixels->data_ == nullptr) {
    if (error) *error = "could not map the shared pixel file";
    return nullptr;
  }
  return pixels;
}

SharedPixels::~SharedPixels() {
#ifdef _WIN32
  if (data_ != nullptr) UnmapViewOfFile(data_);
  if (mapping_ != nullptr) CloseHandle(mapping_);
  if (file_ != nullptr) CloseHandle(file_);
#else
  if (data_ != nullptr) munmap(data_, static_cast<std::size_t>(size_));
  if (fd_ >= 0) close(fd_);
#endif
}

}  // namespace patchy::gimp::host
