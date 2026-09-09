#include "formats/raw_document_io.hpp"

#include "formats/raw_tone.hpp"

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
constexpr int kDraftLongEdge = 1280;

DevelopParams draft_decode_key(DevelopParams params) {
  params.processing_version = kProcessingVersion;
  params.profile = RenderingProfile::Neutral;
  params.contrast = params.highlights = params.shadows = params.saturation = params.vibrance = 0;
  params.half_size = false;
  params.demosaic = DemosaicAlgorithm::Ahd;
  params.noise_reduction = NoiseReductionMode::Off;
  params.wavelet_denoise_threshold = params.color_denoise_passes = 0;
  params.fbdd = FbddNoiseReduction::Off;
  return params;
}

// Decoder stages have different costs. These are completed-work estimates, not
// elapsed-time animation; gaps without LibRaw checkpoints remain at the last value.
int processing_progress(LibRaw_progress stage, int iteration, int expected) {
  int low = 15, high = 20;
  switch (stage) {
    case LIBRAW_PROGRESS_OPEN: case LIBRAW_PROGRESS_IDENTIFY: low = 0; high = 3; break;
    case LIBRAW_PROGRESS_LOAD_RAW: low = 3; high = 15; break;
    case LIBRAW_PROGRESS_SCALE_COLORS: low = 20; high = 45; break;
    case LIBRAW_PROGRESS_PRE_INTERPOLATE: low = 45; high = 50; break;
    case LIBRAW_PROGRESS_INTERPOLATE: low = 50; high = 80; break;
    case LIBRAW_PROGRESS_MIX_GREEN: case LIBRAW_PROGRESS_MEDIAN_FILTER: low = 80; high = 87; break;
    case LIBRAW_PROGRESS_HIGHLIGHTS: low = 87; high = 90; break;
    case LIBRAW_PROGRESS_CONVERT_RGB: low = 90; high = 93; break;
    case LIBRAW_PROGRESS_STRETCH: low = 93; high = 94; break;
    default: break;
  }
  return low + (high - low) * std::clamp(iteration, 0, std::max(1, expected - 1)) / std::max(1, expected - 1);
}

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

DevelopParams normalize_develop_params(DevelopParams params) {
  const auto bounded = [](double value, double low, double high, double fallback) {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
  };
  params.custom_white_balance.temperature_k = bounded(params.custom_white_balance.temperature_k, 2000, 25000, 5500);
  params.custom_white_balance.tint = bounded(params.custom_white_balance.tint, -150, 150, 0);
  params.exposure_ev = bounded(params.exposure_ev, -2, 3, 0);
  params.brightness = bounded(params.brightness, 0.25, 4, 1);
  for (auto* value : {&params.contrast, &params.highlights, &params.shadows, &params.saturation, &params.vibrance})
    *value = bounded(*value, -100, 100, 0);
  params.wavelet_denoise_threshold = std::clamp(params.wavelet_denoise_threshold, 0, 1000);
  params.color_denoise_passes = std::clamp(params.color_denoise_passes, 0, 4);
  if (params.processing_version == 1) {
    params.profile = RenderingProfile::Neutral;
    params.color_denoise_passes = 0;
  }
  return params;
}

