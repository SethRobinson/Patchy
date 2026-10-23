#pragma once

#include "core/document.hpp"
#include "core/ink_space.hpp"
#include "core/layer.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy {

// Reads an installed profile without shipping third-party profile data. Invalid,
// oversized and non-CMYK files are rejected. Paths keep their native encoding.
[[nodiscard]] std::vector<std::uint8_t> read_cmyk_profile(const std::filesystem::path& path);
// The conventional untagged-CMYK working profile, only when already installed.
// Empty on systems without it; never calls Photoshop or downloads a profile.
[[nodiscard]] const std::vector<std::uint8_t>& default_cmyk_profile();

struct ColorTransformSpec {
  std::string source_profile_name{"document"};
  std::string destination_profile_name{"display"};
  std::string rendering_intent{"relative-colorimetric"};
  bool black_point_compensation{true};
};

// Converts CMYK color data to sRGB through an ICC profile (vendored Little CMS core).
// Inputs use the PSD channel convention: INVERTED ink values, 255 = 0% ink. The transform
// uses relative colorimetric intent with black point compensation (Photoshop's conversion
// defaults) and is built without the one-pixel cache, so a constructed instance is safe to
// share across threads and its output is independent of call chunking.
class CmykToRgbTransform {
public:
  // Returns nullopt when the bytes are not a usable CMYK ICC profile.
  [[nodiscard]] static std::optional<CmykToRgbTransform> from_icc_profile(
      std::span<const std::uint8_t> profile_bytes);

  CmykToRgbTransform(CmykToRgbTransform&&) noexcept;
  CmykToRgbTransform& operator=(CmykToRgbTransform&&) noexcept;
  CmykToRgbTransform(const CmykToRgbTransform&) = delete;
  CmykToRgbTransform& operator=(const CmykToRgbTransform&) = delete;
  ~CmykToRgbTransform();

