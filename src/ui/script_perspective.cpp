#include "ui/script_perspective.hpp"

#include "core/perspective_planes.hpp"
#include "ui/document_session.hpp"
#include "ui/main_window.hpp"
#include "ui/script_api.hpp"
#include "ui/script_engine.hpp"

#include <QColor>
#include <QJSEngine>
#include <QJSValueIterator>

#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

std::optional<PlanePoint> parse_point(const QJSValue& value) {
  if (value.isArray() && value.property(QStringLiteral("length")).toInt() == 2) {
    const auto x = value.property(0);
    const auto y = value.property(1);
    if (x.isNumber() && y.isNumber() && std::isfinite(x.toNumber()) && std::isfinite(y.toNumber())) {
      return PlanePoint{x.toNumber(), y.toNumber()};
    }
    return std::nullopt;
  }
  if (value.isObject() && value.property(QStringLiteral("x")).isNumber() && value.property(QStringLiteral("y")).isNumber()) {
    return PlanePoint{value.property(QStringLiteral("x")).toNumber(), value.property(QStringLiteral("y")).toNumber()};
  }
  return std::nullopt;
}

std::optional<PlaneQuad> parse_corners(QJSValue value) {
  if (value.isObject() && !value.isArray() && value.hasProperty(QStringLiteral("corners"))) {
    value = value.property(QStringLiteral("corners"));
  }
  if (!value.isArray() || value.property(QStringLiteral("length")).toInt() != 4) {
    return std::nullopt;
  }
  PlaneQuad quad{};
  for (quint32 i = 0; i < 4; ++i) {
    const auto point = parse_point(value.property(i));
    if (!point.has_value()) {
      return std::nullopt;
    }
    quad[i] = *point;
  }
  return quad;
}

QString problem_token(PlaneQuadProblem problem) {
  switch (problem) {
    case PlaneQuadProblem::None:
      return {};
    case PlaneQuadProblem::TooSmall:
      return QStringLiteral("tooSmall");
    case PlaneQuadProblem::Crossing:
      return QStringLiteral("crossing");
    case PlaneQuadProblem::Concave:
      return QStringLiteral("concave");
    case PlaneQuadProblem::BehindView:
      return QStringLiteral("behindView");
  }
  return {};
}

QJSValue point_value(QJSEngine& engine, PlanePoint point) {
  auto array = engine.newArray(2);
  array.setProperty(0, point.x);
  array.setProperty(1, point.y);
  return array;
}

}  // namespace

ScriptPerspectiveObject::ScriptPerspectiveObject(ScriptEngineHost& host) : host_(host) {}

QString ScriptPerspectiveObject::checkPlane(const QJSValue& corners) {
  const ScriptApiCall api_call(host_);
  const auto quad = parse_corners(corners);
  if (!quad.has_value()) {
    host_.throw_js_error(ScriptEngineHost::tr("checkPlane: pass four [x, y] corners."));
    return {};
  }
  return problem_token(check_plane_quad(*quad));
}

QJSValue ScriptPerspectiveObject::getPlanes(const QJSValue& document) {
  const ScriptApiCall api_call(host_);
  const auto* wrapper = qobject_cast<ScriptDocumentObject*>(document.toQObject());
  const auto planes = wrapper != nullptr ? host_.perspective_planes(wrapper->session_id()) : std::nullopt;
  if (!planes.has_value()) {
    host_.throw_js_error(ScriptEngineHost::tr("getPlanes needs an open document."));
    return QJSValue();
  }
  auto& engine = *host_.engine();
  auto result = engine.newArray(static_cast<quint32>(planes->size()));
  for (std::size_t i = 0; i < planes->size(); ++i) {
    const auto& plane = (*planes)[i];
    auto entry = engine.newObject();
    auto corners = engine.newArray(4);
    for (quint32 k = 0; k < 4; ++k) {
      corners.setProperty(k, point_value(engine, plane.corners[k]));
    }
    entry.setProperty(QStringLiteral("corners"), corners);
    entry.setProperty(QStringLiteral("grid"), plane.grid_size);
    result.setProperty(static_cast<quint32>(i), entry);
  }
  return result;
}

