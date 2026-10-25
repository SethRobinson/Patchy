// Linux HEIF/HEIC decode through the distribution's own libheif, loaded with dlopen at run
// time (compiled for native Linux builds and the AppImage, see CMakeLists). Patchy links
// nothing: the library and its HEVC decoder plug-in (libde265 on Debian/Ubuntu, the RPM
// Fusion build on Fedora) come from the user's package manager, so the binary ships no
// HEVC decoder and the AppImage must never bundle libheif (docs/legal-constraints.md,
// docs/appimage.md). Inside the Flatpak this file still compiles but steps aside:
// the KDE runtime's kimg_heif plug-in plus the codecs-extra extension decode through the
// QImageReader fallback instead (heif_document_io.hpp).
//
// The C ABI mirrored below is the subset libheif has kept unchanged since 1.12 (Ubuntu
// 22.04): enum values, the error struct and the version-tagged decoding options are
// append-only in libheif. Nothing from the vendored 1.23 headers is included because the
// host library may be older than them.
//
// With deep editing on, files deeper than 8 bits decode at their own depth instead of
// through convert_hdr_to_8bit: PQ and HLG (nclx, no ICC profile) become 32-bit linear sRGB
// through formats/hdr_transfer, other high-bit-depth files 16-bit sRGB (docs/heif.md).

#include "formats/heif_document_io.hpp"

#include "core/pixel_depth.hpp"
#include "formats/hdr_transfer.hpp"
#include "formats/image_density_probe.hpp"
#include "support/byte_order.hpp"
#include "support/translate_noop.hpp"

#define CMS_NO_REGISTER_KEYWORD 1
#include "lcms2.h"

#include <dlfcn.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace patchy::heif {

namespace {

constexpr std::uint64_t kMaximumPixels = 268'435'456;  // 256 Mpx, matching the WIC reader.
constexpr std::size_t kMaximumIccProfileBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaximumExifMetadataBytes = 16U * 1024U * 1024U;

// Environment override for the library name or path. Tests point it at a missing file to
// pin the "not installed" message; a user can point it at a libheif outside the loader's
// search path.
constexpr const char* kLibraryOverrideVariable = "PATCHY_HEIF_LIBRARY";

// ---- libheif C ABI (stable subset, see the file comment) ----

struct heif_context;
struct heif_image_handle;
struct heif_image;
using heif_item_id = std::uint32_t;

struct HeifError {
  int code;
  int subcode;
  const char* message;
};

// Prefix of libheif's heif_decoding_options. Versions only append fields, so a struct
// allocated by any newer library can be read and written through this view.
struct HeifDecodingOptions {
  std::uint8_t version;
  std::uint8_t ignore_transformations;  // version 1; 0 applies irot/imir/clap (the default)
  void* start_progress;
  void* on_progress;
  void* end_progress;
  void* progress_user_data;
  std::uint8_t convert_hdr_to_8bit;  // version 2
};

constexpr int kErrorOk = 0;
constexpr int kErrorUnsupportedFeature = 4;
constexpr int kErrorDecoderPluginError = 7;
constexpr int kErrorPluginLoadingError = 11;
constexpr int kSuberrorUnsupportedCodec = 3000;
constexpr int kSuberrorNoMatchingDecoderInstalled = 6003;
constexpr int kCompressionHevc = 1;
constexpr int kColorspaceRgb = 1;
constexpr int kChromaInterleavedRgba = 11;
constexpr int kChromaInterleavedRrggbbaaLe = 15;
constexpr int kChannelInterleaved = 10;
constexpr int kPrimariesBt709 = 1;
constexpr int kPrimariesBt2020 = 9;
constexpr int kPrimariesDisplayP3 = 12;  // SMPTE EG 432-1, P3 with the D65 white
constexpr int kTransferPq = 16;
constexpr int kTransferHlg = 18;

// libheif's heif_color_profile_nclx (version 1 layout; C enums are int-sized).
struct HeifNclx {
  std::uint8_t version;
  int color_primaries;
  int transfer_characteristics;
  int matrix_coefficients;
  std::uint8_t full_range_flag;
  float red_x;
  float red_y;
  float green_x;
  float green_y;
  float blue_x;
  float blue_y;
  float white_x;
  float white_y;
};

struct Library {
  void* handle{nullptr};
  // User-facing reason the library is unusable; empty when every symbol resolved.
  std::string error;
  std::string resolved_name;