  // Interleaved inverted-CMYK pixels (4 bytes each) to packed sRGB (3 bytes each).
  void convert(const std::uint8_t* cmyk_inverted, std::uint8_t* rgb_out,
               std::size_t pixel_count) const;
  // Full-range native-endian u16 samples on both sides, without an 8-bit intermediate.
  void convert16(const std::uint16_t* cmyk_inverted, std::uint16_t* rgb_out,
                 std::size_t pixel_count) const;
  [[nodiscard]] RgbColor convert_single(std::uint8_t cyan_inverted, std::uint8_t magenta_inverted,
                                        std::uint8_t yellow_inverted,
                                        std::uint8_t black_inverted) const;
  [[nodiscard]] const std::string& profile_description() const;

private:
  struct Impl;
  explicit CmykToRgbTransform(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

// The reverse direction for 8-bit samples: packed sRGB (3 bytes each) to inverted CMYK
// (4 bytes each, 255 = no ink), relative colorimetric with black point compensation. Its
// output equals rgb_to_native_color_space(rgb8, ColorMode::CMYK, profile); building it once
// and reusing it avoids that function's per-call transform build, which costs tens of
// milliseconds for a table-based press profile. Cache-free, so shareable across threads.
class RgbToCmykTransform {
public:
  // Returns nullopt when the bytes are not a usable CMYK ICC profile.
  [[nodiscard]] static std::optional<RgbToCmykTransform> from_icc_profile(
      std::span<const std::uint8_t> profile_bytes);

  RgbToCmykTransform(RgbToCmykTransform&&) noexcept;
  RgbToCmykTransform& operator=(RgbToCmykTransform&&) noexcept;
  RgbToCmykTransform(const RgbToCmykTransform&) = delete;
  RgbToCmykTransform& operator=(const RgbToCmykTransform&) = delete;
  ~RgbToCmykTransform();

  void convert(const std::uint8_t* rgb, std::uint8_t* cmyk_inverted_out, std::size_t pixel_count) const;

private:
  struct Impl;
  explicit RgbToCmykTransform(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

// Samples a CMYK ICC profile into the two tables of an InkSpace (core/ink_space.hpp):
// sRGB to the profile's inks and back, with the same intent and black point compensation
// as CmykToRgbTransform. `id` names the space in layer metadata. Returns null when the
// bytes are not a usable CMYK profile or it has no sRGB-to-ink direction.
[[nodiscard]] std::shared_ptr<const InkSpace> build_cmyk_ink_space(std::span<const std::uint8_t> profile_bytes,
                                                                   std::string id);

// The one-channel form for a grayscale document: `profile_bytes` is its gray ICC profile.
// Returns null when the profile is not usable.
[[nodiscard]] std::shared_ptr<const InkSpace> build_gray_ink_space(std::span<const std::uint8_t> profile_bytes,
                                                                   std::string id);

// Converts single-channel gray data to sRGB through an ICC gray profile (Dot Gain 20%,
// Gray Gamma 2.2, sGray, ...). Inputs use the PSD channel convention: 0 = black,
// 255 = white. Same intent, black point compensation and cache-free construction as the
// CMYK transform, so instances are thread-safe and output is independent of chunking.
class GrayToRgbTransform {
public:
  // Returns nullopt when the bytes are not a usable gray ICC profile.
  [[nodiscard]] static std::optional<GrayToRgbTransform> from_icc_profile(
      std::span<const std::uint8_t> profile_bytes);

  GrayToRgbTransform(GrayToRgbTransform&&) noexcept;
  GrayToRgbTransform& operator=(GrayToRgbTransform&&) noexcept;
  GrayToRgbTransform(const GrayToRgbTransform&) = delete;
  GrayToRgbTransform& operator=(const GrayToRgbTransform&) = delete;
  ~GrayToRgbTransform();

  // Gray pixels (1 byte each) to packed sRGB (3 bytes each).
  void convert(const std::uint8_t* gray, std::uint8_t* rgb_out, std::size_t pixel_count) const;
  void convert16(const std::uint16_t* gray, std::uint16_t* rgb_out, std::size_t pixel_count) const;
  [[nodiscard]] RgbColor convert_single(std::uint8_t gray) const;
  [[nodiscard]] const std::string& profile_description() const;

private:
  struct Impl;
  explicit GrayToRgbTransform(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

// Converts CIELAB (D50) pixels to sRGB through lcms2's built-in profiles. Input is the
// ICC v4 Lab16 PCS encoding (L: 0..65535 = 0..100; a/b: 0..65535 with 0x8080 = 0, i.e.
// value/257 - 128) - the encoding Affinity .af LABA16 documents store on the wire, fed
// to lcms as TYPE_Lab_16 triples. Relative colorimetric + black point compensation like
// the CMYK transform; built without the one-pixel cache so instances are thread-safe.
class LabToRgbTransform {
public:
  // Returns nullopt only when lcms cannot build the built-in profiles (out of memory).
  [[nodiscard]] static std::optional<LabToRgbTransform> create();

  LabToRgbTransform(LabToRgbTransform&&) noexcept;
  LabToRgbTransform& operator=(LabToRgbTransform&&) noexcept;
  LabToRgbTransform(const LabToRgbTransform&) = delete;
  LabToRgbTransform& operator=(const LabToRgbTransform&) = delete;
  ~LabToRgbTransform();

  // Interleaved ICC-encoded L,a,b u16 triples to packed sRGB (3 bytes each).
  void convert(const std::uint16_t* lab_encoded, std::uint8_t* rgb_out,
               std::size_t pixel_count) const;
  void convert16(const std::uint16_t* lab_encoded, std::uint16_t* rgb_out,
                 std::size_t pixel_count) const;

private:
  struct Impl;
  explicit LabToRgbTransform(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

class ColorManager {
public:
  void assign_icc_profile(Document& document, std::vector<std::uint8_t> icc_profile) const;
  [[nodiscard]] PixelBuffer preview_rgb8(const Document& document, const PixelBuffer& source,
                                         const ColorTransformSpec& spec = {}) const;
};

// Convert RGB8/RGB16 to an imported document's color space for the PSD merged
// preview and regenerated Smart Object previews/caches. Untouched layer samples
// are preserved separately. Nullopt means the profile cannot be used.
[[nodiscard]] std::optional<PixelBuffer> rgb_to_native_color_space(
    const PixelBuffer& rgb, ColorMode mode, std::span<const std::uint8_t> profile);

// How out-of-gamut colors map into a profile. The tokens are persisted in settings and
// used by the scripting API, so they never change.
enum class RenderingIntent { Perceptual, RelativeColorimetric, Saturation, AbsoluteColorimetric };
[[nodiscard]] std::string_view rendering_intent_token(RenderingIntent intent) noexcept;
[[nodiscard]] std::optional<RenderingIntent> rendering_intent_from_token(std::string_view token) noexcept;

// rgb_to_native_color_space with a chosen intent and black point compensation (the form
// above is relative colorimetric with compensation). A CMYK Proof PSD save separates its
// layers and composite through the working space's choices (docs/cmyk.md).
[[nodiscard]] std::optional<PixelBuffer> rgb_to_native_color_space(
    const PixelBuffer& rgb, ColorMode mode, std::span<const std::uint8_t> profile, RenderingIntent intent,
    bool black_point_compensation);

// Choices for CmykWorkingSpace::separate_rgba8 beyond the profile's own separation.
struct SeparationOptions {
  // Black and grays print with black ink alone (docs/cmyk.md): neutral pixels, and pixels
  // within kBlackInkOnlyTolerance of the color black ink alone prints, separate to
  // 0/0/0/K. Everything else goes through the profile. Off, the profile decides, which
  // turns black text into four-ink rich black.
  bool black_ink_only{false};
};
// Per channel, in 8-bit sRGB steps. Wide enough for antialiased black text over white
// (blends between black ink and paper sit a little off the black-ink curve), narrow
// enough that a tinted gray keeps its color.
inline constexpr int kBlackInkOnlyTolerance = 3;

// Ink amounts as a print shop writes them: 0 = no ink, 100 = full ink.
struct CmykPercent {
  double cyan{0.0};
  double magenta{0.0};
  double yellow{0.0};
  double black{0.0};

  friend bool operator==(const CmykPercent&, const CmykPercent&) = default;
};

// The CMYK space that numbers typed or shown in the interface refer to: the color
// picker's C/M/Y/K fields, the Info panel readout, CMYK swatches in palette files and the
// script conversions. Patchy still edits in sRGB (docs/cmyk.md); this only converts single
// colors. With a profile the conversion runs through Little CMS; without one it is the
// profile-free formula (black = the ink common to all three, the rest scaled), which is
// a rough guide only.
// Instances are immutable and safe to share across threads.
class CmykWorkingSpace {
public:
  // Null when the bytes are not a usable CMYK profile with both directions.
  [[nodiscard]] static std::shared_ptr<const CmykWorkingSpace> from_icc_profile(
      std::vector<std::uint8_t> profile_bytes, RenderingIntent intent, bool black_point_compensation);
  // The same from a profile file; null too when the file is missing, unreadable or
  // larger than 16 MiB. The path keeps its native encoding.
  [[nodiscard]] static std::shared_ptr<const CmykWorkingSpace> from_profile_file(
      const std::filesystem::path& path, RenderingIntent intent, bool black_point_compensation);
  // The profile-free formula. Never null.
  [[nodiscard]] static std::shared_ptr<const CmykWorkingSpace> formula();

  CmykWorkingSpace(const CmykWorkingSpace&) = delete;
  CmykWorkingSpace& operator=(const CmykWorkingSpace&) = delete;
  ~CmykWorkingSpace();

  [[nodiscard]] bool has_profile() const noexcept;
  // The profile's own description tag; empty for the formula.
  [[nodiscard]] const std::string& profile_description() const noexcept;
  [[nodiscard]] const std::vector<std::uint8_t>& profile_bytes() const noexcept;
  [[nodiscard]] RenderingIntent intent() const noexcept;
  [[nodiscard]] bool black_point_compensation() const noexcept;

  [[nodiscard]] CmykPercent cmyk_from_rgb(RgbColor color) const;
  // Values outside 0..100 are clamped.
  [[nodiscard]] RgbColor rgb_from_cmyk(CmykPercent cmyk) const;
  // Separates whole rows for CMYK export: straight-alpha RGBA8 in (alpha ignored), four
  // 8-bit inks per pixel out, 255 = full ink (Qt's Format_CMYK8888 and a PDF DeviceCMYK
  // sample). With a profile this builds one optimized transform per call, so pass large
  // spans; without one it is the formula.
  [[nodiscard]] std::vector<std::uint8_t> separate_rgba8(std::span<const std::uint8_t> rgba,
                                                       SeparationOptions options = {}) const;

private:
  struct Impl;
  explicit CmykWorkingSpace(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

// The process-wide working space. Never null: the formula until the application installs
// the user's choice at startup. Replacing it is thread-safe; holders of the old pointer
// keep a valid space.
[[nodiscard]] std::shared_ptr<const CmykWorkingSpace> working_cmyk_space();
void set_working_cmyk_space(std::shared_ptr<const CmykWorkingSpace> space);

// The fill for out-of-gamut pixels under the gamut warning: a flat mid gray, the
// convention print tools use. It depicts a state of the artwork, not interface chrome,
// so it does not follow the color scheme.
inline constexpr RgbColor kGamutWarningColor{128, 128, 128};

// A display-only view of sRGB pixels as a CMYK working space would print them
// (docs/cmyk.md): the soft proof converts each color to the profile and back for the
// screen, and the gamut warning replaces colors the profile cannot hold with
// kGamutWarningColor. Either part can be off. Built once per setting change (it samples
// the profile into tables), then applied to whole rows; immutable and thread-safe.
class CmykProofTransform {
public:
  // Null when the space has no profile, or neither part is requested.
  [[nodiscard]] static std::shared_ptr<const CmykProofTransform> create(const CmykWorkingSpace& space,
                                                                        bool simulate_colors,
                                                                        bool mark_out_of_gamut);

  CmykProofTransform(const CmykProofTransform&) = delete;
  CmykProofTransform& operator=(const CmykProofTransform&) = delete;
  ~CmykProofTransform();

  [[nodiscard]] bool simulates_colors() const noexcept;
  [[nodiscard]] bool marks_out_of_gamut() const noexcept;
  // Whether the gamut warning marks this color; false when that part is off. The test is
  // Little CMS's own (a round trip through the profile, or its gamut tag), sampled on a
  // 33-step grid per channel, so the edge of the gamut is approximate by design.
  [[nodiscard]] bool out_of_gamut(RgbColor color) const noexcept;
  // In place over straight-alpha RGBA8 pixels; alpha is left alone.
  void apply(std::uint8_t* rgba, std::size_t pixel_count) const;
  // How `color` prints: the soft proof alone, without the gamut warning's gray. The color
  // itself when this transform does not simulate colors.
  [[nodiscard]] RgbColor printed(RgbColor color) const;

private:
  struct Impl;
  explicit CmykProofTransform(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

// Profiles found in the operating system's and Adobe's profile folders, sorted by
// description then path. Nothing is read beyond each file's header and description.
struct InstalledProfile {
  std::filesystem::path path;
  std::string description;
};
using InstalledCmykProfile = InstalledProfile;
using InstalledRgbProfile = InstalledProfile;
[[nodiscard]] std::vector<InstalledCmykProfile> installed_cmyk_profiles();
// RGB display, input, output and color space profiles (no device links or abstract
// profiles): the destinations Edit > Convert to Color Profile lists (docs/rgb-profiles.md).
[[nodiscard]] std::vector<InstalledRgbProfile> installed_rgb_profiles();
// U.S. Web Coated (SWOP) v2 where Adobe software installed it: the automatic choice when
// present, ahead of the profile Patchy bundles (docs/cmyk.md). Empty path when absent.
[[nodiscard]] std::filesystem::path automatic_cmyk_profile_path();

// The CLUT grid of a profile's A2B0 lutAtoB ('mAB ') tag when its three inputs share one
// size, else 0. A Photoshop Color Lookup profile carries its LUT's grid there.
[[nodiscard]] int color_lookup_profile_grid(std::span<const std::uint8_t> profile_bytes);
// Samples a Color Lookup profile at `grid` points per axis into 16-bit RGB nodes, red
// fastest: an RGB device link on its own, or an abstract profile run between sRGB ends.
// No optimization and no white or gray fix-up, so a stored table is applied as it is
// (docs/legal-constraints.md, "Adjustment layers"). Nullopt when the profile is unusable.
[[nodiscard]] std::optional<std::vector<std::uint16_t>> sample_color_lookup_profile(
    std::span<const std::uint8_t> profile_bytes, int grid);

}  // namespace patchy
