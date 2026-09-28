#include "plugins/legacy_photoshop_adapter.hpp"

#include "plugins/pipl.hpp"

#include "support/string_utils.hpp"
#include "support/path_utils.hpp"
#include "support/translate_noop.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

namespace patchy {

namespace {

std::string lower_extension(const std::filesystem::path& path) {
  return normalized_extension(path_to_utf8(path.extension()));
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

std::string detect_binary_architecture(std::span<const std::uint8_t> bytes) {
  if (bytes.size() >= 0x40 && bytes[0] == 'M' && bytes[1] == 'Z') {
    const auto pe_offset = read_u32(bytes, 0x3c);
    if (static_cast<std::size_t>(pe_offset) + 6 <= bytes.size() && bytes[pe_offset] == 'P' && bytes[pe_offset + 1] == 'E' &&
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
  LegacyPhotoshopPluginProbe result;
  result.kind = kind;
  result.architecture = "unknown";
  result.display_name = path_to_utf8(path.stem());
  if (kind == LegacyPhotoshopPluginKind::Unknown) {
    result.reason = PATCHY_TRANSLATE_NOOP("QObject", "Unsupported legacy Photoshop plug-in extension.");
    return result;
  }

  if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
    result.reason = PATCHY_TRANSLATE_NOOP("QObject", "Plug-in file does not exist.");
    return result;
  }

  const auto bytes = read_prefix(path);
  if (bytes.empty()) {
    result.reason = PATCHY_TRANSLATE_NOOP("QObject", "Plug-in file could not be read.");
    return result;
  }

  result.architecture = detect_binary_architecture(bytes);
  const bool windows_binary = result.architecture == "x86" || result.architecture == "x64" ||
                              result.architecture == "arm64" || result.architecture == "pe-unknown";
  if (windows_binary) {
    // The property list names the plug-in and says which filter cases it takes.
    // A pure file read: nothing is loaded.
    result.pipl = pipl::read_pipl_from_pe_file(path);
    if (result.pipl.found) {
      if (!result.pipl.name.empty()) {
        result.display_name = result.pipl.name;
      }
      result.category = result.pipl.category;
      result.entry_point = result.architecture == "x86" ? result.pipl.entry_point_32 : result.pipl.entry_point_64;
    }
  }
#if !defined(_WIN32)
  // Legacy Photoshop plug-ins are Windows PE binaries; probing one on macOS/Linux gets an
  // honest platform answer instead of a misleading architecture comparison.
  if (windows_binary) {
    result.reason = PATCHY_TRANSLATE_NOOP("QObject", "Legacy Photoshop plug-ins are Windows binaries; they require the Windows build of Patchy.");
    return result;
  }
#endif
  if (!windows_binary) {
    result.reason = PATCHY_TRANSLATE_NOOP("QObject", "Not a Windows plug-in binary.");
    return result;
  }
  if (result.architecture != "x86" && result.architecture != "x64") {
    result.reason = PATCHY_TRANSLATE_NOOP("QObject", "Unsupported plug-in architecture; only 32-bit and 64-bit x86 plug-ins run.");
    return result;
  }
  if (kind != LegacyPhotoshopPluginKind::Filter8bf) {
    result.reason = PATCHY_TRANSLATE_NOOP("QObject", "File-format and automation plug-ins are not supported; only filter (.8bf) plug-ins run.");
    return result;
  }
  if (result.pipl.found && result.pipl.kind != 0 && !result.pipl.is_filter()) {
    result.reason = PATCHY_TRANSLATE_NOOP("QObject", "This plug-in is not a filter; only filter (.8bf) plug-ins run.");
    return result;
  }
  result.supported = true;
  result.reason = result.architecture == "x86"
                      ? PATCHY_TRANSLATE_NOOP("QObject", "Photoshop filter plug-in (32-bit).")
                      : PATCHY_TRANSLATE_NOOP("QObject", "Photoshop filter plug-in (64-bit).");
  return result;
}

}  // namespace patchy
