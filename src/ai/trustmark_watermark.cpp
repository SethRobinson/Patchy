#include "ai/trustmark_watermark.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace patchy::ai {
namespace {

bool usable(const PixelBuffer& image) {
  const auto format = image.format();
  return !image.empty() && format.bit_depth == BitDepth::UInt8 && format.color_mode == ColorMode::RGB &&
         (format.channels == 3 || format.channels == 4);
}

// PIL's resampling coefficients for one axis (ImagingResample with the bilinear filter):
// output sample i covers source positions around (i + 0.5) * scale with a triangle filter
// whose support grows with the reduction.
struct AxisWeights {
  std::vector<int> first;
  std::vector<std::vector<double>> weights;
};

AxisWeights axis_weights(int source_size, int output_size) {
  AxisWeights axis;
  axis.first.resize(static_cast<std::size_t>(output_size));
  axis.weights.resize(static_cast<std::size_t>(output_size));
  const double scale = static_cast<double>(source_size) / output_size;
  const double filter_scale = std::max(scale, 1.0);
  const double support = filter_scale;
  for (int i = 0; i < output_size; ++i) {
    const double center = (i + 0.5) * scale;
    const int lo = std::max(static_cast<int>(center - support + 0.5), 0);
    const int hi = std::min(static_cast<int>(center + support + 0.5), source_size);
    auto& weights = axis.weights[static_cast<std::size_t>(i)];
    double total = 0.0;
    for (int x = lo; x < hi; ++x) {
      const double distance = std::abs((x - center + 0.5) / filter_scale);
      const double weight = distance < 1.0 ? 1.0 - distance : 0.0;
      weights.push_back(weight);
      total += weight;
    }
    if (total > 0.0) {
      for (auto& weight : weights) {
        weight /= total;
      }
    }
    axis.first[static_cast<std::size_t>(i)] = lo;
  }
  return axis;
}

std::uint8_t to_byte(double value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

// Bilinear interpolation of one axis as torch's interpolate (align_corners false).
struct Lerp {
  int lo{0};
  int hi{0};
  float t{0.0F};
};

std::vector<Lerp> upsample_axis(int source_size, int output_size) {
  std::vector<Lerp> axis(static_cast<std::size_t>(output_size));
  const double scale = static_cast<double>(source_size) / output_size;
  for (int i = 0; i < output_size; ++i) {
    const double position = std::max((i + 0.5) * scale - 0.5, 0.0);
    const int lo = std::min(static_cast<int>(position), source_size - 1);
    const int hi = std::min(lo + 1, source_size - 1);
    axis[static_cast<std::size_t>(i)] = {lo, hi, static_cast<float>(position - lo)};
  }
  return axis;
}

}  // namespace

std::string_view trustmark_variant_name(TrustMarkVariant variant) noexcept {
  switch (variant) {
    case TrustMarkVariant::Q:
      return "Q";
    case TrustMarkVariant::P:
      return "P";
    case TrustMarkVariant::C:
      return "C";
  }
  return "Q";
}

const std::vector<std::string_view>& trustmark_model_files() {
  static const std::vector<std::string_view> files{"trustmark_decoder_Q.onnx", "trustmark_decoder_P.onnx",
                                                   "trustmark_decoder_C.onnx", "trustmark_encoder_Q.onnx"};
  return files;
}

int trustmark_decoder_input_size(TrustMarkVariant variant) noexcept {
  return variant == TrustMarkVariant::P ? 224 : 256;
}

Rect trustmark_region(int width, int height, TrustMarkVariant variant) noexcept {
  if (width <= 0 || height <= 0) {
    return {};
  }
  if (variant == TrustMarkVariant::P || width > 2 * height || height > 2 * width) {
    const int side = std::min(width, height);
    return {(width - side) / 2, (height - side) / 2, side, side};
  }
  return {0, 0, width, height};
}

std::vector<float> trustmark_model_input(const PixelBuffer& image, Rect region, int size) {
  if (!usable(image) || region.width <= 0 || region.height <= 0 || size <= 0) {
    return {};
  }
  const int channels = image.format().channels;
  const auto horizontal = axis_weights(region.width, size);
  const auto vertical = axis_weights(region.height, size);
  // Horizontal pass to 8 bits, then vertical, as PIL resamples an 8-bit image.
  std::vector<std::uint8_t> rows(static_cast<std::size_t>(region.height) * static_cast<std::size_t>(size) * 3U);
  for (int y = 0; y < region.height; ++y) {
    const auto source = image.row(region.y + y);
    for (int x = 0; x < size; ++x) {
      const auto& weights = horizontal.weights[static_cast<std::size_t>(x)];
      const int first = horizontal.first[static_cast<std::size_t>(x)];
      for (int c = 0; c < 3; ++c) {
        double sum = 0.0;
        for (std::size_t k = 0; k < weights.size(); ++k) {
          const auto column = static_cast<std::size_t>(region.x + first) + k;
          sum += weights[k] * source[column * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)];
        }
        rows[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 3U +
             static_cast<std::size_t>(c)] = to_byte(sum);
      }
    }
  }
  const auto plane = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
  std::vector<float> tensor(plane * 3U);
  for (int y = 0; y < size; ++y) {
    const auto& weights = vertical.weights[static_cast<std::size_t>(y)];
    const int first = vertical.first[static_cast<std::size_t>(y)];
    for (int x = 0; x < size; ++x) {
      for (int c = 0; c < 3; ++c) {
        double sum = 0.0;
        for (std::size_t k = 0; k < weights.size(); ++k) {
          const auto row = static_cast<std::size_t>(first) + k;
          sum += weights[k] * rows[(row * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 3U +
                                   static_cast<std::size_t>(c)];
        }
        tensor[static_cast<std::size_t>(c) * plane + static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
               static_cast<std::size_t>(x)] = static_cast<float>(to_byte(sum)) / 127.5F - 1.0F;
      }
    }
  }
  return tensor;
}

TrustMarkScan scan_trustmark(const PixelBuffer& image, InferenceCancel* cancel) {
  TrustMarkScan scan;
  if (!trustmark_models_available()) {
    scan.unavailable_reason = trustmark_unavailable_reason();
    return scan;
  }
  if (!usable(image)) {
    scan.unavailable_reason = "TrustMark reads 8-bit RGB images only";
    return scan;
  }
  scan.ran = true;
  for (const auto variant : {TrustMarkVariant::Q, TrustMarkVariant::P, TrustMarkVariant::C}) {
    const auto input = trustmark_model_input(image, trustmark_region(image.width(), image.height(), variant),
                                             trustmark_decoder_input_size(variant));
    const auto logits = trustmark_decode_logits(variant, input, cancel);
    trustmark::Packet packet{};
    double magnitude = 0.0;
    for (std::size_t i = 0; i < packet.size(); ++i) {
      packet[i] = static_cast<std::uint8_t>(logits[i] > 0.0F ? 1 : 0);
      magnitude += std::abs(logits[i]);
    }
    const auto confidence = static_cast<float>(magnitude / static_cast<double>(packet.size()));
    const auto decoded = trustmark::decode_packet(packet);
    if (!decoded || confidence < kTrustMarkMinimumConfidence) {
      continue;
    }
    TrustMarkMark mark;
    mark.variant = variant;
    mark.schema = decoded->schema;
    mark.data_bits = decoded->data_bits;
    mark.text = trustmark::bits_to_text(decoded->data_bits);
    mark.hex = trustmark::bits_to_hex(decoded->data_bits);
    mark.corrected_bits = decoded->corrected_bits;
    mark.confidence = confidence;
    scan.marks.push_back(std::move(mark));
  }
  return scan;
}

void apply_trustmark_residual(PixelBuffer& image, Rect region, const std::vector<float>& cover,
                              const std::vector<float>& stego, float strength) {
  constexpr int size = kTrustMarkEncoderInputSize;
  const auto plane = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
  if (!usable(image) || region.width <= 0 || region.height <= 0 || cover.size() != plane * 3U ||
      stego.size() != plane * 3U) {
    return;
  }
  // residual = clamp(stego, -1, 1) - cover, without its per-channel mean (no color shift).
  std::vector<float> residual(plane * 3U);
  for (std::size_t c = 0; c < 3U; ++c) {
    double mean = 0.0;
    for (std::size_t i = 0; i < plane; ++i) {
      const auto index = c * plane + i;
      residual[index] = std::clamp(stego[index], -1.0F, 1.0F) - cover[index];
      mean += residual[index];
    }
    const auto offset = static_cast<float>(mean / static_cast<double>(plane));
    for (std::size_t i = 0; i < plane; ++i) {
      residual[c * plane + i] -= offset;
    }
  }
  const auto columns = upsample_axis(size, region.width);
  const auto rows = upsample_axis(size, region.height);
  const int feather = std::clamp(static_cast<int>(std::min(region.width, region.height) * 0.01), 1, 50);
  const int channels = image.format().channels;
  // The region's own pixel data is detached once here, never by workers (copy-on-write).
  auto* base = image.data().data();
  const auto stride = image.stride_bytes();
  for (int y = 0; y < region.height; ++y) {
    const auto& ry = rows[static_cast<std::size_t>(y)];
    // Feathering as the reference's feather_paste: the outer `feather` rows and columns blend
    // toward the cover, the column blend overriding the row blend at the corners.
    float row_alpha = 1.0F;
    if (y < feather) {
      row_alpha = static_cast<float>(y + 1) / static_cast<float>(feather);
    } else if (y >= region.height - feather) {
      row_alpha = static_cast<float>(region.height - y) / static_cast<float>(feather);
    }
    auto* row = base + static_cast<std::size_t>(region.y + y) * stride;
    for (int x = 0; x < region.width; ++x) {
      const auto& rx = columns[static_cast<std::size_t>(x)];
      float alpha = row_alpha;
      if (x < feather) {
        alpha = static_cast<float>(x + 1) / static_cast<float>(feather);
      } else if (x >= region.width - feather) {
        alpha = static_cast<float>(region.width - x) / static_cast<float>(feather);
      }
      auto* pixel = row + static_cast<std::size_t>(region.x + x) * static_cast<std::size_t>(channels);
      for (int c = 0; c < 3; ++c) {
        const float* r = residual.data() + static_cast<std::size_t>(c) * plane;
        const auto at = [&](int yy, int xx) {
          return r[static_cast<std::size_t>(yy) * static_cast<std::size_t>(size) + static_cast<std::size_t>(xx)];
        };
        const float top = at(ry.lo, rx.lo) + (at(ry.lo, rx.hi) - at(ry.lo, rx.lo)) * rx.t;
        const float bottom = at(ry.hi, rx.lo) + (at(ry.hi, rx.hi) - at(ry.hi, rx.lo)) * rx.t;
        const float delta = top + (bottom - top) * ry.t;
        const float original = pixel[c];
        const float marked =
            (std::clamp(delta * strength + original / 127.5F - 1.0F, -1.0F, 1.0F) * 127.5F) + 127.5F;
        pixel[c] = to_byte(alpha * marked + (1.0F - alpha) * original);
      }
    }
  }
}

void embed_trustmark_q(PixelBuffer& image, const trustmark::Packet& packet, float strength, InferenceCancel* cancel) {
  if (!usable(image)) {
    throw std::runtime_error("TrustMark marks 8-bit RGB images only");
  }
  if (!trustmark_models_available()) {
    throw std::runtime_error(trustmark_unavailable_reason());
  }
  const auto region = trustmark_region(image.width(), image.height(), TrustMarkVariant::Q);
  const auto cover = trustmark_model_input(image, region, kTrustMarkEncoderInputSize);
  std::array<float, kTrustMarkPacketBits> bits{};
  for (std::size_t i = 0; i < bits.size(); ++i) {
    bits[i] = packet[i] != 0 ? 1.0F : 0.0F;
  }
  const auto stego = trustmark_encode_q(cover, bits, cancel);
  apply_trustmark_residual(image, region, cover, stego, strength);
}

}  // namespace patchy::ai
