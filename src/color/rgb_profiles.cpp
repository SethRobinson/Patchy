#include "color/rgb_profiles.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <utility>

// lcms2.h still spells `register` for pre-C++17 compilers; the opt-out keeps the C++ TU
// warning-clean.
#define CMS_NO_REGISTER_KEYWORD 1
#include "lcms2.h"

namespace patchy {
namespace {

void ignore_lcms_error(cmsContext /*context*/, cmsUInt32Number /*code*/, const char* /*text*/) {}

// One Little CMS context with its error output silenced; profiles opened through it close
// with it.
class LcmsContext {
public:
  LcmsContext() : context_(cmsCreateContext(nullptr, nullptr)) {
    if (context_ != nullptr) {
      cmsSetLogErrorHandlerTHR(context_, ignore_lcms_error);
    }
  }
  LcmsContext(const LcmsContext&) = delete;
  LcmsContext& operator=(const LcmsContext&) = delete;
  ~LcmsContext() {
    if (context_ != nullptr) {
      cmsDeleteContext(context_);
    }
  }
  [[nodiscard]] cmsContext get() const noexcept { return context_; }

private:
  cmsContext context_;
};

class Profile {
public:
  Profile() = default;
  explicit Profile(cmsHPROFILE handle) : handle_(handle) {}
  Profile(const Profile&) = delete;
  Profile& operator=(const Profile&) = delete;
  Profile(Profile&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
  Profile& operator=(Profile&& other) noexcept {
    std::swap(handle_, other.handle_);
    return *this;
  }
  ~Profile() {
    if (handle_ != nullptr) {
      cmsCloseProfile(handle_);
    }
  }
  [[nodiscard]] cmsHPROFILE get() const noexcept { return handle_; }
  explicit operator bool() const noexcept { return handle_ != nullptr; }

private:
  cmsHPROFILE handle_{nullptr};
};

// An RGB device or color space profile, or null. Empty bytes open as Little CMS's sRGB.
Profile open_rgb_profile(cmsContext context, std::span<const std::uint8_t> bytes) {
  if (bytes.empty()) {
    return Profile(cmsCreate_sRGBProfileTHR(context));
  }
  if (bytes.size() < 128 || bytes.size() > 0xFFFFFFFFULL) {
    return {};
  }
  Profile profile(cmsOpenProfileFromMemTHR(context, bytes.data(), static_cast<cmsUInt32Number>(bytes.size())));
  if (!profile || cmsGetColorSpace(profile.get()) != cmsSigRgbData) {
    return {};
  }
  const auto device_class = cmsGetDeviceClass(profile.get());
  if (device_class != cmsSigDisplayClass && device_class != cmsSigInputClass && device_class != cmsSigOutputClass &&
      device_class != cmsSigColorSpaceClass) {
    return {};
  }
  return profile;
}

std::string description_of(cmsHPROFILE profile) {
  std::array<char, 256> text{};
  if (cmsGetProfileInfoASCII(profile, cmsInfoDescription, "en", "US", text.data(),
                             static_cast<cmsUInt32Number>(text.size())) == 0) {
    return {};
  }
  std::string description(text.data());
  while (!description.empty() && std::isspace(static_cast<unsigned char>(description.back())) != 0) {
    description.pop_back();
  }
  return description;
}

std::string lowercase(std::string text) {
  for (auto& character : text) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return text;
}

using Matrix3 = std::array<double, 9>;  // row-major

// Columns are the red, green and blue colorants (PCS XYZ, D50-adapted). Nullopt for a
// table-based profile.
std::optional<Matrix3> colorant_matrix(cmsHPROFILE profile) {
  if (cmsIsMatrixShaper(profile) == FALSE) {
    return std::nullopt;
  }
  const auto* red = static_cast<const cmsCIEXYZ*>(cmsReadTag(profile, cmsSigRedColorantTag));
  const auto* green = static_cast<const cmsCIEXYZ*>(cmsReadTag(profile, cmsSigGreenColorantTag));
  const auto* blue = static_cast<const cmsCIEXYZ*>(cmsReadTag(profile, cmsSigBlueColorantTag));
  if (red == nullptr || green == nullptr || blue == nullptr) {
    return std::nullopt;
  }
  return Matrix3{red->X, green->X, blue->X, red->Y, green->Y, blue->Y, red->Z, green->Z, blue->Z};
}

std::optional<Matrix3> inverse(const Matrix3& m) {
  const double a = m[4] * m[8] - m[5] * m[7];
  const double b = m[5] * m[6] - m[3] * m[8];
  const double c = m[3] * m[7] - m[4] * m[6];
  const double determinant = m[0] * a + m[1] * b + m[2] * c;
  if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12) {
    return std::nullopt;
  }
  const double f = 1.0 / determinant;
  return Matrix3{a * f,
                 (m[2] * m[7] - m[1] * m[8]) * f,
                 (m[1] * m[5] - m[2] * m[4]) * f,
                 b * f,
                 (m[0] * m[8] - m[2] * m[6]) * f,
                 (m[2] * m[3] - m[0] * m[5]) * f,
                 c * f,
                 (m[1] * m[6] - m[0] * m[7]) * f,
                 (m[0] * m[4] - m[1] * m[3]) * f};
}

Matrix3 multiply(const Matrix3& left, const Matrix3& right) {
  Matrix3 result{};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      double sum = 0.0;
      for (int k = 0; k < 3; ++k) {
        sum += left[static_cast<std::size_t>(row * 3 + k)] * right[static_cast<std::size_t>(k * 3 + column)];
      }
      result[static_cast<std::size_t>(row * 3 + column)] = sum;
    }
  }
  return result;
}

