#include "core/invisible_watermark.hpp"

#include <algorithm>
#include <cstdlib>

namespace patchy {
namespace {

std::vector<std::uint8_t> bits_of_bytes(std::string_view bytes) {
  std::vector<std::uint8_t> bits;
  bits.reserve(bytes.size() * 8);
  for (const char ch : bytes) {
    const auto byte = static_cast<std::uint8_t>(ch);
    for (int bit = 7; bit >= 0; --bit) {
      bits.push_back(static_cast<std::uint8_t>((byte >> bit) & 1));
    }
  }
  return bits;
}

std::vector<std::uint8_t> bits_of_number(std::uint64_t value, int count) {
  std::vector<std::uint8_t> bits;
  bits.reserve(static_cast<std::size_t>(count));
  for (int bit = count - 1; bit >= 0; --bit) {
    bits.push_back(static_cast<std::uint8_t>((value >> bit) & 1U));
  }
  return bits;
}

// The library's minimum: WatermarkDecoder.decode refuses r * c < 256 * 256.
constexpr std::int64_t kMinimumPixels = 256 * 256;

// One score per 4 x 4 block of the level-1 Haar approximation (8 x 8 pixels), in the
// library's row-major block order. A coefficient is half the sum of a 2 x 2 pixel
// square of U, so the library's `abs(val) % 36 > 18` is `value % 72 > 36` on the
// integer sum (U is never negative in 8 bits), and the argmax over the block's 15
// non-DC coefficients keeps numpy's first-maximum tie rule. With `centered_chroma`
// the sum is taken relative to OpenCV's float chroma zero (U - 127.5 per pixel, so
// |sum - 510|): the domain SDXL marks in, where coefficients can be negative.
std::vector<std::uint8_t> block_scores(std::span<const std::uint8_t> pixels, int width, int height,
                                       std::size_t stride, int channels, bool red_blue_swapped,
                                       bool centered_chroma) {
  const int block_rows = height / 8;
  const int block_cols = width / 8;
  std::vector<std::uint8_t> scores;
  scores.reserve(static_cast<std::size_t>(block_rows) * static_cast<std::size_t>(block_cols));
  const int red = red_blue_swapped ? 2 : 0;
  const int blue = red_blue_swapped ? 0 : 2;
  const int used_cols = block_cols * 8;
  // U for the 8 pixel rows of one block row.
  std::vector<std::int32_t> u(static_cast<std::size_t>(used_cols) * 8);
  for (int block_row = 0; block_row < block_rows; ++block_row) {
    for (int row = 0; row < 8; ++row) {
      const auto offset = static_cast<std::size_t>(block_row * 8 + row) * stride;
      const auto* source = pixels.data() + offset;
      auto* out = u.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(used_cols);
      for (int x = 0; x < used_cols; ++x) {
        const auto* px = source + static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
        const std::int32_t r = px[red];
        const std::int32_t g = px[1];
        const std::int32_t b = px[blue];
        // OpenCV COLOR_BGR2YUV, 8-bit: 14-bit fixed point, rounded (CV_DESCALE).
        const std::int32_t y = (b * 1868 + g * 9617 + r * 4899 + (1 << 13)) >> 14;
        const std::int32_t value = ((b - y) * 8061 + (128 << 14) + (1 << 13)) >> 14;
        out[x] = std::clamp(value, 0, 255);
      }
    }
    for (int block_col = 0; block_col < block_cols; ++block_col) {
      std::int32_t best = -1;
      for (int k = 1; k < 16; ++k) {
        const int r = k / 4;
        const int c = k % 4;
        const auto top = static_cast<std::size_t>(2 * r) * static_cast<std::size_t>(used_cols) +
                         static_cast<std::size_t>(block_col * 8 + 2 * c);
        const auto bottom = top + static_cast<std::size_t>(used_cols);
        const std::int32_t sum = u[top] + u[top + 1] + u[bottom] + u[bottom + 1];
        const std::int32_t value = centered_chroma ? std::abs(sum - 510) : sum;
        if (value > best) {
          best = value;
        }
      }
      scores.push_back(static_cast<std::uint8_t>(best % 72 > 36 ? 1 : 0));
    }
  }
  return scores;
}

// Two-cluster split of the per-bit scores (q_i, 0..1024): the threshold between
// distinct values that maximizes the between-group variance, (S1 n0 - S0 n1)^2 / (n0 n1),
// compared by cross-multiplication in exact integers (fits in 64 bits for these sizes).
// The first best split wins. No variation at all decodes as all zeros.
std::vector<std::uint8_t> two_cluster_bits(const std::vector<std::int64_t>& q) {
  const auto count = q.size();
  std::vector<std::size_t> order(count);
  for (std::size_t i = 0; i < count; ++i) {
    order[i] = i;
  }
  std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return q[a] < q[b]; });
  std::int64_t total = 0;
  for (const auto value : q) {
    total += value;
  }
  std::int64_t lower_sum = 0;
  std::uint64_t best_numerator = 0;
  std::uint64_t best_denominator = 1;
  std::int64_t threshold = -1;  // bits strictly above it decode as 1
  for (std::size_t k = 1; k < count; ++k) {
    lower_sum += q[order[k - 1]];
    if (q[order[k - 1]] == q[order[k]]) {
      continue;
    }
    const auto n0 = static_cast<std::int64_t>(k);
    const auto n1 = static_cast<std::int64_t>(count - k);
    const auto difference = static_cast<std::uint64_t>((total - lower_sum) * n0 - lower_sum * n1);
    const auto numerator = difference * difference;
    const auto denominator = static_cast<std::uint64_t>(n0 * n1);
    if (numerator * best_denominator > best_numerator * denominator) {
      best_numerator = numerator;
      best_denominator = denominator;
      threshold = q[order[k - 1]];
    }
  }
  std::vector<std::uint8_t> bits(count, 0);
  if (threshold >= 0) {
    for (std::size_t i = 0; i < count; ++i) {
      bits[i] = static_cast<std::uint8_t>(q[i] > threshold ? 1 : 0);
    }
  }
  return bits;
}

