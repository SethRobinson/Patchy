#include "formats/avif_document_io.hpp"

#include "core/pixel_depth.hpp"
#include "core/worker_budget.hpp"
#include "formats/animation_timing.hpp"
#include "formats/document_flatten.hpp"
#include "formats/hdr_transfer.hpp"
#include "formats/jxr_document_io.hpp"
#include "render/compositor.hpp"
#include "support/string_utils.hpp"
#include "support/translate_noop.hpp"

#if defined(PATCHY_AVIF_DECODER)
#include <avif/avif.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace patchy::avif {

namespace {

// Brands from the AVIF specification: "avif" for a still image, "avis" for an image
// sequence. Files usually list "mif1"/"miaf" beside them; only these two decide.
constexpr std::array<std::string_view, 2> kAvifBrands = {"avif", "avis"};

[[nodiscard]] bool is_avif_brand(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
  if (offset + 4 > bytes.size()) {
    return false;
  }
  const std::string_view brand(reinterpret_cast<const char*>(bytes.data() + offset), 4);
  return std::find(kAvifBrands.begin(), kAvifBrands.end(), brand) != kAvifBrands.end();
}

#if defined(PATCHY_AVIF_DECODER)
std::string default_frame_name(int index, std::uint32_t delay_ms) {
  return "Frame " + std::to_string(index) + " " + animation::format_delay_seconds_token(delay_ms);
}
#endif

}  // namespace

const std::vector<std::string>& avif_extensions() {
  static const std::vector<std::string> extensions = {"avif", "avifs"};
  return extensions;
}

bool is_avif_extension(std::string_view extension) {
  const auto normalized = normalized_extension(extension, false);
  const auto& extensions = avif_extensions();
  return std::find(extensions.begin(), extensions.end(), normalized) != extensions.end();
}

bool sniff(std::span<const std::uint8_t> bytes) noexcept {
  // ISOBMFF: u32 BE box size, "ftyp", 4-byte major brand, u32 minor version, then 4-byte
  // compatible brands to the end of the box.
  if (bytes.size() < 16 || std::memcmp(bytes.data() + 4, "ftyp", 4) != 0) {
    return false;
  }
  const std::size_t box_size = (static_cast<std::size_t>(bytes[0]) << 24U) |
                               (static_cast<std::size_t>(bytes[1]) << 16U) |
                               (static_cast<std::size_t>(bytes[2]) << 8U) | static_cast<std::size_t>(bytes[3]);
  if (box_size < 16 || (box_size % 4) != 0) {
    return false;
  }
  if (is_avif_brand(bytes, 8)) {
    return true;
  }
  const auto brands_end = std::min<std::size_t>(box_size, bytes.size());
  for (std::size_t offset = 16; offset + 4 <= brands_end; offset += 4) {
    if (is_avif_brand(bytes, offset)) {
      return true;
    }
  }
  return false;
}

FormatReadResult read_avif(std::span<const std::uint8_t> bytes) {
  return std::move(read_avif_detailed(bytes).read);
}

std::vector<std::uint8_t> write_avif(const Document& document) {
  return write_avif(document, WriteOptions{});
}

#if defined(PATCHY_AVIF_DECODER)