  heif_context* (*context_alloc)(){nullptr};
  void (*context_free)(heif_context*){nullptr};
  HeifError (*context_read_from_memory_without_copy)(heif_context*, const void*, std::size_t, const void*){nullptr};
  HeifError (*context_get_primary_image_handle)(heif_context*, heif_image_handle**){nullptr};
  int (*context_get_number_of_top_level_images)(heif_context*){nullptr};
  int (*image_handle_get_width)(const heif_image_handle*){nullptr};
  int (*image_handle_get_height)(const heif_image_handle*){nullptr};
  int (*image_handle_has_alpha_channel)(const heif_image_handle*){nullptr};
  std::size_t (*image_handle_get_raw_color_profile_size)(const heif_image_handle*){nullptr};
  HeifError (*image_handle_get_raw_color_profile)(const heif_image_handle*, void*){nullptr};
  int (*image_handle_get_list_of_metadata_block_IDs)(const heif_image_handle*, const char*, heif_item_id*, int){nullptr};
  std::size_t (*image_handle_get_metadata_size)(const heif_image_handle*, heif_item_id){nullptr};
  HeifError (*image_handle_get_metadata)(const heif_image_handle*, heif_item_id, void*){nullptr};
  void (*image_handle_release)(const heif_image_handle*){nullptr};
  HeifDecodingOptions* (*decoding_options_alloc)(){nullptr};
  void (*decoding_options_free)(HeifDecodingOptions*){nullptr};
  HeifError (*decode_image)(const heif_image_handle*, heif_image**, int, int, const HeifDecodingOptions*){nullptr};
  int (*image_get_width)(const heif_image*, int){nullptr};
  int (*image_get_height)(const heif_image*, int){nullptr};
  int (*image_get_bits_per_pixel)(const heif_image*, int){nullptr};
  const std::uint8_t* (*image_get_plane_readonly)(const heif_image*, int, int*){nullptr};
  void (*image_release)(const heif_image*){nullptr};
  // Optional (newer libheif): null when the host library predates them.
  int (*image_handle_is_premultiplied_alpha)(const heif_image_handle*){nullptr};
  int (*have_decoder_for_format)(int){nullptr};
  // Optional, for decoding at full depth; without them every file comes down to 8 bits.
  int (*image_handle_get_luma_bits_per_pixel)(const heif_image_handle*){nullptr};
  int (*image_get_bits_per_pixel_range)(const heif_image*, int){nullptr};
  HeifError (*image_handle_get_nclx_color_profile)(const heif_image_handle*, HeifNclx**){nullptr};
  void (*nclx_color_profile_free)(HeifNclx*){nullptr};
};

// Both package messages end with the exact install command per distribution, the way the
// Flatpak message carries its "flatpak install" line: the open-failed box shows them as
// selectable text, so the user can copy the line straight into a terminal.
constexpr std::string_view kLibraryMissingMessage = PATCHY_TRANSLATE_NOOP(
    "QObject",
    "This build of Patchy cannot decode HEIC images because the system's libheif library is not installed. "
    "Install it and try again.\nDebian/Ubuntu: sudo apt install libheif1\nFedora: sudo dnf install libheif");

constexpr std::string_view kLibraryTooOldMessage = PATCHY_TRANSLATE_NOOP(
    "QObject",
    "This build of Patchy cannot decode HEIC images because the installed libheif library is too old. "
    "Patchy needs libheif 1.12 or newer.");

constexpr std::string_view kHevcPluginMissingMessage = PATCHY_TRANSLATE_NOOP(
    "QObject",
    "The system's libheif library has no HEVC decoder plug-in, so Patchy cannot decode HEIC images. "
    "Install it and try again.\nDebian/Ubuntu: sudo apt install libheif-plugin-libde265\n"
    "Fedora (RPM Fusion): sudo dnf install libheif-freeworld");

constexpr std::string_view kFlatpakCodecMessage = PATCHY_TRANSLATE_NOOP(
    "QObject",
    "Unable to decode this HEIC image. HEIC decoding needs the Flatpak codec extension; "
    "install it with: flatpak install --user flathub org.freedesktop.Platform.codecs-extra//25.08-extra");

template <typename Function>
bool resolve(void* handle, const char* name, Function& target) {
  target = reinterpret_cast<Function>(dlsym(handle, name));
  return target != nullptr;
}

Library load_library(const std::string& override_name) {
  Library library;
  std::vector<std::string> candidates;
  if (!override_name.empty()) {
    candidates.push_back(override_name);
  } else {
    candidates = {"libheif.so.1", "libheif.so"};
  }
  for (const auto& candidate : candidates) {
    // RTLD_LOCAL: libheif's own dependencies (libde265, libx265, libaom, ...) must not
    // leak symbols into Patchy's global namespace next to its vendored codecs.
    if (void* handle = dlopen(candidate.c_str(), RTLD_NOW | RTLD_LOCAL); handle != nullptr) {
      library.handle = handle;
      library.resolved_name = candidate;
      break;
    }
  }
  if (library.handle == nullptr) {
    library.error = std::string(kLibraryMissingMessage);
    return library;
  }

  void* handle = library.handle;
  const bool complete =
      resolve(handle, "heif_context_alloc", library.context_alloc) &&
      resolve(handle, "heif_context_free", library.context_free) &&
      resolve(handle, "heif_context_read_from_memory_without_copy", library.context_read_from_memory_without_copy) &&
      resolve(handle, "heif_context_get_primary_image_handle", library.context_get_primary_image_handle) &&
      resolve(handle, "heif_context_get_number_of_top_level_images", library.context_get_number_of_top_level_images) &&
      resolve(handle, "heif_image_handle_get_width", library.image_handle_get_width) &&
      resolve(handle, "heif_image_handle_get_height", library.image_handle_get_height) &&
      resolve(handle, "heif_image_handle_has_alpha_channel", library.image_handle_has_alpha_channel) &&
      resolve(handle, "heif_image_handle_get_raw_color_profile_size", library.image_handle_get_raw_color_profile_size) &&
      resolve(handle, "heif_image_handle_get_raw_color_profile", library.image_handle_get_raw_color_profile) &&
      resolve(handle, "heif_image_handle_get_list_of_metadata_block_IDs", library.image_handle_get_list_of_metadata_block_IDs) &&
      resolve(handle, "heif_image_handle_get_metadata_size", library.image_handle_get_metadata_size) &&
      resolve(handle, "heif_image_handle_get_metadata", library.image_handle_get_metadata) &&
      resolve(handle, "heif_image_handle_release", library.image_handle_release) &&
      resolve(handle, "heif_decoding_options_alloc", library.decoding_options_alloc) &&
      resolve(handle, "heif_decoding_options_free", library.decoding_options_free) &&
      resolve(handle, "heif_decode_image", library.decode_image) &&
      resolve(handle, "heif_image_get_width", library.image_get_width) &&
      resolve(handle, "heif_image_get_height", library.image_get_height) &&
      resolve(handle, "heif_image_get_bits_per_pixel", library.image_get_bits_per_pixel) &&
      resolve(handle, "heif_image_get_plane_readonly", library.image_get_plane_readonly) &&
      resolve(handle, "heif_image_release", library.image_release);
  if (!complete) {
    library.error = std::string(kLibraryTooOldMessage);
    return library;
  }
  resolve(handle, "heif_image_handle_is_premultiplied_alpha", library.image_handle_is_premultiplied_alpha);
  resolve(handle, "heif_have_decoder_for_format", library.have_decoder_for_format);
  resolve(handle, "heif_image_handle_get_luma_bits_per_pixel", library.image_handle_get_luma_bits_per_pixel);
  resolve(handle, "heif_image_get_bits_per_pixel_range", library.image_get_bits_per_pixel_range);
  resolve(handle, "heif_image_handle_get_nclx_color_profile", library.image_handle_get_nclx_color_profile);
  resolve(handle, "heif_nclx_color_profile_free", library.nclx_color_profile_free);
  return library;
}

// Loaded once per library name and never closed (a decode may still be running on
// another thread when the next lookup happens, and the handle is tiny).
const Library& library() {
  static std::mutex mutex;
  static std::map<std::string, std::unique_ptr<Library>> libraries;
  const char* override_value = std::getenv(kLibraryOverrideVariable);
  const std::string name = override_value != nullptr ? override_value : "";
  const std::lock_guard<std::mutex> lock(mutex);
  auto found = libraries.find(name);
  if (found == libraries.end()) {
    found = libraries.emplace(name, std::make_unique<Library>(load_library(name))).first;
  }
  return *found->second;
}

[[nodiscard]] bool running_in_flatpak() {
  return std::getenv("FLATPAK_ID") != nullptr;
}

[[nodiscard]] bool codec_missing(const HeifError& error) {
  if (error.code == kErrorPluginLoadingError) {
    return true;
  }
  if (error.subcode == kSuberrorUnsupportedCodec || error.subcode == kSuberrorNoMatchingDecoderInstalled) {
    return true;
  }
  // Older libheif builds report a missing decoder with a generic code and a message.
  if (error.code == kErrorUnsupportedFeature || error.code == kErrorDecoderPluginError) {
    const std::string_view message = error.message != nullptr ? error.message : "";
    return message.find("ecoder") != std::string_view::npos || message.find("codec") != std::string_view::npos;
  }
  return false;
}

[[noreturn]] void throw_heif_error(std::string_view operation, const HeifError& error) {
  if (codec_missing(error)) {
    throw std::runtime_error(std::string(kHevcPluginMissingMessage));
  }
  const std::string detail = error.message != nullptr ? error.message : "unknown error";
  throw std::runtime_error(std::string(operation) + ": " + detail);
}

void require_ok(std::string_view operation, const HeifError& error) {
  if (error.code != kErrorOk) {
    throw_heif_error(operation, error);
  }
}

struct ContextDeleter {
  const Library* lib;
  void operator()(heif_context* value) const noexcept { lib->context_free(value); }
};
struct HandleDeleter {
  const Library* lib;
  void operator()(heif_image_handle* value) const noexcept { lib->image_handle_release(value); }
};
struct ImageDeleter {
  const Library* lib;
  void operator()(heif_image* value) const noexcept { lib->image_release(value); }
};
struct OptionsDeleter {
  const Library* lib;
  void operator()(HeifDecodingOptions* value) const noexcept { lib->decoding_options_free(value); }
};

void ignore_lcms_error(cmsContext /*context*/, cmsUInt32Number /*code*/, const char* /*text*/) {}

// Same transform as the browser reader's (heif_document_io_wasm.cpp): an RGB ICC profile
// (iPhone = Display P3) bakes to sRGB, which is what the Windows WIC path and Qt's
// fallback produce too, so a HEIC opens with identical colors on every platform.
class RgbIccToSrgbTransform {
public:
  // `sixteen_bit` converts RGBA16 rows (deep decodes) instead of RGBA8.
  static std::optional<RgbIccToSrgbTransform> create(std::span<const std::uint8_t> profile_bytes,
                                                     bool sixteen_bit = false) {
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
    cmsHPROFILE source = cmsOpenProfileFromMemTHR(result.context_, profile_bytes.data(),
                                                  static_cast<cmsUInt32Number>(profile_bytes.size()));
    if (source == nullptr) {
      return std::nullopt;
    }
    cmsHPROFILE destination = nullptr;
    if (cmsGetColorSpace(source) == cmsSigRgbData) {
      destination = cmsCreate_sRGBProfileTHR(result.context_);
    }
    if (destination != nullptr) {
      const cmsUInt32Number type = sixteen_bit ? TYPE_RGBA_16 : TYPE_RGBA_8;
      result.transform_ = cmsCreateTransformTHR(result.context_, source, type, destination, type,
                                                INTENT_RELATIVE_COLORIMETRIC,
                                                cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOCACHE);
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
      : context_(std::exchange(other.context_, nullptr)), transform_(std::exchange(other.transform_, nullptr)) {}
  RgbIccToSrgbTransform& operator=(RgbIccToSrgbTransform&& other) noexcept {
    if (this != &other) {
      reset();
      context_ = std::exchange(other.context_, nullptr);
      transform_ = std::exchange(other.transform_, nullptr);
    }
    return *this;
  }
  ~RgbIccToSrgbTransform() { reset(); }

  void convert(const void* source, void* destination, std::size_t pixel_count) const {
    cmsDoTransform(transform_, source, destination, static_cast<cmsUInt32Number>(pixel_count));
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

[[nodiscard]] std::vector<std::uint8_t> read_icc_profile(const Library& lib, const heif_image_handle& handle) {
  const auto profile_size = lib.image_handle_get_raw_color_profile_size(&handle);
  if (profile_size == 0 || profile_size > kMaximumIccProfileBytes) {
    return {};
  }
  std::vector<std::uint8_t> profile(profile_size);
  if (lib.image_handle_get_raw_color_profile(&handle, profile.data()).code != kErrorOk) {
    return {};
  }
  return profile;
}

[[nodiscard]] std::optional<formats::ImageDensity> read_exif_density(const Library& lib, const heif_image_handle& handle) {
  heif_item_id metadata_id = 0;
  if (lib.image_handle_get_list_of_metadata_block_IDs(&handle, "Exif", &metadata_id, 1) < 1) {
    return std::nullopt;
  }
  const auto metadata_size = lib.image_handle_get_metadata_size(&handle, metadata_id);
  if (metadata_size < 5U || metadata_size > kMaximumExifMetadataBytes) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> metadata(metadata_size);
  if (lib.image_handle_get_metadata(&handle, metadata_id, metadata.data()).code != kErrorOk) {
    return std::nullopt;
  }
  // HEIF Exif items start with a u32 offset to the TIFF header (ISO 23008-12 A.2.1).
  const auto tiff_offset = static_cast<std::size_t>(read_u32_be(metadata, 0)) + 4U;
  if (tiff_offset >= metadata.size()) {
    return std::nullopt;
  }
  return formats::probe_exif_tiff_density(std::span<const std::uint8_t>(metadata).subspan(tiff_offset));
}

[[nodiscard]] bool has_meaningful_alpha(const std::uint8_t* plane, int stride, std::int32_t width, std::int32_t height) {
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

void unpremultiply_row(std::uint8_t* rgba, std::int32_t width) {
  for (std::int32_t x = 0; x < width; ++x, rgba += 4) {
    const unsigned alpha = rgba[3];
    if (alpha == 0 || alpha == 255) {
      continue;
    }
    for (int channel = 0; channel < 3; ++channel) {
      const unsigned value = (static_cast<unsigned>(rgba[channel]) * 255U + alpha / 2U) / alpha;
      rgba[channel] = static_cast<std::uint8_t>(value > 255U ? 255U : value);
    }
  }
}

// How a high-bit-depth file decodes with deep editing on; unset `depth` keeps 8 bits.
struct DeepPlan {
  std::optional<BitDepth> depth;
  std::optional<hdr::Transfer> transfer;
  hdr::Chromaticities primaries{hdr::kBt2020Primaries};
};

[[nodiscard]] DeepPlan deep_plan_for(const Library& lib, const heif_image_handle& handle, bool has_icc_profile) {
  DeepPlan plan;
  if (!deep_editing_enabled() || lib.image_handle_get_luma_bits_per_pixel == nullptr ||
      lib.image_get_bits_per_pixel_range == nullptr) {
    return plan;
  }
  if (!has_icc_profile && lib.image_handle_get_nclx_color_profile != nullptr && lib.nclx_color_profile_free != nullptr) {
    HeifNclx* nclx = nullptr;
    if (lib.image_handle_get_nclx_color_profile(&handle, &nclx).code == kErrorOk && nclx != nullptr) {
      if (nclx->transfer_characteristics == kTransferPq) {
        plan.transfer = hdr::Transfer::Pq;
      } else if (nclx->transfer_characteristics == kTransferHlg) {
        plan.transfer = hdr::Transfer::Hlg;
      }
      if (nclx->color_primaries == kPrimariesBt709) {
        plan.primaries = hdr::kBt709Primaries;
      } else if (nclx->color_primaries == kPrimariesDisplayP3) {
        plan.primaries = hdr::kDisplayP3Primaries;
      } else if (nclx->color_primaries != kPrimariesBt2020 && nclx->red_x > 0.0F && nclx->white_y > 0.0F) {
        plan.primaries = {nclx->red_x, nclx->red_y, nclx->green_x, nclx->green_y,
                          nclx->blue_x, nclx->blue_y, nclx->white_x, nclx->white_y};
      }
      lib.nclx_color_profile_free(nclx);
    }
  }
  if (plan.transfer) {
    plan.depth = BitDepth::Float32;
  } else if (lib.image_handle_get_luma_bits_per_pixel(&handle) > 8) {
    plan.depth = BitDepth::UInt16;
  }
  return plan;
}

// The interleaved little-endian 16-bit plane of a deep decode, rescaled from the file's
// bit depth to full-range 16 bits, premultiplied alpha divided out.
[[nodiscard]] std::vector<std::uint16_t> full_range_rgba16(const std::uint8_t* plane, int stride, std::int32_t width,
                                                           std::int32_t height, int bits, bool premultiplied) {
  const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  std::vector<std::uint16_t> rgba(pixel_count * 4U);
  const std::uint32_t maximum = bits >= 16 ? 65535U : (1U << static_cast<unsigned>(bits)) - 1U;
  for (std::int32_t y = 0; y < height; ++y) {
    const auto* row = plane + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    auto* out = rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4U;
    for (std::size_t i = 0; i < static_cast<std::size_t>(width) * 4U; ++i) {
      const std::uint32_t value =
          static_cast<std::uint32_t>(row[i * 2U]) | (static_cast<std::uint32_t>(row[i * 2U + 1U]) << 8U);
      out[i] = static_cast<std::uint16_t>((std::min(value, maximum) * 65535U + maximum / 2U) / maximum);
    }
    if (premultiplied) {
      for (std::int32_t x = 0; x < width; ++x) {
        auto* px = out + static_cast<std::size_t>(x) * 4U;
        const std::uint32_t alpha = px[3];
        if (alpha != 0U && alpha != 65535U) {
          for (int c = 0; c < 3; ++c) {
            px[c] = static_cast<std::uint16_t>(
                std::min<std::uint32_t>(65535U, (static_cast<std::uint32_t>(px[c]) * 65535U + alpha / 2U) / alpha));
          }
        }
      }
    }
  }
  return rgba;
}

// A deep decode as a document: PQ/HLG to 32-bit linear sRGB, anything else to 16-bit sRGB
// (an RGB ICC profile baked by Little CMS at 16 bits, as the 8-bit path bakes it).
FormatReadResult read_deep_pixels(const Library& lib, const heif_image& image, const DeepPlan& plan,
                                  const std::vector<std::uint8_t>& icc_profile, bool premultiplied,
                                  const std::optional<formats::ImageDensity>& density, int image_count) {
  const int width_value = lib.image_get_width(&image, kChannelInterleaved);
  const int height_value = lib.image_get_height(&image, kChannelInterleaved);
  if (width_value <= 0 || height_value <= 0 ||
      static_cast<std::uint64_t>(width_value) * static_cast<std::uint64_t>(height_value) > kMaximumPixels) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This HEIF image's dimensions are not supported"));
  }
  const auto width = static_cast<std::int32_t>(width_value);
  const auto height = static_cast<std::int32_t>(height_value);
  const int bits = lib.image_get_bits_per_pixel_range(&image, kChannelInterleaved);
  int stride = 0;
  const auto* plane = lib.image_get_plane_readonly(&image, kChannelInterleaved, &stride);
  if (plane == nullptr || bits <= 8 || bits > 16 || stride < 0 ||
      static_cast<std::size_t>(stride) < static_cast<std::size_t>(width) * 8U) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "libheif returned an invalid HEIF pixel buffer"));
  }
  auto rgba16 = full_range_rgba16(plane, stride, width, height, bits, premultiplied);
  const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  bool has_alpha = false;
  for (std::size_t i = 3; i < rgba16.size(); i += 4) {
    if (rgba16[i] != 65535U) {
      has_alpha = true;
      break;
    }
  }
  const auto depth = *plan.depth;
  const auto format = with_bit_depth(has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8(), depth);
  PixelBuffer pixels(width, height, format);
  const std::size_t channels = has_alpha ? 4U : 3U;
  if (depth == BitDepth::Float32) {
    std::vector<float> linear(pixel_count * 4U);
    hdr::signal_rgba16_to_linear_srgb(rgba16.data(), pixel_count, *plan.transfer, plan.primaries, linear.data());
    auto* out = reinterpret_cast<float*>(pixels.data().data());
    for (std::size_t i = 0; i < pixel_count; ++i) {
      std::copy_n(linear.data() + i * 4U, channels, out + i * channels);
    }
  } else {
    if (!icc_profile.empty()) {
      if (const auto transform = RgbIccToSrgbTransform::create(icc_profile, true)) {
        transform->convert(rgba16.data(), rgba16.data(), pixel_count);
      }
    }
    auto* out = reinterpret_cast<std::uint16_t*>(pixels.data().data());
    for (std::size_t i = 0; i < pixel_count; ++i) {
      std::copy_n(rgba16.data() + i * 4U, channels, out + i * channels);
    }
  }
  FormatReadResult result;
  result.document = Document(width, height, format);
  result.document.color_state().bit_depth = depth;
  result.document.print_settings().horizontal_ppi = density.has_value() ? density->horizontal_ppi : 72.0;
  result.document.print_settings().vertical_ppi = density.has_value() ? density->vertical_ppi : 72.0;
  result.document.add_pixel_layer("Background", std::move(pixels));
  if (image_count > 1) {
    result.notices.push_back("Opened the primary image only (" + std::to_string(image_count) + " images in the file)");
  }
  return result;
}

}  // namespace

bool host_libheif_available() {
  return !running_in_flatpak() && library().error.empty();
}

std::string host_libheif_load_error() {
  return library().error;
}

FormatReadResult read_heif(std::span<const std::uint8_t> bytes) {
  if (running_in_flatpak()) {
    // The registry error sends load_document_from_path to QImageReader, where the KDE
    // runtime's kimg_heif decodes. This text shows only when that fails too.
    throw std::runtime_error(std::string(kFlatpakCodecMessage));
  }
  const Library& lib = library();
  if (!lib.error.empty()) {
    throw std::runtime_error(lib.error);
  }
  if (!sniff(bytes)) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This file is not a supported HEIC/HEIF image"));
  }
  if (lib.have_decoder_for_format != nullptr && lib.have_decoder_for_format(kCompressionHevc) == 0) {
    // libheif 1.15+ with its HEVC decoder in a separate package that is not installed.
    throw std::runtime_error(std::string(kHevcPluginMissingMessage));
  }

