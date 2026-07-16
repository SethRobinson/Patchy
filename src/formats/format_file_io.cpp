#include "formats/format_file_io.hpp"

#include "core/document.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace patchy::formats {

std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path, std::string_view format_name) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open " + std::string(format_name) + " file");
  }
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void rename_first_layer_to_stem(Document& document, const std::filesystem::path& path) {
  const auto stem = path.stem().string();
  if (!stem.empty() && !document.layers().empty()) {
    document.layers().front().set_name(stem);
  }
}

void write_file_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes,
                      std::string_view format_name) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open " + std::string(format_name) + " file for writing");
  }
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file) {
    throw std::runtime_error("Could not write " + std::string(format_name) + " file");
  }
}

}  // namespace patchy::formats
