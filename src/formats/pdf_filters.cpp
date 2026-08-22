#include "formats/pdf_filters.hpp"

#include "formats/miniz/miniz.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace patchy::pdf {
namespace {

// A single stream must not be allowed to exhaust memory on a hostile or damaged
// file. 512 MB is far above any real page content stream or image plane.
constexpr std::size_t kMaximumDecodedBytes = 512u * 1024u * 1024u;

int hex_value(unsigned char byte) noexcept {
  if (byte >= '0' && byte <= '9') {
    return byte - '0';
  }
  if (byte >= 'a' && byte <= 'f') {
    return byte - 'a' + 10;
  }
  if (byte >= 'A' && byte <= 'F') {
    return byte - 'A' + 10;
  }
  return -1;
}

int parms_int(const Dictionary& parms, std::string_view key, int fallback) {
  const auto it = parms.find(key);
  if (it == parms.end() || !it->second.is_number()) {
    return fallback;
  }
  return static_cast<int>(it->second.integer(fallback));
}

bool parms_bool(const Dictionary& parms, std::string_view key, bool fallback) {
  const auto it = parms.find(key);
  if (it == parms.end()) {
    return fallback;
  }
  if (it->second.is_boolean()) {
    return it->second.boolean(fallback);
  }
  if (it->second.is_number()) {
    return it->second.integer(fallback ? 1 : 0) != 0;
  }
  return fallback;
}

}  // namespace

FilterKind filter_kind_from_name(std::string_view name) noexcept {
  // Both the full names and the abbreviations inline images use (table 94).
  if (name == "FlateDecode" || name == "Fl") {
    return FilterKind::Flate;
  }
  if (name == "LZWDecode" || name == "LZW") {
    return FilterKind::Lzw;
  }
  if (name == "ASCII85Decode" || name == "A85") {
    return FilterKind::Ascii85;
  }
  if (name == "ASCIIHexDecode" || name == "AHx") {
    return FilterKind::AsciiHex;
  }
  if (name == "RunLengthDecode" || name == "RL") {
    return FilterKind::RunLength;
  }
  if (name == "Crypt") {
    return FilterKind::Crypt;
  }
  if (name == "DCTDecode" || name == "DCT") {
    return FilterKind::Dct;
  }
  if (name == "JPXDecode") {
    return FilterKind::Jpx;
  }
  if (name == "CCITTFaxDecode" || name == "CCF") {
    return FilterKind::CcittFax;
  }
  if (name == "JBIG2Decode") {
    return FilterKind::Jbig2;
  }
  if (name.empty()) {
    return FilterKind::None;
  }
  return FilterKind::Unknown;
}

bool filter_is_image_codec(FilterKind kind) noexcept {
  return kind == FilterKind::Dct || kind == FilterKind::Jpx || kind == FilterKind::CcittFax ||
         kind == FilterKind::Jbig2;
}

std::string_view image_codec_extension(FilterKind kind) noexcept {
  switch (kind) {
    case FilterKind::Dct: return "jpg";
    case FilterKind::Jpx: return "jp2";
    default: return {};
  }
}

DecodeResult inflate_bytes(std::span<const std::uint8_t> data) {
  DecodeResult result;
  if (data.empty()) {
    return result;
  }

  // Streaming rather than tinfl_decompress_mem_to_heap so a truncated or corrupt
  // stream keeps the bytes decoded before the break. Damaged Flate streams are
  // common enough in real PDFs that every viewer renders what it got, and the
  // alternative here is losing a whole page over one bad byte.
  tinfl_decompressor decompressor;
  tinfl_init(&decompressor);

  // Some producers omit the zlib header. Try with it, then without.
  for (const mz_uint header_flag : {static_cast<mz_uint>(TINFL_FLAG_PARSE_ZLIB_HEADER), static_cast<mz_uint>(0)}) {
    tinfl_init(&decompressor);
    std::vector<std::uint8_t> out;
    std::array<std::uint8_t, 32768> window{};
    std::size_t in_position = 0;
    std::size_t window_position = 0;
    tinfl_status status = TINFL_STATUS_NEEDS_MORE_INPUT;
    while (true) {
      std::size_t in_bytes = data.size() - in_position;
      std::size_t out_bytes = window.size() - window_position;
      status = tinfl_decompress(&decompressor, data.data() + in_position, &in_bytes, window.data(),
                                window.data() + window_position, &out_bytes,
                                header_flag | (in_position + in_bytes < data.size() ? TINFL_FLAG_HAS_MORE_INPUT : 0));
      in_position += in_bytes;
      if (out_bytes > 0) {
        if (out.size() + out_bytes > kMaximumDecodedBytes) {
          result.data = std::move(out);
          result.error = "a Flate stream decoded to more data than Patchy will hold";
          return result;
        }
        out.insert(out.end(), window.data() + window_position, window.data() + window_position + out_bytes);
        window_position = (window_position + out_bytes) & (window.size() - 1);
      }
      if (status == TINFL_STATUS_DONE) {
        result.data = std::move(out);
        return result;
      }
      if (status < TINFL_STATUS_DONE) {
        break;  // failed: retry without the zlib header, or report below
      }
      if (status == TINFL_STATUS_NEEDS_MORE_INPUT && in_position >= data.size()) {
        break;  // truncated
      }
    }
    if (!out.empty()) {
      // Partial output is better than none, and the caller sees the error.
      result.data = std::move(out);
      result.error = "a Flate stream ended early and was decoded as far as it went";
      return result;
    }
  }

  result.error = "a Flate stream could not be decompressed";
  return result;
}

