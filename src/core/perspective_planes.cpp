#include "core/perspective_planes.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace patchy {

namespace {

constexpr double kPi = 3.14159265358979323846;

double cross(PlanePoint origin, PlanePoint a, PlanePoint b) noexcept {
  return (a.x - origin.x) * (b.y - origin.y) - (a.y - origin.y) * (b.x - origin.x);
}

double distance(PlanePoint a, PlanePoint b) noexcept {
  const double dx = b.x - a.x;
  const double dy = b.y - a.y;
  return std::sqrt(dx * dx + dy * dy);
}

double signed_area(const PlaneQuad& quad) noexcept {
  double sum = 0.0;
  for (std::size_t i = 0; i < 4; ++i) {
    const auto& a = quad[i];
    const auto& b = quad[(i + 1) % 4];
    sum += a.x * b.y - b.x * a.y;
  }
  return sum * 0.5;
}

// Proper crossing of segments ab and cd (touching end points do not count).
bool segments_cross(PlanePoint a, PlanePoint b, PlanePoint c, PlanePoint d) noexcept {
  const double d1 = cross(a, b, c);
  const double d2 = cross(a, b, d);
  const double d3 = cross(c, d, a);
  const double d4 = cross(c, d, b);
  return ((d1 > 0.0 && d2 < 0.0) || (d1 < 0.0 && d2 > 0.0)) && ((d3 > 0.0 && d4 < 0.0) || (d3 < 0.0 && d4 > 0.0));
}

struct Vec3 {
  double x{0.0};
  double y{0.0};
  double z{0.0};
};

Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(double s, Vec3 a) noexcept { return {s * a.x, s * a.y, s * a.z}; }
double dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross3(Vec3 a, Vec3 b) noexcept {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(Vec3 a) noexcept { return std::sqrt(dot(a, a)); }

// The parent plane in camera space: X(u, v) = b1 u + b2 v + b3 over the unit square of
// the parent's stored corner order, and the hinge frame for a connected plane.
struct HingeFrame {
  Vec3 a;          // the hinge edge's first corner
  Vec3 b;          // its second corner
  Vec3 direction;  // unit direction the connected plane leaves the edge in
  double extent{0.0};
};

std::optional<HingeFrame> hinge_frame(const PlaneQuad& parent, int edge, double angle_degrees,
                                      const PerspectiveCamera& camera) noexcept {
  if (check_plane_quad(parent) != PlaneQuadProblem::None || edge < 0 || edge > 3 || !(camera.focal > 0.0)) {
    return std::nullopt;
  }
  PlaneQuad centered{};
  for (std::size_t i = 0; i < 4; ++i) {
    centered[i] = {parent[i].x - camera.center.x, parent[i].y - camera.center.y};
  }
  const auto h = homography_from_unit_square(centered);
  if (!h.has_value()) {
    return std::nullopt;
  }
  const auto& m = h->m;
  const double f = camera.focal;
  Vec3 b1{m[0] / f, m[3] / f, m[6]};
  Vec3 b2{m[1] / f, m[4] / f, m[7]};
  Vec3 b3{m[2] / f, m[5] / f, m[8]};
  const auto point = [&](double u, double v) { return u * b1 + v * b2 + b3; };
  if (point(0.5, 0.5).z <= 0.0) {
    b1 = -1.0 * b1;
    b2 = -1.0 * b2;
    b3 = -1.0 * b3;
  }
  static constexpr std::array<PlanePoint, 4> kUnit{{{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}}};
  const auto ua = kUnit[static_cast<std::size_t>(edge)];
  const auto ub = kUnit[static_cast<std::size_t>((edge + 1) % 4)];
  HingeFrame frame;
  frame.a = point(ua.x, ua.y);
  frame.b = point(ub.x, ub.y);
  const Vec3 center = point(0.5, 0.5);
  const Vec3 along = frame.b - frame.a;
  const double along_length = length(along);
  if (!(along_length > 0.0)) {
    return std::nullopt;
  }
  const Vec3 along_unit = (1.0 / along_length) * along;
  const Vec3 to_center = center - frame.a;
  const Vec3 inward = to_center - dot(to_center, along_unit) * along_unit;
  const double inward_length = length(inward);
  if (!(inward_length > 0.0)) {
    return std::nullopt;
  }
  const Vec3 outward = (-1.0 / inward_length) * inward;
  Vec3 normal = cross3(along_unit, outward);
  const double normal_length = length(normal);
  if (!(normal_length > 0.0)) {
    return std::nullopt;
  }
  normal = (1.0 / normal_length) * normal;
  // Toward the viewer (the camera sits at the origin).
  if (dot(normal, -1.0 * center) < 0.0) {
    normal = -1.0 * normal;
  }
  const auto [sine, cosine] = perspective_sin_cos_degrees(180.0 - angle_degrees);
  frame.direction = cosine * outward + sine * normal;
  frame.extent = 2.0 * inward_length;
  return frame;
}

std::optional<PlanePoint> project(Vec3 point, const PerspectiveCamera& camera, double minimum_z) noexcept {
  if (!(point.z > minimum_z)) {
    return std::nullopt;
  }
  return PlanePoint{camera.focal * point.x / point.z + camera.center.x,
                    camera.focal * point.y / point.z + camera.center.y};
}

std::uint8_t to_byte(std::int64_t value) noexcept {
  return static_cast<std::uint8_t>(std::clamp<std::int64_t>(value, 0, 255));
}

// Source-over of straight RGBA8 `top` onto straight RGBA8 `bottom`, written to `out`.
void over_straight(const std::uint8_t* top, const std::uint8_t* bottom, std::uint8_t* out) noexcept {
  const std::int64_t ta = top[3];
  const std::int64_t ba = bottom[3];
  const std::int64_t alpha = ta * 255 + ba * (255 - ta);  // 0..65025
  if (alpha == 0) {
    out[0] = out[1] = out[2] = out[3] = 0;
    return;
  }
  for (int c = 0; c < 3; ++c) {
    const std::int64_t value = top[c] * ta * 255 + bottom[c] * ba * (255 - ta);
    out[c] = to_byte((value + alpha / 2) / alpha);
  }
  out[3] = to_byte((alpha + 127) / 255);
}

// Straight RGBA8 from premultiplied channels scaled by 256 (alpha 0..65280).
void store_premultiplied(const std::array<std::int64_t, 4>& premultiplied, std::uint8_t* out) noexcept {
  const std::int64_t alpha = premultiplied[3];
  if (alpha <= 0) {
    out[0] = out[1] = out[2] = out[3] = 0;
    return;
  }
  for (int c = 0; c < 3; ++c) {
    out[c] = to_byte((premultiplied[static_cast<std::size_t>(c)] * 255 + alpha / 2) / alpha);
  }
  out[3] = to_byte((alpha + 128) / 256);
}

std::array<std::int64_t, 4> premultiply(const std::uint8_t* pixel) noexcept {
  const std::int64_t a = pixel[3];
  return {(pixel[0] * a * 256 + 127) / 255, (pixel[1] * a * 256 + 127) / 255, (pixel[2] * a * 256 + 127) / 255,
          a * 256};
}

Rect intersect_rect(Rect a, Rect b) noexcept {
  const auto left = std::max(a.x, b.x);
  const auto top = std::max(a.y, b.y);
  const auto right = std::min(a.x + a.width, b.x + b.width);
  const auto bottom = std::min(a.y + a.height, b.y + b.height);
  if (right <= left || bottom <= top) {
    return {};
  }
  return {left, top, right - left, bottom - top};
}

Rect unite_rects(Rect a, Rect b) noexcept {
  if (a.empty()) {
    return b;
  }
  if (b.empty()) {
    return a;
  }
  const auto left = std::min(a.x, b.x);
  const auto top = std::min(a.y, b.y);
  const auto right = std::max(a.x + a.width, b.x + b.width);
  const auto bottom = std::max(a.y + a.height, b.y + b.height);
  return {left, top, right - left, bottom - top};
}

// The pixel rectangle covering points (bounding box, one pixel of margin), or empty when
// a coordinate is not finite or absurdly large.
Rect bounding_pixels(const std::vector<PlanePoint>& points) noexcept {
  if (points.empty()) {
    return {};
  }
  double left = points.front().x;
  double right = left;
  double top = points.front().y;
  double bottom = top;
  for (const auto& point : points) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
      return {};
    }
    left = std::min(left, point.x);
    right = std::max(right, point.x);
    top = std::min(top, point.y);
    bottom = std::max(bottom, point.y);
  }
  constexpr double kLimit = 1.0e9;
  left = std::clamp(left, -kLimit, kLimit);
  right = std::clamp(right, -kLimit, kLimit);
  top = std::clamp(top, -kLimit, kLimit);
  bottom = std::clamp(bottom, -kLimit, kLimit);
  const auto x0 = static_cast<std::int32_t>(std::floor(left)) - 1;
  const auto y0 = static_cast<std::int32_t>(std::floor(top)) - 1;
  const auto x1 = static_cast<std::int32_t>(std::ceil(right)) + 1;
  const auto y1 = static_cast<std::int32_t>(std::ceil(bottom)) + 1;
  return {x0, y0, x1 - x0, y1 - y0};
}

