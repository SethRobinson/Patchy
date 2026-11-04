#include "formats/hdr_document_io.hpp"

#include "core/pixel_depth.hpp"
#include "formats/document_flatten.hpp"
#include "render/compositor.hpp"
#include "support/decode_budget.hpp"
#include "support/string_utils.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>

namespace patchy::hdr {

namespace {

constexpr std::int32_t kMaxSide = 65535;

[[nodiscard]] std::runtime_error damaged() {
  return std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The Radiance HDR file is damaged or truncated"));
}

// One header line (without its terminator); advances `at` past the newline.
[[nodiscard]] std::string read_line(std::span<const std::uint8_t> bytes, std::size_t& at) {
  std::string line;
  while (at < bytes.size() && bytes[at] != '\n') {
    if (bytes[at] != '\r') {
      line.push_back(static_cast<char>(bytes[at]));
    }
    ++at;
  }
  if (at >= bytes.size()) {
    throw damaged();
  }
  ++at;  // the newline
  return line;
}

[[nodiscard]] float srgb_to_linear(float v) noexcept {
  return v <= 0.04045F ? v / 12.92F : std::pow((v + 0.055F) / 1.055F, 2.4F);
}

}  // namespace

const std::vector<std::string>& hdr_extensions() {
  static const std::vector<std::string> extensions = {"hdr", "rgbe"};
  return extensions;
}

bool is_hdr_extension(std::string_view extension) {
  const auto normalized = normalized_extension(extension, false);
  const auto& extensions = hdr_extensions();
  return std::find(extensions.begin(), extensions.end(), normalized) != extensions.end();
}

bool sniff(std::span<const std::uint8_t> bytes) noexcept {
  const auto starts_with = [bytes](std::string_view literal) {
    return bytes.size() >= literal.size() && std::memcmp(bytes.data(), literal.data(), literal.size()) == 0;
  };
  return starts_with("#?RADIANCE") || starts_with("#?RGBE");
}

void rgbe_to_float(const std::uint8_t rgbe[4], float out_rgb[3]) noexcept {
  if (rgbe[3] == 0) {
    out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0F;
    return;
  }
  // Greg Ward's convention: mantissa (m + 0.5) / 256 * 2^(e - 128).
  const float scale = std::ldexp(1.0F, static_cast<int>(rgbe[3]) - (128 + 8));
  out_rgb[0] = (static_cast<float>(rgbe[0]) + 0.5F) * scale;
  out_rgb[1] = (static_cast<float>(rgbe[1]) + 0.5F) * scale;
  out_rgb[2] = (static_cast<float>(rgbe[2]) + 0.5F) * scale;
}

void float_to_rgbe(const float rgb[3], std::uint8_t out_rgbe[4]) noexcept {
  const float r = std::isfinite(rgb[0]) ? std::max(0.0F, rgb[0]) : 0.0F;
  const float g = std::isfinite(rgb[1]) ? std::max(0.0F, rgb[1]) : 0.0F;
  const float b = std::isfinite(rgb[2]) ? std::max(0.0F, rgb[2]) : 0.0F;
  const float largest = std::max({r, g, b});
  if (largest < 1e-32F) {
    out_rgbe[0] = out_rgbe[1] = out_rgbe[2] = out_rgbe[3] = 0;
    return;
  }
  int exponent = 0;
  const float normalized = std::frexp(largest, &exponent);  // largest = normalized * 2^exponent, 0.5 <= normalized < 1
  const float scale = normalized * 256.0F / largest;
  out_rgbe[0] = static_cast<std::uint8_t>(std::min(255.0F, r * scale));
  out_rgbe[1] = static_cast<std::uint8_t>(std::min(255.0F, g * scale));
  out_rgbe[2] = static_cast<std::uint8_t>(std::min(255.0F, b * scale));
  out_rgbe[3] = static_cast<std::uint8_t>(std::clamp(exponent + 128, 0, 255));
}

FormatReadResult read_hdr(std::span<const std::uint8_t> bytes) {
  if (!sniff(bytes)) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This file is not a Radiance HDR image"));
  }
  std::size_t at = 0;
  (void)read_line(bytes, at);  // the magic line
  bool rgbe_format = true;
  while (true) {
    const auto line = read_line(bytes, at);
    if (line.empty()) {
      break;  // the blank line ends the header
    }
    if (line.rfind("FORMAT=", 0) == 0) {
      rgbe_format = line == "FORMAT=32-bit_rle_rgbe";
      if (!rgbe_format && line != "FORMAT=32-bit_rle_xyze") {
        throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This Radiance HDR file uses a pixel format Patchy does not read"));
      }
    }
  }
  if (!rgbe_format) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "XYZE Radiance HDR files are not supported; save the file as RGBE"));
  }
  const auto resolution = read_line(bytes, at);
  // "-Y h +X w" is the standard top-down orientation; "+Y h +X w" is bottom-up.
  std::istringstream fields(resolution);
  std::string y_token;
  std::string x_token;
  long height = 0;
  long width = 0;
  fields >> y_token >> height >> x_token >> width;
  if (fields.fail() || (y_token != "-Y" && y_token != "+Y") || x_token != "+X" || width <= 0 || height <= 0 ||
      width > kMaxSide || height > kMaxSide) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This Radiance HDR file uses an image orientation Patchy does not read"));
  }
  const bool bottom_up = y_token == "+Y";
  const auto w = static_cast<std::int32_t>(width);
  const auto h = static_cast<std::int32_t>(height);
  // The float buffer below is sized from the resolution line alone: the RGBE data after it
  // must be able to describe that many pixels (docs/psd-reader-hardening.md, "Decode budget").
  if (!checked_decode_bytes(w, h, 4U, bytes.size() - at).has_value()) {
    throw damaged();
  }

  PixelBuffer pixels(w, h, with_bit_depth(PixelFormat::rgba8(), BitDepth::Float32));
  std::vector<std::uint8_t> scanline(static_cast<std::size_t>(w) * 4U);
  for (std::int32_t row = 0; row < h; ++row) {
    const bool new_rle = w >= 8 && w <= 0x7fff && at + 4 <= bytes.size() && bytes[at] == 2 && bytes[at + 1] == 2 &&
                         bytes[at + 2] < 128 && ((static_cast<int>(bytes[at + 2]) << 8) | bytes[at + 3]) == w;
    if (new_rle) {
      at += 4;
      // Each channel in turn, run-length coded: count > 128 repeats the next byte, else literals.
      for (int channel = 0; channel < 4; ++channel) {
        std::int32_t x = 0;
        while (x < w) {
          if (at >= bytes.size()) {
            throw damaged();
          }
          auto count = static_cast<int>(bytes[at++]);
          if (count > 128) {
            count -= 128;
            if (at >= bytes.size() || x + count > w) {
              throw damaged();
            }
            const auto value = bytes[at++];
            for (int i = 0; i < count; ++i) {
              scanline[static_cast<std::size_t>(x++) * 4U + static_cast<std::size_t>(channel)] = value;
            }
          } else {
            if (count == 0 || at + static_cast<std::size_t>(count) > bytes.size() || x + count > w) {
              throw damaged();
            }
            for (int i = 0; i < count; ++i) {
              scanline[static_cast<std::size_t>(x++) * 4U + static_cast<std::size_t>(channel)] = bytes[at++];
            }
          }
        }
      }
    } else {
      // Flat RGBE quadruples, with the old run marker (1, 1, 1, count) repeating the previous pixel.
      std::int32_t x = 0;
      int shift = 0;
      while (x < w) {
        if (at + 4 > bytes.size()) {
          throw damaged();
        }
        if (bytes[at] == 1 && bytes[at + 1] == 1 && bytes[at + 2] == 1) {
          if (x == 0) {
            throw damaged();
          }
          const auto count = static_cast<int>(bytes[at + 3]) << shift;
          if (x + count > w) {
            throw damaged();
          }
          for (int i = 0; i < count; ++i) {
            std::memcpy(&scanline[static_cast<std::size_t>(x) * 4U], &scanline[static_cast<std::size_t>(x - 1) * 4U], 4);
            ++x;
          }
          shift += 8;
          at += 4;
        } else {
          std::memcpy(&scanline[static_cast<std::size_t>(x) * 4U], &bytes[at], 4);
          ++x;
          at += 4;
          shift = 0;
        }
      }
    }
    const auto target_row = bottom_up ? h - 1 - row : row;
    auto* out = reinterpret_cast<float*>(pixels.row(target_row).data());
    for (std::int32_t x = 0; x < w; ++x) {
      rgbe_to_float(&scanline[static_cast<std::size_t>(x) * 4U], out + static_cast<std::size_t>(x) * 4U);
      out[static_cast<std::size_t>(x) * 4U + 3U] = 1.0F;
    }
  }

  FormatReadResult result{Document(w, h, with_bit_depth(PixelFormat::rgba8(), BitDepth::Float32)), {}};
  result.document.color_state().bit_depth = BitDepth::Float32;
  result.document.add_pixel_layer("Background", std::move(pixels));
  return result;
}

