#include "formats/heif_document_io.hpp"

#include "formats/image_density_probe.hpp"

#include <libheif/heif.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#define CMS_NO_REGISTER_KEYWORD 1
#include "lcms2.h"

namespace patchy::heif {

namespace {

constexpr std::uint64_t kMaximumPixels = 268'435'456;  // 256 Mpx, matching the WIC reader.
constexpr std::size_t kMaximumIccProfileBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaximumExifMetadataBytes = 16U * 1024U * 1024U;
constexpr std::string_view kWebCodecsUnavailableError = "Browser HEVC decoder unavailable";

template <typename T, auto Release>
struct HeifDeleter {
  void operator()(T* value) const noexcept {
    Release(value);
  }
};

template <typename T, auto Release>
using HeifPtr = std::unique_ptr<T, HeifDeleter<T, Release>>;

using ContextPtr = HeifPtr<heif_context, heif_context_free>;
using HandlePtr = HeifPtr<heif_image_handle, heif_image_handle_release>;
using ImagePtr = HeifPtr<heif_image, heif_image_release>;
using DecodingOptionsPtr = HeifPtr<heif_decoding_options, heif_decoding_options_free>;

[[noreturn]] void throw_heif_error(std::string_view operation, const heif_error& error) {
  const std::string detail = error.message != nullptr ? error.message : "unknown error";
  if (detail.find(kWebCodecsUnavailableError) != std::string::npos) {
    throw std::runtime_error(std::string(kBrowserHevcUnavailableMarker));
  }
  throw std::runtime_error(std::string(operation) + ": " + detail);
}

void require_heif_ok(std::string_view operation, const heif_error& error) {
  if (error.code != heif_error_Ok) {
    throw_heif_error(operation, error);
  }
}

void ignore_lcms_error(cmsContext /*context*/, cmsUInt32Number /*code*/, const char* /*text*/) {}

class RgbIccToSrgbTransform {
public:
  static std::optional<RgbIccToSrgbTransform> create(
      std::span<const std::uint8_t> profile_bytes) {
    if (profile_bytes.empty() || profile_bytes.size() > kMaximumIccProfileBytes ||
        profile_bytes.size() > std::numeric_limits<cmsUInt32Number>::max()) {
      return std::nullopt;
    }

    RgbIccToSrgbTransform result;
    result.context_ = cmsCreateContext(nullptr, nullptr);
    if (result.context_ == nullptr) {
      return std::nullopt;
    }
    cmsSetLogErrorHandlerTHR(result.context_, ignore_lcms_error);

    cmsHPROFILE source = cmsOpenProfileFromMemTHR(
        result.context_, profile_bytes.data(),
        static_cast<cmsUInt32Number>(profile_bytes.size()));
    if (source == nullptr) {
      return std::nullopt;
    }
    cmsHPROFILE destination = nullptr;
    if (cmsGetColorSpace(source) == cmsSigRgbData) {
      destination = cmsCreate_sRGBProfileTHR(result.context_);
    }
    if (destination != nullptr) {
      result.transform_ =
          cmsCreateTransformTHR(result.context_, source, TYPE_RGBA_8, destination, TYPE_RGBA_8,
                                INTENT_RELATIVE_COLORIMETRIC,
                                cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_COPY_ALPHA |
                                    cmsFLAGS_NOCACHE);
      cmsCloseProfile(destination);
    }
    cmsCloseProfile(source);
    if (result.transform_ == nullptr) {
      return std::nullopt;
    }
    return result;
  }

  RgbIccToSrgbTransform() = default;
  RgbIccToSrgbTransform(const RgbIccToSrgbTransform&) = delete;
  RgbIccToSrgbTransform& operator=(const RgbIccToSrgbTransform&) = delete;

  RgbIccToSrgbTransform(RgbIccToSrgbTransform&& other) noexcept
      : context_(std::exchange(other.context_, nullptr)),
        transform_(std::exchange(other.transform_, nullptr)) {}

  RgbIccToSrgbTransform& operator=(RgbIccToSrgbTransform&& other) noexcept {
    if (this != &other) {
      reset();
      context_ = std::exchange(other.context_, nullptr);
      transform_ = std::exchange(other.transform_, nullptr);
    }
    return *this;
  }