// Plane units per target pixel along the plane's u and v axes at a target pixel.
std::pair<double, double> axis_gradients(const std::array<double, 4>& j) noexcept {
  return {std::sqrt(j[0] * j[0] + j[1] * j[1]), std::sqrt(j[2] * j[2] + j[3] * j[3])};
}

// How much of a target pixel lies inside the plane rectangle: its distance to the nearest
// plane edge in target pixels, plus half a pixel, clamped to 0..1, as 0..256.
int plane_coverage(PlanePoint p, const std::array<double, 4>& j, double width, double height) noexcept {
  const auto [gu, gv] = axis_gradients(j);
  if (!(gu > 0.0) || !(gv > 0.0)) {
    return 0;
  }
  const double nearest = std::min({p.x / gu, (width - p.x) / gu, p.y / gv, (height - p.y) / gv});
  const double coverage = std::clamp(nearest + 0.5, 0.0, 1.0);
  return static_cast<int>(coverage * 256.0 + 0.5);
}

// Source pixels one target pixel covers, from the 2x2 derivative of target -> source.
double footprint_of(const std::array<double, 4>& j) noexcept {
  const double along_x = std::sqrt(j[0] * j[0] + j[2] * j[2]);
  const double along_y = std::sqrt(j[1] * j[1] + j[3] * j[3]);
  return std::max(along_x, along_y);
}

// `outer` after `inner`, both 2x2 row-major derivatives.
std::array<double, 4> chain(const std::array<double, 4>& outer, const std::array<double, 4>& inner) noexcept {
  return {outer[0] * inner[0] + outer[1] * inner[2], outer[0] * inner[1] + outer[1] * inner[3],
          outer[2] * inner[0] + outer[3] * inner[2], outer[2] * inner[1] + outer[3] * inner[3]};
}

// The target rectangle a plane-clipped image draw can touch.
Rect image_draw_bounds(const PixelBuffer& target, const Homography& plane_to_target, Rect limit,
                       const PlaneGeometry& plane, const Homography& image_to_plane, std::int32_t image_width,
                       std::int32_t image_height) noexcept {
  std::vector<PlanePoint> corners;
  for (const auto& corner :
       std::array<PlanePoint, 4>{{{0.0, 0.0}, {plane.width, 0.0}, {plane.width, plane.height}, {0.0, plane.height}}}) {
    const auto mapped = plane_to_target.map(corner);
    if (!mapped.has_value()) {
      return {};
    }
    corners.push_back(*mapped);
  }
  auto bounds = bounding_pixels(corners);
  std::vector<PlanePoint> image_corners;
  const auto image_to_target = plane_to_target * image_to_plane;
  for (const auto& corner : std::array<PlanePoint, 4>{
           {{0.0, 0.0},
            {static_cast<double>(image_width), 0.0},
            {static_cast<double>(image_width), static_cast<double>(image_height)},
            {0.0, static_cast<double>(image_height)}}}) {
    const auto mapped = image_to_target.map(corner);
    if (!mapped.has_value()) {
      image_corners.clear();
      break;
    }
    image_corners.push_back(*mapped);
  }
  if (!image_corners.empty()) {
    bounds = intersect_rect(bounds, bounding_pixels(image_corners));
  }
  bounds = intersect_rect(bounds, limit);
  return intersect_rect(bounds, Rect{0, 0, target.width(), target.height()});
}

}  // namespace

double Homography::w(PlanePoint p) const noexcept { return m[6] * p.x + m[7] * p.y + m[8]; }

std::optional<PlanePoint> Homography::map(PlanePoint p) const noexcept {
  const double denominator = w(p);
  const double magnitude = std::abs(m[6] * p.x) + std::abs(m[7] * p.y) + std::abs(m[8]);
  if (!(denominator > 1.0e-12 * magnitude) || !(magnitude > 0.0)) {
    return std::nullopt;
  }
  return PlanePoint{(m[0] * p.x + m[1] * p.y + m[2]) / denominator, (m[3] * p.x + m[4] * p.y + m[5]) / denominator};
}

std::optional<Homography> Homography::inverse() const noexcept {
  const double c00 = m[4] * m[8] - m[5] * m[7];
  const double c01 = m[5] * m[6] - m[3] * m[8];
  const double c02 = m[3] * m[7] - m[4] * m[6];
  const double determinant = m[0] * c00 + m[1] * c01 + m[2] * c02;
  if (!(std::abs(determinant) > 0.0) || !std::isfinite(determinant)) {
    return std::nullopt;
  }
  const double inv = 1.0 / determinant;
  Homography result;
  result.m = {c00 * inv,
              (m[2] * m[7] - m[1] * m[8]) * inv,
              (m[1] * m[5] - m[2] * m[4]) * inv,
              c01 * inv,
              (m[0] * m[8] - m[2] * m[6]) * inv,
              (m[2] * m[3] - m[0] * m[5]) * inv,
              c02 * inv,
              (m[1] * m[6] - m[0] * m[7]) * inv,
              (m[0] * m[4] - m[1] * m[3]) * inv};
  return result;
}

std::array<double, 4> Homography::jacobian(PlanePoint p) const noexcept {
  const double denominator = w(p);
  if (denominator == 0.0) {
    return {0.0, 0.0, 0.0, 0.0};
  }
  const double x = (m[0] * p.x + m[1] * p.y + m[2]) / denominator;
  const double y = (m[3] * p.x + m[4] * p.y + m[5]) / denominator;
  return {(m[0] - x * m[6]) / denominator, (m[1] - x * m[7]) / denominator, (m[3] - y * m[6]) / denominator,
          (m[4] - y * m[7]) / denominator};
}

