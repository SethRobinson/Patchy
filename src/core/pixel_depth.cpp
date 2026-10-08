#include "core/pixel_depth.hpp"

#include "core/environment.hpp"
#include "support/srgb_transfer.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace patchy {
namespace {

// -1 = no override (follow PATCHY_DEEP_EDITING), 0 = off, 1 = on.
std::atomic<int> g_deep_editing_override{-1};

bool deep_editing_from_environment() {
  static const bool enabled = [] {
    const auto value = environment_variable("PATCHY_DEEP_EDITING");
    return value.has_value() && !value->empty() && *value != "0";
  }();
  return enabled;
}

DeepDomain native_domain(BitDepth depth) noexcept {
  return depth == BitDepth::Float32 ? DeepDomain::Linear : DeepDomain::Encoded;
}

// Encoded deep-scale value of an 8-bit sample, decoded to linear deep scale.
const std::array<float, 256>& linear_from_u8_table() {
  static const std::array<float, 256> table = [] {
    std::array<float, 256> values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
      values[i] = static_cast<float>(srgb_decode(static_cast<double>(i) / 255.0) * 255.0);
    }
    return values;
  }();
  return table;
}

const std::vector<float>& linear_from_u16_table() {
  static const std::vector<float> table = [] {
    std::vector<float> values(65536);
    for (std::size_t i = 0; i < values.size(); ++i) {
      values[i] = static_cast<float>(srgb_decode(static_cast<double>(i) / 65535.0) * 255.0);
    }
    return values;
  }();
  return table;
}

float encode_deep(float linear) noexcept {
  return static_cast<float>(srgb_encode(static_cast<double>(linear) / 255.0) * 255.0);
}

float decode_deep(float encoded) noexcept {
  return static_cast<float>(srgb_decode(static_cast<double>(encoded) / 255.0) * 255.0);
}

float finite_or_zero(float value) noexcept {
  if (std::isnan(value)) {
    return 0.0F;
  }
  return std::clamp(value, -std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
}

std::uint8_t round_u8(float deep) noexcept {
  const float clamped = std::clamp(finite_or_zero(deep), 0.0F, 255.0F);
  return static_cast<std::uint8_t>(static_cast<int>(clamped + 0.5F));
}

std::uint16_t round_u16(float deep) noexcept {
  const float clamped = std::clamp(finite_or_zero(deep), 0.0F, 255.0F) * 257.0F;
  return static_cast<std::uint16_t>(static_cast<std::int32_t>(clamped + 0.5F));
}

std::uint16_t read_u16(const std::uint8_t* bytes) noexcept {
  std::uint16_t value = 0;
  std::memcpy(&value, bytes, sizeof(value));
  return value;
}

float read_f32(const std::uint8_t* bytes) noexcept {
  float value = 0.0F;
  std::memcpy(&value, bytes, sizeof(value));
  return value;
}

void write_u16(std::uint8_t* bytes, std::uint16_t value) noexcept {
  std::memcpy(bytes, &value, sizeof(value));
}

void write_f32(std::uint8_t* bytes, float value) noexcept {
  std::memcpy(bytes, &value, sizeof(value));
}

void check_row_span(const PixelBuffer& buffer, std::int32_t y, std::int32_t x, std::int32_t count) {
  if (y < 0 || y >= buffer.height() || x < 0 || count < 0 || x + count > buffer.width()) {
    throw std::out_of_range(PATCHY_TRANSLATE_NOOP("QObject", "Pixel row access is outside the buffer"));
  }
}

}  // namespace

bool deep_editing_enabled() {
  const int forced = g_deep_editing_override.load(std::memory_order_relaxed);
  if (forced >= 0) {
    return forced != 0;
  }
  return deep_editing_from_environment();
}

void set_deep_editing_override(std::optional<bool> enabled) {
  g_deep_editing_override.store(enabled.has_value() ? (*enabled ? 1 : 0) : -1, std::memory_order_relaxed);
}

DeepDomain deep_domain_for(BitDepth document_depth) noexcept {
  return native_domain(document_depth);
}

PixelFormat with_bit_depth(PixelFormat format, BitDepth depth) noexcept {
  format.bit_depth = depth;
  return format;
}

double srgb_decode(double encoded) noexcept {
  encoded = std::clamp(encoded, 0.0, 1.0);
  return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
}