std::vector<std::uint8_t> write_hdr(const Document& document) {
  const auto depth = document.color_state().bit_depth;
  const PixelBuffer flat = depth == BitDepth::UInt8 ? flatten_document_rgba8(document) : Compositor{}.flatten_rgba_deep(document);
  const auto w = flat.width();
  const auto h = flat.height();
  if (w <= 0 || h <= 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Cannot write an empty image as Radiance HDR"));
  }
  std::vector<std::uint8_t> out;
  const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " + std::to_string(h) + " +X " + std::to_string(w) + "\n";
  out.insert(out.end(), header.begin(), header.end());

  const auto channels = flat.format().channels;
  const auto flat_depth = flat.format().bit_depth;
  std::vector<std::uint8_t> scanline(static_cast<std::size_t>(w) * 4U);
  const bool rle = w >= 8 && w <= 0x7fff;
  for (std::int32_t y = 0; y < h; ++y) {
    const auto row = flat.row(y);
    for (std::int32_t x = 0; x < w; ++x) {
      float rgb[3];
      for (int c = 0; c < 3; ++c) {
        const auto index = static_cast<std::size_t>(x) * channels + static_cast<std::size_t>(c);
        switch (flat_depth) {
          case BitDepth::UInt8:
            rgb[c] = srgb_to_linear(static_cast<float>(row[index]) / 255.0F);
            break;
          case BitDepth::UInt16:
            rgb[c] = srgb_to_linear(static_cast<float>(reinterpret_cast<const std::uint16_t*>(row.data())[index]) / 65535.0F);
            break;
          case BitDepth::Float32:
            rgb[c] = reinterpret_cast<const float*>(row.data())[index];
            break;
        }
      }
      float_to_rgbe(rgb, &scanline[static_cast<std::size_t>(x) * 4U]);
    }
    if (!rle) {
      out.insert(out.end(), scanline.begin(), scanline.end());
      continue;
    }
    out.push_back(2);
    out.push_back(2);
    out.push_back(static_cast<std::uint8_t>((w >> 8) & 0x7f));
    out.push_back(static_cast<std::uint8_t>(w & 0xff));
    for (int channel = 0; channel < 4; ++channel) {
      std::int32_t x = 0;
      while (x < w) {
        const auto value = scanline[static_cast<std::size_t>(x) * 4U + static_cast<std::size_t>(channel)];
        std::int32_t run = 1;
        while (x + run < w && run < 127 &&
               scanline[static_cast<std::size_t>(x + run) * 4U + static_cast<std::size_t>(channel)] == value) {
          ++run;
        }
        if (run >= 3) {
          out.push_back(static_cast<std::uint8_t>(128 + run));
          out.push_back(value);
          x += run;
          continue;
        }
        // A literal stretch up to the next run of three (or 128 bytes).
        std::int32_t literal = 0;
        while (x + literal < w && literal < 128) {
          const auto next = x + literal;
          if (next + 2 < w) {
            const auto a = scanline[static_cast<std::size_t>(next) * 4U + static_cast<std::size_t>(channel)];
            const auto b = scanline[static_cast<std::size_t>(next + 1) * 4U + static_cast<std::size_t>(channel)];
            const auto c = scanline[static_cast<std::size_t>(next + 2) * 4U + static_cast<std::size_t>(channel)];
            if (a == b && b == c && literal > 0) {
              break;
            }
          }
          ++literal;
        }
        out.push_back(static_cast<std::uint8_t>(literal));
        for (std::int32_t i = 0; i < literal; ++i) {
          out.push_back(scanline[static_cast<std::size_t>(x + i) * 4U + static_cast<std::size_t>(channel)]);
        }
        x += literal;
      }
    }
  }
  return out;
}

}  // namespace patchy::hdr