Homography operator*(const Homography& after, const Homography& before) noexcept {
  Homography result;
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      double sum = 0.0;
      for (int k = 0; k < 3; ++k) {
        sum += after.m[static_cast<std::size_t>(row * 3 + k)] * before.m[static_cast<std::size_t>(k * 3 + column)];
      }
      result.m[static_cast<std::size_t>(row * 3 + column)] = sum;
    }
  }
  return result;
}

Homography homography_scale_translate(double sx, double sy, double tx, double ty) noexcept {
  Homography result;
  result.m = {sx, 0.0, tx, 0.0, sy, ty, 0.0, 0.0, 1.0};
  return result;
}

std::optional<Homography> homography_from_unit_square(const PlaneQuad& quad) noexcept {
  // Heckbert's square-to-quadrilateral map.
  const double dx1 = quad[1].x - quad[2].x;
  const double dx2 = quad[3].x - quad[2].x;
  const double dx3 = quad[0].x - quad[1].x + quad[2].x - quad[3].x;
  const double dy1 = quad[1].y - quad[2].y;
  const double dy2 = quad[3].y - quad[2].y;
  const double dy3 = quad[0].y - quad[1].y + quad[2].y - quad[3].y;
  const double denominator = dx1 * dy2 - dx2 * dy1;
  if (!(std::abs(denominator) > 0.0) || !std::isfinite(denominator)) {
    return std::nullopt;
  }
  const double g = (dx3 * dy2 - dx2 * dy3) / denominator;
  const double h = (dx1 * dy3 - dx3 * dy1) / denominator;
  Homography result;
  result.m = {quad[1].x - quad[0].x + g * quad[1].x,
              quad[3].x - quad[0].x + h * quad[3].x,
              quad[0].x,
              quad[1].y - quad[0].y + g * quad[1].y,
              quad[3].y - quad[0].y + h * quad[3].y,
              quad[0].y,
              g,
              h,
              1.0};
  return result;
}

PlaneQuadProblem check_plane_quad(const PlaneQuad& quad) noexcept {
  for (const auto& corner : quad) {
    if (!std::isfinite(corner.x) || !std::isfinite(corner.y)) {
      return PlaneQuadProblem::TooSmall;
    }
  }
  std::array<double, 4> sides{};
  for (std::size_t i = 0; i < 4; ++i) {
    sides[i] = distance(quad[i], quad[(i + 1) % 4]);
    if (sides[i] < 4.0) {
      return PlaneQuadProblem::TooSmall;
    }
  }
  if (segments_cross(quad[0], quad[1], quad[2], quad[3]) || segments_cross(quad[1], quad[2], quad[3], quad[0])) {
    return PlaneQuadProblem::Crossing;
  }
  if (std::abs(signed_area(quad)) < 64.0) {
    return PlaneQuadProblem::TooSmall;
  }
  int positive = 0;
  int negative = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    const auto& a = quad[i];
    const auto& b = quad[(i + 1) % 4];
    const auto& c = quad[(i + 2) % 4];
    const double turn = cross(a, b, c);
    // A corner under about 1.1 degrees from straight counts as three corners in a line.
    if (std::abs(turn) < 0.02 * sides[i] * sides[(i + 1) % 4]) {
      return PlaneQuadProblem::Concave;
    }
    (turn > 0.0 ? positive : negative) += 1;
  }
  return positive == 4 || negative == 4 ? PlaneQuadProblem::None : PlaneQuadProblem::Concave;
}

std::pair<double, double> perspective_sin_cos_degrees(double degrees) noexcept {
  if (!std::isfinite(degrees)) {
    return {0.0, 1.0};
  }
  double reduced = degrees - 360.0 * std::floor(degrees / 360.0);
  const auto quadrant = static_cast<int>(std::floor((reduced + 45.0) / 90.0)) % 4;
  reduced -= 90.0 * static_cast<double>(quadrant);
  if (quadrant == 0 && reduced >= 315.0) {
    reduced -= 360.0;
  }
  const double x = reduced * (kPi / 180.0);
  const double x2 = x * x;
  // Taylor series to x^13 / x^14; |x| <= pi/4 keeps the error under 1e-15.
  const double sine =
      x * (1.0 - x2 / 6.0 * (1.0 - x2 / 20.0 * (1.0 - x2 / 42.0 * (1.0 - x2 / 72.0 * (1.0 - x2 / 110.0 *
                                                                                              (1.0 - x2 / 156.0))))));
  const double cosine =
      1.0 - x2 / 2.0 * (1.0 - x2 / 12.0 * (1.0 - x2 / 30.0 * (1.0 - x2 / 56.0 * (1.0 - x2 / 90.0 *
                                                                                           (1.0 - x2 / 132.0 * (1.0 - x2 / 182.0))))));
  switch (quadrant) {
    case 1:
      return {cosine, -sine};
    case 2:
      return {-sine, -cosine};
    case 3:
      return {-cosine, sine};
    default:
      return {sine, cosine};
  }
}

PerspectiveCamera default_perspective_camera(std::int32_t width, std::int32_t height) noexcept {
  const double w = std::max(1, width);
  const double h = std::max(1, height);
  return {std::sqrt(w * w + h * h), {w * 0.5, h * 0.5}};
}

std::optional<double> plane_focal_length(const PlaneQuad& quad, PlanePoint center) noexcept {
  if (check_plane_quad(quad) != PlaneQuadProblem::None) {
    return std::nullopt;
  }
  PlaneQuad centered{};
  for (std::size_t i = 0; i < 4; ++i) {
    centered[i] = {quad[i].x - center.x, quad[i].y - center.y};
  }
  const auto h = homography_from_unit_square(centered);
  if (!h.has_value()) {
    return std::nullopt;
  }
  const auto& m = h->m;
  const double denominator = m[6] * m[7];
  const double numerator = -(m[0] * m[1] + m[3] * m[4]);
  if (denominator == 0.0) {
    return std::nullopt;
  }
  const double squared = numerator / denominator;
  if (!(squared > 0.0) || !std::isfinite(squared)) {
    return std::nullopt;
  }
  return std::sqrt(squared);
}

bool PlaneGeometry::contains(PlanePoint p, double tolerance) const noexcept {
  return p.x >= -tolerance && p.y >= -tolerance && p.x <= width + tolerance && p.y <= height + tolerance;
}

std::optional<PlaneGeometry> plane_geometry(const PlaneQuad& quad, const PerspectiveCamera& camera) noexcept {
  if (check_plane_quad(quad) != PlaneQuadProblem::None || !(camera.focal > 0.0)) {
    return std::nullopt;
  }
  PlaneQuad ordered = quad;
  if (signed_area(quad) < 0.0) {
    ordered = {quad[0], quad[3], quad[2], quad[1]};
  }
  const auto unit = homography_from_unit_square(ordered);
  PlaneQuad centered{};
  for (std::size_t i = 0; i < 4; ++i) {
    centered[i] = {ordered[i].x - camera.center.x, ordered[i].y - camera.center.y};
  }
  const auto centered_unit = homography_from_unit_square(centered);
  if (!unit.has_value() || !centered_unit.has_value()) {
    return std::nullopt;
  }
  const auto& m = centered_unit->m;
  const double f = camera.focal;
  const double u_length = length(Vec3{m[0] / f, m[3] / f, m[6]});
  const double v_length = length(Vec3{m[1] / f, m[4] / f, m[7]});
  if (!(u_length > 0.0) || !(v_length > 0.0)) {
    return std::nullopt;
  }
  const double aspect = std::clamp(u_length / v_length, 1.0e-3, 1.0e3);
  const double ratio = std::max({distance(ordered[0], ordered[1]) / aspect, distance(ordered[1], ordered[2]),
                                 distance(ordered[2], ordered[3]) / aspect, distance(ordered[3], ordered[0])});
  PlaneGeometry geometry;
  geometry.width = aspect * ratio;
  geometry.height = ratio;
  geometry.to_image = *unit * homography_scale_translate(1.0 / geometry.width, 1.0 / geometry.height, 0.0, 0.0);
  const auto inverse = geometry.to_image.inverse();
  if (!inverse.has_value()) {
    return std::nullopt;
  }
  geometry.to_plane = *inverse;
  geometry.quad = ordered;
  return geometry;
}

