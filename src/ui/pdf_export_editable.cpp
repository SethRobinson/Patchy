#include "ui/pdf_export.hpp"

#include "core/layer_metadata.hpp"
#include "core/pattern_resource.hpp"
#include "core/vector_shape.hpp"
#include "formats/document_flatten.hpp"
#include "formats/pdf_text_merge.hpp"
#include "formats/vector_export_plan.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/text_layer_painter.hpp"

#include <QBrush>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QLinearGradient>
#include <QList>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPen>
#include <QRadialGradient>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Editable PDF export: the layer stack walked bottom-up through QPainter on QPdfWriter,
// keeping what Qt's PDF engine can express per object (paths with fills, strokes, and
// clips; linear/radial gradients; tiled pattern fills; images with soft masks; real text
// with embedded fonts; constant opacity) and flattening the rest through the real
// compositor into image chunks, each loss reported as a notice. The walk, the barrier
// rule, and the shape representability checks are the shared vector export plan
// (formats/vector_export_plan.hpp) that SVG export uses too; the PDF-only differences are
// that Qt's PDF engine writes no blend modes, transparency groups, or soft masks, so every
// non-Normal blend is a barrier, a group with opacity rasterizes whole, and a raster mask
// on a vector or text layer sends the layer through the compositor.
//
// Ordering: Patchy's layers()[0] is the bottom layer and PDF paints first-drawn first, so
// the walk is forward with no reversal.
namespace patchy::ui::pdf_detail {
namespace {

using vector_export::Unit;

constexpr double kEpsilon = 1e-10;

bool straight_segment(const PathAnchor& from, const PathAnchor& to) noexcept {
  return std::abs(from.out_x - from.anchor_x) < kEpsilon && std::abs(from.out_y - from.anchor_y) < kEpsilon &&
         std::abs(to.in_x - to.anchor_x) < kEpsilon && std::abs(to.in_y - to.anchor_y) < kEpsilon;
}

// Even-odd within a path is Patchy's exact within-group rule (docs/vector-tools.md).
QPainterPath painter_path(const VectorPath& path) {
  QPainterPath result;
  result.setFillRule(Qt::OddEvenFill);
  for (const auto& subpath : path.subpaths) {
    if (subpath.anchors.empty()) {
      continue;
    }
    const auto segment = [&result](const PathAnchor& from, const PathAnchor& to) {
      if (straight_segment(from, to)) {
        result.lineTo(to.anchor_x, to.anchor_y);
      } else {
        result.cubicTo(from.out_x, from.out_y, to.in_x, to.in_y, to.anchor_x, to.anchor_y);
      }
    };
    result.moveTo(subpath.anchors.front().anchor_x, subpath.anchors.front().anchor_y);
    for (std::size_t i = 1; i < subpath.anchors.size(); ++i) {
      segment(subpath.anchors[i - 1], subpath.anchors[i]);
    }
    if (subpath.closed && subpath.anchors.size() > 1) {
      if (!straight_segment(subpath.anchors.back(), subpath.anchors.front())) {
        segment(subpath.anchors.back(), subpath.anchors.front());
      }
      result.closeSubpath();
    }
  }
  return result;
}

QColor qcolor(RgbColor color, float opacity = 1.0F) {
  return QColor(color.red, color.green, color.blue, std::clamp(static_cast<int>(std::lround(opacity * 255.0F)), 0, 255));
}

bool opacity_is_full(double value) { return std::abs(value - 1.0) < 0.0001; }

struct Writer {
  const Document& document;
  std::vector<std::string>* notices{};
  QPainter& painter;

  void notice(std::string value) {
    if (notices != nullptr && std::find(notices->begin(), notices->end(), value) == notices->end()) {
      notices->push_back(std::move(value));
    }
  }

  // --- paint ---