double srgb_curve(double encoded) {
  return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
}

// Whether all three tone curves follow the sRGB curve to within `tolerance`.
bool has_srgb_tone_curves(cmsHPROFILE profile, double tolerance) {
  for (const auto tag : {cmsSigRedTRCTag, cmsSigGreenTRCTag, cmsSigBlueTRCTag}) {
    const auto* curve = static_cast<const cmsToneCurve*>(cmsReadTag(profile, tag));
    if (curve == nullptr) {
      return false;
    }
    for (int step = 0; step <= 32; ++step) {
      const double x = step / 32.0;
      if (std::abs(static_cast<double>(cmsEvalToneCurveFloat(curve, static_cast<cmsFloat32Number>(x))) -
                   srgb_curve(x)) > tolerance) {
        return false;
      }
    }
  }
  return true;
}

bool is_srgb(cmsContext context, cmsHPROFILE profile) {
  const auto name = lowercase(description_of(profile));
  const bool named_srgb = name.find("srgb") != std::string::npos && name.find("linear") == std::string::npos;
  const auto colorants = colorant_matrix(profile);
  if (!colorants.has_value()) {
    return named_srgb;
  }
  const Profile reference(cmsCreate_sRGBProfileTHR(context));
  const auto srgb_colorants = reference ? colorant_matrix(reference.get()) : std::nullopt;
  if (!srgb_colorants.has_value()) {
    return named_srgb;
  }
  for (std::size_t i = 0; i < colorants->size(); ++i) {
    // Profiles round their colorants to the s15Fixed16 grid and adapt them with slightly
    // different matrices; real other spaces differ by more than 0.01.
    if (std::abs((*colorants)[i] - (*srgb_colorants)[i]) > 0.003) {
      return false;
    }
  }
  return named_srgb || has_srgb_tone_curves(profile, 0.004);
}

cmsUInt32Number lcms_intent(RenderingIntent intent) noexcept {
  switch (intent) {
    case RenderingIntent::Perceptual:
      return INTENT_PERCEPTUAL;
    case RenderingIntent::RelativeColorimetric:
      return INTENT_RELATIVE_COLORIMETRIC;
    case RenderingIntent::Saturation:
      return INTENT_SATURATION;
    case RenderingIntent::AbsoluteColorimetric:
      return INTENT_ABSOLUTE_COLORIMETRIC;
  }
  return INTENT_RELATIVE_COLORIMETRIC;
}

}  // namespace

bool is_usable_rgb_profile(std::span<const std::uint8_t> profile_bytes) {
  if (profile_bytes.empty()) {
    return false;
  }
  const LcmsContext context;
  return context.get() != nullptr && static_cast<bool>(open_rgb_profile(context.get(), profile_bytes));
}

bool is_srgb_profile(std::span<const std::uint8_t> profile_bytes) {
  if (profile_bytes.empty()) {
    return false;
  }
  const LcmsContext context;
  if (context.get() == nullptr) {
    return false;
  }
  const auto profile = open_rgb_profile(context.get(), profile_bytes);
  return profile && is_srgb(context.get(), profile.get());
}