double plane_grid_size(const PerspectivePlane& plane, const PlaneGeometry& geometry) noexcept {
  if (plane.grid_size > 0.0) {
    return plane.grid_size;
  }
  return std::max(1.0, std::max(geometry.width, geometry.height) / 8.0);
}

PerspectiveCamera perspective_camera_for(const std::vector<PerspectivePlane>& planes, std::int32_t width,
                                         std::int32_t height) noexcept {
  auto camera = default_perspective_camera(width, height);
  const PerspectivePlane* first = nullptr;
  for (const auto& plane : planes) {
    if (plane.link.has_value() || check_plane_quad(plane.corners) != PlaneQuadProblem::None) {
      continue;
    }
    const auto focal = plane_focal_length(plane.corners, camera.center);
    // Sloppy corners imply wild lenses: keep within a quarter and six times the diagonal.
    if (!focal.has_value() || *focal < 0.25 * camera.focal || *focal > 6.0 * camera.focal) {
      continue;
    }
    if (first == nullptr || plane.id < first->id) {
      first = &plane;
    }
  }
  if (first != nullptr) {
    camera.focal = *plane_focal_length(first->corners, camera.center);
  }
  return camera;
}

std::optional<PlaneQuad> connected_plane_quad(const PlaneQuad& parent, int edge, double angle_degrees, double depth,
                                              const PerspectiveCamera& camera) noexcept {
  const auto frame = hinge_frame(parent, edge, angle_degrees, camera);
  if (!frame.has_value() || !(depth > 0.0)) {
    return std::nullopt;
  }
  const Vec3 reach = (depth * frame->extent) * frame->direction;
  const double minimum_z = 1.0e-3 * std::min(frame->a.z, frame->b.z);
  const auto a = project(frame->a, camera, minimum_z);
  const auto b = project(frame->b, camera, minimum_z);
  const auto c = project(frame->b + reach, camera, minimum_z);
  const auto d = project(frame->a + reach, camera, minimum_z);
  if (!a.has_value() || !b.has_value() || !c.has_value() || !d.has_value()) {
    return std::nullopt;
  }
  const PlaneQuad quad{*a, *b, *c, *d};
  if (check_plane_quad(quad) != PlaneQuadProblem::None) {
    return std::nullopt;
  }
  return quad;
}

std::optional<double> connected_plane_depth_at(const PlaneQuad& parent, int edge, double angle_degrees,
                                               const PerspectiveCamera& camera, PlanePoint image_point) noexcept {
  const auto frame = hinge_frame(parent, edge, angle_degrees, camera);
  if (!frame.has_value()) {
    return std::nullopt;
  }
  const Vec3 middle = 0.5 * (frame->a + frame->b);
  const double minimum_z = 1.0e-3 * middle.z;
  const auto start = project(middle, camera, minimum_z);
  const auto end = project(middle + frame->extent * frame->direction, camera, minimum_z);
  if (!start.has_value() || !end.has_value()) {
    return std::nullopt;
  }
  // The nearest point on the far edge's screen track, then the depth that projects there.
  const double dx = end->x - start->x;
  const double dy = end->y - start->y;
  const double track = dx * dx + dy * dy;
  if (!(track > 0.0)) {
    return std::nullopt;
  }
  const double along = ((image_point.x - start->x) * dx + (image_point.y - start->y) * dy) / track;
  const PlanePoint target{start->x + along * dx, start->y + along * dy};
  const bool use_x = std::abs(dx) >= std::abs(dy);
  const double screen = use_x ? target.x - camera.center.x : target.y - camera.center.y;
  const double origin = use_x ? middle.x : middle.y;
  const double slope = use_x ? frame->direction.x : frame->direction.y;
  const double denominator = screen * frame->direction.z - camera.focal * slope;
  if (denominator == 0.0) {
    return std::nullopt;
  }
  const double t = (camera.focal * origin - screen * middle.z) / denominator;
  const double depth = t / frame->extent;
  if (!std::isfinite(depth) || !((middle + t * frame->direction).z > minimum_z)) {
    return std::nullopt;
  }
  return std::clamp(depth, 0.02, 20.0);
}

void update_connected_planes(std::vector<PerspectivePlane>& planes, const PerspectiveCamera& camera) {
  std::vector<std::size_t> order(planes.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::sort(order.begin(), order.end(), [&planes](std::size_t a, std::size_t b) { return planes[a].id < planes[b].id; });
  for (const auto index : order) {
    auto& plane = planes[index];
    if (!plane.link.has_value()) {
      continue;
    }
    const auto parent = std::find_if(planes.begin(), planes.end(),
                                     [&plane](const PerspectivePlane& other) { return other.id == plane.link->parent_id; });
    if (parent == planes.end() || parent->id == plane.id) {
      plane.link.reset();
      continue;
    }
    const auto quad =
        connected_plane_quad(parent->corners, plane.link->edge, plane.link->angle_degrees, plane.link->depth, camera);
    if (quad.has_value()) {
      plane.corners = *quad;
    }
  }
}

PerspectiveSampler::PerspectiveSampler(const PixelBuffer& rgba) {
  if (rgba.empty() || rgba.format().channels != 4 || rgba.format().bit_depth != BitDepth::UInt8) {
    return;
  }
  std::int32_t width = rgba.width();
  std::int32_t height = rgba.height();
  levels_.push_back({width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)});
  while ((width > 1 || height > 1) && levels_.size() < 16) {
    width = (width + 1) / 2;
    height = (height + 1) / 2;
    levels_.push_back({width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)});
  }
  update(rgba, Rect{0, 0, rgba.width(), rgba.height()});
}

std::int32_t PerspectiveSampler::width() const noexcept { return levels_.empty() ? 0 : levels_.front().width; }
std::int32_t PerspectiveSampler::height() const noexcept { return levels_.empty() ? 0 : levels_.front().height; }

void PerspectiveSampler::update(const PixelBuffer& rgba, Rect rect) {
  if (levels_.empty() || rgba.width() != levels_.front().width || rgba.height() != levels_.front().height) {
    return;
  }
  rect = intersect_rect(rect, Rect{0, 0, rgba.width(), rgba.height()});
  if (rect.empty()) {
    return;
  }
  auto& base = levels_.front();
  for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
      const auto* source = rgba.pixel(x, y);
      auto* out = base.rgba.data() + (static_cast<std::size_t>(y) * base.width + x) * 4;
      const int a = source[3];
      out[0] = static_cast<std::uint8_t>((source[0] * a + 127) / 255);
      out[1] = static_cast<std::uint8_t>((source[1] * a + 127) / 255);
      out[2] = static_cast<std::uint8_t>((source[2] * a + 127) / 255);
      out[3] = static_cast<std::uint8_t>(a);
    }
  }
  for (std::size_t index = 1; index < levels_.size(); ++index) {
    const auto x0 = rect.x / 2;
    const auto y0 = rect.y / 2;
    const auto x1 = (rect.x + rect.width - 1) / 2 + 1;
    const auto y1 = (rect.y + rect.height - 1) / 2 + 1;
    rect = Rect{x0, y0, x1 - x0, y1 - y0};
    rebuild_level(index, rect);
  }
}