void ScriptPerspectiveObject::setPlanes(const QJSValue& document, const QJSValue& planes) {
  const ScriptApiCall api_call(host_);
  const auto* wrapper = qobject_cast<ScriptDocumentObject*>(document.toQObject());
  if (wrapper == nullptr || !planes.isArray()) {
    host_.throw_js_error(ScriptEngineHost::tr("setPlanes: pass a document and an array of planes."));
    return;
  }
  std::vector<PerspectivePlane> parsed;
  const auto count = planes.property(QStringLiteral("length")).toInt();
  for (int i = 0; i < count; ++i) {
    const auto entry = planes.property(static_cast<quint32>(i));
    const auto quad = parse_corners(entry);
    if (!quad.has_value()) {
      host_.throw_js_error(ScriptEngineHost::tr("setPlanes: plane %1 needs four [x, y] corners.").arg(i));
      return;
    }
    PerspectivePlane plane;
    plane.id = i + 1;
    plane.corners = *quad;
    if (entry.isObject() && entry.property(QStringLiteral("grid")).isNumber()) {
      plane.grid_size = std::max(0.0, entry.property(QStringLiteral("grid")).toNumber());
    }
    parsed.push_back(plane);
  }
  if (!host_.set_perspective_planes(wrapper->session_id(), std::move(parsed))) {
    host_.throw_js_error(ScriptEngineHost::tr("setPlanes needs an open document."));
  }
}

QJSValue ScriptPerspectiveObject::paste(const QJSValue& layer, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  const auto* target = qobject_cast<ScriptLayerObject*>(layer.toQObject());
  if (target == nullptr || !options.isObject()) {
    host_.throw_js_error(ScriptEngineHost::tr("perspective.paste: pass a layer and {plane, source}."));
    return QJSValue();
  }
  const auto planes = host_.perspective_planes(target->session_id()).value_or(std::vector<PerspectivePlane>{});
  const auto resolve_plane = [&planes](const QJSValue& value) -> std::optional<PlaneQuad> {
    if (value.isNumber()) {
      const auto index = value.toInt();
      if (index >= 0 && index < static_cast<int>(planes.size())) {
        return planes[static_cast<std::size_t>(index)].corners;
      }
      return std::nullopt;
    }
    return parse_corners(value);
  };
  const auto quad = resolve_plane(options.property(QStringLiteral("plane")));
  const auto* source = qobject_cast<ScriptLayerObject*>(options.property(QStringLiteral("source")).toQObject());
  if (!quad.has_value() || source == nullptr || source->session_id() != target->session_id()) {
    host_.throw_js_error(ScriptEngineHost::tr(
        "perspective.paste: plane must be four [x, y] corners or a plane index, and source a layer of this document."));
    return QJSValue();
  }
  const auto placement_value = options.property(QStringLiteral("placement"));
  std::optional<PerspectivePlacement> placement;
  bool fit = false;
  if (placement_value.isString()) {
    const auto token = placement_value.toString();
    if (token == QLatin1String("fit")) {
      fit = true;
    } else if (token != QLatin1String("fill")) {
      host_.throw_js_error(ScriptEngineHost::tr("perspective.paste: placement is \"fill\", \"fit\" or {x, y, scale, rotation}."));
      return QJSValue();
    }
  } else if (placement_value.isObject()) {
    PerspectivePlacement custom;
    custom.center_x = placement_value.property(QStringLiteral("x")).toNumber();
    custom.center_y = placement_value.property(QStringLiteral("y")).toNumber();
    const auto scale = placement_value.property(QStringLiteral("scale"));
    custom.scale = scale.isNumber() ? scale.toNumber() : 1.0;
    custom.rotation_degrees = placement_value.property(QStringLiteral("rotation")).toNumber();
    if (!std::isfinite(custom.center_x) || !std::isfinite(custom.center_y) || !(custom.scale > 0.0) ||
        !std::isfinite(custom.rotation_degrees)) {
      host_.throw_js_error(ScriptEngineHost::tr("perspective.paste: placement needs finite x and y and a positive scale."));
      return QJSValue();
    }
    placement = custom;
  } else if (!placement_value.isUndefined()) {
    host_.throw_js_error(ScriptEngineHost::tr("perspective.paste: placement is \"fill\", \"fit\" or {x, y, scale, rotation}."));
    return QJSValue();
  }
  const auto target_value = options.property(QStringLiteral("target")).toString();
  const bool onto_layer = target_value == QLatin1String("layer");
  const auto image = host_.perspective_source_pixels(source->session_id(), source->layer_id());
  if (!image.has_value()) {
    host_.throw_js_error(ScriptEngineHost::tr("perspective.paste: the source must be an 8-bit or 16-bit pixel layer."));
    return QJSValue();
  }
  const auto written = host_.perspective_edit(
      target->session_id(), target->layer_id(), *quad, onto_layer,
      [&](PerspectiveCanvas& canvas, const PlaneGeometry& geometry, QString&) {
        Homography image_to_plane;
        if (placement.has_value()) {
          image_to_plane = placement_to_plane(*placement, image->width(), image->height());
        } else if (fit) {
          image_to_plane = placement_to_plane(fit_placement(geometry, image->width(), image->height()), image->width(),
                                              image->height());
        } else {
          image_to_plane = fill_plane_from_image(geometry, image->width(), image->height());
        }
        canvas.paste(geometry, *image, image_to_plane);
        return true;
      });
  return written.has_value() ? make_layer_value(host_, target->session_id(), *written) : QJSValue(QJSValue::NullValue);
}

