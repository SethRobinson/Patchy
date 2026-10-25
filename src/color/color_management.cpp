#include "color/color_management.hpp"

#include "support/path_utils.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

// lcms2.h still spells `register` for pre-C++17 compilers; the opt-out keeps the C++ TU
// warning-clean.
#define CMS_NO_REGISTER_KEYWORD 1
#include "lcms2.h"

#include "color/lcms_intent.hpp"

namespace patchy {

std::vector<std::uint8_t> read_cmyk_profile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) return {};
  const auto size = input.tellg();
  if (size < 128 || size > 16 * 1024 * 1024) return {};
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) ||
      !CmykToRgbTransform::from_icc_profile(bytes)) return {};
  return bytes;
}

const std::vector<std::uint8_t>& default_cmyk_profile() {
  static const auto profile = [] {
    std::vector<std::filesystem::path> folders;
#if defined(_WIN32)
    for (const auto* variable : {L"CommonProgramFiles", L"CommonProgramFiles(x86)"}) {
      wchar_t* value = nullptr;
      std::size_t size = 0;
      if (_wdupenv_s(&value, &size, variable) == 0 && value != nullptr) {
        folders.emplace_back(std::filesystem::path(value) / L"Adobe/Color/Profiles/Recommended");
      }
      std::free(value);
    }
#elif defined(__APPLE__)
    folders.emplace_back("/Library/Application Support/Adobe/Color/Profiles/Recommended");
    folders.emplace_back("/Library/ColorSync/Profiles");
#else
    folders.emplace_back("/usr/share/color/icc/Adobe/CMYK");
    folders.emplace_back("/usr/share/color/icc");
#endif
    for (const auto& folder : folders) {
      auto bytes = read_cmyk_profile(folder / "USWebCoatedSWOP.icc");
      if (!bytes.empty()) return bytes;
    }
    return std::vector<std::uint8_t>{};
  }();
  return profile;
}

namespace {

// lcms reports recoverable errors through a callback; a bad profile must fall back to the
// naive conversion silently rather than spam stderr.
void ignore_lcms_error(cmsContext /*context*/, cmsUInt32Number /*code*/, const char* /*text*/) {}

// State shared by the embedded-profile-to-sRGB transforms (CMYK and gray).
struct IccToSrgbState {
  cmsContext context{nullptr};
  cmsHTRANSFORM transform{nullptr};
  cmsHTRANSFORM transform16{nullptr};
  std::string description;

  ~IccToSrgbState() {
    if (transform16 != nullptr) {
      cmsDeleteTransform(transform16);
    }
    if (transform != nullptr) {
      cmsDeleteTransform(transform);
    }
    if (context != nullptr) {
      cmsDeleteContext(context);
    }
  }
};

// Opens `profile_bytes`, requires its color space to be `expected_space`, and builds the
// transform from `input_format` to packed 8-bit sRGB with relative colorimetric intent,
// black point compensation (Photoshop's conversion defaults) and no pixel cache. Returns
// false, leaving `state` without a transform, when anything is unusable.
bool open_icc_to_srgb(IccToSrgbState& state, std::span<const std::uint8_t> profile_bytes,
                      cmsColorSpaceSignature expected_space, cmsUInt32Number input_format) {
  if (profile_bytes.empty() || profile_bytes.size() > 0xFFFFFFFFULL) {
    return false;
  }
  state.context = cmsCreateContext(nullptr, nullptr);
  if (state.context == nullptr) {
    return false;
  }
  cmsSetLogErrorHandlerTHR(state.context, ignore_lcms_error);

  cmsHPROFILE source_profile = cmsOpenProfileFromMemTHR(
      state.context, profile_bytes.data(), static_cast<cmsUInt32Number>(profile_bytes.size()));
  if (source_profile == nullptr) {
    return false;
  }
  if (cmsGetColorSpace(source_profile) != expected_space) {
    cmsCloseProfile(source_profile);
    return false;
  }

  std::array<char, 256> description{};
  if (cmsGetProfileInfoASCII(source_profile, cmsInfoDescription, "en", "US", description.data(),
                             static_cast<cmsUInt32Number>(description.size())) > 0) {
    state.description = description.data();
  }

  cmsHPROFILE srgb_profile = cmsCreate_sRGBProfileTHR(state.context);
  if (srgb_profile != nullptr) {
    state.transform = cmsCreateTransformTHR(state.context, source_profile, input_format,
                                            srgb_profile, TYPE_RGB_8, INTENT_RELATIVE_COLORIMETRIC,
                                            cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOCACHE);
    const auto input16 = expected_space == cmsSigCmykData ? TYPE_CMYK_16_REV : TYPE_GRAY_16;
    state.transform16 = cmsCreateTransformTHR(state.context, source_profile, input16,
                                              srgb_profile, TYPE_RGB_16, INTENT_RELATIVE_COLORIMETRIC,
                                              cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOCACHE);
    cmsCloseProfile(srgb_profile);
  }
  cmsCloseProfile(source_profile);
  return state.transform != nullptr && state.transform16 != nullptr;
}

}  // namespace

struct CmykToRgbTransform::Impl : IccToSrgbState {};

std::optional<CmykToRgbTransform> CmykToRgbTransform::from_icc_profile(
    std::span<const std::uint8_t> profile_bytes) {
  auto impl = std::make_unique<Impl>();
  if (!open_icc_to_srgb(*impl, profile_bytes, cmsSigCmykData, TYPE_CMYK_8_REV)) {
    return std::nullopt;
  }
  return CmykToRgbTransform(std::move(impl));
}

