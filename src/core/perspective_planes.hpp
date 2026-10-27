#pragma once

// Perspective planes (docs/perspective-planes.md): flat surfaces the user marks with four
// corner clicks, and paste, clone and paint mapped through one plane's perspective.
//
// Legal boundary (docs/legal-constraints.md, "Perspective editing"): every edit is clipped
// to the ONE plane it starts on and is never continued onto another plane, planes come
// only from the user's clicks and drags (no line or plane detection, no automatic
// straightening), and a clone source is read only inside its own plane.
//
// Geometry is plain IEEE double arithmetic with no library transcendental (sin and cos
// are evaluated by a fixed series here), so the same input gives the same pixels on every
// toolchain (the source is built with -ffp-contract=off). Pixel work is integer.

#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace patchy {

struct PlanePoint {
  double x{0.0};
  double y{0.0};

  friend bool operator==(const PlanePoint&, const PlanePoint&) = default;
};

using PlaneQuad = std::array<PlanePoint, 4>;

// A 3x3 projective map, row-major: (x, y) -> ((m0 x + m1 y + m2) / w, (m3 x + m4 y + m5) / w)
// with w = m6 x + m7 y + m8.
struct Homography {
  std::array<double, 9> m{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};

  [[nodiscard]] double w(PlanePoint p) const noexcept;
  // nullopt where the point maps to or past the horizon (w <= 0).
  [[nodiscard]] std::optional<PlanePoint> map(PlanePoint p) const noexcept;
  [[nodiscard]] std::optional<Homography> inverse() const noexcept;
  // The 2x2 derivative of map() at p: {dx'/dx, dx'/dy, dy'/dx, dy'/dy}.
  [[nodiscard]] std::array<double, 4> jacobian(PlanePoint p) const noexcept;
};

// `after * before`: first `before`, then `after`.
[[nodiscard]] Homography operator*(const Homography& after, const Homography& before) noexcept;
[[nodiscard]] Homography homography_scale_translate(double sx, double sy, double tx, double ty) noexcept;

// The map from the unit square (0,0), (1,0), (1,1), (0,1) onto the quad's corners in that
// order; nullopt for a degenerate quad.
[[nodiscard]] std::optional<Homography> homography_from_unit_square(const PlaneQuad& quad) noexcept;

// Why four corners cannot be a plane, in the order they are checked.
enum class PlaneQuadProblem {
  None,
  TooSmall,    // a side shorter than 4 px or an area under 64 px^2
  Crossing,    // two sides cross (the corners are not in order around the surface)
  Concave,     // a corner points inward, or three corners are in a line
  BehindView,  // a connected plane that would bend behind the viewer
};
[[nodiscard]] PlaneQuadProblem check_plane_quad(const PlaneQuad& quad) noexcept;

// sin and cos of an angle in degrees by a fixed series: bit-identical on every toolchain.
[[nodiscard]] std::pair<double, double> perspective_sin_cos_degrees(double degrees) noexcept;

// The pinhole camera the planes share: focal length in document pixels around the
// document center. Only connected planes and plane proportions use it.
struct PerspectiveCamera {
  double focal{1000.0};
  PlanePoint center{};
};
[[nodiscard]] PerspectiveCamera default_perspective_camera(std::int32_t width, std::int32_t height) noexcept;
// The focal length one plane implies when its sides meet at right angles in the scene
// (Zhang's single-rectangle constraint); nullopt when the quad does not fix it (a side
// pair is parallel on screen) or the result is implausible.
[[nodiscard]] std::optional<double> plane_focal_length(const PlaneQuad& quad, PlanePoint center) noexcept;

// A plane pulled out of an edge of another plane: it hinges on that edge at `angle_degrees`
// (90 folds toward the viewer, 180 continues flat, 270 folds away) and reaches `depth`
// times the parent's extent across the edge. Linked planes follow their parent until
// their own corners are dragged.
struct PlaneLink {
  int parent_id{0};
  int edge{0};  // corners edge and edge + 1 of the parent
  double angle_degrees{90.0};
  double depth{1.0};

  friend bool operator==(const PlaneLink&, const PlaneLink&) = default;
};

struct PerspectivePlane {
  int id{0};
  PlaneQuad corners{};
  // Grid cell size in plane units; 0 picks one giving about 8 cells along the long side.
  double grid_size{0.0};
  std::optional<PlaneLink> link{};

  friend bool operator==(const PerspectivePlane&, const PerspectivePlane&) = default;
};

