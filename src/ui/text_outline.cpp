#include "ui/text_outline.hpp"

#include "core/layer_metadata.hpp"
#include "core/vector_raster.hpp"

#include <QImage>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPen>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <utility>

namespace patchy::ui {

class OutlineRecordingEngine final : public QPaintEngine {
 public:
  // Every feature, so QPainter hands over untransformed primitives (PrimitiveTransform) and
  // never emulates anything through a raster. Text items are left to QPaintEngine's default
  // drawTextItem, which turns the glyphs into one outline path and fills it through drawPath.
  OutlineRecordingEngine() : QPaintEngine(QPaintEngine::AllFeatures) {}

  bool begin(QPaintDevice*) override { return true; }
  bool end() override { return true; }
  [[nodiscard]] Type type() const override { return QPaintEngine::User; }

  void updateState(const QPaintEngineState& changed) override {
    const auto flags = changed.state();
    if (flags.testFlag(QPaintEngine::DirtyTransform)) {
      transform_ = changed.transform();
    }
    if (flags.testFlag(QPaintEngine::DirtyPen)) {
      pen_ = changed.pen();
    }
    if (flags.testFlag(QPaintEngine::DirtyBrush)) {
      brush_ = changed.brush();
    }
  }

  void drawPath(const QPainterPath& path) override { record(path, false); }

  using QPaintEngine::drawPolygon;
  void drawPolygon(const QPointF* points, int count, PolygonDrawMode mode) override {
    if (points == nullptr || count < 2) {
      return;
    }
    QPolygonF polygon;
    polygon.reserve(count);
    for (int index = 0; index < count; ++index) {
      polygon.push_back(points[index]);
    }
    QPainterPath path;
    path.addPolygon(polygon);
    if (mode != PolylineMode) {
      path.closeSubpath();
    }
    path.setFillRule(mode == WindingMode ? Qt::WindingFill : Qt::OddEvenFill);
    record(path, mode == PolylineMode);
  }

  void drawPixmap(const QRectF&, const QPixmap&, const QRectF&) override { ++skipped_images_; }
  void drawImage(const QRectF&, const QImage&, const QRectF&, Qt::ImageConversionFlags) override {
    ++skipped_images_;
  }
  void drawTiledPixmap(const QRectF&, const QPixmap&, const QPointF&) override { ++skipped_images_; }

  std::vector<TextOutlinePaint> paints;
  int skipped_images_{0};

 private:
  void record(const QPainterPath& path, bool outline_only) {
    if (path.isEmpty()) {
      return;
    }
    if (!outline_only && brush_.style() != Qt::NoBrush && brush_.color().alpha() > 0) {
      auto mapped = transform_.map(path);
      mapped.setFillRule(path.fillRule());
      paints.push_back(TextOutlinePaint{std::move(mapped), brush_.color()});
    }
    // A cosmetic pen is a hairline of no document width: nothing to keep.
    if (pen_.style() != Qt::NoPen && !pen_.isCosmetic() && pen_.widthF() > 0.0 && pen_.color().alpha() > 0) {
      QPainterPathStroker stroker(pen_);
      auto stroke = transform_.map(stroker.createStroke(path));
      stroke.setFillRule(Qt::WindingFill);
      paints.push_back(TextOutlinePaint{std::move(stroke), pen_.color()});
    }
  }