CmykToRgbTransform::CmykToRgbTransform(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CmykToRgbTransform::CmykToRgbTransform(CmykToRgbTransform&&) noexcept = default;
CmykToRgbTransform& CmykToRgbTransform::operator=(CmykToRgbTransform&&) noexcept = default;
CmykToRgbTransform::~CmykToRgbTransform() = default;

void CmykToRgbTransform::convert(const std::uint8_t* cmyk_inverted, std::uint8_t* rgb_out,
                                 std::size_t pixel_count) const {
  cmsDoTransform(impl_->transform, cmyk_inverted, rgb_out,
                 static_cast<cmsUInt32Number>(pixel_count));
}

void CmykToRgbTransform::convert16(const std::uint16_t* cmyk_inverted, std::uint16_t* rgb_out,
                                  std::size_t pixel_count) const {
  cmsDoTransform(impl_->transform16, cmyk_inverted, rgb_out, static_cast<cmsUInt32Number>(pixel_count));
}

RgbColor CmykToRgbTransform::convert_single(std::uint8_t cyan_inverted,
                                            std::uint8_t magenta_inverted,
                                            std::uint8_t yellow_inverted,
                                            std::uint8_t black_inverted) const {
  const std::array<std::uint8_t, 4> cmyk{cyan_inverted, magenta_inverted, yellow_inverted,
                                         black_inverted};
  std::array<std::uint8_t, 3> rgb{};
  convert(cmyk.data(), rgb.data(), 1);
  return RgbColor{rgb[0], rgb[1], rgb[2]};
}

const std::string& CmykToRgbTransform::profile_description() const {
  return impl_->description;
}

struct RgbToCmykTransform::Impl {
  cmsContext context{nullptr};
  cmsHTRANSFORM transform{nullptr};

  ~Impl() {
    if (transform != nullptr) cmsDeleteTransform(transform);
    if (context != nullptr) cmsDeleteContext(context);
  }
};

std::optional<RgbToCmykTransform> RgbToCmykTransform::from_icc_profile(
    std::span<const std::uint8_t> profile_bytes) {
  if (profile_bytes.empty() || profile_bytes.size() > 0xFFFFFFFFULL) return std::nullopt;
  auto impl = std::make_unique<Impl>();
  impl->context = cmsCreateContext(nullptr, nullptr);
  if (impl->context == nullptr) return std::nullopt;
  cmsSetLogErrorHandlerTHR(impl->context, ignore_lcms_error);
  // The same profiles, formats, intent and flags as rgb_to_native_color_space, so both
  // produce the same inks.
  const auto source = cmsCreate_sRGBProfileTHR(impl->context);
  const auto destination = cmsOpenProfileFromMemTHR(impl->context, profile_bytes.data(),
                                                    static_cast<cmsUInt32Number>(profile_bytes.size()));
  if (source != nullptr && destination != nullptr && cmsGetColorSpace(destination) == cmsSigCmykData) {
    impl->transform = cmsCreateTransformTHR(impl->context, source, TYPE_RGB_8, destination, TYPE_CMYK_8_REV,
                                            INTENT_RELATIVE_COLORIMETRIC,
                                            cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOCACHE);
  }
  if (source != nullptr) cmsCloseProfile(source);
  if (destination != nullptr) cmsCloseProfile(destination);
  if (impl->transform == nullptr) return std::nullopt;
  return RgbToCmykTransform(std::move(impl));
}

RgbToCmykTransform::RgbToCmykTransform(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RgbToCmykTransform::RgbToCmykTransform(RgbToCmykTransform&&) noexcept = default;
RgbToCmykTransform& RgbToCmykTransform::operator=(RgbToCmykTransform&&) noexcept = default;
RgbToCmykTransform::~RgbToCmykTransform() = default;

void RgbToCmykTransform::convert(const std::uint8_t* rgb, std::uint8_t* cmyk_inverted_out,
                                 std::size_t pixel_count) const {
  cmsDoTransform(impl_->transform, rgb, cmyk_inverted_out, static_cast<cmsUInt32Number>(pixel_count));
}

std::shared_ptr<const InkSpace> build_cmyk_ink_space(std::span<const std::uint8_t> profile_bytes,
                                                     std::string id) {
  if (profile_bytes.empty() || profile_bytes.size() > 0xFFFFFFFFULL) {
    return nullptr;
  }
  cmsContext context = cmsCreateContext(nullptr, nullptr);
  if (context == nullptr) {
    return nullptr;
  }
  cmsSetLogErrorHandlerTHR(context, ignore_lcms_error);
  cmsHPROFILE ink_profile = cmsOpenProfileFromMemTHR(context, profile_bytes.data(),
                                                     static_cast<cmsUInt32Number>(profile_bytes.size()));
  cmsHPROFILE srgb_profile = cmsCreate_sRGBProfileTHR(context);
  cmsHTRANSFORM to_ink = nullptr;
  cmsHTRANSFORM to_rgb = nullptr;
  if (ink_profile != nullptr && srgb_profile != nullptr && cmsGetColorSpace(ink_profile) == cmsSigCmykData) {
    constexpr cmsUInt32Number flags = cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOCACHE;
    to_ink = cmsCreateTransformTHR(context, srgb_profile, TYPE_RGB_16, ink_profile, TYPE_CMYK_16_REV,
                                   INTENT_RELATIVE_COLORIMETRIC, flags);
    to_rgb = cmsCreateTransformTHR(context, ink_profile, TYPE_CMYK_16_REV, srgb_profile, TYPE_RGB_16,
                                   INTENT_RELATIVE_COLORIMETRIC, flags);
  }
  std::shared_ptr<InkSpace> space;
  if (to_ink != nullptr && to_rgb != nullptr) {
    space = std::make_shared<InkSpace>();
    space->id = std::move(id);
    space->rgb_grid = 33;
    space->ink_grid = 17;
    const auto node_value = [](int node, int grid) {
      return static_cast<std::uint16_t>((static_cast<std::uint32_t>(node) * 65535U + static_cast<std::uint32_t>(grid - 1) / 2U) /
                                        static_cast<std::uint32_t>(grid - 1));
    };
    // One row of nodes per transform call keeps the buffers small.
    std::vector<std::uint16_t> input;
    space->rgb_to_ink.resize(static_cast<std::size_t>(33) * 33U * 33U * 4U);
    input.resize(33U * 3U);
    std::size_t out = 0;
    for (int red = 0; red < 33; ++red) {
      for (int green = 0; green < 33; ++green) {
        for (int blue = 0; blue < 33; ++blue) {
          input[static_cast<std::size_t>(blue) * 3U] = node_value(red, 33);
          input[static_cast<std::size_t>(blue) * 3U + 1U] = node_value(green, 33);
          input[static_cast<std::size_t>(blue) * 3U + 2U] = node_value(blue, 33);
        }
        cmsDoTransform(to_ink, input.data(), space->rgb_to_ink.data() + out, 33U);
        out += 33U * 4U;
      }
    }
    space->ink_to_rgb.resize(static_cast<std::size_t>(17) * 17U * 17U * 17U * 3U);
    input.resize(17U * 4U);
    out = 0;
    for (int cyan = 0; cyan < 17; ++cyan) {
      for (int magenta = 0; magenta < 17; ++magenta) {
        for (int yellow = 0; yellow < 17; ++yellow) {
          for (int black = 0; black < 17; ++black) {
            input[static_cast<std::size_t>(black) * 4U] = node_value(cyan, 17);
            input[static_cast<std::size_t>(black) * 4U + 1U] = node_value(magenta, 17);
            input[static_cast<std::size_t>(black) * 4U + 2U] = node_value(yellow, 17);
            input[static_cast<std::size_t>(black) * 4U + 3U] = node_value(black, 17);
          }
          cmsDoTransform(to_rgb, input.data(), space->ink_to_rgb.data() + out, 17U);
          out += 17U * 3U;
        }
      }
    }
  }
  if (to_ink != nullptr) {
    cmsDeleteTransform(to_ink);
  }
  if (to_rgb != nullptr) {
    cmsDeleteTransform(to_rgb);
  }
  if (srgb_profile != nullptr) {
    cmsCloseProfile(srgb_profile);
  }
  if (ink_profile != nullptr) {
    cmsCloseProfile(ink_profile);
  }
  cmsDeleteContext(context);
  return space;
}

struct GrayToRgbTransform::Impl : IccToSrgbState {};

std::optional<GrayToRgbTransform> GrayToRgbTransform::from_icc_profile(
    std::span<const std::uint8_t> profile_bytes) {
  auto impl = std::make_unique<Impl>();
  if (!open_icc_to_srgb(*impl, profile_bytes, cmsSigGrayData, TYPE_GRAY_8)) {
    return std::nullopt;
  }
  return GrayToRgbTransform(std::move(impl));
}

GrayToRgbTransform::GrayToRgbTransform(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
GrayToRgbTransform::GrayToRgbTransform(GrayToRgbTransform&&) noexcept = default;
GrayToRgbTransform& GrayToRgbTransform::operator=(GrayToRgbTransform&&) noexcept = default;
GrayToRgbTransform::~GrayToRgbTransform() = default;

void GrayToRgbTransform::convert(const std::uint8_t* gray, std::uint8_t* rgb_out,
                                 std::size_t pixel_count) const {
  cmsDoTransform(impl_->transform, gray, rgb_out, static_cast<cmsUInt32Number>(pixel_count));
}

void GrayToRgbTransform::convert16(const std::uint16_t* gray, std::uint16_t* rgb_out,
                                  std::size_t pixel_count) const {
  cmsDoTransform(impl_->transform16, gray, rgb_out, static_cast<cmsUInt32Number>(pixel_count));
}

RgbColor GrayToRgbTransform::convert_single(std::uint8_t gray) const {
  std::array<std::uint8_t, 3> rgb{};
  convert(&gray, rgb.data(), 1);
  return RgbColor{rgb[0], rgb[1], rgb[2]};
}

const std::string& GrayToRgbTransform::profile_description() const {
  return impl_->description;
}

std::shared_ptr<const InkSpace> build_gray_ink_space(std::span<const std::uint8_t> profile_bytes,
                                                     std::string id) {
  const auto transform = GrayToRgbTransform::from_icc_profile(profile_bytes);
  if (!transform.has_value()) {
    return nullptr;
  }
  auto space = std::make_shared<InkSpace>();
  space->id = std::move(id);
  std::array<std::uint8_t, 256> ramp{};
  for (std::size_t value = 0; value < ramp.size(); ++value) {
    ramp[value] = static_cast<std::uint8_t>(value);
  }
  space->gray_to_rgb.resize(768U);
  transform->convert(ramp.data(), space->gray_to_rgb.data(), ramp.size());
  // The inverse: for each sRGB level, the stored gray whose converted green is nearest
  // (the lower one on a tie, so the table does not depend on search order).
  space->rgb_to_gray.resize(256U);
  for (int level = 0; level < 256; ++level) {
    int best = 0;
    int best_distance = 256;
    for (int gray = 0; gray < 256; ++gray) {
      const auto distance = std::abs(static_cast<int>(space->gray_to_rgb[static_cast<std::size_t>(gray) * 3U + 1U]) - level);
      if (distance < best_distance) {
        best_distance = distance;
        best = gray;
      }
    }
    space->rgb_to_gray[static_cast<std::size_t>(level)] = static_cast<std::uint8_t>(best);
  }
  return space;
}

struct LabToRgbTransform::Impl {
  cmsContext context{nullptr};
  cmsHTRANSFORM transform{nullptr};
  cmsHTRANSFORM transform16{nullptr};

  ~Impl() {
    if (transform16 != nullptr) {
      cmsDeleteTransform(transform16);
    }
    if (transform != nullptr) {
      cmsDeleteTransform(transform);
    }
    if (context != nullptr) {
      cmsDeleteContext(context);
    }
  }
};

std::optional<LabToRgbTransform> LabToRgbTransform::create() {
  auto impl = std::make_unique<Impl>();
  impl->context = cmsCreateContext(nullptr, nullptr);
  if (impl->context == nullptr) {
    return std::nullopt;
  }
  cmsSetLogErrorHandlerTHR(impl->context, ignore_lcms_error);

  cmsHPROFILE lab_profile = cmsCreateLab4ProfileTHR(impl->context, nullptr);  // D50
  cmsHPROFILE srgb_profile = cmsCreate_sRGBProfileTHR(impl->context);
  if (lab_profile != nullptr && srgb_profile != nullptr) {
    impl->transform = cmsCreateTransformTHR(impl->context, lab_profile, TYPE_Lab_16, srgb_profile,
                                            TYPE_RGB_8, INTENT_RELATIVE_COLORIMETRIC,
                                            cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOCACHE);
    impl->transform16 = cmsCreateTransformTHR(impl->context, lab_profile, TYPE_Lab_16, srgb_profile,
                                              TYPE_RGB_16, INTENT_RELATIVE_COLORIMETRIC,
                                              cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOCACHE);
  }
  if (lab_profile != nullptr) {
    cmsCloseProfile(lab_profile);
  }
  if (srgb_profile != nullptr) {
    cmsCloseProfile(srgb_profile);
  }
  if (impl->transform == nullptr || impl->transform16 == nullptr) {
    return std::nullopt;
  }
  return LabToRgbTransform(std::move(impl));
}

LabToRgbTransform::LabToRgbTransform(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
LabToRgbTransform::LabToRgbTransform(LabToRgbTransform&&) noexcept = default;
LabToRgbTransform& LabToRgbTransform::operator=(LabToRgbTransform&&) noexcept = default;
LabToRgbTransform::~LabToRgbTransform() = default;

void LabToRgbTransform::convert(const std::uint16_t* lab_encoded, std::uint8_t* rgb_out,
                                std::size_t pixel_count) const {
  cmsDoTransform(impl_->transform, lab_encoded, rgb_out,
                 static_cast<cmsUInt32Number>(pixel_count));
}

void LabToRgbTransform::convert16(const std::uint16_t* lab_encoded, std::uint16_t* rgb_out,
                                 std::size_t pixel_count) const {
  cmsDoTransform(impl_->transform16, lab_encoded, rgb_out, static_cast<cmsUInt32Number>(pixel_count));
}

void ColorManager::assign_icc_profile(Document& document, std::vector<std::uint8_t> icc_profile) const {
  document.color_state().embedded_icc_profile = std::move(icc_profile);
}

PixelBuffer ColorManager::preview_rgb8(const Document& /*document*/, const PixelBuffer& source,
                                       const ColorTransformSpec& /*spec*/) const {
  if (source.format() != PixelFormat::rgb8()) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Color preview placeholder currently accepts RGB8 buffers only"));
  }
  return source;
}

std::optional<PixelBuffer> rgb_to_native_color_space(
    const PixelBuffer& rgb, ColorMode mode, std::span<const std::uint8_t> profile) {
  return rgb_to_native_color_space(rgb, mode, profile, RenderingIntent::RelativeColorimetric, true);
}

std::optional<PixelBuffer> rgb_to_native_color_space(
    const PixelBuffer& rgb, ColorMode mode, std::span<const std::uint8_t> profile, RenderingIntent intent,
    bool black_point_compensation) {
  if ((rgb.format() != PixelFormat::rgb16() && rgb.format() != PixelFormat::rgb8()) ||
      (mode != ColorMode::CMYK && mode != ColorMode::Lab && mode != ColorMode::Grayscale)) {
    return std::nullopt;
  }
  const std::uint16_t channels = mode == ColorMode::CMYK ? 4 : mode == ColorMode::Lab ? 3 : 1;
  const bool deep = rgb.format().bit_depth == BitDepth::UInt16;
  const std::size_t sample_bytes = deep ? 2U : 1U;
  PixelBuffer result(rgb.width(), rgb.height(), PixelFormat{mode, rgb.format().bit_depth, channels});
  if (profile.empty() && mode != ColorMode::Lab) {
    // Inverse of the profile-free import: neutral gray, or no black ink.
    for (int y = 0; y < rgb.height(); ++y) {
      const auto input = rgb.row(y);
      auto output = result.row(y);
      for (int x = 0; x < rgb.width(); ++x) {
        if (mode == ColorMode::Grayscale) {
          std::memcpy(output.data() + x * sample_bytes, input.data() + (x * 3 + 1) * sample_bytes, sample_bytes);
        } else {
          std::memcpy(output.data() + x * 4 * sample_bytes, input.data() + x * 3 * sample_bytes, 3 * sample_bytes);
          std::memset(output.data() + (x * 4 + 3) * sample_bytes, 255, sample_bytes);
        }
      }
    }
    return result;
  }
  if (profile.size() > 0xFFFFFFFFULL) return std::nullopt;
  const auto context = cmsCreateContext(nullptr, nullptr);
  if (context == nullptr) return std::nullopt;
  cmsSetLogErrorHandlerTHR(context, ignore_lcms_error);
  const auto source = cmsCreate_sRGBProfileTHR(context);
  const auto destination = mode == ColorMode::Lab
      ? cmsCreateLab4ProfileTHR(context, nullptr)
      : cmsOpenProfileFromMemTHR(context, profile.data(), static_cast<cmsUInt32Number>(profile.size()));
  const auto signature = mode == ColorMode::CMYK ? cmsSigCmykData
                         : mode == ColorMode::Lab ? cmsSigLabData : cmsSigGrayData;
  const auto format = mode == ColorMode::CMYK ? (deep ? TYPE_CMYK_16_REV : TYPE_CMYK_8_REV)
                      : mode == ColorMode::Lab ? (deep ? TYPE_Lab_16 : TYPE_Lab_8)
                                              : (deep ? TYPE_GRAY_16 : TYPE_GRAY_8);
  const auto transform = source != nullptr && destination != nullptr && cmsGetColorSpace(destination) == signature
      ? cmsCreateTransformTHR(context, source, deep ? TYPE_RGB_16 : TYPE_RGB_8, destination, format,
                              lcms_intent(intent),
                              (black_point_compensation ? cmsFLAGS_BLACKPOINTCOMPENSATION : 0U) | cmsFLAGS_NOCACHE)
      : nullptr;
  if (source != nullptr) cmsCloseProfile(source);
  if (destination != nullptr) cmsCloseProfile(destination);
  if (transform != nullptr) {
    for (int y = 0; y < rgb.height(); ++y) {
      cmsDoTransform(transform, rgb.row(y).data(), result.row(y).data(), static_cast<cmsUInt32Number>(rgb.width()));
    }
    cmsDeleteTransform(transform);
  }
  cmsDeleteContext(context);
  return transform != nullptr ? std::optional<PixelBuffer>{std::move(result)} : std::nullopt;
}

namespace {

[[nodiscard]] double clamped_percent(double value) noexcept {
  // A NaN compares false both ways and lands on 0.
  return value >= 100.0 ? 100.0 : (value > 0.0 ? value : 0.0);
}

// An ICC header names its data color space at byte 16 and carries 'acsp' at byte 36.
[[nodiscard]] bool header_is_cmyk_profile(std::span<const std::uint8_t> header) noexcept {
  return header.size() >= 128 && std::memcmp(header.data() + 16, "CMYK", 4) == 0 &&
         std::memcmp(header.data() + 36, "acsp", 4) == 0;
}

[[nodiscard]] bool file_is_cmyk_profile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::array<std::uint8_t, 128> header{};
  return input && input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size())) &&
         header_is_cmyk_profile(header);
}

// An RGB profile that describes a device or color space (display, input, output, color
// space classes), not a device link, abstract or named-color profile.
[[nodiscard]] bool header_is_rgb_profile(std::span<const std::uint8_t> header) noexcept {
  if (header.size() < 128 || std::memcmp(header.data() + 16, "RGB ", 4) != 0 ||
      std::memcmp(header.data() + 36, "acsp", 4) != 0) {
    return false;
  }
  const auto* device_class = header.data() + 12;
  return std::memcmp(device_class, "mntr", 4) == 0 || std::memcmp(device_class, "scnr", 4) == 0 ||
         std::memcmp(device_class, "prtr", 4) == 0 || std::memcmp(device_class, "spac", 4) == 0;
}

[[nodiscard]] bool file_is_rgb_profile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::array<std::uint8_t, 128> header{};
  return input && input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size())) &&
         header_is_rgb_profile(header);
}

