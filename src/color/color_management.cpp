#include "color/color_management.hpp"

#include "support/translate_noop.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

// lcms2.h still spells `register` for pre-C++17 compilers; the opt-out keeps the C++ TU
// warning-clean.
#define CMS_NO_REGISTER_KEYWORD 1
#include "lcms2.h"

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
                              INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOCACHE)
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

}  // namespace patchy
