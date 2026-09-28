#include "color/color_management.hpp"

#include "support/translate_noop.hpp"

#include <array>
#include <stdexcept>
#include <utility>

// lcms2.h still spells `register` for pre-C++17 compilers; the opt-out keeps the C++ TU
// warning-clean.
#define CMS_NO_REGISTER_KEYWORD 1
#include "lcms2.h"

namespace patchy {

namespace {

// lcms reports recoverable errors through a callback; a bad profile must fall back to the
// naive conversion silently rather than spam stderr.
void ignore_lcms_error(cmsContext /*context*/, cmsUInt32Number /*code*/, const char* /*text*/) {}

// State shared by the embedded-profile-to-sRGB transforms (CMYK and gray).
struct IccToSrgbState {
  cmsContext context{nullptr};
  cmsHTRANSFORM transform{nullptr};
  std::string description;

  ~IccToSrgbState() {
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
    cmsCloseProfile(srgb_profile);
  }
  cmsCloseProfile(source_profile);
  return state.transform != nullptr;
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

RgbColor GrayToRgbTransform::convert_single(std::uint8_t gray) const {
  std::array<std::uint8_t, 3> rgb{};
  convert(&gray, rgb.data(), 1);
  return RgbColor{rgb[0], rgb[1], rgb[2]};
}

const std::string& GrayToRgbTransform::profile_description() const {
  return impl_->description;
}

struct LabToRgbTransform::Impl {
  cmsContext context{nullptr};
  cmsHTRANSFORM transform{nullptr};

  ~Impl() {
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
  }
  if (lab_profile != nullptr) {
    cmsCloseProfile(lab_profile);
  }
  if (srgb_profile != nullptr) {
    cmsCloseProfile(srgb_profile);
  }
  if (impl->transform == nullptr) {
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

}  // namespace patchy