std::string rgb_profile_description(std::span<const std::uint8_t> profile_bytes) {
  if (profile_bytes.empty() || profile_bytes.size() > 0xFFFFFFFFULL) {
    return {};
  }
  const LcmsContext context;
  if (context.get() == nullptr) {
    return {};
  }
  const Profile profile(cmsOpenProfileFromMemTHR(context.get(), profile_bytes.data(),
                                                 static_cast<cmsUInt32Number>(profile_bytes.size())));
  return profile ? description_of(profile.get()) : std::string();
}

std::string document_rgb_profile_name(std::span<const std::uint8_t> embedded_profile) {
  if (embedded_profile.empty()) {
    return kSrgbProfileName;
  }
  auto name = rgb_profile_description(embedded_profile);
  return name.empty() ? std::string("Unnamed profile") : name;
}

std::vector<std::uint8_t> read_rgb_profile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    return {};
  }
  const auto size = input.tellg();
  if (size < 128 || size > 16 * 1024 * 1024) {
    return {};
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) ||
      !is_usable_rgb_profile(bytes)) {
    return {};
  }
  return bytes;
}

std::vector<std::uint8_t> create_rgb_matrix_profile(const RgbMatrixProfileSpec& spec) {
  const LcmsContext context;
  if (context.get() == nullptr) {
    return {};
  }
  const cmsCIExyY white{spec.white_x, spec.white_y, 1.0};
  const cmsCIExyYTRIPLE primaries{{spec.red_x, spec.red_y, 1.0},
                                  {spec.green_x, spec.green_y, 1.0},
                                  {spec.blue_x, spec.blue_y, 1.0}};
  cmsToneCurve* curve = nullptr;
  if (spec.gamma > 0.0) {
    curve = cmsBuildGamma(context.get(), spec.gamma);
  } else {
    const cmsFloat64Number parameters[5] = {2.4, 1.0 / 1.055, 0.055 / 1.055, 1.0 / 12.92, 0.04045};
    curve = cmsBuildParametricToneCurve(context.get(), 4, parameters);
  }
  if (curve == nullptr) {
    return {};
  }
  cmsToneCurve* curves[3] = {curve, curve, curve};
  const Profile profile(cmsCreateRGBProfileTHR(context.get(), &white, &primaries, curves));
  cmsFreeToneCurve(curve);
  if (!profile) {
    return {};
  }
  cmsSetProfileVersion(profile.get(), 2.1);
  if (cmsMLU* text = cmsMLUalloc(context.get(), 1); text != nullptr) {
    cmsMLUsetASCII(text, "en", "US", spec.description.c_str());
    cmsWriteTag(profile.get(), cmsSigProfileDescriptionTag, text);
    cmsMLUfree(text);
  }
  if (cmsMLU* text = cmsMLUalloc(context.get(), 1); text != nullptr) {
    cmsMLUsetASCII(text, "en", "US", "No copyright, use freely");
    cmsWriteTag(profile.get(), cmsSigCopyrightTag, text);
    cmsMLUfree(text);
  }
  cmsUInt32Number size = 0;
  if (cmsSaveProfileToMem(profile.get(), nullptr, &size) == FALSE || size == 0) {
    return {};
  }
  std::vector<std::uint8_t> bytes(size);
  if (cmsSaveProfileToMem(profile.get(), bytes.data(), &size) == FALSE) {
    return {};
  }
  bytes.resize(size);
  // Little CMS stamps the save time into the header; a fixed date keeps equal specs byte-equal.
  constexpr std::array<std::uint8_t, 12> kCreated{0x07, 0xEA, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0};  // 2026-01-01
  if (bytes.size() >= 36) {
    std::copy(kCreated.begin(), kCreated.end(), bytes.begin() + 24);
  }
  return bytes;
}

struct RgbProfileTransform::Impl {
  cmsContext context{nullptr};
  cmsHTRANSFORM rgb8{nullptr};
  cmsHTRANSFORM rgba8{nullptr};
  cmsHTRANSFORM rgb16{nullptr};
  cmsHTRANSFORM rgba16{nullptr};
  std::optional<Matrix3> linear;