EffectiveNoiseReduction effective_noise_reduction(const DevelopParams& params, const RawFileInfo& info) {
  EffectiveNoiseReduction result;
  result.auto_available = info.is_three_color_bayer && std::isfinite(info.iso) && info.iso > 0;
  if (params.noise_reduction == NoiseReductionMode::Manual) {
    result.wavelet_threshold = std::clamp(params.wavelet_denoise_threshold, 0, 1000);
    result.fbdd = params.fbdd;
    if (params.processing_version >= 2 && info.is_three_color_bayer)
      result.color_passes = std::clamp(params.color_denoise_passes, 0, 4);
  } else if (params.noise_reduction == NoiseReductionMode::Auto && result.auto_available) {
    // Fixed, metadata-only policies. Version 2 also controls the brightness grain
    // made more visible by photographic midtone rendering. Version 1 is frozen.
    const double strength = params.processing_version == 1 ? 50.0 : 100.0;
    result.wavelet_threshold = static_cast<int>(std::lround(
        std::clamp(strength * std::log2(std::max(info.iso, 400.0) / 400.0), 0.0, strength * 5.0)));
    result.fbdd = info.iso >= 1600 ? FbddNoiseReduction::Full :
                  info.iso >= 800 ? FbddNoiseReduction::Light : FbddNoiseReduction::Off;
    if (params.processing_version >= 2)
      result.color_passes = info.iso >= 1600 ? 2 : info.iso >= 800 ? 1 : 0;
  }
  return result;
}

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
  const libraw_output_params_t initial_output{processor.imgdata.params};
  RawFileInfo info;
  bool needs_unpack{true};
  bool needs_reopen{false};
  std::shared_ptr<libraw_processed_image_t> draft_pixels;
  DevelopParams draft_key;
  DevelopedImage draft_metadata;

  // At SCALE_COLORS entry, raw2image has subtracted the sensor black levels and
  // packed each 2x2 Bayer cell into four channels. Average those cells in place
  // before WB, exposure, RGB conversion or tone processing. The next raw2image
  // restores the original geometry from rawdata, so final processing is untouched.
  bool reduce_draft_sensor(const DevelopOptions& options) {
    if (!info.is_three_color_bayer) return true;
    auto& sizes = processor.imgdata.sizes;
    const int width = sizes.iwidth, height = sizes.iheight;
    const int step = (std::max(width, height) + kDraftLongEdge - 1) / kDraftLongEdge;
    if (step <= 1) return true;
    const int reduced_width = (width + step - 1) / step;
    const int reduced_height = (height + step - 1) / step;
    auto* pixels = processor.imgdata.image;
    for (int y = 0; y < reduced_height; ++y) {
      if (options.cancelled && options.cancelled()) return false;
      for (int x = 0; x < reduced_width; ++x) {
        std::array<std::uint64_t, 4> sums{};
        unsigned count = 0;
        for (int sy = y * step; sy < std::min((y + 1) * step, height); ++sy)
          for (int sx = x * step; sx < std::min((x + 1) * step, width); ++sx) {
            for (int c = 0; c < 4; ++c) sums[c] += pixels[sy * width + sx][c];
            ++count;
          }
        for (int c = 0; c < 4; ++c)
          pixels[y * reduced_width + x][c] = static_cast<unsigned short>((sums[c] + count / 2) / count);
      }
    }
    sizes.iwidth = static_cast<unsigned short>(reduced_width);
    sizes.iheight = static_cast<unsigned short>(reduced_height);
    sizes.width = static_cast<unsigned short>(reduced_width * 2);
    sizes.height = static_cast<unsigned short>(reduced_height * 2);
    return true;
  }

  void apply_params(const DevelopParams& params, DevelopQuality quality) {
    auto& output = processor.imgdata.params;
    // 16-bit output: the tone/color stage below runs at raw precision and quantizes to
    // 8 bits only at the very end.
    output.output_bps = 16;
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

    output.highlight = libraw_highlight_for(params.highlight_recovery);
    output.no_auto_bright = params.auto_brighten ? 0 : 1;
    output.bright = static_cast<float>(std::clamp(params.brightness, 0.25, 4.0));
    output.user_qual = libraw_quality_for(params.demosaic);
    const auto noise = effective_noise_reduction(params, info);
    output.threshold = quality == DevelopQuality::Final ? static_cast<float>(noise.wavelet_threshold) : 0.0f;
    output.fbdd_noiserd = quality == DevelopQuality::Final ? std::clamp(static_cast<int>(noise.fbdd), 0, 2) : 0;
    output.med_passes = quality == DevelopQuality::Final ? noise.color_passes : 0;
    output.half_size = quality == DevelopQuality::Draft ? 1 : 0;
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
    info.is_three_color_bayer = !info.is_foveon && idata.filters > 1000 && idata.colors == 3;
    if (info.is_three_color_bayer) {
      std::array<int, 3> sites{};
      const auto channel_at = [&](int y, int x) {
        const auto channel = processor.fcol(y, x);
        return channel == 3 ? 1 : channel;
      };
      for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
          const auto channel = channel_at(y, x);
          if (channel < 0 || channel > 2 || channel != channel_at(y % 2, x % 2))
            info.is_three_color_bayer = false;
          else if (y < 2 && x < 2) ++sites[static_cast<std::size_t>(channel)];
        }
      }
      info.is_three_color_bayer = info.is_three_color_bayer && sites == std::array{1, 2, 1};
    }

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
  // Metadata and the embedded preview are available before the expensive unpack.
  impl_->gather_info();
}

DevelopSession::~DevelopSession() = default;

const RawFileInfo& DevelopSession::info() const noexcept {
  return impl_->info;
}