double srgb_encode(double linear) noexcept {
  linear = std::clamp(linear, 0.0, 1.0);
  return linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

void load_rgba_row(const PixelBuffer& buffer, std::int32_t y, std::int32_t x, std::int32_t count,
                   DeepDomain domain, std::span<float> out) {
  check_row_span(buffer, y, x, count);
  if (out.size() < static_cast<std::size_t>(count) * 4U) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Pixel row access is outside the buffer"));
  }
  const auto format = buffer.format();
  const auto channels = static_cast<std::size_t>(format.channels);
  const auto sample_bytes = bytes_per_channel(format.bit_depth);
  const auto* row = buffer.row(y).data() + static_cast<std::size_t>(x) * channels * sample_bytes;
  const bool color = channels >= 3;
  const bool decode = color && domain == DeepDomain::Linear && format.bit_depth != BitDepth::Float32;
  const bool encode = color && domain == DeepDomain::Encoded && format.bit_depth == BitDepth::Float32;
  const auto& u8_linear = linear_from_u8_table();
  for (std::int32_t i = 0; i < count; ++i) {
    float values[4] = {0.0F, 0.0F, 0.0F, 255.0F};
    const auto* pixel = row + static_cast<std::size_t>(i) * channels * sample_bytes;
    for (std::size_t c = 0; c < std::min<std::size_t>(channels, 4U); ++c) {
      const auto* sample = pixel + c * sample_bytes;
      const bool is_color = color && c < 3U;
      switch (format.bit_depth) {
        case BitDepth::UInt8:
          values[c] = decode && is_color ? u8_linear[*sample] : static_cast<float>(*sample);
          break;
        case BitDepth::UInt16: {
          const auto raw = read_u16(sample);
          values[c] = decode && is_color ? linear_from_u16_table()[raw] : static_cast<float>(raw) / 257.0F;
          break;
        }
        case BitDepth::Float32: {
          const auto raw = finite_or_zero(read_f32(sample));
          if (is_color) {
            values[c] = encode ? encode_deep(raw * 255.0F) : raw * 255.0F;
          } else {
            values[c] = std::clamp(raw, 0.0F, 1.0F) * 255.0F;
          }
          break;
        }
      }
    }
    auto* target = out.data() + static_cast<std::size_t>(i) * 4U;
    if (channels == 1U) {
      target[0] = target[1] = target[2] = target[3] = values[0];
    } else if (channels == 2U) {
      target[0] = target[1] = target[2] = values[0];
      target[3] = values[1];
    } else {
      std::copy(values, values + 4, target);
    }
  }
}

void store_rgba_row(PixelBuffer& buffer, std::int32_t y, std::int32_t x, std::int32_t count,
                    DeepDomain domain, std::span<const float> in) {
  check_row_span(buffer, y, x, count);
  if (in.size() < static_cast<std::size_t>(count) * 4U) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Pixel row access is outside the buffer"));
  }
  const auto format = buffer.format();
  const auto channels = static_cast<std::size_t>(format.channels);
  if (channels < 3U) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "RGBA rows need a color buffer"));
  }
  const auto sample_bytes = bytes_per_channel(format.bit_depth);
  auto* row = buffer.row(y).data() + static_cast<std::size_t>(x) * channels * sample_bytes;
  const bool to_linear = domain == DeepDomain::Encoded && format.bit_depth == BitDepth::Float32;
  const bool to_encoded = domain == DeepDomain::Linear && format.bit_depth != BitDepth::Float32;
  for (std::int32_t i = 0; i < count; ++i) {
    const auto* source = in.data() + static_cast<std::size_t>(i) * 4U;
    auto* pixel = row + static_cast<std::size_t>(i) * channels * sample_bytes;
    for (std::size_t c = 0; c < std::min<std::size_t>(channels, 4U); ++c) {
      float value = finite_or_zero(source[c]);
      const bool is_color = c < 3U;
      if (is_color && to_linear) {
        value = decode_deep(value);
      } else if (is_color && to_encoded) {
        value = encode_deep(value);
      }
      auto* sample = pixel + c * sample_bytes;
      switch (format.bit_depth) {
        case BitDepth::UInt8:
          *sample = round_u8(value);
          break;
        case BitDepth::UInt16:
          write_u16(sample, round_u16(value));
          break;
        case BitDepth::Float32:
          write_f32(sample, is_color ? value / 255.0F : std::clamp(value / 255.0F, 0.0F, 1.0F));
          break;
      }
    }
  }
}

void load_coverage_row(const PixelBuffer& buffer, std::int32_t y, std::int32_t x, std::int32_t count,
                       std::span<float> out) {
  check_row_span(buffer, y, x, count);
  if (out.size() < static_cast<std::size_t>(count)) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Pixel row access is outside the buffer"));
  }
  const auto format = buffer.format();
  const auto channels = static_cast<std::size_t>(format.channels);
  const auto sample_bytes = bytes_per_channel(format.bit_depth);
  const auto* row = buffer.row(y).data() + static_cast<std::size_t>(x) * channels * sample_bytes;
  for (std::int32_t i = 0; i < count; ++i) {
    const auto* sample = row + static_cast<std::size_t>(i) * channels * sample_bytes;
    switch (format.bit_depth) {
      case BitDepth::UInt8:
        out[static_cast<std::size_t>(i)] = static_cast<float>(*sample);
        break;
      case BitDepth::UInt16:
        out[static_cast<std::size_t>(i)] = static_cast<float>(read_u16(sample)) / 257.0F;
        break;
      case BitDepth::Float32:
        out[static_cast<std::size_t>(i)] = std::clamp(finite_or_zero(read_f32(sample)), 0.0F, 1.0F) * 255.0F;
        break;
    }
  }
}

