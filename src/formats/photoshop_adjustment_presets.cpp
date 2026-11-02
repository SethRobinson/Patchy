#include "formats/photoshop_adjustment_presets.hpp"

#include "psd/psd_io_internal.hpp"
#include "support/atomic_file_write.hpp"
#include "support/translate_noop.hpp"

#include <fstream>
#include <stdexcept>

namespace patchy {

namespace {

// Every preset body is a few hundred bytes; anything far larger is not a preset.
constexpr std::streamoff kMaxPresetFileBytes = 64 * 1024;

std::vector<std::uint8_t> read_preset_bytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Could not open the preset file"));
  }
  file.seekg(0, std::ios::end);
  const auto end = file.tellg();
  if (end < std::streampos{}) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Could not read the preset file"));
  }
  const auto length = static_cast<std::streamoff>(end);
  if (length > kMaxPresetFileBytes) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The preset file is too large"));
  }
  file.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
  if (!bytes.empty()) {
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Could not read the preset file"));
    }
  }
  return bytes;
}

void write_preset_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  // Sibling temporary plus rename: a failed export keeps the previous preset.
  write_file_bytes_atomically(path, bytes,
                              PATCHY_TRANSLATE_NOOP("QObject", "Could not open the preset file for writing"),
                              PATCHY_TRANSLATE_NOOP("QObject", "Could not write the preset file"));
}

[[noreturn]] void throw_unsupported() {
  throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The preset file is damaged or unsupported"));
}

}  // namespace

namespace ahu {

HueSaturationAdjustment read(std::span<const std::uint8_t> bytes) {
  const auto parsed = psd::parse_photoshop_hue2_adjustment(bytes);
  if (!parsed.has_value()) {
    throw_unsupported();
  }
  return parsed->hue_saturation;
}

HueSaturationAdjustment read_file(const std::filesystem::path& path) {
  return read(read_preset_bytes(path));
}

std::vector<std::uint8_t> write(const HueSaturationAdjustment& settings) {
  return psd::photoshop_hue2_payload(settings, nullptr);
}

void write_file(const std::filesystem::path& path, const HueSaturationAdjustment& settings) {
  write_preset_bytes(path, write(settings));
}

}  // namespace ahu

namespace alv {

LevelsAdjustment read(std::span<const std::uint8_t> bytes) {
  const auto parsed = psd::parse_photoshop_levels_adjustment(bytes);
  if (!parsed.has_value()) {
    throw_unsupported();
  }
  auto levels = parsed->levels;
  levels.channel = LevelsChannel::Rgb;
  return levels;
}

LevelsAdjustment read_file(const std::filesystem::path& path) {
  return read(read_preset_bytes(path));
}

std::vector<std::uint8_t> write(const LevelsAdjustment& settings) {
  return psd::photoshop_levels_payload(settings);
}

void write_file(const std::filesystem::path& path, const LevelsAdjustment& settings) {
  write_preset_bytes(path, write(settings));
}

}  // namespace alv

namespace blw {

BlackWhiteAdjustment read(std::span<const std::uint8_t> bytes) {
  const auto parsed = psd::parse_photoshop_black_white_adjustment(bytes);
  if (!parsed.has_value()) {
    throw_unsupported();
  }
  return parsed->black_white;
}

BlackWhiteAdjustment read_file(const std::filesystem::path& path) {
  return read(read_preset_bytes(path));
}

std::vector<std::uint8_t> write(const BlackWhiteAdjustment& settings) {
  return psd::photoshop_black_white_payload(clamp_black_white(settings), nullptr);
}

void write_file(const std::filesystem::path& path, const BlackWhiteAdjustment& settings) {
  write_preset_bytes(path, write(settings));
}

}  // namespace blw

}  // namespace patchy