[[nodiscard]] std::filesystem::path environment_folder(const char* name) {
#if defined(_WIN32)
  const std::wstring wide_name(name, name + std::strlen(name));
  wchar_t* value = nullptr;
  std::size_t size = 0;
  std::filesystem::path folder;
  if (_wdupenv_s(&value, &size, wide_name.c_str()) == 0 && value != nullptr) {
    folder = value;
  }
  std::free(value);
  return folder;
#else
  const char* value = std::getenv(name);
  return value != nullptr ? std::filesystem::path(value) : std::filesystem::path();
#endif
}

// Where Adobe software installs its profiles.
[[nodiscard]] std::vector<std::filesystem::path> adobe_profile_folders() {
  std::vector<std::filesystem::path> folders;
#if defined(_WIN32)
  for (const char* variable : {"CommonProgramFiles", "CommonProgramFiles(x86)"}) {
    if (const auto root = environment_folder(variable); !root.empty()) {
      folders.push_back(root / "Adobe" / "Color" / "Profiles");
    }
  }
#elif defined(__APPLE__)
  folders.emplace_back("/Library/Application Support/Adobe/Color/Profiles");
#else
  folders.emplace_back("/usr/share/color/icc/Adobe");
#endif
  return folders;
}

// The operating system's own profile folders, shared ones before the user's.
[[nodiscard]] std::vector<std::filesystem::path> system_profile_folders() {
  std::vector<std::filesystem::path> folders;
#if defined(_WIN32)
  if (const auto root = environment_folder("SystemRoot"); !root.empty()) {
    folders.push_back(root / "System32" / "spool" / "drivers" / "color");
  }
#elif defined(__APPLE__)
  folders.emplace_back("/Library/ColorSync/Profiles");
  folders.emplace_back("/System/Library/ColorSync/Profiles");
  if (const auto home = environment_folder("HOME"); !home.empty()) {
    folders.push_back(home / "Library" / "ColorSync" / "Profiles");
  }
#else
  folders.emplace_back("/usr/share/color/icc");
  folders.emplace_back("/usr/local/share/color/icc");
  if (const auto home = environment_folder("HOME"); !home.empty()) {
    folders.push_back(home / ".local" / "share" / "icc");
    folders.push_back(home / ".color" / "icc");
  }
#endif
  return folders;
}