void PerspectiveSampler::rebuild_level(std::size_t index, Rect rect) {
  const auto& below = levels_[index - 1];
  auto& level = levels_[index];
  rect = intersect_rect(rect, Rect{0, 0, level.width, level.height});
  for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
      std::array<int, 4> sum{};
      for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
          const auto sx = x * 2 + dx;
          const auto sy = y * 2 + dy;
          if (sx >= below.width || sy >= below.height) {
            continue;
          }
          const auto* texel = below.rgba.data() + (static_cast<std::size_t>(sy) * below.width + sx) * 4;
          for (int c = 0; c < 4; ++c) {
            sum[static_cast<std::size_t>(c)] += texel[c];
          }
        }
      }
      auto* out = level.rgba.data() + (static_cast<std::size_t>(y) * level.width + x) * 4;
      for (int c = 0; c < 4; ++c) {
        out[c] = static_cast<std::uint8_t>((sum[static_cast<std::size_t>(c)] + 2) / 4);
      }
    }
  }
}

std::array<std::uint32_t, 4> PerspectiveSampler::sample_level(const Level& level, double x, double y) const noexcept {
  const double fx = x - 0.5;
  const double fy = y - 0.5;
  if (!(fx > -2.0) || !(fy > -2.0) || !(fx < level.width + 1.0) || !(fy < level.height + 1.0)) {
    return {0, 0, 0, 0};
  }
  const double floor_x = std::floor(fx);
  const double floor_y = std::floor(fy);
  const auto x0 = static_cast<std::int32_t>(floor_x);
  const auto y0 = static_cast<std::int32_t>(floor_y);
  const auto tx = std::clamp(static_cast<std::uint32_t>((fx - floor_x) * 256.0), 0U, 255U);
  const auto ty = std::clamp(static_cast<std::uint32_t>((fy - floor_y) * 256.0), 0U, 255U);
  const auto texel = [&level](std::int32_t px, std::int32_t py) -> const std::uint8_t* {
    if (px < 0 || py < 0 || px >= level.width || py >= level.height) {
      return nullptr;
    }
    return level.rgba.data() + (static_cast<std::size_t>(py) * level.width + px) * 4;
  };
  const std::uint8_t* t00 = texel(x0, y0);
  const std::uint8_t* t10 = texel(x0 + 1, y0);
  const std::uint8_t* t01 = texel(x0, y0 + 1);
  const std::uint8_t* t11 = texel(x0 + 1, y0 + 1);
  std::array<std::uint32_t, 4> result{};
  for (std::size_t c = 0; c < 4; ++c) {
    const std::uint32_t v00 = t00 != nullptr ? t00[c] : 0U;
    const std::uint32_t v10 = t10 != nullptr ? t10[c] : 0U;
    const std::uint32_t v01 = t01 != nullptr ? t01[c] : 0U;
    const std::uint32_t v11 = t11 != nullptr ? t11[c] : 0U;
    const std::uint32_t top = v00 * (256U - tx) + v10 * tx;
    const std::uint32_t bottom = v01 * (256U - tx) + v11 * tx;
    result[c] = (top * (256U - ty) + bottom * ty) >> 8U;
  }
  return result;
}

std::array<std::uint32_t, 4> PerspectiveSampler::sample(double x, double y, double footprint) const noexcept {
  if (levels_.empty()) {
    return {0, 0, 0, 0};
  }
  if (!(footprint > 1.0) || levels_.size() == 1) {
    return sample_level(levels_.front(), x, y);
  }
  // log2 by exponent and a linear mantissa step: exact on every toolchain.
  int exponent = 0;
  const double mantissa = std::frexp(footprint, &exponent);  // footprint = mantissa * 2^exponent
  const double level = static_cast<double>(exponent - 1) + (2.0 * mantissa - 1.0);
  const auto lower = std::min(static_cast<std::size_t>(level), levels_.size() - 1);
  const auto upper = std::min(lower + 1, levels_.size() - 1);
  const auto weight = lower == upper ? 0U : std::clamp(static_cast<std::uint32_t>((level - static_cast<double>(lower)) * 256.0), 0U, 256U);
  const auto a = sample_level(levels_[lower], std::ldexp(x, -static_cast<int>(lower)), std::ldexp(y, -static_cast<int>(lower)));
  if (weight == 0U) {
    return a;
  }
  const auto b = sample_level(levels_[upper], std::ldexp(x, -static_cast<int>(upper)), std::ldexp(y, -static_cast<int>(upper)));
  std::array<std::uint32_t, 4> result{};
  for (std::size_t c = 0; c < 4; ++c) {
    result[c] = (a[c] * (256U - weight) + b[c] * weight) >> 8U;
  }
  return result;
}

Homography placement_to_plane(const PerspectivePlacement& placement, std::int32_t image_width,
                              std::int32_t image_height) noexcept {
  const auto [sine, cosine] = perspective_sin_cos_degrees(placement.rotation_degrees);
  const double tall = placement.scale * placement.stretch;
  const double a = placement.scale * cosine;
  const double b = -tall * sine;
  const double d = placement.scale * sine;
  const double e = tall * cosine;
  const double half_w = image_width * 0.5;
  const double half_h = image_height * 0.5;
  Homography result;
  result.m = {a, b, placement.center_x - (a * half_w + b * half_h),
              d, e, placement.center_y - (d * half_w + e * half_h),
              0.0, 0.0, 1.0};
  return result;
}

Homography fill_plane_from_image(const PlaneGeometry& plane, std::int32_t image_width, std::int32_t image_height) noexcept {
  return homography_scale_translate(plane.width / std::max(1, image_width), plane.height / std::max(1, image_height), 0.0,
                                    0.0);
}

PerspectivePlacement fill_placement(const PlaneGeometry& plane, std::int32_t image_width,
                                    std::int32_t image_height) noexcept {
  PerspectivePlacement placement;
  placement.center_x = plane.width * 0.5;
  placement.center_y = plane.height * 0.5;
  placement.scale = plane.width / std::max(1, image_width);
  const double height_scale = plane.height / std::max(1, image_height);
  placement.stretch = placement.scale > 0.0 ? height_scale / placement.scale : 1.0;
  return placement;
}

PerspectivePlacement fit_placement(const PlaneGeometry& plane, std::int32_t image_width, std::int32_t image_height) noexcept {
  PerspectivePlacement placement;
  placement.center_x = plane.width * 0.5;
  placement.center_y = plane.height * 0.5;
  // Plane directions -v, +u, +v, -u are the image's top at 0, 90, 180 and 270 degrees.
  const std::array<PlanePoint, 4> directions{{{0.0, -1.0}, {1.0, 0.0}, {0.0, 1.0}, {-1.0, 0.0}}};
  const PlanePoint center{placement.center_x, placement.center_y};
  const auto center_image = plane.to_image.map(center);
  int best = 0;
  double best_up = -std::numeric_limits<double>::infinity();
  const double step = 0.05 * std::min(plane.width, plane.height);
  for (int k = 0; k < 4; ++k) {
    const auto& direction = directions[static_cast<std::size_t>(k)];
    const auto moved = plane.to_image.map({center.x + direction.x * step, center.y + direction.y * step});
    if (!center_image.has_value() || !moved.has_value()) {
      continue;
    }
    const double dx = moved->x - center_image->x;
    const double dy = moved->y - center_image->y;
    const double len = std::sqrt(dx * dx + dy * dy);
    const double up = len > 0.0 ? -dy / len : -1.0;
    if (up > best_up + 1.0e-9) {
      best_up = up;
      best = k;
    }
  }
  placement.rotation_degrees = 90.0 * best;
  const double w = std::max(1, (best % 2) == 0 ? image_width : image_height);
  const double h = std::max(1, (best % 2) == 0 ? image_height : image_width);
  placement.scale = std::min(1.0, 0.8 * std::min(plane.width / w, plane.height / h));
  return placement;
}

