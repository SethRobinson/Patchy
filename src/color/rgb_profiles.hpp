#pragma once

// RGB color profiles (docs/rgb-profiles.md). Patchy edits and shows every RGB document as
// sRGB. A file whose pixels are in another RGB profile (Display P3, Adobe RGB, ProPhoto)
// converts to sRGB when it opens, and Edit > Convert to Profile converts a document's
// pixels from its profile to another one.

#include "color/color_management.hpp"
#include "core/document.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace patchy {

// The name an untagged RGB document's profile shows under: the profile Patchy assumes.
inline constexpr const char* kSrgbProfileName = "sRGB IEC61966-2.1";

// Whether Little CMS opens the bytes as an RGB device or color space profile.
[[nodiscard]] bool is_usable_rgb_profile(std::span<const std::uint8_t> profile_bytes);
// Whether the profile describes sRGB, whatever its bytes: a matrix profile by its colorants
// and tone curves (any sRGB variant, v2 or v4, table or parametric curves), a table-based
// one by a description naming sRGB. False for empty or unusable bytes.
[[nodiscard]] bool is_srgb_profile(std::span<const std::uint8_t> profile_bytes);
// The profile's description tag; empty when the bytes cannot be read or carry none.
[[nodiscard]] std::string rgb_profile_description(std::span<const std::uint8_t> profile_bytes);
// The name to show for a document's embedded profile: its description, kSrgbProfileName
// when there is none, the description or "Unnamed profile" for a profile without a name.
[[nodiscard]] std::string document_rgb_profile_name(std::span<const std::uint8_t> embedded_profile);
// Reads an RGB profile file (for Convert to Profile). Empty when the file is missing,
// unreadable, larger than 16 MiB or not a usable RGB profile. The path keeps its native
// encoding.
[[nodiscard]] std::vector<std::uint8_t> read_rgb_profile(const std::filesystem::path& path);

// A matrix/shaper RGB profile from chromaticities (CIE xy of white and the three primaries)
// and one tone curve for all channels: `gamma` > 0 is a pure power curve, 0 the sRGB curve.
// Equal specs give equal bytes. For tests and test fixtures; Patchy ships no RGB profile data.
struct RgbMatrixProfileSpec {
  std::string description;
  double white_x{0.3127};
  double white_y{0.3290};
  double red_x{0.64};
  double red_y{0.33};
  double green_x{0.30};
  double green_y{0.60};
  double blue_x{0.15};
  double blue_y{0.06};
  double gamma{0.0};
};
[[nodiscard]] std::vector<std::uint8_t> create_rgb_matrix_profile(const RgbMatrixProfileSpec& spec);

// Converts RGB pixels from one profile to another. Empty profile bytes mean sRGB. 8 and
// 16-bit buffers convert through Little CMS with the chosen intent and black point
// compensation (Convert to Profile's choices; opening a file uses relative colorimetric
// with compensation, Photoshop's defaults). 32-bit buffers hold linear light, so they
// convert through the profiles' colorant matrices without clipping; that needs matrix
// profiles on both ends. Built without the pixel cache, so an instance is thread-safe.
class RgbProfileTransform {
public:
  // Nullopt when either profile is not a usable RGB profile.
  [[nodiscard]] static std::optional<RgbProfileTransform> create(std::span<const std::uint8_t> source,
                                                                 std::span<const std::uint8_t> destination,
                                                                 RenderingIntent intent,
                                                                 bool black_point_compensation);

  RgbProfileTransform(RgbProfileTransform&&) noexcept;
  RgbProfileTransform& operator=(RgbProfileTransform&&) noexcept;
  RgbProfileTransform(const RgbProfileTransform&) = delete;
  RgbProfileTransform& operator=(const RgbProfileTransform&) = delete;
  ~RgbProfileTransform();

  // Whether 32-bit (linear) buffers can convert: both profiles are matrix profiles.
  [[nodiscard]] bool converts_linear() const noexcept;
  // A 3 or 4-channel color buffer in place, at any depth; alpha is left alone. Returns
  // false (buffer untouched) for other buffers, and for 32-bit ones without
  // converts_linear().
  bool convert(PixelBuffer& pixels) const;
  [[nodiscard]] RgbColor convert_color(RgbColor color) const;

private:
  struct Impl;
  explicit RgbProfileTransform(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

// What convert_document_rgb_profile did.
struct RgbProfileConversion {
  std::string source_name;
  std::string destination_name;
  // The document holds text, shape, fill or adjustment layers or layer effects, whose
  // stored colors keep their values (docs/rgb-profiles.md, "What converts").
  bool kept_stored_colors{false};
};

// Whether a document's embedded RGB profile is one an open converts to sRGB: usable, and
// not sRGB.
[[nodiscard]] bool rgb_profile_needs_conversion(std::span<const std::uint8_t> embedded_profile);

// Converts the document's pixels from its embedded profile (sRGB when it has none) to
// `destination` (empty = sRGB) and makes `destination` the embedded profile, none for
// sRGB. Pixel layers, Smart Object previews, the imported palette and the retained flat
// composite convert; masks and channels are coverage and stay. Nullopt, document
// untouched, when a profile is unusable or a 32-bit document has a table-based one.
[[nodiscard]] std::optional<RgbProfileConversion> convert_document_rgb_profile(
    Document& document, std::span<const std::uint8_t> destination, RenderingIntent intent,
    bool black_point_compensation);

}  // namespace patchy