// A plane's own coordinate system. Plane units are proportioned like the real surface
// (from the camera) and scaled so they are document pixels along the side nearest the
// viewer, so a brush or a pasted image keeps its size there and shrinks with distance.
struct PlaneGeometry {
  Homography to_image;  // plane units -> document pixels
  Homography to_plane;  // document pixels -> plane units
  double width{0.0};
  double height{0.0};
  // The corners in plane order (0,0), (W,0), (W,H), (0,H); clockwise on screen, so plane
  // space is never mirrored.
  PlaneQuad quad{};

  [[nodiscard]] bool contains(PlanePoint plane_point, double tolerance = 0.0) const noexcept;
};
[[nodiscard]] std::optional<PlaneGeometry> plane_geometry(const PlaneQuad& quad, const PerspectiveCamera& camera) noexcept;
[[nodiscard]] double plane_grid_size(const PerspectivePlane& plane, const PlaneGeometry& geometry) noexcept;

// The camera for a set of planes: the focal length of the first plane (lowest id) that
// fixes one, else the default for the document size.
[[nodiscard]] PerspectiveCamera perspective_camera_for(const std::vector<PerspectivePlane>& planes,
                                                       std::int32_t width, std::int32_t height) noexcept;

// The corners of a plane hinged on `edge` of `parent` (PlaneLink above). nullopt when the
// result would reach behind the viewer or is not a usable quad.
[[nodiscard]] std::optional<PlaneQuad> connected_plane_quad(const PlaneQuad& parent, int edge, double angle_degrees,
                                                            double depth, const PerspectiveCamera& camera) noexcept;
// The depth whose far edge passes closest to `image_point` (dragging the far edge).
[[nodiscard]] std::optional<double> connected_plane_depth_at(const PlaneQuad& parent, int edge, double angle_degrees,
                                                             const PerspectiveCamera& camera,
                                                             PlanePoint image_point) noexcept;
// Recomputes every linked plane from its parent (parents first). A link whose result is
// unusable keeps its last corners.
void update_connected_planes(std::vector<PerspectivePlane>& planes, const PerspectiveCamera& camera);

// Premultiplied RGBA8 mip chain for filtered sampling. Outside the image is transparent,
// so bilinear sampling fades an image's own edge over one source pixel.
class PerspectiveSampler {
public:
  PerspectiveSampler() = default;
  // `rgba` is RGBA8 with straight alpha.
  explicit PerspectiveSampler(const PixelBuffer& rgba);
  // Refreshes the levels under `rect` (level-0 pixels) from `rgba`.
  void update(const PixelBuffer& rgba, Rect rect);

  [[nodiscard]] std::int32_t width() const noexcept;
  [[nodiscard]] std::int32_t height() const noexcept;
  [[nodiscard]] bool empty() const noexcept { return levels_.empty(); }
  // Premultiplied RGBA at (x, y) in level-0 pixel coordinates (pixel centers at +0.5), each
  // channel scaled by 256 (0..65280). `footprint` is how many source pixels one output
  // pixel covers: above 1 the sample blends the two nearest mip levels.
  [[nodiscard]] std::array<std::uint32_t, 4> sample(double x, double y, double footprint) const noexcept;

private:
  struct Level {
    std::int32_t width{0};
    std::int32_t height{0};
    std::vector<std::uint8_t> rgba;  // premultiplied
  };
  [[nodiscard]] std::array<std::uint32_t, 4> sample_level(const Level& level, double x, double y) const noexcept;
  void rebuild_level(std::size_t index, Rect rect);
  std::vector<Level> levels_;
};

// Where a pasted image sits on its plane: its center in plane units, a uniform scale
// (1 = one image pixel per plane unit) and a rotation in plane space.
struct PerspectivePlacement {
  double center_x{0.0};
  double center_y{0.0};
  double scale{1.0};
  double rotation_degrees{0.0};

  friend bool operator==(const PerspectivePlacement&, const PerspectivePlacement&) = default;
};
// Image pixels -> plane units for `placement`.
[[nodiscard]] Homography placement_to_plane(const PerspectivePlacement& placement, std::int32_t image_width,
                                            std::int32_t image_height) noexcept;
// The placement that fills the whole plane with the image (stretched to its corners).
[[nodiscard]] Homography fill_plane_from_image(const PlaneGeometry& plane, std::int32_t image_width,
                                               std::int32_t image_height) noexcept;
// A placement centred on the plane that fits the image inside it with a margin, turned a
// quarter turn at a time so the image's top faces up on screen.
[[nodiscard]] PerspectivePlacement fit_placement(const PlaneGeometry& plane, std::int32_t image_width,
                                                 std::int32_t image_height) noexcept;