QJSValue ScriptPerspectiveObject::placeLayer(const QJSValue& layer, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  const auto* target = qobject_cast<ScriptLayerObject*>(layer.toQObject());
  if (target == nullptr || !options.isObject()) {
    host_.throw_js_error(ScriptEngineHost::tr("perspective.placeLayer: pass a layer and {plane}."));
    return QJSValue();
  }
  const auto planes = host_.perspective_planes(target->session_id()).value_or(std::vector<PerspectivePlane>{});
  const auto plane_value = options.property(QStringLiteral("plane"));
  std::optional<PlaneQuad> quad;
  if (plane_value.isNumber()) {
    const auto index = plane_value.toInt();
    if (index >= 0 && index < static_cast<int>(planes.size())) {
      quad = planes[static_cast<std::size_t>(index)].corners;
    }
  } else {
    quad = parse_corners(plane_value);
  }
  if (!quad.has_value()) {
    host_.throw_js_error(ScriptEngineHost::tr("perspective.placeLayer: plane must be four [x, y] corners or a plane index."));
    return QJSValue();
  }
  const auto placement_value = options.property(QStringLiteral("placement"));
  bool fit = false;
  std::optional<PerspectivePlacement> custom;
  if (placement_value.isString() && placement_value.toString() == QLatin1String("fit")) {
    fit = true;
  } else if (placement_value.isObject()) {
    PerspectivePlacement placed;
    placed.center_x = placement_value.property(QStringLiteral("x")).toNumber();
    placed.center_y = placement_value.property(QStringLiteral("y")).toNumber();
    const auto scale = placement_value.property(QStringLiteral("scale"));
    placed.scale = scale.isNumber() ? scale.toNumber() : 1.0;
    placed.rotation_degrees = placement_value.property(QStringLiteral("rotation")).toNumber();
    if (!std::isfinite(placed.center_x) || !std::isfinite(placed.center_y) || !(placed.scale > 0.0) ||
        !std::isfinite(placed.rotation_degrees)) {
      host_.throw_js_error(
          ScriptEngineHost::tr("perspective.placeLayer: placement needs finite x and y and a positive scale."));
      return QJSValue();
    }
    custom = placed;
  } else if (!placement_value.isUndefined() &&
             !(placement_value.isString() && placement_value.toString() == QLatin1String("fill"))) {
    host_.throw_js_error(
        ScriptEngineHost::tr("perspective.placeLayer: placement is \"fill\", \"fit\" or {x, y, scale, rotation}."));
    return QJSValue();
  }
  // Place Layer replaces the layer unless a new layer is asked for.
  const bool onto_layer = options.property(QStringLiteral("target")).toString() != QLatin1String("newLayer");
  if (!host_.perspective_layer_can_place(target->session_id(), target->layer_id())) {
    host_.throw_js_error(ScriptEngineHost::tr("perspective.placeLayer needs an 8-bit layer with transparency."));
    return QJSValue();
  }
  const auto written = host_.perspective_edit(
      target->session_id(), target->layer_id(), *quad, onto_layer,
      [&](PerspectiveCanvas& canvas, const PlaneGeometry& geometry, QString& error) {
        auto lifted = canvas.lift(nullptr);
        if (!lifted.has_value()) {
          error = ScriptEngineHost::tr("perspective.placeLayer: the layer is empty.");
          return false;
        }
        const auto& content = lifted->first;
        const auto placement = custom.has_value() ? *custom
                               : fit             ? fit_placement(geometry, content.width(), content.height())
                                                 : fill_placement(geometry, content.width(), content.height());
        canvas.paste(geometry, content, placement_to_plane(placement, content.width(), content.height()));
        return true;
      });
  return written.has_value() ? make_layer_value(host_, target->session_id(), *written) : QJSValue(QJSValue::NullValue);
}

