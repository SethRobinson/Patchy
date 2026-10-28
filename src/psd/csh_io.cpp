#include "psd/csh_io.hpp"

#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_io_internal.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <utility>

namespace patchy::psd {

namespace {

constexpr std::uint32_t kMaxShapes = 65536U;
constexpr std::uint32_t kMaxNameUnits = 4096U;
// Bounds are pixel extents of the defining document; anything wider than this
// is not a real shape and would overflow the fixed-point mapping.
constexpr std::int64_t kMaxExtent = 1 << 24;

std::string utf8_from_utf16_units(std::span<const std::uint8_t> bytes) {
  std::string text;
  const auto count = bytes.size() / 2U;
  for (std::size_t i = 0; i < count; ++i) {
    std::uint32_t unit = (static_cast<std::uint32_t>(bytes[2 * i]) << 8U) | bytes[2 * i + 1];
    if (unit == 0U) {
      break;
    }
    if (unit >= 0xD800U && unit <= 0xDBFFU && i + 1 < count) {
      const std::uint32_t low = (static_cast<std::uint32_t>(bytes[2 * i + 2]) << 8U) | bytes[2 * i + 3];
      if (low >= 0xDC00U && low <= 0xDFFFU) {
        unit = 0x10000U + ((unit - 0xD800U) << 10U) + (low - 0xDC00U);
        ++i;
      } else {
        unit = 0xFFFDU;
      }
    } else if (unit >= 0xD800U && unit <= 0xDFFFU) {
      unit = 0xFFFDU;
    }
    append_utf8(text, unit);
  }
  return text;
}

std::int32_t read_csh_i32(BigEndianReader& reader) {
  return static_cast<std::int32_t>(reader.read_u32());
}

// Extends [low, high] by one coordinate of a cubic segment, its endpoints and
// any interior extremum (the roots of the derivative in (0, 1)).
void extend_by_cubic(double p0, double p1, double p2, double p3, double& low, double& high) {
  const auto at = [&](double t) {
    const double u = 1.0 - t;
    return u * u * u * p0 + 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t * p3;
  };
  const auto include = [&](double value) {
    low = std::min(low, value);
    high = std::max(high, value);
  };
  include(p0);
  include(p3);
  // B'(t)/3 = a t^2 + b t + c
  const double a = -p0 + 3.0 * p1 - 3.0 * p2 + p3;
  const double b = 2.0 * (p0 - 2.0 * p1 + p2);
  const double c = p1 - p0;
  constexpr double kEpsilon = 1e-12;
  if (std::abs(a) < kEpsilon) {
    if (std::abs(b) >= kEpsilon) {
      const double t = -c / b;
      if (t > 0.0 && t < 1.0) include(at(t));
    }
    return;
  }
  const double discriminant = b * b - 4.0 * a * c;
  if (discriminant < 0.0) {
    return;
  }
  const double root = std::sqrt(discriminant);
  for (const double t : {(-b + root) / (2.0 * a), (-b - root) / (2.0 * a)}) {
    if (t > 0.0 && t < 1.0) include(at(t));
  }
}

struct CurveBounds {
  double left{0.0};
  double top{0.0};
  double right{0.0};
  double bottom{0.0};
};

std::optional<CurveBounds> curve_bounds(const VectorPath& path) {
  bool any = false;
  CurveBounds bounds;
  for (const auto& subpath : path.subpaths) {
    const auto count = subpath.anchors.size();
    if (count == 0U) {
      continue;
    }
    if (!any) {
      bounds = {subpath.anchors[0].anchor_x, subpath.anchors[0].anchor_y, subpath.anchors[0].anchor_x,
                subpath.anchors[0].anchor_y};
      any = true;
    }
    bounds.left = std::min(bounds.left, subpath.anchors[0].anchor_x);
    bounds.right = std::max(bounds.right, subpath.anchors[0].anchor_x);
    bounds.top = std::min(bounds.top, subpath.anchors[0].anchor_y);
    bounds.bottom = std::max(bounds.bottom, subpath.anchors[0].anchor_y);
    const auto segments = subpath.closed ? count : count - 1U;
    for (std::size_t i = 0; i < segments; ++i) {
      const auto& from = subpath.anchors[i];
      const auto& to = subpath.anchors[(i + 1U) % count];
      extend_by_cubic(from.anchor_x, from.out_x, to.in_x, to.anchor_x, bounds.left, bounds.right);
      extend_by_cubic(from.anchor_y, from.out_y, to.in_y, to.anchor_y, bounds.top, bounds.bottom);
    }
  }
  if (!any) {
    return std::nullopt;
  }
  return bounds;
}

// Decodes one shape block; nullopt when its geometry is unusable.
std::optional<CshShape> read_shape_block(std::span<const std::uint8_t> block) {
  BigEndianReader reader(block);
  const auto id_length = reader.read_u8();
  const auto id_bytes = reader.read_span(id_length);
  CshShape shape;
  shape.id.assign(id_bytes.begin(), id_bytes.end());
  const auto top = read_csh_i32(reader);
  const auto left = read_csh_i32(reader);
  const auto bottom = read_csh_i32(reader);
  const auto right = read_csh_i32(reader);
  const auto width = static_cast<std::int64_t>(right) - left;
  const auto height = static_cast<std::int64_t>(bottom) - top;
  if (width <= 0 || height <= 0 || width > kMaxExtent || height > kMaxExtent) {
    return std::nullopt;
  }
  auto path = parse_path_resource_records(block.subspan(reader.position()), static_cast<std::int32_t>(width),
                                          static_cast<std::int32_t>(height));
  if (!path.has_value()) {
    return std::nullopt;
  }
  std::erase_if(path->subpaths, [](const PathSubpath& subpath) { return subpath.anchors.empty(); });
  const auto bounds = curve_bounds(*path);
  if (!bounds.has_value()) {
    return std::nullopt;
  }
  const double extent = std::max(bounds->right - bounds->left, bounds->bottom - bounds->top);
  if (!(extent > 1e-9)) {
    return std::nullopt;
  }
  const double scale = 1.0 / extent;
  transform_vector_path(*path, {scale, 0.0, 0.0, scale, -bounds->left * scale, -bounds->top * scale});
  shape.path = std::move(*path);
  return shape;
}

}  // namespace

std::optional<CshReadResult> read_csh(std::span<const std::uint8_t> bytes, std::string& error) {
  constexpr std::array<std::uint8_t, 4> kMagic{'c', 'u', 's', 'h'};
  if (bytes.size() < 12U || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    error = PATCHY_TRANSLATE_NOOP("QObject", "Not a Photoshop custom shapes file");
    return std::nullopt;
  }
  BigEndianReader reader(bytes);
  reader.skip(4);
  const auto version = reader.read_u32();
  if (version != 2U) {
    error = PATCHY_TRANSLATE_NOOP("QObject", "Unsupported custom shapes file version");
    return std::nullopt;
  }
  const auto count = reader.read_u32();
  CshReadResult result;
  bool damaged_tail = false;
  for (std::uint32_t index = 0; index < std::min(count, kMaxShapes); ++index) {
    try {
      const auto name_units = reader.read_u32();
      if (name_units > kMaxNameUnits) {
        damaged_tail = true;
        break;
      }
      const auto name = utf8_from_utf16_units(reader.read_span(static_cast<std::size_t>(name_units) * 2U));
      reader.skip((4U - reader.position() % 4U) % 4U);
      (void)reader.read_u32();  // always 1 in the files seen
      const auto length = reader.read_u32();
      const auto block = reader.read_span(length);
      std::optional<CshShape> shape;
      try {
        shape = read_shape_block(block);
      } catch (const std::exception&) {
        shape.reset();
      }
      if (!shape.has_value()) {
        result.warnings.emplace_back(
            PATCHY_TRANSLATE_NOOP("QObject", "Skipped a shape with damaged or empty geometry"));
        continue;
      }
      shape->name = name;
      result.shapes.push_back(std::move(*shape));
    } catch (const std::exception&) {
      damaged_tail = true;
      break;
    }
  }
  if (damaged_tail || count > kMaxShapes) {
    result.warnings.emplace_back(PATCHY_TRANSLATE_NOOP("QObject", "The file is damaged past the last decoded shape"));
  }
  if (result.shapes.empty()) {
    error = PATCHY_TRANSLATE_NOOP("QObject", "The file holds no usable shapes");
    return std::nullopt;
  }
  return result;
}

}  // namespace patchy::psd