DecodeResult decode_lzw(std::span<const std::uint8_t> data, bool early_change) {
  // The variable-code-width LZW of clause 7.4.4, which differs from GIF's in bit
  // order (MSB first) and in the EarlyChange rule.
  DecodeResult result;
  constexpr int kClearCode = 256;
  constexpr int kEodCode = 257;
  constexpr int kMaximumCode = 4096;

  std::array<int, kMaximumCode> prefix{};
  std::array<std::uint8_t, kMaximumCode> suffix{};
  std::vector<std::uint8_t> sequence;
  sequence.reserve(64);

  int next_code = kEodCode + 1;
  int code_width = 9;
  int previous = -1;
  std::uint32_t bit_buffer = 0;
  int bit_count = 0;
  std::size_t position = 0;

  const auto emit = [&](int code) {
    sequence.clear();
    int walker = code;
    while (walker >= kEodCode + 1 && walker < kMaximumCode) {
      sequence.push_back(suffix[static_cast<std::size_t>(walker)]);
      walker = prefix[static_cast<std::size_t>(walker)];
      if (sequence.size() > kMaximumCode) {
        return false;  // a cycle in a corrupt table
      }
    }
    if (walker < 0 || walker > 255) {
      return false;
    }
    sequence.push_back(static_cast<std::uint8_t>(walker));
    result.data.insert(result.data.end(), sequence.rbegin(), sequence.rend());
    return result.data.size() <= kMaximumDecodedBytes;
  };

  while (true) {
    while (bit_count < code_width) {
      if (position >= data.size()) {
        if (result.data.empty()) {
          result.error = "an LZW stream ended before any data was decoded";
        }
        return result;
      }
      bit_buffer = (bit_buffer << 8) | data[position++];
      bit_count += 8;
    }
    const int code = static_cast<int>((bit_buffer >> (bit_count - code_width)) & ((1U << code_width) - 1U));
    bit_count -= code_width;

    if (code == kEodCode) {
      return result;
    }
    if (code == kClearCode) {
      next_code = kEodCode + 1;
      code_width = 9;
      previous = -1;
      continue;
    }
    if (previous < 0) {
      if (code > 255) {
        result.error = "an LZW stream started with an invalid code";
        return result;
      }
      if (!emit(code)) {
        result.error = "an LZW stream decoded to more data than Patchy will hold";
        return result;
      }
      previous = code;
      continue;
    }

    const int decoded = code < next_code ? code : previous;
    const std::size_t before = result.data.size();
    if (!emit(decoded)) {
      result.error = "an LZW stream contained a corrupt code table";
      return result;
    }
    const std::uint8_t first = result.data[before];
    if (code >= next_code) {
      result.data.push_back(first);
    }
    if (next_code < kMaximumCode) {
      prefix[static_cast<std::size_t>(next_code)] = previous;
      suffix[static_cast<std::size_t>(next_code)] = first;
      ++next_code;
    }
    previous = code;

    // EarlyChange (the default) widens one code sooner than the table actually needs.
    const int threshold = next_code + (early_change ? 1 : 0);
    if (threshold > 4096) {
      code_width = 12;
    } else if (threshold > 2048) {
      code_width = 12;
    } else if (threshold > 1024) {
      code_width = 11;
    } else if (threshold > 512) {
      code_width = 10;
    } else {
      code_width = 9;
    }
    code_width = std::min(code_width, 12);
  }
}