QJSValue ScriptPerspectiveObject::clone(const QJSValue& layer, const QJSValue& options) {
  return stroke(layer, options, true);
}

QJSValue ScriptPerspectiveObject::paint(const QJSValue& layer, const QJSValue& options) {
  return stroke(layer, options, false);
}

QJSValue ScriptPerspectiveObject::stroke(const QJSValue& layer, const QJSValue& options, bool clone) {
  const ScriptApiCall api_call(host_);
  const QString verb = clone ? QStringLiteral("perspective.clone") : QStringLiteral("perspective.paint");
  const auto* target = qobject_cast<ScriptLayerObject*>(layer.toQObject());
  if (target == nullptr || !options.isObject()) {
    host_.throw_js_error(ScriptEngineHost::tr("%1: pass a layer and an options object.").arg(verb));
    return QJSValue();
  }
  const auto planes = host_.perspective_planes(target->session_id()).value_or(std::vector<PerspectivePlane>{});
  const auto resolve_plane = [&planes](const QJSValue& value) -> std::optional<PlaneQuad> {
    if (value.isNumber()) {
      const auto index = value.toInt();
      if (index >= 0 && index < static_cast<int>(planes.size())) {
        return planes[static_cast<std::size_t>(index)].corners;
      }
      return std::nullopt;
    }
    return parse_corners(value);
  };
  const auto quad = resolve_plane(options.property(QStringLiteral("plane")));
  if (!quad.has_value()) {
    host_.throw_js_error(ScriptEngineHost::tr("%1: plane must be four [x, y] corners or a plane index.").arg(verb));
    return QJSValue();
  }
  std::vector<PlanePoint> points;
  const auto points_value = options.property(QStringLiteral("points"));
  const auto count = points_value.isArray() ? points_value.property(QStringLiteral("length")).toInt() : 0;
  for (int i = 0; i < count; ++i) {
    const auto point = parse_point(points_value.property(static_cast<quint32>(i)));
    if (!point.has_value()) {
      host_.throw_js_error(ScriptEngineHost::tr("%1: points must be [x, y] pairs.").arg(verb));
      return QJSValue();
    }
    points.push_back(*point);
  }
  if (points.empty()) {
    host_.throw_js_error(ScriptEngineHost::tr("%1: points needs at least one [x, y] point.").arg(verb));
    return QJSValue();
  }
  PerspectiveBrush brush;
  const auto number = [&options](const char* key, double fallback) {
    const auto value = options.property(QLatin1String(key));
    return value.isNumber() && std::isfinite(value.toNumber()) ? value.toNumber() : fallback;
  };
  brush.size = std::clamp(number("size", 60.0), 1.0, 5000.0);
  brush.hardness = static_cast<int>(std::lround(std::clamp(number("hardness", 50.0), 0.0, 100.0)));
  brush.opacity = static_cast<int>(std::lround(std::clamp(number("opacity", 100.0), 1.0, 100.0)));
  std::array<std::uint8_t, 4> color{0, 0, 0, 255};
  std::optional<PlaneQuad> source_quad;
  std::optional<PlanePoint> from;
  if (clone) {
    source_quad = options.hasProperty(QStringLiteral("sourcePlane"))
                      ? resolve_plane(options.property(QStringLiteral("sourcePlane")))
                      : quad;
    from = parse_point(options.property(QStringLiteral("from")));
    if (!source_quad.has_value() || !from.has_value()) {
      host_.throw_js_error(
          ScriptEngineHost::tr("perspective.clone: from must be an [x, y] point on sourcePlane (default: plane)."));
      return QJSValue();
    }
  } else {
    const auto color_value = options.property(QStringLiteral("color"));
    QColor parsed;
    if (color_value.isString()) {
      parsed = QColor(color_value.toString());
    } else if (color_value.isArray() && color_value.property(QStringLiteral("length")).toInt() >= 3) {
      parsed = QColor(std::clamp(color_value.property(0).toInt(), 0, 255), std::clamp(color_value.property(1).toInt(), 0, 255),
                      std::clamp(color_value.property(2).toInt(), 0, 255));
    } else if (color_value.isUndefined()) {
      parsed = QColor(Qt::black);
    }
    if (!parsed.isValid()) {
      host_.throw_js_error(ScriptEngineHost::tr("perspective.paint: color is \"#rrggbb\" or [r, g, b]."));
      return QJSValue();
    }
    color = {static_cast<std::uint8_t>(parsed.red()), static_cast<std::uint8_t>(parsed.green()),
             static_cast<std::uint8_t>(parsed.blue()), 255};
  }
  const bool onto_layer = options.property(QStringLiteral("target")).toString() == QLatin1String("layer");
  const auto written = host_.perspective_edit(
      target->session_id(), target->layer_id(), *quad, onto_layer,
      [&](PerspectiveCanvas& canvas, const PlaneGeometry& geometry, QString& error) {
        PerspectiveStrokeSetup setup;
        setup.kind = clone ? PerspectiveStrokeKind::Clone : PerspectiveStrokeKind::Paint;
        setup.plane = geometry;
        setup.brush = brush;
        setup.color = color;
        const auto first = geometry.to_plane.map(points.front());
        if (!first.has_value()) {
          error = ScriptEngineHost::tr("%1: the first point is past the plane's horizon.").arg(verb);
          return false;
        }
        if (clone) {
          const auto camera = perspective_camera_for({PerspectivePlane{1, *quad, 0.0, std::nullopt}}, canvas.width(),
                                                     canvas.height());
          const auto source_geometry = plane_geometry(*source_quad, camera);
          const auto source_point = source_geometry.has_value() ? source_geometry->to_plane.map(*from) : std::nullopt;
          if (!source_geometry.has_value() || !source_point.has_value()) {
            error = ScriptEngineHost::tr("perspective.clone: sourcePlane cannot be used.");
            return false;
          }
          setup.source_plane = *source_geometry;
          setup.source_offset = {source_point->x - first->x, source_point->y - first->y};
        }
        canvas.begin_stroke(setup);
        for (const auto& point : points) {
          const auto mapped = geometry.to_plane.map(point);
          if (mapped.has_value()) {
            canvas.stroke_to(*mapped);
          }
        }
        canvas.end_stroke();
        return true;
      });
  return written.has_value() ? make_layer_value(host_, target->session_id(), *written) : QJSValue(QJSValue::NullValue);
}

