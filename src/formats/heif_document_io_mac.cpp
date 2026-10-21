// macOS HEIF reader for files deeper than 8 bits (compiled on macOS only, see CMakeLists).
// ImageIO is the system HEVC decoder, so Patchy still ships no codec of its own
// (docs/legal-constraints.md). Ordinary 8-bit HEIC keeps the historical route: read_heif
// throws and load_document_from_path retries through QImageReader (Qt's qmacheif, which
// decodes through ImageIO too but returns 8-bit pixels). This reader takes only what that
// route would narrow, and only while deep editing is on:
//   - PQ and HLG (BT.2100) images open as 32-bit linear sRGB documents, 1.0 at SDR
//     reference white. CoreGraphics draws them into an extended linear sRGB float context,
//     which places 203 nits (ITU-R BT.2408) on 1.0 and keeps out-of-gamut colors negative,
//     exactly Patchy's 32-bit model (measured against hdr::signal_rgba16_to_linear_srgb).
//   - Other images with more than 8 bits per component (10-bit Display P3 captures) open
//     as 16-bit documents baked to sRGB, as the 8-bit route bakes them.

#include "formats/heif_document_io.hpp"

#include "core/pixel_depth.hpp"
#include "support/translate_noop.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace patchy::heif {

namespace {

// Same ceiling as the Windows reader: 256 Mpx.
constexpr std::size_t kMaxPixels = 268'435'456;

template <typename T>
class CfRef {
public:
  explicit CfRef(T ref = nullptr) noexcept : ref_(ref) {}
  ~CfRef() {
    if (ref_ != nullptr) {
      CFRelease(ref_);
    }
  }
  CfRef(const CfRef&) = delete;
  CfRef& operator=(const CfRef&) = delete;
  [[nodiscard]] T get() const noexcept {
    return ref_;
  }

private:
  T ref_;
};

[[noreturn]] void defer_to_qt() {
  // The same message the 8-bit route reports when Qt cannot decode the file either.
  throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Unable to decode this HEIC image with the system codec."));
}

[[nodiscard]] double number_property(CFDictionaryRef properties, CFStringRef key, double fallback) {
  if (properties == nullptr) {
    return fallback;
  }
  const auto* value = static_cast<CFNumberRef>(CFDictionaryGetValue(properties, key));
  double result = fallback;
  if (value == nullptr || CFGetTypeID(value) != CFNumberGetTypeID() ||
      !CFNumberGetValue(value, kCFNumberDoubleType, &result)) {
    return fallback;
  }
  return result;
}

// Draws `image` into a tightly packed RGBA buffer of `Sample` (float or uint16) in
// `space`, then divides the premultiplied alpha out again.
template <typename Sample>
[[nodiscard]] std::vector<Sample> draw_unpremultiplied(CGImageRef image, CGColorSpaceRef space, std::size_t width,
                                                       std::size_t height) {
  constexpr bool is_float = std::is_same_v<Sample, float>;
  std::vector<Sample> rgba(width * height * 4U);
  // The enums are distinct types; combine them as plain integers.
  const auto alpha_info = static_cast<std::uint32_t>(kCGImageAlphaPremultipliedLast);
  const auto info = static_cast<CGBitmapInfo>(
      is_float ? alpha_info | static_cast<std::uint32_t>(kCGBitmapFloatComponents) |
                     static_cast<std::uint32_t>(kCGBitmapByteOrder32Host)
               : alpha_info | static_cast<std::uint32_t>(kCGBitmapByteOrder16Host));
  CfRef<CGContextRef> context(CGBitmapContextCreate(rgba.data(), width, height, sizeof(Sample) * 8U,
                                                    width * 4U * sizeof(Sample), space, info));
  if (context.get() == nullptr) {
    defer_to_qt();
  }
  CGContextSetBlendMode(context.get(), kCGBlendModeCopy);
  CGContextDrawImage(context.get(), CGRectMake(0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)),
                     image);
  for (std::size_t i = 0; i < width * height; ++i) {
    Sample* px = rgba.data() + i * 4U;
    if constexpr (is_float) {
      const float alpha = std::clamp(px[3], 0.0F, 1.0F);
      for (int c = 0; c < 3; ++c) {
        px[c] = alpha > 0.0F && std::isfinite(px[c]) ? px[c] / alpha : 0.0F;
      }
      px[3] = alpha;
    } else {
      const auto alpha = static_cast<std::uint32_t>(px[3]);
      if (alpha != 65535U) {
        for (int c = 0; c < 3; ++c) {
          px[c] = alpha == 0U ? 0
                              : static_cast<Sample>(std::min<std::uint32_t>(
                                    65535U, (static_cast<std::uint32_t>(px[c]) * 65535U + alpha / 2U) / alpha));
        }
      }
    }
  }
  return rgba;
}

}  // namespace