DecodeResult decode_ascii85(std::span<const std::uint8_t> data) {
  DecodeResult result;
  std::uint32_t group = 0;
  int count = 0;
  std::size_t position = 0;
  // A leading "<~" is legal and some producers emit it.
  if (data.size() >= 2 && data[0] == '<' && data[1] == '~') {
    position = 2;
  }
  for (; position < data.size(); ++position) {
    const auto byte = data[position];
    if (byte == '~') {
      break;  // "~>" ends the stream
    }
    if (is_whitespace(byte)) {
      continue;
    }
    if (byte == 'z' && count == 0) {
      result.data.insert(result.data.end(), 4, 0);
      continue;
    }
    if (byte < '!' || byte > 'u') {
      continue;  // junk between groups is ignored rather than fatal
    }
    group = group * 85 + static_cast<std::uint32_t>(byte - '!');
    if (++count < 5) {
      continue;
    }
    for (int shift = 24; shift >= 0; shift -= 8) {
      result.data.push_back(static_cast<std::uint8_t>((group >> shift) & 0xFFU));
    }
    group = 0;
    count = 0;
    if (result.data.size() > kMaximumDecodedBytes) {
      result.error = "an ASCII85 stream decoded to more data than Patchy will hold";
      return result;
    }
  }
  if (count > 0) {
    // A partial final group is padded with 'u' and yields count-1 bytes.
    if (count == 1) {
      result.error = "an ASCII85 stream ended with an incomplete group";
      return result;
    }
    for (int pad = count; pad < 5; ++pad) {
      group = group * 85 + 84;
    }
    for (int index = 0; index < count - 1; ++index) {
      result.data.push_back(static_cast<std::uint8_t>((group >> (24 - index * 8)) & 0xFFU));
    }
  }
  return result;
}

DecodeResult decode_ascii_hex(std::span<const std::uint8_t> data) {
  DecodeResult result;
  int high = -1;
  for (const auto byte : data) {
    if (byte == '>') {
      break;
    }
    const int digit = hex_value(byte);
    if (digit < 0) {
      continue;
    }
    if (high < 0) {
      high = digit;
      continue;
    }
    result.data.push_back(static_cast<std::uint8_t>(high * 16 + digit));
    high = -1;
  }
  if (high >= 0) {
    result.data.push_back(static_cast<std::uint8_t>(high * 16));
  }
  return result;
}

DecodeResult decode_run_length(std::span<const std::uint8_t> data) {
  DecodeResult result;
  std::size_t position = 0;
  while (position < data.size()) {
    const int length = data[position++];
    if (length == 128) {
      break;  // end-of-data marker
    }
    if (length < 128) {
      const std::size_t count = static_cast<std::size_t>(length) + 1;
      if (position + count > data.size()) {
        result.data.insert(result.data.end(), data.begin() + static_cast<std::ptrdiff_t>(position), data.end());
        result.error = "a RunLength stream ended mid-run";
        return result;
      }
      result.data.insert(result.data.end(), data.begin() + static_cast<std::ptrdiff_t>(position),
                         data.begin() + static_cast<std::ptrdiff_t>(position + count));
      position += count;
      continue;
    }
    if (position >= data.size()) {
      result.error = "a RunLength stream ended mid-run";
      return result;
    }
    result.data.insert(result.data.end(), 257 - static_cast<std::size_t>(length), data[position++]);
  }
  return result;
}