// The description tag of a profile file in `space`; nullopt when it is not one Little CMS
// opens.
[[nodiscard]] std::optional<std::string> profile_file_description(const std::filesystem::path& path,
                                                                  cmsColorSpaceSignature space) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    return std::nullopt;
  }
  const auto size = input.tellg();
  if (size < 128 || size > 16 * 1024 * 1024) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
    return std::nullopt;
  }
  const auto context = cmsCreateContext(nullptr, nullptr);
  if (context == nullptr) {
    return std::nullopt;
  }
  cmsSetLogErrorHandlerTHR(context, ignore_lcms_error);
  std::optional<std::string> description;
  if (const auto profile = cmsOpenProfileFromMemTHR(context, bytes.data(), static_cast<cmsUInt32Number>(bytes.size()));
      profile != nullptr) {
    if (cmsGetColorSpace(profile) == space) {
      std::array<char, 256> text{};
      description = cmsGetProfileInfoASCII(profile, cmsInfoDescription, "en", "US", text.data(),
                                           static_cast<cmsUInt32Number>(text.size())) > 0
                        ? std::string(text.data())
                        : std::string();
    }
    cmsCloseProfile(profile);
  }
  cmsDeleteContext(context);
  return description;
}

[[nodiscard]] bool has_profile_extension(const std::filesystem::path& path) {
  auto extension = path_to_utf8(path.extension());
  for (auto& character : extension) {
    character = static_cast<char>(character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character);
  }
  return extension == ".icc" || extension == ".icm";
}

}  // namespace

std::string_view rendering_intent_token(RenderingIntent intent) noexcept {
  switch (intent) {
    case RenderingIntent::Perceptual:
      return "perceptual";
    case RenderingIntent::RelativeColorimetric:
      return "relative-colorimetric";
    case RenderingIntent::Saturation:
      return "saturation";
    case RenderingIntent::AbsoluteColorimetric:
      return "absolute-colorimetric";
  }
  return "relative-colorimetric";
}

std::optional<RenderingIntent> rendering_intent_from_token(std::string_view token) noexcept {
  for (const auto intent : {RenderingIntent::Perceptual, RenderingIntent::RelativeColorimetric,
                            RenderingIntent::Saturation, RenderingIntent::AbsoluteColorimetric}) {
    if (token == rendering_intent_token(intent)) {
      return intent;
    }
  }
  return std::nullopt;
}