DevelopSession::DevelopedImage DevelopSession::develop(const DevelopParams& requested, const DevelopOptions& options) {
  if (requested.processing_version < 1 || requested.processing_version > kProcessingVersion)
    throw std::invalid_argument("Unsupported RAW processing version");
  if (options.cancelled && options.cancelled()) throw DevelopCancelled{};
  const auto params = normalize_develop_params(requested);
  int last_progress = -1;
  const auto report = [&](int percent) {
    if (percent > last_progress) {
      last_progress = percent;
      if (options.progress) options.progress(percent);
    }
  };
  report(0);
  const auto decode_key = draft_decode_key(params);
  const bool cached = options.quality == DevelopQuality::Draft && impl_->draft_pixels && impl_->draft_key == decode_key;
  DevelopedImage image;
  const auto release = [](libraw_processed_image_t* pixels) { LibRaw::dcraw_clear_mem(pixels); };
  std::unique_ptr<libraw_processed_image_t, decltype(release)> guard(nullptr, release);
  libraw_processed_image_t* processed = nullptr;
  if (cached) {
    image = impl_->draft_metadata;
    image.reused_draft_decode = true;
    processed = impl_->draft_pixels.get();
  } else {
    auto& processor = impl_->processor;
    const auto progress_callback = [&](LibRaw_progress stage, int iteration, int expected) {
      if (options.cancelled && options.cancelled()) return 1;
      if (options.quality == DevelopQuality::Draft && stage == LIBRAW_PROGRESS_SCALE_COLORS && iteration == 0 &&
          !impl_->reduce_draft_sensor(options)) return 1;
      report(processing_progress(stage, iteration, expected));
      return 0;
    };
    processor.set_progress_handler([](void* context, LibRaw_progress stage, int iteration, int expected) {
      return (*static_cast<const decltype(progress_callback)*>(context))(stage, iteration, expected);
    }, const_cast<void*>(static_cast<const void*>(&progress_callback)));
    struct ClearCallback {
      LibRaw& processor;
      ~ClearCallback() { processor.set_progress_handler(nullptr, nullptr); }
    } clear_callback{processor};
    const auto check_processing = [&](int code, const char* stage) {
      if (code != LIBRAW_SUCCESS) impl_->needs_unpack = impl_->needs_reopen = true;
      if (code == LIBRAW_CANCELLED_BY_CALLBACK) throw DevelopCancelled{};
      check_libraw(code, stage);
    };
    if (impl_->needs_unpack) {
      if (impl_->needs_reopen) {
        processor.recycle();
        // Identification uses threshold/half_size when calculating active dimensions.
        // Reopen with the same neutral decoder options as the initial unpack.
        processor.imgdata.params = impl_->initial_output;
        check_processing(processor.open_buffer(impl_->file_bytes.data(), impl_->file_bytes.size()), "open");
      }
      check_processing(processor.unpack(), "decode");
      impl_->needs_unpack = impl_->needs_reopen = false;
    }
    impl_->apply_params(params, options.quality);
    check_processing(processor.dcraw_process(), "develop");

    int error_code = LIBRAW_SUCCESS;
    report(94);
    processed = impl_->processor.dcraw_make_mem_image(&error_code);
    if (processed == nullptr) {
      throw_libraw_error(error_code, "develop");
    }
    guard.reset(processed);

    if (processed->type != LIBRAW_IMAGE_BITMAP || processed->bits != 16 ||
        (processed->colors != 3 && processed->colors != 1)) {
      throw std::runtime_error("Camera raw develop failed: unexpected decoder output format");
    }

    image.width = processed->width;
    image.height = processed->height;
    image.quality = options.quality;
    image.processing_version = params.processing_version;
    image.profile = params.profile;
    image.fast_half_size = options.quality == DevelopQuality::Draft;
    if (impl_->info.is_three_color_bayer && !image.fast_half_size) image.demosaic = params.demosaic;
    image.noise = effective_noise_reduction(params, impl_->info);
    if (image.fast_half_size || !impl_->info.is_three_color_bayer) image.noise.fbdd = FbddNoiseReduction::Off;
    if (image.fast_half_size) image.noise.wavelet_threshold = image.noise.color_passes = 0;
    const auto& color = processor.imgdata.color;
    if (color.pre_mul[0] > 0 && color.pre_mul[1] > 0 && color.pre_mul[2] > 0) {
      image.white_balance_multipliers = std::array<double, 4>{
          color.pre_mul[0] / color.pre_mul[1], 1.0, color.pre_mul[2] / color.pre_mul[1],
          color.pre_mul[3] > 0 ? color.pre_mul[3] / color.pre_mul[1] : 1.0};
      image.effective_white_balance = white_balance_for_multipliers(
          *image.white_balance_multipliers, camera_matrix_from(color.cam_xyz));
    }
    if (options.quality == DevelopQuality::Draft) {
      impl_->draft_metadata = image;
      impl_->draft_key = decode_key;
      impl_->draft_pixels.reset(guard.release(), release);
    }
  }
  image.processing_version = params.processing_version;
  image.profile = params.profile;
  report(95);
  const auto pixel_count = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
  const auto channel_count = static_cast<std::size_t>(processed->colors);
  if (processed->data_size < pixel_count * channel_count * 2) {
    throw std::runtime_error("Camera raw develop failed: decoder returned a short image");
  }
  // libraw_processed_image_t's data payload starts 2-byte aligned (fixed 16-byte header).
  const auto* source = reinterpret_cast<const std::uint16_t*>(processed->data);

  // Patchy's tone/color stage runs here, on the 16-bit data, then bakes to 8 bits.
  const ToneParams tone{params.contrast, params.highlights, params.shadows};
  const bool color_active = params.saturation != 0.0 || params.vibrance != 0.0;
  const auto lut = build_tone_lut(tone);
  const bool natural = params.profile == RenderingProfile::Natural;
  static const auto original_profile_lut = build_natural_profile_lut(2);
  static const auto current_profile_lut = build_natural_profile_lut();
  const auto& profile_lut = params.processing_version <= 2 ? original_profile_lut : current_profile_lut;
  const auto quantize8 = [](std::uint32_t value16) {
    return static_cast<std::uint8_t>((value16 * 255U + 32767U) / 65535U);
  };

  const int source_width = image.width;
  const int source_height = image.height;
  const int step = options.quality == DevelopQuality::Draft ?
      std::max(1, (std::max(source_width, source_height) + kDraftLongEdge - 1) / kDraftLongEdge) :
      params.half_size ? 2 : 1;
  image.width = (source_width + step - 1) / step;
  image.height = (source_height + step - 1) / step;
  image.output_width = options.quality == DevelopQuality::Final ? image.width :
      params.half_size ? (impl_->info.output_width + 1) / 2 : impl_->info.output_width;
  image.output_height = options.quality == DevelopQuality::Final ? image.height :
      params.half_size ? (impl_->info.output_height + 1) / 2 : impl_->info.output_height;
  image.rgb.resize(static_cast<std::size_t>(image.width) * image.height * 3);
  for (int y = 0; y < image.height; ++y) {
    if (options.cancelled && options.cancelled()) throw DevelopCancelled{};
    report(95 + 4 * y / image.height);
    for (int x = 0; x < image.width; ++x) {
      std::array<std::uint32_t, 3> sum{};
      std::uint32_t samples = 0;
      for (int sy = y * step; sy < std::min((y + 1) * step, source_height); ++sy) {
        for (int sx = x * step; sx < std::min((x + 1) * step, source_width); ++sx) {
          const auto* in = source + (static_cast<std::size_t>(sy) * source_width + sx) * channel_count;
          std::array<std::uint16_t, 3> channels = channel_count == 3 ?
              std::array<std::uint16_t, 3>{in[0], in[1], in[2]} :
              std::array<std::uint16_t, 3>{in[0], in[0], in[0]};
          if (natural) apply_natural_profile(channels, profile_lut, params.processing_version);
          for (auto& channel : channels) channel = lut[channel];
          if (color_active && channel_count == 3)
            apply_color(channels, params.saturation, params.vibrance);
          for (std::size_t c = 0; c < 3; ++c) sum[c] += channels[c];
          ++samples;
        }
      }
      auto* out = image.rgb.data() + (static_cast<std::size_t>(y) * image.width + x) * 3;
      for (std::size_t c = 0; c < 3; ++c) out[c] = quantize8((sum[c] + samples / 2) / samples);
    }
  }
  report(100);
  return image;
}

FormatReadResult DevelopSession::develop_document(const DevelopParams& params, const DevelopOptions& options) {
  return document_from_developed(develop(params, options));
}

FormatReadResult document_from_developed(const DevelopSession::DevelopedImage& image) {
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