std::vector<std::uint8_t> vote(const std::vector<std::uint8_t>& scores, int bit_count, DwtDctBitRule rule) {
  std::vector<std::int64_t> ones(static_cast<std::size_t>(bit_count), 0);
  std::vector<std::int64_t> totals(static_cast<std::size_t>(bit_count), 0);
  for (std::size_t n = 0; n < scores.size(); ++n) {
    const auto bit = n % static_cast<std::size_t>(bit_count);
    ones[bit] += scores[n];
    ++totals[bit];
  }
  if (rule == DwtDctBitRule::TwoCluster) {
    std::vector<std::int64_t> q(static_cast<std::size_t>(bit_count), 0);
    for (std::size_t i = 0; i < q.size(); ++i) {
      q[i] = totals[i] > 0 ? ones[i] * 1024 / totals[i] : 0;
    }
    return two_cluster_bits(q);
  }
  std::vector<std::uint8_t> bits(static_cast<std::size_t>(bit_count), 0);
  for (std::size_t i = 0; i < bits.size(); ++i) {
    // The library's `mean * 255 > 127`, kept exact in integers.
    bits[i] = static_cast<std::uint8_t>(totals[i] > 0 && ones[i] * 255 > totals[i] * 127 ? 1 : 0);
  }
  return bits;
}

bool valid_input(std::span<const std::uint8_t> pixels, int width, int height, std::size_t stride, int channels) {
  if (width <= 0 || height <= 0 || (channels != 3 && channels != 4)) {
    return false;
  }
  if (static_cast<std::int64_t>(width) * height < kMinimumPixels) {
    return false;
  }
  const auto row_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
  return stride >= row_bytes && pixels.size() >= stride * static_cast<std::size_t>(height - 1) + row_bytes;
}

}  // namespace

const std::vector<KnownInvisibleWatermark>& known_invisible_watermarks() {
  static const std::vector<KnownInvisibleWatermark> known{
      // CompVis/stable-diffusion scripts/txt2img.py and img2img.py: wm = "StableDiffusionV1".
      {"stableDiffusion1", "Stable Diffusion 1.x", bits_of_bytes("StableDiffusionV1")},
      // Stability-AI/stablediffusion scripts: wm = "SDV2".
      {"stableDiffusion2", "Stable Diffusion 2.x", bits_of_bytes("SDV2")},
      // Stability-AI/generative-models sgm/inference/helpers.py WATERMARK_MESSAGE, also
      // diffusers' StableDiffusionXLWatermarker.
      {"stableDiffusionXL", "Stable Diffusion XL", bits_of_number(0xB3EC907BB19EULL, 48)},
  };
  return known;
}

int invisible_watermark_tolerance(int bit_count) noexcept {
  return bit_count / 12;
}

std::vector<std::uint8_t> decode_dwt_dct_bits(std::span<const std::uint8_t> pixels, int width, int height,
                                              std::size_t stride, int channels, int bit_count,
                                              const DwtDctDecodeOptions& options) {
  if (bit_count <= 0 || !valid_input(pixels, width, height, stride, channels)) {
    return {};
  }
  return vote(block_scores(pixels, width, height, stride, channels, options.red_blue_swapped,
                           options.centered_chroma),
              bit_count, options.rule);
}

InvisibleWatermarkScan scan_invisible_watermarks(std::span<const std::uint8_t> pixels, int width, int height,
                                                 std::size_t stride, int channels) {
  InvisibleWatermarkScan scan;
  if (!valid_input(pixels, width, height, stride, channels)) {
    return scan;
  }
  scan.scanned = true;
  for (const int reading : {0, 1, 2, 3}) {
    const bool swapped = (reading & 1) != 0;
    const auto scores = block_scores(pixels, width, height, stride, channels, swapped, (reading & 2) != 0);
    for (const auto& known : known_invisible_watermarks()) {
      const auto bit_count = static_cast<int>(known.bits.size());
      int differing = bit_count;
      for (const auto rule : {DwtDctBitRule::Library, DwtDctBitRule::TwoCluster}) {
        const auto bits = vote(scores, bit_count, rule);
        int rule_differing = 0;
        for (std::size_t i = 0; i < bits.size(); ++i) {
          rule_differing += bits[i] != known.bits[i] ? 1 : 0;
        }
        differing = std::min(differing, rule_differing);
      }
      if (differing > invisible_watermark_tolerance(bit_count)) {
        continue;
      }
      const auto existing = std::find_if(scan.matches.begin(), scan.matches.end(),
                                         [&](const auto& match) { return match.id == known.id; });
      if (existing == scan.matches.end()) {
        scan.matches.push_back({known.id, known.name, bit_count, differing, swapped});
      } else if (differing < existing->differing_bits) {
        *existing = {known.id, known.name, bit_count, differing, swapped};
      }
    }
  }
  return scan;
}

InvisibleWatermarkScan scan_invisible_watermarks(const PixelBuffer& rgb8) {
  const auto format = rgb8.format();
  if (rgb8.empty() || format.bit_depth != BitDepth::UInt8 || format.color_mode != ColorMode::RGB) {
    return {};
  }
  return scan_invisible_watermarks(rgb8.data(), rgb8.width(), rgb8.height(), rgb8.stride_bytes(), format.channels);
}

}  // namespace patchy