struct CmykWorkingSpace::Impl {
  cmsContext context{nullptr};
  cmsHTRANSFORM to_cmyk{nullptr};
  cmsHTRANSFORM to_rgb{nullptr};
  std::vector<std::uint8_t> profile;
  std::string description;
  RenderingIntent intent{RenderingIntent::RelativeColorimetric};
  bool black_point_compensation{true};

  ~Impl() {
    if (to_cmyk != nullptr) {
      cmsDeleteTransform(to_cmyk);
    }
    if (to_rgb != nullptr) {
      cmsDeleteTransform(to_rgb);
    }
    if (context != nullptr) {
      cmsDeleteContext(context);
    }
  }
};

CmykWorkingSpace::CmykWorkingSpace(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CmykWorkingSpace::~CmykWorkingSpace() = default;

std::shared_ptr<const CmykWorkingSpace> CmykWorkingSpace::from_icc_profile(
    std::vector<std::uint8_t> profile_bytes, RenderingIntent intent, bool black_point_compensation) {
  if (!header_is_cmyk_profile(profile_bytes) || profile_bytes.size() > 0xFFFFFFFFULL) {
    return nullptr;
  }
  auto impl = std::make_unique<Impl>();
  impl->context = cmsCreateContext(nullptr, nullptr);
  if (impl->context == nullptr) {
    return nullptr;
  }
  cmsSetLogErrorHandlerTHR(impl->context, ignore_lcms_error);
  cmsHPROFILE ink_profile = cmsOpenProfileFromMemTHR(impl->context, profile_bytes.data(),
                                                     static_cast<cmsUInt32Number>(profile_bytes.size()));
  cmsHPROFILE srgb_profile = cmsCreate_sRGBProfileTHR(impl->context);
  if (ink_profile != nullptr && srgb_profile != nullptr && cmsGetColorSpace(ink_profile) == cmsSigCmykData) {
    std::array<char, 256> description{};
    if (cmsGetProfileInfoASCII(ink_profile, cmsInfoDescription, "en", "US", description.data(),
                               static_cast<cmsUInt32Number>(description.size())) > 0) {
      impl->description = description.data();
    }
    // 16-bit integers on the ink side: 0..65535 is 0..100 percent. One color at a time
    // needs no precalculated device link, so skipping it keeps startup cheap.
    const cmsUInt32Number flags = cmsFLAGS_NOCACHE | cmsFLAGS_NOOPTIMIZE |
                                  (black_point_compensation ? cmsFLAGS_BLACKPOINTCOMPENSATION : 0U);
    impl->to_cmyk = cmsCreateTransformTHR(impl->context, srgb_profile, TYPE_RGB_8, ink_profile, TYPE_CMYK_16,
                                          lcms_intent(intent), flags);
    impl->to_rgb = cmsCreateTransformTHR(impl->context, ink_profile, TYPE_CMYK_16, srgb_profile, TYPE_RGB_8,
                                         lcms_intent(intent), flags);
  }
  if (srgb_profile != nullptr) {
    cmsCloseProfile(srgb_profile);
  }
  if (ink_profile != nullptr) {
    cmsCloseProfile(ink_profile);
  }
  if (impl->to_cmyk == nullptr || impl->to_rgb == nullptr) {
    return nullptr;
  }
  impl->profile = std::move(profile_bytes);
  impl->intent = intent;
  impl->black_point_compensation = black_point_compensation;
  return std::shared_ptr<const CmykWorkingSpace>(new CmykWorkingSpace(std::move(impl)));
}

std::shared_ptr<const CmykWorkingSpace> CmykWorkingSpace::from_profile_file(
    const std::filesystem::path& path, RenderingIntent intent, bool black_point_compensation) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    return nullptr;
  }
  const auto size = input.tellg();
  if (size < 128 || size > 16 * 1024 * 1024) {
    return nullptr;
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
    return nullptr;
  }
  return from_icc_profile(std::move(bytes), intent, black_point_compensation);
}

std::shared_ptr<const CmykWorkingSpace> CmykWorkingSpace::formula() {
  static const std::shared_ptr<const CmykWorkingSpace> space(new CmykWorkingSpace(std::make_unique<Impl>()));
  return space;
}

bool CmykWorkingSpace::has_profile() const noexcept { return impl_->to_cmyk != nullptr; }
const std::string& CmykWorkingSpace::profile_description() const noexcept { return impl_->description; }
const std::vector<std::uint8_t>& CmykWorkingSpace::profile_bytes() const noexcept { return impl_->profile; }
RenderingIntent CmykWorkingSpace::intent() const noexcept { return impl_->intent; }
bool CmykWorkingSpace::black_point_compensation() const noexcept { return impl_->black_point_compensation; }

CmykPercent CmykWorkingSpace::cmyk_from_rgb(RgbColor color) const {
  if (impl_->to_cmyk != nullptr) {
    const std::array<std::uint8_t, 3> rgb{color.red, color.green, color.blue};
    std::array<std::uint16_t, 4> ink{};
    cmsDoTransform(impl_->to_cmyk, rgb.data(), ink.data(), 1);
    return CmykPercent{ink[0] * 100.0 / 65535.0, ink[1] * 100.0 / 65535.0, ink[2] * 100.0 / 65535.0,
                       ink[3] * 100.0 / 65535.0};
  }
  const int strongest = std::max({color.red, color.green, color.blue});
  if (strongest == 0) {
    return CmykPercent{0.0, 0.0, 0.0, 100.0};
  }
  const auto ink = [strongest](std::uint8_t channel) {
    return static_cast<double>(strongest - channel) * 100.0 / static_cast<double>(strongest);
  };
  return CmykPercent{ink(color.red), ink(color.green), ink(color.blue),
                     static_cast<double>(255 - strongest) * 100.0 / 255.0};
}

RgbColor CmykWorkingSpace::rgb_from_cmyk(CmykPercent cmyk) const {
  const std::array<double, 4> percent{clamped_percent(cmyk.cyan), clamped_percent(cmyk.magenta),
                                      clamped_percent(cmyk.yellow), clamped_percent(cmyk.black)};
  if (impl_->to_rgb != nullptr) {
    std::array<std::uint16_t, 4> ink{};
    for (std::size_t index = 0; index < ink.size(); ++index) {
      ink[index] = static_cast<std::uint16_t>(std::lround(percent[index] * 65535.0 / 100.0));
    }
    std::array<std::uint8_t, 3> rgb{};
    cmsDoTransform(impl_->to_rgb, ink.data(), rgb.data(), 1);
    return RgbColor{rgb[0], rgb[1], rgb[2]};
  }
  const auto channel = [&percent](std::size_t index) {
    return static_cast<std::uint8_t>(
        std::lround(255.0 * (100.0 - percent[index]) * (100.0 - percent[3]) / 10000.0));
  };
  return RgbColor{channel(0), channel(1), channel(2)};
}

struct CmykProofTransform::Impl {
  static constexpr int kGamutGrid = 33;

  cmsContext context{nullptr};
  cmsHTRANSFORM proof{nullptr};
  // kGamutGrid^3 flags, red varying slowest; empty when the gamut warning is off.
  std::vector<std::uint8_t> out_of_gamut;

  ~Impl() {
    if (proof != nullptr) {
      cmsDeleteTransform(proof);
    }
    if (context != nullptr) {
      cmsDeleteContext(context);
    }
  }

  [[nodiscard]] static std::size_t gamut_node(std::uint8_t value) noexcept {
    return (static_cast<std::size_t>(value) * (kGamutGrid - 1) + 127U) / 255U;
  }
  [[nodiscard]] bool marked(std::uint8_t red, std::uint8_t green, std::uint8_t blue) const noexcept {
    return out_of_gamut[(gamut_node(red) * kGamutGrid + gamut_node(green)) * kGamutGrid + gamut_node(blue)] != 0;
  }
};