FormatReadResult read_heif(std::span<const std::uint8_t> bytes) {
  if (!deep_editing_enabled() || bytes.empty()) {
    defer_to_qt();
  }
  CfRef<CFDataRef> data(CFDataCreateWithBytesNoCopy(nullptr, bytes.data(), static_cast<CFIndex>(bytes.size()),
                                                    kCFAllocatorNull));
  CfRef<CGImageSourceRef> source(data.get() != nullptr ? CGImageSourceCreateWithData(data.get(), nullptr) : nullptr);
  if (source.get() == nullptr || CGImageSourceGetCount(source.get()) == 0) {
    defer_to_qt();
  }
  const std::size_t index = CGImageSourceGetPrimaryImageIndex(source.get());
  CfRef<CFDictionaryRef> properties(CGImageSourceCopyPropertiesAtIndex(source.get(), index, nullptr));
  CfRef<CGImageRef> image(CGImageSourceCreateImageAtIndex(source.get(), index, nullptr));
  if (image.get() == nullptr) {
    defer_to_qt();
  }
  const auto width = CGImageGetWidth(image.get());
  const auto height = CGImageGetHeight(image.get());
  if (width == 0 || height == 0 || width > 65535 || height > 65535 || width * height > kMaxPixels) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This HEIF image's dimensions are not supported"));
  }
  CGColorSpaceRef image_space = CGImageGetColorSpace(image.get());
  const bool hdr = image_space != nullptr && CGColorSpaceUsesITUR_2100TF(image_space);
  const auto bits = std::max<double>(static_cast<double>(CGImageGetBitsPerComponent(image.get())),
                                     number_property(properties.get(), kCGImagePropertyDepth, 8.0));
  if (!hdr && bits <= 8.0) {
    defer_to_qt();
  }

  const auto depth = hdr ? BitDepth::Float32 : BitDepth::UInt16;
  std::vector<std::uint8_t> raw;
  bool has_alpha = false;
  if (hdr) {
    CfRef<CGColorSpaceRef> linear(CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB));
    const auto rgba = draw_unpremultiplied<float>(image.get(), linear.get(), width, height);
    for (std::size_t i = 3; i < rgba.size(); i += 4) {
      if (rgba[i] < 1.0F) {
        has_alpha = true;
        break;
      }
    }
    raw.resize(rgba.size() * sizeof(float));
    std::memcpy(raw.data(), rgba.data(), raw.size());
  } else {
    CfRef<CGColorSpaceRef> srgb(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    const auto rgba = draw_unpremultiplied<std::uint16_t>(image.get(), srgb.get(), width, height);
    for (std::size_t i = 3; i < rgba.size(); i += 4) {
      if (rgba[i] != 65535U) {
        has_alpha = true;
        break;
      }
    }
    raw.resize(rgba.size() * sizeof(std::uint16_t));
    std::memcpy(raw.data(), rgba.data(), raw.size());
  }

  // ImageIO hands over stored pixels; the container's irot/imir arrive as an EXIF-style
  // orientation, as from WIC.
  const auto sample_bytes = depth == BitDepth::Float32 ? sizeof(float) : sizeof(std::uint16_t);
  const int orientation = static_cast<int>(number_property(properties.get(), kCGImagePropertyOrientation, 1.0));
  auto oriented = apply_exif_orientation(raw, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height),
                                         orientation, sample_bytes * 4U);
  raw.clear();
  raw.shrink_to_fit();

  const auto format = with_bit_depth(has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8(), depth);
  PixelBuffer pixels(oriented.width, oriented.height, format);
  const std::size_t out_pixel = (has_alpha ? 4U : 3U) * sample_bytes;
  const std::size_t in_pixel = 4U * sample_bytes;
  for (std::int32_t y = 0; y < oriented.height; ++y) {
    const auto* row_in = oriented.rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(oriented.width) * in_pixel;
    auto row = pixels.row(y);
    if (has_alpha) {
      std::memcpy(row.data(), row_in, row.size());
    } else {
      for (std::int32_t x = 0; x < oriented.width; ++x) {
        std::memcpy(row.data() + static_cast<std::size_t>(x) * out_pixel, row_in + static_cast<std::size_t>(x) * in_pixel,
                    out_pixel);
      }
    }
  }

  FormatReadResult result;
  result.document = Document(oriented.width, oriented.height, format);
  result.document.color_state().bit_depth = depth;
  // ImageIO reports 72 for files that record no density.
  const double dpi_x = number_property(properties.get(), kCGImagePropertyDPIWidth, 72.0);
  const double dpi_y = number_property(properties.get(), kCGImagePropertyDPIHeight, 72.0);
  result.document.print_settings().horizontal_ppi = dpi_x > 1.0 ? dpi_x : 72.0;
  result.document.print_settings().vertical_ppi = dpi_y > 1.0 ? dpi_y : 72.0;
  result.document.add_pixel_layer("Background", std::move(pixels));
  return result;
}

}  // namespace patchy::heif