  QBrush gradient_brush(const LayerStyleGradient& gradient, const VectorPath& path, double opacity_scale) const {
    const auto geometry = vector_export::gradient_export_geometry(gradient, path, document.width(), document.height());
    const auto stops = vector_export::gradient_export_stops(gradient);
    const auto apply_stops = [&stops, opacity_scale](QGradient& target) {
      for (const auto& stop : stops) {
        target.setColorAt(stop.offset, qcolor(stop.color, static_cast<float>(stop.opacity * opacity_scale)));
      }
    };
    if (gradient.type == LayerStyleGradientType::Radial) {
      QRadialGradient radial(geometry.center_x, geometry.center_y, geometry.radius);
      apply_stops(radial);
      return QBrush(radial);
    }
    QLinearGradient linear(geometry.x1, geometry.y1, geometry.x2, geometry.y2);
    if (geometry.reflected) {
      linear.setSpread(QGradient::ReflectSpread);
    }
    apply_stops(linear);
    return QBrush(linear);
  }

  // A texture brush tiles the image at its pixel size in the painter's logical space;
  // the fill's scale, angle, and phase ride the brush transform (the same anchoring SVG
  // export uses: document origin, layer-linked placement approximated).
  QBrush pattern_brush(const VectorFill& fill) {
    const auto* resource = document.metadata().patterns.find(fill.pattern_id);
    if (resource == nullptr || resource->tile.empty() || pattern_tile_is_unrenderable(resource->tile)) {
      notice("A pattern fill's tile was missing and exported as gray");
      return QBrush(QColor(128, 128, 128));
    }
    if (fill.pattern_linked) {
      notice("A layer-linked pattern fill was exported anchored to the document origin");
    }
    QBrush brush(qimage_from_pixel_buffer(resource->tile));
    QTransform transform;
    transform.translate(fill.pattern_phase_x, fill.pattern_phase_y);
    transform.rotate(fill.pattern_angle_degrees);
    const double scale = std::clamp(fill.pattern_scale, 0.01, 100.0);
    transform.scale(scale, scale);
    brush.setTransform(transform);
    return brush;
  }

  QBrush brush_for(const VectorFill& fill, const VectorPath& path, double opacity_scale = 1.0) {
    switch (fill.kind) {
      case VectorFillKind::None:
        return QBrush(Qt::NoBrush);
      case VectorFillKind::Solid:
        return QBrush(qcolor(fill.color, static_cast<float>(opacity_scale)));
      case VectorFillKind::Gradient:
        return gradient_brush(fill.gradient, path, opacity_scale);
      case VectorFillKind::Pattern:
        return pattern_brush(fill);
    }
    return QBrush(Qt::NoBrush);
  }

  // `width_multiplier` is 2 for the inside/outside alignments, which render a
  // double-width stroke clipped or under-filled back to one half. Dash entries are
  // stored in width multiples and QPen measures its pattern in pen widths, so a doubled
  // pen halves them.
  QPen pen_for(const VectorStroke& stroke, const VectorPath& path, double width_multiplier) {
    QPen pen(brush_for(stroke.content, path, stroke.opacity), stroke.width * width_multiplier);
    pen.setCapStyle(stroke.cap == VectorStrokeCap::Round    ? Qt::RoundCap
                    : stroke.cap == VectorStrokeCap::Square ? Qt::SquareCap
                                                            : Qt::FlatCap);
    pen.setJoinStyle(stroke.join == VectorStrokeJoin::Round   ? Qt::RoundJoin
                     : stroke.join == VectorStrokeJoin::Bevel ? Qt::BevelJoin
                                                              : Qt::MiterJoin);
    // Patchy (like SVG) limits the miter by tip distance over HALF the width; Qt measures
    // it in whole pen widths.
    pen.setMiterLimit(std::max(stroke.miter_limit, 1.0) / 2.0);
    if (!stroke.dashes.empty()) {
      QList<qreal> pattern;
      for (const double dash : stroke.dashes) {
        pattern.push_back(std::max(0.0, dash) / width_multiplier);
      }
      if (pattern.size() % 2 != 0) {
        pattern.append(pattern);  // an odd list repeats, the SVG rule
      }
      pen.setDashPattern(pattern);
      pen.setDashOffset(stroke.dash_offset / width_multiplier);
    }
    return pen;
  }