// ---------------------------------------------------------------------------
// ScriptEngineHost services (friend access to MainWindow stays inside host members).

std::optional<std::vector<PerspectivePlane>> ScriptEngineHost::perspective_planes(std::int64_t session_id) {
  const auto* session = window_.session_with_id(session_id);
  if (session == nullptr) {
    return std::nullopt;
  }
  return session->perspective_planes;
}

bool ScriptEngineHost::set_perspective_planes(std::int64_t session_id, std::vector<PerspectivePlane> planes) {
  auto* session = window_.session_with_id(session_id);
  if (session == nullptr) {
    return false;
  }
  session->perspective_planes = std::move(planes);
  return true;
}

bool ScriptEngineHost::perspective_layer_can_place(std::int64_t session_id, LayerId layer_id) {
  auto* session = window_.session_with_id(session_id);
  if (session == nullptr || window_.active_session() != session) {
    return false;
  }
  const auto* layer = std::as_const(session->document).find_layer(layer_id);
  const auto input = window_.perspective_layer_input(layer_id);
  return layer != nullptr && input.has_value() && input->can_write_onto_layer &&
         std::as_const(*layer).pixels().format() == PixelFormat::rgba8();
}

std::optional<PixelBuffer> ScriptEngineHost::perspective_source_pixels(std::int64_t session_id, LayerId layer_id) {
  auto* session = window_.session_with_id(session_id);
  if (session == nullptr || window_.active_session() != session) {
    return std::nullopt;
  }
  const auto input = window_.perspective_layer_input(layer_id);
  if (!input.has_value()) {
    return std::nullopt;
  }
  return input->pixels;
}