  ~RgbIccToSrgbTransform() {
    reset();
  }

  void convert(const std::uint8_t* source, std::uint8_t* destination,
               std::size_t pixel_count) const {
    cmsDoTransform(transform_, source, destination,
                   static_cast<cmsUInt32Number>(pixel_count));
  }

private:
  void reset() noexcept {
    if (transform_ != nullptr) {
      cmsDeleteTransform(transform_);
      transform_ = nullptr;
    }
    if (context_ != nullptr) {
      cmsDeleteContext(context_);
      context_ = nullptr;
    }
  }

  cmsContext context_{nullptr};
  cmsHTRANSFORM transform_{nullptr};
};

[[nodiscard]] std::vector<std::uint8_t> read_icc_profile(
    const heif_image_handle& handle) {
  const auto profile_size = heif_image_handle_get_raw_color_profile_size(&handle);
  if (profile_size == 0 || profile_size > kMaximumIccProfileBytes) {
    return {};
  }
  std::vector<std::uint8_t> profile(profile_size);
  const auto error = heif_image_handle_get_raw_color_profile(&handle, profile.data());
  if (error.code != heif_error_Ok) {
    return {};
  }
  return profile;
}

[[nodiscard]] std::uint32_t read_u32_be(std::span<const std::uint8_t> bytes,
                                        std::size_t offset) {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
         static_cast<std::uint32_t>(bytes[offset + 3U]);
}

[[nodiscard]] std::optional<formats::ImageDensity> read_exif_density(
    const heif_image_handle& handle) {
  heif_item_id metadata_id = 0;
  if (heif_image_handle_get_list_of_metadata_block_IDs(&handle, "Exif", &metadata_id, 1) < 1) {
    return std::nullopt;
  }
  const auto metadata_size = heif_image_handle_get_metadata_size(&handle, metadata_id);
  if (metadata_size < 5U || metadata_size > kMaximumExifMetadataBytes) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> metadata(metadata_size);
  if (heif_image_handle_get_metadata(&handle, metadata_id, metadata.data()).code !=
      heif_error_Ok) {
    return std::nullopt;
  }

  const auto tiff_offset = static_cast<std::size_t>(read_u32_be(metadata, 0)) + 4U;
  if (tiff_offset >= metadata.size()) {
    return std::nullopt;
  }
  return formats::probe_exif_tiff_density(
      std::span<const std::uint8_t>(metadata).subspan(tiff_offset));
}

[[nodiscard]] bool has_meaningful_alpha(const std::uint8_t* plane, int stride,
                                        std::int32_t width, std::int32_t height) {
  for (std::int32_t y = 0; y < height; ++y) {
    const auto* row = plane + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (std::int32_t x = 0; x < width; ++x) {
      if (row[static_cast<std::size_t>(x) * 4U + 3U] != 0xFFU) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

FormatReadResult read_heif_impl(std::span<const std::uint8_t> bytes) {
  if (!sniff(bytes)) {
    throw std::runtime_error("This file is not a supported HEIC/HEIF image");
  }

  ContextPtr context(heif_context_alloc());
  if (!context) {
    throw std::runtime_error("Unable to initialize the HEIF parser");
  }
  if (auto* limits = heif_context_get_security_limits(context.get()); limits != nullptr) {
    if (limits->max_image_size_pixels == 0 ||
        limits->max_image_size_pixels > kMaximumPixels) {
      limits->max_image_size_pixels = kMaximumPixels;
    }
    if (limits->max_color_profile_size == 0 ||
        limits->max_color_profile_size > kMaximumIccProfileBytes) {
      limits->max_color_profile_size =
          static_cast<std::uint32_t>(kMaximumIccProfileBytes);
    }
  }
  require_heif_ok(
      "Unable to parse this HEIC/HEIF image",
      heif_context_read_from_memory_without_copy(context.get(), bytes.data(), bytes.size(), nullptr));

  heif_image_handle* raw_handle = nullptr;
  require_heif_ok("Unable to find the primary HEIF image",
                  heif_context_get_primary_image_handle(context.get(), &raw_handle));
  HandlePtr handle(raw_handle);

  const auto density = read_exif_density(*handle);
  auto icc_profile = read_icc_profile(*handle);
  std::optional<RgbIccToSrgbTransform> icc_transform;
  // The WebCodecs backend normally returns source YUV for 8-bit images, so retaining the
  // source NCLX while Little CMS applies an ICC profile gives ICC-only Display P3 files
  // the same sRGB result as native platforms. Its 10-bit fallback is already browser-
  // converted RGBA, so do not apply a second profile transform there.
  if (!icc_profile.empty() && heif_image_handle_get_luma_bits_per_pixel(handle.get()) == 8) {
    icc_transform = RgbIccToSrgbTransform::create(icc_profile);
  }

  DecodingOptionsPtr options(heif_decoding_options_alloc());
  if (!options) {
    throw std::runtime_error("Unable to initialize HEIF decoding options");
  }
  options->decoder_id = "webcodecs";
  options->convert_hdr_to_8bit = true;
  options->autocorrect_broken_input = true;
  if (icc_transform.has_value()) {
    options->output_image_nclx_profile_passthrough = true;
  }

  heif_image* raw_image = nullptr;
  require_heif_ok("Unable to decode this HEIC image",
                  heif_decode_image(handle.get(), &raw_image, heif_colorspace_RGB,
                                    heif_chroma_interleaved_RGBA, options.get()));
  ImagePtr image(raw_image);

  const int width_value = heif_image_get_width(image.get(), heif_channel_interleaved);
  const int height_value = heif_image_get_height(image.get(), heif_channel_interleaved);
  if (width_value <= 0 || height_value <= 0 ||
      static_cast<std::uint64_t>(width_value) * static_cast<std::uint64_t>(height_value) >
          kMaximumPixels) {
    throw std::runtime_error("This HEIF image's dimensions are not supported");
  }
  const auto width = static_cast<std::int32_t>(width_value);
  const auto height = static_cast<std::int32_t>(height_value);

  int stride = 0;
  const auto* plane =
      heif_image_get_plane_readonly(image.get(), heif_channel_interleaved, &stride);
  const auto tight_stride = static_cast<std::size_t>(width) * 4U;
  if (plane == nullptr || stride < 0 || static_cast<std::size_t>(stride) < tight_stride) {
    throw std::runtime_error("The browser returned an invalid HEIF pixel buffer");
  }

  const bool has_alpha = has_meaningful_alpha(plane, stride, width, height);
  const auto pixel_format = has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8();
  PixelBuffer pixels(width, height, pixel_format);
  std::vector<std::uint8_t> converted_row;
  if (icc_transform.has_value() && !has_alpha) {
    converted_row.resize(tight_stride);
  }

  for (std::int32_t y = 0; y < height; ++y) {
    const auto* source =
        plane + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    auto destination = pixels.row(y);
    const std::uint8_t* rgba_source = source;
    if (icc_transform.has_value()) {
      auto* rgba_destination =
          has_alpha ? destination.data() : converted_row.data();
      icc_transform->convert(source, rgba_destination, static_cast<std::size_t>(width));
      rgba_source = rgba_destination;
    }

    if (has_alpha) {
      if (!icc_transform.has_value()) {
        std::memcpy(destination.data(), rgba_source, tight_stride);
      }
    } else {
      for (std::int32_t x = 0; x < width; ++x) {
        std::memcpy(destination.data() + static_cast<std::size_t>(x) * 3U,
                    rgba_source + static_cast<std::size_t>(x) * 4U, 3U);
      }
    }
  }

  FormatReadResult result;
  result.document = Document(width, height, pixel_format);
  result.document.print_settings().horizontal_ppi =
      density.has_value() ? density->horizontal_ppi : 72.0;
  result.document.print_settings().vertical_ppi =
      density.has_value() ? density->vertical_ppi : 72.0;
  result.document.add_pixel_layer("Background", std::move(pixels));
  return result;
}

FormatReadResult read_heif(std::span<const std::uint8_t> bytes) {
  try {
    return read_heif_impl(bytes);
  } catch (const std::exception& error) {
    const std::string_view detail(error.what());
    if (detail.starts_with(kBrowserHevcUnavailableMarker)) {
      throw;
    }
    throw std::runtime_error(std::string(kBrowserHeifDecodeFailedMarker) +
                             std::string(detail));
  }
}

}  // namespace patchy::heif