namespace {

constexpr std::uint64_t kMaxPixels = 268'435'456ULL;
constexpr std::uint64_t kMaxAnimationPixels = 1'073'741'824ULL;

struct DecoderDeleter {
  void operator()(avifDecoder* decoder) const noexcept {
    avifDecoderDestroy(decoder);
  }
};
using DecoderPtr = std::unique_ptr<avifDecoder, DecoderDeleter>;

struct RgbPixels {
  avifRGBImage rgb{};
  bool allocated{false};
  ~RgbPixels() {
    if (allocated) {
      avifRGBImageFreePixels(&rgb);
    }
  }
};

[[nodiscard]] std::runtime_error failure(const char* noop_message, const avifDecoder* decoder, avifResult result) {
  std::string message = noop_message;
  std::string detail;
  if (decoder != nullptr && decoder->diag.error[0] != '\0') {
    detail = decoder->diag.error;
  } else if (result != AVIF_RESULT_OK) {
    detail = avifResultToString(result);
  }
  if (!detail.empty()) {
    message += ": ";
    message += detail;
  }
  return std::runtime_error(message);
}

struct DecodedFrame {
  PixelBuffer pixels;
  std::uint32_t delay_ms{0};
};

// The MIAF transforms on tightly packed pixels of `bpp` bytes, applied clap, irot, imir in
// that order as ISO/IEC 23000-22 prescribes.
PixelBuffer crop(const PixelBuffer& source, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
  PixelBuffer out(width, height, source.format());
  const auto bpp = bytes_per_pixel(source.format());
  for (std::int32_t row = 0; row < height; ++row) {
    const auto src = source.row(y + row);
    auto dst = out.row(row);
    std::memcpy(dst.data(), src.data() + static_cast<std::size_t>(x) * bpp, static_cast<std::size_t>(width) * bpp);
  }
  return out;
}

// irot: `quarter_turns` * 90 degrees anti-clockwise.
PixelBuffer rotate_anticlockwise(const PixelBuffer& source, int quarter_turns) {
  quarter_turns &= 3;
  if (quarter_turns == 0) {
    return source;
  }
  const auto w = source.width();
  const auto h = source.height();
  const bool swaps = quarter_turns % 2 == 1;
  PixelBuffer out(swaps ? h : w, swaps ? w : h, source.format());
  const auto bpp = bytes_per_pixel(source.format());
  for (std::int32_t y = 0; y < h; ++y) {
    const auto src = source.row(y);
    for (std::int32_t x = 0; x < w; ++x) {
      std::int32_t ox = 0;
      std::int32_t oy = 0;
      switch (quarter_turns) {
        case 1:  // 90 anti-clockwise: the top row becomes the left column, read upward
          ox = y;
          oy = w - 1 - x;
          break;
        case 2:
          ox = w - 1 - x;
          oy = h - 1 - y;
          break;
        default:  // 270 anti-clockwise (90 clockwise)
          ox = h - 1 - y;
          oy = x;
          break;
      }
      auto dst = out.row(oy);
      std::memcpy(dst.data() + static_cast<std::size_t>(ox) * bpp, src.data() + static_cast<std::size_t>(x) * bpp, bpp);
    }
  }
  return out;
}

// imir axis 0 exchanges top and bottom, axis 1 left and right.
PixelBuffer mirror(const PixelBuffer& source, int axis) {
  const auto w = source.width();
  const auto h = source.height();
  PixelBuffer out(w, h, source.format());
  const auto bpp = bytes_per_pixel(source.format());
  for (std::int32_t y = 0; y < h; ++y) {
    const auto src = source.row(y);
    auto dst = out.row(axis == 0 ? h - 1 - y : y);
    if (axis == 0) {
      std::memcpy(dst.data(), src.data(), static_cast<std::size_t>(w) * bpp);
    } else {
      for (std::int32_t x = 0; x < w; ++x) {
        std::memcpy(dst.data() + static_cast<std::size_t>(w - 1 - x) * bpp, src.data() + static_cast<std::size_t>(x) * bpp,
                    bpp);
      }
    }
  }
  return out;
}

PixelBuffer apply_transforms(PixelBuffer pixels, const avifImage& image, std::vector<std::string>& notices) {
  if ((image.transformFlags & AVIF_TRANSFORM_CLAP) != 0) {
    avifCropRect rect{};
    avifDiagnostics diag{};
    if (avifCropRectFromCleanApertureBox(&rect, &image.clap, image.width, image.height, &diag) == AVIF_TRUE &&
        rect.width > 0 && rect.height > 0 && rect.x + rect.width <= image.width && rect.y + rect.height <= image.height &&
        (rect.width != image.width || rect.height != image.height)) {
      pixels = crop(pixels, static_cast<std::int32_t>(rect.x), static_cast<std::int32_t>(rect.y),
                    static_cast<std::int32_t>(rect.width), static_cast<std::int32_t>(rect.height));
    } else if (rect.width == 0) {
      notices.emplace_back(PATCHY_TRANSLATE_NOOP("QObject", "AVIF: the clean aperture (crop) property was invalid and was ignored"));
    }
  }
  if ((image.transformFlags & AVIF_TRANSFORM_IROT) != 0 && image.irot.angle != 0) {
    pixels = rotate_anticlockwise(pixels, image.irot.angle);
  }
  if ((image.transformFlags & AVIF_TRANSFORM_IMIR) != 0) {
    pixels = mirror(pixels, image.imir.axis);
  }
  return pixels;
}

// How PQ and HLG files decode (nclx-only files; an ICC profile keeps the stored values).
// Unset `transfer`: an SDR file, decoded as stored.
struct HdrPlan {
  std::optional<hdr::Transfer> transfer;
  hdr::Chromaticities primaries{hdr::kBt2020Primaries};
  // Deep editing off: the linear values go through the JPEG XR highlight rolloff to 8 bits.
  bool tone_map{false};
};

HdrPlan hdr_plan_for(const avifImage& image, bool deep) {
  HdrPlan plan;
  if (image.icc.size > 0) {
    return plan;
  }
  if (image.transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_PQ) {
    plan.transfer = hdr::Transfer::Pq;
  } else if (image.transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_HLG) {
    plan.transfer = hdr::Transfer::Hlg;
  } else {
    return plan;
  }
  // HDR files without stated primaries are BT.2020, the only primaries PQ and HLG are
  // defined with in BT.2100.
  if (image.colorPrimaries != AVIF_COLOR_PRIMARIES_UNSPECIFIED) {
    float xy[8] = {};
    avifColorPrimariesGetValues(image.colorPrimaries, xy);
    plan.primaries = {xy[0], xy[1], xy[2], xy[3], xy[4], xy[5], xy[6], xy[7]};
  }
  plan.tone_map = !deep;
  return plan;
}

// One decoded image as a tightly packed RGBA buffer at the document's depth. libavif
// rescales 10 and 12-bit samples to 16 and unpremultiplies alpha on the way. HDR frames
// decode at 16 bits and convert to linear sRGB (32-bit documents) or tone map to 8 bits.
PixelBuffer convert_frame(const avifDecoder& decoder, BitDepth depth, const HdrPlan& hdr_plan) {
  const avifImage* image = decoder.image;
  RgbPixels out;
  avifRGBImageSetDefaults(&out.rgb, image);
  out.rgb.format = AVIF_RGB_FORMAT_RGBA;
  out.rgb.depth = depth == BitDepth::UInt16 || hdr_plan.transfer ? 16 : 8;
  out.rgb.alphaPremultiplied = AVIF_FALSE;
  out.rgb.maxThreads = decoder.maxThreads;
  if (avifRGBImageAllocatePixels(&out.rgb) != AVIF_RESULT_OK) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The AVIF image is too large to open"));
  }
  out.allocated = true;
  if (const auto result = avifImageYUVToRGB(image, &out.rgb); result != AVIF_RESULT_OK) {
    throw failure(PATCHY_TRANSLATE_NOOP("QObject", "The AVIF image could not be decoded"), nullptr, result);
  }
  const auto width = static_cast<std::int32_t>(image->width);
  const auto height = static_cast<std::int32_t>(image->height);
  if (hdr_plan.transfer) {
    const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint16_t> signal(pixel_count * 4U);
    for (std::int32_t y = 0; y < height; ++y) {
      std::memcpy(signal.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4U,
                  out.rgb.pixels + static_cast<std::size_t>(y) * out.rgb.rowBytes,
                  static_cast<std::size_t>(width) * 4U * sizeof(std::uint16_t));
    }
    std::vector<float> linear(pixel_count * 4U);
    hdr::signal_rgba16_to_linear_srgb(signal.data(), pixel_count, *hdr_plan.transfer, hdr_plan.primaries,
                                      linear.data());
    if (hdr_plan.tone_map) {
      const auto rgba8 = jxr::tone_map_scrgb_to_rgba8(linear, width, height);
      PixelBuffer pixels(width, height, PixelFormat::rgba8());
      std::memcpy(pixels.data().data(), rgba8.data(), rgba8.size());
      return pixels;
    }
    PixelBuffer pixels(width, height, with_bit_depth(PixelFormat::rgba8(), BitDepth::Float32));
    std::memcpy(pixels.data().data(), linear.data(), linear.size() * sizeof(float));
    return pixels;
  }
  PixelBuffer pixels(width, height, with_bit_depth(PixelFormat::rgba8(), depth));
  const auto row_bytes = static_cast<std::size_t>(width) * bytes_per_pixel(pixels.format());
  for (std::int32_t y = 0; y < height; ++y) {
    auto dst = pixels.row(y);
    std::memcpy(dst.data(), out.rgb.pixels + static_cast<std::size_t>(y) * out.rgb.rowBytes, row_bytes);
  }
  return pixels;
}

}  // namespace