  // --- clips ---

  QPainterPath vector_mask_clip(const LayerVectorMask& mask) const {
    QPainterPath clip = painter_path(mask.path);
    if (mask.inverted) {
      QPainterPath inverted;
      inverted.setFillRule(Qt::OddEvenFill);
      inverted.addRect(0.0, 0.0, static_cast<qreal>(document.width()), static_cast<qreal>(document.height()));
      inverted.addPath(clip);
      return inverted;
    }
    return clip;
  }

  void apply_vector_mask(const Layer& layer) {
    if (const auto* mask = layer.vector_mask(); mask != nullptr && !mask->disabled) {
      painter.setClipPath(vector_mask_clip(*mask), Qt::IntersectClip);
    }
  }

  // --- representability (the PDF-only additions to the shared checks) ---

  static bool raster_mask_applies(const Layer& layer) {
    return layer.mask().has_value() && !layer.mask()->disabled && !layer.mask()->pixels.empty();
  }

  bool vector_representable(const Layer& layer) const {
    if (!vector_export::shape_layer_exportable_as_vector(layer, document.metadata().patterns)) {
      return false;
    }
    const auto& shape = *layer.vector_shape();
    if (shape.stroke.enabled && shape.stroke.content.kind == VectorFillKind::Pattern) {
      return false;  // a pattern pen has no clean opacity/alignment story; bake it
    }
    return !raster_mask_applies(layer);  // Qt's PDF engine writes no soft masks
  }

  bool text_representable(const Layer& layer) const {
    if (!layer_is_text(layer) || !layer.layer_style().empty() || !opacity_is_full(layer.fill_opacity())) {
      return false;
    }
    if (const auto* mask = layer.vector_mask();
        mask != nullptr && (mask->disabled || mask->density != 255 || mask->feather > 0.0001)) {
      return false;
    }
    return !raster_mask_applies(layer);
  }

  bool group_representable(const Layer& group) const {
    // No transparency groups in Qt's PDF engine: a group's own opacity or blend cannot
    // wrap its children, so those groups bake whole.
    return vector_export::group_exportable(group) && opacity_is_full(group.opacity()) &&
           (group.blend_mode() == BlendMode::Normal || group.blend_mode() == BlendMode::PassThrough) &&
           !raster_mask_applies(group);
  }

  static bool blend_expressible(BlendMode mode) { return mode == BlendMode::Normal; }

  // --- emission ---

  void emit_vector_layer(const Layer& layer) {
    const auto& shape = *layer.vector_shape();
    painter.save();
    painter.setOpacity(layer.opacity());
    apply_vector_mask(layer);

    std::vector<QPainterPath> paths;
    if (shape.path.empty()) {
      // Fill layer: the empty path covers the whole canvas.
      QPainterPath canvas;
      canvas.addRect(0.0, 0.0, static_cast<qreal>(document.width()), static_cast<qreal>(document.height()));
      paths.push_back(canvas);
    } else if (vector_export::classify_combine(shape.path) == vector_export::CombineExport::SeparatePaths) {
      // Overlapping Add groups: one path per group, exact because the paint is opaque.
      for (const auto& group : vector_export::split_shape_groups(shape.path)) {
        paths.push_back(painter_path(group));
      }
    } else {
      paths.push_back(painter_path(shape.path));
    }

    const QBrush fill =
        shape.stroke.enabled && !shape.stroke.fill_enabled ? QBrush(Qt::NoBrush) : brush_for(shape.fill, shape.path);
    const bool stroked = shape.stroke.enabled && shape.stroke.width > 0.0;
    for (const auto& path : paths) {
      if (stroked && shape.stroke.alignment == VectorStrokeAlignment::Outside) {
        // The outside half: a double-width stroke under an opaque fill (representability
        // guarantees the fill covers the inner half).
        painter.strokePath(path, pen_for(shape.stroke, shape.path, 2.0));
      }
      if (fill.style() != Qt::NoBrush) {
        painter.fillPath(path, fill);
      }
      if (stroked && shape.stroke.alignment == VectorStrokeAlignment::Center) {
        painter.strokePath(path, pen_for(shape.stroke, shape.path, 1.0));
      } else if (stroked && shape.stroke.alignment == VectorStrokeAlignment::Inside) {
        painter.save();
        painter.setClipPath(path, Qt::IntersectClip);
        painter.strokePath(path, pen_for(shape.stroke, shape.path, 2.0));
        painter.restore();
      }
    }
    painter.restore();
  }