// Draws `image` (through image -> plane map `image_to_plane`) onto `target` (RGBA8 straight
// alpha), source-over, clipped to `plane` with an anti-aliased edge and to `limit` (target
// pixels). `document_to_target` maps document pixels to target pixels: identity for the
// full-resolution write, the view transform for an on-screen preview. Returns the target
// rectangle it changed (empty when nothing).
Rect render_perspective_image(PixelBuffer& target, const Homography& document_to_target, Rect limit,
                              const PlaneGeometry& plane, const PerspectiveSampler& image,
                              const Homography& image_to_plane, int opacity = 255);

struct PerspectiveBrush {
  double size{60.0};  // diameter in plane units
  int hardness{50};   // 0..100
  int opacity{100};   // 1..100
};

enum class PerspectiveStrokeKind { Clone, Paint };

// One clone or paint stroke. Clone reads the workspace image as it was when the stroke
// began, at `source_offset` (plane units) from each painted point, through the source
// plane, and only inside that plane; paint lays `color` (RGBA, straight).
struct PerspectiveStrokeSetup {
  PerspectiveStrokeKind kind{PerspectiveStrokeKind::Paint};
  PlaneGeometry plane;
  PerspectiveBrush brush;
  std::array<std::uint8_t, 4> color{0, 0, 0, 255};
  PlaneGeometry source_plane;
  PlanePoint source_offset{};
};

// The workspace's image: the layer (placed on the canvas) under an edits layer the
// tools write, and their composite. Every change is recorded as tile pairs so the
// workspace can undo it.
class PerspectiveCanvas {
public:
  static constexpr std::int32_t kTileSize = 64;

  struct TileChange {
    std::int32_t tile_x{0};
    std::int32_t tile_y{0};
    std::vector<std::uint8_t> before;
    std::vector<std::uint8_t> after;
  };
  struct Change {
    std::vector<TileChange> tiles;
    Rect bounds{};
    [[nodiscard]] bool empty() const noexcept { return tiles.empty(); }
  };

  PerspectiveCanvas() = default;
  // `layer` is RGBA8 straight alpha at `layer_bounds` in a `width` x `height` document.
  PerspectiveCanvas(const PixelBuffer& layer, Rect layer_bounds, std::int32_t width, std::int32_t height);

  [[nodiscard]] std::int32_t width() const noexcept { return edits_.width(); }
  [[nodiscard]] std::int32_t height() const noexcept { return edits_.height(); }
  [[nodiscard]] const PixelBuffer& edits() const noexcept { return edits_; }
  [[nodiscard]] const PixelBuffer& composite() const noexcept { return composite_; }
  [[nodiscard]] const PixelBuffer& original() const noexcept { return original_; }
  [[nodiscard]] bool has_edits() const noexcept;

  // Pastes `image` onto `plane` and returns the change (empty when nothing changed).
  Change paste(const PlaneGeometry& plane, const PixelBuffer& image, const Homography& image_to_plane);

  void begin_stroke(const PerspectiveStrokeSetup& setup);
  // Adds dabs from the last point (or a single dab for the first) to `plane_point`, in
  // the stroke plane's units. Returns the document rectangle that changed.
  Rect stroke_to(PlanePoint plane_point);
  [[nodiscard]] bool stroke_active() const noexcept { return stroke_.has_value(); }
  Change end_stroke();

  // Writes a recorded change back (undo) or forward (redo).
  Rect revert(const Change& change);
  Rect reapply(const Change& change);

  // The edits alone, cropped to what they cover (nullopt when there are none).
  [[nodiscard]] std::optional<std::pair<PixelBuffer, Rect>> cropped_edits() const;

private:
  struct Stroke {
    PerspectiveStrokeSetup setup;
    std::optional<PlanePoint> last;
    double carry{0.0};
    std::vector<std::uint16_t> mask;  // canvas-sized coverage, 0..65535
    std::map<std::pair<std::int32_t, std::int32_t>, std::vector<std::uint8_t>> backup;
    Rect bounds{};
  };
  void backup_tiles(std::map<std::pair<std::int32_t, std::int32_t>, std::vector<std::uint8_t>>& backup,
                    Rect rect) const;
  Change make_change(std::map<std::pair<std::int32_t, std::int32_t>, std::vector<std::uint8_t>> backup,
                     Rect bounds) const;
  void write_tiles(const Change& change, bool after);
  void refresh(Rect rect);
  Rect dab(PlanePoint center);

  PixelBuffer original_;
  PixelBuffer edits_;
  PixelBuffer composite_;
  PerspectiveSampler composite_sampler_;
  std::optional<Stroke> stroke_;
};

// The edits' layer composited onto the layer itself (straight RGBA8). The result covers
// the layer and every edited canvas pixel; `bounds` receives its document rectangle.
[[nodiscard]] PixelBuffer composite_edits_onto_layer(const PixelBuffer& layer, Rect layer_bounds,
                                                     const PixelBuffer& edits, Rect edits_bounds, Rect& bounds);

}  // namespace patchy
