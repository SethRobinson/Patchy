#include "plugins/legacy_photoshop_adapter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

namespace photoslop {

namespace {

std::string lower_extension(const std::filesystem::path& path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension;
}

std::uint16_t read_u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
  if (offset + 2 > bytes.size()) {
    return 0;
  }
  return static_cast<std::uint16_t>(bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U));
}

std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
  if (offset + 4 > bytes.size()) {
    return 0;
  }
  return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

std::string pe_machine_name(std::uint16_t machine) {
  switch (machine) {
    case 0x014c:
      return "x86";
    case 0x8664:
      return "x64";
    case 0xaa64:
      return "arm64";
    default:
      return "unknown";
  }
}

std::string host_architecture() {
#if defined(_M_X64) || defined(__x86_64__)
  return "x64";
#elif defined(_M_IX86) || defined(__i386__)
  return "x86";
#elif defined(_M_ARM64) || defined(__aarch64__)
  return "arm64";
#else
  return "unknown";
#endif
}

std::string detect_binary_architecture(std::span<const std::uint8_t> bytes) {
  if (bytes.size() >= 0x40 && bytes[0] == 'M' && bytes[1] == 'Z') {
    const auto pe_offset = read_u32(bytes, 0x3c);
    if (pe_offset + 6 <= bytes.size() && bytes[pe_offset] == 'P' && bytes[pe_offset + 1] == 'E' &&
        bytes[pe_offset + 2] == 0 && bytes[pe_offset + 3] == 0) {
      return pe_machine_name(read_u16(bytes, pe_offset + 4));
    }
    return "pe-unknown";
  }

  if (bytes.size() >= 4) {
    const std::array<std::uint8_t, 4> magic = {bytes[0], bytes[1], bytes[2], bytes[3]};
    if (magic == std::array<std::uint8_t, 4>{0xfe, 0xed, 0xfa, 0xcf} ||
        magic == std::array<std::uint8_t, 4>{0xcf, 0xfa, 0xed, 0xfe}) {
      return "mach-o";
    }
    if (magic == std::array<std::uint8_t, 4>{0x7f, 'E', 'L', 'F'}) {
      return "elf";
    }
  }

  return "unknown";
}

std::vector<std::uint8_t> read_prefix(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return {};
  }
  std::vector<std::uint8_t> bytes(4096);
  input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  bytes.resize(static_cast<std::size_t>(input.gcount()));
  return bytes;
}

LegacyPhotoshopPluginKind kind_from_extension(const std::string& extension) {
  if (extension == ".8bf") {
    return LegacyPhotoshopPluginKind::Filter8bf;
  }
  if (extension == ".8bi") {
    return LegacyPhotoshopPluginKind::Format8bi;
  }
  if (extension == ".8li") {
    return LegacyPhotoshopPluginKind::Automation8li;
  }
  return LegacyPhotoshopPluginKind::Unknown;
}

}  // namespace

LegacyPhotoshopPluginProbe LegacyPhotoshopAdapter::probe(const std::filesystem::path& path) const {
  const auto extension = lower_extension(path);
  const auto kind = kind_from_extension(extension);
  if (kind == LegacyPhotoshopPluginKind::Unknown) {
    return {kind, false, "Unsupported legacy Photoshop plug-in extension.", "unknown"};
  }

  if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
    return {kind, false, "Plug-in file does not exist.", "unknown"};
  }

  const auto bytes = read_prefix(path);
  if (bytes.empty()) {
    return {kind, false, "Plug-in file could not be read.", "unknown"};
  }

  const auto architecture = detect_binary_architecture(bytes);
  if (architecture == "x86" && host_architecture() != "x86") {
    return {kind, false, "32-bit Photoshop plug-ins require a 32-bit compatibility host.", architecture};
  }
  if ((architecture == "x64" || architecture == "arm64") && host_architecture() != architecture) {
    return {kind, false, "Plug-in architecture does not match this Photoslop build.", architecture};
  }

  if (kind == LegacyPhotoshopPluginKind::Automation8li) {
    return {kind, false, "Automation plug-ins are recognized but not supported by the first compatibility adapter.",
            architecture};
  }
  if (kind == LegacyPhotoshopPluginKind::Filter8bf) {
    return {kind, true,
            "Classic Photoshop filter plug-in candidate. Runtime execution will be isolated out-of-process.",
            architecture};
  }
  return {kind, true,
          "Classic Photoshop file-format plug-in candidate. Runtime execution will be isolated out-of-process.",
          architecture};
}

}  // namespace photoslop