CmykProofTransform::CmykProofTransform(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CmykProofTransform::~CmykProofTransform() = default;

std::shared_ptr<const CmykProofTransform> CmykProofTransform::create(const CmykWorkingSpace& space,
                                                                     bool simulate_colors,
                                                                     bool mark_out_of_gamut) {
  const auto& profile_bytes = space.profile_bytes();
  if (!space.has_profile() || profile_bytes.empty() || (!simulate_colors && !mark_out_of_gamut)) {
    return nullptr;
  }
  auto impl = std::make_unique<Impl>();
  impl->context = cmsCreateContext(nullptr, nullptr);
  if (impl->context == nullptr) {
    return nullptr;
  }
  cmsSetLogErrorHandlerTHR(impl->context, ignore_lcms_error);
  cmsHPROFILE ink_profile = cmsOpenProfileFromMemTHR(impl->context, profile_bytes.data(),
                                                     static_cast<cmsUInt32Number>(profile_bytes.size()));
  cmsHPROFILE srgb_profile = cmsCreate_sRGBProfileTHR(impl->context);
  bool usable = ink_profile != nullptr && srgb_profile != nullptr;
  const cmsUInt32Number black_point = space.black_point_compensation() ? cmsFLAGS_BLACKPOINTCOMPENSATION : 0U;
  if (usable && simulate_colors) {
    // sRGB to the profile by the working intent, then back to the screen relative
    // colorimetric with black point compensation: the print's colors, neither tinted
    // toward the paper white nor washed out to the ink's black (Photoshop's default proof,
    // Simulate Paper Color and Simulate Black Ink off). cmsCreateProofingTransform would
    // leave compensation off on the way back, which lifted the shadows of every color. The
    // profile appears twice, as Little CMS chains a device space: output, then input. The
    // precalculated device link makes whole rows cheap.
    std::array<cmsHPROFILE, 4> chain{srgb_profile, ink_profile, ink_profile, srgb_profile};
    std::array<cmsBool, 4> compensate{black_point != 0U, black_point != 0U, FALSE, TRUE};
    std::array<cmsUInt32Number, 4> intents{lcms_intent(space.intent()), lcms_intent(space.intent()),
                                           INTENT_RELATIVE_COLORIMETRIC, INTENT_RELATIVE_COLORIMETRIC};
    std::array<cmsFloat64Number, 4> adaptation{1.0, 1.0, 1.0, 1.0};
    impl->proof = cmsCreateExtendedTransform(impl->context, static_cast<cmsUInt32Number>(chain.size()), chain.data(),
                                             compensate.data(), intents.data(), adaptation.data(), nullptr, 0,
                                             TYPE_RGBA_8, TYPE_RGBA_8, cmsFLAGS_NOCACHE | cmsFLAGS_COPY_ALPHA);
    usable = impl->proof != nullptr;
  }
  if (usable && mark_out_of_gamut) {
    // Little CMS's own gamut test (cmsgmt.c GamutSampler): a color is in gamut when a
    // relative colorimetric round trip through the profile returns it within 5 dE. Its
    // cmsFLAGS_GAMUTCHECK compares against the color before black point compensation,
    // which marks pure black and every deep shadow, though the separation prints them as
    // the press's own black. So the target here is the color after the same black
    // scaling the separation applies (cmscnvrt.c ComputeBlackPointCompensation).
    cmsHPROFILE lab_profile = cmsCreateLab4ProfileTHR(impl->context, nullptr);
    cmsHPROFILE xyz_profile = cmsCreateXYZProfileTHR(impl->context);
    cmsHTRANSFORM to_xyz = nullptr;
    cmsHTRANSFORM forward = nullptr;
    cmsHTRANSFORM reverse = nullptr;
    if (lab_profile != nullptr && xyz_profile != nullptr) {
      to_xyz = cmsCreateTransformTHR(impl->context, srgb_profile, TYPE_RGB_8, xyz_profile, TYPE_XYZ_DBL,
                                     INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOCACHE);
      forward = cmsCreateTransformTHR(impl->context, lab_profile, TYPE_Lab_DBL, ink_profile, TYPE_CMYK_16,
                                      INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOCACHE);
      reverse = cmsCreateTransformTHR(impl->context, ink_profile, TYPE_CMYK_16, lab_profile, TYPE_Lab_DBL,
                                      INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOCACHE);
    }
    usable = to_xyz != nullptr && forward != nullptr && reverse != nullptr;
    if (usable) {
      const cmsCIEXYZ& white = *cmsD50_XYZ();
      cmsCIEXYZ black_in{0.0, 0.0, 0.0};
      cmsCIEXYZ black_out{0.0, 0.0, 0.0};
      if (black_point != 0U) {
        cmsDetectBlackPoint(&black_in, srgb_profile, INTENT_RELATIVE_COLORIMETRIC, 0);
        cmsDetectDestinationBlackPoint(&black_out, ink_profile, INTENT_RELATIVE_COLORIMETRIC, 0);
      }
      const auto compensate_axis = [](double value, double in, double out, double white_axis) {
        const double span = in - white_axis;
        return span == 0.0 ? value : value * (out - white_axis) / span - white_axis * (out - in) / span;
      };
      constexpr double kThreshold = 5.0;  // ERR_THRESHOLD for table-based profiles
      constexpr int grid = Impl::kGamutGrid;
      const auto node_value = [](int node) { return static_cast<std::uint8_t>((node * 255 + (grid - 1) / 2) / (grid - 1)); };
      const std::size_t node_count = static_cast<std::size_t>(grid) * grid * grid;
      std::vector<std::uint8_t> nodes(node_count * 3U);
      std::size_t out = 0;
      for (int red = 0; red < grid; ++red) {
        for (int green = 0; green < grid; ++green) {
          for (int blue = 0; blue < grid; ++blue) {
            nodes[out++] = node_value(red);
            nodes[out++] = node_value(green);
            nodes[out++] = node_value(blue);
          }
        }
      }
      const auto count = static_cast<cmsUInt32Number>(node_count);
      std::vector<cmsCIEXYZ> xyz(node_count);
      cmsDoTransform(to_xyz, nodes.data(), xyz.data(), count);
      std::vector<cmsCIELab> target(node_count);
      for (std::size_t node = 0; node < node_count; ++node) {
        auto scaled = xyz[node];
        scaled.X = compensate_axis(scaled.X, black_in.X, black_out.X, white.X);
        scaled.Y = compensate_axis(scaled.Y, black_in.Y, black_out.Y, white.Y);
        scaled.Z = compensate_axis(scaled.Z, black_in.Z, black_out.Z, white.Z);
        cmsXYZ2Lab(&white, &target[node], &scaled);
      }
      std::vector<cmsUInt16Number> ink(node_count * 4U);
      std::vector<cmsCIELab> once(node_count);
      std::vector<cmsCIELab> twice(node_count);
      cmsDoTransform(forward, target.data(), ink.data(), count);
      cmsDoTransform(reverse, ink.data(), once.data(), count);
      cmsDoTransform(forward, once.data(), ink.data(), count);
      cmsDoTransform(reverse, ink.data(), twice.data(), count);
      impl->out_of_gamut.resize(node_count);
      for (std::size_t node = 0; node < node_count; ++node) {
        const double direct = cmsDeltaE(&target[node], &once[node]);
        const double again = cmsDeltaE(&once[node], &twice[node]);
        bool outside = false;
        if (direct > kThreshold) {
          // A large second error too can be the table's own remapping; then only a large
          // ratio counts.
          outside = again < kThreshold || (again == 0.0 ? direct : direct / again) > kThreshold;
        }
        impl->out_of_gamut[node] = outside ? 1 : 0;
      }
    }
    for (auto* transform : {to_xyz, forward, reverse}) {
      if (transform != nullptr) {
        cmsDeleteTransform(transform);
      }
    }
    for (auto* profile : {lab_profile, xyz_profile}) {
      if (profile != nullptr) {
        cmsCloseProfile(profile);
      }
    }
  }
  if (srgb_profile != nullptr) {
    cmsCloseProfile(srgb_profile);
  }
  if (ink_profile != nullptr) {
    cmsCloseProfile(ink_profile);
  }
  if (!usable) {
    return nullptr;
  }
  return std::shared_ptr<const CmykProofTransform>(new CmykProofTransform(std::move(impl)));
}

bool CmykProofTransform::simulates_colors() const noexcept { return impl_->proof != nullptr; }
bool CmykProofTransform::marks_out_of_gamut() const noexcept { return !impl_->out_of_gamut.empty(); }

bool CmykProofTransform::out_of_gamut(RgbColor color) const noexcept {
  return marks_out_of_gamut() && impl_->marked(color.red, color.green, color.blue);
}

RgbColor CmykProofTransform::printed(RgbColor color) const {
  if (impl_->proof == nullptr) {
    return color;
  }
  std::array<std::uint8_t, 4> pixel{color.red, color.green, color.blue, 255};
  cmsDoTransform(impl_->proof, pixel.data(), pixel.data(), 1);
  return RgbColor{pixel[0], pixel[1], pixel[2]};
}

void CmykProofTransform::apply(std::uint8_t* rgba, std::size_t pixel_count) const {
  constexpr std::size_t kChunk = 1024;
  std::array<std::uint8_t, kChunk> marks{};
  const bool mark = marks_out_of_gamut();
  for (std::size_t done = 0; done < pixel_count; done += kChunk) {
    const auto count = std::min(kChunk, pixel_count - done);
    auto* pixels = rgba + done * 4U;
    if (mark) {
      // Judged on the document's colors, before the proof changes them.
      for (std::size_t index = 0; index < count; ++index) {
        const auto* pixel = pixels + index * 4U;
        marks[index] = impl_->marked(pixel[0], pixel[1], pixel[2]) ? 1 : 0;
      }
    }
    if (impl_->proof != nullptr) {
      cmsDoTransform(impl_->proof, pixels, pixels, static_cast<cmsUInt32Number>(count));
    }
    if (mark) {
      for (std::size_t index = 0; index < count; ++index) {
        if (marks[index] != 0) {
          auto* pixel = pixels + index * 4U;
          pixel[0] = kGamutWarningColor.red;
          pixel[1] = kGamutWarningColor.green;
          pixel[2] = kGamutWarningColor.blue;
        }
      }
    }
  }
}

std::vector<std::uint8_t> CmykWorkingSpace::separate_rgba8(std::span<const std::uint8_t> rgba,
                                                           SeparationOptions options) const {
  const auto pixel_count = rgba.size() / 4U;
  std::vector<std::uint8_t> inks(pixel_count * 4U);
  if (pixel_count == 0) {
    return inks;
  }
  cmsHTRANSFORM transform = nullptr;
  cmsContext context = nullptr;
  if (has_profile()) {
    context = cmsCreateContext(nullptr, nullptr);
    if (context != nullptr) {
      cmsSetLogErrorHandlerTHR(context, ignore_lcms_error);
      cmsHPROFILE ink_profile = cmsOpenProfileFromMemTHR(context, impl_->profile.data(),
                                                         static_cast<cmsUInt32Number>(impl_->profile.size()));
      cmsHPROFILE srgb_profile = cmsCreate_sRGBProfileTHR(context);
      if (ink_profile != nullptr && srgb_profile != nullptr) {
        transform = cmsCreateTransformTHR(
            context, srgb_profile, TYPE_RGBA_8, ink_profile, TYPE_CMYK_8, lcms_intent(impl_->intent),
            cmsFLAGS_NOCACHE | (impl_->black_point_compensation ? cmsFLAGS_BLACKPOINTCOMPENSATION : 0U));
      }
      if (srgb_profile != nullptr) {
        cmsCloseProfile(srgb_profile);
      }
      if (ink_profile != nullptr) {
        cmsCloseProfile(ink_profile);
      }
    }
  }
  if (transform != nullptr) {
    // Chunked only to keep the count inside cmsUInt32Number.
    constexpr std::size_t kChunk = std::size_t{1} << 20;
    for (std::size_t done = 0; done < pixel_count; done += kChunk) {
      const auto count = std::min(kChunk, pixel_count - done);
      cmsDoTransform(transform, rgba.data() + done * 4U, inks.data() + done * 4U, static_cast<cmsUInt32Number>(count));
    }
    cmsDeleteTransform(transform);
  } else {
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
      const auto ink = cmyk_from_rgb({rgba[pixel * 4U], rgba[pixel * 4U + 1U], rgba[pixel * 4U + 2U]});
      const auto to_byte = [](double percent) {
        return static_cast<std::uint8_t>(std::lround(clamped_percent(percent) * 255.0 / 100.0));
      };
      inks[pixel * 4U] = to_byte(ink.cyan);
      inks[pixel * 4U + 1U] = to_byte(ink.magenta);
      inks[pixel * 4U + 2U] = to_byte(ink.yellow);
      inks[pixel * 4U + 3U] = to_byte(ink.black);
    }
  }
  if (context != nullptr) {
    cmsDeleteContext(context);
  }
  if (options.black_ink_only) {
    // What black ink alone prints at each of the 256 ink levels, and for each sRGB green
    // level the ink whose green is nearest (the lower ink on a tie).
    std::array<RgbColor, 256> curve{};
    for (std::size_t level = 0; level < curve.size(); ++level) {
      curve[level] = rgb_from_cmyk({0.0, 0.0, 0.0, static_cast<double>(level) * 100.0 / 255.0});
    }
    std::array<std::uint8_t, 256> ink_for_green{};
    for (int green = 0; green < 256; ++green) {
      int best = 0;
      int best_distance = 256;
      for (int level = 0; level < 256; ++level) {
        const int distance = std::abs(static_cast<int>(curve[static_cast<std::size_t>(level)].green) - green);
        if (distance < best_distance) {
          best_distance = distance;
          best = level;
        }
      }
      ink_for_green[static_cast<std::size_t>(green)] = static_cast<std::uint8_t>(best);
    }
    const auto near = [](std::uint8_t value, std::uint8_t target) {
      return std::abs(static_cast<int>(value) - static_cast<int>(target)) <= kBlackInkOnlyTolerance;
    };
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
      const auto* color = rgba.data() + pixel * 4U;
      const auto level = ink_for_green[color[1]];
      const auto& target = curve[level];
      const bool neutral = color[0] == color[1] && color[1] == color[2];
      if (neutral || (near(color[0], target.red) && near(color[1], target.green) && near(color[2], target.blue))) {
        auto* ink = inks.data() + pixel * 4U;
        ink[0] = 0;
        ink[1] = 0;
        ink[2] = 0;
        ink[3] = level;
      }
    }
  }
  return inks;
}

