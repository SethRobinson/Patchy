#pragma once

// Reads the explicit physical pixel density (PPI) recorded inside PNG and JPEG byte
// streams, without decoding pixels. Qt's QImage reports a screen-derived default
// dotsPerMeter when a file carries no density, which is indistinguishable from a real
// value; this probe exists so the import policy can tell "tagged" from "untagged" and
// give untagged files Photoshop's 72 PPI convention (see docs/resolution-units.md).

#include <cstdint>
#include <optional>
#include <span>

namespace patchy::formats {

struct ImageDensity {
  double horizontal_ppi{0.0};
  double vertical_ppi{0.0};
};

enum class ImageDensityContainer {
  Unrecognized,  // not a PNG or JPEG stream
  Png,
  Jpeg,
};

struct ImageDensityProbe {
  ImageDensityContainer container{ImageDensityContainer::Unrecognized};
  // Set only when the file records a usable absolute density. Aspect-ratio-only
  // densities (JFIF unit 0, pHYs unit 0) and EXIF ResolutionUnit "none" stay empty.
  std::optional<ImageDensity> density;
};

// Never throws; malformed structures simply end the scan with whatever was found.
// JPEG prefers EXIF XResolution/YResolution (tags 282/283, unit 296) over the JFIF
// APP0 density, matching how Photoshop resolves the common camera case (JFIF unit 0
// beside EXIF 72 ppi opens as 72).
[[nodiscard]] ImageDensityProbe probe_image_density(std::span<const std::uint8_t> bytes) noexcept;

}  // namespace patchy::formats