void store_coverage_row(PixelBuffer& buffer, std::int32_t y, std::int32_t x, std::int32_t count,
                        std::span<const float> in) {
  check_row_span(buffer, y, x, count);
  if (in.size() < static_cast<std::size_t>(count)) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Pixel row access is outside the buffer"));
  }
  const auto format = buffer.format();
  const auto channels = static_cast<std::size_t>(format.channels);
  const auto sample_bytes = bytes_per_channel(format.bit_depth);
  auto* row = buffer.row(y).data() + static_cast<std::size_t>(x) * channels * sample_bytes;
  for (std::int32_t i = 0; i < count; ++i) {
    auto* sample = row + static_cast<std::size_t>(i) * channels * sample_bytes;
    const float value = in[static_cast<std::size_t>(i)];
    switch (format.bit_depth) {
      case BitDepth::UInt8:
        *sample = round_u8(value);
        break;
      case BitDepth::UInt16:
        write_u16(sample, round_u16(value));
        break;
      case BitDepth::Float32:
        write_f32(sample, std::clamp(finite_or_zero(value) / 255.0F, 0.0F, 1.0F));
        break;
    }
  }
}

float coverage_at(const PixelBuffer& buffer, std::int32_t x, std::int32_t y) {
  const auto* sample = buffer.pixel(x, y);
  switch (buffer.format().bit_depth) {
    case BitDepth::UInt8:
      return static_cast<float>(*sample) / 255.0F;
    case BitDepth::UInt16:
      return static_cast<float>(read_u16(sample)) / 65535.0F;
    case BitDepth::Float32:
      return std::clamp(finite_or_zero(read_f32(sample)), 0.0F, 1.0F);
  }
  return 0.0F;
}

float pixel_alpha_at(const PixelBuffer& buffer, std::int32_t x, std::int32_t y) {
  const auto format = buffer.format();
  if (format.channels < 4) {
    return 1.0F;
  }
  const auto* sample = buffer.pixel(x, y) + 3U * bytes_per_channel(format.bit_depth);
  switch (format.bit_depth) {
    case BitDepth::UInt8:
      return static_cast<float>(*sample) / 255.0F;
    case BitDepth::UInt16:
      return static_cast<float>(read_u16(sample)) / 65535.0F;
    case BitDepth::Float32:
      return std::clamp(finite_or_zero(read_f32(sample)), 0.0F, 1.0F);
  }
  return 1.0F;
}

std::array<std::uint8_t, 4> display_rgba8_at(const PixelBuffer& buffer, std::int32_t x, std::int32_t y) {
  std::array<float, 4> values{};
  load_rgba_row(buffer, y, x, 1, DeepDomain::Encoded, values);
  return {round_u8(values[0]), round_u8(values[1]), round_u8(values[2]), round_u8(values[3])};
}

PixelBuffer convert_pixel_buffer_depth(const PixelBuffer& source, BitDepth depth, SampleKind kind) {
  const auto from = source.format().bit_depth;
  if (from == depth || source.empty()) {
    if (source.empty()) {
      return PixelBuffer(source.width(), source.height(), with_bit_depth(source.format(), depth));
    }
    return source;
  }
  PixelBuffer result(source.width(), source.height(), with_bit_depth(source.format(), depth));
  const auto channels = static_cast<std::size_t>(source.format().channels);
  const bool color = kind == SampleKind::Color && channels >= 3U;
  const auto from_bytes = bytes_per_channel(from);
  const auto to_bytes = bytes_per_channel(depth);
  const auto samples_per_row = static_cast<std::size_t>(source.width()) * channels;
  const auto& u8_linear = linear_from_u8_table();
  for (std::int32_t y = 0; y < source.height(); ++y) {
    const auto* in = source.row(y).data();
    auto* out = result.row(y).data();
    for (std::size_t s = 0; s < samples_per_row; ++s) {
      const bool is_color = color && (s % channels) < 3U;
      const auto* sample = in + s * from_bytes;
      auto* target = out + s * to_bytes;
      if (from == BitDepth::UInt8 && depth == BitDepth::UInt16) {
        write_u16(target, widen_u8_to_u16(*sample));
      } else if (from == BitDepth::UInt16 && depth == BitDepth::UInt8) {
        *target = narrow_u16_to_u8(read_u16(sample));
      } else if (depth == BitDepth::Float32) {
        float value = 0.0F;
        if (from == BitDepth::UInt8) {
          value = is_color ? u8_linear[*sample] / 255.0F : static_cast<float>(*sample) / 255.0F;
        } else {
          const auto raw = read_u16(sample);
          value = is_color ? linear_from_u16_table()[raw] / 255.0F : static_cast<float>(raw) / 65535.0F;
        }
        write_f32(target, value);
      } else {
        // From 32-bit: clamp, then encode color; coverage scales linearly.
        const float raw = std::clamp(finite_or_zero(read_f32(sample)), 0.0F, 1.0F);
        if (depth == BitDepth::UInt8) {
          *target = is_color ? linear_to_srgb8(raw) : static_cast<std::uint8_t>(std::lround(raw * 255.0F));
        } else {
          const double encoded = is_color ? srgb_encode(raw) : static_cast<double>(raw);
          write_u16(target, static_cast<std::uint16_t>(std::lround(encoded * 65535.0)));
        }
      }
    }
  }
  return result;
}

}  // namespace patchy