namespace {

std::mutex& working_space_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::shared_ptr<const CmykWorkingSpace>& working_space_slot() {
  static std::shared_ptr<const CmykWorkingSpace> space = CmykWorkingSpace::formula();
  return space;
}

}  // namespace

std::shared_ptr<const CmykWorkingSpace> working_cmyk_space() {
  const std::scoped_lock lock(working_space_mutex());
  return working_space_slot();
}

void set_working_cmyk_space(std::shared_ptr<const CmykWorkingSpace> space) {
  if (space == nullptr) {
    space = CmykWorkingSpace::formula();
  }
  const std::scoped_lock lock(working_space_mutex());
  working_space_slot() = std::move(space);
}

namespace {

// Profiles of one color space in the operating system's and Adobe's profile folders, sorted
// by description then path.
std::vector<InstalledCmykProfile> installed_profiles(bool (*is_wanted)(const std::filesystem::path&),
                                                     cmsColorSpaceSignature space) {
  auto folders = adobe_profile_folders();
  for (auto& folder : system_profile_folders()) {
    folders.push_back(std::move(folder));
  }
  std::vector<InstalledCmykProfile> profiles;
  for (const auto& folder : folders) {
    std::error_code error;
    std::filesystem::recursive_directory_iterator entries(
        folder, std::filesystem::directory_options::skip_permission_denied, error);
    // Bounded: profile folders are shallow, and a stray link must not walk a whole disk.
    for (int visited = 0; !error && entries != std::filesystem::recursive_directory_iterator() && visited < 4096;
         entries.increment(error), ++visited) {
      if (entries.depth() >= 3) {
        entries.disable_recursion_pending();
      }
      const auto path = entries->path();
      std::error_code file_error;
      if (!has_profile_extension(path) || !entries->is_regular_file(file_error) || file_error ||
          !is_wanted(path)) {
        continue;
      }
      if (std::any_of(profiles.begin(), profiles.end(), [&path](const auto& known) { return known.path == path; })) {
        continue;  // one listed folder sits inside another
      }
      // Only the description is read: building a transform per profile took seconds with
      // Adobe's set installed, all of it while Preferences opened. A profile that turns
      // out unusable is refused when it is chosen.
      auto description = profile_file_description(path, space);
      if (!description.has_value()) {
        continue;
      }
      if (description->empty()) {
        description = path_to_utf8(path.stem());
      }
      profiles.push_back(InstalledCmykProfile{path, std::move(*description)});
    }
  }
  std::sort(profiles.begin(), profiles.end(), [](const auto& left, const auto& right) {
    return left.description != right.description ? left.description < right.description : left.path < right.path;
  });
  return profiles;
}

}  // namespace

