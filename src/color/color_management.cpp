#include "color/color_management.hpp"

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

}  // namespace

struct CmykToRgbTransform::Impl {
  cmsContext context{nullptr};
  cmsHTRANSFORM transform{nullptr};
  std::string description;

  ~Impl() {
    if (transform != nullptr) {
      cmsDeleteTransform(transform);
    }
    if (context != nullptr) {
      cmsDeleteContext(context);
    }
  }
};

std::optional<CmykToRgbTransform> CmykToRgbTransform::from_icc_profile(
    std::span<const std::uint8_t> profile_bytes) {
  if (profile_bytes.empty() || profile_bytes.size() > 0xFFFFFFFFULL) {
    return std::nullopt;
  }
  auto impl = std::make_unique<Impl>();
  impl->context = cmsCreateContext(nullptr, nullptr);
  if (impl->context == nullptr) {
    return std::nullopt;
  }
  cmsSetLogErrorHandlerTHR(impl->context, ignore_lcms_error);

  cmsHPROFILE cmyk_profile = cmsOpenProfileFromMemTHR(
      impl->context, profile_bytes.data(), static_cast<cmsUInt32Number>(profile_bytes.size()));
  if (cmyk_profile == nullptr) {
    return std::nullopt;
  }
  if (cmsGetColorSpace(cmyk_profile) != cmsSigCmykData) {
    cmsCloseProfile(cmyk_profile);
    return std::nullopt;
  }

  std::array<char, 256> description{};
  if (cmsGetProfileInfoASCII(cmyk_profile, cmsInfoDescription, "en", "US", description.data(),
                             static_cast<cmsUInt32Number>(description.size())) > 0) {
    impl->description = description.data();
  }

  cmsHPROFILE srgb_profile = cmsCreate_sRGBProfileTHR(impl->context);
  if (srgb_profile != nullptr) {
    impl->transform = cmsCreateTransformTHR(impl->context, cmyk_profile, TYPE_CMYK_8_REV,
                                            srgb_profile, TYPE_RGB_8, INTENT_RELATIVE_COLORIMETRIC,
                                            cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOCACHE);
    cmsCloseProfile(srgb_profile);
  }
  cmsCloseProfile(cmyk_profile);
  if (impl->transform == nullptr) {
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

void ColorManager::assign_icc_profile(Document& document, std::vector<std::uint8_t> icc_profile) const {
  document.color_state().embedded_icc_profile = std::move(icc_profile);
}

PixelBuffer ColorManager::preview_rgb8(const Document& /*document*/, const PixelBuffer& source,
                                       const ColorTransformSpec& /*spec*/) const {
  if (source.format() != PixelFormat::rgb8()) {
    throw std::invalid_argument("Color preview placeholder currently accepts RGB8 buffers only");
  }
  return source;
}

}  // namespace patchy