Rect render_perspective_image(PixelBuffer& target, const Homography& document_to_target, Rect limit,
                              const PlaneGeometry& plane, const PerspectiveSampler& image,
                              const Homography& image_to_plane, int opacity) {
  if (target.empty() || image.empty() || target.format().channels != 4 || target.format().bit_depth != BitDepth::UInt8) {
    return {};
  }
  const auto plane_to_target = document_to_target * plane.to_image;
  const auto target_to_plane = plane_to_target.inverse();
  const auto plane_to_image = image_to_plane.inverse();
  if (!target_to_plane.has_value() || !plane_to_image.has_value()) {
    return {};
  }
  const auto bounds = image_draw_bounds(target, plane_to_target, limit, plane, image_to_plane, image.width(), image.height());
  if (bounds.empty()) {
    return {};
  }
  const int strength = std::clamp(opacity, 0, 255);
  Rect changed{};
  // The data() call detaches shared storage once, here, before the loop writes pixels.
  (void)target.data();
  for (std::int32_t y = bounds.y; y < bounds.y + bounds.height; ++y) {
    for (std::int32_t x = bounds.x; x < bounds.x + bounds.width; ++x) {
      const PlanePoint center{x + 0.5, y + 0.5};
      const auto p = target_to_plane->map(center);
      if (!p.has_value()) {
        continue;
      }
      const auto j = target_to_plane->jacobian(center);
      const int coverage = plane_coverage(*p, j, plane.width, plane.height);
      if (coverage <= 0) {
        continue;
      }
      const auto q = plane_to_image->map(*p);
      if (!q.has_value()) {
        continue;
      }
      const auto footprint = footprint_of(chain(plane_to_image->jacobian(*p), j));
      const auto sample = image.sample(q->x, q->y, footprint);
      const std::int64_t factor = (static_cast<std::int64_t>(coverage) * strength + 127) / 255;  // 0..256
      std::array<std::int64_t, 4> source{};
      for (std::size_t c = 0; c < 4; ++c) {
        source[c] = (static_cast<std::int64_t>(sample[c]) * factor) >> 8;
      }
      if (source[3] <= 0) {
        continue;
      }
      auto* pixel = target.pixel(x, y);
      const auto below = premultiply(pixel);
      std::array<std::int64_t, 4> out{};
      for (std::size_t c = 0; c < 4; ++c) {
        out[c] = source[c] + (below[c] * (65280 - source[3]) + 32640) / 65280;
      }
      std::array<std::uint8_t, 4> stored{};
      store_premultiplied(out, stored.data());
      if (std::memcmp(stored.data(), pixel, 4) != 0) {
        std::memcpy(pixel, stored.data(), 4);
        changed = unite_rects(changed, Rect{x, y, 1, 1});
      }
    }
  }
  return changed;
}

PerspectiveCanvas::PerspectiveCanvas(const PixelBuffer& layer, Rect layer_bounds, std::int32_t width,
                                     std::int32_t height)
    : original_(std::max(1, width), std::max(1, height), PixelFormat::rgba8()),
      edits_(std::max(1, width), std::max(1, height), PixelFormat::rgba8()) {
  original_.clear(0);
  edits_.clear(0);
  if (layer.format().channels == 4 && layer.format().bit_depth == BitDepth::UInt8) {
    const auto overlap = intersect_rect(layer_bounds, Rect{0, 0, original_.width(), original_.height()});
    for (std::int32_t y = overlap.y; y < overlap.y + overlap.height; ++y) {
      std::memcpy(original_.pixel(overlap.x, y), layer.pixel(overlap.x - layer_bounds.x, y - layer_bounds.y),
                  static_cast<std::size_t>(overlap.width) * 4);
    }
  }
  initial_original_ = original_;
  composite_ = original_;
  (void)composite_.data();
  composite_sampler_ = PerspectiveSampler(composite_);
}

std::optional<std::pair<PixelBuffer, PerspectiveCanvas::Lift>> PerspectiveCanvas::lift(const PixelBuffer* coverage) {
  const bool masked = coverage != nullptr && coverage->width() == width() && coverage->height() == height() &&
                      coverage->format().channels == 1;
  const auto weight = [&](std::int32_t x, std::int32_t y) -> int { return masked ? coverage->pixel(x, y)[0] : 255; };
  const auto& original = std::as_const(original_);
  std::int32_t left = width();
  std::int32_t top = height();
  std::int32_t right = -1;
  std::int32_t bottom = -1;
  for (std::int32_t y = 0; y < height(); ++y) {
    for (std::int32_t x = 0; x < width(); ++x) {
      if (original.pixel(x, y)[3] != 0 && weight(x, y) != 0) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    }
  }
  if (right < left || bottom < top) {
    return std::nullopt;
  }
  Lift record;
  record.rect = Rect{left, top, right - left + 1, bottom - top + 1};
  record.before = PixelBuffer(record.rect.width, record.rect.height, PixelFormat::rgba8());
  record.after = PixelBuffer(record.rect.width, record.rect.height, PixelFormat::rgba8());
  PixelBuffer content(record.rect.width, record.rect.height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < record.rect.height; ++y) {
    for (std::int32_t x = 0; x < record.rect.width; ++x) {
      const auto* source = original.pixel(record.rect.x + x, record.rect.y + y);
      const int w = weight(record.rect.x + x, record.rect.y + y);
      std::memcpy(record.before.pixel(x, y), source, 4);
      auto* lifted = content.pixel(x, y);
      auto* left_behind = record.after.pixel(x, y);
      std::memcpy(lifted, source, 4);
      std::memcpy(left_behind, source, 4);
      lifted[3] = static_cast<std::uint8_t>((source[3] * w + 127) / 255);
      left_behind[3] = static_cast<std::uint8_t>(source[3] - lifted[3]);
    }
  }
  set_original_region(record.rect, record.after);
  return std::make_pair(std::move(content), std::move(record));
}

Rect PerspectiveCanvas::set_original_region(Rect rect, const PixelBuffer& pixels) {
  rect = intersect_rect(rect, Rect{0, 0, width(), height()});
  if (rect.empty() || pixels.width() < rect.width || pixels.height() < rect.height) {
    return {};
  }
  (void)original_.data();
  for (std::int32_t y = 0; y < rect.height; ++y) {
    std::memcpy(original_.pixel(rect.x, rect.y + y), pixels.pixel(0, y), static_cast<std::size_t>(rect.width) * 4);
  }
  refresh(rect);
  composite_sampler_.update(composite_, rect);
  return rect;
}

bool PerspectiveCanvas::original_changed() const {
  const auto current = original_.data();
  const auto initial = initial_original_.data();
  return current.size() != initial.size() || !std::equal(current.begin(), current.end(), initial.begin());
}

bool PerspectiveCanvas::has_edits() const noexcept { return cropped_edits().has_value(); }