  QTransform transform_;
  QPen pen_{Qt::NoPen};
  QBrush brush_{Qt::NoBrush};
};

OutlineRecordingDevice::OutlineRecordingDevice() : engine_(std::make_unique<OutlineRecordingEngine>()) {}

OutlineRecordingDevice::~OutlineRecordingDevice() = default;

QPaintEngine* OutlineRecordingDevice::paintEngine() const { return engine_.get(); }

const std::vector<TextOutlinePaint>& OutlineRecordingDevice::paints() const { return engine_->paints; }

int OutlineRecordingDevice::skipped_images() const { return engine_->skipped_images_; }

int OutlineRecordingDevice::metric(PaintDeviceMetric metric) const {
  // The resolution a QImage reports, so fonts resolve on this device as on the raster the
  // text normally renders into.
  static const int dpi = QImage(1, 1, QImage::Format_ARGB32_Premultiplied).logicalDpiX();
  constexpr int kExtent = 1 << 20;
  switch (metric) {
    case PdmWidth:
    case PdmHeight:
      return kExtent;
    case PdmWidthMM:
    case PdmHeightMM:
      return static_cast<int>(std::lround(kExtent * 25.4 / std::max(1, dpi)));
    case PdmNumColors:
      return std::numeric_limits<int>::max();
    case PdmDepth:
      return 32;
    case PdmDpiX:
    case PdmDpiY:
    case PdmPhysicalDpiX:
    case PdmPhysicalDpiY:
      return dpi;
    default:
      return QPaintDevice::metric(metric);
  }
}

namespace {

using Cubic = std::array<QPointF, 4>;

QPointF cubic_point(const Cubic& c, double t) {
  const double u = 1.0 - t;
  return c[0] * (u * u * u) + c[1] * (3.0 * u * u * t) + c[2] * (3.0 * u * t * t) + c[3] * (t * t * t);
}

QPointF cubic_derivative(const Cubic& c, double t) {
  const double u = 1.0 - t;
  return (c[1] - c[0]) * (3.0 * u * u) + (c[2] - c[1]) * (6.0 * u * t) + (c[3] - c[2]) * (3.0 * t * t);
}

double length_of(QPointF v) { return std::hypot(v.x(), v.y()); }

bool same_point(QPointF a, QPointF b) { return std::abs(a.x() - b.x()) < 1e-9 && std::abs(a.y() - b.y()) < 1e-9; }

// True when both handles sit on the chord between the ends (an affine-mapped line).
bool cubic_is_straight(const Cubic& c) {
  const auto chord = c[3] - c[0];
  const double chord_length = length_of(chord);
  if (chord_length < 1e-9) {
    return same_point(c[1], c[0]) && same_point(c[2], c[0]);
  }
  for (const auto& handle : {c[1], c[2]}) {
    const auto offset = handle - c[0];
    const double cross = (chord.x() * offset.y() - chord.y() * offset.x()) / chord_length;
    const double along = (chord.x() * offset.x() + chord.y() * offset.y()) / chord_length;
    if (std::abs(cross) > 1e-6 || along < -1e-6 || along > chord_length + 1e-6) {
      return false;
    }
  }
  return true;
}

struct Contour {
  std::vector<PathAnchor> anchors;
  QPolygonF polygon;
  QRectF rect;
  double area{0.0};  // signed (shoelace, y down)
  int parent{-1};
  bool kept{true};
  int group_root{-1};
};

QPointF anchor_point(const PathAnchor& anchor) { return {anchor.anchor_x, anchor.anchor_y}; }

PathAnchor corner_anchor(QPointF point) {
  PathAnchor anchor;
  anchor.anchor_x = anchor.in_x = anchor.out_x = point.x();
  anchor.anchor_y = anchor.in_y = anchor.out_y = point.y();
  return anchor;
}

// A smooth knot: both handles pulled out and pointing in opposite directions.
void mark_smooth(PathAnchor& anchor) {
  const QPointF in(anchor.in_x - anchor.anchor_x, anchor.in_y - anchor.anchor_y);
  const QPointF out(anchor.out_x - anchor.anchor_x, anchor.out_y - anchor.anchor_y);
  const double in_length = length_of(in);
  const double out_length = length_of(out);
  if (in_length < 1e-6 || out_length < 1e-6) {
    anchor.smooth = false;
    return;
  }
  const double cross = (in.x() * out.y() - in.y() * out.x()) / (in_length * out_length);
  const double dot = (in.x() * out.x() + in.y() * out.y()) / (in_length * out_length);
  anchor.smooth = dot < 0.0 && std::abs(cross) < 0.02;
}

void finish_contour(std::vector<PathAnchor>& anchors, std::vector<Contour>& contours) {
  if (anchors.size() >= 2 && same_point(anchor_point(anchors.front()), anchor_point(anchors.back()))) {
    anchors.front().in_x = anchors.back().in_x;
    anchors.front().in_y = anchors.back().in_y;
    anchors.pop_back();
  }
  if (anchors.size() < 2) {
    anchors.clear();
    return;
  }
  QPainterPath closed;
  closed.moveTo(anchor_point(anchors.front()));
  for (std::size_t index = 0; index < anchors.size(); ++index) {
    const auto& from = anchors[index];
    const auto& to = anchors[(index + 1) % anchors.size()];
    closed.cubicTo(QPointF(from.out_x, from.out_y), QPointF(to.in_x, to.in_y), anchor_point(to));
  }
  Contour contour;
  contour.polygon = closed.toFillPolygon();
  double twice_area = 0.0;
  for (qsizetype index = 0; index < contour.polygon.size(); ++index) {
    const auto& a = contour.polygon[index];
    const auto& b = contour.polygon[(index + 1) % contour.polygon.size()];
    twice_area += a.x() * b.y() - b.x() * a.y();
  }
  contour.area = twice_area / 2.0;
  if (std::abs(contour.area) < 1e-6) {
    anchors.clear();
    return;
  }
  contour.rect = contour.polygon.boundingRect();
  for (auto& anchor : anchors) {
    mark_smooth(anchor);
  }
  contour.anchors = std::move(anchors);
  anchors.clear();
  contours.push_back(std::move(contour));
}

std::vector<Contour> contours_of(const QPainterPath& path) {
  std::vector<Contour> contours;
  std::vector<PathAnchor> anchors;
  QPointF current;
  const auto append_segment = [&](const Cubic& segment) {
    if (anchors.empty()) {
      anchors.push_back(corner_anchor(segment[0]));
    }
    if (cubic_is_straight(segment)) {
      if (!same_point(segment[3], segment[0])) {
        anchors.push_back(corner_anchor(segment[3]));
      }
      return;
    }
    anchors.back().out_x = segment[1].x();
    anchors.back().out_y = segment[1].y();
    auto end = corner_anchor(segment[3]);
    end.in_x = segment[2].x();
    end.in_y = segment[2].y();
    anchors.push_back(end);
  };
  for (int index = 0; index < path.elementCount(); ++index) {
    const auto element = path.elementAt(index);
    const QPointF point(element.x, element.y);
    if (element.isMoveTo()) {
      finish_contour(anchors, contours);
      current = point;
      anchors.push_back(corner_anchor(point));
    } else if (element.isLineTo()) {
      append_segment(Cubic{current, current, point, point});
      current = point;
    } else if (element.isCurveTo() && index + 2 < path.elementCount()) {
      const auto c2 = path.elementAt(index + 1);
      const auto end = path.elementAt(index + 2);
      const QPointF end_point(end.x, end.y);
      append_segment(Cubic{current, point, QPointF(c2.x, c2.y), end_point});
      current = end_point;
      index += 2;
    }
  }
  finish_contour(anchors, contours);
  return contours;
}

// `inner` lies inside `outer`: its box fits and sample points of its outline are inside.
bool contour_contains(const Contour& outer, const Contour& inner) {
  if (!outer.rect.contains(inner.rect) || std::abs(outer.area) <= std::abs(inner.area)) {
    return false;
  }
  const auto count = inner.polygon.size();
  for (const auto sample : {qsizetype{0}, count / 3, (2 * count) / 3}) {
    if (!outer.polygon.containsPoint(inner.polygon[sample], Qt::OddEvenFill)) {
      return false;
    }
  }
  return true;
}

}  // namespace

QPainterPath map_outline_path(const QPainterPath& path, const std::function<QPointF(QPointF)>& map,
                              double max_piece_length) {
  QPainterPath mapped;
  mapped.setFillRule(path.fillRule());
  max_piece_length = std::max(0.5, max_piece_length);
  // The map's derivative along `direction` at `point`, by central differences.
  const auto derivative = [&map](QPointF point, QPointF direction) {
    const double length = length_of(direction);
    if (length < 1e-12) {
      return QPointF();
    }
    const double step = 0.05 / length;
    return (map(point + direction * step) - map(point - direction * step)) / (2.0 * step);
  };
  QPointF current;
  for (int index = 0; index < path.elementCount(); ++index) {
    const auto element = path.elementAt(index);
    const QPointF point(element.x, element.y);
    Cubic segment;
    if (element.isMoveTo()) {
      current = point;
      mapped.moveTo(map(point));
      continue;
    }
    if (element.isLineTo()) {
      if (same_point(point, current)) {
        continue;
      }
      segment = Cubic{current, current + (point - current) / 3.0, current + (point - current) * (2.0 / 3.0), point};
    } else if (element.isCurveTo() && index + 2 < path.elementCount()) {
      const auto c2 = path.elementAt(index + 1);
      const auto end = path.elementAt(index + 2);
      segment = Cubic{current, point, QPointF(c2.x, c2.y), QPointF(end.x, end.y)};
      index += 2;
    } else {
      continue;
    }
    const double hull = length_of(segment[1] - segment[0]) + length_of(segment[2] - segment[1]) +
                        length_of(segment[3] - segment[2]);
    const int pieces = std::clamp(static_cast<int>(std::ceil(hull / max_piece_length)), 1, 64);
    auto start = map(segment[0]);
    for (int piece = 0; piece < pieces; ++piece) {
      const double t0 = static_cast<double>(piece) / pieces;
      const double t1 = static_cast<double>(piece + 1) / pieces;
      const auto p0 = cubic_point(segment, t0);
      const auto p1 = cubic_point(segment, t1);
      const auto d0 = cubic_derivative(segment, t0) * (t1 - t0);
      const auto d1 = cubic_derivative(segment, t1) * (t1 - t0);
      const auto end = map(p1);
      mapped.cubicTo(start + derivative(p0, d0) / 3.0, end - derivative(p1, d1) / 3.0, end);
      start = end;
    }
    current = segment[3];
  }
  return mapped;
}

void append_outline_subpaths(const QPainterPath& path, VectorPath& target, std::vector<std::int32_t>* groups) {
  auto contours = contours_of(path);
  if (contours.empty()) {
    return;
  }
  // Parents: the smallest contour that holds each one (larger contours first).
  std::vector<int> by_size(contours.size());
  for (std::size_t index = 0; index < contours.size(); ++index) {
    by_size[index] = static_cast<int>(index);
  }
  std::stable_sort(by_size.begin(), by_size.end(), [&contours](int a, int b) {
    return std::abs(contours[static_cast<std::size_t>(a)].area) > std::abs(contours[static_cast<std::size_t>(b)].area);
  });
  std::vector<int> winding_inside(contours.size(), 0);
  for (std::size_t position = 0; position < by_size.size(); ++position) {
    auto& contour = contours[static_cast<std::size_t>(by_size[position])];
    for (std::size_t earlier = position; earlier-- > 0;) {
      if (contour_contains(contours[static_cast<std::size_t>(by_size[earlier])], contour)) {
        contour.parent = by_size[earlier];
        break;
      }
    }
    // Nonzero winding just outside and just inside this contour (parents come first).
    const int outside = contour.parent >= 0 ? winding_inside[static_cast<std::size_t>(contour.parent)] : 0;
    const int inside = outside + (contour.area > 0.0 ? 1 : -1);
    winding_inside[static_cast<std::size_t>(by_size[position])] = inside;
    // Only a contour where the fill starts or stops is an edge of the area.
    contour.kept = (inside != 0) != (outside != 0);
  }
  // Each kept contour joins the group of its outermost kept ancestor.
  for (auto& contour : contours) {
    if (!contour.kept) {
      continue;
    }
    int root = -1;
    for (int ancestor = contour.parent; ancestor >= 0; ancestor = contours[static_cast<std::size_t>(ancestor)].parent) {
      if (contours[static_cast<std::size_t>(ancestor)].kept) {
        root = ancestor;
      }
    }
    contour.group_root = root;
  }
  // Groups in outline order, each lead followed by its contours (the PSD writer needs a group's
  // records together).
  std::vector<std::vector<std::size_t>> members(contours.size());
  for (std::size_t index = 0; index < contours.size(); ++index) {
    if (contours[index].kept && contours[index].group_root >= 0) {
      members[static_cast<std::size_t>(contours[index].group_root)].push_back(index);
    }
  }
  auto next_group = target.next_shape_group();
  for (std::size_t index = 0; index < contours.size(); ++index) {
    auto& lead = contours[index];
    if (!lead.kept || lead.group_root >= 0) {
      continue;
    }
    const auto group = next_group++;
    if (groups != nullptr) {
      groups->push_back(group);
    }
    const auto append = [&target, group](Contour& contour) {
      PathSubpath subpath;
      subpath.anchors = std::move(contour.anchors);
      subpath.closed = true;
      subpath.op = PathCombineOp::Add;
      subpath.shape_group = group;
      target.subpaths.push_back(std::move(subpath));
    };
    append(lead);
    for (const auto member : members[index]) {
      append(contours[member]);
    }
  }
}

VectorPath vector_path_from_text_outlines(const std::vector<TextOutlinePaint>& paints) {
  VectorPath path;
  for (const auto& paint : paints) {
    append_outline_subpaths(paint.path, path, nullptr);
  }
  return path;
}

std::optional<VectorShapeContent> shape_content_from_text_outlines(const std::vector<TextOutlinePaint>& paints) {
  VectorShapeContent content;
  std::vector<std::pair<QRgb, std::vector<std::int32_t>>> colors;
  for (const auto& paint : paints) {
    std::vector<std::int32_t> groups;
    append_outline_subpaths(paint.path, content.path, &groups);
    if (groups.empty()) {
      continue;
    }
    const auto rgb = paint.color.rgb();
    auto found = std::find_if(colors.begin(), colors.end(), [rgb](const auto& entry) { return entry.first == rgb; });
    if (found == colors.end()) {
      colors.emplace_back(rgb, std::vector<std::int32_t>{});
      found = std::prev(colors.end());
    }
    found->second.insert(found->second.end(), groups.begin(), groups.end());
  }
  if (content.path.subpaths.empty()) {
    return std::nullopt;
  }
  const auto solid = [](QRgb rgb) {
    VectorFill fill;
    fill.kind = VectorFillKind::Solid;
    fill.color = RgbColor{static_cast<std::uint8_t>(qRed(rgb)), static_cast<std::uint8_t>(qGreen(rgb)),
                          static_cast<std::uint8_t>(qBlue(rgb))};
    return fill;
  };
  content.fill = solid(colors.front().first);
  content.stroke.enabled = false;
  if (colors.size() > 1U) {
    for (const auto& [rgb, groups] : colors) {
      VectorShapePart part;
      part.groups = groups;
      part.fill = solid(rgb);
      part.stroke.enabled = false;
      content.parts.push_back(std::move(part));
    }
  }
  return content;
}

void convert_text_layer_to_shape(Layer& layer, VectorShapeContent content, Rect canvas, const PatternStore* patterns) {
  clear_layer_text_metadata(layer);
  layer.set_vector_shape(std::move(content));
  layer.metadata()[kLayerMetadataVectorShape] = "1";
  layer.metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
  mark_layer_vector_block_dirty(layer);
  update_vector_shape_raster(layer, canvas, patterns);
}

}  // namespace patchy::ui