  bool emit_text_layer(const Layer& layer) {
    painter.save();
    painter.setOpacity(layer.opacity());
    apply_vector_mask(layer);
    const bool drawn = draw_text_layer_to_painter(layer, painter);
    painter.restore();
    return drawn;
  }

  // Flattens `layers` (bottom..top slice of one sibling list) through the real
  // compositor into one cropped image drawn at `opacity`.
  void emit_raster_chunk(std::vector<Layer> layers, double opacity) {
    Document scratch(document.width(), document.height(), PixelFormat::rgba8());
    scratch.metadata().patterns = document.metadata().patterns;
    for (auto& layer : layers) {
      scratch.add_layer(std::move(layer));
    }
    const auto pixels = flatten_document_rgba8(scratch);
    const auto bounds = vector_export::opaque_bounds(pixels);
    if (!bounds.has_value()) {
      return;  // nothing visible, nothing to embed
    }
    const QImage image = qimage_from_pixel_buffer(vector_export::crop_pixels(pixels, *bounds));
    painter.save();
    painter.setOpacity(opacity);
    painter.drawImage(QRectF(bounds->x, bounds->y, bounds->width, bounds->height), image);
    painter.restore();
  }

  // One layer (or clip run) rasterized on its own: bake with Normal/full opacity, then
  // reapply the opacity on the image so it still composites correctly against what's
  // below. `why` names the feature that forced the bake (empty for plain pixel layers,
  // whose image form loses nothing).
  void emit_raster_unit(std::span<const Layer> run, const std::string& why) {
    const Layer& base = run.front();
    std::vector<Layer> copies;
    copies.reserve(run.size());
    for (const auto& member : run) {
      copies.push_back(member);
    }
    copies.front().set_visible(true);
    copies.front().set_opacity(1.0F);
    copies.front().set_blend_mode(BlendMode::Normal);
    if (!why.empty()) {
      notice("'" + base.name() + "' was flattened to an image for PDF export (" + why + ")");
    }
    emit_raster_chunk(std::move(copies), base.opacity());
  }

  void emit_group(const Layer& group) {
    if (!group_representable(group)) {
      emit_raster_unit(std::span<const Layer>(&group, 1),
                       !opacity_is_full(group.opacity()) ? "group opacity" : "group masks or styles");
      return;
    }
    painter.save();
    apply_vector_mask(group);
    emit_siblings(group.children());
    painter.restore();
  }

  // Hidden layers have no PDF form: drop them (and the clipped run a hidden base owns)
  // before the walk so they neither paint nor count as barriers.
  static std::vector<Layer> visible_siblings(const std::vector<Layer>& siblings) {
    std::vector<Layer> visible;
    visible.reserve(siblings.size());
    bool skipping_clipped_run = false;
    for (const auto& layer : siblings) {
      if (layer.clipped()) {
        if (!skipping_clipped_run && layer.visible()) {
          visible.push_back(layer);
        }
        continue;
      }
      skipping_clipped_run = !layer.visible();
      if (layer.visible()) {
        visible.push_back(layer);
      }
    }
    return visible;
  }