void PerspectiveCanvas::backup_tiles(std::map<std::pair<std::int32_t, std::int32_t>, std::vector<std::uint8_t>>& backup,
                                     Rect rect) const {
  rect = intersect_rect(rect, Rect{0, 0, width(), height()});
  if (rect.empty()) {
    return;
  }
  for (std::int32_t ty = rect.y / kTileSize; ty <= (rect.y + rect.height - 1) / kTileSize; ++ty) {
    for (std::int32_t tx = rect.x / kTileSize; tx <= (rect.x + rect.width - 1) / kTileSize; ++tx) {
      const auto key = std::make_pair(tx, ty);
      if (backup.count(key) != 0) {
        continue;
      }
      const auto tile = intersect_rect(Rect{tx * kTileSize, ty * kTileSize, kTileSize, kTileSize}, Rect{0, 0, width(), height()});
      std::vector<std::uint8_t> bytes(static_cast<std::size_t>(tile.width) * tile.height * 4);
      for (std::int32_t y = 0; y < tile.height; ++y) {
        std::memcpy(bytes.data() + static_cast<std::size_t>(y) * tile.width * 4, edits_.pixel(tile.x, tile.y + y),
                    static_cast<std::size_t>(tile.width) * 4);
      }
      backup.emplace(key, std::move(bytes));
    }
  }
}

PerspectiveCanvas::Change PerspectiveCanvas::make_change(
    std::map<std::pair<std::int32_t, std::int32_t>, std::vector<std::uint8_t>> backup, Rect bounds) const {
  Change change;
  for (auto& [key, before] : backup) {
    const auto tile = intersect_rect(Rect{key.first * kTileSize, key.second * kTileSize, kTileSize, kTileSize},
                                     Rect{0, 0, width(), height()});
    std::vector<std::uint8_t> after(before.size());
    for (std::int32_t y = 0; y < tile.height; ++y) {
      std::memcpy(after.data() + static_cast<std::size_t>(y) * tile.width * 4, edits_.pixel(tile.x, tile.y + y),
                  static_cast<std::size_t>(tile.width) * 4);
    }
    if (after == before) {
      continue;
    }
    change.bounds = unite_rects(change.bounds, tile);
    change.tiles.push_back({key.first, key.second, std::move(before), std::move(after)});
  }
  if (change.tiles.empty()) {
    change.bounds = {};
  } else if (!bounds.empty()) {
    // Every changed pixel lies in `bounds`, so it tightens the tile union.
    change.bounds = intersect_rect(change.bounds, bounds);
  }
  return change;
}

void PerspectiveCanvas::write_tiles(const Change& change, bool after) {
  (void)edits_.data();
  for (const auto& tile_change : change.tiles) {
    const auto tile = intersect_rect(
        Rect{tile_change.tile_x * kTileSize, tile_change.tile_y * kTileSize, kTileSize, kTileSize},
        Rect{0, 0, width(), height()});
    const auto& bytes = after ? tile_change.after : tile_change.before;
    if (bytes.size() != static_cast<std::size_t>(tile.width) * tile.height * 4) {
      continue;
    }
    for (std::int32_t y = 0; y < tile.height; ++y) {
      std::memcpy(edits_.pixel(tile.x, tile.y + y), bytes.data() + static_cast<std::size_t>(y) * tile.width * 4,
                  static_cast<std::size_t>(tile.width) * 4);
    }
  }
}

void PerspectiveCanvas::refresh(Rect rect) {
  rect = intersect_rect(rect, Rect{0, 0, width(), height()});
  if (rect.empty()) {
    return;
  }
  (void)composite_.data();
  for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
      over_straight(edits_.pixel(x, y), original_.pixel(x, y), composite_.pixel(x, y));
    }
  }
}

PerspectiveCanvas::Change PerspectiveCanvas::paste(const PlaneGeometry& plane, const PixelBuffer& image,
                                                   const Homography& image_to_plane) {
  if (stroke_.has_value()) {
    end_stroke();
  }
  const PerspectiveSampler sampler(image);
  if (sampler.empty()) {
    return {};
  }
  const auto plane_to_target = plane.to_image;
  const auto bounds = image_draw_bounds(edits_, plane_to_target, Rect{0, 0, width(), height()}, plane, image_to_plane,
                                        image.width(), image.height());
  std::map<std::pair<std::int32_t, std::int32_t>, std::vector<std::uint8_t>> backup;
  backup_tiles(backup, bounds);
  const auto changed =
      render_perspective_image(edits_, Homography{}, Rect{0, 0, width(), height()}, plane, sampler, image_to_plane);
  auto change = make_change(std::move(backup), changed);
  refresh(change.bounds);
  composite_sampler_.update(composite_, change.bounds);
  return change;
}

void PerspectiveCanvas::begin_stroke(const PerspectiveStrokeSetup& setup) {
  if (stroke_.has_value()) {
    end_stroke();
  }
  Stroke stroke;
  stroke.setup = setup;
  stroke.setup.brush.size = std::clamp(setup.brush.size, 1.0, 5000.0);
  stroke.setup.brush.hardness = std::clamp(setup.brush.hardness, 0, 100);
  stroke.setup.brush.opacity = std::clamp(setup.brush.opacity, 1, 100);
  stroke.mask.assign(static_cast<std::size_t>(width()) * height(), 0);
  stroke_ = std::move(stroke);
}

