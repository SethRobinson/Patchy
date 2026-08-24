#pragma once

#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

// Divide Scanned Photos: find the photos lying on a background in one scan (or
// one camera picture), and cut each to its own image, optionally straightened
// (min-area-rect rotation) or perspective-rectified (edge-fit quad, homography,
// aspect recovery).
//
// The pipeline is built from published, long-expired techniques: border-seeded
// flood fill segmentation against a median/MAD background model, binary
// morphology (Serra 1982), two-pass union-find component labeling (Rosenfeld
// & Pfaltz 1966), monotone-chain convex hulls (Andrew 1979), minimum-area
// enclosing rectangles by rotating calipers (Freeman & Shapira 1975; Toussaint
// 1983), rect-to-quad homographies (core/warp_mesh), and aspect recovery of a
// projected rectangle (Zhang & He, MSR-TR-2003-39). Every stage is integer or
// deterministic-double math with fixed tie-breaks (the cross-toolchain rule).
//
// Legal boundary (docs/legal-constraints.md, "Scanned-photo division"):
// detection runs once per explicit request or dialog parameter change on a
// still image; fixed deterministic geometry, global parameters, user-editable
// regions. Never add live-camera or per-frame detection, automatic capture,
// ML or content classification, or automatic selection between algorithms.
namespace patchy {

enum class PhotoExtractMode : std::uint8_t { Cut, Straighten, Perspective };

struct PhotoRegion {
  // Source-space corners, pixel-corner convention, order TL, TR, BR, BL as
  // (x, y) pairs. `quad` is always the min-area rect (what Straighten
  // samples); `perspective_corners` is the per-side edge fit used by
  // Perspective mode, valid only when perspective_quad is true.
  std::array<double, 8> quad{};
  std::array<double, 8> perspective_corners{};
  double angle_degrees{0.0};     // min-area-rect angle in [-45, 45); 0 when snapped
  Rect bounding_box{};           // integer axis-aligned content bbox (Cut mode)
  bool perspective_quad{false};  // perspective_corners came from per-side edge fits
  bool user_added{false};        // dialog-owned; survives re-detection
};

struct PhotoDetectOptions {
  int sensitivity{50};    // 0..100; higher separates photos closer to the background color
  double source_ppi{0.0};  // 0 = unknown; enables the physical minimum photo size
};

struct PhotoDetectResult {
  std::vector<PhotoRegion> regions;  // reading order (rows top to bottom, left to right)
  std::int32_t analysis_width{0};    // size of the internal downscaled analysis image
  std::int32_t analysis_height{0};
};

// Detects photo regions in an RGB, RGBA, or gray buffer (8/16-bit and float
// convert to 8-bit for analysis; alpha composites over white). `cancelled`
// (optional) is polled between stages and regions; a cancelled or unsupported
// detection returns an empty result.
[[nodiscard]] PhotoDetectResult detect_photo_regions(const PixelBuffer& source,
                                                     const PhotoDetectOptions& options,
                                                     const std::function<bool()>& cancelled = {});

// Sorts regions into reading order: rows top to bottom (banded by half the
// median region height), left to right inside a row. detect_photo_regions
// already returns this order; the dialog re-sorts after user edits.
void order_photo_regions_reading_order(std::vector<PhotoRegion>& regions);

struct PhotoOutputGeometry {
  std::int32_t width{0};
  std::int32_t height{0};
  std::array<double, 8> source_quad{};  // the quad extract_photo_region samples
};

// Output size and sampled quad for a region under a mode. Cut uses the integer
// bounding box. Straighten uses the quad's own side lengths. Perspective
// recovers the true aspect ratio (Zhang closed form, principal point at the
// source center; near-affine quads fall back to mean side lengths) and snaps
// it to common print ratios within 3%.
[[nodiscard]] PhotoOutputGeometry photo_output_geometry(const PhotoRegion& region,
                                                        PhotoExtractMode mode, double source_width,
                                                        double source_height);

// Aspect ratio (width / height) of the rectangle whose projection is `quad`
// (corners TL, TR, BR, BL). nullopt when the closed form is degenerate.
[[nodiscard]] std::optional<double> rectified_aspect_ratio(const std::array<double, 8>& quad,
                                                           double source_width,
                                                           double source_height);

// Snaps to 1:1, 5:4, 4:3, 7:5, 3:2, 16:9 (either orientation) within 3%;
// returns the input unchanged when no ratio is close.
[[nodiscard]] double snap_aspect_to_print_ratios(double aspect);

// Cut copies bounding_box rows byte for byte (clamped to the source).
// Straighten/Perspective inverse-map through the rect-to-quad homography with
// bilinear sampling, edge-clamped; axis-aligned quads take the exact copy
// path (the crop_document convention). Result format matches the source.
[[nodiscard]] PixelBuffer extract_photo_region(const PixelBuffer& source, const PhotoRegion& region,
                                               PhotoExtractMode mode);

}  // namespace patchy