  Impl() = default;
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  ~Impl() {
    for (auto* transform : {rgb8, rgba8, rgb16, rgba16}) {
      if (transform != nullptr) {
        cmsDeleteTransform(transform);
      }
    }
    if (context != nullptr) {
      cmsDeleteContext(context);
    }
  }
};

std::optional<RgbProfileTransform> RgbProfileTransform::create(std::span<const std::uint8_t> source,
                                                               std::span<const std::uint8_t> destination,
                                                               RenderingIntent intent,
                                                               bool black_point_compensation) {
  auto impl = std::make_unique<Impl>();
  impl->context = cmsCreateContext(nullptr, nullptr);
  if (impl->context == nullptr) {
    return std::nullopt;
  }
  cmsSetLogErrorHandlerTHR(impl->context, ignore_lcms_error);
  const auto from = open_rgb_profile(impl->context, source);
  const auto to = open_rgb_profile(impl->context, destination);
  if (!from || !to) {
    return std::nullopt;
  }
  const auto flags = (black_point_compensation ? cmsFLAGS_BLACKPOINTCOMPENSATION : 0U) | cmsFLAGS_NOCACHE;
  const auto lcms = lcms_intent(intent);
  const auto build = [&](cmsUInt32Number format, cmsUInt32Number extra_flags) {
    return cmsCreateTransformTHR(impl->context, from.get(), format, to.get(), format, lcms, flags | extra_flags);
  };
  impl->rgb8 = build(TYPE_RGB_8, 0);
  impl->rgba8 = build(TYPE_RGBA_8, cmsFLAGS_COPY_ALPHA);
  impl->rgb16 = build(TYPE_RGB_16, 0);
  impl->rgba16 = build(TYPE_RGBA_16, cmsFLAGS_COPY_ALPHA);
  if (impl->rgb8 == nullptr || impl->rgba8 == nullptr || impl->rgb16 == nullptr || impl->rgba16 == nullptr) {
    return std::nullopt;
  }
  // Linear light: the destination's inverse colorants after the source's. Relative and
  // perceptual intents of matrix profiles are this same mapping (the colorants are
  // already adapted to the D50 connection space), and black is 0 at both ends.
  if (const auto source_matrix = colorant_matrix(from.get()), destination_matrix = colorant_matrix(to.get());
      source_matrix.has_value() && destination_matrix.has_value()) {
    if (const auto inverted = inverse(*destination_matrix); inverted.has_value()) {
      impl->linear = multiply(*inverted, *source_matrix);
    }
  }
  return RgbProfileTransform(std::move(impl));
}

RgbProfileTransform::RgbProfileTransform(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RgbProfileTransform::RgbProfileTransform(RgbProfileTransform&&) noexcept = default;
RgbProfileTransform& RgbProfileTransform::operator=(RgbProfileTransform&&) noexcept = default;
RgbProfileTransform::~RgbProfileTransform() = default;

bool RgbProfileTransform::converts_linear() const noexcept {
  return impl_->linear.has_value();
}

bool RgbProfileTransform::convert(PixelBuffer& pixels) const {
  const auto format = pixels.format();
  if (pixels.empty() || (format.channels != 3 && format.channels != 4)) {
    return false;
  }
  const auto width = static_cast<std::size_t>(pixels.width());
  if (format.bit_depth == BitDepth::Float32) {
    if (!impl_->linear.has_value()) {
      return false;
    }
    const auto& m = *impl_->linear;
    const std::size_t step = format.channels;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      auto row = pixels.row(y);
      auto* samples = reinterpret_cast<float*>(row.data());
      for (std::size_t x = 0; x < width; ++x) {
        float* px = samples + x * step;
        const double r = px[0];
        const double g = px[1];
        const double b = px[2];
        px[0] = static_cast<float>(m[0] * r + m[1] * g + m[2] * b);
        px[1] = static_cast<float>(m[3] * r + m[4] * g + m[5] * b);
        px[2] = static_cast<float>(m[6] * r + m[7] * g + m[8] * b);
      }
    }
    return true;
  }
  const bool alpha = format.channels == 4;
  cmsHTRANSFORM transform = format.bit_depth == BitDepth::UInt16 ? (alpha ? impl_->rgba16 : impl_->rgb16)
                                                                 : (alpha ? impl_->rgba8 : impl_->rgb8);
  std::vector<std::uint8_t> converted(bytes_per_pixel(format) * width);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    auto row = pixels.row(y);
    cmsDoTransform(transform, row.data(), converted.data(), static_cast<cmsUInt32Number>(width));
    std::memcpy(row.data(), converted.data(), converted.size());
  }
  return true;
}

