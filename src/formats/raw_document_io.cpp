#include "formats/raw_document_io.hpp"

#include "libraw/libraw.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace patchy::raw {

namespace {

// LibRaw's supported linear exposure-shift range (0.25 = 2 stops darker, 8 = 3 lighter).
constexpr double kMinExposureShift = 0.25;
constexpr double kMaxExposureShift = 8.0;

[[noreturn]] void throw_libraw_error(int code, const char* stage) {
  std::string message = "Camera raw ";
  message += stage;
  message += " failed: ";
  switch (code) {
    case LIBRAW_FILE_UNSUPPORTED:
      message += "this is not a supported camera raw file";
      break;
    case LIBRAW_IO_ERROR:
    case LIBRAW_DATA_ERROR:
      message += "the file appears damaged or truncated";
      break;
    case LIBRAW_UNSUFFICIENT_MEMORY:
      message += "not enough memory to decode the sensor data";
      break;
    case LIBRAW_TOO_BIG:
      message += "the sensor data is larger than the decoder supports";
      break;
    default:
      message += libraw_strerror(code);
      break;
  }
  if (code == LIBRAW_FILE_UNSUPPORTED || code == LIBRAW_UNSUPPORTED_THUMBNAIL) {
    message +=
        " (lossy- and deflate-compressed DNG variants and a few newest proprietary "
        "compressions are not supported)";
  }
  throw std::runtime_error(message);
}

void check_libraw(int code, const char* stage) {
  if (code != LIBRAW_SUCCESS) {
    throw_libraw_error(code, stage);
  }
}

CameraMatrix camera_matrix_from(const float cam_xyz[4][3]) {
  CameraMatrix matrix{};
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 3; ++column) {
      matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] = cam_xyz[row][column];
    }
  }
  return matrix;
}

int libraw_quality_for(DemosaicAlgorithm algorithm) {
  switch (algorithm) {
    case DemosaicAlgorithm::Linear:
      return 0;
    case DemosaicAlgorithm::Vng:
      return 1;
    case DemosaicAlgorithm::Ppg:
      return 2;
    case DemosaicAlgorithm::Ahd:
      return 3;
    case DemosaicAlgorithm::Dcb:
      return 4;
    case DemosaicAlgorithm::Dht:
      return 11;
    case DemosaicAlgorithm::ModifiedAhd:
      return 12;
  }
  return 3;
}

int libraw_highlight_for(HighlightMode mode) {
  switch (mode) {
    case HighlightMode::Clip:
      return 0;
    case HighlightMode::Unclip:
      return 1;
    case HighlightMode::Blend:
      return 2;
    case HighlightMode::Rebuild:
      // 3..9 rebuild with varying color bias; 5 is dcraw's documented starting point.
      return 5;
  }
  return 0;
}

}  // namespace

const std::vector<std::string>& camera_raw_extensions() {
  // Mainstream interchangeable-lens and compact camera formats LibRaw decodes. ".raw" is
  // deliberately absent (it is used for arbitrary sensor/firmware dumps), and TIFF-based
  // raws saved as ".tif" stay with the regular TIFF path.
  static const std::vector<std::string> extensions = {
      "dng",  // Adobe/standard (phones, drones, many cameras)
      "cr2", "cr3", "crw",  // Canon
      "nef", "nrw",         // Nikon
      "arw", "sr2", "srf",  // Sony
      "orf",                // Olympus/OM System
      "raf",                // Fujifilm
      "rw2",                // Panasonic
      "pef",                // Pentax
      "srw",                // Samsung
      "mrw",                // Minolta
      "3fr", "fff",         // Hasselblad
      "iiq",                // Phase One
      "erf",                // Epson
      "kdc", "dcr",         // Kodak
      "mos",                // Leaf
      "rwl",                // Leica
      "x3f",                // Sigma (Foveon)
  };
  return extensions;
}