bool is_available() noexcept {
  return true;
}

ReadResult read_avif_detailed(std::span<const std::uint8_t> bytes, const ReadOptions& options) {
  if (!sniff(bytes)) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This file is not an AVIF image"));
  }
  DecoderPtr decoder(avifDecoderCreate());
  if (!decoder) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The AVIF decoder could not be created"));
  }
  decoder->maxThreads = std::clamp(max_blocking_fanout_workers(std::clamp(hardware_worker_threads(), 1, 16)), 1, 16);
  // File Info reads the Exif and XMP items from the file itself (metadata_containers.cpp).
  decoder->ignoreExif = AVIF_TRUE;
  decoder->ignoreXMP = AVIF_TRUE;
  // Older encoders left out pixi and wrote odd clap boxes; open those files anyway, the way
  // most viewers do.
  decoder->strictFlags = AVIF_STRICT_DISABLED;
  decoder->imageSizeLimit = static_cast<std::uint32_t>(kMaxPixels);
  if (const auto result = avifDecoderSetIOMemory(decoder.get(), bytes.data(), bytes.size()); result != AVIF_RESULT_OK) {
    throw failure(PATCHY_TRANSLATE_NOOP("QObject", "The AVIF file is damaged or not supported"), decoder.get(), result);
  }
  if (const auto result = avifDecoderParse(decoder.get()); result != AVIF_RESULT_OK) {
    throw failure(PATCHY_TRANSLATE_NOOP("QObject", "The AVIF file is damaged or not supported"), decoder.get(), result);
  }
  const avifImage* info = decoder->image;
  const auto pixel_count = static_cast<std::uint64_t>(info->width) * info->height;
  if (info->width == 0 || info->height == 0 || pixel_count > kMaxPixels || info->width > 65535 || info->height > 65535) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The AVIF image is too large to open"));
  }
  const auto hdr_plan = hdr_plan_for(*info, deep_editing_enabled());
  const bool deep = info->depth > 8 && deep_editing_enabled();
  const auto depth = hdr_plan.transfer ? (hdr_plan.tone_map ? BitDepth::UInt8 : BitDepth::Float32)
                                       : (deep ? BitDepth::UInt16 : BitDepth::UInt8);
  const bool sequence = decoder->imageCount > 1;

  ReadResult result;
  std::vector<DecodedFrame> frames;
  std::uint64_t decoded_pixels = 0;
  avifResult next = AVIF_RESULT_OK;
  while ((next = avifDecoderNextImage(decoder.get())) == AVIF_RESULT_OK) {
    const avifImage* image = decoder->image;
    if (image->width != info->width || image->height != info->height) {
      break;  // a sequence whose frames change size: keep what was read
    }
    decoded_pixels += pixel_count;
    if (decoded_pixels > kMaxAnimationPixels) {
      result.read.notices.emplace_back(
          PATCHY_TRANSLATE_NOOP("QObject", "AVIF: the image sequence is very large; later frames were not imported"));
      break;
    }
    auto pixels = apply_transforms(convert_frame(*decoder, depth, hdr_plan), *image, result.read.notices);
    std::uint32_t delay_ms = 0;
    if (sequence && decoder->imageTiming.timescale > 0) {
      const double ms = decoder->imageTiming.duration * 1000.0;
      delay_ms = static_cast<std::uint32_t>(std::clamp(std::lround(ms), 0L, 0x7FFFFFFFL));
    }
    frames.push_back({std::move(pixels), delay_ms});
    if (!sequence) {
      break;
    }
  }
  if (frames.empty()) {
    throw failure(PATCHY_TRANSLATE_NOOP("QObject", "The AVIF image could not be decoded"), decoder.get(), next);
  }
  if (sequence && next != AVIF_RESULT_OK && next != AVIF_RESULT_NO_IMAGES_REMAINING &&
      frames.size() < static_cast<std::size_t>(decoder->imageCount)) {
    result.read.notices.emplace_back(
        PATCHY_TRANSLATE_NOOP("QObject", "AVIF: a frame of the image sequence could not be decoded; the frames before it were imported"));
  }

  const auto width = frames.front().pixels.width();
  const auto height = frames.front().pixels.height();
  Document document(width, height, with_bit_depth(PixelFormat::rgba8(), depth));
  document.color_state().bit_depth = depth;
  document.print_settings().horizontal_ppi = 72.0;
  document.print_settings().vertical_ppi = 72.0;
  if (info->icc.size > 0 && info->icc.data != nullptr) {
    document.color_state().embedded_icc_profile.assign(info->icc.data, info->icc.data + info->icc.size);
    result.pixels_in_source_space = true;
  } else if (hdr_plan.tone_map) {
    result.read.notices.emplace_back(
        PATCHY_TRANSLATE_NOOP("QObject", "This HDR image was tone mapped to 8-bit sRGB; Patchy edits 8 bits per channel."));
  }
  if (frames.size() > 1) {
    // AVIF_REPETITION_COUNT_INFINITE and UNKNOWN both play forever, the WebP convention for 0.
    const int repetitions = decoder->repetitionCount < 0 ? 0 : decoder->repetitionCount + 1;
    document.metadata().values[kLoopCountMetadata] = std::to_string(repetitions);
    const std::function<std::string(int, std::uint32_t)> name =
        options.frame_name ? options.frame_name : std::function<std::string(int, std::uint32_t)>(default_frame_name);
    int index = 0;
    for (auto& frame : frames) {
      document.add_pixel_layer(name(++index, frame.delay_ms), std::move(frame.pixels));
    }
    std::reverse(document.layers().begin(), document.layers().end());
    document.set_active_layer(document.layers().back().id());
  } else {
    document.add_pixel_layer("Background", std::move(frames.front().pixels));
  }
  result.frame_count = static_cast<std::uint32_t>(frames.size());
  result.read.document = std::move(document);
  return result;
}