RgbColor RgbProfileTransform::convert_color(RgbColor color) const {
  const std::array<std::uint8_t, 3> input{color.red, color.green, color.blue};
  std::array<std::uint8_t, 3> output{};
  cmsDoTransform(impl_->rgb8, input.data(), output.data(), 1);
  return RgbColor{output[0], output[1], output[2]};
}

bool rgb_profile_needs_conversion(std::span<const std::uint8_t> embedded_profile) {
  return !embedded_profile.empty() && is_usable_rgb_profile(embedded_profile) && !is_srgb_profile(embedded_profile);
}

namespace {

// Image content: pixel layers and Smart Object previews. Text, shape and fill rasters
// re-render from their stored colors, which stay as they are.
bool layer_pixels_convert(const Layer& layer) {
  return (layer.kind() == LayerKind::Pixel && layer.vector_shape() == nullptr) ||
         layer.kind() == LayerKind::SmartObject;
}

bool layer_keeps_stored_colors(const Layer& layer) {
  return layer.kind() == LayerKind::Text || layer.kind() == LayerKind::Adjustment ||
         layer.kind() == LayerKind::Vector || layer.vector_shape() != nullptr ||
         layer.layer_style().effect_count() != 0;
}

void convert_layers(std::vector<Layer>& layers, const RgbProfileTransform& transform, bool& kept_stored_colors) {
  for (auto& layer : layers) {
    if (layer_keeps_stored_colors(std::as_const(layer))) {
      kept_stored_colors = true;
    }
    if (layer_pixels_convert(std::as_const(layer))) {
      if (const auto& source = std::as_const(layer).pixels(); !source.empty()) {
        auto pixels = source;
        if (transform.convert(pixels)) {
          // The mutable accessor bumps every revision; set_pixels would also move the layer.
          layer.pixels() = std::move(pixels);
        }
      }
    }
    if (!std::as_const(layer).children().empty()) {
      convert_layers(layer.children(), transform, kept_stored_colors);
    }
  }
}

}  // namespace

std::optional<RgbProfileConversion> convert_document_rgb_profile(Document& document,
                                                                 std::span<const std::uint8_t> destination_view,
                                                                 RenderingIntent intent,
                                                                 bool black_point_compensation) {
  // Copies: the destination may be the document's own profile.
  const std::vector<std::uint8_t> destination(destination_view.begin(), destination_view.end());
  const auto source = std::as_const(document).color_state().embedded_icc_profile;
  const bool source_srgb = source.empty() || is_srgb_profile(source);
  const bool destination_srgb = destination.empty() || is_srgb_profile(destination);
  if ((!source.empty() && !is_usable_rgb_profile(source)) ||
      (!destination.empty() && !is_usable_rgb_profile(destination))) {
    return std::nullopt;
  }
  RgbProfileConversion result;
  result.source_name = document_rgb_profile_name(source);
  result.destination_name =
      destination_srgb ? std::string(kSrgbProfileName) : document_rgb_profile_name(destination);
  const bool same_space = (source_srgb && destination_srgb) ||
                          (source.size() == destination.size() &&
                           std::equal(source.begin(), source.end(), destination.begin()));
  if (!same_space) {
    const auto transform = RgbProfileTransform::create(source, destination, intent, black_point_compensation);
    if (!transform.has_value() ||
        (document.color_state().bit_depth == BitDepth::Float32 && !transform->converts_linear())) {
      return std::nullopt;
    }
    convert_layers(document.layers(), *transform, result.kept_stored_colors);
    if (auto& palette = document.indexed_palette(); palette.has_value()) {
      for (auto& color : palette->colors) {
        color = transform->convert_color(color);
      }
    }
    if (auto& composite = document.metadata().psd_flat_composite; composite.has_value()) {
      if (!transform->convert(*composite)) {
        composite.reset();
      }
    }
  }
  if (destination_srgb) {
    document.color_state().embedded_icc_profile.clear();
  } else {
    document.color_state().embedded_icc_profile = destination;
  }
  return result;
}

}  // namespace patchy