DecodeResult undo_predictor(std::vector<std::uint8_t> data, int predictor, int colors, int bits_per_component,
                            int columns) {
  DecodeResult result;
  if (predictor <= 1) {
    result.data = std::move(data);
    return result;
  }
  colors = std::clamp(colors, 1, 32);
  bits_per_component = std::clamp(bits_per_component, 1, 16);
  columns = std::max(columns, 1);

  const std::size_t bits_per_pixel = static_cast<std::size_t>(colors) * static_cast<std::size_t>(bits_per_component);
  const std::size_t row_bytes = (bits_per_pixel * static_cast<std::size_t>(columns) + 7) / 8;
  // The distance back to the same component in the previous pixel, at least one byte.
  const std::size_t pixel_bytes = std::max<std::size_t>(1, bits_per_pixel / 8);

  if (predictor == 2) {
    // TIFF horizontal differencing. Only the 8-bit case is representable byte-wise;
    // sub-byte depths are rare enough that leaving them alone beats guessing.
    if (bits_per_component != 8) {
      result.data = std::move(data);
      result.error = "a TIFF predictor with sub-byte samples was left undecoded";
      return result;
    }
    for (std::size_t row = 0; row * row_bytes < data.size(); ++row) {
      const std::size_t base = row * row_bytes;
      const std::size_t end = std::min(base + row_bytes, data.size());
      for (std::size_t index = base + pixel_bytes; index < end; ++index) {
        data[index] = static_cast<std::uint8_t>(data[index] + data[index - pixel_bytes]);
      }
    }
    result.data = std::move(data);
    return result;
  }

  // PNG predictors: each row is prefixed with a filter-type byte (RFC 2083).
  const std::size_t stride = row_bytes + 1;
  if (stride == 0) {
    result.data = std::move(data);
    return result;
  }
  const std::size_t rows = data.size() / stride;
  std::vector<std::uint8_t> out;
  out.resize(rows * row_bytes);
  std::vector<std::uint8_t> previous(row_bytes, 0);

  for (std::size_t row = 0; row < rows; ++row) {
    const std::uint8_t* source = data.data() + row * stride;
    const std::uint8_t filter = source[0];
    ++source;
    std::uint8_t* target = out.data() + row * row_bytes;
    for (std::size_t index = 0; index < row_bytes; ++index) {
      const int raw = source[index];
      const int left = index >= pixel_bytes ? target[index - pixel_bytes] : 0;
      const int up = previous[index];
      const int up_left = index >= pixel_bytes ? previous[index - pixel_bytes] : 0;
      int value = raw;
      switch (filter) {
        case 0: break;                       // None
        case 1: value = raw + left; break;   // Sub
        case 2: value = raw + up; break;     // Up
        case 3: value = raw + ((left + up) / 2); break;  // Average
        case 4: {                            // Paeth
          const int estimate = left + up - up_left;
          const int distance_left = std::abs(estimate - left);
          const int distance_up = std::abs(estimate - up);
          const int distance_up_left = std::abs(estimate - up_left);
          const int nearest = (distance_left <= distance_up && distance_left <= distance_up_left) ? left
                              : (distance_up <= distance_up_left)                                 ? up
                                                                                                  : up_left;
          value = raw + nearest;
          break;
        }
        default: break;  // an unknown filter type is treated as None
      }
      target[index] = static_cast<std::uint8_t>(value & 0xFF);
    }
    std::copy_n(target, row_bytes, previous.begin());
  }
  result.data = std::move(out);
  return result;
}

DecodeResult apply_filter(FilterKind kind, std::span<const std::uint8_t> data, const Dictionary& parms) {
  DecodeResult result;
  switch (kind) {
    case FilterKind::None:
    case FilterKind::Crypt:
      result.data.assign(data.begin(), data.end());
      return result;
    case FilterKind::Flate: result = inflate_bytes(data); break;
    case FilterKind::Lzw: result = decode_lzw(data, parms_bool(parms, "EarlyChange", true)); break;
    case FilterKind::Ascii85: return decode_ascii85(data);
    case FilterKind::AsciiHex: return decode_ascii_hex(data);
    case FilterKind::RunLength: return decode_run_length(data);
    case FilterKind::Dct:
    case FilterKind::Jpx:
    case FilterKind::CcittFax:
    case FilterKind::Jbig2:
      result.data.assign(data.begin(), data.end());
      result.image_codec = kind;
      return result;
    case FilterKind::Unknown:
      result.data.assign(data.begin(), data.end());
      result.error = "an unsupported stream filter was skipped";
      return result;
  }

  // Only Flate and LZW carry predictors.
  const int predictor = parms_int(parms, "Predictor", 1);
  if (predictor > 1 && !result.data.empty()) {
    auto predicted = undo_predictor(std::move(result.data), predictor, parms_int(parms, "Colors", 1),
                                    parms_int(parms, "BitsPerComponent", 8), parms_int(parms, "Columns", 1));
    if (result.error.empty()) {
      result.error = predicted.error;
    }
    result.data = std::move(predicted.data);
  }
  return result;
}

DecodeResult apply_filter_chain(std::span<const std::uint8_t> data, const std::vector<FilterStep>& steps) {
  DecodeResult result;
  result.data.assign(data.begin(), data.end());
  for (const auto& step : steps) {
    if (filter_is_image_codec(step.kind)) {
      result.image_codec = step.kind;
      return result;  // hand the still-encoded image bytes back
    }
    auto next = apply_filter(step.kind, result.data, step.parms);
    if (!next.error.empty() && result.error.empty()) {
      result.error = next.error;
    }
    result.data = std::move(next.data);
    if (result.data.empty() && !result.error.empty()) {
      return result;
    }
  }
  return result;
}

}  // namespace patchy::pdf