  std::unique_ptr<heif_context, ContextDeleter> context(lib.context_alloc(), ContextDeleter{&lib});
  if (!context) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Unable to initialize the HEIF parser"));
  }
  require_ok("Unable to parse this HEIC/HEIF image",
             lib.context_read_from_memory_without_copy(context.get(), bytes.data(), bytes.size(), nullptr));

  heif_image_handle* raw_handle = nullptr;
  require_ok("Unable to find the primary HEIF image", lib.context_get_primary_image_handle(context.get(), &raw_handle));
  std::unique_ptr<heif_image_handle, HandleDeleter> handle(raw_handle, HandleDeleter{&lib});

  // Bound the decode before asking for pixels: the handle reports the displayed size
  // (after irot), which is what the output buffer will hold.
  const int handle_width = lib.image_handle_get_width(handle.get());
  const int handle_height = lib.image_handle_get_height(handle.get());
  if (handle_width <= 0 || handle_height <= 0 ||
      static_cast<std::uint64_t>(handle_width) * static_cast<std::uint64_t>(handle_height) > kMaximumPixels) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This HEIF image's dimensions are not supported"));
  }

  const auto density = read_exif_density(lib, *handle);
  const auto icc_profile = read_icc_profile(lib, *handle);
  std::optional<RgbIccToSrgbTransform> icc_transform;
  if (!icc_profile.empty()) {
    icc_transform = RgbIccToSrgbTransform::create(icc_profile);
  }
  const bool premultiplied =
      lib.image_handle_is_premultiplied_alpha != nullptr && lib.image_handle_has_alpha_channel(handle.get()) != 0 &&
      lib.image_handle_is_premultiplied_alpha(handle.get()) != 0;

  std::unique_ptr<HeifDecodingOptions, OptionsDeleter> options(lib.decoding_options_alloc(), OptionsDeleter{&lib});
  if (!options) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Unable to initialize HEIF decoding options"));
  }
  // Defaults apply the container's rotation, mirroring and clean-aperture crop. Without
  // deep editing, 10-bit files (iPhone HDR captures) come down to 8 bits inside libheif.
  const auto plan = deep_plan_for(lib, *handle, !icc_profile.empty());
  if (options->version >= 2) {
    options->convert_hdr_to_8bit = plan.depth ? 0 : 1;
  }

  heif_image* raw_image = nullptr;
  require_ok("Unable to decode this HEIC image",
             lib.decode_image(handle.get(), &raw_image, kColorspaceRgb,
                              plan.depth ? kChromaInterleavedRrggbbaaLe : kChromaInterleavedRgba, options.get()));
  std::unique_ptr<heif_image, ImageDeleter> image(raw_image, ImageDeleter{&lib});
  if (plan.depth) {
    return read_deep_pixels(lib, *image, plan, icc_profile, premultiplied, density,
                            lib.context_get_number_of_top_level_images(context.get()));
  }

  const int width_value = lib.image_get_width(image.get(), kChannelInterleaved);
  const int height_value = lib.image_get_height(image.get(), kChannelInterleaved);
  if (width_value <= 0 || height_value <= 0 ||
      static_cast<std::uint64_t>(width_value) * static_cast<std::uint64_t>(height_value) > kMaximumPixels) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This HEIF image's dimensions are not supported"));
  }
  if (lib.image_get_bits_per_pixel(image.get(), kChannelInterleaved) != 32) {
    // An old libheif without convert_hdr_to_8bit hands back 16-bit interleaved samples.
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP(
        "QObject", "This HEIC image's bit depth is not supported by the installed libheif library."));
  }
  const auto width = static_cast<std::int32_t>(width_value);
  const auto height = static_cast<std::int32_t>(height_value);

  int stride = 0;
  const auto* plane = lib.image_get_plane_readonly(image.get(), kChannelInterleaved, &stride);
  const auto tight_stride = static_cast<std::size_t>(width) * 4U;
  if (plane == nullptr || stride < 0 || static_cast<std::size_t>(stride) < tight_stride) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "libheif returned an invalid HEIF pixel buffer"));
  }

  const bool has_alpha = has_meaningful_alpha(plane, stride, width, height);
  const auto pixel_format = has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8();
  PixelBuffer pixels(width, height, pixel_format);
  std::vector<std::uint8_t> scratch_row(tight_stride);
  for (std::int32_t y = 0; y < height; ++y) {
    const auto* source = plane + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    auto destination = pixels.row(y);
    const std::uint8_t* rgba = source;
    if (premultiplied && has_alpha) {
      std::memcpy(scratch_row.data(), source, tight_stride);
      unpremultiply_row(scratch_row.data(), width);
      rgba = scratch_row.data();
    }
    if (icc_transform.has_value()) {
      // In place when the row is already in the scratch buffer; the transform accepts
      // overlapping source and destination.
      icc_transform->convert(rgba, scratch_row.data(), static_cast<std::size_t>(width));
      rgba = scratch_row.data();
    }
    if (has_alpha) {
      std::memcpy(destination.data(), rgba, tight_stride);
    } else {
      for (std::int32_t x = 0; x < width; ++x) {
        std::memcpy(destination.data() + static_cast<std::size_t>(x) * 3U, rgba + static_cast<std::size_t>(x) * 4U, 3U);
      }
    }
  }

  FormatReadResult result;
  result.document = Document(width, height, pixel_format);
  result.document.print_settings().horizontal_ppi = density.has_value() ? density->horizontal_ppi : 72.0;
  result.document.print_settings().vertical_ppi = density.has_value() ? density->vertical_ppi : 72.0;
  result.document.add_pixel_layer("Background", std::move(pixels));
  if (const int image_count = lib.context_get_number_of_top_level_images(context.get()); image_count > 1) {
    result.notices.push_back("Opened the primary image only (" + std::to_string(image_count) + " images in the file)");
  }
  return result;
}

}  // namespace patchy::heif