namespace {

struct EncoderDeleter {
  void operator()(avifEncoder* encoder) const noexcept {
    avifEncoderDestroy(encoder);
  }
};
struct ImageDeleter {
  void operator()(avifImage* image) const noexcept {
    avifImageDestroy(image);
  }
};

[[nodiscard]] bool any_transparent(const PixelBuffer& rgba) {
  if (rgba.format().channels < 4) {
    return false;
  }
  for (std::int32_t y = 0; y < rgba.height(); ++y) {
    for (std::int32_t x = 0; x < rgba.width(); ++x) {
      if (pixel_alpha_at(rgba, x, y) < 1.0F) {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] std::runtime_error encode_failure(const char* noop_message, const avifEncoder* encoder,
                                                avifResult result) {
  std::string message = noop_message;
  std::string detail;
  if (encoder != nullptr && encoder->diag.error[0] != '\0') {
    detail = encoder->diag.error;
  } else if (result != AVIF_RESULT_OK) {
    detail = avifResultToString(result);
  }
  if (!detail.empty()) {
    message += ": ";
    message += detail;
  }
  return std::runtime_error(message);
}

}  // namespace

std::vector<std::uint8_t> write_avif(const Document& document, const WriteOptions& options) {
  const bool deep = document.color_state().bit_depth == BitDepth::UInt16;
  // 32-bit documents go through the 8-bit flatten (sRGB-encoded), like JPEG 2000.
  const PixelBuffer flat = deep ? Compositor{}.flatten_rgba_deep(document) : flatten_document_rgba8(document);
  const auto width = flat.width();
  const auto height = flat.height();
  if (width <= 0 || height <= 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Cannot write an empty image as AVIF"));
  }
  if (flat.format().channels != 4 || flat.format().bit_depth != (deep ? BitDepth::UInt16 : BitDepth::UInt8)) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The AVIF file could not be written"));
  }
  const bool alpha = any_transparent(flat);
  // 12-bit is the deepest AVIF sample; libavif rescales the 16-bit input on the way.
  std::unique_ptr<avifImage, ImageDeleter> image(avifImageCreate(
      static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), deep ? 12 : 8, AVIF_PIXEL_FORMAT_YUV444));
  if (!image) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The AVIF file could not be written"));
  }
  image->yuvRange = AVIF_RANGE_FULL;
  const auto& icc = document.color_state().embedded_icc_profile;
  // An RGB profile header carries "RGB " at offset 16; anything else is not written.
  const bool has_icc = icc.size() >= 20 && std::memcmp(icc.data() + 16, "RGB ", 4) == 0;
  if (has_icc) {
    if (avifImageSetProfileICC(image.get(), icc.data(), icc.size()) != AVIF_RESULT_OK) {
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The AVIF file could not be written"));
    }
    image->colorPrimaries = AVIF_COLOR_PRIMARIES_UNSPECIFIED;
    image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED;
  } else {
    image->colorPrimaries = AVIF_COLOR_PRIMARIES_SRGB;
    image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
  }
  // Lossless needs the identity matrix (no YUV rounding); lossy uses the AVIF default.
  image->matrixCoefficients = static_cast<avifMatrixCoefficients>(
      options.lossless ? AVIF_MATRIX_COEFFICIENTS_IDENTITY : AVIF_MATRIX_COEFFICIENTS_BT601);
  if (!options.exif.empty() &&
      avifImageSetMetadataExif(image.get(), options.exif.data(), options.exif.size()) != AVIF_RESULT_OK) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The AVIF file could not be written"));
  }
  if (!options.xmp.empty() &&
      avifImageSetMetadataXMP(image.get(), options.xmp.data(), options.xmp.size()) != AVIF_RESULT_OK) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The AVIF file could not be written"));
  }

  avifRGBImage rgb;
  avifRGBImageSetDefaults(&rgb, image.get());
  rgb.format = AVIF_RGB_FORMAT_RGBA;
  rgb.depth = deep ? 16 : 8;
  rgb.ignoreAlpha = alpha ? AVIF_FALSE : AVIF_TRUE;
  rgb.alphaPremultiplied = AVIF_FALSE;
  rgb.rowBytes = static_cast<std::uint32_t>(static_cast<std::size_t>(width) * bytes_per_pixel(flat.format()));
  rgb.pixels = const_cast<std::uint8_t*>(flat.data().data());  // libavif only reads the source
  rgb.maxThreads = std::clamp(max_blocking_fanout_workers(std::clamp(hardware_worker_threads(), 1, 16)), 1, 16);
  if (const auto result = avifImageRGBToYUV(image.get(), &rgb); result != AVIF_RESULT_OK) {
    throw encode_failure(PATCHY_TRANSLATE_NOOP("QObject", "The AVIF file could not be written"), nullptr, result);
  }

  std::unique_ptr<avifEncoder, EncoderDeleter> encoder(avifEncoderCreate());
  if (!encoder) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The AVIF encoder could not be created"));
  }
  encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
  encoder->maxThreads = rgb.maxThreads;
  encoder->speed = std::clamp(options.speed, AVIF_SPEED_SLOWEST, AVIF_SPEED_FASTEST);
  const int quality = std::clamp(options.quality, AVIF_QUALITY_WORST, AVIF_QUALITY_BEST);
  encoder->quality = options.lossless ? AVIF_QUALITY_LOSSLESS : quality;
  // Alpha edges show compression sooner than color does; keep the alpha plane at least at 90.
  encoder->qualityAlpha = options.lossless ? AVIF_QUALITY_LOSSLESS : std::max(quality, 90);
  encoder->autoTiling = AVIF_TRUE;
  avifRWData output = AVIF_DATA_EMPTY;
  const auto result = avifEncoderWrite(encoder.get(), image.get(), &output);
  std::vector<std::uint8_t> bytes;
  if (result == AVIF_RESULT_OK && output.data != nullptr) {
    bytes.assign(output.data, output.data + output.size);
  }
  avifRWDataFree(&output);
  if (result != AVIF_RESULT_OK) {
    throw encode_failure(PATCHY_TRANSLATE_NOOP("QObject", "The AVIF file could not be written"), encoder.get(), result);
  }
  return bytes;
}

#else  // PATCHY_AVIF_DECODER

bool is_available() noexcept {
  return false;
}

ReadResult read_avif_detailed(std::span<const std::uint8_t> /*bytes*/, const ReadOptions& /*options*/) {
  throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "AVIF images cannot be opened in the browser build of Patchy"));
}

std::vector<std::uint8_t> write_avif(const Document& /*document*/, const WriteOptions& /*options*/) {
  throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "AVIF images cannot be saved in the browser build of Patchy"));
}

#endif  // PATCHY_AVIF_DECODER

}  // namespace patchy::avif