bool is_camera_raw_extension(std::string_view extension) {
  const auto& extensions = camera_raw_extensions();
  return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

struct DevelopSession::Impl {
  // Declared before the processor: LibRaw's stream reads from this buffer, so it must
  // outlive the processor (members destroy in reverse order).
  std::vector<std::uint8_t> file_bytes;
  LibRaw processor;
  RawFileInfo info;

  void apply_params(const DevelopParams& params) {
    auto& output = processor.imgdata.params;
    output.output_bps = 8;
    output.output_color = 1;  // sRGB primaries
    // sRGB transfer curve (dcraw -g 2.4 12.92); LibRaw's default is BT.709.
    output.gamm[0] = 1.0 / 2.4;
    output.gamm[1] = 12.92;
    output.user_flip = -1;  // camera orientation

    output.use_camera_wb = 0;
    output.use_auto_wb = 0;
    std::fill(std::begin(output.user_mul), std::end(output.user_mul), 0.0f);
    switch (params.white_balance) {
      case WhiteBalanceMode::AsShot:
        output.use_camera_wb = 1;
        break;
      case WhiteBalanceMode::Auto:
        output.use_auto_wb = 1;
        break;
      case WhiteBalanceMode::Custom: {
        const auto multipliers = multipliers_for_white_balance(
            params.custom_white_balance, camera_matrix_from(processor.imgdata.color.cam_xyz));
        for (std::size_t channel = 0; channel < 4; ++channel) {
          output.user_mul[channel] = static_cast<float>(multipliers[channel]);
        }
        break;
      }
    }

    const auto shift = std::clamp(std::exp2(params.exposure_ev), kMinExposureShift, kMaxExposureShift);
    output.exp_correc = std::abs(params.exposure_ev) > 1e-3 ? 1 : 0;
    output.exp_shift = static_cast<float>(shift);
    // Keep some highlight detail when pushing exposure up; no effect when darkening.
    output.exp_preser = 0.8f;

    output.highlight = libraw_highlight_for(params.highlights);
    output.no_auto_bright = params.auto_brighten ? 0 : 1;
    output.bright = static_cast<float>(std::clamp(params.brightness, 0.25, 4.0));
    output.user_qual = libraw_quality_for(params.demosaic);
    output.threshold = static_cast<float>(std::clamp(params.wavelet_denoise_threshold, 0, 1000));
    output.fbdd_noiserd = std::clamp(static_cast<int>(params.fbdd), 0, 2);
    output.half_size = params.half_size ? 1 : 0;
  }

  void gather_info() {
    const auto& idata = processor.imgdata.idata;
    const auto& other = processor.imgdata.other;
    const auto& sizes = processor.imgdata.sizes;

    info.camera_make = idata.normalized_make[0] != '\0' ? idata.normalized_make : idata.make;
    info.camera_model = idata.normalized_model[0] != '\0' ? idata.normalized_model : idata.model;
    info.lens = processor.imgdata.lens.Lens;
    info.iso = other.iso_speed;
    info.shutter_seconds = other.shutter;
    info.aperture_f_number = other.aperture;
    info.focal_length_mm = other.focal_len;
    info.timestamp = static_cast<long long>(other.timestamp);
    const bool swaps_axes = sizes.flip == 5 || sizes.flip == 6;
    info.output_width = swaps_axes ? sizes.height : sizes.width;
    info.output_height = swaps_axes ? sizes.width : sizes.height;
    info.orientation_flip = sizes.flip;
    info.is_xtrans = idata.filters == 9;
    info.is_foveon = idata.is_foveon != 0;

    const auto& color = processor.imgdata.color;
    if (color.cam_mul[0] > 0.0f && color.cam_mul[1] > 0.0f && color.cam_mul[2] > 0.0f) {
      const std::array<double, 4> multipliers = {
          color.cam_mul[0] / color.cam_mul[1],
          1.0,
          color.cam_mul[2] / color.cam_mul[1],
          color.cam_mul[3] > 0.0f ? color.cam_mul[3] / color.cam_mul[1] : 1.0,
      };
      info.as_shot_white_balance = white_balance_for_multipliers(multipliers, camera_matrix_from(color.cam_xyz));
    }

    // Best-effort embedded preview; JPEG bytes are handed to the caller verbatim.
    if (processor.unpack_thumb() == LIBRAW_SUCCESS) {
      const auto& thumbnail = processor.imgdata.thumbnail;
      if (thumbnail.tformat == LIBRAW_THUMBNAIL_JPEG && thumbnail.thumb != nullptr && thumbnail.tlength > 0) {
        const auto* begin = reinterpret_cast<const std::uint8_t*>(thumbnail.thumb);
        info.thumbnail.assign(begin, begin + thumbnail.tlength);
      }
    }
  }
};

DevelopSession::DevelopSession(std::vector<std::uint8_t> file_bytes) : impl_(std::make_unique<Impl>()) {
  impl_->file_bytes = std::move(file_bytes);
  if (impl_->file_bytes.empty()) {
    throw std::runtime_error("Camera raw open failed: the file is empty");
  }
  check_libraw(impl_->processor.open_buffer(impl_->file_bytes.data(), impl_->file_bytes.size()), "open");
  check_libraw(impl_->processor.unpack(), "decode");
  impl_->gather_info();
}

DevelopSession::~DevelopSession() = default;

const RawFileInfo& DevelopSession::info() const noexcept {
  return impl_->info;
}

DevelopSession::DevelopedImage DevelopSession::develop(const DevelopParams& params) {
  impl_->apply_params(params);
  check_libraw(impl_->processor.dcraw_process(), "develop");

  int error_code = LIBRAW_SUCCESS;
  auto* processed = impl_->processor.dcraw_make_mem_image(&error_code);
  if (processed == nullptr) {
    throw_libraw_error(error_code, "develop");
  }
  const auto release = [](libraw_processed_image_t* image) { LibRaw::dcraw_clear_mem(image); };
  const std::unique_ptr<libraw_processed_image_t, decltype(release)> guard(processed, release);

  if (processed->type != LIBRAW_IMAGE_BITMAP || processed->bits != 8 ||
      (processed->colors != 3 && processed->colors != 1)) {
    throw std::runtime_error("Camera raw develop failed: unexpected decoder output format");
  }

  DevelopedImage image;
  image.width = processed->width;
  image.height = processed->height;
  const auto pixel_count = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
  image.rgb.resize(pixel_count * 3);
  if (processed->colors == 3) {
    if (processed->data_size < pixel_count * 3) {
      throw std::runtime_error("Camera raw develop failed: decoder returned a short image");
    }
    std::memcpy(image.rgb.data(), processed->data, pixel_count * 3);
  } else {
    // Monochrome sensors (Leica M Monochrom and similar) develop to one channel.
    if (processed->data_size < pixel_count) {
      throw std::runtime_error("Camera raw develop failed: decoder returned a short image");
    }
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
      const auto value = processed->data[pixel];
      image.rgb[pixel * 3 + 0] = value;
      image.rgb[pixel * 3 + 1] = value;
      image.rgb[pixel * 3 + 2] = value;
    }
  }
  return image;
}

FormatReadResult DevelopSession::develop_document(const DevelopParams& params) {
  auto image = develop(params);
  PixelBuffer pixels(image.width, image.height, PixelFormat::rgb8());
  for (std::int32_t y = 0; y < image.height; ++y) {
    const auto* source = image.rgb.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) * 3;
    auto row = pixels.row(y);
    std::memcpy(row.data(), source, static_cast<std::size_t>(image.width) * 3);
  }

  FormatReadResult result;
  result.document = Document(image.width, image.height, PixelFormat::rgb8());
  result.document.add_pixel_layer("Background", std::move(pixels));
  return result;
}

FormatReadResult read_camera_raw(std::span<const std::uint8_t> bytes, const DevelopParams& params) {
  DevelopSession session(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
  return session.develop_document(params);
}

}  // namespace patchy::raw