std::optional<LayerId> ScriptEngineHost::perspective_edit(
    std::int64_t session_id, LayerId layer_id, const PlaneQuad& plane, bool onto_layer,
    const std::function<bool(PerspectiveCanvas&, const PlaneGeometry&, QString&)>& edit) {
  pump_progress_indicator();
  auto* session = window_.session_with_id(session_id);
  if (session == nullptr || session->canvas == nullptr) {
    throw_js_error(tr("The document is no longer open."));
    return std::nullopt;
  }
  if (window_.active_session() != session) {
    throw_js_error(tr("patchy.perspective edits the active document (call doc.activate() first)."));
    return std::nullopt;
  }
  if (const auto problem = check_plane_quad(plane); problem != PlaneQuadProblem::None) {
    throw_js_error(tr("patchy.perspective: the plane's corners cannot be a plane (%1).").arg(problem_token(problem)));
    return std::nullopt;
  }
  if (window_.layer_id_locks_image_pixels(layer_id)) {
    throw_js_error(tr("Layer pixels are locked."));
    return std::nullopt;
  }
  const auto input = window_.perspective_layer_input(layer_id);
  if (!input.has_value()) {
    throw_js_error(tr("patchy.perspective needs an editable 8-bit or 16-bit RGB pixel layer."));
    return std::nullopt;
  }
  const auto& document = std::as_const(session->document);
  PerspectiveCanvas canvas(input->pixels, input->bounds, document.width(), document.height());
  const auto camera =
      perspective_camera_for({PerspectivePlane{1, plane, 0.0, std::nullopt}}, document.width(), document.height());
  const auto geometry = plane_geometry(plane, camera);
  if (!geometry.has_value()) {
    throw_js_error(tr("patchy.perspective: the plane cannot be used."));
    return std::nullopt;
  }
  QString error;
  if (!edit(canvas, *geometry, error)) {
    throw_js_error(error);
    return std::nullopt;
  }
  auto edits = canvas.cropped_edits();
  const bool lifted = canvas.original_changed();
  if (!edits.has_value() && !lifted) {
    return std::nullopt;
  }
  if (!prepare_mutation(session_id)) {
    return std::nullopt;
  }
  session = window_.session_with_id(session_id);
  if (session == nullptr || window_.active_session() != session) {
    throw_js_error(tr("The document is no longer open."));
    return std::nullopt;
  }
  const auto written = window_.apply_perspective_edits(layer_id, std::move(edits), lifted ? &canvas.original() : nullptr,
                                                       onto_layer, tr("Perspective Planes"), /*record_history=*/false);
  if (written.has_value()) {
    if (*written != layer_id) {
      note_structure_changed(session_id);
    }
    // A lift changes the layer anywhere on the canvas, so refresh all of it.
    note_pixels_changed(session_id, QRect());
  }
  return written;
}

}  // namespace patchy::ui
