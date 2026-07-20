#pragma once

#include "core/document.hpp"
#include "core/layer.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace patchy {

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
  [[nodiscard]] RgbColor convert_single(std::uint8_t cyan_inverted, std::uint8_t magenta_inverted,
                                        std::uint8_t yellow_inverted,
                                        std::uint8_t black_inverted) const;
  [[nodiscard]] const std::string& profile_description() const;

private:
  struct Impl;
  explicit CmykToRgbTransform(std::unique_ptr<Impl> impl);
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

}  // namespace patchy