std::vector<InstalledCmykProfile> installed_cmyk_profiles() {
  return installed_profiles(file_is_cmyk_profile, cmsSigCmykData);
}

std::vector<InstalledRgbProfile> installed_rgb_profiles() {
  return installed_profiles(file_is_rgb_profile, cmsSigRgbData);
}

int color_lookup_profile_grid(std::span<const std::uint8_t> profile_bytes) {
  const auto u32 = [&profile_bytes](std::size_t offset) -> std::uint32_t {
    if (offset + 4U > profile_bytes.size()) {
      return 0;
    }
    return (static_cast<std::uint32_t>(profile_bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(profile_bytes[offset + 1]) << 16U) |
           (static_cast<std::uint32_t>(profile_bytes[offset + 2]) << 8U) | profile_bytes[offset + 3];
  };
  const auto tags = u32(128);
  for (std::uint32_t index = 0; index < tags && index < 256U; ++index) {
    const std::size_t entry = 132U + 12U * index;
    if (u32(entry) != 0x41324230U) {  // 'A2B0'
      continue;
    }
    const std::size_t tag = u32(entry + 4U);
    if (u32(tag) != 0x6D414220U || tag + 32U > profile_bytes.size()) {  // 'mAB '
      return 0;
    }
    const std::size_t clut = u32(tag + 24U);
    if (clut == 0 || tag + clut + 3U > profile_bytes.size()) {
      return 0;
    }
    const int grid = profile_bytes[tag + clut];
    if (profile_bytes[tag + clut + 1U] != grid || profile_bytes[tag + clut + 2U] != grid) {
      return 0;
    }
    return grid >= 2 ? grid : 0;
  }
  return 0;
}

std::optional<std::vector<std::uint16_t>> sample_color_lookup_profile(std::span<const std::uint8_t> profile_bytes,
                                                                      int grid) {
  if (profile_bytes.empty() || profile_bytes.size() > 0xFFFFFFFFULL || grid < 2 || grid > 256) {
    return std::nullopt;
  }
  const auto context = cmsCreateContext(nullptr, nullptr);
  if (context == nullptr) {
    return std::nullopt;
  }
  cmsSetLogErrorHandlerTHR(context, ignore_lcms_error);
  std::optional<std::vector<std::uint16_t>> result;
  cmsHPROFILE profile = cmsOpenProfileFromMemTHR(context, profile_bytes.data(),
                                                 static_cast<cmsUInt32Number>(profile_bytes.size()));
  if (profile != nullptr) {
    constexpr cmsUInt32Number kFlags = cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOWHITEONWHITEFIXUP | cmsFLAGS_NOCACHE;
    cmsHTRANSFORM transform = nullptr;
    const auto device_class = cmsGetDeviceClass(profile);
    if (device_class == cmsSigLinkClass && cmsGetColorSpace(profile) == cmsSigRgbData &&
        cmsGetPCS(profile) == cmsSigRgbData) {
      transform = cmsCreateTransformTHR(context, profile, TYPE_RGB_16, nullptr, TYPE_RGB_16, INTENT_PERCEPTUAL, kFlags);
    } else if (device_class == cmsSigAbstractClass) {
      cmsHPROFILE srgb_in = cmsCreate_sRGBProfileTHR(context);
      cmsHPROFILE srgb_out = cmsCreate_sRGBProfileTHR(context);
      if (srgb_in != nullptr && srgb_out != nullptr) {
        std::array<cmsHPROFILE, 3> chain{srgb_in, profile, srgb_out};
        transform = cmsCreateMultiprofileTransformTHR(context, chain.data(), 3, TYPE_RGB_16, TYPE_RGB_16,
                                                      INTENT_RELATIVE_COLORIMETRIC, kFlags);
      }
      if (srgb_in != nullptr) {
        cmsCloseProfile(srgb_in);
      }
      if (srgb_out != nullptr) {
        cmsCloseProfile(srgb_out);
      }
    }
    if (transform != nullptr) {
      const auto points = static_cast<std::size_t>(grid);
      std::vector<std::uint16_t> input(points * points * points * 3U);
      std::size_t cursor = 0;
      for (std::size_t b = 0; b < points; ++b) {
        for (std::size_t g = 0; g < points; ++g) {
          for (std::size_t r = 0; r < points; ++r) {
            for (const auto index : {r, g, b}) {
              input[cursor++] = static_cast<std::uint16_t>((index * 65535U + (points - 1U) / 2U) / (points - 1U));
            }
          }
        }
      }
      std::vector<std::uint16_t> output(input.size());
      cmsDoTransform(transform, input.data(), output.data(), static_cast<cmsUInt32Number>(points * points * points));
      cmsDeleteTransform(transform);
      result = std::move(output);
    }
    cmsCloseProfile(profile);
  }
  cmsDeleteContext(context);
  return result;
}

std::filesystem::path automatic_cmyk_profile_path() {
  std::vector<std::filesystem::path> candidates;
  for (const auto& folder : adobe_profile_folders()) {
#if defined(_WIN32) || defined(__APPLE__)
    candidates.push_back(folder / "Recommended" / "USWebCoatedSWOP.icc");
#else
    candidates.push_back(folder / "CMYK" / "USWebCoatedSWOP.icc");
#endif
  }
  for (const auto& folder : system_profile_folders()) {
    candidates.push_back(folder / "USWebCoatedSWOP.icc");
  }
  for (const auto& candidate : candidates) {
    if (file_is_cmyk_profile(candidate)) {
      return candidate;
    }
  }
  return {};
}

}  // namespace patchy