Rect PerspectiveCanvas::dab(PlanePoint center) {
  auto& stroke = *stroke_;
  const auto& plane = stroke.setup.plane;
  const double radius = stroke.setup.brush.size * 0.5;
  // Constraint (docs/legal-constraints.md, "Perspective editing"): a dab writes only inside
  // the stroke's own plane. Its square is cut to the plane rectangle before it is mapped,
  // and every pixel's weight is multiplied by its coverage of that plane; nothing
  // continues onto a neighbouring plane.
  const double left = std::max(0.0, center.x - radius);
  const double top = std::max(0.0, center.y - radius);
  const double right = std::min(plane.width, center.x + radius);
  const double bottom = std::min(plane.height, center.y + radius);
  if (!(right > left) || !(bottom > top)) {
    return {};
  }
  std::vector<PlanePoint> corners;
  for (const auto& corner : std::array<PlanePoint, 4>{{{left, top}, {right, top}, {right, bottom}, {left, bottom}}}) {
    const auto mapped = plane.to_image.map(corner);
    if (!mapped.has_value()) {
      return {};
    }
    corners.push_back(*mapped);
  }
  const auto bounds = intersect_rect(bounding_pixels(corners), Rect{0, 0, width(), height()});
  if (bounds.empty()) {
    return {};
  }
  backup_tiles(stroke.backup, bounds);
  (void)edits_.data();
  const bool clone = stroke.setup.kind == PerspectiveStrokeKind::Clone;
  const auto& source_plane = stroke.setup.source_plane;
  const double hardness = stroke.setup.brush.hardness / 100.0;
  const std::int64_t opacity = stroke.setup.brush.opacity;
  const auto paint = premultiply(stroke.setup.color.data());
  Rect changed{};
  std::pair<std::int32_t, std::int32_t> cached_key{-1, -1};
  const std::vector<std::uint8_t>* cached_tile = nullptr;
  Rect cached_rect{};
  for (std::int32_t y = bounds.y; y < bounds.y + bounds.height; ++y) {
    for (std::int32_t x = bounds.x; x < bounds.x + bounds.width; ++x) {
      const PlanePoint pixel_center{x + 0.5, y + 0.5};
      const auto p = plane.to_plane.map(pixel_center);
      if (!p.has_value()) {
        continue;
      }
      const auto j = plane.to_plane.jacobian(pixel_center);
      const int coverage = plane_coverage(*p, j, plane.width, plane.height);
      if (coverage <= 0) {
        continue;
      }
      const double dx = p->x - center.x;
      const double dy = p->y - center.y;
      const double reach = std::sqrt(dx * dx + dy * dy);
      if (reach >= radius) {
        continue;
      }
      const double pixel_size = std::max(1.0e-6, std::max(axis_gradients(j).first, axis_gradients(j).second));
      const double feather = std::max(radius * (1.0 - hardness), std::min(radius, pixel_size));
      const double inner = radius - feather;
      double weight = 1.0;
      if (reach > inner) {
        const double t = (reach - inner) / feather;
        weight = 1.0 - t * t * (3.0 - 2.0 * t);
      }
      std::array<std::int64_t, 4> source = paint;
      if (clone) {
        const PlanePoint from{p->x + stroke.setup.source_offset.x, p->y + stroke.setup.source_offset.y};
        // The clone source is read only inside its own plane as well.
        if (!source_plane.contains(from, 0.0)) {
          continue;
        }
        const auto image_point = source_plane.to_image.map(from);
        if (!image_point.has_value()) {
          continue;
        }
        const double footprint = footprint_of(chain(source_plane.to_image.jacobian(from), j));
        const auto sample = composite_sampler_.sample(image_point->x, image_point->y, footprint);
        source = {sample[0], sample[1], sample[2], sample[3]};
      }
      const auto mask_value = static_cast<std::uint16_t>(
          std::clamp(weight * (coverage / 256.0) * 65535.0 + 0.5, 0.0, 65535.0));
      auto& stored_mask = stroke.mask[static_cast<std::size_t>(y) * width() + x];
      if (mask_value <= stored_mask) {
        continue;
      }
      stored_mask = mask_value;
      const auto key = std::make_pair(x / kTileSize, y / kTileSize);
      if (key != cached_key) {
        cached_key = key;
        cached_tile = &stroke.backup.at(key);
        cached_rect = intersect_rect(Rect{key.first * kTileSize, key.second * kTileSize, kTileSize, kTileSize},
                                     Rect{0, 0, width(), height()});
      }
      const auto* before_pixel =
          cached_tile->data() +
          (static_cast<std::size_t>(y - cached_rect.y) * cached_rect.width + (x - cached_rect.x)) * 4;
      const auto before = premultiply(before_pixel);
      const std::int64_t amount = static_cast<std::int64_t>(stored_mask) * opacity;  // 0..6553500
      std::array<std::int64_t, 4> out{};
      for (std::size_t c = 0; c < 4; ++c) {
        out[c] = before[c] + ((source[c] - before[c]) * amount) / 6553500;
      }
      auto* pixel = edits_.pixel(x, y);
      std::array<std::uint8_t, 4> stored{};
      store_premultiplied(out, stored.data());
      if (std::memcmp(stored.data(), pixel, 4) != 0) {
        std::memcpy(pixel, stored.data(), 4);
        changed = unite_rects(changed, Rect{x, y, 1, 1});
      }
    }
  }
  refresh(changed);
  stroke.bounds = unite_rects(stroke.bounds, changed);
  return changed;
}

Rect PerspectiveCanvas::stroke_to(PlanePoint plane_point) {
  if (!stroke_.has_value() || !std::isfinite(plane_point.x) || !std::isfinite(plane_point.y)) {
    return {};
  }
  auto& stroke = *stroke_;
  if (!stroke.last.has_value()) {
    stroke.last = plane_point;
    stroke.carry = 0.0;
    return dab(plane_point);
  }
  const double spacing = std::max(0.5, stroke.setup.brush.size * 0.15);
  const PlanePoint from = *stroke.last;
  const double dx = plane_point.x - from.x;
  const double dy = plane_point.y - from.y;
  const double span = std::sqrt(dx * dx + dy * dy);
  Rect changed{};
  double travelled = spacing - stroke.carry;
  // A huge jump (a far, foreshortened drag) still ends; the cap keeps one move bounded.
  int dabs = 0;
  while (travelled <= span && dabs < 4096) {
    const double t = travelled / span;
    changed = unite_rects(changed, dab({from.x + dx * t, from.y + dy * t}));
    travelled += spacing;
    ++dabs;
  }
  stroke.carry = span - (travelled - spacing);
  stroke.last = plane_point;
  return changed;
}

PerspectiveCanvas::Change PerspectiveCanvas::end_stroke() {
  if (!stroke_.has_value()) {
    return {};
  }
  auto change = make_change(std::move(stroke_->backup), stroke_->bounds);
  stroke_.reset();
  composite_sampler_.update(composite_, change.bounds);
  return change;
}

Rect PerspectiveCanvas::revert(const Change& change) {
  write_tiles(change, false);
  refresh(change.bounds);
  composite_sampler_.update(composite_, change.bounds);
  return change.bounds;
}

Rect PerspectiveCanvas::reapply(const Change& change) {
  write_tiles(change, true);
  refresh(change.bounds);
  composite_sampler_.update(composite_, change.bounds);
  return change.bounds;
}

std::optional<std::pair<PixelBuffer, Rect>> PerspectiveCanvas::cropped_edits() const {
  std::int32_t left = width();
  std::int32_t top = height();
  std::int32_t right = -1;
  std::int32_t bottom = -1;
  for (std::int32_t y = 0; y < height(); ++y) {
    const auto row = edits_.row(y);
    for (std::int32_t x = 0; x < width(); ++x) {
      if (row[static_cast<std::size_t>(x) * 4 + 3] != 0) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    }
  }
  if (right < left || bottom < top) {
    return std::nullopt;
  }
  const Rect rect{left, top, right - left + 1, bottom - top + 1};
  PixelBuffer cropped(rect.width, rect.height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < rect.height; ++y) {
    std::memcpy(cropped.pixel(0, y), edits_.pixel(rect.x, rect.y + y), static_cast<std::size_t>(rect.width) * 4);
  }
  return std::make_pair(std::move(cropped), rect);
}

PixelBuffer composite_edits_onto_layer(const PixelBuffer& layer, Rect layer_bounds, const PixelBuffer& edits,
                                       Rect edits_bounds, Rect& bounds) {
  bounds = layer.empty() ? edits_bounds : unite_rects(layer_bounds, edits_bounds);
  PixelBuffer result(std::max(1, bounds.width), std::max(1, bounds.height), PixelFormat::rgba8());
  result.clear(0);
  if (layer.format().channels == 4 && layer.format().bit_depth == BitDepth::UInt8) {
    for (std::int32_t y = 0; y < layer.height(); ++y) {
      std::memcpy(result.pixel(layer_bounds.x - bounds.x, layer_bounds.y - bounds.y + y), layer.pixel(0, y),
                  static_cast<std::size_t>(layer.width()) * 4);
    }
  }
  if (edits.format().channels == 4 && edits.format().bit_depth == BitDepth::UInt8) {
    for (std::int32_t y = 0; y < edits.height(); ++y) {
      for (std::int32_t x = 0; x < edits.width(); ++x) {
        const auto* top = edits.pixel(x, y);
        if (top[3] == 0) {
          continue;
        }
        auto* below = result.pixel(edits_bounds.x - bounds.x + x, edits_bounds.y - bounds.y + y);
        std::array<std::uint8_t, 4> out{};
        over_straight(top, below, out.data());
        std::memcpy(below, out.data(), 4);
      }
    }
  }
  return result;
}

}  // namespace patchy