  void emit_siblings(const std::vector<Layer>& all_siblings) {
    const bool all_visible = std::all_of(all_siblings.begin(), all_siblings.end(),
                                         [](const Layer& layer) { return layer.visible(); });
    const std::vector<Layer> filtered = all_visible ? std::vector<Layer>{} : visible_siblings(all_siblings);
    const std::vector<Layer>& siblings = all_visible ? all_siblings : filtered;

    const auto units = vector_export::build_units(siblings);
    std::size_t barrier_end = 0;
    for (std::size_t i = 0; i < units.size(); ++i) {
      if (vector_export::unit_is_barrier(siblings, units[i], blend_expressible)) {
        barrier_end = i + 1;
      }
    }
    std::size_t resume_at = 0;
    if (barrier_end > 0) {
      std::vector<Layer> chunk(siblings.begin(),
                               siblings.begin() + static_cast<std::ptrdiff_t>(units[barrier_end - 1].end));
      std::string names;
      for (const auto& layer : chunk) {
        if (!names.empty()) {
          names += ", ";
        }
        names += layer.name();
      }
      notice("Merged into one flattened image for PDF export (adjustment layers or blend modes): " + names);
      emit_raster_chunk(std::move(chunk), 1.0);
      resume_at = barrier_end;
    }
    for (std::size_t i = resume_at; i < units.size(); ++i) {
      const auto& unit = units[i];
      const Layer& base = siblings[unit.begin];
      if (unit.end - unit.begin > 1) {
        emit_raster_unit(std::span<const Layer>(siblings.data() + unit.begin, unit.end - unit.begin),
                         "clipping mask");
        continue;
      }
      if (base.kind() == LayerKind::Group) {
        emit_group(base);
        continue;
      }
      if (layer_is_vector_shape(base)) {
        if (vector_representable(base)) {
          emit_vector_layer(base);
        } else {
          emit_raster_unit(std::span<const Layer>(&base, 1), "shape features PDF export cannot keep as a path");
        }
        continue;
      }
      if (layer_is_text(base)) {
        if (text_representable(base) && emit_text_layer(base)) {
          continue;
        }
        emit_raster_unit(std::span<const Layer>(&base, 1), "text features PDF export cannot keep as text");
        continue;
      }
      // Pixel, smart-object, and everything else: an image is their natural PDF form.
      // Styles and masks bake in silently; the pixels were never anything else.
      emit_raster_unit(std::span<const Layer>(&base, 1), std::string());
    }
  }

  void run() { emit_siblings(document.layers()); }
};

}  // namespace

void write_editable_pdf_document_file(const Document& document, const QString& path, const PdfExportOptions& options,
                                      std::vector<std::string>* notices) {
  if (document.width() <= 0 || document.height() <= 0) {
    throw std::runtime_error("The document could not be rendered for PDF export.");
  }
  QPdfWriter writer(path);
  configure_document_page(writer, document);

  QPainter painter;
  if (!painter.begin(&writer)) {
    throw std::runtime_error("The PDF file could not be opened for writing.");
  }
  painter.setRenderHint(QPainter::LosslessImageRendering, options.lossless);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.setRenderHint(QPainter::Antialiasing, true);
  // Window = the document's pixel grid, mapped onto the whole page: every layer draws in
  // document coordinates and the page (already sized pixels / PPI) takes care of the
  // physical size, including the 14400 pt cap shrink.
  painter.setWindow(QRect(0, 0, document.width(), document.height()));

  Writer layer_writer{document, notices, painter};
  layer_writer.run();
  painter.end();

  // Qt wrote every glyph as its own Tj; fold each line of text back into one TJ run so
  // importers see words, not letters (formats/pdf_text_merge.hpp). A file that does not
  // match the pass's expectations is left as Qt wrote it.
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return;
  }
  const QByteArray written = file.readAll();
  file.close();
  std::vector<std::uint8_t> bytes(written.begin(), written.end());
  if (!pdf::merge_glyph_runs_in_qt_pdf(bytes)) {
    return;
  }
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    throw std::runtime_error("The PDF file could not be rewritten after export.");
  }
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
  file.close();
}

}  // namespace patchy::ui::pdf_detail
